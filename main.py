"""
Parking core backend (FastAPI + SQLite)
Dynamic ID generation, Grace Period, and Car-to-Slot Mapping applied.
"""
import hmac
import json
import math
import os
import sqlite3
import time
from contextlib import asynccontextmanager, contextmanager
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import List, Optional

from fastapi import Depends, FastAPI, Header, HTTPException, Query
from fastapi.responses import FileResponse
from pydantic import BaseModel

# ---------------------------------------------------------------- config
DB_PATH = os.getenv("DB_PATH", "parking.db")
DEVICE_KEY = os.getenv("DEVICE_KEY", "change-me-device")
NUM_SLOTS = 6
RATE_PER_HOUR = float(os.getenv("RATE_PER_HOUR", "20"))
TZ_HOURS = float(os.getenv("TZ_HOURS", "8"))
REQUIRE_PAYMENT = os.getenv("REQUIRE_PAYMENT", "1") == "1"
BLOCK_ENTRY_WHEN_FULL = os.getenv("BLOCK_ENTRY_WHEN_FULL", "0") == "1"
GRACE_PERIOD_SEC = float(os.getenv("GRACE_PERIOD_SEC", "20")) 

SCHEMA = """
CREATE TABLE IF NOT EXISTS slots(
  slot INTEGER PRIMARY KEY,
  occupied INTEGER NOT NULL DEFAULT 0,
  since REAL NOT NULL
);
CREATE TABLE IF NOT EXISTS slot_sessions(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  slot INTEGER NOT NULL,
  start_ts REAL NOT NULL,
  end_ts REAL,
  duration_s REAL
);
CREATE TABLE IF NOT EXISTS parking_sessions(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  tag_uid TEXT NOT NULL,
  ticket_no TEXT,
  slot_id INTEGER, -- Added to track which slot the car is in
  entry_ts REAL NOT NULL,
  exit_ts REAL,
  status TEXT NOT NULL DEFAULT 'PARKED',
  paid INTEGER NOT NULL DEFAULT 0,
  amount_paid REAL,
  paid_ts REAL
);
CREATE TABLE IF NOT EXISTS events(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  ts REAL NOT NULL,
  type TEXT NOT NULL,
  detail TEXT
);
"""

# ---------------------------------------------------------------- helpers
def now() -> float:
    return time.time()

def iso(ts: Optional[float]) -> Optional[str]:
    if ts is None:
        return None
    return datetime.fromtimestamp(ts, timezone(timedelta(hours=TZ_HOURS))).isoformat(timespec="seconds")

@contextmanager
def db():
    con = sqlite3.connect(DB_PATH, timeout=15)
    con.row_factory = sqlite3.Row
    try:
        yield con
        con.commit()
    except Exception:
        con.rollback()
        raise
    finally:
        con.close()

def init_db():
    with db() as con:
        con.execute("PRAGMA journal_mode=WAL")
        con.executescript(SCHEMA)
        for i in range(1, NUM_SLOTS + 1):
            con.execute("INSERT OR IGNORE INTO slots(slot, occupied, since) VALUES(?,0,?)", (i, now()))

def log_event(con, etype: str, detail: dict):
    con.execute("INSERT INTO events(ts,type,detail) VALUES(?,?,?)", (now(), etype, json.dumps(detail)))

def fee_for(duration_s: float) -> float:
    if duration_s <= GRACE_PERIOD_SEC:
        return 0.0 # Free if they just enter and leave
    return RATE_PER_HOUR * max(1, math.ceil(duration_s / 3600))

def session_dict(r) -> dict:
    end = r["exit_ts"] or now()
    dur = max(0.0, end - r["entry_ts"])
    fee = fee_for(dur)
    return {
        "session_id": r["id"],
        "ticket_no": r["ticket_no"],
        "tag_uid": r["tag_uid"],
        "slot_id": r["slot_id"],
        "status": r["status"],
        "entry_time": iso(r["entry_ts"]),
        "exit_time": iso(r["exit_ts"]),
        "duration_seconds": int(dur),
        "fee_due": fee,
        "paid": bool(r["paid"]) or (fee == 0), 
        "amount_paid": r["amount_paid"] or 0.0,
        "paid_time": iso(r["paid_ts"]),
    }

