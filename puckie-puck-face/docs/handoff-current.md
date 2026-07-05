# Puckie Puck Face - Current Handoff

Updated: 2026-07-05

## Scope

Firmware for an ELECROW CrowPanel 2.1-inch round ESP32-S3 rotary display,
currently targeting a Xiegu X6200 through CI-V over TCP. The TCP endpoint is a
Python simulator today and can later be a USB-serial bridge to a physical radio.

## Build and Test

```sh
cd firmware
pio run
cd ..
firmware/test/host/run_tests.sh
```

Flash by replacing the port with the locally detected ESP32 device:

```sh
cd firmware
pio run -t upload --upload-port /dev/cu.usbmodemXXXXX
```

Personal settings belong in gitignored
`firmware/include/config_local.h`:

```cpp
#pragma once
#define WIFI_SSID "your-2.4-ghz-network"
#define WIFI_PASSWORD "your-password"
#define RADIO_HOST "192.168.1.100"
```

Run the simulator:

```sh
cd simulator
python3 x6200_sim.py --verbose
```

## Navigation

Top-level groups, selected with an encoder double-click:

- Radio: main tuner, Quick Memories, Band Select
- Time: Grayline, Clock
- Conditions: Band Conditions
- Controls: Radio Controls
- System: Status

Encoder behavior:

- Main tuner rotate: tune frequency.
- Main tuner click: Quick Memories.
- Main tuner hold: change tuning step.
- Quick Memories rotate/click: select and tune preset.
- Quick Memories hold: Band Select.
- Band Select rotate/click: select and change band.
- Band Select hold: return to main tuner.
- Double-click: next top-level group.

Every non-tuner screen has a 48 px circular back-arrow button at bottom-center
that returns to the main tuner.

## Main Tuner

The main display is organized as:

1. Link state, VFO, mode, and band.
2. Live signal strip.
3. Large frequency display.
4. Tuning step and S value.
5. Fine radial tuning scale around the bezel.

The outer scale has 81 radial ticks over 270 degrees. Its active accent tick
moves only with frequency and tuning step; signal strength remains in the inner
strip and S label.

Touch actions:

- Tap mode: cycle `LSB -> USB -> CW -> CWR -> AM -> NFM`.
- Long-touch mode: toggle data mode (`USB-D`, etc.).
- Tap frequency or list icon: Quick Memories.
- Tap band: Band Select.
- Tap step: next tuning step.
- Tap S value: Radio Controls/meters.
- Tap link state: Status.

SSB is represented by its actual sidebands, LSB and USB.

## Radio Controls

The core controls page provides:

- Mode and data mode
- Filter 1/2/3
- Preamp on/off
- Attenuator on/off
- AGC: Auto/Fast/Slow/Off
- ATU on/off
- Start antenna tune
- Raw `0..255` S, RF output, SWR, and voltage meter readings

Rotate selects a control; click changes it or starts tuning. State is polled in
the background so physical-radio changes should eventually be reflected.

`TUNE START` sends documented X6200 command `1C 01 02`; ATU on/off sends
`1C 01 01` / `1C 01 00`. These pass simulator tests but require safe physical
verification with low power and a suitable antenna or dummy load.

## Profiles, Bands, and Memories

Radio-specific data is in:

- `firmware/src/radio_profile.h`
- `firmware/src/radio_profile.cpp`

The X6200 profile includes CI-V addresses, supported modes, 160m through 6m,
landing frequencies, and quick FT8/calling presets. Band Select remembers each
band's last frequency/mode/data state in RAM. Persistence and editing are not
implemented yet.

The profile boundary is intended to support IC-7300 and other radios later.

## Grayline and Themes

- Native `420x210` indexed monochrome map generated from public-domain Natural
  Earth 1:110m polygons.
- Black oceans with grayscale day/twilight/night land.
- Accent-colored station marker.
- Pre-NTP initialization avoids a blank map.
- Grayline math precomputes longitude cosine once per column.
- Antarctica is omitted to avoid date-line artifacts at this resolution.

`kDefaultTheme` contains the shared palette, including minor/major dial colors.
Future themes should be small flash-resident color tables with selection saved
in NVS; layouts and buffers should remain shared.

## Simulator Lifecycle

The simulator now allows one active client per source IP, closes older sockets
on reconnect, expires idle clients after 15 seconds, and enables TCP keepalive.
This prevents stale connections after ESP32 flashing/resetting.

## Hardware Direction

Future PCB haptics:

- Optional LRA coin actuator with a DRV2605L-class I2C driver.
- Do not drive the actuator directly from an ESP32 GPIO.
- Budget roughly 100 mA peak and include decoupling and motor pads/connector.
- Keep actuator routing away from radio/audio-sensitive paths and test RFI.
- Firmware should expose off/low/medium/high intensity.

## Known Limitations

- Physical X6200 operation needs a TCP-to-USB-serial bridge. Stock X6200 CAT is
  on the USB-C DEV port, CH342 SERIAL-B at 19200 8N1.
- Commands derive from Radioddity's X6200 CI-V document for firmware 1.0.6 but
  remain unverified on physical hardware.
- Meter values stay raw until RF/SWR/voltage calibration is verified.
- POTA fetch code remains, but POTA is no longer a primary UI screen.
- PSK Reporter is deferred until core radio workflows are proven.

## Verification

- Simulator unit tests: 25 passing.
- CI-V codec host tests: passing.
- Simulator/firmware codec integration: passing.
- PlatformIO firmware build: passing.
- Firmware has been repeatedly flashed and exercised on the CrowPanel.

## Recommended Next Work

1. Test every touch target and back button on the physical puck.
2. Connect through a USB serial bridge and verify read-only X6200 commands.
3. Verify mode/filter/PRE/ATT/AGC/ATU commands individually on real hardware.
4. Verify meter calibration and safe antenna tuning.
5. Add editable persistent memories and per-band state using NVS.
6. Investigate native memory, voice-message, CW-keyer, power, and PTT commands.
7. Add CW helper/macros, then PSK Reporter after core control is proven.

## External Reference

Interaction ideas were informed by the commercial FT-Control Yaesu manual:

`https://documents.roskosch.de/ham-control-yaesu-ios/`

It does not appear open source. Do not copy its code or assets.
