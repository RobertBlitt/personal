#!/usr/bin/env bash
# Host-side test runner for the Puckie Puck Face firmware.
#
# Runs three things, none of which need ESP32 hardware or toolchain:
#   1. the Python simulator's unit tests (25 tests)
#   2. the firmware CI-V codec compiled and unit-tested with host g++
#   3. the integration test: firmware codec vs live simulator over TCP
#
# Usage: ./run_tests.sh    (from this directory or anywhere)

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
FW_SRC="$HERE/../../src"
SIM_DIR="$HERE/../../../simulator"
BUILD="$HERE/build"
PORT=7399

mkdir -p "$BUILD"

echo "=== 1/3 simulator unit tests ==="
(cd "$SIM_DIR" && python3 -m unittest test_x6200_sim -v 2>&1 | tail -3)

echo "=== 2/3 civ codec host unit tests ==="
g++ -std=c++17 -Wall -Wextra -I"$FW_SRC" \
    "$HERE/test_civ_host.cpp" "$FW_SRC/civ.cpp" -o "$BUILD/test_civ"
"$BUILD/test_civ"

echo "=== 3/3 integration: firmware codec vs live simulator ==="
g++ -std=c++17 -Wall -Wextra -I"$FW_SRC" \
    "$HERE/test_integration.cpp" "$FW_SRC/civ.cpp" -o "$BUILD/test_integration"

python3 "$SIM_DIR/x6200_sim.py" --host 127.0.0.1 --port "$PORT" &
SIM_PID=$!
trap 'kill $SIM_PID 2>/dev/null || true' EXIT
sleep 1

"$BUILD/test_integration" "$PORT"

echo "=== all host-side tests passed ==="