def need_device(x_device_key: str = Header(default="")):
    if not hmac.compare_digest(x_device_key.encode(), DEVICE_KEY.encode()):
        raise HTTPException(401, "bad device key")

# ---------------------------------------------------------------- app
@asynccontextmanager
async def lifespan(app: FastAPI):
    init_db()
    yield

app = FastAPI(title="Parking Core Backend", lifespan=lifespan)

class SlotsIn(BaseModel):
    slots: List[bool]

class ScanIn(BaseModel):
    reader: str
    uid: str

class PayIn(BaseModel):
    amount: Optional[float] = None

@app.get("/", include_in_schema=False)
def dashboard():
    return FileResponse(Path(__file__).with_name("dashboard.html"))

# ------------------------------------------------ ESP32 endpoints
@app.post("/api/device/slots", dependencies=[Depends(need_device)])
def device_slots(body: SlotsIn):
    if len(body.slots) != NUM_SLOTS:
        raise HTTPException(422, f"need exactly {NUM_SLOTS} values")
    t = now()
    changes = []
    with db() as con:
        for i, occ in enumerate(body.slots, start=1):
            row = con.execute("SELECT occupied FROM slots WHERE slot=?", (i,)).fetchone()
            if bool(row["occupied"]) == occ:
                continue
            
            con.execute("UPDATE slots SET occupied=?, since=? WHERE slot=?", (int(occ), t, i))
            
            if occ:
                con.execute("INSERT INTO slot_sessions(slot,start_ts) VALUES(?,?)", (i, t))
                
                # LINKING LOGIC: Find oldest unparked car inside the lot and assign this slot to it
                unassigned = con.execute("SELECT id, ticket_no FROM parking_sessions WHERE status='PARKED' AND slot_id IS NULL ORDER BY id ASC LIMIT 1").fetchone()
                if unassigned:
                    con.execute("UPDATE parking_sessions SET slot_id=? WHERE id=?", (i, unassigned["id"]))
                    log_event(con, "CAR_PARKED", {"slot": i, "ticket_no": unassigned["ticket_no"]})
            else:
                con.execute("UPDATE slot_sessions SET end_ts=?, duration_s=?-start_ts WHERE slot=? AND end_ts IS NULL", (t, t, i))
                
                # UNLINKING LOGIC: Remove the slot assignment (car is driving to exit)
                con.execute("UPDATE parking_sessions SET slot_id=NULL WHERE status='PARKED' AND slot_id=?", (i,))
                
            changes.append({"slot": i, "occupied": occ})
            log_event(con, "SLOT_OCCUPIED" if occ else "SLOT_FREED", {"slot": i})
    return {"ok": True, "changes": changes}

