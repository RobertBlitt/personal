# Host-side tests

Everything here runs on a normal PC, no ESP32 toolchain or hardware.
These are the tests CI runs on every push (see
`.github/workflows/puckie-tests.yml`).

## run_tests.sh

Five stages:

1. The simulator's 25 Python unit tests.
2. Static review-regression checks for ESP32-bound firmware modules.
3. `test_civ_host.cpp`: the firmware's CI-V codec (`firmware/src/civ.cpp`)
   compiled with host g++ and checked against the same byte vectors as
   the Python suite, including the Radioddity document's worked example.
4. `test_radio_profile_host.cpp`: portable profile helpers, including
   band-name lookup from the profile table.
5. `test_integration.cpp`: the firmware codec talking to the LIVE Python
   simulator over a real TCP socket. Identify, read, tune, mode change,
   band jump, then a 90-transaction poll burst. If the C++ and Python
   halves of this project ever disagree on a byte, this stage fails.

## check_syntax.sh

Compiles the firmware modules with `g++ -fsyntax-only` against the real
LVGL 8.3.6 headers (shallow-cloned into `build/`), with thin Arduino API
stubs from `stubs/`. Catches LVGL API misuse and C++ errors early.
`display.cpp` is excluded: it needs the Arduino_GFX and esp-idf headers,
and it is a direct port of Elecrow's vendor demo.

## What only real hardware can verify

The xtensa target compile (`pio run`), the display init, touch, encoder
feel, WiFi behaviour, and the flagged CI-V assumptions listed in
`docs/ci-v-notes.md`.
