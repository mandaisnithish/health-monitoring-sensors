#!/usr/bin/env bash
# Install the board cores and libraries this repo needs.
# Requires arduino-cli (https://arduino.github.io/arduino-cli/).

set -euo pipefail

URLS="https://arduino.esp8266.com/stable/package_esp8266com_index.json,https://espressif.github.io/arduino-esp32/package_esp32_index.json"

arduino-cli core update-index --additional-urls "$URLS"
arduino-cli core install esp8266:esp8266 esp32:esp32 --additional-urls "$URLS"

arduino-cli lib install "MAX30100lib" "TinyGPSPlus" "Firebase Arduino Client Library for ESP8266 and ESP32" "Adafruit MLX90614 Library" "ArduinoJson"
