#!/usr/bin/env python3
"""
Unit tests for the X6200 CI-V simulator.

Run with either of:
    python3 -m unittest test_x6200_sim.py -v
    python3 test_x6200_sim.py

These tests double as protocol documentation. Each test spells out the
exact bytes on the wire, so if you are new to CI-V you can learn the frame
format by reading the assertions. The ground truth for every byte pattern
is the Radioddity "CI-V implementation of Xiegu X6200" document, firmware
V1.0.6.
"""

import threading
import socket
import time
import unittest

from x6200_sim import (
    RadioState,
    handle_command,
    make_frame,
    extract_frames,
    freq_to_bcd,
    bcd_to_freq,
    level_to_bcd,
    bcd_to_level,
    ADDR_RADIO,
    ADDR_CONTROLLER,
    CODE_OK,
    CODE_NG,
)


def quiet_log(_msg):
    """The dispatcher wants a logger; tests do not want the noise."""


def cmd(state, *body_bytes):
    """Shorthand: run one command body through the dispatcher."""
    return handle_command(state, bytes(body_bytes), quiet_log)


class TestBcdCodec(unittest.TestCase):
    """The BCD helpers, checked against the document's own worked example."""

    def test_doc_worked_example(self):
        # Radioddity doc, section 1: 21,002,360 Hz on the wire is
        # 60 23 00 21 00. If this test fails, nothing else matters.
        self.assertEqual(freq_to_bcd(21_002_360), bytes.fromhex("6023002100"))

    def test_doc_worked_example_roundtrip(self):
        self.assertEqual(bcd_to_freq(bytes.fromhex("6023002100")), 21_002_360)

    def test_common_ham_frequencies_roundtrip(self):
        for hz in (1_840_000, 7_074_000, 14_074_000, 21_074_000,
                   28_500_123, 50_313_000, 145_500_000):
            self.assertEqual(bcd_to_freq(freq_to_bcd(hz)), hz, f"failed at {hz}")

    def test_rejects_garbage_bcd(self):
        # 0xAB has nibbles > 9, which is not a decimal digit.
        with self.assertRaises(ValueError):
            bcd_to_freq(bytes.fromhex("AB00000000"))

    def test_level_encoding(self):
        # "BCD format 0-255": 255 -> 02 55, 42 -> 00 42, 0 -> 00 00.
        self.assertEqual(level_to_bcd(255), bytes.fromhex("0255"))
        self.assertEqual(level_to_bcd(42), bytes.fromhex("0042"))
        self.assertEqual(level_to_bcd(0), bytes.fromhex("0000"))

    def test_level_roundtrip(self):
        for v in range(0, 256):
            self.assertEqual(bcd_to_level(level_to_bcd(v)), v)


class TestFraming(unittest.TestCase):
    """Frame extraction from a raw TCP byte stream."""

    def test_reply_framing(self):
        # Replies must be addressed controller-first: FE FE 00 A4 ... FD.
        frame = make_frame(bytes([0x03, 0x60, 0x23, 0x00, 0x21, 0x00]))
        self.assertEqual(frame[:4], bytes([0xFE, 0xFE, ADDR_CONTROLLER, ADDR_RADIO]))
        self.assertEqual(frame[-1], 0xFD)

    def test_extract_single_frame(self):
        buf = bytearray(bytes.fromhex("FEFE A400 03 FD".replace(" ", "")))
        bodies = list(extract_frames(buf))
        self.assertEqual(bodies, [bytes.fromhex("A40003")])
        self.assertEqual(len(buf), 0)

    def test_extract_partial_then_complete(self):
        # TCP can split a frame anywhere. Feed half, then the rest.
        buf = bytearray(bytes.fromhex("FEFEA400"))
        self.assertEqual(list(extract_frames(buf)), [])
        buf.extend(bytes.fromhex("03FD"))
        self.assertEqual(list(extract_frames(buf)), [bytes.fromhex("A40003")])

    def test_extract_skips_line_noise(self):
        buf = bytearray(bytes.fromhex("0011 FEFE A400 03 FD".replace(" ", "")))
        self.assertEqual(list(extract_frames(buf)), [bytes.fromhex("A40003")])

    def test_extract_two_frames_in_one_read(self):
        buf = bytearray(bytes.fromhex("FEFEA40003FD FEFEA4001900FD".replace(" ", "")))
        self.assertEqual(list(extract_frames(buf)),
                         [bytes.fromhex("A40003"), bytes.fromhex("A4001900")])


