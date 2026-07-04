#!/usr/bin/env bash
# Host syntax check of the firmware modules against the REAL LVGL 8.3.6
# and ArduinoJson 6.21.5 headers, with thin stubs standing in for the
# Arduino/ESP32 core (see stubs/). This catches LVGL API misuse and plain
# C++ errors without needing the xtensa toolchain.
#
# What it cannot catch: problems in display.cpp (which needs the
# Arduino_GFX and esp-idf headers) and anything about the ESP32 runtime.
# The real `pio run` remains the final word.
#
# Downloads (shallow git clones, cached in build/): lvgl v8.3.6,
# ArduinoJson v6.21.5.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
FW="$HERE/../.."
BUILD="$HERE/build"
mkdir -p "$BUILD"

if [ ! -d "$BUILD/lvgl" ]; then
    git clone --depth 1 --branch v8.3.6 https://github.com/lvgl/lvgl.git "$BUILD/lvgl"
fi
if [ ! -d "$BUILD/arduinojson" ]; then
    git clone --depth 1 --branch v6.21.5 https://github.com/bblanchon/ArduinoJson.git "$BUILD/arduinojson"
fi

FLAGS=(-std=c++17 -fsyntax-only -Wall -Wextra -Wno-unused-parameter
       -DLV_CONF_INCLUDE_SIMPLE
       -I"$HERE/stubs" -I"$FW/include" -I"$FW/src"
       -I"$BUILD/lvgl" -I"$BUILD/arduinojson/src")

FAIL=0
for f in ui.cpp input.cpp radio_client.cpp net.cpp main.cpp io_expander.cpp; do
    if g++ "${FLAGS[@]}" "$FW/src/$f"; then
        echo "OK   $f"
    else
        echo "FAIL $f"
        FAIL=1
    fi
done

if [ "$FAIL" -eq 0 ]; then
    echo "=== syntax check passed (display.cpp excluded by design) ==="
fi
exit "$FAIL"
