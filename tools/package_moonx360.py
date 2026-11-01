"""Create a clean MoonX360 release from an explicit allowlist, never user data."""
from pathlib import Path
import hashlib, json, shutil, zipfile, struct, io, os
from PIL import Image
from datetime import date

ROOT=Path(__file__).resolve().parents[1]
BUILD=Path(os.environ.get('MOONX_BUILD_DIR',str(ROOT/'xdk/build/MoonX360/aggressive'))).resolve()
NAME=os.environ.get('MOONX_PACKAGE_NAME','MoonX360')
assert NAME and all(c.isalnum() or c in '-_' for c in NAME)
DIST=ROOT/'dist'
DEST=DIST/NAME
DEST.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def copy(src,dst):
 dst=DEST/dst;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/src,dst)

# Validate the extracted, embedded dashboard resource, not merely the source icon.
spa=(BUILD/'MoonX360-embedded.spa').read_bytes()
assert spa==(BUILD/'MoonX360.spa').read_bytes() and spa[:4]==b'XDBF'
_,version,count,used,free_count,free_used=struct.unpack('>4s5I',spa[:24])
base=24+count*18+free_count*8
images=[];names=[]
for i in range(used):
 namespace,ident,offset,size=struct.unpack('>HQII',spa[24+i*18:24+(i+1)*18])
 data=spa[base+offset:base+offset+size]
 if namespace==2 and ident==32768:
  assert data==(ROOT/'assets/brand/MoonX360-64.png').read_bytes()
  assert Image.open(io.BytesIO(data)).size==(64,64);images.append(ident)
 if namespace==3:
  assert b'MoonX360' in data;names.append(ident)
assert images==[32768] and names
xex=(BUILD/'default.xex').read_bytes();assert xex[:4]==b'XEX2'
headers={struct.unpack('>II',xex[24+i*8:32+i*8])[0]:struct.unpack('>II',xex[24+i*8:32+i*8])[1] for i in range(int.from_bytes(xex[20:24],'big'))}
assert 0x103ff in headers and 0x2ff in headers
assert xex[headers[0x40006]+12:headers[0x40006]+16]==bytes.fromhex('4D583336')
copy(BUILD/'default.xex','default.xex')
copy('docs/MoonX360.md','LEIA-ME.md')
copy('docs/RECURSOS_20261028.md','docs/RECURSOS_20261028.md')
copy('docs/EXPANSAO_20261028.md','docs/EXPANSAO_20261028.md')
copy('docs/PERFORMANCE_20261029.md','docs/PERFORMANCE_20261029.md')
copy('docs/HOSTS_UI_20261031.md','docs/HOSTS_UI_20261031.md')
copy('docs/UI_CLEANUP_20261101.md','docs/UI_CLEANUP_20261101.md')
copy('docs/STATE_FIXES_20261101.md','docs/STATE_FIXES_20261101.md')
copy('LICENSE','licenses/MoonX360-GPL-3.0.txt')
for size in (1024,512,256,128,64,32):
 src=ROOT/f'assets/brand/MoonX360-{size}.png'
 im=Image.open(src);assert im.size==(size,size) and im.mode=='RGBA' and im.getextrema()[3][0]==0
 copy(src.relative_to(ROOT),f'icons/{src.name}')
copy('assets/brand/MoonX360.svg','icons/MoonX360.svg')
licenses={
 'Moonlight':'xdk/deps/moonlight-common-c/LICENSE.txt',
 'mbedTLS':'xdk/deps/mbedtls-2.16.12/apache-2.0.txt',
 'Opus':'xdk/deps/opus-1.5.2/COPYING',
 'FFmpeg-LGPL':'xdk/deps/XBMC-360/libraries/ffmpeg/COPYING.LGPLv2.1',
 'FFmpeg-GPL':'xdk/deps/XBMC-360/libraries/ffmpeg/COPYING.GPLv2',
 'Inter':'assets/fonts/Inter-LICENSE.txt',
 'ENet':'xdk/deps/moonlight-common-c/enet/LICENSE',
 'nanors':'xdk/deps/moonlight-common-c/nanors/LICENSE',
}
for name,path in licenses.items():copy(path,f'licenses/{name}.txt')
stb=(ROOT/'third_party/stb/stb_truetype.h').read_text(encoding='utf-8')
(DEST/'licenses/stb.txt').write_text(stb[stb.rindex('This software is available under 2 licenses'):],encoding='utf-8')
profile=json.loads((BUILD/'ffmpeg/build-manifest.json').read_text())['profile']
assert profile in ('compatible','aggressive')
(DEST/'BUILD_INFO.json').write_text(json.dumps({'name':'MoonX360','date':date.today().isoformat(),'profile':profile,'hardware_tested':False,'title_id':'4D583336','xex_sha256':sha(BUILD/'default.xex'),'embedded_spa_verified':True,'dashboard_icon':'64x64 RGBA PNG; image 32768','icon_master':'icons/MoonX360.svg'},indent=2),encoding='utf-8')
allowed={'default.xex','LEIA-ME.md','BUILD_INFO.json','SHA256SUMS.txt'}
assert all(p.name in allowed or p.relative_to(DEST).parts[0] in ('licenses','icons','docs') for p in DEST.rglob('*') if p.is_file())
assert not any(p.suffix.lower() in ('.pem','.key','.ini','.tsv','.pfx','.crt') for p in DEST.rglob('*') if p.is_file())
files=sorted(p for p in DEST.rglob('*') if p.is_file() and p.name!='SHA256SUMS.txt')
(DEST/'SHA256SUMS.txt').write_text(''.join(sha(p)+'  '+p.relative_to(DEST).as_posix()+'\n' for p in files),encoding='ascii')
for archive,files,base in [(DIST/(NAME+'.zip'),sorted(p for p in DEST.rglob('*') if p.is_file()),DEST.parent),(DIST/(NAME+'-icons.zip'),sorted((DEST/'icons').iterdir()),DEST)]:
 with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
  for p in files:z.write(p,p.relative_to(base))
 with zipfile.ZipFile(archive) as z:assert z.testzip() is None
 print(archive,sha(archive))
(DIST/(NAME+'-SHA256SUMS.txt')).write_text(''.join(sha(p)+'  '+p.relative_to(DIST).as_posix()+'\n' for p in (DEST/'default.xex',DIST/(NAME+'.zip'),DIST/(NAME+'-icons.zip'))),encoding='ascii')
print('PASS: XEX2/imports, Title ID, embedded icon/name, six alpha PNG sizes, clean allowlist and ZIP CRC.')
