# Puckie Puck Face

A pocket-sized wireless control head for the Xiegu X6200 HF transceiver,
built on the ELECROW CrowPanel 2.1" ESP32-S3 rotary display. Spin the knob
to tune, click to change the step, read the S-meter from across the room.

This is Phase 0 + 1 of the project: a working standalone ham dashboard plus
a complete radio-control UI that talks CI-V over WiFi. Since the real radio
is not on the bench yet, a Python simulator plays the part of the X6200,
answering the exact same bytes the radio will.

```
+-------------------+        WiFi / TCP         +--------------------------+
|  CrowPanel knob   | <----- CI-V frames -----> |  today: x6200_sim.py     |
|  (this firmware)  |                           |  later: Pi + socat + rig |
+-------------------+                           |  later: rooted X6200     |
                                                +--------------------------+
```

The trick that makes this future-proof: the firmware speaks raw CI-V over a
TCP socket. The simulator, a Raspberry Pi bridging USB CAT, and a rooted
radio all look identical from the knob's side. Choosing between them later
changes one IP address in `config.h`, not the firmware.

## What is in here

| Path | What |
| --- | --- |
| `firmware/` | PlatformIO project for the CrowPanel (C++, LVGL 8) |
| `simulator/` | Python X6200 CI-V simulator plus its test suite |
| `docs/` | Architecture decisions, CI-V notes, hardware pin map |

## Quick start

### 1. Run the simulator (any Linux or macOS box, Python 3.8+)

```bash
cd simulator
python3 x6200_sim.py --verbose
```

It listens on TCP port 7373 and prints every CI-V exchange. Run the tests
with `python3 -m unittest test_x6200_sim -v` (25 tests, all protocol bytes
checked against the official Radioddity CI-V document).

### 2. Configure and flash the firmware

Edit `firmware/include/config.h`:

* `WIFI_SSID` / `WIFI_PASSWORD`: your 2.4 GHz network
* `RADIO_HOST`: the IP of the machine running the simulator
* `STATION_GRID` and `TIME_ZONE` if you are not in Honolulu

Then, with [PlatformIO](https://platformio.org/install/cli) installed:

```bash
cd firmware
pio run                 # compile
pio run -t upload       # flash over the CrowPanel's USB-C
pio device monitor      # watch the boot log at 115200 baud
```

Note on flashing: the CrowPanel's USB-C goes through a CH340-style serial
bridge. If the first upload fails, hold BOOT, tap RESET, release BOOT, and
retry.

### 3. Drive it

* Double click: cycle screens (Radio, Clock, Band conditions, Controls, Status)
* Radio screen: turn to tune, click to change tuning step (10 Hz to 10 kHz),
  long press cycles mode (placeholder for the future pairing gesture)

## The screens

1. **Radio**: frequency in big digits, mode, S-meter arc around the bezel,
   link status.
2. **Quick memories / band select**: jump to profile-defined memories and
   band landing points.
3. **Grayline / Clock**: UTC and local time via NTP, date, grid square, and
   grayline map.
4. **Band conditions**: solar flux, A and K index, plus day/night ratings
   for the HF band groups, from N0NBH's hamqsl.com feed.
5. **Controls / Status**: mode, filter, preamp, attenuator, AGC, ATU, meters,
   WiFi, and radio-link status.

## When the real radio arrives

Plug the X6200's USB-C DEV port into any Linux box (a Pi Zero 2 W works)
and expose SERIAL-B, which carries CAT at 19200 baud, over TCP:

```bash
socat TCP-LISTEN:7373,fork,reuseaddr FILE:/dev/ttyACM1,b19200,raw
```

Point `RADIO_HOST` at the Pi. Done: the same firmware now controls real
hardware. (Check which of the CH342's two ports is SERIAL-B; it is usually
the second one enumerated.)

See `docs/architecture.md` for the longer-term paths, including the
dual-MCU Bluetooth option and the rooted-radio option, and why this repo
is structured so that choice can wait.

## Verification status

Honest accounting of what has been tested where. Run everything below
yourself with `firmware/test/host/run_tests.sh`; CI runs it on every push.

* Simulator: 25 unit tests, all passing, including a live socket round
  trip.
* Firmware CI-V codec (`civ.cpp`): compiled and unit-tested on a host PC
  against the same byte vectors as the simulator tests. All passing.
* Integration: the firmware codec talking to the live Python simulator
  over TCP (identify, tune, mode, band jump, 90-transaction poll burst).
  All passing.
* Firmware UI, input, radio client, net modules: syntax-checked against
  the real LVGL 8.3.6 headers. Not yet compiled for the ESP32-S3 target
  (the build sandbox that produced this code could not download the xtensa
  toolchain), and NOT yet run on hardware.
* Display bring-up (`display.cpp`): direct port of Elecrow's known-good
  vendor demo for this exact panel, unmodified init sequence and timings.

A second review pass audited the one-shot build against the vendor
library sources and fixed three real bugs before any hardware was flashed:
a shared-expander init conflict that would have blanked the panel, an
encoder tuning double-count, and a POTA frequency display that relied on
float printf support LVGL ships disabled.

Expect the first `pio run` plus first flash to surface something small;
that is normal. The bones are verified.
