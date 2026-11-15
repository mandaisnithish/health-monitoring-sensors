// ESP32 multi-sensor node: SPI pressure sensor, MLX90614 IR temperature (I2C bus 0),
// ADXL345 accelerometer (I2C bus 1). Every second: POST all readings as JSON to an HTTP
// server and broadcast the pressure value over ESP-NOW.
// Board: ESP32 Dev Module. Builds on ESP32 Arduino core 2.x and 3.x.

#include <esp_now.h>
#include <WiFi.h>
#include <SPI.h>
#include <Wire.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_MLX90614.h>
#include "secrets.h"  // WIFI_NETWORKS[], DATA_SERVER_URL

// SPI pressure sensor (VSPI)
const int PRESSURE_MISO_PIN = 19;
const int PRESSURE_MOSI_PIN = 23;
const int PRESSURE_SCK_PIN = 18;
const int PRESSURE_CS_PIN = 5;

// I2C bus 0: MLX90614
const int MLX_SDA_PIN = 21;
const int MLX_SCL_PIN = 22;

// I2C bus 1: ADXL345
const int ADXL_SDA_PIN = 25;
const int ADXL_SCL_PIN = 26;
const uint8_t ADXL345_I2C_ADDRESS = 0x53;
const uint8_t ADXL345_REG_POWER_CTL = 0x2D;
const uint8_t ADXL345_REG_DATAX0 = 0x32;
const uint8_t ADXL345_MEASURE_MODE = 0x08;

// 14-bit sensor output, 10% to 90% of full scale = 0 to 60 psi
const float PRESSURE_MIN_PSI = 0.0;
const float PRESSURE_MAX_PSI = 60.0;
const uint16_t PRESSURE_COUNTS_MIN = 1638;
const uint16_t PRESSURE_COUNTS_MAX = 14745;

const int WIFI_ATTEMPTS_PER_NETWORK = 10;
const unsigned long LOOP_INTERVAL_MS = 1000;

// broadcast address: every ESP-NOW receiver on the channel gets it
uint8_t espNowPeerAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct EspNowPressureMessage {
  float pressurePsi;
} espNowMessage;

SPIClass pressureSpi(VSPI);
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
TwoWire adxlWire = TwoWire(1);

struct Acceleration {
  int16_t x;
  int16_t y;
  int16_t z;
};

// ESP32 Arduino core 3.3 (ESP-IDF 5.5) changed the first argument of the send callback
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 0)
typedef wifi_tx_info_t EspNowSendInfo;
#else
typedef uint8_t EspNowSendInfo;
#endif

void onEspNowSent(const EspNowSendInfo* sendInfo, esp_now_send_status_t status) {
  Serial.print("ESP-NOW send status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Failed");
}

// tries every network in WIFI_NETWORKS until one connects
bool connectToKnownWifi() {
  for (int i = 0; i < WIFI_NETWORK_COUNT; i++) {
    Serial.print("Trying SSID: ");
    Serial.println(WIFI_NETWORKS[i].ssid);
    WiFi.begin(WIFI_NETWORKS[i].ssid, WIFI_NETWORKS[i].password);
    for (int attempt = 0; attempt < WIFI_ATTEMPTS_PER_NETWORK && WiFi.status() != WL_CONNECTED; attempt++) {
      Serial.print(".");
      delay(1000);
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("\nConnected to: ");
      Serial.println(WIFI_NETWORKS[i].ssid);
      return true;
    }
    Serial.println("\nFailed");
  }
  return false;
}

bool initEspNow() {
  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_send_cb(onEspNowSent);

  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));  // unset fields must be zero
  memcpy(peerInfo.peer_addr, espNowPeerAddress, 6);
  peerInfo.channel = 0;  // 0 = current Wi-Fi channel
  peerInfo.encrypt = false;
  return esp_now_add_peer(&peerInfo) == ESP_OK;
}

bool initAdxl345() {
  adxlWire.beginTransmission(ADXL345_I2C_ADDRESS);
  if (adxlWire.endTransmission() != 0) return false;
  adxlWire.beginTransmission(ADXL345_I2C_ADDRESS);
  adxlWire.write(ADXL345_REG_POWER_CTL);
  adxlWire.write(ADXL345_MEASURE_MODE);
  adxlWire.endTransmission();
  return true;
}

uint16_t readPressureCounts() {
  digitalWrite(PRESSURE_CS_PIN, LOW);
  uint8_t highByte = pressureSpi.transfer(0x00);
  uint8_t lowByte = pressureSpi.transfer(0x00);
  digitalWrite(PRESSURE_CS_PIN, HIGH);
  return ((highByte << 8) | lowByte) & 0x3FFF;  // top 2 bits are status
}