@app.post("/api/device/scan", dependencies=[Depends(need_device)])
def device_scan(body: ScanIn):
    uid = body.uid.strip().upper()
    reader = body.reader.strip().lower()
    t = now()

    def deny(reason, **extra):
        return {"action": "deny", "reason": reason, **extra}

    with db() as con:
        active = con.execute(
            "SELECT * FROM parking_sessions WHERE tag_uid=? AND status='PARKED' ORDER BY id DESC LIMIT 1",
            (uid,),
        ).fetchone()

        if reader == "entry":
            if active:
                log_event(con, "ENTRY_DENIED", {"uid": uid, "reason": "already_inside"})
                return deny("already_inside", session_id=active["id"])
            if BLOCK_ENTRY_WHEN_FULL:
                taken = con.execute("SELECT COUNT(*) c FROM slots WHERE occupied=1").fetchone()["c"]
                if taken >= NUM_SLOTS:
                    log_event(con, "ENTRY_DENIED", {"uid": uid, "reason": "full"})
                    return deny("full")
            
            cur = con.execute("INSERT INTO parking_sessions(tag_uid, entry_ts) VALUES(?,?)", (uid, t))
            sid = cur.lastrowid
            ticket_no = f"TKT-{sid:04d}"
            con.execute("UPDATE parking_sessions SET ticket_no=? WHERE id=?", (ticket_no, sid))
            
            log_event(con, "ENTRY", {"uid": uid, "ticket_no": ticket_no, "session_id": sid})
            return {"action": "open", "session_id": sid}

        # exit reader still strictly requires the RFID scan to open
        if not active:
            log_event(con, "EXIT_DENIED", {"uid": uid, "reason": "no_active_session"})
            return deny("no_active_session")
            
        due = fee_for(t - active["entry_ts"])
        
        if REQUIRE_PAYMENT and not active["paid"] and due > 0:
            log_event(con, "EXIT_DENIED", {"uid": uid, "reason": "unpaid", "session_id": active["id"], "fee_due": due})
            return deny("unpaid", session_id=active["id"], ticket_no=active["ticket_no"], fee_due=due)
            
        con.execute("UPDATE parking_sessions SET exit_ts=?, status='EXITED', slot_id=NULL WHERE id=?", (t, active["id"]))
        log_event(con, "EXIT", {"uid": uid, "ticket_no": active["ticket_no"], "session_id": active["id"]})
        return {"action": "open", "session_id": active["id"]}

# ------------------------------------------------ payment
@app.post("/api/sessions/{session_id}/pay")
def pay_session(session_id: int, body: Optional[PayIn] = None):
    with db() as con:
        row = con.execute("SELECT * FROM parking_sessions WHERE id=?", (session_id,)).fetchone()
        if not row:
            raise HTTPException(404, "session not found")
        if row["status"] == "EXITED":
            raise HTTPException(409, "session already closed")
        amount = body.amount if body and body.amount is not None else fee_for(now() - row["entry_ts"])
        con.execute(
            "UPDATE parking_sessions SET paid=1, amount_paid=?, paid_ts=? WHERE id=?",
            (amount, now(), session_id),
        )
        log_event(con, "PAYMENT", {"session_id": session_id, "ticket_no": row["ticket_no"], "amount": amount})
    return {"ok": True, "session_id": session_id, "amount_paid": amount}

# ------------------------------------------------ read endpoints
@app.get("/api/slots")
def get_slots():
    t = now()
    slots_data = []
    with db() as con:
        rows = con.execute("SELECT * FROM slots ORDER BY slot").fetchall()
        for r in rows:
            ticket_no = None
            if r["occupied"]:
                active_car = con.execute("SELECT ticket_no FROM parking_sessions WHERE status='PARKED' AND slot_id=?", (r["slot"],)).fetchone()
                if active_car:
                    ticket_no = active_car["ticket_no"]
            
            slots_data.append({
                "slot": r["slot"],
                "status": "OCCUPIED" if r["occupied"] else "EMPTY",
                "since": iso(r["since"]),
                "seconds_in_state": int(t - r["since"]),
                "ticket_no": ticket_no
            })
            
    return {
        "time": iso(t),
        "free": sum(1 for r in rows if not r["occupied"]),
        "slots": slots_data
    }

@app.get("/api/sessions")
def list_sessions(status: Optional[str] = None, limit: int = Query(50, le=500)):
    where, args = [], []
    if status:
        where.append("status=?")
        args.append(status.upper())
    sql = "SELECT * FROM parking_sessions"
    if where:
        sql += " WHERE " + " AND ".join(where)
    sql += " ORDER BY id DESC LIMIT ?"
    args.append(limit)
    with db() as con:
        rows = con.execute(sql, args).fetchall()
    return [session_dict(r) for r in rows]

@app.get("/api/events")
def get_events(limit: int = Query(100, le=1000)):
    with db() as con:
        rows = con.execute("SELECT * FROM events ORDER BY id DESC LIMIT ?", (limit,)).fetchall()
    return [
        {"id": r["id"], "time": iso(r["ts"]), "type": r["type"], "detail": json.loads(r["detail"] or "{}")}
        for r in rows
    ]