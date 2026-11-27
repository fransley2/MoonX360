#!/usr/bin/env python3
"""Unit tests for MoonX360 CRT Host Companion (mocked Win32 backend)."""

import json
import os
import tempfile
import unittest

import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import crt_host_companion as chc


class FakeBackend:
    def __init__(self, current: chc.DisplayMode, valid_keys=None):
        self.current = current
        self.valid_keys = valid_keys or {current.key(), (640, 240, 60, 32), (320, 240, 60, 32)}
        self.test_calls = []
        self.apply_calls = []

    def get_current_mode(self, device=None):
        return self.current

    def test_mode(self, mode: chc.DisplayMode) -> int:
        self.test_calls.append(mode.key())
        return chc.DISP_CHANGE_SUCCESSFUL if mode.key() in self.valid_keys else chc.DISP_CHANGE_BADMODE

    def apply_mode(self, mode: chc.DisplayMode) -> int:
        rc = self.test_mode(mode)
        if rc != chc.DISP_CHANGE_SUCCESSFUL:
            return rc
        self.apply_calls.append(mode.key())
        self.current = mode
        return chc.DISP_CHANGE_SUCCESSFUL


class TestCrtHostCompanion(unittest.TestCase):
    def setUp(self):
        self.profiles_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "profiles.example.json")
        self.profiles = chc.load_profiles(self.profiles_path)

    def test_example_profiles_loaded_with_four_layers(self):
        for name in [
            "crt_standard_480",
            "crt_retro_640x240",
            "crt_320x240",
            "crt_256x240",
            "crt_400x240",
            "crt_super_1920x240",
        ]:
            self.assertIn(name, self.profiles)
            summary = self.profiles[name].layer_summary()
            self.assertIn("A_game_resolution", summary["layers"])
            self.assertIn("B_capture_resolution", summary["layers"])
            self.assertIn("C_stream_resolution", summary["layers"])
            self.assertIn("D_xbox_output_resolution", summary["layers"])

    def test_dry_run_default_never_applies_or_writes_snapshot(self):
        with tempfile.TemporaryDirectory() as td:
            snap = os.path.join(td, "snap.json")
            backend = FakeBackend(chc.DisplayMode("\\\\.\\DISPLAY1", 1920, 1080, 60, 32))
            res = chc.switch_profile_display(
                backend,
                self.profiles["crt_retro_640x240"],
                snap,
                apply_live=False,
                register_atexit=False,
            )
            self.assertTrue(res["dry_run"])
            self.assertFalse(res["applied"])
            self.assertEqual(res["cds_test_rc"], chc.DISP_CHANGE_SUCCESSFUL)
            self.assertEqual(backend.apply_calls, [])
            self.assertEqual(backend.current.width, 1920)
            self.assertFalse(os.path.exists(snap))

    def test_apply_saves_snapshot_and_restore_roundtrip(self):
        with tempfile.TemporaryDirectory() as td:
            snap = os.path.join(td, "snap.json")
            initial = chc.DisplayMode("\\\\.\\DISPLAY1", 1920, 1080, 60, 32)
            backend = FakeBackend(initial)

            sw = chc.switch_profile_display(
                backend,
                self.profiles["crt_retro_640x240"],
                snap,
                apply_live=True,
                register_atexit=False,
            )
            self.assertTrue(sw["applied"])
            self.assertEqual(backend.current.width, 640)
            self.assertEqual(backend.current.height, 240)
            self.assertTrue(os.path.exists(snap))

            rest = chc.restore_display(backend, snap, apply_live=True)
            self.assertTrue(rest["restored"])
            self.assertEqual(backend.current.width, 1920)
            self.assertEqual(backend.current.height, 1080)

    def test_invalid_mode_rejected_by_cds_test_before_apply(self):
        with tempfile.TemporaryDirectory() as td:
            snap = os.path.join(td, "snap.json")
            initial = chc.DisplayMode("\\\\.\\DISPLAY1", 1920, 1080, 60, 32)
            backend = FakeBackend(initial)  # 256x240 not in valid_keys

            sw = chc.switch_profile_display(
                backend,
                self.profiles["crt_256x240"],
                snap,
                apply_live=True,
                register_atexit=False,
            )
            self.assertFalse(sw["applied"])
            self.assertEqual(sw["cds_test_rc"], chc.DISP_CHANGE_BADMODE)
            self.assertEqual(backend.apply_calls, [])
            self.assertFalse(os.path.exists(snap))

    def test_retroarch_override_generation(self):
        cfg = chc.build_retroarch_override(self.profiles["crt_super_1920x240"])
        self.assertIn('video_fullscreen_x = "1920"', cfg)
        self.assertIn('video_fullscreen_y = "240"', cfg)
        self.assertIn('video_scale_integer = "true"', cfg)
        self.assertIn('video_smooth = "false"', cfg)
        self.assertIn('crt_switch_resolution_super = "1920"', cfg)

    def test_sunshine_prep_cmd_defaults_to_dry_run(self):
        snippet = chc.build_sunshine_prep_cmd(
            self.profiles["crt_320x240"],
            "C:\\Tools\\crt_host_companion.py",
            self.profiles_path,
            "C:\\Tools\\snap.json",
            apply_live=False,
        )
        self.assertEqual(len(snippet["prep-cmd"]), 1)
        do_cmd = snippet["prep-cmd"][0]["do"]
        undo_cmd = snippet["prep-cmd"][0]["undo"]
        self.assertIn("--dry-run", do_cmd)
        self.assertIn("--dry-run", undo_cmd)
        self.assertIn("crt_320x240", do_cmd)
        # Ensure JSON serializable
        json.dumps(snippet)

    def test_external_switchres_handles_missing_and_dry_run(self):
        missing = chc.run_external_switchres(320, 240, 60, switchres_bin=None, apply_live=False)
        # Depending on host PATH, either available=False or available=True dry_run
        self.assertFalse(missing["executed"])
        explicit = chc.run_external_switchres(
            320,
            240,
            60,
            switchres_bin="C:\\nonexistent\\switchres.exe",
            apply_live=False,
        )
        self.assertTrue(explicit["available"])
        self.assertTrue(explicit["dry_run"])
        self.assertFalse(explicit["executed"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
