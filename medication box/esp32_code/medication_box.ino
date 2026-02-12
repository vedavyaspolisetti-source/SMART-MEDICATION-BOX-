/**
 * =====================================================
 * SMART MEDICATION BOX - ESP32 FIRMWARE v6.0 (FULL FEATURES)
 * =====================================================
 *
 * NEW FEATURES:
 * 1. Alarm Queue Guard: Prevents repetitive adding to queue.
 * 2. IR Debounce: Filters noise (active for 200ms).
 * 3. Missed Dose Logic: Marks missed if > 15 mins.
 * 4. Battery Monitor: Reads GPIO 39 (ADC), updates Firebase.
 */

#include "addons/RTDBHelper.h"
#include "addons/TokenHelper.h"
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <Firebase_ESP_Client.h>
#include <LiquidCrystal_I2C.h>
#include <NTPClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>

// =====================================================
// 1. CONFIGURATION
// =====================================================

// ⚠️ UPDATE THESE
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define API_KEY "AIzaSyBfoXX_E1568WwAR6sCls_5o9L5h1FgZqc"
#define DATABASE_URL                                                           \
  "https://smart-medication-box-e8e5b-default-rtdb.firebaseio.com"

// Hardware Pins
const int LED_PINS[] = {25, 26, 27, 14}; // Box 1-4
const int IR_PINS[] = {33, 34, 35, 36};  // Box 1-4
const int BUZZER_PIN = 32;
const int BATTERY_PIN = 39; // VN Pin (Input Only, No Pullup, ADC1)

#define IR_ACTIVE_LEVEL LOW
#define MISSED_TIMEOUT_MS 900000     // 15 Minutes
#define BATTERY_CHECK_INTERVAL 60000 // 60 Seconds

#define LCD_ADDRESS 0x27
#define LCD_COLS 16
#define LCD_ROWS 2

// Servo Configuration
const int SERVO_PINS[] = {16, 17, 18, 19}; // Box 1-4
const int SERVO_OPEN_ANGLE = 90;
const int SERVO_CLOSE_ANGLE = 0;

// =====================================================
// 2. GLOBAL OBJECTS & STATE
// =====================================================

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLS, LCD_ROWS);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000);
Servo compartmentServos[4];

bool signupOK = false;
unsigned long lastCheck = 0;
unsigned long lastBatteryCheck = 0;
const int CHECK_INTERVAL = 5000;
bool midnightResetDone = false;

// Alarm Queue & Logic
// FEATURE A: Re-Queue Prevention
bool pendingAlarms[4] = {false, false, false, false};
bool alreadyQueued[4] = {false, false, false, false}; // Guard flag

int activeAlarmIndex = -1;
unsigned long alarmStartTime = 0; // For Missed Dose Timer

struct CompartmentState {
  String time;
  bool buzzer;
  bool taken;
  bool missed; // Track locally too
  // Medicine Data
  String medNames[5];
  int medCounts[5];
  int totalMeds;
};

CompartmentState boxes[4];

// =====================================================
// 3. SETUP
// =====================================================

void setup() {
  Serial.begin(115200);

  // Init Hardware
  for (int i = 0; i < 4; i++) {
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);
    pinMode(IR_PINS[i], INPUT);
    boxes[i].taken = false;

    // Servo Init
    compartmentServos[i].attach(SERVO_PINS[i]);
    compartmentServos[i].write(SERVO_CLOSE_ANGLE);
  }
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  // ADC for Battery
  analogReadResolution(12); // 0-4095

  lcd.init();
  lcd.backlight();
  lcd.print("System Boot v6.0");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  lcd.clear();
  lcd.print("WiFi Connected");

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  if (Firebase.signUp(&config, &auth, "", "")) {
    signupOK = true;
  }
  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  timeClient.begin();

  if (signupOK)
    syncData();
}

// =====================================================
// 4. MAIN LOOP
// =====================================================

void loop() {
  timeClient.update();
  checkMidnightReset();

  // Feature D: Battery Monitoring
  if (millis() - lastBatteryCheck > BATTERY_CHECK_INTERVAL) {
    updateBatteryStatus();
    lastBatteryCheck = millis();
  }

  if (activeAlarmIndex != -1) {
    handleAlarm(activeAlarmIndex);
  } else {
    // Queue Processing
    int nextAlarm = -1;
    for (int i = 0; i < 4; i++) {
      if (pendingAlarms[i]) {
        nextAlarm = i;
        break;
      }
    }

    if (nextAlarm != -1) {
      activateAlarm(nextAlarm);
    } else {
      if (millis() - lastCheck > CHECK_INTERVAL) {
        syncData();
        lastCheck = millis();
      }
      lcdStatus(); // Idle Screen
    }
  }
}

