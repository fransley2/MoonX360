"""Join bounded MoonX360 diagnostic windows by picture ID, never by unlike clocks.

Usage: python xdk/scripts/analyze_refresh_stability.py CAPTURE_DIRECTORY
Correlations are observations, not a network, decoder, quantization or Wi-Fi diagnosis.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path


def read_report(path):
    if not path.exists():
        return {}, []
    meta, rows, columns = {}, [], None
    for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
        if '=' in line:
            key, value = line.split('=', 1)
            meta[key] = value
            if key == 'columns':
                columns = next(csv.reader([value]))
        elif columns and line.strip():
            values = next(csv.reader([line]))
            if len(values) == len(columns):
                try:
                    rows.append(dict(zip(columns, map(int, values))))
                except ValueError:
                    pass
    return meta, rows


def distribution(values):
    values = sorted(values)
    if not values:
        return {'count': 0, 'p50': None, 'p95': None, 'p99': None}
    return {'count': len(values), **{
        f'p{p}': values[max(0, math.ceil(len(values) * p / 100) - 1)] for p in (50, 95, 99)}}


def analyze(folder):
    network, arrivals = read_report(folder / 'network-session.txt')
    decoder, pictures = read_report(folder / 'refresh-frame-trace.txt')
    _, presents = read_report(folder / 'present-trace.txt')
    arrivals = {f['frame'] & 0x7fffffff: f for f in arrivals}
    result = {
        'scope': 'bounded windows; Present calls are not scanout; correlation is not causation',
        'link': network.get('link', 'Unknown'),
        'host_path': 'unknown', 'exact_packet_loss_rate': None, 'ip_fragmentation': None,
        'udp_packets_per_second': network.get('udp_packets_per_second'),
        'idr_frequency_per_second': decoder.get('idr_inputs_per_second'),
        'source_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in
                          (folder / 'network-session.txt', folder / 'refresh-frame-trace.txt', folder / 'present-trace.txt') if p.exists()},
    }
    for kind, idr in [('idr', 1), ('non_idr', 0)]:
        group = [f for f in pictures if f.get('input_idr') == idr]
        matched = [arrivals[f['input_frame'] & 0x7fffffff] for f in group if f['input_frame'] & 0x7fffffff in arrivals]
        result[kind] = {
            'callback_samples': len(group), 'network_frame_matches': len(matched),
            'payload_bytes': distribution([f['bytes'] for f in group]),
            'decode_us': distribution([f['decode_us'] for f in group]),
            'predecode_queue_age_us': distribution([f['predecode_queue_age_us'] for f in group]),
            'accepted_packets_per_frame': distribution([f['accepted_data'] + f['accepted_fec'] for f in matched]),
            'expected_shards_per_frame': distribution([f['expected_data'] + f['expected_fec'] for f in matched]),
            'assembly_span_us': distribution([f['last_us'] - f['first_us'] for f in matched]),
            'present_interval_ending_at_frame_us': distribution([
                row['start_us'] - presents[i-1]['start_us'] for i, row in enumerate(presents)
                if i and row.get('idr') == idr and row['start_us'] >= presents[i-1]['start_us']]),
        }
    result['scene_quantization'] = 'unverified; sparse luma points cannot distinguish quantization, motion or corruption'
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path)
    print(json.dumps(analyze(parser.parse_args().folder), indent=2))
