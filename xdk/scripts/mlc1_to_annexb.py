"""Extract bounded, unchanged Annex-B access units from MoonX360 capture.bin."""
import argparse
import hashlib
from pathlib import Path
import struct

MAX_BYTES = 40 * 1024 * 1024


def extract(source: Path, output: Path) -> tuple[int, int, int, str]:
    if output.exists():
        raise ValueError('Refusing to overwrite an existing output')
    if source.stat().st_size > MAX_BYTES:
        raise ValueError('Capture exceeds MoonX360 40 MiB bound')
    data = source.read_bytes()
    if len(data) < 16 or data[:4] != b'MLC1':
        raise ValueError('Invalid MLC1 header')
    width, height, count = struct.unpack_from('>III', data, 4)
    if not (1 <= width <= 1920 and 1 <= height <= 1080 and 1 <= count <= 600):
        raise ValueError('Invalid geometry or frame count')
    offset = 16
    units = []
    for _ in range(count):
        if len(data) - offset < 4:
            raise ValueError('Truncated access-unit length')
        length, = struct.unpack_from('>I', data, offset)
        offset += 4
        if not (1 <= length <= 2 * 1024 * 1024) or length > len(data) - offset:
            raise ValueError('Invalid access-unit length')
        unit = data[offset:offset + length]
        if not unit.startswith((b'\0\0\1', b'\0\0\0\1')):
            raise ValueError('Access unit does not begin with an Annex-B start code')
        units.append(unit)
        offset += length
    if offset != len(data):
        raise ValueError('Trailing bytes after final access unit')
    output.write_bytes(b''.join(units))
    return width, height, count, hashlib.sha256(data).hexdigest()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    print('width=%d height=%d access_units=%d capture_sha256=%s' % extract(args.capture, args.output))
