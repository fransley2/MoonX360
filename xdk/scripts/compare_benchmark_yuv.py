"""Compare E's bounded Y/U/V frame hashes with identical-bitstream PC raw YUV."""
import argparse
import json
from pathlib import Path


def fnv1a32(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def compare(report: Path, raw: Path) -> dict:
    rows = report.read_text(encoding='utf-8').splitlines()
    fields = dict(line.split('=', 1) for line in rows if '=' in line)
    width, height, frames = (int(fields[key]) for key in ('width', 'height', 'frames'))
    if not (1 <= width <= 1920 and 1 <= height <= 1080 and 1 <= frames <= 600):
        raise ValueError('Invalid benchmark dimensions')
    data_rows = [row.split(',') for row in rows if row and row[0].isdigit() and ',' in row]
    if any(len(row) != 4 or int(row[0]) != index for index, row in enumerate(data_rows)):
        raise ValueError('Frame indices are incomplete or out of order')
    expected = [tuple(int(piece, 16) for piece in row[1:]) for row in data_rows]
    if len(expected) != frames or any(len(row) != 3 for row in expected):
        raise ValueError('Incomplete frame hash report')
    sizes = (width * height, ((width + 1) // 2) * ((height + 1) // 2))
    frame_bytes = sizes[0] + 2 * sizes[1]
    if raw.stat().st_size != frame_bytes * frames:
        raise ValueError('Raw YUV size differs from benchmark geometry and count')
    first = {'Y': None, 'U': None, 'V': None}
    differing = {'Y': 0, 'U': 0, 'V': 0}
    with raw.open('rb') as source:
        for frame in range(frames):
            for plane, size in enumerate((sizes[0], sizes[1], sizes[1])):
                digest = fnv1a32(source.read(size))
                name = 'YUV'[plane]
                if digest != expected[frame][plane]:
                    differing[name] += 1
                    if first[name] is None:
                        first[name] = frame
    return {'frames': frames, 'first_divergent_frame': first,
            'differing_frames': differing,
            'reference_capture_fingerprint': fields.get('capture_fingerprint')}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('decoder_frame_hashes', type=Path)
    parser.add_argument('reference_yuv420p', type=Path)
    args = parser.parse_args()
    print(json.dumps(compare(args.decoder_frame_hashes, args.reference_yuv420p), indent=2))
