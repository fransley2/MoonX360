"""Verify and package a local candidate. No network, tag, commit or publication."""
from pathlib import Path
from datetime import datetime, timezone, timedelta
import csv
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
RC = ROOT/'build/release-candidate'
BUILD = RC/'toolchain'
EVIDENCE = RC/'evidence'
SDK = Path(os.environ.get('XEDK', r'C:\Program Files (x86)\Microsoft Xbox 360 SDK'))

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def copy(src, dst):
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.is_file() and sha(dst) != sha(src):
        backup = EVIDENCE/'superseded'/(dst.name+'.'+sha(dst))
        backup.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(dst, backup)
    shutil.copy2(src, dst)

checks = json.loads((EVIDENCE/'pc-results.json').read_text(encoding='utf-8'))
assert all(c['exit'] == 0 for c in checks), 'Fix failing PC checks before packaging'
host = json.loads((EVIDENCE/'host-results.json').read_text(encoding='utf-8'))
assert host and all(not c['error'] for c in host)
static = json.loads((EVIDENCE/'static-analysis.json').read_text())
assert static['exit'] == 0
assert not re.search(r'warning C6\d+', (EVIDENCE/'static-analysis.log').read_text(errors='replace')), 'Review static-analysis findings'
report=['# PC validation results — 2026-11-08','',
        'Physical status: **PENDING PHYSICAL XBOX 360 VALIDATION**. No console or live host was contacted.',
        '', 'The complete per-test commands, errors, components and timing are in `evidence/host-results.json` and `evidence/pc-results.json`. Timings describe test execution on the desktop, not Xbox performance.',
        '', '## Final desktop suites', '', '| Check | Environment | Result | Errors |','|---|---|---|---|']
for c in checks:
    report.append('| '+c['name']+' | '+c['environment']+' | '+c['result']+' | '+('None' if not c['exit'] else str(c['exit']))+' |')
report+=['','## Individual host fixtures','','| Test | Environment | Result | Components |','|---|---|---|---|']
for c in host:
    report.append('| '+c['name']+' | Desktop MSVC / simulated APIs | '+c['result']+' | '+', '.join(c['components'])+' |')
report+=['','## Unavailable and deferred checks','',
         '- NOT TESTED — TOOL UNAVAILABLE: GCC portable runner (`g++`/`gcc` are absent); equivalent desktop coverage uses MSVC.',
         '- NOT TESTED — TOOL UNAVAILABLE: whole-program Xenon analyzer / Xbox GPU emulator. Desktop `/analyze` is limited to configuration/persistence.',
         '- PENDING — PHYSICAL XBOX REQUIRED: exact-XEX launch, VMX execution/bit-exactness on Xenon, CPU/GPU/audio/input timing, real UDP/FEC/network behavior, live host compatibility, CRT/display output, corruption/stutter comparison and endurance.',
         '', 'All candidate-critical reproducible failures have been resolved. Earlier development-run failures remain in baseline/retry logs; they are not represented as passing runs.',
         '', 'The historical 2026-10 rebrand pixel reference differs from the pre-existing native dashboard and is not a stabilization reference. The release check uses the protected pre-edit snapshot and requires exact equality of every message/startup pixel. Historical comparison remains available without the baseline environment override.',
         '', 'No removed assertions or narrowed pixel masks were used to pass the release UI check. Stale fixture dependencies and permission expectations were updated to the actual current contracts.']
(ROOT/'docs/release-candidate/TEST_RESULTS.md').write_text('\n'.join(report)+'\n',encoding='utf-8')

binaries = []
app_command=json.loads((BUILD/'app/xdk_src_video_xdk.obj.command.json').read_text())
for definition in ('ADAPTIVE_FAST_DECODE=0','OPTIMIZED_YUV_COPY=0','DIAGNOSTIC_LEGACY_UPLOAD=0',
                   'XDK_H264_REFERENCE_MODE=0','XDK_H264_NO_RFI=0','XDK_H264_SINGLE_SLICE_REQUEST=0',
                   'XDK_SLICE_DISPATCH_BATCHED=0','XDK_WORKER_MAP_PREVIOUS=0','NDEBUG'):
    assert definition in app_command[1], 'Unexpected release configuration: '+definition
