"""Native Windows build. Requires the user's XDK and pinned deps from xdk/README.md."""
from pathlib import Path
import os,sys,subprocess,importlib.util
r=Path(__file__).resolve().parents[2]
missing=[]
sdk=os.environ.get('XEDK','')
if os.name!='nt':missing.append('Windows host')
for rel in ['TechPreview/Jul12Compiler/bin/win32/cl.exe','bin/win32/imagexex.exe','include/xbox/xtl.h']:
    if not sdk or not (Path(sdk)/rel).is_file():missing.append(str(Path(sdk or '<unset XEDK>')/rel))
for rel in ['TechPreview/Jul12Compiler/bin/win32/lib.exe','TechPreview/Jul12Compiler/bin/win32/link.exe',
            'TechPreview/Jul12Compiler/include/xbox/stdio.h','include/xbox/xaudio2.h',
            'TechPreview/Jul12Compiler/lib/xbox/libcmt.lib']:
    if not sdk or not (Path(sdk)/rel).is_file():missing.append(str(Path(sdk or '<unset XEDK>')/rel))
for rel in ['XBMC-360/libraries/ffmpeg/libavcodec/xb_thread.c','moonlight-common-c/src/Limelight.h','mbedtls-2.16.12/include/mbedtls/ssl.h','opus-1.5.2/include/opus.h']:
    if not (r/'xdk/deps'/rel).is_file():missing.append(str(r/'xdk/deps'/rel))
spec=importlib.util.spec_from_file_location('build',r/'xdk/build.py')
build=importlib.util.module_from_spec(spec);spec.loader.exec_module(build)
for name in build.SDK_LIBS:
    if not any((Path(sdk)/base/name).is_file() for base in ['TechPreview/Jul12Compiler/lib/xbox','lib/xbox']):
        missing.append(str(Path(sdk or '<unset XEDK>')/'lib/xbox'/name))
if missing:
    print('Native build unavailable; missing:\n- '+'\n- '.join(missing));sys.exit(2)
for profile in ['aggressive','compatible']:
    env=dict(os.environ);out=r/'xdk/build/stability'/profile
    env['XDK_BUILD_DIR']=str(out);env['XDK_OPTIMIZATION']=profile
    env['PYTHONUNBUFFERED']='1'
    print('BUILD:',profile,'XEDK:',sdk,'OUT:',out,flush=True)
    subprocess.run([sys.executable,str(r/'xdk/scripts/build_ffmpeg.py')],cwd=r,env=env,check=True)
    subprocess.run([sys.executable,str(r/'xdk/build.py')],cwd=r,env=env,check=True)
    print('BUILT:',out/'default.xex')
