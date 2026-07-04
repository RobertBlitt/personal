# X6200 CI-V simulator

A stand-in for the Xiegu X6200's CAT port, speaking real CI-V frames over
TCP. The knob firmware cannot tell it from the real radio (that is the
point). Python 3.8+, standard library only.

## Run

```bash
python3 x6200_sim.py                     # 0.0.0.0:7373
python3 x6200_sim.py --verbose           # hex-dump every frame
python3 x6200_sim.py --freq 7074000 --mode LSB
```

The S-meter wanders on its own and random "stations" key up now and then,
so the knob UI has something alive to display.

## Test

```bash
python3 -m unittest test_x6200_sim -v
```

25 tests. The BCD codec is checked against the worked example in the
Radioddity CI-V document (21,002,360 Hz is `60 23 00 21 00` on the wire),
and one test drives a real socket end to end.

## Poke it by hand

```bash
# read frequency
printf '\xfe\xfe\xa4\x00\x03\xfd' | nc -q1 localhost 7373 | od -An -tx1
```

See `../docs/ci-v-notes.md` for the command tables and more examples.

## Replacing it with the real radio

On whatever Linux box the radio's USB DEV cable reaches:

```bash
socat TCP-LISTEN:7373,fork,reuseaddr FILE:/dev/ttyACM1,b19200,raw
```

SERIAL-B of the radio's CH342 carries CAT at 19200 8N1. Nothing on the
knob changes except `RADIO_HOST` in its config.