float countsToPsi(uint16_t counts) {
  if (counts < PRESSURE_COUNTS_MIN) return PRESSURE_MIN_PSI;
  if (counts > PRESSURE_COUNTS_MAX) return PRESSURE_MAX_PSI;
  return PRESSURE_MIN_PSI + (float)(counts - PRESSURE_COUNTS_MIN) /
                                (PRESSURE_COUNTS_MAX - PRESSURE_COUNTS_MIN) *
                                (PRESSURE_MAX_PSI - PRESSURE_MIN_PSI);
}

// DATAX0..DATAZ1: 6 bytes, little endian
Acceleration readAcceleration() {
  Acceleration accel = {0, 0, 0};
  adxlWire.beginTransmission(ADXL345_I2C_ADDRESS);
  adxlWire.write(ADXL345_REG_DATAX0);
  adxlWire.endTransmission(false);
  adxlWire.requestFrom(ADXL345_I2C_ADDRESS, (uint8_t)6, (uint8_t)true);
  if (adxlWire.available() == 6) {
    uint8_t raw[6];
    for (int i = 0; i < 6; i++) raw[i] = adxlWire.read();  // read in a fixed order
    accel.x = (int16_t)(raw[0] | (raw[1] << 8));
    accel.y = (int16_t)(raw[2] | (raw[3] << 8));
    accel.z = (int16_t)(raw[4] | (raw[5] << 8));
  }
  return accel;
}

void postReadingsToServer(float pressurePsi, float objectTempC, float ambientTempC, const Acceleration &accel) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, skipping upload");
    return;
  }

  JsonDocument json;
  json["pressure"] = pressurePsi;
  json["incontact_temp"] = objectTempC;
  json["ambient_temp"] = ambientTempC;
  json["acc_x"] = accel.x;
  json["acc_y"] = accel.y;
  json["acc_z"] = accel.z;
  String payload;
  serializeJson(json, payload);

  HTTPClient http;
  http.begin(DATA_SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  int httpStatus = http.POST(payload);
  if (httpStatus > 0) Serial.printf("Server response: %d\n", httpStatus);
  else Serial.printf("Upload failed: %s\n", http.errorToString(httpStatus).c_str());
  http.end();
}

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  if (!connectToKnownWifi()) Serial.println("No known WiFi network available");

  Wire.begin(MLX_SDA_PIN, MLX_SCL_PIN);

  pressureSpi.begin(PRESSURE_SCK_PIN, PRESSURE_MISO_PIN, PRESSURE_MOSI_PIN, PRESSURE_CS_PIN);
  pinMode(PRESSURE_CS_PIN, OUTPUT);
  digitalWrite(PRESSURE_CS_PIN, HIGH);

  adxlWire.begin(ADXL_SDA_PIN, ADXL_SCL_PIN);

  // keep going without ESP-NOW, HTTP upload still works
  if (!initEspNow()) Serial.println("ESP-NOW init failed");

  if (!mlx.begin()) {
    Serial.println("MLX90614 not found");
    while (true) { delay(1000); }
  }
  if (!initAdxl345()) {
    Serial.println("ADXL345 not found");
    while (true) { delay(1000); }
  }
  Serial.println("All sensors initialized");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost, reconnecting");
    connectToKnownWifi();
    delay(5000);
    return;
  }

  float pressurePsi = countsToPsi(readPressureCounts());
  float objectTempC = mlx.readObjectTempC();
  float ambientTempC = mlx.readAmbientTempC();
  Acceleration accel = readAcceleration();

  Serial.printf("Pressure: %.2f psi\n", pressurePsi);
  Serial.printf("Object temp: %.2f C\n", objectTempC);
  Serial.printf("Ambient temp: %.2f C\n", ambientTempC);
  Serial.printf("Accel X: %d Y: %d Z: %d\n", accel.x, accel.y, accel.z);

  espNowMessage.pressurePsi = pressurePsi;
  esp_err_t espNowResult = esp_now_send(espNowPeerAddress, (uint8_t*)&espNowMessage, sizeof(espNowMessage));
  Serial.println(espNowResult == ESP_OK ? "Pressure queued on ESP-NOW" : "ESP-NOW send failed");

  postReadingsToServer(pressurePsi, objectTempC, ambientTempC, accel);

  Serial.println("----------------------------");
  delay(LOOP_INTERVAL_MS);
}
