"""Checks profile isolation and rebuilds after flag changes without needing the SDK."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


def load_build(profile):
    with patch.dict(os.environ, {"XDK_OPTIMIZATION": profile}):
        spec = importlib.util.spec_from_file_location("xdk_build", Path(__file__).parents[1] / "build.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module


class BuildTests(unittest.TestCase):
    def test_shared_header_changes_invalidate_objects(self):
        build = load_build("compatible")
        with tempfile.TemporaryDirectory() as folder:
            src = Path(folder) / "source.c"
            obj = Path(folder) / "source.obj"
            header = Path(folder) / "input_session.h"
            src.write_text("source")
            header.write_text("header")
            def compiler(cmd):
                obj.write_bytes(b"object")
                return 0, ""
            with patch.object(build.glob, "glob", return_value=[str(header)]), \
                 patch.object(build, "run", side_effect=compiler) as run:
                build.compile_one((str(src), str(obj), ["/O2"]))
                build.compile_one((str(src), str(obj), ["/O2"]))
                self.assertEqual(run.call_count, 1)
                old = header.stat().st_mtime
                os.utime(header, (old + 2, old + 2))
                build.compile_one((str(src), str(obj), ["/O2"]))
                self.assertEqual(run.call_count, 2)

    def test_profiles_preserve_crypto_and_system_flags(self):
        fast = load_build("aggressive")
        compatible = load_build("compatible")
        self.assertEqual(fast.BASE, compatible.BASE)
        self.assertIn("/O2", fast.BASE)
        self.assertIn("/Ox", fast.HOT)
        self.assertIn("/Ou", fast.HOT)
        self.assertIn("/Oc", fast.HOT)
        self.assertNotIn("/O2", fast.HOT)
        self.assertEqual(compatible.HOT, compatible.BASE)

    def test_flag_changes_and_failed_compiles_invalidate_cache(self):
        build = load_build("compatible")
        with tempfile.TemporaryDirectory() as folder:
            src = Path(folder) / "source.c"
            obj = Path(folder) / "source.obj"
            src.write_text("source")
            job = (str(src), str(obj), ["/O2"])

            def compiler(cmd):
                obj.write_bytes(b"object")
                return 0, ""

            with patch.object(build, "run", side_effect=compiler) as run:
                build.compile_one(job)
                build.compile_one(job)
                self.assertEqual(run.call_count, 1)
                build.compile_one((str(src), str(obj), ["/Ox", "/Ou", "/Oc"]))
                self.assertEqual(run.call_count, 2)
            with patch.object(build, "run", return_value=(1, "failure")):
                self.assertEqual(build.compile_one(job)[1], 1)
            self.assertFalse(Path(str(obj) + ".command.json").exists())
            with patch.object(build, "run", side_effect=compiler) as run:
                build.compile_one(job)
                self.assertEqual(run.call_count, 1)


if __name__ == "__main__":
    unittest.main()
