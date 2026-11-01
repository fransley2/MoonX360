"""Execute production launch entry/preflight, before transport initialization."""
from pathlib import Path
import sys

root=Path(__file__).resolve().parents[2]
source=(root/'src/app.c').read_text(encoding='utf-8')
def function(signature):
    a=source.index(signature);b=source.index('{',a)+1;depth=1
    while depth:depth+=(source[b]=='{')-(source[b]=='}');b+=1
    return source[a:b]

once=function('static bool stream_once(')
prefix=once[:once.index('    /* A cached online')]+ '\nreturn true;\n}'
assert once.count('    launch_begin(')==1 and once.count('    art_launch_begin(')==1 and once.count('ui_launch_begin(')==1
assert once.index('launch_present();')<once.index('discovery_pause();')<once.index('refresh_server();')
retry=once[once.index('    if(cj.ret!=0 && !launch_cancelled()'):once.index('    if(task_was_cancelled || task_timed_out)',once.index('    if(cj.ret!=0 && !launch_cancelled()'))]
assert 'launch_begin(' not in retry and 'launch_end(' not in retry and 'launch_prepare(' not in retry
assert 'ui_draw_launch' not in source

fixture=r'''
#define _XBOX 1
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "../src/config.h"
#include "../src/connection_flow.h"
#include "../src/launch_settings.h"
#include "../src/launch_cover.h"
#include "../src/ui/ui.h"
static config cfg;
static bool stream_return_home,launch_connecting,task_was_cancelled,server_ok,active,cancelled;
static bool fail_host,cancel_refresh,pair_needed,pair_cancel,pair_failure;
static int busy_draws,pair_draws,begins,ends,presents,refreshes,recovers,phase;
static volatile int launch_handoff,task_cancel;
static uint32_t launch_ui_started;
static char launch_pair_pin[5];static ui_app launch_app;
static struct {int id;} apps[1]={{42}};
static int app_count=1,home_art;
static uint32_t pixels=123,*app_art[1]={&pixels},art_generation[1]={1};
static struct {bool paired;int current_game;} server;
static const char *host_key(){return "uuid:PC";}
static const char *home_key(){return host_key();}
static const uint32_t *home_art_get(int*,const char*,int,uint32_t*){return NULL;}
static const char *selected_profile_name(){return "Smooth";}
char *storage_read(const char*,size_t*){errno=ENOENT;return NULL;}
bool storage_write(const char*,const void*,size_t){return true;}
static void crypto_random(void *p,size_t n){memset(p,1,n);}
static uint32_t plat_now_ms(){return 100;}
static void plat_atomic_store(volatile int *p,int n){*p=n;}
static int plat_atomic_load(volatile int *p){return *p;}
void launch_begin(unsigned wait,bool){assert(!active);assert(wait==0 || wait==5000);active=true;cancelled=false;begins++;}
bool launch_active(){return active;}
bool launch_cancelled(){return cancelled;}
void launch_end(){assert(active);active=false;ends++;}
void launch_command(bool cancel,bool){cancelled|=cancel;}
void launch_set_status(launch_status status){assert(active);phase=status;}
bool launch_prepare(){assert(active);return true;}
void launch_present(){assert(active);presents++;}
bool launch_offer_art(const uint32_t*,int,int,uint32_t){return true;}
void ui_art_size(int *w,int *h){*w=*h=1;}
void ui_launch_begin(const ui_app*,uint32_t,bool){assert(active);}
void ui_launch_session(const char*,int,int,int,int){}
void ui_launch_pair_pin(const char *pin){assert(strlen(pin)==4);}
static void ui_draw_software(){}
static void draw_launch(void*,uint32_t){assert(active);}
static void art_launch_begin(int){assert(active);}
static void art_launch_tick(){assert(active);}
static void discovery_pause(){assert(active && presents);}
static bool recover_host_ui(){assert(!active);recovers++;return false;}
struct busy_view {const char *title;char message[200];};
static volatile int task_phase;
void ui_draw_busy(const char*,const char*,uint32_t){busy_draws++;}
/* BUSY */
static void refresh_server(){
    assert(active);refreshes++;busy_view v={"Checking host",""};draw_busy(&v,100);
    if(cancel_refresh){task_was_cancelled=true;launch_command(true,false);}
    server_ok=!fail_host;server.paired=!pair_needed;
}
static void do_pair(){assert(active);launch_set_status(LAUNCH_PAIRING);assert(strlen(launch_pair_pin)==4);pair_draws++;server.paired=!pair_cancel && !pair_failure;pair_needed=false;if(pair_failure)launch_end();if(pair_cancel){task_was_cancelled=true;launch_command(true,false);}}
static void message(int,const char*,const char*){assert(!active);}
#define ICON_INFO 1
/* PREFLIGHT */
int main(){
    cfg.width=1280;cfg.height=720;cfg.fps=60;cfg.bitrate_kbps=12000;cfg.launch_cover_ms=5000;
    const char *titles[]={"Desktop","Steam Big Picture","Normal game"};
    for(int i=0;i<3;i++){
        begins=ends=presents=refreshes=0;task_was_cancelled=false;
        assert(stream_once(42,titles[i]));assert(begins==1 && ends==0 && refreshes==1 && presents==1 && !busy_draws);launch_end();
    }
    cancel_refresh=true;task_was_cancelled=false;assert(!stream_once(42,"Game") && !active);cancel_refresh=false;
    fail_host=true;task_was_cancelled=false;assert(!stream_once(42,"Game") && !active && recovers==1);fail_host=false;
    pair_needed=true;task_was_cancelled=false;assert(stream_once(42,"Game") && pair_draws==1 && active);launch_end();
    pair_needed=pair_cancel=true;task_was_cancelled=false;assert(!stream_once(42,"Game") && !active && pair_draws==2);
    pair_needed=pair_failure=true;pair_cancel=false;task_was_cancelled=false;int prior=refreshes;assert(!stream_once(42,"Game") && !active && refreshes==prior+1);pair_failure=false;
    pair_needed=pair_cancel=false;task_was_cancelled=false;server.current_game=99;assert(!stream_once(42,"Game") && !active);
    puts("PASS: actual Desktop/Steam/game entry starts exactly one owner before discovery/HTTP, no busy overlay; cancel/offline/pairing/other-app exits; stereo retry retains owner");
}
'''
Path(sys.argv[1]).write_text(fixture.replace('/* BUSY */',function('static void draw_busy(')).replace('/* PREFLIGHT */',prefix),encoding='utf-8')
