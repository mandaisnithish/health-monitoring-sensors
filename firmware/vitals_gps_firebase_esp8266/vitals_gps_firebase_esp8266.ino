// Vitals + location uploader: heart rate and SpO2 (MAX30100), one ECG sample (AD8232 analog out)
// and GPS position, written to Firebase RTDB /data once per second.
// Board: ESP8266 NodeMCU-style board.

#include <Wire.h>
#include <ESP8266WiFi.h>
#include <Firebase_ESP_Client.h>
#include <TinyGPSPlus.h>
#include <SoftwareSerial.h>
#include <MAX30100_PulseOximeter.h>
#include "secrets.h"  // WIFI_SSID, WIFI_PASSWORD, FIREBASE_DATABASE_URL, FIREBASE_DATABASE_SECRET

const int GPS_RX_PIN = D5;
const int GPS_TX_PIN = D6;
const int I2C_SDA_PIN = D2;
const int I2C_SCL_PIN = D1;
const int ECG_ANALOG_PIN = A0;

const unsigned long UPLOAD_INTERVAL_MS = 1000;

FirebaseData firebaseData;
FirebaseAuth firebaseAuth;
FirebaseConfig firebaseConfig;

SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);
TinyGPSPlus gps;
PulseOximeter pulseOximeter;

unsigned long lastUploadMs = 0;

void connectWifi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
}

void uploadVitals() {
  FirebaseJson json;
  bool hasFix = gps.location.isValid();
  json.set("/gps/lat", hasFix ? gps.location.lat() : 0.0);
  json.set("/gps/lng", hasFix ? gps.location.lng() : 0.0);
  json.set("/hr", pulseOximeter.getHeartRate());
  json.set("/spo2", pulseOximeter.getSpO2());
  json.set("/ecg", analogRead(ECG_ANALOG_PIN));

  if (Firebase.RTDB.setJSON(&firebaseData, "/data", &json)) {
    Serial.println("Data sent to Firebase");
  } else {
    Serial.print("Firebase failed: ");
    Serial.println(firebaseData.errorReason());
  }
}

void setup() {
  Serial.begin(115200);
  connectWifi();

  firebaseConfig.database_url = FIREBASE_DATABASE_URL;
  firebaseConfig.signer.tokens.legacy_token = FIREBASE_DATABASE_SECRET;
  Firebase.begin(&firebaseConfig, &firebaseAuth);
  Firebase.reconnectWiFi(true);

  gpsSerial.begin(9600);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  if (!pulseOximeter.begin()) {
    Serial.println("MAX30100 not found");
    while (true) { delay(100); }  // delay keeps the ESP8266 watchdog fed
  }
  pulseOximeter.setIRLedCurrent(MAX30100_LED_CURR_7_6MA);

  pinMode(ECG_ANALOG_PIN, INPUT);
}

void loop() {
  while (gpsSerial.available() > 0) gps.encode(gpsSerial.read());

  // must run as often as possible, the library samples inside update()
  pulseOximeter.update();

  if (millis() - lastUploadMs > UPLOAD_INTERVAL_MS) {
    lastUploadMs = millis();
    uploadVitals();
  }
}
