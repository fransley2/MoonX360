"""Collect allowlisted MoonX360 diagnostics via XBDM's read-only getfile command.

The SDK DmReceiveFileW call fails on some Aurora XBDM implementations even
though getfile works. Run after a session ends, with a new output directory.
No settings, pairing data, keys, or certificates are requested.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import socket
import struct


FILES = (
    'latency-capture.txt', 'fps-request-path.txt', 'frame-cadence.txt',
    'performance-session.txt', 'present-trace.txt',
    'decoder-session.txt', 'last-session.txt', 'last-log.txt',
    'h264-parallel-session.txt', 'h264-parallel-trace.txt', 'h264-stage-session.txt',
    'diagnostic-stage.txt', 'shutdown-stage.txt', 'connection-timing.txt',
    'last-video-stall.txt', 'gpu-cached-selftest.txt', 'vmx-selftest.txt',
    'capture-info.txt', 'decoder-benchmark.txt', 'decoder-frame-hashes.txt',
    'smoothing-session.txt', 'seamless-session.txt', 'network-session.txt', 'refresh-frame-trace.txt', 'console-environment.txt',
)
MAX_REPORT_BYTES = 1024 * 1024


def read_exact(sock, count):
    data = bytearray()
    while len(data) < count:
        chunk = sock.recv(min(count - len(data), 65536))
        if not chunk:
            raise OSError('XBDM closed during file transfer')
        data.extend(chunk)
    return bytes(data)


def read_line(sock):
    data = bytearray()
    while not data.endswith(b'\r\n'):
        if len(data) >= 512:
            raise ValueError('XBDM status line too long')
        data.extend(read_exact(sock, 1))
    return bytes(data)


def getfile(console, remote, max_bytes=MAX_REPORT_BYTES):
    with socket.create_connection((console, 730), timeout=5) as sock:
        sock.settimeout(10)
        greeting = read_line(sock)
        if not greeting.startswith(b'201'):
            raise OSError('Unexpected XBDM greeting: ' + repr(greeting))
        sock.sendall(b'getfile name="' + remote.encode('ascii') + b'"\r\n')
        status = read_line(sock)
        if not status.startswith(b'203'):
            raise OSError('XBDM getfile: ' + status.decode('ascii', 'replace').strip())
        length = struct.unpack('<I', read_exact(sock, 4))[0]
        if length > max_bytes:
            raise ValueError('Refusing oversized diagnostic file')
        return read_exact(sock, length)


def collect(console, remote_folder, output):
    if output.exists():
        raise ValueError('Use a new output directory to preserve prior evidence')
    if not remote_folder.lower().startswith('hdd:\\apps\\moonx360\\moonlight'):
        raise ValueError('Expected the current MoonX360 diagnostic folder')
    output.mkdir(parents=True)
    result = {
        'console': console, 'remote_folder': remote_folder,
        'collected_utc': datetime.now(timezone.utc).isoformat(), 'files': {},
    }
    for name in FILES:
        try:
            data = getfile(console, remote_folder.rstrip('\\') + '\\' + name)
            (output / name).write_bytes(data)
            result['files'][name] = {
                'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
            }
        except (OSError, ValueError) as error:
            result['files'][name] = {'error': str(error)}
    (output / 'collection.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('console')
    parser.add_argument('output', type=Path)
    parser.add_argument('--remote-folder', default='HDD:\\apps\\moonx360\\moonlight')
    args = parser.parse_args()
    result = collect(args.console, args.remote_folder, args.output)
    print(json.dumps(result['files'], indent=2))
