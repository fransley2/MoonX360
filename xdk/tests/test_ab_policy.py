"""Synthetic policy tests only; contains no Xbox measurement results."""
from pathlib import Path
import hashlib,importlib.util,json,os,sys,tempfile,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'xdk/scripts'))
import patch_worker_map as workers
from collect_ab_results import parse_report

class ABPolicyTests(unittest.TestCase):
    def test_both_exact_layouts_and_idempotence(self):
        original=(ROOT/'xdk/deps/XBMC-360/libraries/ffmpeg/libavcodec/xb_thread.c').read_text()
        workers.validate(original)
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder)/'xb_thread.c';p.write_text(original)
            for mode in ('previous','current','previous'):
                self.assertTrue(workers.patch(p,mode));self.assertTrue(workers.validate(p.read_text()))
                self.assertIn(workers.LAYOUTS[mode],p.read_text());first=p.read_bytes()
                self.assertFalse(workers.patch(p,mode));self.assertEqual(first,p.read_bytes())
            p.write_text(p.read_text()+'\n/* unknown version */');before=p.read_bytes()
            with self.assertRaises(RuntimeError):workers.patch(p,'current')
            self.assertEqual(before,p.read_bytes())
            with self.assertRaises(ValueError):workers.patch(p,'unknown')

    def test_link_rejects_wrong_layout_before_application_or_libraries(self):
        with tempfile.TemporaryDirectory() as folder,patch.dict(os.environ,{'XDK_BUILD_DIR':folder,'XDK_WORKER_MAP':'previous','XDK_OPTIMIZATION':'compatible'}):
            spec=importlib.util.spec_from_file_location('ab_native',ROOT/'xdk/build.py');b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
            ff=Path(folder)/'ffmpeg';ff.mkdir()
            h=hashlib.sha256((Path(b.FFMPEG)/'libavcodec/xb_thread.c').read_bytes()).hexdigest()
            (ff/'build-manifest.json').write_text(json.dumps({'profile':'compatible','worker_map':'current','worker_sha256':h}))
            with patch.object(b,'target_app',side_effect=AssertionError('must reject before compilation')):
                with self.assertRaisesRegex(SystemExit,'profile/worker patch changed'):b.target_xex()

    def test_parser_preserves_unavailable_and_does_not_invent_fps(self):
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder)/'decoder-session.txt';p.write_text('received=0\ndecoded=0\nshown=0\ndropped=0\ndecode_samples=2\ndecode_p99_us=0\n')
            r=parse_report(p);self.assertIsNone(r['callback_fps']);self.assertIsNone(r['decode_p99_us'])
            self.assertEqual(r['drops'],0);self.assertIsNone(r['packet_loss']);self.assertIsNone(r['idr_requests'])

    def test_parser_reports_rates_tail_scope_and_physical_refresh(self):
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder)/'decoder-session.txt';p.write_text('stream_duration_us=2000000\nreceived=120\ndecoded=118\nshown=116\nworker_map=previous\nworkers_requested=3\nworkers_active=3\nshader=FAST\ndisplay_hz=59.940\ndecode_samples=256\ndecode_p99_us=19000\npipeline_percentiles=rolling_256_samples; CPU_wall_times; overlay_required\n')
            r=parse_report(p);self.assertEqual((r['callback_fps'],r['decoded_fps'],r['shown_fps']),(60,59,58))
            self.assertEqual(r['decode_p99_us'],19000);self.assertEqual(r['physical_hz'],59.94)
            self.assertIn('rolling_256',r['percentile_scope']);self.assertIsNone(r['network_received_fps'])
            self.assertEqual(r['worker_map'],'previous');self.assertEqual(r['warnings'],[])

    def test_case_id_detects_wrong_or_stale_bitrate(self):
        with tempfile.TemporaryDirectory() as folder:
            case=Path(folder)/'P720-18-C4B-A1';case.mkdir()
            p=case/'decoder-session.txt';p.write_text('video=1280x720@60\nbitrate_kbps=20000\n')
            self.assertTrue(any('possible stale report' in w for w in parse_report(p)['warnings']))

if __name__=='__main__':unittest.main()
