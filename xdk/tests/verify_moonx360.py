"""Focused desktop evidence for the rebrand, language fixes and permission dialog."""
from pathlib import Path
import os, subprocess, sys, zipfile, re, json
from PIL import Image, ImageChops

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'evidencias/moonx360-20261028/host'
OUT.mkdir(parents=True,exist_ok=True)
vcvars=Path(os.environ.get('VS_VCVARS64',r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'))
r=subprocess.run(f'cmd.exe /d /s /c ""{vcvars}" >nul && set"',capture_output=True,text=True,check=True)
env=dict(os.environ)
for line in r.stdout.splitlines():
 if '=' in line and not line.startswith('='):
  k,v=line.split('=',1);env[k]=v
cl=next(str(Path(p)/'cl.exe') for p in env['PATH'].split(';') if (Path(p)/'cl.exe').exists())
flags=['/nologo','/O2','/EHsc','/D_CRT_SECURE_NO_WARNINGS','/Dstrtok_r=strtok_s','/I'+str(ROOT/'xdk/deps/moonlight-common-c/src'),'/I'+str(ROOT/'host-tests')]
def run(args):
 r=subprocess.run(list(map(str,args)),env=env,cwd=OUT,capture_output=True,text=True,errors='replace')
 print(r.stdout,r.stderr,flush=True)
 if r.returncode:raise SystemExit(r.returncode)
def build(name,sources):
 run([cl,*flags,*[('/Tp'+str(p)) if Path(p).suffix in ('.cpp','.c') else str(p) for p in sources],'/Fe'+name+'.exe'])
 run([OUT/(name+'.exe')])

fonts=ROOT/'xdk/build/MoonX360/aggressive/gen/fonts_data.c'
run([cl,*flags,'/TC','/c',fonts,'/Fofonts.obj'])
ui=[ROOT/'src/ui'/n for n in ('canvas.c','font.c','draw.c','ui.c')]
for gen,name,sources in [
 ('make_ui_features_test.py','ui-features',[*ui[:3],OUT/'fonts.obj']),
 ('make_gpu_ui_test.py','gpu-ui',[*ui,OUT/'fonts.obj']),
 ('make_loading_test.py','loading',[])]:
 fixture=OUT/(name+'.cpp');run([sys.executable,ROOT/'xdk/tests'/gen,fixture]);build(name,[fixture,*sources])
build('home',[ROOT/'xdk/tests/test_home_ui.cpp',*ui,OUT/'fonts.obj'])

# Exercise the actual XML status parser, including escaped/absent/truncated messages.
gs=(ROOT/'src/gamestream.c').read_text()
a=gs.index('static bool xml_status_ok(');b=gs.index('\nstatic bool request(',a)
fixture='''#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
static char last_error[256];
static void set_error(const char *fmt,const char *arg){snprintf(last_error,sizeof(last_error),fmt,arg?arg:"");}
'''+gs[a:b]+r'''
int main(){
 assert(xml_status_ok("<root status_code=\"200\"/>"));
 assert(!xml_status_ok("<root status_code=\"403\" status_message=\"Permission denied: &quot;Launch applications&quot;\"/>"));
 assert(strstr(last_error,"Client Management") && !strstr(last_error,"&quot;"));
 assert(!xml_status_ok("<root status_code=\"403\"/>"));assert(strstr(last_error,"did not allow"));
 assert(!xml_status_ok("<root status_code=\"403\" status_message=\"A paired TLS client identity is required\"/>"));assert(strstr(last_error,"pairing"));
 assert(!xml_status_ok("<root status_code=\"500\" status_message=\"Encoder unavailable\"/>"));assert(strstr(last_error,"Encoder unavailable"));
 assert(!xml_status_ok("<root/>"));assert(strstr(last_error,"Malformed"));
 puts("PASS: XML permission messages, pairing denial, unrelated errors and success preserved.");
}
'''
(OUT/'host-errors.cpp').write_text(fixture);build('host-errors',[OUT/'host-errors.cpp'])

# Compare identical English UI states with pre-change sources, outside brand bounds.
before=OUT/'before'
with zipfile.ZipFile(ROOT/'evidencias/moonx360-20261028/source-before.zip') as z:
 for name in z.namelist():
  if name.startswith('src/') or name.startswith('third_party/stb/'):
   z.extract(name,before)
for label,src in [('before',before/'src/ui/ui.c'),('after',ROOT/'src/ui/ui.c')]:
 fixture='#include "'+src.as_posix()+'"\n'+r'''
#include <assert.h>
void ui_present(){}
static void save(const char *n){
 ui_draw_software();FILE *f=fopen(n,"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",cv.w,cv.h);
 for(int i=0;i<cv.w*cv.h;i++){unsigned char p[3]={(unsigned char)(cv.px[i]>>16),(unsigned char)(cv.px[i]>>8),(unsigned char)cv.px[i]};fwrite(p,1,3,f);}fclose(f);
}
int main(){
 assert(ui_init(1280,720));ui_draw_enable(true);
 printf("Legacy brand width=%d\n",font_text_width(FONT_BOLD,30,"XENONLIGHT"));
 ui_set_page(UI_HOME);ui_draw_message(ICON_INFO,"Settings","Choose an option.");save("LABEL-message.ppm");
 ui_draw_splash("Loading...",500);save("LABEL-splash.ppm");
 ui_set_language(CONFIG_LANGUAGE_PT_BR);
 ui_draw_message(ICON_ERROR,"Couldn't start the app","The host did not allow this action. On the PC, open Vibepollo or Sunshine and check this device's app and streaming permissions in Client Management.");save("LABEL-permission-pt.ppm");
}
'''
 fixture=fixture.replace('LABEL',label)
 (OUT/(label+'.cpp')).write_text(fixture);build(label,[OUT/(label+'.cpp'),*ui[:3],OUT/'fonts.obj'])
for state,regions in [('message',[(60,25,285,90)]),('splash',[(575,205,705,340),(280,335,1000,420)])]:
 a=Image.open(OUT/f'before-{state}.ppm').convert('RGB');b=Image.open(OUT/f'after-{state}.ppm').convert('RGB')
 diff=ImageChops.difference(a,b)
 for box in regions:diff.paste((0,0,0),box)
 assert diff.getbbox() is None,(state,diff.getbbox())
 b.save(OUT/f'MoonX360-{state}.png')
Image.open(OUT/'after-permission-pt.ppm').save(OUT/'MoonX360-permission-pt.png')
print('PASS: unchanged pixels outside the brand in message and startup screens.')
print('PASS: focused MoonX360 desktop verification; no hardware test.')