// =====================================================
// 5. CORE LOGIC
// =====================================================

void checkMidnightReset() {
  int h = timeClient.getHours();
  int m = timeClient.getMinutes();

  if (h == 0 && m == 0) {
    if (!midnightResetDone) {
      if (Firebase.ready() && signupOK) {
        for (int i = 1; i <= 4; i++) {
          String basePath = "/medication_box/compartment_" + String(i);
          Firebase.RTDB.setBool(&fbdo, (basePath + "/medicine_taken").c_str(),
                                false);
          Firebase.RTDB.setBool(&fbdo, (basePath + "/missed").c_str(),
                                false); // Reset Missed
          // Reset Local Guard
          alreadyQueued[i - 1] = false;
        }
        midnightResetDone = true;
        for (int i = 0; i < 4; i++) {
          pendingAlarms[i] = false;
        }
      }
    }
  } else {
    midnightResetDone = false;
  }
}

void syncData() {
  if (!Firebase.ready() || !signupOK)
    return;
  String currentTime = getFormattedTime();

  if (Firebase.RTDB.getJSON(&fbdo, "/medication_box")) {
    FirebaseJson &json = fbdo.jsonObject();

    for (int i = 0; i < 4; i++) {
      String compKey = "compartment_" + String(i + 1);
      FirebaseJsonData result;
      json.get(result, compKey);

      if (result.success) {
        FirebaseJson compJson;
        compJson.setJsonData(result.stringValue);

        FirebaseJsonData t, b, taken, meds, missed;
        compJson.get(t, "time");
        compJson.get(b, "buzzer");
        compJson.get(taken, "medicine_taken");
        compJson.get(missed, "missed");
        compJson.get(meds, "medicines");

        boxes[i].time = t.stringValue;
        boxes[i].buzzer = b.boolValue;
        boxes[i].taken = taken.boolValue;
        boxes[i].missed = missed.boolValue;

        // Trigger Check: Match Time + Not Taken + Not Missed + Not Already
        // Queued
        if (boxes[i].time == currentTime && !boxes[i].taken &&
            !boxes[i].missed && !alreadyQueued[i]) {
          parseMedicineData(i, meds);
          pendingAlarms[i] = true;
          alreadyQueued[i] = true; // Lock Re-Queue
          Serial.println("Queueing Box " + String(i + 1));
        }
      }
    }
  }
}

void parseMedicineData(int index, FirebaseJsonData &meds) {
  boxes[index].totalMeds = 0;
  if (meds.type != "array")
    return;

  FirebaseJsonArray arr;
  arr.setJsonArrayData(meds.stringValue);

  for (size_t k = 0; k < arr.size(); k++) {
    if (k >= 5)
      break;
    FirebaseJsonData itemData;
    arr.get(itemData, k);
    FirebaseJson item;
    item.setJsonData(itemData.stringValue);
    FirebaseJsonData n, c;
    item.get(n, "name");
    item.get(c, "tablets");
    boxes[index].medNames[k] = n.stringValue;
    boxes[index].medCounts[k] = c.intValue;
    boxes[index].totalMeds++;
  }
}

void activateAlarm(int index) {
  activeAlarmIndex = index;
  pendingAlarms[index] = false;
  alarmStartTime = millis(); // Start Timer for Missed Logic
  digitalWrite(LED_PINS[index], HIGH);
  compartmentServos[index].write(SERVO_OPEN_ANGLE); // Open Lid
  Serial.println("ALARM ACTIVE: Box " + String(index + 1));
}

void handleAlarm(int index) {
  // Feature C: Missed Dose Logic
  if (millis() - alarmStartTime > MISSED_TIMEOUT_MS) {
    markAsMissed(index);
    return;
  }

  if (boxes[index].buzzer) {
    pulseBuzzer();
  }

  updateActiveLCD(index);

  // Feature B: IR Debounce
  if (digitalRead(IR_PINS[index]) == IR_ACTIVE_LEVEL) {
    delay(200); // 200ms Debounce
    if (digitalRead(IR_PINS[index]) == IR_ACTIVE_LEVEL) {
      completeAlarm(index);
    }
  }
}

