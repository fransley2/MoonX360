"""Directed checks using the real pinned xb_thread.c; no Xbox timing claims."""
from pathlib import Path
import importlib.util
import os
import re
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'xdk/scripts'))
import patch_worker_map as worker

def load_ffmpeg(profile, out):
    with patch.dict(os.environ, {'XDK_OPTIMIZATION':profile,'XDK_BUILD_DIR':str(out)}):
        spec=importlib.util.spec_from_file_location('ffmpeg_'+profile,ROOT/'xdk/scripts/build_ffmpeg.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        return module

class NativePolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text=(ROOT/'xdk/deps/XBMC-360/libraries/ffmpeg/libavcodec/xb_thread.c').read_text()
        worker.validate(cls.text)
        cls.original=cls.text.replace(worker.NEW,worker.OLD)

    def test_exact_patch_and_idempotence(self):
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder)/'xb_thread.c';p.write_text(self.original)
            self.assertTrue(worker.patch(p));first=p.read_bytes()
            self.assertFalse(worker.patch(p));self.assertEqual(first,p.read_bytes())
            self.assertEqual(p.read_text().count(worker.NEW),1)

    def test_unknown_duplicate_and_partial_versions_rejected_without_write(self):
        variants=[self.original+'\n/* unknown */',self.original.replace(worker.OLD,'#define hw_thread(x) (x+2)'),
                  self.original+'\n'+worker.OLD,self.original.replace(worker.OLD,worker.NEW)+'\n/* unknown */',
                  self.original.replace(worker.OLD,worker.NEW+'\n'+worker.OLD)]
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder)/'xb_thread.c'
            for text in variants:
                with self.subTest(version=text[-60:]):
                    p.write_text(text);before=p.read_bytes()
                    with self.assertRaises(RuntimeError):worker.patch(p)
                    self.assertEqual(p.read_bytes(),before)

    def test_actual_worker_indices_and_three_four_worker_selection(self):
        self.assertEqual(self.original.count('XSetThreadProcessor(hThread, hw_thread(i));'),2)
        mapping=[int(x) for x in re.search(r'xenon_worker_map\[5\] = \{([^}]+)\}',worker.NEW)[1].split(',')]
        self.assertEqual(mapping,[1,2,4,3,5])
        for count in (3,4):
            selected=[mapping[i%5] for i in range(count)]
            self.assertEqual(len(set(selected)),count)
            self.assertTrue(all(0<=x<6 and x!=5 for x in selected))
        video=(ROOT/'xdk/src/video_xdk.cpp').read_text()
        self.assertIn('options.decode_workers==3?3:4',video)

    def test_ffmpeg_profiles_have_separate_output_and_correct_flags(self):
        a=load_ffmpeg('aggressive',ROOT/'xdk/build/stability/aggressive')
        c=load_ffmpeg('compatible',ROOT/'xdk/build/stability/compatible')
        self.assertNotEqual(a.OUT,c.OUT)
        self.assertEqual(a.OPTIMIZATION,['/Ox','/Ou','/Oc'])
        self.assertEqual(c.OPTIMIZATION,['/O2'])
        self.assertNotIn('/O2',a.COMMON)
        for flag in ('/Ox','/Ou','/Oc'):self.assertNotIn(flag,c.COMMON)
        self.assertNotIn('test-support',a.ENV['INCLUDE'])
        self.assertNotIn('test-support',c.ENV['INCLUDE'])
        with self.assertRaises(SystemExit):load_ffmpeg('unknown',ROOT/'host-tests')

if __name__=='__main__':unittest.main()
