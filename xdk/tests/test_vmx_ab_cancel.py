"""Exercise the actual benchmark screen with a stale remote Close App latch.

The worker/device boundary is simulated; this is not an Xbox timing test.
"""
from pathlib import Path
import importlib.util
import os
import subprocess
import tempfile

root=Path(__file__).resolve().parents[2]
source=(root/'xdk/src/vmx_ab_xdk.h').read_text()
start=source.index('static void vmx_ab_screen(void)')
screen=source[start:]
fixture=r'''
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <string>
typedef void* HANDLE;typedef void* LPVOID;typedef int32_t LONG;typedef uint32_t DWORD;
enum {CREATE_SUSPENDED=1,INFINITE=-1,A_FLAG=1,B_FLAG=2,ICON_ERROR=1,ICON_INFO=0};
struct vmx_ab_native {FILE *file;volatile LONG cancel,done,rows;int result;char path[320];};
static vmx_ab_native *job;static bool running,stale_close,press_back;
static int remote_close_queries,worker_steps,observed_cancel;static std::string result_message;
static const char *storage_dir(){return ".";}
static DWORD GetTickCount(){static DWORD value=100;return ++value;}
static LONG InterlockedCompareExchange(volatile LONG *p,LONG value,LONG compare){LONG old=*p;if(old==compare)*p=value;return old;}
static LONG InterlockedExchange(volatile LONG *p,LONG value){LONG old=*p;*p=value;return old;}
static DWORD vmx_ab_worker(LPVOID){return 0;}
static HANDLE CreateThread(void*,unsigned,DWORD(*)(LPVOID),void *context,int,void*){job=(vmx_ab_native*)context;return (HANDLE)1;}
static void XSetThreadProcessor(HANDLE,int){}static void ResumeThread(HANDLE){}
static void WaitForSingleObject(HANDLE,int){}static void CloseHandle(HANDLE){}
static void plat_log(const char*){}
static void ui_draw_busy(const char*,const char*,DWORD){}static void ui_present(){}
static void ui_draw_message(int,const char*,const char *text){result_message=text;}
static int input_menu_pressed(){return job->done?A_FLAG:press_back?B_FLAG:0;}
static bool input_close_app_requested(){remote_close_queries++;return stale_close;}
static void Sleep(int){
    worker_steps++;observed_cancel=job->cancel;
    assert(job->cancel==(press_back?1:0));
    job->result=job->cancel?2:0;fclose(job->file);job->file=NULL;job->done=1;
}
/* ACTUAL_SCREEN */
int main(){
    for(int cancel=0;cancel<2;cancel++){
        stale_close=true;press_back=cancel!=0;worker_steps=remote_close_queries=0;result_message.clear();
        vmx_ab_screen();assert(worker_steps==1 && observed_cancel==cancel);
        assert(remote_close_queries==0);
        assert(result_message.find(cancel?"Cancelled":"Integrity and benchmark completed")!=std::string::npos);
    }
    puts("PASS: actual benchmark screen ignores stale remote Close App, retains new B cancellation and joins worker before returning.");
}
'''.replace('/* ACTUAL_SCREEN */',screen)
vcvars=Path(os.environ.get('VS_VCVARS64',r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'))
r=subprocess.run(f'cmd.exe /d /s /c ""{vcvars}" >nul && set"',capture_output=True,text=True,check=True)
env=dict(os.environ)
for line in r.stdout.splitlines():
    if '=' in line and not line.startswith('='):
        k,v=line.split('=',1);env[k.upper()]=v
cl=next(str(Path(p)/'cl.exe') for p in env['PATH'].split(';') if (Path(p)/'cl.exe').exists())
out=root/'host-tests-vmx-cancel';out.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(dir=out) as folder:
    path=Path(folder);(path/'test.cpp').write_text(fixture)
    subprocess.run([cl,'/nologo','/EHsc','/O2','/D_CRT_SECURE_NO_WARNINGS','test.cpp','/Fetest.exe'],cwd=path,env=env,check=True)
    subprocess.run([str(path/'test.exe')],cwd=path,env=env,check=True)
