#!/usr/bin/env bash
# Compile every sketch in this repo for its target board with arduino-cli.
# Usage: scripts/compile_all.sh [filter]     e.g. scripts/compile_all.sh firmware
# Sketches that include secrets.h are built against a copy of secrets.example.h
# (secrets.h is git-ignored, so a real one is never overwritten or committed).

set -u
cd "$(dirname "$0")/.."

NODEMCU=esp8266:esp8266:nodemcuv2
ESP32=esp32:esp32:esp32

SKETCHES="
$NODEMCU  firmware/vitals_gps_firebase_esp8266
$ESP32    firmware/pressure_temp_motion_esp32
"

filter="${1:-}"
passed=0
failed=()

while read -r fqbn sketch; do
  [ -z "$fqbn" ] && continue
  case "$sketch" in *"$filter"*) ;; *) continue ;; esac

  if [ -f "$sketch/secrets.example.h" ] && [ ! -f "$sketch/secrets.h" ]; then
    cp "$sketch/secrets.example.h" "$sketch/secrets.h"
  fi

  printf '%-28s %s ... ' "$fqbn" "$sketch"
  if log=$(arduino-cli compile --fqbn "$fqbn" --warnings default "$sketch" 2>&1); then
    echo "ok"
    passed=$((passed + 1))
  else
    echo "FAILED"
    echo "$log" | sed 's/^/    /'
    failed+=("$sketch")
  fi
done <<< "$SKETCHES"

echo
echo "$passed passed, ${#failed[@]} failed"
[ ${#failed[@]} -eq 0 ]
