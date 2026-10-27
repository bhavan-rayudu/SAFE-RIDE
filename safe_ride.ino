#include <Wire.h>
#include <math.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>

// =====================================================
// SAFE-RIDE - ESP32 ACCIDENT ALERT SYSTEM
// =====================================================

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(
  SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET
);

// MPU6050
#define MPU_ADDR 0x68

// GPIO
#define BUTTON_PIN 23
#define BUZZER_PIN 4
#define LED_PIN 2

// GPS: NEO-6M
HardwareSerial GPS(2);
#define GPS_RX 16
#define GPS_TX 17
TinyGPSPlus gps;

// MODEM: A7670C
HardwareSerial Modem(1);
#define MODEM_RX 26
#define MODEM_TX 27

// Configure to match the modem's actual UART baud rate
#define MODEM_BAUD 115200
#define GPS_BAUD 9600

// DRIVER / VEHICLE DETAILS
const char DRIVER_NAME[] = "BABU PRASAD";
const char VEHICLE_NUMBER[] = "AP37ES1430";
const char VEHICLE_TYPE[] = "SUV CAR";

// Replace every placeholder with an authorized contact.
// Keep international format, e.g. +91 followed by 10 digits.
const char* EMERGENCY_NUMBERS[] = {
  "+91XXXXXXXXXX",
  "+91XXXXXXXXXX",
  "+91XXXXXXXXXX"
};

const int CONTACT_COUNT =
  sizeof(EMERGENCY_NUMBERS) / sizeof(EMERGENCY_NUMBERS[0]);

// Accident detection settings
// Prototype starting value only; validate on your actual vehicle.
const float IMPACT_THRESHOLD_G = 2.5;
const unsigned long ALERT_COOLDOWN_MS = 60000UL;
const unsigned long CANCEL_WINDOW_MS = 10000UL;
const unsigned long SOS_HOLD_MS = 2000UL;
const unsigned long CALL_DURATION_MS = 20000UL;

unsigned long lastAlertTime = 0;
bool modemReady = false;
bool mpuReady = false;
bool oledReady = false;
bool alertInProgress = false;

// =====================================================
// OLED
// =====================================================

void showMessage(
  const char* line1,
  const char* line2 = "",
  const char* line3 = ""
) {
  if (!oledReady) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 3);
  display.println(line1);

  display.setCursor(0, 24);
  display.println(line2);

  display.setCursor(0, 45);
  display.println(line3);

  display.display();
}

// =====================================================
// MODEM RESPONSE
// =====================================================

bool waitForResponse(
  const char* expected,
  unsigned long timeout
) {
  String response;
  response.reserve(350);

  unsigned long start = millis();

  while (millis() - start < timeout) {
    while (Modem.available()) {
      char c = Modem.read();
      Serial.write(c);
      response += c;

      if (response.indexOf(expected) >= 0) {
        return true;
      }

      if (response.length() > 300) {
        response.remove(0, 150);
      }
    }
    delay(1);
  }

  return false;
}

bool sendCommand(
  const char* command,
  const char* expected,
  unsigned long timeout
) {
  while (Modem.available()) Modem.read();

  Serial.print("CMD: ");
  Serial.println(command);

  Modem.println(command);

  return waitForResponse(expected, timeout);
}

// =====================================================
// MODEM INITIALIZATION
// =====================================================

bool initModem() {
  showMessage("Starting modem...", "A7670C");

  // Retry basic communication
  bool atOK = false;

  for (int i = 0; i < 5; i++) {
    if (sendCommand("AT", "OK", 2000)) {
      atOK = true;
      break;
    }
    delay(500);
  }

  if (!atOK) {
    Serial.println("MODEM NOT RESPONDING");
    return false;
  }

  sendCommand("ATE0", "OK", 2000);
  sendCommand("AT+CMEE=2", "OK", 2000);

  if (!sendCommand("AT+CMGF=1", "OK", 3000)) {
    return false;
  }

  // Configure SMS text character set
  if (!sendCommand("AT+CSCS=\"GSM\"", "OK", 3000)) {
    return false;
  }

  Serial.println("MODEM INITIALIZED");
  return true;
}

// =====================================================
// GPS
// =====================================================

void readGPS() {
  while (GPS.available()) {
    gps.encode(GPS.read());
  }
}

String getGPSLink() {
  showMessage("Getting GPS...", "Open sky helps");

  unsigned long start = millis();

  while (millis() - start < 20000UL) {
    readGPS();

    if (gps.location.isValid() &&
        gps.location.age() < 5000UL) {

      String link = "https://maps.google.com/?q=";
      link += String(gps.location.lat(), 6);
      link += ",";
      link += String(gps.location.lng(), 6);

      Serial.println("GPS LINK:");
      Serial.println(link);

      return link;
    }

    delay(10);
  }

  Serial.println("GPS FIX UNAVAILABLE");
  return "GPS location unavailable";
}

