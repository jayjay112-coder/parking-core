// ============================================
// ESP32 + 2x RC522
// RFID COMMUNICATION DIAGNOSTIC
// ============================================

#include <SPI.h>
#include <MFRC522.h>

// ============================================
// PIN CONFIGURATION
// ============================================

// RFID #1
const int SS1  = 27;   // Physical Pin 11

// RFID #2
const int SS2  = 16;   // Physical Pin 27

// Shared SPI
const int SCK_PIN  = 18;   // Physical Pin 30
const int MISO_PIN = 19;   // Physical Pin 31
const int MOSI_PIN = 23;   // Physical Pin 37

// Shared RST
const int RST_PIN = 17;    // Physical Pin 28


MFRC522 rfid1(SS1, RST_PIN);
MFRC522 rfid2(SS2, RST_PIN);


void setup() {

  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println("========================================");
  Serial.println("       RC522 COMMUNICATION TEST");
  Serial.println("========================================");

  Serial.println();
  Serial.println("SPI PINOUT:");
  Serial.println("SCK  = GPIO18 / Physical Pin 30");
  Serial.println("MISO = GPIO19 / Physical Pin 31");
  Serial.println("MOSI = GPIO23 / Physical Pin 37");
  Serial.println("RST  = GPIO17 / Physical Pin 28");
  Serial.println();

  Serial.println("RFID #1 SS = GPIO27 / Physical Pin 11");
  Serial.println("RFID #2 SS = GPIO16 / Physical Pin 27");
  Serial.println();


  // Start SPI
  SPI.begin(
    SCK_PIN,
    MISO_PIN,
    MOSI_PIN
  );

  delay(100);


  // Initialize reader #1
  rfid1.PCD_Init();

  delay(100);


  // Initialize reader #2
  rfid2.PCD_Init();

  delay(100);


  // ==========================================
  // CHECK RFID #1
  // ==========================================

  Serial.println("----------------------------------------");
  Serial.println("RFID #1 VERSION:");

  byte version1 = rfid1.PCD_ReadRegister(
    MFRC522::VersionReg
  );

  Serial.print("Version = 0x");

  if (version1 < 0x10) {
    Serial.print("0");
  }

  Serial.println(version1, HEX);


  if (version1 == 0x00 || version1 == 0xFF) {
    Serial.println("ERROR: RFID #1 NOT DETECTED");
  }
  else {
    Serial.println("RFID #1 COMMUNICATION OK");
  }


  // ==========================================
  // CHECK RFID #2
  // ==========================================

  Serial.println();
  Serial.println("----------------------------------------");
  Serial.println("RFID #2 VERSION:");

  byte version2 = rfid2.PCD_ReadRegister(
    MFRC522::VersionReg
  );

  Serial.print("Version = 0x");

  if (version2 < 0x10) {
    Serial.print("0");
  }

  Serial.println(version2, HEX);


  if (version2 == 0x00 || version2 == 0xFF) {
    Serial.println("ERROR: RFID #2 NOT DETECTED");
  }
  else {
    Serial.println("RFID #2 COMMUNICATION OK");
  }


  // ==========================================
  // READY
  // ==========================================

  Serial.println();
  Serial.println("========================================");
  Serial.println("DIAGNOSTIC COMPLETE");
  Serial.println("========================================");
  Serial.println();

  Serial.println("Now place a compatible RFID card");
  Serial.println("near either reader.");
  Serial.println();
}


void loop() {

  // ==========================================
  // RFID #1 CARD TEST
  // ==========================================

  if (rfid1.PICC_IsNewCardPresent() &&
      rfid1.PICC_ReadCardSerial()) {

    Serial.println();
    Serial.println("******** RFID #1 DETECTED ********");

    Serial.print("UID: ");

    for (byte i = 0; i < rfid1.uid.size; i++) {

      if (rfid1.uid.uidByte[i] < 0x10) {
        Serial.print("0");
      }

      Serial.print(
        rfid1.uid.uidByte[i],
        HEX
      );

      if (i < rfid1.uid.size - 1) {
        Serial.print(":");
      }
    }

    Serial.println();
    Serial.println("*********************************");

    rfid1.PICC_HaltA();
    rfid1.PCD_StopCrypto1();

    delay(500);
  }


  // ==========================================
  // RFID #2 CARD TEST
  // ==========================================

  if (rfid2.PICC_IsNewCardPresent() &&
      rfid2.PICC_ReadCardSerial()) {

    Serial.println();
    Serial.println("******** RFID #2 DETECTED ********");

    Serial.print("UID: ");

    for (byte i = 0; i < rfid2.uid.size; i++) {

      if (rfid2.uid.uidByte[i] < 0x10) {
        Serial.print("0");
      }

      Serial.print(
        rfid2.uid.uidByte[i],
        HEX
      );

      if (i < rfid2.uid.size - 1) {
        Serial.print(":");
      }
    }

    Serial.println();
    Serial.println("*********************************");

    rfid2.PICC_HaltA();
    rfid2.PCD_StopCrypto1();

    delay(500);
  }

  delay(50);
}