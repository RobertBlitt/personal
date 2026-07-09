#!/usr/bin/env python3
"""
x6200_sim.py, a Xiegu X6200 CI-V simulator.

WHAT THIS IS
------------
A stand-in for the CAT (Computer Aided Transceiver) serial port of a Xiegu
X6200 that does not exist on your bench yet. It listens on a TCP port and
speaks raw CI-V frames, byte for byte, the way the real radio speaks them on
SERIAL-B of its USB-C DEV port at 19200 baud.

Why TCP instead of a fake serial port? Because TCP is the seam this whole
project is built around. The Puckie Puck Face knob firmware opens a TCP
socket and writes CI-V frames into it. Today that socket lands here. Later
it can land on:

  * a Raspberry Pi running `socat TCP-LISTEN:7373,fork,reuseaddr FILE:/dev/ttyACM1,b19200,raw`
    with the real radio plugged into the Pi over USB, or
  * a rooted X6200 running a tiny daemon that bridges TCP to its internal
    CAT plumbing.

In all three cases the knob firmware is identical. Only the IP changes.

WHERE THE PROTOCOL FACTS COME FROM
----------------------------------
Every command implemented here is taken from the official document
"CI-V implementation of Xiegu X6200, as of Firmware V1.0.6" published by
Radioddity (2025-06-20 PDF). Do not add commands from generic ICOM manuals
without checking that document first; the X6200 implements a subset.

Facts used from that document:
  * Every frame starts with the preamble FE FE, then the destination
    address, then the source address, and ends with FD.
  * The radio's CI-V address is 0xA4. The controller (us, the PC or knob)
    is 0x00. So a command travels as FE FE A4 00 ... FD and the reply
    travels back as FE FE 00 A4 ... FD (addresses swapped).
  * Frequencies are 5 bytes of little-endian packed BCD (two decimal
    digits per byte, least significant digits first). The document's own
    worked example: 21,002,360 Hz encodes as 60 23 00 21 00.
  * Mode is 3 bytes on command 0x26: mode number, data flag, filter number.
  * Meters (command 0x15) and levels (command 0x14) use a 2-byte BCD value
    in the range 0000 to 0255.

ONE DOCUMENTED ASSUMPTION
-------------------------
The Radioddity document lists the commands but does not spell out the OK/NG
acknowledgement bytes. Standard ICOM CI-V practice, which the X6200 follows
for everything else, is:
    OK  ("FB"): FE FE 00 A4 FB FD
    NG  ("FA"): FE FE 00 A4 FA FD
This simulator uses those. Flag: VERIFY against the real radio when it
arrives, then delete this paragraph or confirm it.

USAGE
-----
    python3 x6200_sim.py                 # listen on 0.0.0.0:7373
    python3 x6200_sim.py --port 7373 --verbose
    python3 x6200_sim.py --freq 14074000 --mode USB

Quick smoke test from another terminal (asks for the VFO frequency):
    printf '\\xfe\\xfe\\xa4\\x00\\x03\\xfd' | nc -q1 localhost 7373 | xxd

No third-party dependencies. Python 3.8 or newer.
"""

import argparse
import random
import socket
import threading
import time

# ----------------------------------------------------------------------------
# CI-V protocol constants (source: Radioddity X6200 CI-V doc, V1.0.6)
# ----------------------------------------------------------------------------

PREAMBLE = 0xFE          # every frame starts with two of these
TERMINATOR = 0xFD        # every frame ends with exactly one of these
ADDR_RADIO = 0xA4        # the X6200's CI-V bus address
ADDR_CONTROLLER = 0x00   # "the PC" in the document; our knob uses it too

CODE_OK = 0xFB           # ICOM "FB" (fine business) acknowledgement
CODE_NG = 0xFA           # ICOM "NG" (no good) rejection

# Mode numbers from Table 3 of the document. Byte 1 is the mode, byte 2 is
# the data-mode flag (0x01 means USB-D / LSB-D style digital sub mode), and
# byte 3 is the filter slot (1, 2 or 3).
MODES = {
    0x00: "LSB",
    0x01: "USB",
    0x02: "AM",
    0x03: "CW",
    0x05: "NFM",
    0x07: "CWR",
}

