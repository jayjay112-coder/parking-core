// ============================================
// ESP32 PARKING SYSTEM (NETWORKED)
// 6 IR SENSORS | 6x 74HC595 TRAFFIC LIGHTS
// 2x RC522 RFID | PCA9685 + 2x SERVO
// ============================================

#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <WiFi.h>
#include <HTTPClient.h>

// ============================================
// NETWORK CONFIGURATION
// ============================================

const char* WIFI_SSID = "-----";        // CHANGE HERE: ----------> Change to your WiFi name
const char* WIFI_PASS = "-----";    // CHANGE:HERE ---------> Change to your WiFi password
const String SERVER_IP = "----";         // CHANGE HERE: --------> Change to your PC's IPv4 Address
const String SERVER_URL = "http://" + SERVER_IP + ":8000/api";
const String DEVICE_KEY = "change-me-device";    // Matches the backend default

// ============================================
// 74HC595 TRAFFIC LIGHT PINS
// ============================================
const int DATA_PIN  = 13;
const int CLOCK_PIN = 12;
const int LATCH_PIN = 14;
const int NUM_LIGHTS = 6;

/// ============================================
// IR SENSOR PINS & STATE
// ============================================
const int IR1 = 34;
const int IR2 = 35;
const int IR3 = 32;
const int IR4 = 33;
const int IR5 = 25;
const int IR6 = 26;

// NEW: 3-Second Delay Variables to prevent false triggers
unsigned long slotTimers[6] = {0, 0, 0, 0, 0, 0};
bool confirmedSlots[6] = {false, false, false, false, false, false};
const unsigned long DEBOUNCE_DELAY = 3000; // 3000 milliseconds = 3 seconds
// ============================================
// RFID PINS & OBJECTS
// ============================================
const int SS_PIN_1 = 27;
const int SS_PIN_2 = 16;
const int SCK_PIN  = 18;
const int MISO_PIN = 19;
const int MOSI_PIN = 23;
const int RST_PIN  = 17;

MFRC522 rfid1(SS_PIN_1, RST_PIN);
MFRC522 rfid2(SS_PIN_2, RST_PIN);

// ============================================
// PCA9685 SERVO SETUP
// ============================================
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);
const int SERVO_1 = 0; // Entry
const int SERVO_2 = 1; // Exit
const int SERVO_CLOSED = 0;
const int SERVO_OPEN   = 90;
const unsigned long SERVO_OPEN_TIME = 3000;

bool servo1Open = false;
bool servo2Open = false;
unsigned long servo1StartTime = 0;
unsigned long servo2StartTime = 0;

// ============================================
// TRAFFIC LIGHT STATES
// ============================================
const byte GREEN = 0b00000001;
const byte RED   = 0b00000010;
const byte OFF   = 0b00000000;

// ============================================
// HTTP POST HELPER FUNCTION
// ============================================
String sendPostRequest(String endpoint, String payload) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(SERVER_URL + endpoint);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Device-Key", DEVICE_KEY);
    
    int httpResponseCode = http.POST(payload);
    String response = "{}";
    
    if (httpResponseCode > 0) {
      response = http.getString();
      Serial.println("Server Response: " + response);
    } else {
      Serial.print("HTTP Error: ");
      Serial.println(httpResponseCode);
    }
    http.end();
    return response;
  }
  Serial.println("WiFi Disconnected!");
  return "{}";
}

// ============================================
// HARDWARE FUNCTIONS
// ============================================
void setServoAngle(int channel, int angle) {
  int pulse = map(angle, 0, 180, 150, 600);
  pwm.setPWM(channel, 0, pulse);
}

void openServo1() {
  setServoAngle(SERVO_1, SERVO_OPEN);
  servo1Open = true;
  servo1StartTime = millis();
  Serial.println("GATE #1 (ENTRY) -> OPEN");
}

void openServo2() {
  setServoAngle(SERVO_2, SERVO_OPEN);
  servo2Open = true;
  servo2StartTime = millis();
  Serial.println("GATE #2 (EXIT) -> OPEN");
}

void updateServos() {
  if (servo1Open && (millis() - servo1StartTime >= SERVO_OPEN_TIME)) {
    setServoAngle(SERVO_1, SERVO_CLOSED);
    servo1Open = false;
    Serial.println("GATE #1 -> CLOSED");
  }
  if (servo2Open && (millis() - servo2StartTime >= SERVO_OPEN_TIME)) {
    setServoAngle(SERVO_2, SERVO_CLOSED);
    servo2Open = false;
    Serial.println("GATE #2 -> CLOSED");
  }
}

void updateLights(byte states[]) {
  digitalWrite(LATCH_PIN, LOW);
  for (int i = NUM_LIGHTS - 1; i >= 0; i--) {
    shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, states[i]);
  }
  digitalWrite(LATCH_PIN, HIGH);
}

// Convert RFID UID to formatted String (e.g., AA:BB:CC:DD)
String getUIDString(MFRC522 &rfid) {
  String uidStr = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uidStr += "0";
    uidStr += String(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) uidStr += ":";
  }
  uidStr.toUpperCase();
  return uidStr;
}

