"""Run actual FFmpeg C vs VMX arithmetic/access models; no host performance claims."""
from pathlib import Path
import argparse
import json
import os
import subprocess
import sys

root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=root/'host-tests-vmx-ab-20261112')
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
subprocess.run([sys.executable,str(root/'xdk/tests/make_vmx_mc_test.py'),str(out)],check=True)
vcvars=Path(os.environ.get('VS_VCVARS64',r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'))
r=subprocess.run(f'cmd.exe /d /s /c ""{vcvars}" >nul && set"',capture_output=True,text=True,check=True)
env=dict(os.environ)
for line in r.stdout.splitlines():
    if '=' in line and not line.startswith('='):
        k,v=line.split('=',1);env[k.upper()]=v
cl=next(str(Path(path)/'cl.exe') for path in env['PATH'].split(';') if (Path(path)/'cl.exe').exists())
flags=['/nologo','/TP','/O2','/EHsc','/W3','/DVMX_AB_HOST_MODEL=1','/D_CRT_SECURE_NO_WARNINGS']
for directory in (out,root/'xdk/tests',root/'xdk/ffmpeg_xbox',root/'xdk/deps/XBMC-360/libraries/ffmpeg'):
    flags.append('/I'+str(directory))
sources=[out/name for name in ('reference.cpp','moon.cpp','multiplex_qpel.cpp','multiplex_chroma.cpp','main.cpp','negative.cpp')]
sources.append(root/'xdk/ffmpeg_xbox/vmx_ab.c')
r=subprocess.run([cl,*flags,*map(str,sources),'/Fe'+str(out/'vmx-mc.exe')],cwd=out,env=env,capture_output=True,text=True,errors='replace')
(out/'compile.log').write_text(r.stdout+r.stderr,encoding='utf-8')
if r.returncode:
    print(r.stdout+r.stderr);raise SystemExit(r.returncode)
with (out/'integrity-model.csv').open('w',encoding='utf-8') as log:
    r=subprocess.run([str(out/'vmx-mc.exe')],cwd=out,env=env,stdout=log,stderr=subprocess.PIPE,text=True)
if r.stderr:print(r.stderr)
data=(out/'integrity-model.csv').read_text()
probe=subprocess.run([str(out/'vmx-mc.exe'),'--negative-store'],cwd=out,env=env,capture_output=True,text=True)
negative_detected=probe.returncode!=0 and 'model out-of-block store' in probe.stderr
(out/'negative-store.txt').write_text(probe.stderr)
checks=[line for line in data.splitlines() if line.startswith('CHECK,')]
result={'environment':'Windows arithmetic/access model; no Xenon instructions or timing',
        'exit':r.returncode,'checks':len(checks),'passed':sum(line.endswith(',PASS') for line in checks),
        'rejected':[line for line in checks if line.endswith(',REJECT')],
        'integrity_complete':'# integrity_complete=1' in data,'timing_rows':sum(line.startswith('TIME,') for line in data.splitlines()),
        'unchanged_out_of_block_store_detected':negative_detected,
        'native_validation':'PENDING; console offline','source_hashes':json.loads((out/'source-hashes.json').read_text())}
(out/'results.json').write_text(json.dumps(result,indent=2))
print(json.dumps({k:v for k,v in result.items() if k!='source_hashes'},indent=2))
if r.returncode or len(checks)!=960 or result['passed']!=960 or not result['integrity_complete'] or result['timing_rows'] or not negative_detected:
    raise SystemExit(1)