// =====================================================
// MPU6050
// =====================================================

bool initMPU() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);   // Power management
  Wire.write(0x00);   // Wake up
  if (Wire.endTransmission() != 0) return false;

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1C);   // Accelerometer configuration
  Wire.write(0x00);   // +/- 2g
  if (Wire.endTransmission() != 0) return false;

  delay(100);
  return true;
}

// Returns total acceleration magnitude in g.
float getAcceleration() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return -1.0;

  int count = Wire.requestFrom(MPU_ADDR, 6, true);
  if (count != 6) return -1.0;

  int16_t ax = (int16_t)(
    ((uint16_t)Wire.read() << 8) | Wire.read()
  );
  int16_t ay = (int16_t)(
    ((uint16_t)Wire.read() << 8) | Wire.read()
  );
  int16_t az = (int16_t)(
    ((uint16_t)Wire.read() << 8) | Wire.read()
  );

  float x = ax / 16384.0f;
  float y = ay / 16384.0f;
  float z = az / 16384.0f;

  return sqrtf(x*x + y*y + z*z);
}

// =====================================================
// BUTTON / COUNTDOWN
// =====================================================

// Accident countdown can be cancelled by pressing button.
// A long button hold while idle triggers manual SOS.

bool accidentCountdown() {
  unsigned long start = millis();

  while (millis() - start < CANCEL_WINDOW_MS) {
    unsigned long remaining =
      (CANCEL_WINDOW_MS - (millis() - start) + 999) / 1000;

    char line[24];
    snprintf(line, sizeof(line), "Sending in %lu sec", remaining);

    showMessage("ACCIDENT DETECTED!", line, "Press to cancel");

    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    tone(BUZZER_PIN, 2000, 100);

    if (digitalRead(BUTTON_PIN) == LOW) {
      delay(40);

      if (digitalRead(BUTTON_PIN) == LOW) {
        noTone(BUZZER_PIN);
        digitalWrite(LED_PIN, LOW);
        showMessage("Alert cancelled", "Check vehicle");
        delay(1200);

        // Wait until button is released
        while (digitalRead(BUTTON_PIN) == LOW) delay(10);
        return false;
      }
    }

    delay(100);
  }

  noTone(BUZZER_PIN);
  digitalWrite(LED_PIN, LOW);
  return true;
}

// =====================================================
// SEND SMS
// =====================================================

bool sendSMS(const char* number, const String& location) {
  if (!modemReady) return false;

  showMessage("Sending SMS...", number);

  if (!sendCommand("AT+CMGF=1", "OK", 3000)) {
    return false;
  }

  while (Modem.available()) Modem.read();

  Modem.print("AT+CMGS=\"");
  Modem.print(number);
  Modem.println("\"");

  if (!waitForResponse(">", 7000)) {
    Serial.println("SMS PROMPT FAILED");
    return false;
  }

  String message;
  message.reserve(300);

  message += "SAFE-RIDE ACCIDENT ALERT\n";
  message += "Driver: ";
  message += DRIVER_NAME;
  message += "\nVehicle No: ";
  message += VEHICLE_NUMBER;
  message += "\nVehicle Type: ";
  message += VEHICLE_TYPE;
  message += "\nLocation: ";
  message += location;

  Modem.print(message);
  Modem.write(26);  // Ctrl+Z

  // Wait for modem's final SMS result
  bool sent = waitForResponse("+CMGS:", 20000);

  if (sent) {
    // Normally +CMGS is followed by OK.
    waitForResponse("OK", 3000);
    Serial.print("SMS SUBMITTED TO MODEM: ");
    Serial.println(number);
    return true;
  }

  Serial.print("SMS FAILED: ");
  Serial.println(number);
  return false;
}

// =====================================================
// MAKE EMERGENCY CALL
// =====================================================

bool makeCall(const char* number) {
  if (!modemReady) return false;

  showMessage("Calling contact...", number);

  while (Modem.available()) Modem.read();

  Modem.print("ATD");
  Modem.print(number);
  Modem.println(";");

  bool accepted = waitForResponse("OK", 8000);

  if (!accepted) {
    Serial.println("CALL COMMAND NOT CONFIRMED");
    return false;
  }

  Serial.print("CALL STARTED: ");
  Serial.println(number);

  unsigned long start = millis();

  while (millis() - start < CALL_DURATION_MS) {
    readGPS();

    // Keep the call open for the configured duration.
    // This does not prove the other person answered.
    delay(20);
  }

  sendCommand("ATH", "OK", 5000);
  Serial.println("CALL ENDED");
  return true;
}

