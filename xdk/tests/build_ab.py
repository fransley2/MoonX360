"""Build current/previous worker-map controls from isolated, pinned FFmpeg copies."""
from pathlib import Path
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'xdk/build/ab'
sys.path.insert(0,str(ROOT/'xdk/scripts'))
import patch_worker_map as workers

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def run_logged(args,env,log,quiet=False):
    print('COMMAND:',subprocess.list2cmdline([str(x) for x in args]),flush=True)
    with log.open('w',encoding='utf-8') as f:
        process=subprocess.Popen([str(x) for x in args],cwd=ROOT,env=env,stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT,text=True,errors='replace')
        for line in process.stdout:
            f.write(line);f.flush()
            if not quiet:print(line,end='',flush=True)
        if process.wait():raise RuntimeError('Build failed; see '+str(log))

def main():
    if os.name!='nt':raise SystemExit('Native A/B build requires Windows and the local XDK')
    sdk=Path(os.environ.get('XEDK','<unset XEDK>'))
    required=['include/xbox/xtl.h','TechPreview/Jul12Compiler/bin/win32/cl.exe',
              'TechPreview/Jul12Compiler/bin/win32/lib.exe','TechPreview/Jul12Compiler/bin/win32/link.exe',
              'TechPreview/Jul12Compiler/include/xbox/stdio.h','bin/win32/imagexex.exe']
    missing=[str(sdk/p) for p in required if not (sdk/p).is_file()]
    for rel in ['XBMC-360/libraries/ffmpeg/libavcodec/xb_thread.c','moonlight-common-c/src/Limelight.h',
                'mbedtls-2.16.12/include/mbedtls/ssl.h','opus-1.5.2/include/opus.h']:
        if not (ROOT/'xdk/deps'/rel).is_file():missing.append(str(ROOT/'xdk/deps'/rel))
    if missing:raise SystemExit('Missing:\n'+'\n'.join(missing))
    original=ROOT/'xdk/deps/XBMC-360/libraries/ffmpeg'
    workers.validate((original/'libavcodec/xb_thread.c').read_text())
    original_hash=sha(original/'libavcodec/xb_thread.c')
    results={}
    for layout in ('current','previous'):
        source=OUT/'ffmpeg-sources'/layout
        shutil.copytree(original,source,dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('.git','*.obj','*.lib','__pycache__'))
        workers.patch(source/'libavcodec/xb_thread.c',layout)
        out=OUT/('compatible-'+layout);out.mkdir(parents=True,exist_ok=True)
        env=dict(os.environ,XEDK=str(sdk),XDK_OPTIMIZATION='compatible',XDK_BUILD_DIR=str(out),
                 XDK_FFMPEG_ROOT=str(source),XDK_WORKER_MAP=layout,PYTHONUNBUFFERED='1')
        run_logged([sys.executable,ROOT/'xdk/scripts/build_ffmpeg.py'],env,out/'ffmpeg-build.log')
        run_logged([sys.executable,ROOT/'xdk/build.py'],env,out/'native-build.log')
        xex=out/'default.xex'
        if xex.read_bytes()[:4]!=b'XEX2':raise RuntimeError('Not an XEX2: '+str(xex))
        run_logged([sdk/'bin/win32/imagexex.exe','/dump',xex],env,out/'imagexex-dump.log',quiet=True)
        manifest=json.loads((out/'ffmpeg/build-manifest.json').read_text())
        if manifest['worker_map']!=layout:raise RuntimeError('FFmpeg layout manifest mismatch')
        results[layout]={'profile':'compatible','worker_map':layout,'path':str(xex),
                         'sha256':sha(xex),'bytes':xex.stat().st_size,
                         'ffmpeg_worker_sha256':manifest['worker_sha256'],
                         'workers_runtime':[3,4],'aux_runtime':['balanced','legacy_aux']}
        (out/'build-identity.json').write_text(json.dumps(results[layout],indent=2),encoding='utf-8')
        print('BUILT:',layout,xex,results[layout]['sha256'],flush=True)
    if sha(original/'libavcodec/xb_thread.c')!=original_hash:raise RuntimeError('Canonical FFmpeg source changed')
    (OUT/'build-results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')

if __name__=='__main__':main()
