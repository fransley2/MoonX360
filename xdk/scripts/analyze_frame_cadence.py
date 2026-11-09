"""Summarize a bounded MoonX360 frame-cadence.txt without inferring host FPS."""
import argparse
import json
from pathlib import Path


def percentile(values, percent):
    ordered = sorted(values)
    return ordered[(len(ordered) * percent + 99) // 100 - 1] if ordered else None


def analyze(path):
    rows = []
    fields = {}
    for line in path.read_text(encoding='utf-8').splitlines():
        if line.startswith('frame_number,'):
            continue
        if ',' in line:
            parts = line.split(',')
            if len(parts) != 4:
                raise ValueError('Invalid frame row: ' + line)
            rows.append(tuple(int(value) for value in parts))
        elif '=' in line:
            key, value = line.split('=', 1)
            fields[key] = value
    if fields.get('schema') != '1' or fields.get('stage') != 'rtp_last_fec_block_complete_before_depacketizer':
        raise ValueError('Unsupported frame cadence schema/stage')
    if len(rows) != int(fields['count']) or len(rows) > 128:
        raise ValueError('Frame count outside the recorded bound')
    for before, after in zip(rows, rows[1:]):
        expected_interval = after[1] - before[1]
        expected_skips = max(0, after[0] - before[0] - 1)
        if expected_interval < 0 or after[2] != expected_interval or after[3] != expected_skips:
            raise ValueError('Inconsistent timestamps or frame ID gap')
    intervals = [row[2] for row in rows[1:]]
    elapsed = rows[-1][1] - rows[0][1] if len(rows) > 1 else 0
    return {
        'stage': fields['stage'],
        'capture_window_us': int(fields['window_us']),
        'tail_frame_count': len(rows),
        'first_frame_number': rows[0][0] if rows else None,
        'last_frame_number': rows[-1][0] if rows else None,
        'internal_skipped_ids': sum(row[3] for row in rows[1:]),
        'nonincreasing_id_pairs': sum(after[0] <= before[0] for before, after in zip(rows, rows[1:])),
        'interval_count': len(intervals),
        'interval_p50_us': percentile(intervals, 50),
        'interval_p95_us': percentile(intervals, 95),
        'interval_p99_us': percentile(intervals, 99),
        'intervals_13_to_20_ms': sum(13000 <= value <= 20000 for value in intervals),
        'intervals_28_to_38_ms': sum(28000 <= value <= 38000 for value in intervals),
        'intervals_other': sum(not (13000 <= value <= 20000 or 28000 <= value <= 38000) for value in intervals),
        'tail_span_fps': round((len(rows) - 1) * 1_000_000 / elapsed, 3) if elapsed else None,
        'interpretation_limit': 'Client RTP completion cadence only; host source/capture/encoder FPS is not inferred.',
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = json.dumps(analyze(args.trace), indent=2) + '\n'
    if args.output:
        args.output.write_text(result, encoding='utf-8')
    print(result, end='')
