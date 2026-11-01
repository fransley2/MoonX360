"""Focused artwork checks; generated files/binaries remain in the selected output directory."""
import os
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[2]
OUT=Path(os.environ.get('MOONX_ART_TEST_OUT',ROOT/'host-tests/artwork-20261031'))
OUT.mkdir(parents=True,exist_ok=True)
vcvars=Path(os.environ.get('VS_VCVARS64',r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'))
r=subprocess.run(f'cmd.exe /d /s /c ""{vcvars}" >nul && set"',capture_output=True,text=True,check=True)
env=dict(os.environ)
for line in r.stdout.splitlines():
 if '=' in line and not line.startswith('='):
  key,value=line.split('=',1);env[key.upper()]=value
cl=next(str(Path(p)/'cl.exe') for p in env['PATH'].split(';') if (Path(p)/'cl.exe').exists())
def run(args):
 r=subprocess.run(list(map(str,args)),cwd=OUT,env=env,capture_output=True,text=True,errors='replace')
 print(r.stdout.strip(),flush=True)
 if r.returncode:print(r.stderr);raise SystemExit(r.returncode)
flags=['/nologo','/std:c++17','/O2','/W3','/EHsc','/D_CRT_SECURE_NO_WARNINGS','/I'+str(ROOT/'xdk/deps/moonlight-common-c/src'),'/FI'+str(ROOT/'src/performance.h')]
jobs=[('cover-policy',[ROOT/'xdk/tests/test_cover_policy.cpp']),('cover-disk',[ROOT/'xdk/tests/test_cover_disk.cpp']),('cover-decode',[ROOT/'xdk/tests/test_cover_decode.cpp',ROOT/'src/ui/image.c']),('home-art',[ROOT/'xdk/tests/test_home_art.cpp']),('launch-cover',[ROOT/'xdk/tests/test_launch_cover.cpp'])]
for gen,name in [('make_cover_cache_test.py','cover-cache'),('make_cover_http_test.py','cover-http'),('make_launch_handoff_test.py','launch-handoff')]:
 target=OUT/(name+'.cpp');run([sys.executable,ROOT/'xdk/tests'/gen,target])
 target.write_text(target.read_text().replace('../src/',(ROOT/'src').as_posix()+'/'))
 jobs.append((name,[target]))
for name,sources in jobs:
 print('TEST:',name,flush=True)
 run([cl,*flags,*['/Tp'+str(s) for s in sources],'/Fe'+name+'.exe'])
 run([OUT/(name+'.exe')])
print('PASS: focused artwork fixtures (desktop only)',flush=True)
