"""Focused host checks; fake devices cannot validate physical Xbox I/O."""
from pathlib import Path
import os,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
OUT=Path(os.environ.get('MOONX_REMOTE_OUT',ROOT/'host-tests/remote/host'))
OUT.mkdir(parents=True,exist_ok=True)
vcvars=Path(os.environ.get('VS_VCVARS64',r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'))
r=subprocess.run(f'cmd.exe /d /s /c ""{vcvars}" >nul && set"',capture_output=True,text=True,check=True)
env=dict(os.environ)
for line in r.stdout.splitlines():
    if '=' in line and not line.startswith('='):
        k,v=line.split('=',1);env[k]=v
cl=next(str(Path(p)/'cl.exe') for p in env['PATH'].split(';') if (Path(p)/'cl.exe').exists())
def run(args):
    r=subprocess.run(list(map(str,args)),cwd=OUT,env=env,capture_output=True,text=True,errors='replace')
    print(r.stdout,r.stderr,flush=True)
    if r.returncode:raise SystemExit(r.returncode)
flags=['/nologo','/O2','/EHsc','/D_CRT_SECURE_NO_WARNINGS','/Dstrtok_r=strtok_s','/I'+str(OUT),'/I'+str(ROOT/'xdk/deps/moonlight-common-c/src'),'/FI'+str(ROOT/'src/performance.h')]
def function(text,signature):
    a=text.index(signature);b=text.index('{',a)+1;depth=1
    while depth:
        depth+=(text[b]=='{')-(text[b]=='}');b+=1
    return text[a:b]
run([cl,*flags,*['/Tp'+str(ROOT/p) for p in ('xdk/tests/test_remote_features.cpp','src/config.c','src/host_cache.c')],'/Feremote-features.exe'])
run([OUT/'remote-features.exe'])
audio=(ROOT/'xdk/src/audio_xdk.cpp').read_text()
audio=audio[audio.index('#define MAX_FRAME_SAMPLES'):]
fixture=(ROOT/'xdk/tests/audio_remote_fixture.cpp').read_text().replace('/* ACTUAL_AUDIO */',audio)
fixture=fixture.replace('/* ACTUAL_RETRY */',function((ROOT/'src/app.c').read_text(),'    if(cj.ret!=0 && lj.sc.audioConfiguration==AUDIO_CONFIGURATION_51_SURROUND)'))
(OUT/'audio-remote.cpp').write_text(fixture)
run([cl,*flags,OUT/'audio-remote.cpp','/Feaudio-remote.exe'])
run([OUT/'audio-remote.exe'])
app=(ROOT/'src/app.c').read_text()
a=app.index('struct wake_job {');b=app.index('};',a)+2
worker=app[a:b]+'\n'+function(app,'static void *wake_task(')
fixture=(ROOT/'xdk/tests/wake_remote_fixture.cpp').read_text()
fixture=fixture.replace('/* ACTUAL_WORKER */',worker).replace('/* ACTUAL_SEND */',function((ROOT/'src/wake_net.c').read_text(),'bool wake_send('))
(OUT/'wake-remote.cpp').write_text(fixture)
run([cl,*flags,OUT/'wake-remote.cpp','/Fewake-remote.exe'])
run([OUT/'wake-remote.exe'])
http=(ROOT/'src/http.c').read_text()
code='\n'.join(function(http,s) for s in (
    'static bool probe_live(', 'static bool probe_wait(', 'static bool probe_open(',
    'static int conn_write(', 'static int conn_read(', 'static const char *find_nocase(',
    'static long dechunk(', 'static bool http_get_impl('))
fixture=(ROOT/'xdk/tests/http_probe_fixture.cpp').read_text().replace('/* ACTUAL_HTTP */',code)
(OUT/'http-probe.cpp').write_text(fixture)
run([cl,*flags,OUT/'http-probe.cpp','/Dstrncasecmp=_strnicmp','/Fehttp-probe.exe','ws2_32.lib'])
run([OUT/'http-probe.exe'])