for name, folder, title in [('main', BUILD, '4D583336'), ('shortcut', BUILD/'MoonX360Shortcut', '4D583353')]:
    src = folder/'default.xex'
    data = src.read_bytes()
    assert data[:4] == b'XEX2'
    count = struct.unpack_from('>I', data, 20)[0]
    headers = dict(struct.unpack_from('>II', data, 24+i*8) for i in range(count))
    assert 0x103ff in headers and 0x2ff in headers  # imports and resource table
    assert data[headers[0x40006]+12:headers[0x40006]+16] == bytes.fromhex(title)
    exe = folder/('MoonX360.exe' if name == 'main' else 'MoonX360Shortcut.exe')
    pe = exe.read_bytes()
    pe_offset = struct.unpack_from('<I', pe, 60)[0]
    assert pe[pe_offset:pe_offset+4] == b'PE\0\0'
    assert struct.unpack_from('<H', pe, pe_offset+4)[0] == 0x1f2  # PowerPC big endian
    result = subprocess.run([str(SDK/'bin/win32/imagexex.exe'), '/dump', str(src)], capture_output=True)
    (EVIDENCE/(name+'-imagexex-dump.txt')).write_bytes(result.stdout+result.stderr)
    assert result.returncode == 0
    dump = (result.stdout+result.stderr).decode(errors='replace')
    assert 'xboxkrnl.exe' in dump
    if name == 'main':
        assert 'xam.xex' in dump
    stem = 'MoonX360' if name == 'main' else 'MoonX360Shortcut'
    assert (folder/(stem+'.spa')).read_bytes() == (folder/(stem+'-embedded.spa')).read_bytes()
    dst = RC/'default.xex' if name == 'main' else RC/'MoonX360Shortcut/default.xex'
    copy(src, dst)
    binaries.append(dict(name=name, path=str(dst.resolve()), package_path=dst.relative_to(RC).as_posix(),
                         bytes=dst.stat().st_size, sha256=sha(dst), title_id=title,
                         architecture='PowerPC big-endian (PE machine 0x01f2)',
                         xex2=True, imports_checked=True, embedded_spa_verified=True))

# Record warnings without conflating pre-existing warnings with new ones.
warnings = []
for p in sorted(BUILD.rglob('*.log')):
    for line in p.read_text(errors='replace').splitlines():
        if re.search(r'warning (?:C|LNK|IM)\d+', line):
            warnings.append(dict(log=p.relative_to(RC).as_posix(), message=line))
(EVIDENCE/'compiler-warnings.json').write_text(json.dumps(warnings, indent=2), encoding='utf-8')

# Exact local source provenance includes prior uncommitted changes. No SDK files.
names = subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'], cwd=ROOT).decode().split('\0')
names += [p.relative_to(ROOT).as_posix() for p in (ROOT/'test-support').rglob('*') if p.is_file()]
source_files = []
allowed_roots = ('src/', 'xdk/src/', 'xdk/deps/', 'xdk/compat/', 'xdk/ffmpeg_xbox/',
                 'xdk/scripts/', 'xdk/tests/', 'xdk/patches/', 'third_party/', 'assets/', 'tools/', 'test-support/', 'docs/release-candidate/')
for name in sorted(set(names)):
    p = ROOT/name
    if not name or not p.is_file():
        continue
    if not (name.startswith(allowed_roots) or (name.startswith('xdk/') and '/' not in name[4:]) or name in ('LICENSE','README.md','Makefile','.gitignore','.gitattributes','THIRD_PARTY_NOTICES.md','docs/BUILD.md','docs/DEPENDENCIES.md','docs/LIMITATIONS.md')):
        continue
    if p.suffix.lower() in ('.exe','.dll','.lib','.obj','.o','.xex','.pdb','.zip','.pem','.key','.crt','.pfx','.tsv','.ini','.pyc'):
        continue
    assert SDK.resolve() not in p.resolve().parents
    source_files.append(dict(path=name, bytes=p.stat().st_size, sha256=sha(p)))
(EVIDENCE/'source-hashes.json').write_text(json.dumps(source_files, indent=2), encoding='utf-8')
source_digest = hashlib.sha256(json.dumps(source_files, sort_keys=True).encode()).hexdigest()
manifest = dict(name='MoonX360 stabilization Fast Decode RC',
                status='RELEASE CANDIDATE — READY FOR XBOX TESTING',
                physical_status='PENDING PHYSICAL XBOX 360 VALIDATION', public_release_approved=False,
                build_date=datetime.now(timezone(timedelta(hours=-3))).isoformat(), build_timezone='America/Sao_Paulo (UTC-03:00)',
                git_revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT).decode().strip(),
                branch=subprocess.check_output(['git','branch','--show-current'],cwd=ROOT).decode().strip(),
                dirty_working_tree=True, source_manifest_sha256=source_digest,
                configuration='Release', optimization='aggressive', compiler='XDK Jul12 cl 16.10.16045.00',
                sdk_import_version='2.0.21256.0', worker_map='current', slice_dispatch='legacy',
                ffmpeg=json.loads((BUILD/'ffmpeg/build-manifest.json').read_text()),
                diagnostic_overrides=dict(adaptive_fast_decode=0,optimized_yuv_copy=0,legacy_upload=0,
                                          h264_reference_mode=0,no_rfi=0,single_slice_request=0,stage_profile=0),
                binaries=binaries, host_fixtures=len(host), pc_checks=checks, compiler_warning_count=len(warnings),
                limitations=['Full FAST can omit cross-slice reference filtering',
                             'Partial may provide no speed benefit on all-reference streams',
                             'Accurate decode can be slower than legacy discard-all',
                             'Existing factory presets may select experimental Full',
                             'No live host/console/GPU/audio/network measurements',
                             'No byte-identical relink claim; timestamps can differ'])
(RC/'manifest.json').write_text(json.dumps(manifest, indent=2, ensure_ascii=False), encoding='utf-8')

