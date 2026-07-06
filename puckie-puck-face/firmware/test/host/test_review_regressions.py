#!/usr/bin/env python3
"""Static regression checks for PR #3 review findings.

The affected firmware modules are ESP32/LVGL-bound, so these checks pin the
specific source-level contracts from the review until there is a fuller host
simulation layer for radio_client/ui internals.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "src"
INCLUDE = ROOT / "include"
SIM = ROOT.parent / "simulator"


def text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def block(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    finish = source.index(end, begin)
    return source[begin:finish]


class ReviewRegressionChecks(unittest.TestCase):
    def setUp(self):
        self.radio = text(SRC / "radio_client.cpp")
        self.ui = text(SRC / "ui.cpp")
        self.net_cpp = text(SRC / "net.cpp")
        self.net_h = text(SRC / "net.h")
        self.config = text(INCLUDE / "config.h")
        self.profile_h = text(SRC / "radio_profile.h")
        self.profile_cpp = text(SRC / "radio_profile.cpp")
        self.sim = text(SIM / "x6200_sim.py")

    def test_failed_control_intents_are_rearmed_with_values(self):
        cases = [
            ("if (preampValid)", "if (attenuatorValid)", ["pendingPreampValid = true;", "pendingPreamp = preamp;"]),
            ("if (attenuatorValid)", "if (agcValid)", ["pendingAttenuatorValid = true;", "pendingAttenuator = attenuator;"]),
            ("if (agcValid)", "if (tunerValid)", ["pendingAgcValid = true;", "pendingAgc = agc;"]),
            ("if (tunerValid)", "return true;", ["pendingTunerValid = true;", "pendingTunerCommand = tunerCommand;"]),
        ]
        for start, end, required in cases:
            with self.subTest(start=start):
                section = block(self.radio, start, end)
                for snippet in required:
                    self.assertIn(snippet, section)

    def test_mode_filter_updates_and_survives_failed_send(self):
        mode_section = block(self.radio, "if (modeValid)", "if (preampValid)")
        self.assertIn("pendingFilter = filter;", mode_section)
        self.assertIn("state.filter = filter;", mode_section)

    def test_control_poll_failure_marks_link_unhealthy(self):
        health_section = block(
            self.radio,
            "controlPollPhase = (controlPollPhase + 1) % 7;",
            "vTaskDelay(pdMS_TO_TICKS(RADIO_POLL_INTERVAL_MS));",
        )
        self.assertRegex(health_section, r"if\s*\(\s*ok\s*&&\s*controlOk\s*\)")
        self.assertNotRegex(health_section, r"if\s*\(\s*ok\s*\)\s*\{")

    def test_pota_dead_code_is_removed_from_firmware(self):
        joined = "\n".join([self.net_cpp, self.net_h, self.config, self.radio])
        for forbidden in ("PotaData", "PotaSpot", "refreshPota", "net::pota", "POTA_"):
            self.assertNotIn(forbidden, joined)

    def test_band_names_come_from_profile(self):
        self.assertNotRegex(self.ui, r"\bbandNameForFreq\s*\(")
        self.assertIn("bandNameForFrequency", self.profile_h)
        self.assertIn("bandNameForFrequency", self.profile_cpp)
        self.assertIn("profile::bandNameForFrequency(s.freqHz)", self.ui)

    def test_tuning_tick_refresh_only_restyles_changed_tick(self):
        self.assertIn("lastActiveTuningTick", self.ui)
        self.assertIn("styleTuningTick(", self.ui)
        refresh = block(self.ui, "void refreshRadio()", "void refreshClock()")
        self.assertNotRegex(refresh, r"for\s*\([^)]*kTuningTickCount[^)]*\)\s*\{[^}]*lv_obj_set_style_line_color")

    def test_grayline_buffer_is_not_internal_bss(self):
        self.assertNotRegex(self.ui, r"uint8_t\s+grayCanvasBuf\s*\[")
        self.assertIn("heap_caps_malloc", self.ui)
        self.assertIn("MALLOC_CAP_SPIRAM", self.ui)

    def test_simulator_connection_eviction_is_opt_in(self):
        self.assertIn("--single-client-per-ip", self.sim)
        self.assertIn("--idle-timeout", self.sim)
        self.assertRegex(self.sim, r"default=0(?:\.0)?")


if __name__ == "__main__":
    unittest.main(verbosity=2)
