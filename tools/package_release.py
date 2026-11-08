"""Package an already validated dashboard build and its exact Git source revision."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import zipfile

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--version', required=True)
    parser.add_argument('--revision', required=True)
    parser.add_argument('--validation', required=True, type=Path)
    args = parser.parse_args()
    assert re.fullmatch(r'v\d+\.\d+\.\d+', args.version)
    revision = subprocess.check_output(['git', 'rev-parse', args.revision], cwd=ROOT).decode().strip()
    validation = json.loads(args.validation.read_text(encoding='utf-8'))
    assert validation['passed'] and validation['failures'] == []
    build = args.build.resolve()
    dist = ROOT/'dist'
    destination = dist/('MoonX360-'+args.version)
    assert not destination.exists(), 'Use a fresh package directory; never merge with previous files'
    install = destination/'MoonX360'
    install.mkdir(parents=True)

    def copy(src, name):
        target = install/name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, target)

    binaries = []
    sdk = Path(__import__('os').environ.get('XEDK', r'C:\Program Files (x86)\Microsoft Xbox 360 SDK'))
    for folder, stem, title, target in [
        (build, 'MoonX360', '4D583336', 'default.xex'),
        (build/'MoonX360Shortcut', 'MoonX360Shortcut', '4D583353', 'MoonX360Shortcut/default.xex'),
    ]:
        source = folder/'default.xex'
        data = source.read_bytes()
        assert data[:4] == b'XEX2'
        headers = dict(struct.unpack_from('>II', data, 24+i*8)
                       for i in range(struct.unpack_from('>I', data, 20)[0]))
        assert 0x103ff in headers and 0x2ff in headers
        assert data[headers[0x40006]+12:headers[0x40006]+16] == bytes.fromhex(title)
        assert (folder/(stem+'.spa')).read_bytes() == (folder/(stem+'-embedded.spa')).read_bytes()
        result = subprocess.run([str(sdk/'bin/win32/imagexex.exe'), '/dump', str(source)], capture_output=True, check=True)
        dump = (result.stdout+result.stderr).decode(errors='replace')
        assert 'xboxkrnl.exe' in dump
        if stem == 'MoonX360':
            assert 'xam.xex' in dump
        copy(source, target)
        binaries.append(dict(path=target, sha256=sha(source), bytes=len(data), title_id=title,
                             xex2=True, imports_checked=True, embedded_spa_verified=True))

    wallpaper = ROOT/'assets/wallpapers/home-wallpaper.png'
    with Image.open(wallpaper) as im:
        assert im.format == 'PNG' and 0 < im.width <= 2048 and 0 < im.height <= 2048
        dimensions = list(im.size)
        im.verify()
    assert wallpaper.stat().st_size <= 8*1024*1024
    copy(wallpaper, 'Wallpapers/home-wallpaper.png')
    copy(ROOT/'assets/wallpapers/README.md', 'Wallpapers/README.md')
    copy(ROOT/f'docs/RELEASE_NOTES_{args.version}.md', 'README.md')
    copy(ROOT/'THIRD_PARTY_NOTICES.md', 'THIRD_PARTY_NOTICES.md')
    for name in ('AURORA_DIRECT_LAUNCH.md', 'ARTWORK_CACHE_AURORA_METADATA.md', 'SETUP.md', 'LIMITATIONS.md'):
        copy(ROOT/'docs'/name, 'docs/'+name)
    licenses = {
        'MoonX360-GPL-3.0': 'LICENSE',
        'Moonlight': 'xdk/deps/moonlight-common-c/LICENSE.txt',
        'mbedTLS': 'xdk/deps/mbedtls-2.16.12/apache-2.0.txt',
        'mbedTLS-selector': 'xdk/deps/mbedtls-2.16.12/LICENSE',
        'mbedTLS-GPLv2': 'xdk/deps/mbedtls-2.16.12/gpl-2.0.txt',
        'Opus': 'xdk/deps/opus-1.5.2/COPYING',
        'Opus-authors': 'xdk/deps/opus-1.5.2/AUTHORS',
        'Inter': 'assets/fonts/Inter-LICENSE.txt',
        'ENet': 'xdk/deps/moonlight-common-c/enet/LICENSE',
        'nanors': 'xdk/deps/moonlight-common-c/nanors/LICENSE',
    }
    ffroot = 'xdk/deps/XBMC-360/libraries/ffmpeg/'
    licenses.update({name: ffroot+path for name, path in {
        'FFmpeg-LGPL': 'COPYING.LGPLv2.1', 'FFmpeg-GPL': 'COPYING.GPLv2',
        'FFmpeg-GPLv3': 'COPYING.GPLv3', 'FFmpeg-LGPLv3': 'COPYING.LGPLv3',
        'FFmpeg-inventory': 'LICENSE', 'FFmpeg-credits': 'CREDITS',
        'pthreads': 'vcproj/external/pthreads/COPYING',
        'pthreads-contributors': 'vcproj/external/pthreads/CONTRIBUTORS',
        'zlib': 'vcproj/external/zlib/README',
    }.items()})
    for name, path in sorted(licenses.items()):
        copy(ROOT/path, 'licenses/'+name+'.txt')
    stb = (ROOT/'third_party/stb/stb_truetype.h').read_text(encoding='utf-8')
    (install/'licenses/stb.txt').write_text(stb[stb.rindex('This software is available under 2 licenses'):], encoding='utf-8')
    for name in ('MoonX360Sync', 'MoonX360Probe'):
        copy(ROOT/f'xdk/aurora/{name}/Main.lua', f'AuroraScripts/{name}/Main.lua')
        copy(ROOT/'assets/brand/MoonX360-64.png', f'AuroraScripts/{name}/icon.png')
    for image in sorted((ROOT/'assets/brand').glob('MoonX360*')):
        assert image.suffix in ('.png', '.svg')
        copy(image, 'icons/'+image.name)
    (install/'docs/VALIDATION.json').write_text(json.dumps(validation, indent=2)+'\n', encoding='utf-8')
    ffmpeg = json.loads((build/'ffmpeg/build-manifest.json').read_text())
    ffmpeg.pop('source_root', None)
    info = dict(name='MoonX360', version=args.version, source_revision=revision,
                release_date='2026-11-08', timezone='America/Sao_Paulo', hardware_tested=False,
                physical_status='PENDING PHYSICAL XBOX 360 VALIDATION',
                profile=ffmpeg['profile'], configuration='Release', ffmpeg=ffmpeg,
                binaries=binaries, wallpaper=dict(path='Wallpapers/home-wallpaper.png',
                dimensions=dimensions, sha256=sha(wallpaper)),
                fast_decode='On/Off; legacy behavior; changes apply next connection',
                validation='docs/VALIDATION.json')
    (install/'BUILD_INFO.json').write_text(json.dumps(info, indent=2)+'\n', encoding='utf-8')
    files = sorted(p for p in install.rglob('*') if p.is_file())
    allowed = {'default.xex', 'README.md', 'THIRD_PARTY_NOTICES.md', 'BUILD_INFO.json'}
    assert all(p.relative_to(install).as_posix() in allowed or
               p.relative_to(install).parts[0] in ('Wallpapers', 'licenses', 'docs', 'icons', 'AuroraScripts', 'MoonX360Shortcut') for p in files)
    assert all(p.suffix.lower() not in ('.ini', '.pem', '.key', '.pfx', '.crt', '.tsv', '.lib', '.obj', '.pdb', '.exe', '.dll') for p in files)
    (install/'SHA256SUMS.txt').write_text(''.join(sha(p)+'  '+p.relative_to(install).as_posix()+'\n' for p in files), encoding='ascii')
    archive = dist/('MoonX360-'+args.version+'.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for p in sorted(install.rglob('*')):
            if p.is_file():
                z.write(p, p.relative_to(destination))
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        for line in z.read('MoonX360/SHA256SUMS.txt').decode().splitlines():
            digest, name = line.split('  ', 1)
            assert hashlib.sha256(z.read('MoonX360/'+name)).hexdigest() == digest
    sources = dist/('MoonX360-'+args.version+'-sources.zip')
    subprocess.run(['git', 'archive', '--format=zip', '--prefix=MoonX360-source/',
                    '--output='+str(sources), revision], cwd=ROOT, check=True)
    with zipfile.ZipFile(sources) as z:
        assert z.testzip() is None
        assert z.read('MoonX360-source/assets/wallpapers/home-wallpaper.png') == wallpaper.read_bytes()
        assert not any(Path(n).suffix.lower() in ('.pem', '.key', '.pfx', '.crt', '.ini', '.tsv', '.exe', '.dll', '.lib', '.obj', '.xex') for n in z.namelist())
        assert not any('/xbox_remote_dev/' in n or '/dev kits/' in n.lower() for n in z.namelist())
    standalone = dist/('MoonX360-'+args.version+'-default.xex')
    shutil.copy2(build/'default.xex', standalone)
    checksums = dist/('SHA256SUMS-'+args.version+'.txt')
    checksums.write_text(''.join(sha(p)+'  '+p.name+'\n' for p in (archive, sources, standalone)), encoding='ascii')
    print(json.dumps(dict(package=str(archive), sources=str(sources), checksums=str(checksums),
                          xex_sha256=sha(standalone), zip_sha256=sha(archive), files=len(files)+1), indent=2))


if __name__ == '__main__':
    main()
