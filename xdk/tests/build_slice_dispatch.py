"""Isolated Compatible A/B: identical app and libraries except the slice dispatcher."""
from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'xdk/build/slice-dispatch-20261029'
EVIDENCE=ROOT/'evidencias/slice-dispatch-20261029'
SEED=ROOT/'xdk/build/ab/compatible-current'
FFMPEG=ROOT/'xdk/build/ab/ffmpeg-sources/current'
if not (SEED/'ffmpeg/build-manifest.json').exists() or not FFMPEG.exists():
    raise SystemExit('Missing verified Compatible seed; rebuild xdk/tests/build_ab.py first')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def run(args,env,log):
    with log.open('w',encoding='utf-8') as f:
        result=subprocess.run(list(map(str,args)),cwd=ROOT,env=env,stdout=f,stderr=subprocess.STDOUT)
    print(log, 'exit',result.returncode,flush=True)
    if result.returncode:raise SystemExit(result.returncode)
results={}
for mode in ('legacy','batched'):
    out=OUT/mode;out.mkdir(parents=True,exist_ok=True)
    for name in ('mbedtls.lib','opus.lib','compat.lib','moonlight.lib'):
        if not (out/name).exists():shutil.copy2(SEED/name,out/name)
    if not (out/'ffmpeg').exists():shutil.copytree(SEED/'ffmpeg',out/'ffmpeg')
    env=dict(os.environ,XDK_OPTIMIZATION='compatible',XDK_WORKER_MAP='current',XDK_SLICE_DISPATCH=mode,
             XDK_BUILD_DIR=str(out),XDK_FFMPEG_ROOT=str(FFMPEG),PYTHONUNBUFFERED='1')
    if mode=='batched':
        run([sys.executable,ROOT/'xdk/scripts/build_ffmpeg.py','avcodec'],env,out/'ffmpeg-build.log')
    else:
        manifest=json.loads((out/'ffmpeg/build-manifest.json').read_text())
        manifest['slice_dispatch']='legacy';(out/'ffmpeg/build-manifest.json').write_text(json.dumps(manifest,indent=2))
    run([sys.executable,ROOT/'xdk/build.py','app','xex'],env,out/'native-build.log')
    xex=out/'default.xex';assert xex.read_bytes()[:4]==b'XEX2'
    run([Path(env['XEDK'])/'bin/win32/imagexex.exe','/dump',xex],env,out/'imagexex-dump.txt')
    dump=(out/'imagexex-dump.txt').read_text(errors='replace')
    assert '4D583336' in dump.upper() and 'xboxkrnl' in dump.lower()
    assert (out/'MoonX360.spa').read_bytes()==(out/'MoonX360-embedded.spa').read_bytes()
    unexpected=[];warnings=[]
    allowed={'src_ui_image.obj.log':['C4068']*3,'xdk_src_input_xdk.obj.log':['C4800']}
    for p in (out/'app').glob('*.obj.log'):
        codes=re.findall(r'warning (C\d+)',p.read_text(encoding='utf-8'))
        remaining=allowed.get(p.name,[]).copy()
        for code in codes:
            warnings.append((p.name,code))
            if code in remaining:remaining.remove(code)
            else:unexpected.append((p.name,code))
    assert not unexpected,unexpected
    dest=ROOT/('dist/MoonX360-Slice-Dispatch-20261029/'+('A-Control-Compatible' if mode=='legacy' else 'B-Batched-Compatible'))
    dest.mkdir(parents=True,exist_ok=True);shutil.copy2(xex,dest/'default.xex')
    (dest/'SHA256SUMS.txt').write_text(sha(xex)+'  default.xex\n',encoding='ascii')
    manifest=json.loads((out/'ffmpeg/build-manifest.json').read_text())
    object_differences=[]
    if mode=='batched':
        for old in (SEED/'ffmpeg/avcodec').glob('*.obj'):
            if sha(old)!=sha(out/'ffmpeg/avcodec'/old.name):object_differences.append(old.name)
        assert object_differences==['libavcodec_xb_thread.obj'],object_differences
        worker_log=out/'ffmpeg/avcodec/libavcodec_xb_thread.obj.log'
        assert not re.search(r'warning C\d+',worker_log.read_text(encoding='utf-8'))
    results[mode]={'profile':'compatible','slice_dispatch':mode,'worker_map':'current','xex':str(dest/'default.xex'),
                   'sha256':sha(xex),'bytes':xex.stat().st_size,'warnings':warnings,'new_warnings':unexpected,
                   'ffmpeg_manifest':manifest,'avcodec_object_differences':object_differences}
    print('BUILT',mode,sha(xex),flush=True)
for lib in ('avutil','pthreads'):
    assert results['legacy']['ffmpeg_manifest']['libraries'][lib]==results['batched']['ffmpeg_manifest']['libraries'][lib]
for name in ('mbedtls.lib','opus.lib','compat.lib','moonlight.lib'):
    assert sha(OUT/'legacy'/name)==sha(OUT/'batched'/name)
(EVIDENCE/'build-results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
print('PASS: isolated XEX2, imports, SPA, library checksums, scheduler identity and zero new application warnings')
