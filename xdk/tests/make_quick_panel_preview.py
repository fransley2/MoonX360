"""Render the actual Quick Panel canvas using desktop font/canvas code."""
from pathlib import Path
import os,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
OUT=Path(os.environ.get('MOONX_TEST_OUT',ROOT/'host-tests'))
OUT.mkdir(parents=True,exist_ok=True)
vcvars=Path(os.environ.get('VS_VCVARS64',r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'))
r=subprocess.run(f'cmd.exe /d /s /c ""{vcvars}" >nul && set"',capture_output=True,text=True,check=True)
env=dict(os.environ)
for line in r.stdout.splitlines():
    if '=' in line and not line.startswith('='):
        k,v=line.split('=',1);env[k.upper()]=v
cl=next(str(Path(p)/'cl.exe') for p in env['PATH'].split(';') if (Path(p)/'cl.exe').exists())
gfx=(ROOT/'xdk/src/gfx_stream_menu.cpp').read_text()
fixture=(ROOT/'xdk/tests/quick_panel_preview.cpp').read_text().replace(
    '/* ACTUAL_STREAM_MENU */',gfx[gfx.index('struct stream_panel {'):])
source=OUT/'quick-panel-preview.cpp';source.write_text(fixture)
cmd=[cl,'/nologo','/O2','/EHsc','/D_CRT_SECURE_NO_WARNINGS','/I'+str(ROOT),
     '/I'+str(ROOT/'xdk/deps/moonlight-common-c/src'),
     source,'/Tp'+str(ROOT/'src/ui/canvas.c'),'/Tp'+str(ROOT/'src/ui/font.c'),
     ROOT/'xdk/build/quick-panel-20261106/gen/fonts_data.c','/Fequick-panel-preview.exe']
r=subprocess.run(list(map(str,cmd)),cwd=OUT,env=env,capture_output=True,text=True,errors='replace')
print(r.stdout,r.stderr)
if r.returncode:raise SystemExit(r.returncode)
for style in ('0','1'):
    for language in ('0','1','2'):
        r=subprocess.run([OUT/'quick-panel-preview.exe',OUT/f'quick-panel-preview-{style}-{language}.png',style,language],cwd=OUT)
        if r.returncode:raise SystemExit(r.returncode)

for aspect in ('0','1'):
    r=subprocess.run([OUT/'quick-panel-preview.exe',OUT/f'desktop-layout-{aspect}.png','0','2',aspect],cwd=OUT)
    if r.returncode:raise SystemExit(r.returncode)
