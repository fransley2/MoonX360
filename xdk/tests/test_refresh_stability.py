from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from analyze_refresh_stability import analyze


class RefreshReportTests(unittest.TestCase):
    def test_unknown_stays_unknown_and_different_windows_are_not_fabricated(self):
        with tempfile.TemporaryDirectory() as d:
            r = analyze(Path(d))
            self.assertIsNone(r['exact_packet_loss_rate'])
            self.assertIsNone(r['ip_fragmentation'])
            self.assertEqual(r['link'], 'Unknown')
            self.assertEqual(r['idr']['network_frame_matches'], 0)
            self.assertIsNone(r['idr']['decode_us']['p99'])

    def test_picture_identity_join_and_separate_idr_pipeline_samples(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            (p/'network-session.txt').write_text('link=Wired\ncolumns=frame,expected_data,expected_fec,accepted_data,accepted_fec,first_us,last_us\n10,10,2,9,1,100,700\n')
            (p/'refresh-frame-trace.txt').write_text('columns=input_frame,input_idr,bytes,decode_us,predecode_queue_age_us\n10,1,9000,24000,8000\n11,0,1000,10000,3000\n')
            (p/'present-trace.txt').write_text('columns=serial,start_us,idr\n1,50000,0\n2,75000,1\n3,100000,0\n')
            r = analyze(p)
            self.assertEqual(r['idr']['network_frame_matches'], 1)
            self.assertEqual(r['non_idr']['network_frame_matches'], 0)
            self.assertEqual(r['idr']['accepted_packets_per_frame']['p99'], 10)
            self.assertEqual(r['idr']['decode_us']['p99'], 24000)
            self.assertEqual(r['idr']['present_interval_ending_at_frame_us']['p99'], 25000)
            self.assertIsNone(r['exact_packet_loss_rate'])


if __name__ == '__main__':
    unittest.main()
