"""Summarize the last 64 native G stage samples; never substitute for live FPS."""
import argparse
import csv
import json
from pathlib import Path


def percentile(values: list[float], percent: int) -> float:
    ordered = sorted(values)
    return ordered[(len(ordered) * percent + 99) // 100 - 1]


def analyze(trace: Path, stage: Path) -> dict:
    fields = dict(line.split('=', 1) for line in stage.read_text().splitlines() if '=' in line)
    frequency = int(fields['qpc_frequency'])
    if frequency <= 0:
        raise ValueError('Invalid QPC frequency')
    rows = trace.read_text().splitlines()
    columns_line = next((line for line in rows if line.startswith('columns=')), None)
    if not columns_line:
        raise ValueError('Trace has no column header')
    columns = columns_line[8:].split(',')
    required = {'frame', 'decode_us', 'slice_ticks', 'loopfilter_ticks', 'entropy_sample_ticks', 'entropy_samples'}
    if not required.issubset(columns):
        raise ValueError('Trace lacks G stage columns')
    samples = list(csv.DictReader((line for line in rows if line and line[0].isdigit()), fieldnames=columns))
    if not samples or len(samples) > 64:
        raise ValueError('Expected 1 to 64 frame rows')
    scale = 1_000_000 / frequency
    timings = {name: [] for name in ('decode', 'slice_cpu', 'loopfilter_cpu', 'slice_nonfilter_cpu')}
    entropy = []
    for row in samples:
        slice_ticks = int(row['slice_ticks'])
        filter_ticks = int(row['loopfilter_ticks'])
        if filter_ticks > slice_ticks:
            raise ValueError('Filter ticks exceed enclosing slice ticks')
        timings['decode'].append(int(row['decode_us']) / 1000)
        timings['slice_cpu'].append(slice_ticks * scale / 1000)
        timings['loopfilter_cpu'].append(filter_ticks * scale / 1000)
        timings['slice_nonfilter_cpu'].append((slice_ticks - filter_ticks) * scale / 1000)
        count = int(row['entropy_samples'])
        if count:
            entropy.append(int(row['entropy_sample_ticks']) * scale / count / 1000)
    result = {'frames': len(samples), 'scope': 'last_trace_frames; CPU sum may exceed wall time if parallel',
              'entropy_mean_sampled_ms_per_mb': sum(entropy) / len(entropy) if entropy else None}
    for name, values in timings.items():
        result[name + '_ms'] = {'mean': sum(values) / len(values),
                                'p95': percentile(values, 95), 'p99': percentile(values, 99),
                                'max': max(values)}
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('h264_parallel_trace', type=Path)
    parser.add_argument('h264_stage_session', type=Path)
    args = parser.parse_args()
    print(json.dumps(analyze(args.h264_parallel_trace, args.h264_stage_session), indent=2))
