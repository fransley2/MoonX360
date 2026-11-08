"""Read-only XBDM snapshot of title state, screen and MoonX360 reports.

This does not launch a title or start/stop a stream. Reports can be stale;
collection.json records the title loaded when they were fetched.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import socket
import struct
import zlib

from collect_latency_xbdm import FILES, getfile, read_exact, read_line


def command(console, name, multiline=False):
    with socket.create_connection((console, 730), timeout=5) as sock:
        sock.settimeout(8)
        greeting = read_line(sock)
        if not greeting.startswith(b'201'):
            raise OSError('Unexpected XBDM greeting')
        sock.sendall(name.encode('ascii') + b'\r\n')
        status = read_line(sock)
        if not status.startswith((b'200', b'202')):
            raise OSError(name + ': ' + status.decode('ascii', 'replace').strip())
        if not multiline:
            return status.decode('ascii', 'replace').strip()
        lines = []
        while True:
            line = read_line(sock)
            if line.strip(b'\r\n') == b'.':
                break
            lines.append(line.decode('ascii', 'replace').strip())
            if len(lines) > 256:
                raise ValueError('XBDM multiline response too long')
        return lines


def tiled_offset(x, y, width):
    """Return the four-byte Xenos surface texel index for x, y.

    XGAddress2DTiledOffset mapping; see OXDK xbox360/tools/xbdm/xbdm.py.
    """
    log_bpp = 2
    macro = ((x >> 5) + (y >> 5) * (((width + 31) & ~31) >> 5)) << (log_bpp + 7)
    micro = ((x & 7) + ((y & 6) << 2)) << log_bpp
    offset = macro + ((micro & ~15) << 1) + (micro & 15)
    offset += ((y & 8) << (3 + log_bpp)) + ((y & 1) << 4)
    return ((((offset & ~511) << 3) + ((offset & 448) << 2) + (offset & 63)
             + ((y & 16) << 7) + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)) >> log_bpp)


def png_chunk(name, payload):
    return (struct.pack('>I', len(payload)) + name + payload +
            struct.pack('>I', zlib.crc32(name + payload) & 0xffffffff))


def write_tiled_png(data, width, height, output):
    texels = len(data) // 4
    rows = []
    for y in range(height):
        row = bytearray(1 + width * 3)
        for x in range(width):
            offset = tiled_offset(x, y, width)
            if offset >= texels:
                raise ValueError('XBDM screenshot surface is incomplete')
            pixel = offset * 4
            dest = 1 + x * 3
            row[dest:dest + 3] = bytes((data[pixel + 2], data[pixel + 1], data[pixel]))
        rows.append(row)
    raw = b''.join(rows)
    output.write_bytes(b'\x89PNG\r\n\x1a\n' +
                       png_chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
                       png_chunk(b'IDAT', zlib.compress(raw, 6)) + png_chunk(b'IEND', b''))


def screenshot(console, output):
    with socket.create_connection((console, 730), timeout=5) as sock:
        sock.settimeout(20)
        if not read_line(sock).startswith(b'201'):
            raise OSError('Unexpected XBDM greeting')
        sock.sendall(b'screenshot\r\n')
        if not read_line(sock).startswith(b'203'):
            raise OSError('XBDM screenshot unavailable')
        header = read_line(sock).decode('ascii', 'replace')
        fields = {key: int(value, 16) for key, value in
                  re.findall(r'(pitch|width|height|framebuffersize|format)=0x([0-9a-fA-F]+)', header)}
        pitch, width, height, size, fmt = (fields[k] for k in
                                           ('pitch', 'width', 'height', 'framebuffersize', 'format'))
        if not (0 < width <= 4096 and 0 < height <= 2160 and
                width * 4 <= pitch <= 32768 and pitch * height <= size <= 32 * 1024 * 1024):
            raise ValueError('Invalid XBDM screenshot dimensions')
        if not fmt & 0x100:
            raise ValueError('Unsupported XBDM screenshot layout')
        payload = bytearray()
        while len(payload) < size:
            chunk = sock.recv(min(65536, size - len(payload)))
            if not chunk:
                raise OSError(f'XBDM screenshot closed after {len(payload)}/{size} bytes')
            payload.extend(chunk)
    data = bytes(payload)
    write_tiled_png(data, width, height, output)
    return {'width': width, 'height': height, 'sha256': hashlib.sha256(data).hexdigest()}


def collect(console, output, folder, try_screenshot=False):
    if output.exists():
        raise ValueError('Use a new output directory')
    if not folder.lower().startswith('hdd:\\apps\\moonx360\\moonlight'):
        raise ValueError('Expected MoonX360 diagnostic folder')
    output.mkdir(parents=True)
    result = {'console': console, 'collected_utc': datetime.now(timezone.utc).isoformat(),
              'exec_state': command(console, 'getexecstate'),
              'modules': command(console, 'modules', True), 'reports': {}}
    title_lines = [line for line in result['modules'] if 'base=0x82000000' in line]
    result['title_module'] = (re.search(r'name="([^"]+)"', title_lines[0]).group(1)
                              if title_lines else None)
    result['moonx360_name_match'] = bool(result['title_module'] and
                                         'moonx360' in result['title_module'].lower())
    if try_screenshot:
        try:
            result['screenshot'] = screenshot(console, output / 'screenshot.png')
        except (OSError, ValueError) as error:
            result['screenshot_error'] = str(error)
        result['exec_state_after_screenshot'] = command(console, 'getexecstate')
    for name in FILES:
        try:
            data = getfile(console, folder.rstrip('\\') + '\\' + name)
            (output / name).write_bytes(data)
            result['reports'][name] = {'bytes': len(data),
                                       'sha256': hashlib.sha256(data).hexdigest(),
                                       'fresh_session_proven': False}
        except (OSError, ValueError) as error:
            result['reports'][name] = {'error': str(error)}
    (output / 'collection.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('console')
    parser.add_argument('output', type=Path)
    parser.add_argument('--remote-folder', default='HDD:\\apps\\moonx360\\moonlight')
    parser.add_argument('--screenshot', action='store_true',
                        help='Attempt framebuffer capture; this XBDM may stop or truncate it')
    args = parser.parse_args()
    result = collect(args.console, args.output, args.remote_folder, args.screenshot)
    print(json.dumps({k: v for k, v in result.items() if k != 'modules'}, indent=2))