void completeAlarm(int index) {
  Serial.println("Taken! Box " + String(index + 1));
  compartmentServos[index].write(SERVO_CLOSE_ANGLE); // Close Lid
  stopAlarmHardware(index);

  String basePath = "/medication_box/compartment_" + String(index + 1);
  Firebase.RTDB.setBool(&fbdo, (basePath + "/medicine_taken").c_str(), true);
  Firebase.RTDB.setBool(&fbdo, (basePath + "/missed").c_str(),
                        false); // Clear missed
  Firebase.RTDB.setString(&fbdo, (basePath + "/last_taken_time").c_str(),
                          getFormattedTime());

  boxes[index].taken = true;

  lcd.clear();
  lcd.print("Thank You!");
  delay(2000);

  activeAlarmIndex = -1;
}

void markAsMissed(int index) {
  Serial.println("MISSED! Box " + String(index + 1));
  compartmentServos[index].write(SERVO_CLOSE_ANGLE); // Close Lid safely
  stopAlarmHardware(index);

  String basePath = "/medication_box/compartment_" + String(index + 1);
  Firebase.RTDB.setBool(&fbdo, (basePath + "/missed").c_str(), true);
  // Don't mark taken, leave it pending/missed

  lcd.clear();
  lcd.print("Missed Dose!");
  delay(2000);

  activeAlarmIndex = -1;
}

void stopAlarmHardware(int index) {
  digitalWrite(LED_PINS[index], LOW);
  digitalWrite(BUZZER_PIN, LOW);
}

// Feature D: Battery Monitoring (2S LiPo 7.4V)
// Assumes Voltage Divider: V_out = V_in / 3.0 (e.g., R1=20k, R2=10k)
// 3.3V ADC full scale. Max V_in = 3.3 * 3.0 = 9.9V (Safe for 8.4V max)
#define VOLTAGE_DIVIDER_FACTOR 3.0

void updateBatteryStatus() {
  int raw = analogRead(BATTERY_PIN);
  float voltage = (raw / 4095.0) * 3.3 * VOLTAGE_DIVIDER_FACTOR;

  // 2S LiPo: Empty ~6.0V, Full ~8.4V
  int percentage = map((long)(voltage * 100), 600, 840, 0, 100);

  if (percentage > 100)
    percentage = 100;
  if (percentage < 0)
    percentage = 0;

  // Path: /medication_box/system_status/...
  Firebase.RTDB.setInt(
      &fbdo, "/medication_box/system_status/battery_percentage", percentage);
  Firebase.RTDB.setFloat(&fbdo, "/medication_box/system_status/battery_voltage",
                         voltage);

  bool lowBattery = (percentage < 20);
  Firebase.RTDB.setBool(&fbdo, "/medication_box/system_status/low_battery",
                        lowBattery);

  if (lowBattery) {
    lcd.setCursor(0, 0);
    lcd.print("LOW BATTERY!    ");
  }
}

void updateActiveLCD(int index) {
  static unsigned long lastUpdate = 0;
  static int cycle = -1;
  if (millis() - lastUpdate > 2000) {
    lcd.clear();
    if (cycle == -1) {
      lcd.setCursor(0, 0);
      lcd.print("TAKE MEDS!");
      lcd.setCursor(0, 1);
      lcd.print("Open Box " + String(index + 1));
      cycle = 0;
      if (boxes[index].totalMeds == 0)
        cycle = -1;
    } else {
      lcd.setCursor(0, 0);
      lcd.print(boxes[index].medNames[cycle]);
      lcd.setCursor(0, 1);
      lcd.print(String(boxes[index].medCounts[cycle]) + " pills");
      cycle++;
      if (cycle >= boxes[index].totalMeds)
        cycle = -1;
    }
    lastUpdate = millis();
  }
}

void lcdStatus() {
  static unsigned long last = 0;
  if (millis() - last > 5000) {
    // Only overwrite if NO active alarm (checked in loop)
    // And maybe check if low battery?
    // For simplicity, we just show time/status.
    // If updateBatteryStatus runs, it might briefly show Low Battery.
    // Ideally we'd use a global flag, but keeping it simple as requested.
    lcd.setCursor(0, 0);
    lcd.print("System Ready    ");
    lcd.setCursor(0, 1);
    lcd.print(getFormattedTime());
    last = millis();
  }
}

String getFormattedTime() {
  int h = timeClient.getHours();
  int m = timeClient.getMinutes();
  String suffix = (h >= 12) ? "PM" : "AM";
  int h12 = (h > 12) ? h - 12 : h;
  if (h12 == 0)
    h12 = 12;
  char buf[10];
  sprintf(buf, "%02d:%02d %s", h12, m, suffix.c_str());
  return String(buf);
}

void pulseBuzzer() {
  static bool state = false;
  static unsigned long last = 0;
  if (millis() - last > 300) {
    state = !state;
    digitalWrite(BUZZER_PIN, state);
    last = millis();
  }
}
