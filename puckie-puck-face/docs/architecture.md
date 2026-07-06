# Architecture

## The one decision that matters: the transport seam

Every viable way of reaching the X6200 eventually reduces to "move CI-V
frames between the knob and the radio." The paths differ only in plumbing:

| Path | Plumbing | Status |
| --- | --- | --- |
| Simulator | TCP to `x6200_sim.py` | working now |
| Pi bridge | TCP to `socat`/`ser2net` in front of the radio's USB CAT port | when radio arrives |
| Pi bridge, smarter | TCP to hamlib `rigctld` (different protocol, see below) | optional upgrade |
| Rooted radio | TCP to a daemon on the radio itself | someday, maybe |
| Dual-MCU Bluetooth | UART to an original-ESP32 co-processor speaking BT Classic SPP | if wireless-without-any-bridge is required |

So the firmware isolates transport in one module, `radio_client.cpp`. The
UI calls intent functions (`tuneBy`, `tuneTo`, `requestMode`) and reads a
`Snapshot` struct. It does not know sockets exist. Changing paths means
touching one file, worst case.

Phase 2 (choosing the real path) was deliberately deferred. Open questions
gating it, from the project handoff:

1. Does the X6200's stock Bluetooth CAT come from the internal radio or
   only via the WF-01 dongle? (Either way it is BT Classic SPP, which the
   ESP32-S3 cannot speak; only an original ESP32 can.)
2. Does the radio's WiFi/BT combo chip support BLE at the hardware level?
   That gates the dream path of a rooted radio running a BlueZ GATT server.
3. Is there a spare TTL UART carrying CAT on the internal headers, or only
   the Linux console? See github.com/tom-acco/Xiegu-X6200-Research.

## Why raw CI-V over TCP, not rigctld's protocol

hamlib's `rigctld` speaks its own text protocol, not CI-V. It would work,
and it papers over radio quirks nicely. But raw CI-V was chosen for now
because:

* the simulator then exercises the exact bytes the real radio will see,
  which makes the simulator a protocol test fixture, not just a mock;
* a dumb `socat` bridge is the least software that can possibly sit next
  to the radio;
* CI-V knowledge transfers to the dual-MCU path, where there is no Linux
  box to run rigctld on.

If the Pi bridge becomes permanent, adding a second transport that speaks
rigctld would slot in behind the same intent functions.

## Firmware structure

```
main.cpp          setup() wires modules together; loop() runs LVGL
 |
 +-- display.*    panel + touch bring-up (vendor-derived), LVGL glue
 +-- input.*      encoder + button -> event queue        [task, core 0]
 +-- net.*        WiFi, NTP, hamqsl solar fetcher        [task, core 0]
 +-- radio_client.*  CI-V over TCP, polling, intents     [task, core 0]
 +-- civ.*        pure CI-V codec, no I/O, host-testable
 +-- ui.*         LVGL screens, consumes everything      [loop, core 1]
```

Threading rule: only the `loop()` thread touches LVGL. Background tasks
publish through mutex-guarded snapshots and a FreeRTOS event queue. The
ESP32-S3 has two cores; rendering owns core 1, everything else shares
core 0.

Polling model: the radio task round-robins frequency, mode and S-meter
reads every `RADIO_POLL_INTERVAL_MS`, so front-panel changes made on the
radio itself show up on the knob within about half a second. User intents
(tuning) jump the queue and are coalesced, so spinning the encoder fast
produces a few set-frequency commands, not hundreds.

## Hardware notes

Pin map and provenance: see `firmware/include/pins.h`. Everything came
from Elecrow's vendor demo, not from datasheets, because the vendor demo
is known to light this exact panel.

Constraints that shaped the design (verified during project research):

* The CrowPanel's USB-C is a CH340-style serial bridge, not the S3's
  native USB, so the knob can never be a USB host for the radio's CAT
  port. Wireless or a bridge is mandatory.
* The ESP32-S3 has BLE only. The X6200's stock Bluetooth CAT is BT
  Classic SPP. They cannot pair. Only the original ESP32 has Classic,
  hence the dual-MCU option if a bridge-free link is ever required.
* The X6200 exposes CAT only on the USB DEV port (CH342, SERIAL-B,
  19200 8N1). There is no TTL CAT on the ACC jack.

## The pairing gesture

Long press is reserved in `input.cpp`/`ui.cpp` as the future "pair" action
(discover and remember the radio). Today it cycles modes as a placeholder.
When Phase 2 lands, the long press should trigger whatever discovery makes
sense for the chosen path (mDNS scan for the bridge, SPP discovery on the
co-processor, and so on), then persist the result in NVS flash.

## Sources

* Radioddity, "CI-V implementation of Xiegu X6200", firmware V1.0.6,
  2025-06-20 PDF. The only authority used for opcodes.
* Elecrow CrowPanel 2.1inch-HMI vendor repo (pin map, init sequence,
  library versions).
* N0NBH solar XML feed, hamqsl.com.