// =====================================================
// COMPLETE EMERGENCY SEQUENCE
// =====================================================

void sendEmergencyAlert(const char* reason) {
  if (alertInProgress) return;

  alertInProgress = true;
  lastAlertTime = millis();

  digitalWrite(LED_PIN, HIGH);
  tone(BUZZER_PIN, 1800);

  Serial.println();
  Serial.println("===== SAFE-RIDE ALERT =====");
  Serial.println(reason);

  showMessage("EMERGENCY ALERT", "Getting location...");

  // Get GPS fix; still send alert if GPS is unavailable.
  String location = getGPSLink();

  int smsSuccess = 0;

  for (int i = 0; i < CONTACT_COUNT; i++) {
    if (sendSMS(EMERGENCY_NUMBERS[i], location)) {
      smsSuccess++;
    }
  }

  // Call the first configured contact.
  makeCall(EMERGENCY_NUMBERS[0]);

  noTone(BUZZER_PIN);
  digitalWrite(LED_PIN, LOW);

  char result[24];
  snprintf(result, sizeof(result), "SMS sent: %d/%d",
           smsSuccess, CONTACT_COUNT);

  showMessage("Alert sequence done", result,
              "Check Serial Monitor");

  Serial.println("===== SEQUENCE FINISHED =====");

  delay(3000);
  alertInProgress = false;
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);
  noTone(BUZZER_PIN);

  Wire.begin(21, 22);
  Wire.setClock(100000);

  oledReady = display.begin(
    SSD1306_SWITCHCAPVCC, OLED_ADDR
  );

  if (oledReady) {
    showMessage("SAFE-RIDE", "Initializing...");
  }

  GPS.begin(
    GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX
  );

  Modem.begin(
    MODEM_BAUD, SERIAL_8N1, MODEM_RX, MODEM_TX
  );

  delay(1500);

  mpuReady = initMPU();
  modemReady = initModem();

  Serial.print("MPU6050: ");
  Serial.println(mpuReady ? "OK" : "FAILED");

  Serial.print("A7670C: ");
  Serial.println(modemReady ? "OK" : "FAILED");

  if (!oledReady) {
    Serial.println("OLED NOT FOUND");
  }

  if (oledReady && mpuReady && modemReady) {
    showMessage("SAFE-RIDE READY",
                "GPS: waiting for fix",
                "Hold button for SOS");
  } else {
    showMessage("CHECK HARDWARE",
                "See Serial Monitor",
                "Verify wiring/power");
  }
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop() {
  readGPS();

  // Manual SOS: hold button for 2 seconds.
  if (digitalRead(BUTTON_PIN) == LOW &&
      !alertInProgress) {

    unsigned long pressStart = millis();

    while (digitalRead(BUTTON_PIN) == LOW &&
           millis() - pressStart < SOS_HOLD_MS) {
      readGPS();
      delay(10);
    }

    bool longPress =
      digitalRead(BUTTON_PIN) == LOW &&
      millis() - pressStart >= SOS_HOLD_MS;

    while (digitalRead(BUTTON_PIN) == LOW) {
      readGPS();
      delay(10);
    }

    if (longPress) {
      sendEmergencyAlert("MANUAL SOS");
      return;
    }
  }

  // Read impact acceleration.
  if (mpuReady) {
    float accelerationG = getAcceleration();

    if (accelerationG >= IMPACT_THRESHOLD_G &&
        accelerationG < 20.0f &&
        (lastAlertTime == 0 ||
         millis() - lastAlertTime >= ALERT_COOLDOWN_MS)) {

      Serial.print("HIGH ACCELERATION: ");
      Serial.print(accelerationG);
      Serial.println(" g");

      if (accidentCountdown()) {
        sendEmergencyAlert("POSSIBLE ACCIDENT");
      }

      return;
    }
  }

  // Normal status
  static unsigned long lastDisplayUpdate = 0;

  if (millis() - lastDisplayUpdate >= 1000UL) {
    lastDisplayUpdate = millis();

    if (!mpuReady || !modemReady) {
      showMessage("SAFE-RIDE WARNING",
                  mpuReady ? "MPU OK" : "MPU FAILED",
                  modemReady ? "MODEM OK" : "MODEM FAILED");
    } else if (gps.location.isValid() &&
               gps.location.age() < 5000UL) {
      showMessage("SAFE-RIDE READY",
                  "GPS: FIXED",
                  "Hold button for SOS");
    } else {
      showMessage("SAFE-RIDE READY",
                  "GPS: searching",
                  "Hold button for SOS");
    }
  }

  delay(10);
}