"""Desktop CPU/storage benchmark of production artwork routines; never GPU evidence."""
import argparse, json, os, pathlib, subprocess, tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--baseline-source',type=pathlib.Path);a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=True)
vcvars=os.environ.get('VS_VCVARS64',r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat')
r=subprocess.run(f'cmd.exe /d /s /c ""{vcvars}" >nul && set"',capture_output=True,text=True,check=True);env=dict(os.environ)
for line in r.stdout.splitlines():
 if '=' in line and not line.startswith('='):k,v=line.split('=',1);env[k.upper()]=v
cl=next(str(pathlib.Path(d)/'cl.exe') for d in env['PATH'].split(';') if (pathlib.Path(d)/'cl.exe').is_file())
code=r"""
#include <chrono>
#include <vector>
#include <algorithm>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cover_disk.h"
#include "ui/image.h"
#include "ui/canvas.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb/stb_image_write.h"
using Clock=std::chrono::steady_clock;
static double us(Clock::time_point t){return std::chrono::duration<double,std::micro>(Clock::now()-t).count();}
static void output(void *ctx,void *p,int n){auto &v=*(std::vector<unsigned char>*)ctx;v.insert(v.end(),(unsigned char*)p,(unsigned char*)p+n);}
static double percentile(std::vector<double> v,double p){std::sort(v.begin(),v.end());return v[(size_t)((v.size()-1)*p)];}
int main(int argc,char **argv){
 assert(argc==2);std::vector<uint32_t> source(640*850);for(size_t i=0;i<source.size();i++)source[i]=0xff000000u|((i*997u) & 0xffffffu);
 std::vector<unsigned char> png;assert(stbi_write_png_to_func(output,&png,640,850,4,source.data(),640*4));
 printf("{\"environment\":\"Windows desktop CPU and local storage; synthetic PNG; no Xbox/GPU/network\",\"samples\":128,\"sizes\":[");
 for(int mode=0;mode<2;mode++){
 int w=mode?192:150,h=mode?256:200;ui_cover_key key={};strcpy(key.host,"uuid:benchmark");key.app=mode+1;key.w=w;key.h=h;
 std::vector<double> decode,scale,read;uint32_t *last=nullptr;
 for(int n=0;n<128;n++){int sw,sh;auto t=Clock::now();uint32_t *full=image_decode_argb(png.data(),png.size(),&sw,&sh);decode.push_back(us(t));assert(full);t=Clock::now();auto small=image_scale_cover(full,sw,sh,w,h);scale.push_back(us(t));assert(small);free(full);free(last);last=small;}
 cover_disk_cache disk={};assert(cover_disk_write(&disk,argv[1],&key,last,1000));free(last);
 cover_disk_cache cold={};uint32_t fetched;auto t=Clock::now();auto pixels=cover_disk_read(&cold,argv[1],&key,1001,&fetched);double cold_us=us(t);assert(pixels);free(pixels);
 for(int n=0;n<128;n++){t=Clock::now();pixels=cover_disk_read(&cold,argv[1],&key,1002+n,&fetched);read.push_back(us(t));assert(pixels);free(pixels);}
 printf("%s{\"width\":%d,\"height\":%d,\"argb_bytes\":%d,\"twelve_argb_bytes\":%d,\"decode_p50_us\":%.3f,\"decode_p95_us\":%.3f,\"scale_p50_us\":%.3f,\"scale_p95_us\":%.3f,\"cold_scan_and_read_us\":%.3f,\"warm_read_p50_us\":%.3f,\"warm_read_p95_us\":%.3f,\"gpu_upload_us\":null}",mode?",":"",w,h,w*h*4,w*h*4*12,percentile(decode,.5),percentile(decode,.95),percentile(scale,.5),percentile(scale,.95),cold_us,percentile(read,.5),percentile(read,.95));
 }
 puts("]}");
}
"""
variants={'candidate':ROOT/'src'}
if a.baseline_source:variants={'baseline':a.baseline_source.resolve()/'src',**variants}
results={}
for name,include in variants.items():
 source=a.out/(name+'.cpp');source.write_text(code);exe=a.out/(name+'.exe')
 cmd=[cl,'/nologo','/O2','/EHsc','/std:c++17','/D_CRT_SECURE_NO_WARNINGS','/I'+str(include),'/I'+str(ROOT/'src'),'/I'+str(ROOT),str(source),'/Tp'+str(ROOT/'src/ui/image.c'),'/Tp'+str(ROOT/'src/ui/canvas.c'),'/Fe'+str(exe)]
 r=subprocess.run(cmd,cwd=a.out,env=env,capture_output=True,text=True);(a.out/(name+'-compile.log')).write_text(r.stdout+r.stderr);r.check_returncode()
 with tempfile.TemporaryDirectory(prefix='moonx-art-benchmark-') as directory:
  r=subprocess.run([str(exe),directory],capture_output=True,text=True,check=True);results[name]=json.loads(r.stdout)
(a.out/'results.json').write_text(json.dumps(results,indent=2));print(json.dumps(results,indent=2))