// ============================================
// RFID #1 (ENTRY GATE)
// ============================================
bool checkRFID1() {
  if (!rfid1.PICC_IsNewCardPresent() || !rfid1.PICC_ReadCardSerial()) return false;

  String uid = getUIDString(rfid1);
  Serial.println("\nENTRY SCAN: " + uid);

  // Send request to server
  String payload = "{\"reader\":\"entry\",\"uid\":\"" + uid + "\"}";
  String response = sendPostRequest("/device/scan", payload);

  // Check if server authorized entry
  if (response.indexOf("\"action\":\"open\"") != -1 || response.indexOf("\"action\": \"open\"") != -1) {
    openServo1();
  } else {
    Serial.println("ENTRY DENIED BY SERVER (Full or Already Inside)");
  }

  rfid1.PICC_HaltA();
  rfid1.PCD_StopCrypto1();
  return true;
}

// ============================================
// RFID #2 (EXIT GATE)
// ============================================
bool checkRFID2() {
  if (!rfid2.PICC_IsNewCardPresent() || !rfid2.PICC_ReadCardSerial()) return false;

  String uid = getUIDString(rfid2);
  Serial.println("\nEXIT SCAN: " + uid);

  // Send request to server
  String payload = "{\"reader\":\"exit\",\"uid\":\"" + uid + "\"}";
  String response = sendPostRequest("/device/scan", payload);

  // Check if server authorized exit (Grace period or Paid)
  if (response.indexOf("\"action\":\"open\"") != -1 || response.indexOf("\"action\": \"open\"") != -1) {
    openServo2();
  } else {
    Serial.println("EXIT DENIED BY SERVER (Unpaid)");
  }

  rfid2.PICC_HaltA();
  rfid2.PCD_StopCrypto1();
  return true;
}

// ============================================
// SETUP
// ============================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Hardware Setup
  pinMode(DATA_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(LATCH_PIN, OUTPUT);
  
  pinMode(IR1, INPUT); pinMode(IR2, INPUT); pinMode(IR3, INPUT);
  pinMode(IR4, INPUT); pinMode(IR5, INPUT); pinMode(IR6, INPUT);

  byte startupLights[NUM_LIGHTS] = {OFF, OFF, OFF, OFF, OFF, OFF};
  updateLights(startupLights);

  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN);
  rfid1.PCD_Init(); delay(100);
  rfid2.PCD_Init(); delay(100);

  Wire.begin(21, 22);
  pwm.begin();
  pwm.setPWMFreq(50);
  delay(100);

  setServoAngle(SERVO_1, SERVO_CLOSED);
  setServoAngle(SERVO_2, SERVO_CLOSED);

  // Connect to Wi-Fi
  Serial.println("\nConnecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to Wi-Fi!");
  Serial.print("ESP32 IP Address: ");
  Serial.println(WiFi.localIP());

  Serial.println("\n========================================");
  Serial.println("    NETWORKED PARKING SYSTEM READY");
  Serial.println("========================================");
}

// ============================================
// MAIN LOOP
// ============================================
void loop() {
  // 1. READ RAW IR SENSORS (LOW = Occupied, HIGH = Empty)
  bool instantReading[6] = {
    digitalRead(IR1) == LOW,
    digitalRead(IR2) == LOW,
    digitalRead(IR3) == LOW,
    digitalRead(IR4) == LOW,
    digitalRead(IR5) == LOW,
    digitalRead(IR6) == LOW
  };

  bool slotsChanged = false;

  // 2. APPLY THE 3-SECOND RULE (Debounce Logic)
  for (int i = 0; i < 6; i++) {
    // If the sensor is reading something different than our confirmed state...
    if (instantReading[i] != confirmedSlots[i]) {
      
      // Start the timer if it hasn't started yet
      if (slotTimers[i] == 0) {
        slotTimers[i] = millis();
      } 
      // If the sensor has held this new state for 3 seconds continuously...
      else if (millis() - slotTimers[i] > DEBOUNCE_DELAY) {
        confirmedSlots[i] = instantReading[i]; // Make it official
        slotsChanged = true;                   // Trigger a backend update
        slotTimers[i] = 0;                     // Reset the timer
      }
    } 
    else {
      // If the reading flickers back, reset the timer (ignore false alarms)
      slotTimers[i] = 0; 
    }
  }

  // 3. UPDATE LOCAL TRAFFIC LIGHTS (Only uses confirmed slots)
  byte lights[NUM_LIGHTS];
  for (int i = 0; i < NUM_LIGHTS; i++) {
    lights[i] = confirmedSlots[i] ? RED : GREEN;
  }
  updateLights(lights);

  // 4. SEND TO SERVER ONLY IF A CAR ACTUALLY PARKED/LEFT
  if (slotsChanged) {
    String payload = "{\"slots\":[" + 
      String(confirmedSlots[0] ? "true" : "false") + "," +
      String(confirmedSlots[1] ? "true" : "false") + "," +
      String(confirmedSlots[2] ? "true" : "false") + "," +
      String(confirmedSlots[3] ? "true" : "false") + "," +
      String(confirmedSlots[4] ? "true" : "false") + "," +
      String(confirmedSlots[5] ? "true" : "false") + "]}";
      
    Serial.println("Slot change confirmed! Updating server...");
    sendPostRequest("/device/slots", payload);
  }

  // 5. CHECK RFIDs & SERVOS
  bool cardDetected = checkRFID1();
  if (!cardDetected) {
    checkRFID2();
  }
  updateServos();

  delay(50);
}