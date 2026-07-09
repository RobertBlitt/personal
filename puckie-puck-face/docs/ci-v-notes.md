# CI-V notes for the X6200

Source of truth: Radioddity, "CI-V implementation of Xiegu X6200, as of
Firmware V1.0.6" (2025-06-20 PDF, 13 pages). Everything below is extracted
from that document. Nothing is taken from generic ICOM manuals; the X6200
implements a subset and the subset is what the tables say.

## Framing

```
controller -> radio:  FE FE A4 00 <cmd> [sub] [data...] FD
radio -> controller:  FE FE 00 A4 <cmd> [sub] [data...] FD
```

`A4` is the radio's address, `00` is the controller. Replies swap them.

Physical transport on the stock radio: USB-C DEV port, CH342 dual serial,
SERIAL-B at 19200 8N1 (SERIAL-A is a terminal, up to 115200).

## Frequency BCD (Table 2)

Five bytes, two decimal digits each, least significant pair first.
The document's own example: 21,002,360 Hz is `60 23 00 21 00`.

## Commands this project uses

| Frame | Meaning |
| --- | --- |
| `03` | get active VFO frequency |
| `25 00` / `25 00 + 5 BCD` | get / set selected VFO frequency |
| `25 01 ...` | same for the non-selected VFO |
| `26 00` / `26 00 mm dd ff` | get / set mode, data flag, filter (Table 3) |
| `15 02` | get S-meter, 2-byte BCD 0..255 |
| `1A 01` / `1A 01 bb xx` | get / set band stacking register (Table 4) |
| `19 00` | get radio CI-V address (returns A4) |
| `1C 00` | get / set PTT (not used by the UI yet) |

Modes (Table 3): LSB=00, USB=01, AM=02, CW=03, NFM=05, CWR=07. The second
byte is the data-mode flag (USB-D and so on), the third is filter 1..3.
Note: changing the filter applies to both VFOs, per the document.

Bands (Table 4): 1=160m, 2=80m, 3=60m, 4=40m, 5=30m, 6=20m, 7=17m, 8=15m,
9=12m, A=10m, B=6m, C=FM/AIR. The second byte is always 02.

Levels (command 14) and meters (command 15) are 2-byte BCD in 0..255:
255 encodes as `02 55`.

## Assumptions to verify on real hardware

Flagged in code comments as VERIFY:

1. OK/NG acknowledgements. The document lists commands but does not show
   ack bytes. Standard ICOM practice is `FB` (ok) and `FA` (no good) with
   swapped addresses; the simulator and firmware both assume it.
2. The exact reply layout of `1D 19` (model ID) and `02` (frequency range,
   documented as "Freq(dash)Freq" which we implement as a literal `2D`
   between two 5-byte BCD values).
3. Whether the real radio echoes controller frames back on the wire (a
   shared-bus CI-V behaviour). The firmware's parser drops such echoes
   already, so either answer is safe.

## Handy manual pokes

Ask the simulator (or radio behind socat) for its frequency:

```bash
printf '\xfe\xfe\xa4\x00\x03\xfd' | nc -q1 localhost 7373 | xxd
```

Set 7.074 MHz then read it back:

```bash
printf '\xfe\xfe\xa4\x00\x25\x00\x00\x40\x07\x07\x00\xfd\xfe\xfe\xa4\x00\x03\xfd' | nc -q1 localhost 7373 | xxd
```
