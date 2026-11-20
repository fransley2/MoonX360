"""Run the real portable launch protocol against desktop MSVC."""
from pathlib import Path
import os,subprocess,tempfile
import importlib.util
root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('moonx_xdk_build',root/'xdk/build.py')
build=importlib.util.module_from_spec(spec);spec.loader.exec_module(build)
assert build.shortcut_variant_flags('normal')==[]
assert build.shortcut_variant_flags('diagnostic')==['/D','MOONX_SHORTCUT_DIAGNOSTIC']
assert build.shortcut_variant_flags('main_only')==['/D','MOONX_SHORTCUT_LAUNCH_MAIN_ONLY']
vcvars=Path(os.environ.get('VS_VCVARS64',r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'))
setup=subprocess.run(f'cmd.exe /d /s /c ""{vcvars}" >nul && set"',capture_output=True,text=True,check=True)
env=dict(os.environ)
for line in setup.stdout.splitlines():
    if '=' in line and not line.startswith('='):
        key,value=line.split('=',1);env[key.upper()]=value
cl=next(str(Path(p)/'cl.exe') for p in env['PATH'].split(';') if (Path(p)/'cl.exe').exists())
with tempfile.TemporaryDirectory(prefix='moonx-aurora-test-') as tmp:
    exe=Path(tmp)/'aurora_launch.exe'
    command=[cl,'/nologo','/W4','/WX','/wd4505','/EHsc','/std:c++14','/D_CRT_SECURE_NO_WARNINGS',
             '/I'+str(root/'src'),str(root/'xdk/tests/test_aurora_launch.cpp'),'/Fe'+str(exe)]
    subprocess.run(command,cwd=tmp,env=env,check=True)
    subprocess.run([str(exe)],cwd=tmp,env=env,check=True)
