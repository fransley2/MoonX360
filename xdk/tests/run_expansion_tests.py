"""Focused local feature tests, using desktop MSVC and simulated devices."""
from pathlib import Path
import os,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
OUT=Path(os.environ.get('MOONX_EXPANSION_OUT',ROOT/'evidencias/expansao-20261028/host'));OUT.mkdir(parents=True,exist_ok=True)
vcvars=Path(os.environ.get('VS_VCVARS64',r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'))
r=subprocess.run(f'cmd.exe /d /s /c ""{vcvars}" >nul && set"',capture_output=True,text=True,check=True)
env=dict(os.environ)
for line in r.stdout.splitlines():
    if '=' in line and not line.startswith('='):
        k,v=line.split('=',1);env[k.upper()]=v
cl=next(str(Path(p)/'cl.exe') for p in env['PATH'].split(';') if (Path(p)/'cl.exe').exists())
def run(args):
    r=subprocess.run(list(map(str,args)),cwd=OUT,env=env,capture_output=True,text=True,errors='replace')
    print(r.stdout,r.stderr,flush=True)
    if r.returncode:raise SystemExit(r.returncode)
flags=['/nologo','/O2','/EHsc','/D_CRT_SECURE_NO_WARNINGS','/Dstrtok_r=strtok_s','/I'+str(ROOT/'evidencias/expansao-20261028/host'),'/I'+str(ROOT/'xdk/deps/moonlight-common-c/src')]
run([cl,*flags,*['/Tp'+str(ROOT/p) for p in ('xdk/tests/test_expansion.cpp','src/config.c','src/host_cache.c')],'/Feexpansion.exe'])
run([OUT/'expansion.exe'])
def function(text,signature):
    a=text.index(signature);b=text.index('{',a)+1;depth=1
    while depth:
        depth+=(text[b]=='{')-(text[b]=='}');b+=1
    return text[a:b]
app=(ROOT/'src/app.c').read_text()
functions='\n'.join(function(app,s) for s in (
    'static int selected_host_index(void)\n{','static const char *host_key(void)\n{','static const char *host_label(',
    'static void migrate_host_records(','static void save_hosts(',
    'static void connection_diagnosis_menu(','static void manage_host(void)\n{',
    'static void game_profile_menu(int id)\n{','static void pointer_settings_menu(',
    'static void library_context(','static int library_map(',
    'static void stream(int app_id,'))
fixture=(ROOT/'xdk/tests/expansion_menu_fixture.cpp').read_text().replace('/* ACTUAL_FUNCTIONS */',functions)
(OUT/'expansion-menus.cpp').write_text(fixture)
run([cl,*flags,OUT/'expansion-menus.cpp','/Tp'+str(ROOT/'src/config.c'),'/Tp'+str(ROOT/'src/host_cache.c'),'/Feexpansion-menus.exe'])
run([OUT/'expansion-menus.exe'])
gfx=(ROOT/'xdk/src/gfx_stream_menu.cpp').read_text()
fixture=(ROOT/'xdk/tests/stream_menu_fixture.cpp').read_text().replace('/* ACTUAL_STREAM_MENU */',gfx[gfx.index('struct stream_panel {'):])
(OUT/'stream-menu.cpp').write_text(fixture)
run([cl,*flags,OUT/'stream-menu.cpp','/Festream-menu.exe'])
run([OUT/'stream-menu.exe'])
main=(ROOT/'xdk/src/main_xdk.cpp').read_text()
fixture=(ROOT/'xdk/tests/storage_recovery_fixture.cpp').read_text().replace('/* ACTUAL_STORAGE */',function(main,'char *storage_read(')+'\n'+function(main,'static void storage_log_failure(')+'\n'+function(main,'bool storage_write('))
(OUT/'storage-recovery.cpp').write_text(fixture)
run([cl,*flags,OUT/'storage-recovery.cpp','/Festorage-recovery.exe'])
run([OUT/'storage-recovery.exe'])
