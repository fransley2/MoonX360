import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "host"))
from configure_360stream import ACTIVE, PLUGIN, apply, candidate


INI = b"\xef\xbb\xbf[Plugins]\r\nplugin1 = working-xbdm.xex\r\nplugin2 = usb-patch.xex\r\nplugin3 = \r\nplugin4 =\r\n[Settings]\r\nvalue=true\r\n"


class Files:
    def __init__(self, fail_promotion=False):
        self.files = {ACTIVE: INI}
        self.fail_promotion = fail_promotion

    def exists(self, path): return path in self.files
    def read(self, path): return self.files[path]
    def upload_new(self, path, data): self.files[path] = data
    def rename(self, old, new):
        if self.fail_promotion and old == "stage":
            raise OSError("simulated promotion failure")
        if new in self.files: raise OSError("destination exists")
        self.files[new] = self.files.pop(old)


class ConfigTests(unittest.TestCase):
    def test_only_empty_plugin3_changes_preserving_bom_and_crlf(self):
        after = candidate(INI)
        self.assertEqual(after.replace(PLUGIN, b""), INI)

    def test_occupied_or_duplicate_plugin_slot_refused(self):
        for data in (INI.replace(b"plugin3 = ", b"plugin3 = existing.xex"), INI.replace(b"plugin4 =", b"plugin3 =")):
            with self.assertRaises(ValueError): candidate(data)

    def test_verified_activation_keeps_remote_original(self):
        remote = Files()
        after = candidate(INI)
        apply(remote, INI, after, "backup", "stage", "failed")
        self.assertEqual(remote.files[ACTIVE], after)
        self.assertEqual(remote.files["backup"], INI)

    def test_failed_promotion_restores_original(self):
        remote = Files(fail_promotion=True)
        with self.assertRaisesRegex(OSError, "original INI restored"):
            apply(remote, INI, candidate(INI), "backup", "stage", "failed")
        self.assertEqual(remote.files[ACTIVE], INI)

    def test_concurrent_edit_refused_before_upload(self):
        remote = Files()
        remote.files[ACTIVE] += b"; user edit\r\n"
        with self.assertRaises(ValueError):
            apply(remote, INI, candidate(INI), "backup", "stage", "failed")
        self.assertNotIn("stage", remote.files)


if __name__ == "__main__":
    unittest.main()
