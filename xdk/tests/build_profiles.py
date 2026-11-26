"""Build both XEX profiles, retaining per-unit compiler output and warning evidence.

Reuses the local, already-built FFmpeg libraries without changing decoder flags.
Usage: python xdk/tests/build_profiles.py [output directory]
"""
from pathlib import Path
import importlib.util
import json
import os
import re
import shutil
import sys

ROOT=Path(__file__).resolve().parents[2]
DEST=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else ROOT/'xdk/build/ui-mdns'
for profile in ('aggressive','compatible'):
    out=DEST/profile
    out.mkdir(parents=True,exist_ok=True)
    (out/'ffmpeg').mkdir(exist_ok=True)
    for name in ('avcodec.lib','avutil.lib','pthreads.lib'):
        shutil.copy2(ROOT/'xdk/build/ffmpeg'/name,out/'ffmpeg'/name)
    os.environ['XDK_BUILD_DIR']=str(out)
    os.environ['XDK_OPTIMIZATION']=profile
    spec=importlib.util.spec_from_file_location('build_'+profile,ROOT/'xdk/build.py')
    b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
    results=[]
    original=b.compile_one
    def audit(job):
        src,obj,flags=job
        result=original(job)
        log=Path(str(obj)+'.log')
        # Preserve prior compiler evidence when the incremental cache is hit.
        if result[2] or not log.exists():log.write_text(result[2],encoding='utf-8')
        output=log.read_text(encoding='utf-8')
        results.append({'source':str(Path(src).relative_to(ROOT)), 'returncode':result[1],
                        'warnings':re.findall(r'warning [A-Z]+\d+[^\n]*',output)})
        return result
    b.compile_one=audit
    for name,fn in b.TARGETS.items():
        print(profile,name,flush=True)
        if fn() is None:raise SystemExit('Build failed: '+profile+'/'+name)
    results.sort(key=lambda r:r['source'])
    (out/'compile-results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    app=[r for r in results if r['source'].replace('\\','/').startswith(('src/','xdk/src/'))
         and 'PlatformCrypto' not in r['source'] and 'enet_xbox' not in r['source']]
    # Unchanged local code already emits these with Jul12 /W3. Preserve input fixes
    # and bundled image decoding; require zero additional warnings.
    baseline={'src/ui/image.c': ['C4068']*3,'xdk/src/input_xdk.cpp':['C4800']}
    unexpected=[]
    for unit in app:
        codes=[re.search(r'C\d+',w)[0] for w in unit['warnings']]
        allowed=baseline.get(unit['source'].replace('\\','/'),[])
        for code in codes:
            if code in allowed:allowed=allowed.copy();allowed.remove(code)
            else:unexpected.append((unit['source'],code))
    warnings=sum(len(r['warnings']) for r in app)
    print(profile,'application units',len(app),'existing warnings',warnings,'new warnings',len(unexpected),flush=True)
    if unexpected:raise SystemExit('New application warnings: '+str(unexpected))
    assert (out/'default.xex').read_bytes()[:4]==b'XEX2'