# Band stacking register numbers from Table 4. 1=160M ... 0x0C=FM/AIR.
BANDS = {
    0x01: "160M", 0x02: "80M", 0x03: "60M", 0x04: "40M", 0x05: "30M",
    0x06: "20M", 0x07: "17M", 0x08: "15M", 0x09: "12M", 0x0A: "10M",
    0x0B: "6M", 0x0C: "FM/AIR",
}

# A convenient "middle of the band" frequency for each band register, used
# when a controller sends command 0x1A 0x01 (set band). The real radio
# recalls whatever was last used on that band; a simulator has no history,
# so we jump to a sensible calling-frequency-ish spot instead.
BAND_DEFAULT_FREQ = {
    0x01: 1_840_000, 0x02: 3_573_000, 0x03: 5_357_000, 0x04: 7_074_000,
    0x05: 10_136_000, 0x06: 14_074_000, 0x07: 18_100_000, 0x08: 21_074_000,
    0x09: 24_915_000, 0x0A: 28_074_000, 0x0B: 50_313_000, 0x0C: 118_000_000,
}


# ----------------------------------------------------------------------------
# BCD helpers. CI-V packs two decimal digits into each byte: the high nibble
# is the tens digit, the low nibble is the units digit. Frequencies put the
# LEAST significant pair first (little endian), which trips everyone up the
# first time.
# ----------------------------------------------------------------------------