class TestCommands(unittest.TestCase):
    """The dispatcher, one documented command at a time."""

    def setUp(self):
        self.state = RadioState(freq=14_074_000, mode=0x01)  # 20m USB

    def test_read_frequency_cmd_03(self):
        reply = cmd(self.state, 0x03)
        # Expected: FE FE 00 A4 03 <5 BCD bytes> FD
        self.assertEqual(reply, make_frame(bytes([0x03]) + freq_to_bcd(14_074_000)))

    def test_set_then_read_frequency_cmd_25(self):
        # Set selected VFO to the doc's example frequency, then read it back
        # via both 0x25 and the legacy 0x03.
        reply = cmd(self.state, 0x25, 0x00, *freq_to_bcd(21_002_360))
        self.assertEqual(reply, make_frame(bytes([CODE_OK])))
        self.assertEqual(cmd(self.state, 0x25, 0x00),
                         make_frame(bytes([0x25, 0x00]) + freq_to_bcd(21_002_360)))
        self.assertEqual(cmd(self.state, 0x03),
                         make_frame(bytes([0x03]) + freq_to_bcd(21_002_360)))

    def test_unselected_vfo_is_independent(self):
        cmd(self.state, 0x25, 0x01, *freq_to_bcd(7_074_000))  # set VFO-B
        # VFO-A (selected) is untouched.
        self.assertEqual(cmd(self.state, 0x25, 0x00),
                         make_frame(bytes([0x25, 0x00]) + freq_to_bcd(14_074_000)))
        # VFO-B carries the new value.
        self.assertEqual(cmd(self.state, 0x25, 0x01),
                         make_frame(bytes([0x25, 0x01]) + freq_to_bcd(7_074_000)))

    def test_vfo_swap_cmd_07(self):
        cmd(self.state, 0x25, 0x01, *freq_to_bcd(7_074_000))
        self.assertEqual(cmd(self.state, 0x07, 0xB0), make_frame(bytes([CODE_OK])))
        # After the swap, the selected VFO reads back as the old VFO-B.
        self.assertEqual(cmd(self.state, 0x03),
                         make_frame(bytes([0x03]) + freq_to_bcd(7_074_000)))

    def test_set_and_read_mode_cmd_26(self):
        # Set CW with filter 1: mode 0x03, data flag 0x00, filter 0x01.
        reply = cmd(self.state, 0x26, 0x00, 0x03, 0x00, 0x01)
        self.assertEqual(reply, make_frame(bytes([CODE_OK])))
        self.assertEqual(cmd(self.state, 0x26, 0x00),
                         make_frame(bytes([0x26, 0x00, 0x03, 0x00, 0x01])))

    def test_data_mode_flag(self):
        # USB-D: mode 0x01 with data flag 0x01 (how FT8 setups run).
        cmd(self.state, 0x26, 0x00, 0x01, 0x01, 0x02)
        self.assertEqual(cmd(self.state, 0x26, 0x00),
                         make_frame(bytes([0x26, 0x00, 0x01, 0x01, 0x02])))

    def test_mode_rejects_bad_filter(self):
        self.assertEqual(cmd(self.state, 0x26, 0x00, 0x01, 0x00, 0x07),
                         make_frame(bytes([CODE_NG])))

    def test_s_meter_cmd_15_02(self):
        self.state.s_meter = 137
        self.assertEqual(cmd(self.state, 0x15, 0x02),
                         make_frame(bytes([0x15, 0x02]) + level_to_bcd(137)))

    def test_level_get_set_cmd_14(self):
        # Set AF volume to 200, read it back.
        self.assertEqual(cmd(self.state, 0x14, 0x01, *level_to_bcd(200)),
                         make_frame(bytes([CODE_OK])))
        self.assertEqual(cmd(self.state, 0x14, 0x01),
                         make_frame(bytes([0x14, 0x01]) + level_to_bcd(200)))

    def test_radio_id_cmd_19(self):
        self.assertEqual(cmd(self.state, 0x19, 0x00),
                         make_frame(bytes([0x19, 0x00, ADDR_RADIO])))

    def test_band_change_cmd_1a_01(self):
        # Jump to 40m (band register 4), frequency should land inside 40m.
        self.assertEqual(cmd(self.state, 0x1A, 0x01, 0x04, 0x00),
                         make_frame(bytes([CODE_OK])))
        freq = bcd_to_freq(cmd(self.state, 0x03)[5:10])
        self.assertTrue(7_000_000 <= freq <= 7_300_000, f"got {freq}")
        # And the band register read reports 40m with the literal 0x02.
        self.assertEqual(cmd(self.state, 0x1A, 0x01),
                         make_frame(bytes([0x1A, 0x01, 0x04, 0x02])))

    def test_ptt_cmd_1c(self):
        self.assertEqual(cmd(self.state, 0x1C, 0x00),
                         make_frame(bytes([0x1C, 0x00, 0x00])))
        self.assertEqual(cmd(self.state, 0x1C, 0x00, 0x01),
                         make_frame(bytes([CODE_OK])))
        self.assertEqual(cmd(self.state, 0x1C, 0x00),
                         make_frame(bytes([0x1C, 0x00, 0x01])))
        cmd(self.state, 0x1C, 0x00, 0x00)  # back to receive

    def test_unknown_command_gets_ng(self):
        # 0x33 is not in the Radioddity table.
        self.assertEqual(cmd(self.state, 0x33),
                         make_frame(bytes([CODE_NG])))


class TestOverTcp(unittest.TestCase):
    """One end-to-end smoke test through a real socket, exactly the path
    the knob firmware will use."""

    def test_freq_query_over_socket(self):
        import x6200_sim

        state = RadioState(freq=21_002_360, mode=0x01)
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("127.0.0.1", 0))  # port 0: the OS picks a free port
        server.listen(1)
        port = server.getsockname()[1]

        def one_client():
            conn, peer = server.accept()
            x6200_sim.serve_client(conn, peer, state, verbose=False)

        t = threading.Thread(target=one_client, daemon=True)
        t.start()

        client = socket.create_connection(("127.0.0.1", port), timeout=2)
        # Ask for the active VFO frequency: FE FE A4 00 03 FD
        client.sendall(bytes.fromhex("FEFEA40003FD"))
        reply = b""
        deadline = time.time() + 2
        while not reply.endswith(b"\xfd") and time.time() < deadline:
            reply += client.recv(64)
        client.close()
        server.close()

        # The doc's exact example reply: FE FE 00 A4 03 60 23 00 21 00 FD
        self.assertEqual(reply, bytes.fromhex("FEFE00A4036023002100FD"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
