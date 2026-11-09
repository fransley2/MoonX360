"""Launch an Xbox 360 title over XBDM and verify the title actually loaded.

XBDM's ``magicboot`` acknowledgement only confirms receipt of the command.
The module list and optional startup logs are the evidence of what ran.
This probe leaves the installed default.xex intact and can restore it after
testing a separate candidate file.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import socket
import struct
import time

from collect_latency_xbdm import getfile, read_exact, read_line
from xbox_snapshot_xbdm import command


ROOT = 'HDD:\\Apps\\MoonX360\\'
REPORTS = ('direct-launch.log', 'startup-stage.txt')


def checked_path(value):
    if not value.lower().startswith(ROOT.lower()) or not value.lower().endswith('.xex'):
        raise ValueError('Expected an XEX under HDD:\\Apps\\MoonX360')
    if any(char in value for char in ('"', '\r', '\n')):
        raise ValueError('Invalid XBDM path')
    return value


def current_title(console):
    lines = command(console, 'modules', True)
    matches = [line for line in lines if 'base=0x82000000' in line]
    if not matches:
        return None
    match = re.search(r'name="([^"]+)"', matches[0])
    return match.group(1) if match else None


def launch(console, path):
    checked_path(path)
    directory = path.rsplit('\\', 1)[0] + '\\'
    return command(console, 'magicboot title="' + path + '" directory="' + directory + '"')


def remote_size(console, path):
    directory, name = path.rsplit('\\', 1)
    for line in command(console, 'dirlist name="' + directory + '"', True):
        match = re.search(r'name="([^"]+)" sizehi=0x([0-9a-fA-F]+) sizelo=0x([0-9a-fA-F]+)', line)
        if match and match.group(1).lower() == name.lower():
            return (int(match.group(2), 16) << 32) | int(match.group(3), 16)
    return None


def upload_new(console, local, remote):
    checked_path(remote)
    if remote_size(console, remote) is not None:
        raise ValueError('Remote file already exists; refusing overwrite')
    if not local.is_file() or local.stat().st_size < 4096:
        raise ValueError('Local XEX missing or too small')
    digest = hashlib.sha256()
    with local.open('rb') as source:
        if source.read(4) != b'XEX2':
            raise ValueError('Local file is not XEX2')
        source.seek(0)
        with socket.create_connection((console, 730), timeout=5) as sock:
            sock.settimeout(30)
            if not read_line(sock).startswith(b'201'):
                raise OSError('Unexpected XBDM greeting')
            line = f'sendfile name="{remote}" length=0x{local.stat().st_size:x}\r\n'
            sock.sendall(line.encode('ascii'))
            status = read_line(sock)
            if not status.startswith(b'204'):
                raise OSError('XBDM sendfile: ' + status.decode('ascii', 'replace').strip())
            while chunk := source.read(65536):
                digest.update(chunk)
                sock.sendall(chunk)
            completion = read_line(sock)
            if not completion.startswith(b'200'):
                raise OSError('XBDM sendfile: ' + completion.decode('ascii', 'replace').strip())
    size = remote_size(console, remote)
    if size != local.stat().st_size:
        raise OSError('Remote size differs from local XEX')
    return {'local_bytes': local.stat().st_size, 'local_sha256': digest.hexdigest(),
            'remote_bytes': size, 'remote_hash_verified': False}


def hash_remote(console, remote):
    checked_path(remote)
    with socket.create_connection((console, 730), timeout=5) as sock:
        sock.settimeout(15)
        if not read_line(sock).startswith(b'201'):
            raise OSError('Unexpected XBDM greeting')
        sock.sendall(('getfile name="' + remote + '"\r\n').encode('ascii'))
        status = read_line(sock)
        if not status.startswith(b'203'):
            raise OSError('XBDM getfile: ' + status.decode('ascii', 'replace').strip())
        size = struct.unpack('<I', read_exact(sock, 4))[0]
        if not 4096 <= size <= 64 * 1024 * 1024:
            raise ValueError('Invalid remote XEX size')
        digest = hashlib.sha256()
        received = 0
        while received < size:
            chunk = sock.recv(min(65536, size - received))
            if not chunk:
                raise OSError(f'XBDM closed after {received}/{size} XEX bytes')
            digest.update(chunk)
            received += len(chunk)
    return {'bytes': size, 'sha256': digest.hexdigest()}


def observe(console, expected, seconds=12):
    observations = []
    start = time.monotonic()
    for delay in (1, 2, 3, 6):
        time.sleep(delay)
        elapsed = round(time.monotonic() - start, 3)
        try:
            title = current_title(console)
            observations.append({'after_seconds': elapsed, 'title': title})
        except (OSError, ValueError) as error:
            observations.append({'after_seconds': elapsed, 'error': str(error)})
        if elapsed >= seconds:
            break
    confirmed = bool(observations and (observations[-1].get('title') or '').lower() == expected.lower())
    return observations, confirmed


def startup_logs(console):
    result = {}
    for name in REPORTS:
        try:
            data = getfile(console, ROOT + 'moonlight\\' + name)
            result[name] = data.decode('utf-8', 'replace')[-6000:]
        except (OSError, ValueError) as error:
            result[name] = {'error': str(error)}
    return result


def probe(console, candidate, restore):
    result = {'console': console, 'collected_utc': datetime.now(timezone.utc).isoformat(),
              'candidate': candidate, 'expected': candidate.rsplit('\\', 1)[-1]}
    result['before_state'] = command(console, 'getexecstate')
    if result['before_state'] != '200- start':
        raise OSError('Console debugger is not running; resume before launching a title')
    result['before'] = current_title(console)
    try:
        result['remote_xex'] = hash_remote(console, candidate)
    except (OSError, ValueError) as error:
        result['remote_xex_error'] = str(error)
    try:
        result['launch_reply'] = launch(console, candidate)
        result['observations'], result['candidate_confirmed'] = observe(console, result['expected'])
        if not result['candidate_confirmed']:
            result['startup_logs_after_failure'] = startup_logs(console)
    finally:
        if restore:
            result['restore_reply'] = launch(console, restore)
            result['restore_observations'], result['restore_confirmed'] = observe(
                console, restore.rsplit('\\', 1)[-1])
            result['after_state'] = command(console, 'getexecstate')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('console')
    parser.add_argument('candidate', type=checked_path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--restore', type=checked_path)
    parser.add_argument('--upload-local', type=Path,
                        help='Upload to a new remote filename before launch; never overwrites')
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Use a new output file to preserve existing evidence')
    upload = upload_new(args.console, args.upload_local, args.candidate) if args.upload_local else None
    evidence = probe(args.console, args.candidate, args.restore)
    if upload:
        evidence['upload'] = upload
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({key: value for key, value in evidence.items()
                      if key not in ('startup_logs_after_failure',)}, indent=2))
