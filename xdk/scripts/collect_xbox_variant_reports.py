"""Read-only XBDM collection from one verified Aurora MoonX360 variant folder.

Example: python xdk/scripts/collect_xbox_variant_reports.py 192.168.15.10
  "G - stage profile" evidencias/G-20261106T220000Z

No credentials, pairing files, capture video, or configuration are fetched.
Run only after leaving the stream so teardown reports have been written.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re

from collect_latency_xbdm import FILES, getfile
from xbox_snapshot_xbdm import command

BASE = r'HDD:\Apps\MoonX360'
REPORTS = tuple(dict.fromkeys((*FILES, 'performance-menu.txt')))
ARCHIVE_REPORTS = (
    'last-session.txt', 'decoder-session.txt', 'performance-session.txt',
    'present-trace.txt', 'last-log.txt', 'h264-parallel-session.txt',
    'h264-parallel-trace.txt', 'h264-stage-session.txt',
    'latency-capture.txt', 'frame-cadence.txt',
)


def collect(console: str, variant: str, output: Path) -> dict:
    if not re.fullmatch(r'[A-Za-z0-9 _-]{1,64}', variant) or variant in ('.', '..'):
        raise ValueError('Variant must be one simple Aurora folder name')
    if output.exists():
        raise ValueError('Use a new output directory to preserve raw evidence')
    root = BASE + '\\' + variant
    folder = root + r'\moonlight'
    root_list = command(console, 'dirlist name="' + root + '"', True)
    if not any('name="default.xex"' in line and 'directory' not in line for line in root_list):
        raise ValueError('Selected folder has no default.xex')
    report_list = command(console, 'dirlist name="' + folder + '"', True)
    available = set(re.findall(r'name="([^"]+)"', '\n'.join(report_list)))
    archives = tuple(sorted(
        name for name in available
        if any(name == f'session-archive-{slot}-{report}'
               for slot in range(1, 6)
               for report in (*ARCHIVE_REPORTS, 'index.txt'))))
    if 'session-archive-next.txt' in available:
        archives += ('session-archive-next.txt',)
    output.mkdir(parents=True)
    result = {
        'collected_utc': datetime.now(timezone.utc).isoformat(),
        'console': console, 'variant_root': root, 'report_folder': folder,
        'exec_state': command(console, 'getexecstate'),
        'modules': command(console, 'modules', True),
        'root_listing': root_list, 'report_listing': report_list,
        'reports': {},
    }
    try:
        for name in (*REPORTS, *archives):
            if name not in available:
                result['reports'][name] = {'listed': False, 'status': 'absent_in_dirlist'}
                continue
            try:
                data = getfile(console, folder + '\\' + name)
                (output / name).write_bytes(data)
                result['reports'][name] = {
                    'listed': True, 'bytes': len(data),
                    'sha256': hashlib.sha256(data).hexdigest(),
                }
            except (OSError, ValueError) as error:
                result['reports'][name] = {
                    'listed': True, 'status': 'getfile_failed', 'error': str(error),
                }
    finally:
        (output / 'collection.json').write_text(
            json.dumps(result, indent=2, ensure_ascii=True) + '\n', encoding='utf-8')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('console')
    parser.add_argument('variant', help='Exact folder name under HDD:\\Apps\\MoonX360')
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = collect(args.console, args.variant, args.output)
    print(json.dumps({'output': str(args.output), 'reports': result['reports']}, indent=2))
