"""Exercise actual startup loading without SDK calls or minimum display delays."""
from pathlib import Path
import sys
r=Path(__file__).resolve().parents[2]
s=(r/'xdk/src/main_xdk.cpp').read_text()
def fn(sig):
 a=s.index(sig);b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
fixture=r'''
#include <assert.h>
#include <stdio.h>
#include <stddef.h>
static int frames,sleeps,inits,joins,finish_after;static bool fail_start;
struct plat_thread {void *(*fn)(void*);void *arg;};static plat_thread task;
static void video_xdk_init(){inits++;}
static void plat_atomic_store(volatile int *p,int v){*p=v;}
static int plat_atomic_load(volatile int *p){return *p;}
static plat_thread *plat_thread_start(void *(*f)(void*),void *arg){
 if(fail_start)return NULL;task.fn=f;task.arg=arg;
 if(!finish_after){f(arg);task.fn=NULL;}return &task;
}
static void plat_thread_join(plat_thread*){assert(!task.fn);joins++;}
static void splash(const char*){frames++;}
static void plat_sleep_ms(unsigned ms){assert(ms==16);sleeps++;if(sleeps==finish_after){task.fn(task.arg);task.fn=NULL;}}
/* CODE */
int main(){
 finish_after=0;prepare_video();assert(inits==1 && joins==1 && !frames && !sleeps);
 finish_after=3;prepare_video();assert(inits==2 && joins==2 && frames==3 && sleeps==3);
 fail_start=true;prepare_video();assert(inits==3 && joins==2 && frames==3 && sleeps==3);
 puts("PASS: actual startup animates only while work is pending; immediate completion has no delay; joins before return; thread failure safely runs inline.");
}
'''
Path(sys.argv[1]).write_text(fixture.replace('/* CODE */',fn('static void *prepare_video_worker(')+'\n'+fn('static void prepare_video(')))