def freq_to_bcd(hz: int) -> bytes:
    """Encode an integer frequency in Hz as 5 little-endian BCD bytes.

    Worked example straight from the Radioddity document, section 1:
    21,002,360 Hz -> 60 23 00 21 00.

    Walk through it: the decimal digits of 21002360, padded to ten
    digits, are 0 0 2 1 0 0 2 3 6 0 (from 1 GHz down to 1 Hz). Pair them
    from the right: (6,0)=0x60, (2,3)=0x23, (0,0)=0x00, (2,1)=0x21,
    (0,0)=0x00. That is exactly the byte order on the wire.
    """
    if not 0 <= hz <= 9_999_999_999:
        raise ValueError(f"frequency out of range: {hz}")
    out = bytearray()
    for _ in range(5):
        # Each byte carries the two lowest remaining decimal digits.
        units = hz % 10
        tens = (hz // 10) % 10
        out.append((tens << 4) | units)
        hz //= 100
    return bytes(out)


def bcd_to_freq(data: bytes) -> int:
    """Decode 5 little-endian BCD bytes back into an integer Hz value."""
    if len(data) != 5:
        raise ValueError(f"expected 5 BCD bytes, got {len(data)}")
    hz = 0
    # Walk from the most significant byte (last on the wire) down.
    for byte in reversed(data):
        tens = (byte >> 4) & 0x0F
        units = byte & 0x0F
        if tens > 9 or units > 9:
            raise ValueError(f"invalid BCD nibble in {data.hex()}")
        hz = hz * 100 + tens * 10 + units
    return hz


def level_to_bcd(value: int) -> bytes:
    """Encode a 0-255 level/meter value as the 2-byte BCD form CI-V uses.

    The document says levels and meters are "BCD format 0-255". On the wire
    that is two bytes holding the DECIMAL digits: 255 becomes 0x02 0x55,
    42 becomes 0x00 0x42. Note this is decimal-as-nibbles, not the raw
    binary value.
    """
    if not 0 <= value <= 255:
        raise ValueError(f"level out of range: {value}")
    hundreds = value // 100
    tens = (value % 100) // 10
    units = value % 10
    return bytes([hundreds, (tens << 4) | units])


def bcd_to_level(data: bytes) -> int:
    """Decode the 2-byte BCD level form back to an integer 0-255."""
    if len(data) != 2:
        raise ValueError(f"expected 2 BCD bytes, got {len(data)}")
    hundreds = data[0] & 0x0F
    tens = (data[1] >> 4) & 0x0F
    units = data[1] & 0x0F
    return hundreds * 100 + tens * 10 + units


# ----------------------------------------------------------------------------
# Radio state. One instance shared by all connections, protected by a lock,
# because a real radio also has exactly one VFO no matter how many cables
# you plug into it.
# ----------------------------------------------------------------------------

class RadioState:
    def __init__(self, freq: int, mode: int):
        self.lock = threading.Lock()

        # Two VFOs like the real radio. Index 0 is VFO-A, 1 is VFO-B.
        # "selected" says which one the tuning knob currently drives.
        self.vfo_freq = [freq, freq]
        self.vfo_mode = [mode, mode]        # mode number per Table 3
        self.vfo_data = [0x00, 0x00]        # data-mode flag per VFO
        self.selected = 0                   # 0 = VFO-A, 1 = VFO-B
        self.filter = 0x02                  # filter slot 1-3, shared per doc

        self.split = False
        self.attenuator = False
        self.preamp = False
        self.agc = 0x03                     # 0 off, 1 fast, 2 slow, 3 auto
        self.nb = False
        self.nr = False
        self.dnf = False
        self.comp = False
        self.locked = False
        self.ptt = False
        self.atu = 0x00                     # 00 off, 01 on, 02 tuning

        # Command 0x14 levels, all 0-255. Keys are the sub-command bytes.
        self.levels = {
            0x01: 128,  # AF volume
            0x02: 255,  # RF gain
            0x03: 0,    # squelch
            0x06: 128,  # NR level
            0x09: 96,   # CW sidetone (00=400Hz, 255=1200Hz)
            0x0A: 145,  # TX power (doc: 0=0.5W, 72=3W, 145=5W, 255=8W)
            0x0B: 128,  # hand mic gain
            0x0C: 77,   # keyer speed
            0x0D: 128,  # DNF centre frequency
            0x0F: 50,   # QSK hang time
            0x12: 0,    # NB level
            0x15: 64,   # MONI level
            0x19: 200,  # LCD backlight
        }

        # Meter values, animated by the background thread so the knob UI
        # has something alive to display. 0-255 scale per the document.
        self.s_meter = 30
        self.rf_meter = 0
        self.swr_meter = 20
        self.volt_meter = 180

    def band_register_for_freq(self) -> int:
        """Best-effort mapping of the current frequency to a Table 4 band."""
        mhz = self.vfo_freq[self.selected] / 1e6
        edges = [
            (0x01, 1.8, 2.0), (0x02, 3.5, 4.0), (0x03, 5.25, 5.45),
            (0x04, 7.0, 7.3), (0x05, 10.1, 10.15), (0x06, 14.0, 14.35),
            (0x07, 18.068, 18.168), (0x08, 21.0, 21.45),
            (0x09, 24.89, 24.99), (0x0A, 28.0, 29.7), (0x0B, 50.0, 54.0),
        ]
        for reg, lo, hi in edges:
            if lo <= mhz <= hi:
                return reg
        return 0x0C  # anything else lands in the FM/AIR register


# ----------------------------------------------------------------------------
# The command dispatcher. Takes one complete inbound frame (already stripped
# of preamble and terminator), returns the list of frames to send back.
# ----------------------------------------------------------------------------

def make_frame(payload: bytes) -> bytes:
    """Wrap a payload in radio-to-controller framing: FE FE 00 A4 ... FD."""
    return bytes([PREAMBLE, PREAMBLE, ADDR_CONTROLLER, ADDR_RADIO]) + payload + bytes([TERMINATOR])


def reply_ok() -> bytes:
    return make_frame(bytes([CODE_OK]))


def reply_ng() -> bytes:
    return make_frame(bytes([CODE_NG]))


def handle_command(state: RadioState, body: bytes, log) -> bytes:
    """Process one command body (cmd byte onward) and build the reply.

    `body` is everything between the source address and the terminator of
    the inbound frame: command byte, optional sub-command, optional data.
    Get-style commands answer by echoing the command (and sub-command) and
    appending the data, which mirrors how ICOM radios respond. Set-style
    commands answer OK or NG.
    """
    if not body:
        return reply_ng()

    cmd = body[0]
    rest = body[1:]

    with state.lock:
        # ---- 0x02: get receiver frequency range --------------------------
        # Doc: "Returned as Freq(dash)Freq in BCD". The dash is a literal
        # 0x2D byte between the two 5-byte BCD frequencies.
        if cmd == 0x02:
            lo = freq_to_bcd(500_000)         # placeholder RX range for the
            hi = freq_to_bcd(56_000_000)      # sim; VERIFY on real radio
            return make_frame(bytes([0x02]) + lo + bytes([0x2D]) + hi)

        # ---- 0x03: get active VFO frequency ------------------------------
        if cmd == 0x03:
            return make_frame(bytes([0x03]) + freq_to_bcd(state.vfo_freq[state.selected]))

        # ---- 0x07: VFO select / swap --------------------------------------
        if cmd == 0x07 and len(rest) >= 1:
            sub = rest[0]
            if sub == 0x00:
                state.selected = 0
            elif sub == 0x01:
                state.selected = 1
            elif sub == 0xB0:
                state.selected ^= 1
            else:
                return reply_ng()
            return reply_ok()

        # ---- 0x0F: split on/off -------------------------------------------
        if cmd == 0x0F and len(rest) >= 1:
            if rest[0] in (0x00, 0x01):
                state.split = bool(rest[0])
                return reply_ok()
            return reply_ng()

        # ---- 0x11: attenuator get/set --------------------------------------
        if cmd == 0x11:
            if not rest:  # get
                return make_frame(bytes([0x11, 0x01 if state.attenuator else 0x00]))
            if rest[0] in (0x00, 0x01):  # set
                state.attenuator = bool(rest[0])
                return reply_ok()
            return reply_ng()

        # ---- 0x14: levels get/set -------------------------------------------
        if cmd == 0x14 and len(rest) >= 1:
            sub = rest[0]
            if sub not in state.levels:
                return reply_ng()
            if len(rest) == 1:  # get: echo cmd+sub then the 2-byte BCD value
                return make_frame(bytes([0x14, sub]) + level_to_bcd(state.levels[sub]))
            try:  # set: data is the 2-byte BCD value
                state.levels[sub] = bcd_to_level(rest[1:3])
            except ValueError:
                return reply_ng()
            return reply_ok()

        # ---- 0x15: meters (read only) ----------------------------------------
        if cmd == 0x15 and len(rest) >= 1:
            sub = rest[0]
            meters = {
                0x02: state.s_meter,
                0x11: state.rf_meter,
                0x12: state.swr_meter,
                0x15: state.volt_meter,
            }
            if sub in meters:
                return make_frame(bytes([0x15, sub]) + level_to_bcd(meters[sub]))
            return reply_ng()

        # ---- 0x16: function switches get/set -----------------------------------
        if cmd == 0x16 and len(rest) >= 1:
            sub = rest[0]
            # Map each sub-command to a (getter, setter) pair on the state.
            if sub == 0x02:
                if len(rest) == 1:
                    return make_frame(bytes([0x16, sub, 0x01 if state.preamp else 0x00]))
                state.preamp = bool(rest[1])
                return reply_ok()
            if sub == 0x12:
                if len(rest) == 1:
                    return make_frame(bytes([0x16, sub, state.agc]))
                if rest[1] <= 0x03:
                    state.agc = rest[1]
                    return reply_ok()
                return reply_ng()
            simple = {0x22: "nb", 0x40: "nr", 0x41: "dnf", 0x44: "comp", 0x50: "locked"}
            if sub in simple:
                attr = simple[sub]
                if len(rest) == 1:
                    return make_frame(bytes([0x16, sub, 0x01 if getattr(state, attr) else 0x00]))
                setattr(state, attr, bool(rest[1]))
                return reply_ok()
            return reply_ng()

        # ---- 0x19 0x00: get radio CI-V address ----------------------------------
        if cmd == 0x19 and rest[:1] == b"\x00":
            return make_frame(bytes([0x19, 0x00, ADDR_RADIO]))

        # ---- 0x1A: band register, filter width, lock -----------------------------
        if cmd == 0x1A and len(rest) >= 1:
            sub = rest[0]
            if sub == 0x01:
                if len(rest) == 1:  # get: band byte then literal 0x02
                    return make_frame(bytes([0x1A, 0x01, state.band_register_for_freq(), 0x02]))
                band = rest[1]      # set: D1 is the band number 01-0C
                if band in BAND_DEFAULT_FREQ:
                    state.vfo_freq[state.selected] = BAND_DEFAULT_FREQ[band]
                    log(f"band change -> {BANDS[band]}")
                    return reply_ok()
                return reply_ng()
            if sub == 0x03:
                if len(rest) == 1:  # get IF filter width (Table 5 index)
                    return make_frame(bytes([0x1A, 0x03, 0x1F]))
                return reply_ok()   # accept a set silently
            if sub == 0x05 and rest[1:3] == b"\x00\x62":
                if len(rest) == 3:  # get lock status
                    return make_frame(bytes([0x1A, 0x05, 0x00, 0x62, 0x01 if state.locked else 0x00]))
                state.locked = bool(rest[3])
                return reply_ok()
            return reply_ng()

        # ---- 0x1C: PTT and antenna tuner --------------------------------------------
        if cmd == 0x1C and len(rest) >= 1:
            sub = rest[0]
            if sub == 0x00:  # PTT
                if len(rest) == 1:
                    return make_frame(bytes([0x1C, 0x00, 0x01 if state.ptt else 0x00]))
                state.ptt = bool(rest[1])
                # Fake some RF and SWR readings while "transmitting".
                state.rf_meter = 150 if state.ptt else 0
                state.swr_meter = 40 if state.ptt else 20
                return reply_ok()
            if sub == 0x01:  # ATU
                if len(rest) == 1:
                    return make_frame(bytes([0x1C, 0x01, state.atu]))
                if rest[1] in (0x00, 0x01):
                    state.atu = rest[1]
                    return reply_ok()
                if rest[1] == 0x02:  # tune request: doc says it turns ATU on
                    state.atu = 0x01
                    return reply_ok()
            return reply_ng()

        # ---- 0x1D 0x19: get model ID -----------------------------------------------
        # Doc says only "Get Xiegu model ID (6200)". The exact reply byte
        # layout is not spelled out; we return the digits as BCD 62 00.
        # VERIFY against the real radio.
        if cmd == 0x1D and rest[:1] == b"\x19":
            return make_frame(bytes([0x1D, 0x19, 0x62, 0x00]))

        # ---- 0x25: selected / unselected VFO frequency get/set ------------------------
        if cmd == 0x25 and len(rest) >= 1:
            sub = rest[0]
            if sub not in (0x00, 0x01):
                return reply_ng()
            # sub 0x00 targets the currently selected VFO, 0x01 the other one
            idx = state.selected if sub == 0x00 else state.selected ^ 1
            if len(rest) == 1:  # get
                return make_frame(bytes([0x25, sub]) + freq_to_bcd(state.vfo_freq[idx]))
            try:  # set
                state.vfo_freq[idx] = bcd_to_freq(rest[1:6])
            except ValueError:
                return reply_ng()
            log(f"VFO-{'AB'[idx]} freq -> {state.vfo_freq[idx]:,} Hz")
            return reply_ok()

        # ---- 0x26: selected / unselected VFO mode get/set -------------------------------
        if cmd == 0x26 and len(rest) >= 1:
            sub = rest[0]
            if sub not in (0x00, 0x01):
                return reply_ng()
            idx = state.selected if sub == 0x00 else state.selected ^ 1
            if len(rest) == 1:  # get: mode, data flag, filter (3 bytes)
                return make_frame(bytes([0x26, sub, state.vfo_mode[idx],
                                         state.vfo_data[idx], state.filter]))
            if len(rest) >= 4 and rest[1] in MODES and rest[3] in (1, 2, 3):
                state.vfo_mode[idx] = rest[1]
                state.vfo_data[idx] = 0x01 if rest[2] else 0x00
                state.filter = rest[3]  # doc: filter change applies to both VFOs
                log(f"VFO-{'AB'[idx]} mode -> {MODES[rest[1]]}"
                    f"{'-D' if rest[2] else ''} F{rest[3]}")
                return reply_ok()
            return reply_ng()

    # Anything not in the Radioddity table gets an NG, same as a real ICOM
    # style radio rejecting a command it does not implement.
    log(f"unimplemented command {body.hex(' ')}")
    return reply_ng()


# ----------------------------------------------------------------------------
# Frame extraction. CI-V is a byte stream, and TCP gives no message
# boundaries, so we hunt for FE FE ... FD sequences in a rolling buffer.
# ----------------------------------------------------------------------------

def extract_frames(buffer: bytearray):
    """Yield complete frame bodies (dest, src, cmd...) from the buffer.

    Mutates `buffer` in place, leaving any trailing partial frame for the
    next read. A "body" here is everything between the second preamble
    byte and the terminator.
    """
    while True:
        # Find the start of a frame. Anything before it is line noise.
        start = buffer.find(bytes([PREAMBLE, PREAMBLE]))
        if start == -1:
            buffer.clear()
            return
        if start > 0:
            del buffer[:start]
        end = buffer.find(bytes([TERMINATOR]))
        if end == -1:
            return  # frame not complete yet, wait for more bytes
        body = bytes(buffer[2:end])
        del buffer[:end + 1]
        if len(body) >= 3:  # need at least dest, src, cmd
            yield body


# ----------------------------------------------------------------------------
# Background "band activity" so the S-meter on the knob looks alive.
# ----------------------------------------------------------------------------

def activity_thread(state: RadioState, stop: threading.Event):
    signal_until = 0.0
    while not stop.wait(0.15):
        now = time.monotonic()
        with state.lock:
            if state.ptt:
                continue  # S-meter is meaningless while transmitting
            # Random chance a "station" keys up for a few seconds.
            if now > signal_until and random.random() < 0.02:
                signal_until = now + random.uniform(2.0, 8.0)
            target = random.randint(120, 220) if now < signal_until else random.randint(15, 45)
            # Ease toward the target so the needle moves, not teleports.
            state.s_meter += int((target - state.s_meter) * 0.3)
            state.s_meter = max(0, min(255, state.s_meter))
            # Battery voltage sags very slowly.
            if random.random() < 0.01:
                state.volt_meter = max(140, state.volt_meter - 1)


# ----------------------------------------------------------------------------
# TCP server plumbing.
# ----------------------------------------------------------------------------

def serve_client(conn: socket.socket, peer, state: RadioState, verbose: bool):
    def log(msg):
        print(f"[{peer[0]}:{peer[1]}] {msg}", flush=True)

    log("connected")
    buffer = bytearray()
    try:
        while True:
            data = conn.recv(256)
            if not data:
                break
            buffer.extend(data)
            for body in extract_frames(buffer):
                dest, src = body[0], body[1]
                if verbose:
                    log(f"rx fe fe {body.hex(' ')} fd")
                # Ignore traffic not addressed to this radio. Real CI-V is a
                # shared bus; frames for other addresses are simply ignored.
                if dest != ADDR_RADIO:
                    continue
                reply = handle_command(state, body[2:], log)
                if verbose:
                    log(f"tx {reply.hex(' ')}")
                conn.sendall(reply)
    except (ConnectionResetError, BrokenPipeError):
        pass
    finally:
        conn.close()
        log("disconnected")


def main():
    parser = argparse.ArgumentParser(description="Xiegu X6200 CI-V over TCP simulator")
    parser.add_argument("--host", default="0.0.0.0", help="bind address (default all interfaces)")
    parser.add_argument("--port", type=int, default=7373, help="TCP port (default 7373)")
    parser.add_argument("--freq", type=int, default=14_074_000, help="initial VFO frequency in Hz")
    parser.add_argument("--mode", default="USB", choices=list(MODES.values()),
                        help="initial mode (default USB)")
    parser.add_argument("--verbose", action="store_true", help="hex-dump every frame")
    args = parser.parse_args()

    mode_num = {v: k for k, v in MODES.items()}[args.mode]
    state = RadioState(args.freq, mode_num)

    stop = threading.Event()
    threading.Thread(target=activity_thread, args=(state, stop), daemon=True).start()

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((args.host, args.port))
    server.listen(4)
    print(f"X6200 simulator listening on {args.host}:{args.port} "
          f"(VFO {args.freq:,} Hz {args.mode})", flush=True)
    print("Point the knob firmware, or `nc`, or rigctl at this port.", flush=True)

    try:
        while True:
            conn, peer = server.accept()
            threading.Thread(target=serve_client, args=(conn, peer, state, args.verbose),
                             daemon=True).start()
    except KeyboardInterrupt:
        print("\nshutting down")
    finally:
        stop.set()
        server.close()


if __name__ == "__main__":
    main()