for p in (ROOT/'docs/release-candidate').glob('*.md'):
    copy(p, RC/'docs'/p.name)
copy(ROOT/'LICENSE', RC/'licenses/MoonX360-GPL-3.0.txt')
copy(ROOT/'THIRD_PARTY_NOTICES.md', RC/'THIRD_PARTY_NOTICES.md')
licenses = {'Moonlight':'xdk/deps/moonlight-common-c/LICENSE.txt', 'mbedTLS':'xdk/deps/mbedtls-2.16.12/apache-2.0.txt',
            'Opus':'xdk/deps/opus-1.5.2/COPYING', 'FFmpeg-LGPL':'xdk/deps/XBMC-360/libraries/ffmpeg/COPYING.LGPLv2.1',
            'FFmpeg-GPL':'xdk/deps/XBMC-360/libraries/ffmpeg/COPYING.GPLv2', 'Inter':'assets/fonts/Inter-LICENSE.txt',
            'FFmpeg-GPLv3':'xdk/deps/XBMC-360/libraries/ffmpeg/COPYING.GPLv3',
            'FFmpeg-LGPLv3':'xdk/deps/XBMC-360/libraries/ffmpeg/COPYING.LGPLv3',
            'FFmpeg-inventory':'xdk/deps/XBMC-360/libraries/ffmpeg/LICENSE',
            'FFmpeg-credits':'xdk/deps/XBMC-360/libraries/ffmpeg/CREDITS',
            'pthreads':'xdk/deps/XBMC-360/libraries/ffmpeg/vcproj/external/pthreads/COPYING',
            'pthreads-contributors':'xdk/deps/XBMC-360/libraries/ffmpeg/vcproj/external/pthreads/CONTRIBUTORS',
            'zlib':'xdk/deps/XBMC-360/libraries/ffmpeg/vcproj/external/zlib/README',
            'mbedTLS-selector':'xdk/deps/mbedtls-2.16.12/LICENSE',
            'mbedTLS-GPLv2':'xdk/deps/mbedtls-2.16.12/gpl-2.0.txt',
            'Opus-authors':'xdk/deps/opus-1.5.2/AUTHORS',
            'ENet':'xdk/deps/moonlight-common-c/enet/LICENSE', 'nanors':'xdk/deps/moonlight-common-c/nanors/LICENSE'}
for name, path in licenses.items():
    copy(ROOT/path, RC/('licenses/'+name+'.txt'))
stb = (ROOT/'third_party/stb/stb_truetype.h').read_text(encoding='utf-8')
(RC/'licenses/stb.txt').write_text(stb[stb.rindex('This software is available under 2 licenses'):], encoding='utf-8')
for p in (BUILD/'MoonX360/AuroraScripts').rglob('*'):
    if p.is_file():
        copy(p, RC/'AuroraScripts'/p.relative_to(BUILD/'MoonX360/AuroraScripts'))
with (RC/'hardware-cases.csv').open('w', newline='', encoding='utf-8') as f:
    writer = csv.writer(f)
    writer.writerow(['case','resolution','fps','mbps','mode','status','notes'])
    for w,h,fps,mbps in [(640,480,30,6),(640,480,60,9),(1280,720,30,6),(1280,720,60,12),(1920,1080,24,8)]:
        for mode in ('Off','Partial','Full'):
            writer.writerow([f'{w}x{h}-{fps}-{mbps}-{mode}',f'{w}x{h}',fps,mbps,mode,'PENDING PHYSICAL XBOX 360 VALIDATION',''])

sources_zip = RC/'MoonX360-stabilization-rc-sources.zip'
with zipfile.ZipFile(sources_zip, 'w', zipfile.ZIP_DEFLATED) as z:
    for entry in source_files:
        z.write(ROOT/entry['path'], entry['path'])
with zipfile.ZipFile(sources_zip) as z:
    assert z.testzip() is None
allowed = {'default.xex','manifest.json','hardware-cases.csv','THIRD_PARTY_NOTICES.md'}
package_files = sorted(p for p in RC.rglob('*') if p.is_file() and
                       (p.relative_to(RC).as_posix() in allowed or p.relative_to(RC).parts[0] in ('docs','licenses','AuroraScripts','MoonX360Shortcut')))
assert all(p.suffix.lower() not in ('.lib','.obj','.pdb','.pem','.key','.ini','.tsv','.crt','.exe','.dll') for p in package_files)
archive = RC/'MoonX360-stabilization-rc.zip'
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
    for p in package_files:
        z.write(p,p.relative_to(RC))
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
(RC/'SHA256SUMS.txt').write_text(''.join(sha(p)+'  '+p.relative_to(RC).as_posix()+'\n' for p in [RC/'default.xex', RC/'MoonX360Shortcut/default.xex', archive, sources_zip]), encoding='ascii')
for record in json.loads((EVIDENCE/'baseline-binaries.json').read_text()):
    assert sha(Path(record['path'])) == record['sha256'], 'Protected previous XEX changed'
print(json.dumps(dict(status=manifest['status'], binaries=binaries, package=str(archive), sources=str(sources_zip)), indent=2, ensure_ascii=False))
