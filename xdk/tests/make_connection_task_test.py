from pathlib import Path
import sys
r=Path(__file__).resolve().parents[2];s=(r/'src/app.c').read_text()
def function(sig):
 a=s.index(sig);b=s.index('{',a)+1;d=1
 while d:d+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
a=s.index('struct task {');b=s.index('};',a)+2
code=s[a:b]+'\n'+function('static void *task_entry(')+'\n'+function('static void *run_task(')
fixture=r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../src/connection_flow.h"
#define B_FLAG 1
static volatile int task_cancel;static bool delayed;static unsigned cancel_at,interrupts;
static void art_pause(bool){}
static uint32_t tick,duration;static int presents,sleeps,joins;static bool ui_suspended,fail_start;
struct plat_thread {void *(*fn)(void*);void *arg;};static plat_thread pending;
static uint32_t plat_now_ms(){return tick;}
static int plat_atomic_load(volatile int *p){return *p;}
static void plat_atomic_store(volatile int *p,int n){*p=n;}
static void *work(void*){tick+=duration;return (void*)123;}
static plat_thread *plat_thread_start(void *(*fn)(void*),void *arg){pending.fn=fn;pending.arg=arg;return fail_start?NULL:&pending;}
static void plat_thread_join(plat_thread*){joins++;}
static void plat_log(const char *line){assert(strstr(line,"[NET] run_task total:") || strstr(line,"[OP] begin"));}
static void ui_present(){presents++;}
static void draw(void*,uint32_t){}
static void plat_sleep_ms(unsigned ms){assert(ms==4);tick+=ms;sleeps++;if(pending.fn && (!delayed || task_cancel)){void *(*fn)(void*)=pending.fn;pending.fn=NULL;fn(pending.arg);}}
static unsigned task_generation;
static bool task_start_failed,task_was_cancelled,task_timed_out,task_interrupt_transport;
static volatile int task_phase;static unsigned task_limit_ms=30000;
static int input_menu_pressed(){return cancel_at && tick>=cancel_at?B_FLAG:0;}
static void LiInterruptConnection(){interrupts++;}
static void gs_set_request_cancel(volatile int*){}
static void task_state(int state){task_phase=state;}
#define ICON_ERROR 1
static int task_errors;
static void message(int,const char*,const char*){task_errors++;}
/* CODE */
int main(){
 for(int wrap=0;wrap<2;wrap++) {
  tick=wrap?UINT32_MAX-2:0;duration=0;presents=sleeps=joins=0;
  uint32_t start=tick;assert(run_task(work,NULL,draw,NULL)==(void*)123);
  assert(tick-start==4 && !presents && sleeps==1 && joins==1);
 }
 ui_suspended=true;tick=0;duration=100;assert(run_task(work,NULL,draw,NULL)==(void*)123);assert(!presents);
 fail_start=true;uint32_t previous=tick;assert(run_task(work,NULL,draw,NULL)==NULL && task_errors==1 && task_start_failed && tick==previous);
 fail_start=false;ui_suspended=false;delayed=true;tick=0;duration=0;cancel_at=12;task_interrupt_transport=true;
 assert(run_task(work,NULL,draw,NULL)==(void*)123 && task_was_cancelled && !task_timed_out && tick<=16 && interrupts);
 cancel_at=0;tick=0;task_limit_ms=20;
 assert(run_task(work,NULL,draw,NULL)==(void*)123 && !task_was_cancelled && task_timed_out && tick<=24);
 puts("PASS: actual B acknowledgement / cancellation token / transport interrupt and deadline; actual task publication, 4 ms completion observation, no GPU redraw for fast task, suspended UI, thread-start failure stays off UI worker path and tick wrap.");
}
'''
Path(sys.argv[1]).write_text(fixture.replace('/* CODE */',code))
