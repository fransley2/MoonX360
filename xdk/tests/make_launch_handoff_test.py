"""Real task pump and stage callback, with two real host threads and fake GPU ownership."""
from pathlib import Path
import sys
r=Path(__file__).resolve().parents[2];s=(r/'src/app.c').read_text()
def fn(sig):
 a=s.index(sig);b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
a=s.index('struct task {');b=s.index('};',a)+2
fixture=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>
#include <Limelight.h>
#include "../src/ui/ui.h"
#include "../src/launch_cover.h"
#include "../src/connection_flow.h"
#define _XBOX
static void launch_set_status(enum launch_status){}
struct plat_thread {std::thread value;};
static unsigned task_generation;
static bool ui_suspended,task_start_failed,launch_connecting;
static volatile int launch_handoff,task_cancel,task_phase;
static bool task_was_cancelled,task_timed_out,task_interrupt_transport;static unsigned task_limit_ms=30000;
static void task_state(int n){task_phase=n;}static void gs_set_request_cancel(volatile int*){}
static ui_app launch_app;
static uint32_t launch_ui_started,connection_stage_ms;
static char stage_text[128];
static std::mutex atom_lock;
static std::atomic<bool> renderer(false),menu(false),released(false),prepared(false);
static std::thread::id main_owner;
static int draws,prepares,releases,presents;
static int plat_atomic_load(volatile int *p){std::lock_guard<std::mutex> guard(atom_lock);return *p;}
static void plat_atomic_store(volatile int *p,int n){std::lock_guard<std::mutex> guard(atom_lock);*p=n;}
static uint32_t plat_now_ms(){return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static void plat_sleep_ms(unsigned ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
static plat_thread *plat_thread_start(void *(*fn)(void*),void *arg){return new plat_thread{std::thread([=]{fn(arg);})};}
static void plat_thread_join(plat_thread *t){t->value.join();delete t;}
static void plat_log(const char*){}
static void cl_log(const char*,...){}
const char *LiGetStageName(int){return "Video init";}
static void plat_diagnostics_checkpoint(const char*){}
static void message(int,const char*,const char*){assert(false);}
static bool launch_active(){return true;}
static bool launch_cancelled(){return false;}
static void launch_command(bool,bool){}
static int input_menu_pressed(){return 0;}
void LiInterruptConnection(){assert(false);}
void ui_draw_launch(const ui_app*,const char*,uint32_t){assert(std::this_thread::get_id()==main_owner && !renderer);draws++;}
void ui_present();
static bool launch_prepare(){assert(std::this_thread::get_id()==main_owner && !renderer);prepared=true;prepares++;return true;}
static void art_launch_tick(){}
static void art_pause(bool){}
static void launch_present(){ui_present();}
static void launch_drop_art(){assert(released && !renderer);}
static void plat_release_menu_textures(){assert(prepared && !renderer);released=true;releases++;}
void ui_present(){assert(std::this_thread::get_id()==main_owner && !renderer);menu=true;presents++;menu=false;}
static void draw(void*,uint32_t){assert(!renderer);}
/* CODE */
static void *connection(void*){
    plat_sleep_ms(60);cl_stage_starting(STAGE_VIDEO_STREAM_INIT);
    assert(released && prepared && plat_atomic_load(&launch_handoff)==2 && !menu);
    renderer=true;plat_sleep_ms(80);assert(!menu);renderer=false;return NULL;
}
int main(){
    main_owner=std::this_thread::get_id();
    for(int session=0;session<10;session++) {
        ui_suspended=false;launch_connecting=true;prepared=released=false;plat_atomic_store(&launch_handoff,0);
        launch_prepare();run_task(connection,NULL,draw,NULL);assert(!renderer && !menu && ui_suspended && prepared && released);
    }
    assert(prepares==10 && releases==10 && presents>=10);
    puts("PASS: actual task/stage handoff on real threads, menu owner snapshots and releases before video init, no simultaneous menu/video presentation, consecutive connections");
}
'''
Path(sys.argv[1]).write_text(fixture.replace('/* CODE */',s[a:b]+'\n'+fn('static void *task_entry(')+'\n'+fn('static void *run_task(')+'\n'+fn('static void cl_stage_starting(')))
