"""Summarize the last 64 native G / v0.2.6 stage samples; never substitute for live FPS."""
import argparse
import csv
import json
from pathlib import Path


def percentile(values: list[float], percent: int) -> float:
    ordered = sorted(values)
    return ordered[(len(ordered) * percent + 99) // 100 - 1]


def summarize(values: list[float]) -> dict[str, float]:
    return {
        'mean': sum(values) / len(values),
        'p95': percentile(values, 95),
        'p99': percentile(values, 99),
        'max': max(values),
    }


def analyze(trace: Path, stage: Path) -> dict:
    fields = dict(line.split('=', 1) for line in stage.read_text().splitlines() if '=' in line)
    schema = int(fields.get('schema', 1))
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
    schema2_cols = {
        'reconstruct_wall', 'filter_wall', 'mc_ticks', 'idct_ticks', 'intra_ticks',
        'lf_cache_ticks', 'lf_filter_ticks', 'wavefront_wait_ticks',
        'dispatch_sync_ticks', 'worker_span_ticks',
    }
    has_schema2 = schema >= 2 or schema2_cols.issubset(columns)
    if schema >= 2 and not schema2_cols.issubset(columns):
        raise ValueError('Schema 2 trace lacks deep stage columns')
    samples = list(csv.DictReader((line for line in rows if line and line[0].isdigit()), fieldnames=columns))
    if not samples or len(samples) > 64:
        raise ValueError('Expected 1 to 64 frame rows')
    scale = 1_000_000 / frequency
    if not has_schema2:
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
        result = {'schema': schema, 'frames': len(samples),
                  'scope': 'last_trace_frames; CPU sum may exceed wall time if parallel',
                  'entropy_mean_sampled_ms_per_mb': sum(entropy) / len(entropy) if entropy else None}
        for name, values in timings.items():
            result[name + '_ms'] = summarize(values)
        return result

    wall = {name: [] for name in ('decode', 'reconstruct_wall', 'filter_wall', 'worker_span', 'dispatch_sync')}
    cpu = {name: [] for name in (
        'slice_cpu', 'entropy', 'mc', 'idct', 'intra', 'recon_other',
        'loopfilter_cpu', 'lf_cache', 'lf_filter', 'wavefront_wait',
    )}
    entropy_per_mb = []
    for row in samples:
        slice_ticks = int(row['slice_ticks'])
        filter_ticks = int(row['loopfilter_ticks'])
        entropy_ticks = int(row['entropy_sample_ticks'])
        reconstruct_wall = int(row['reconstruct_wall'])
        filter_wall = int(row['filter_wall'])
        mc_ticks = int(row['mc_ticks'])
        idct_ticks = int(row['idct_ticks'])
        intra_ticks = int(row['intra_ticks'])
        lf_cache_ticks = int(row['lf_cache_ticks'])
        lf_filter_ticks = int(row['lf_filter_ticks'])
        wavefront_wait_ticks = int(row['wavefront_wait_ticks'])
        dispatch_sync_ticks = int(row['dispatch_sync_ticks'])
        worker_span_ticks = int(row['worker_span_ticks'])

        if lf_cache_ticks + lf_filter_ticks > filter_ticks:
            raise ValueError('LF cache+filter ticks exceed loopfilter ticks')
        if entropy_ticks + mc_ticks + idct_ticks + intra_ticks > slice_ticks:
            raise ValueError('Reconstruction sub-stage ticks exceed slice ticks')

        wall['decode'].append(int(row['decode_us']) / 1000)
        wall['reconstruct_wall'].append(reconstruct_wall * scale / 1000)
        wall['filter_wall'].append(filter_wall * scale / 1000)
        wall['worker_span'].append(worker_span_ticks * scale / 1000)
        wall['dispatch_sync'].append(dispatch_sync_ticks * scale / 1000)

        cpu['slice_cpu'].append(slice_ticks * scale / 1000)
        cpu['entropy'].append(entropy_ticks * scale / 1000)
        cpu['mc'].append(mc_ticks * scale / 1000)
        cpu['idct'].append(idct_ticks * scale / 1000)
        cpu['intra'].append(intra_ticks * scale / 1000)
        inline_filter = filter_ticks if filter_wall == 0 else 0
        other_ticks = max(0, slice_ticks - entropy_ticks - mc_ticks - idct_ticks - intra_ticks - inline_filter)
        cpu['recon_other'].append(other_ticks * scale / 1000)
        cpu['loopfilter_cpu'].append(filter_ticks * scale / 1000)
        cpu['lf_cache'].append(lf_cache_ticks * scale / 1000)
        cpu['lf_filter'].append(lf_filter_ticks * scale / 1000)
        cpu['wavefront_wait'].append(wavefront_wait_ticks * scale / 1000)

        count = int(row['entropy_samples'])
        if count:
            entropy_per_mb.append(entropy_ticks * scale / count / 1000)

    return {
        'schema': schema,
        'clock': fields.get('clock', 'mftb32_ticks'),
        'frames': len(samples),
        'scope': 'last_trace_frames; wall=coordinator_critical_path; worker_cpu_sum=all_threads_may_exceed_wall',
        'entropy_mean_sampled_ms_per_mb': sum(entropy_per_mb) / len(entropy_per_mb) if entropy_per_mb else None,
        'decode_ms': summarize(wall['decode']),
        'slice_cpu_ms': summarize(cpu['slice_cpu']),
        'loopfilter_cpu_ms': summarize(cpu['loopfilter_cpu']),
        'wall_ms': {name: summarize(values) for name, values in wall.items()},
        'worker_cpu_sum_ms': {name: summarize(values) for name, values in cpu.items()},
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('h264_parallel_trace', type=Path)
    parser.add_argument('h264_stage_session', type=Path)
    args = parser.parse_args()
    print(json.dumps(analyze(args.h264_parallel_trace, args.h264_stage_session), indent=2))
