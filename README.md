# parking-core
# Smart Parking System (Core Backend & ESP32 Firmware)

A networked smart parking management system utilizing an ESP32, RFID access control, and IR sensor slot tracking. The system dynamically assigns ticket IDs to vehicles, tracks slot occupancy in real-time with debounce filtering, and calculates parking fees based on duration.

## 🛠️ Hardware Requirements
* **Microcontroller:** ESP32 (DOIT DevKit V1)
* **Access Control:** 2x RC522 RFID Scanners (Shared SPI, unique SS pins)
* **Gate Actuators:** 2x Servo Motors driven by a PCA9685 I2C Module
* **Slot Tracking:** 6x IR Obstacle Sensors
* **Visual Indicators:** 6x Traffic Light Modules driven by daisy-chained 74HC595 Shift Registers

## 💻 Backend Setup (Local Server)
The backend is built with Python (FastAPI) and uses a local SQLite database. It runs completely offline on your local network.

1. Install Python on your PC.
2. Open your terminal in the `parking-core` directory and install dependencies:
   ```bash
   pip install -r requirements.txt

1. Start the Uvicorn server:
   ```bash
    python -m uvicorn main:app --host 0.0.0.0 --port 8000
2. Open a web browser and navigate to http://localhost:8000/ to view the live dashboard.

🚗 System Execution Flow
Entry: Tap a unique RFID card on the Entry scanner. The server generates a dynamic Ticket ID (e.g., TKT-0001), the entry gate opens, and the dashboard marks the car as Driving (No Slot).

Parking: When a car backs into a slot, the IR sensor must be blocked for 3 full seconds (debounce logic). The system then automatically links TKT-0001 to that specific slot.

Leaving the Slot: The car leaves the slot, unblocking the IR sensor for 3 seconds. The car returns to Driving (No Slot) status.

Exit (Grace Period): If a car enters and exits within 20 seconds, the fee is ₱0. Tapping the exit scanner will automatically open the gate.

Exit (Paid): If parked beyond the 20-second grace period, standard rates apply. The exit scanner will deny access until the Mark Paid button is clicked on the web dashboard.


⚠️ Important Reminders & Troubleshooting
HTTP Error -1 (Connection Refused):

Check if your PC's IP address changed or dropped to an IPv6-only address. Reconnect your network to force an IPv4 assignment and update the SERVER_IP in the code.

Windows Defender Firewall will block Port 8000 by default. Temporarily disable the firewall or add an Inbound Rule allowing Port 8000.

If using a public/campus network, router AP Isolation might block device communication. Use a smartphone mobile hotspot instead.

Cars Merging / 1 RFID ID Detected:

Factory-included RFID tags often have identical, permanently locked UIDs. Do not use cloned cards to demonstrate multiple cars simultaneously.

Slots Stuck / False Sensor Triggers:

The ESP32 is programmed with a 3-second debounce timer. Swiping a hand past the sensor will not trigger it; an object must remain in place for 3 seconds to register a parking event.

Clearing the System:

To reset the system and clear all stuck sessions for a fresh demonstration, stop the Python server (Ctrl + C), delete the parking.db file, and restart the server.
