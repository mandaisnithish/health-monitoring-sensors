# Health Monitoring Sensor Nodes

Two independent sensor nodes for health monitoring experiments. One sends vital signs and position to Firebase; the other sends pressure, contact temperature and motion to an HTTP server and broadcasts the pressure over ESP-NOW.

## Nodes

### `firmware/vitals_gps_firebase_esp8266` (ESP8266, NodeMCU)

Once per second, writes this to `/data` in a Firebase Realtime Database:

| Field | Source |
|---|---|
| `hr`, `spo2` | MAX30100 pulse oximeter (I2C) |
| `ecg` | One raw ADC sample from an AD8232 front end on A0 |
| `gps/lat`, `gps/lng` | NMEA GPS on SoftwareSerial, `0.0` until there is a fix |

The MAX30100 library does its sampling inside `update()`, so the loop calls it continuously and only the upload is on a timer.

### `firmware/pressure_temp_motion_esp32` (ESP32 Dev Module)

Once per second, reads three sensors on three separate buses:

| Sensor | Bus | Notes |
|---|---|---|
| Pressure sensor, 14-bit SPI output | VSPI | Two bytes per read, top two bits are status. Counts 1638 to 14745 (10 % to 90 % of full scale) map linearly to 0 to 60 psi |
| MLX90614 IR thermometer | I2C bus 0 | Object and ambient temperature, through the Adafruit library |
| ADXL345 accelerometer | I2C bus 1 | Driven directly at register level: `POWER_CTL` to start measuring, then a six-byte burst read from `DATAX0` |

The ADXL345 is on the ESP32's second I2C controller so it does not share a bus with the MLX90614.

The readings then go out two ways:

- **HTTP POST** to `DATA_SERVER_URL` as JSON:

  ```json
  {"pressure": 12.34, "incontact_temp": 36.5, "ambient_temp": 24.1, "acc_x": 12, "acc_y": -3, "acc_z": 251}
  ```

- **ESP-NOW broadcast** of the pressure value alone (a single `float`), for a nearby receiver that should react without going through Wi-Fi infrastructure.

Wi-Fi credentials are a list; the node tries each network in turn at boot and again if the connection drops.

## Pins

**ESP8266 node**

| Signal | Pin |
|---|---|
| MAX30100 SDA / SCL | D2 / D1 |
| GPS RX / TX | D5 / D6 |
| AD8232 output | A0 |

**ESP32 node**

| Signal | GPIO |
|---|---|
| Pressure MISO / MOSI / SCK / CS | 19 / 23 / 18 / 5 |
| MLX90614 SDA / SCL | 21 / 22 |
| ADXL345 SDA / SCL | 25 / 26 |

## Build and upload

Libraries: **MAX30100lib**, **TinyGPSPlus**, **Firebase Arduino Client Library for ESP8266 and ESP32**, **Adafruit MLX90614 Library**, **ArduinoJson** (v7).

In each sketch folder, copy `secrets.example.h` to `secrets.h` and fill it in.

```bash
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 firmware/vitals_gps_firebase_esp8266
arduino-cli compile --fqbn esp32:esp32:esp32         firmware/pressure_temp_motion_esp32
```

The ESP32 sketch builds on both the 2.x and 3.x ESP32 Arduino cores.

## Tested versions

Last compiled with `arduino-cli` 1.6.0 against:

| Package | Version |
|---|---|
| ESP8266 (`esp8266:esp8266`) | 3.1.2 |
| ESP32 (`esp32:esp32`) | 3.3.12 |
| MAX30100lib | 1.2.1 |
| TinyGPSPlus | 1.0.3 |
| Firebase Arduino Client Library for ESP8266 and ESP32 | 4.4.17 |
| Adafruit MLX90614 Library | 2.1.7 |
| ArduinoJson | 7.4.3 |

`scripts/install_deps.sh` installs all of these and `scripts/compile_all.sh` builds every sketch in the repo. Both run in CI on each push.

## Known limitations

- These are prototypes for experimenting with the sensors. Nothing here is calibrated or suitable for medical use.
- The ESP8266 node authenticates with a Firebase legacy database secret, which gives full access to the database. Fine for a private test project, not for anything shared.
- The Firebase write blocks for a few hundred milliseconds each second. `PulseOximeter::update()` is not called during that time, which can cost beats in the heart-rate calculation.
- ECG is uploaded as one sample per second, which only shows that the front end is alive. A usable trace needs a few hundred samples per second and a batched upload.
- The ESP32 node posts over plain HTTP, and the ESP-NOW broadcast is unencrypted.
- The ESP32 node halts at boot if the MLX90614 or ADXL345 does not respond, and skips readings entirely while Wi-Fi is down.
- Acceleration is reported in raw counts (about 3.9 mg per count at the default ±2 g range).

## Ideas for later

- Sample the ECG on a timer into a buffer and upload it in batches.
- Keep reading sensors and broadcasting on ESP-NOW when Wi-Fi is down.
- Use Firebase email or anonymous auth with database rules instead of the legacy secret.

## License

MIT, see [LICENSE](LICENSE).
