"""Exercise the actual render tick at the timeout/frame-arrival boundary."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
source = (Path(sys.argv[2]) if len(sys.argv) > 2 else root / 'xdk/src/video_xdk.cpp').read_text()

def function(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

if 'static void render_tick(' in source:
    tick = function('static void render_tick(')
else:
    # Replay the previous real loop once: continue leaves this one iteration.
    body = function('static DWORD WINAPI render_main(')
    body = body[body.index('        if(gfx_system_ui_active()) {'):body.index('\n    }\n\n    d = gfx_begin();')]
    tick = 'static void render_tick(DWORD wake) { D3DDevice *d;uint64_t t0;int s;\n' + body.replace('continue;', 'return;') + '\n}'

fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../src/video.h"
#include "../src/stream_metrics.h"
typedef unsigned DWORD;typedef int32_t LONG;
enum {WAIT_OBJECT_0=0,WAIT_TIMEOUT=258,PIPE_AUTO,PIPE_CACHED,PIPE_LEGACY,PIPE_SWITCHING,PIPE_DISABLED};
static video_stats stats;
static int pipeline,slot_pending,slot_rendering,mailbox_lock;
static unsigned video_presents,idle_presents,guide_presents,fallbacks;
static bool guide,opening,ready,draw_ok,arrive_during_idle,owned;
static uint64_t clock_us;
struct D3DDevice {};
static D3DDevice device;
static uint64_t now_us(){return clock_us+=10;}
static bool gfx_system_ui_active(){return guide;}
static void gfx_present_system_ui(){guide_presents++;}
static int current_pipeline(){return pipeline;}
static void activate_legacy_fallback(){fallbacks++;pipeline=PIPE_LEGACY;}
static void EnterCriticalSection(int *lock){assert(!*lock);*lock=1;}
static void LeaveCriticalSection(int *lock){assert(*lock);*lock=0;}
static LONG InterlockedIncrement(volatile LONG *p){return ++*p;}
static bool gfx_launch_pending(){return opening;}
static D3DDevice *gfx_begin(){assert(!owned);owned=true;return &device;}
static void gfx_end(){assert(owned);owned=false;}
static void gfx_launch_idle(D3DDevice*){
    assert(owned);idle_presents++;
    if(arrive_during_idle){ready=true;slot_pending=1;}
}
static int draw_cached_frame(){if(!ready || !draw_ok)return 0;ready=false;video_presents++;return 1;}
static bool draw_frame(int s){assert(s>=0 && slot_rendering==s);slot_rendering=-1;if(!draw_ok)return false;video_presents++;return true;}
/* ACTUAL_TICK */
static void reset(int mode){
    stats=video_stats();pipeline=mode;slot_pending=slot_rendering=-1;mailbox_lock=0;
    video_presents=idle_presents=guide_presents=fallbacks=0;
    guide=ready=arrive_during_idle=owned=false;opening=draw_ok=true;clock_us=0;
}
int main(){
    for(int mode=PIPE_CACHED;mode<=PIPE_LEGACY;mode++){
        reset(mode);ready=true;slot_pending=1;
        render_tick(WAIT_TIMEOUT);
        assert(video_presents==1 && !idle_presents && stats.rendered==1);
        // A frame arrives during the idle Present: leave it for the next wake.
        reset(mode);arrive_during_idle=true;
        render_tick(WAIT_TIMEOUT);
        assert(idle_presents==1 && !video_presents && !stats.rendered && ready);
        render_tick(WAIT_OBJECT_0);
        assert(video_presents==1 && idle_presents==1 && stats.rendered==1);
        reset(mode);render_tick(WAIT_OBJECT_0);assert(!video_presents && !idle_presents);
        reset(mode);opening=false;render_tick(WAIT_TIMEOUT);assert(!idle_presents);
        reset(mode);guide=ready=true;slot_pending=1;
        render_tick(WAIT_TIMEOUT);assert(guide_presents==1 && !video_presents && !idle_presents && ready);
    }
    reset(PIPE_LEGACY);slot_pending=1;draw_ok=false;render_tick(WAIT_TIMEOUT);
    assert(stats.dropped==1 && !stats.rendered && idle_presents==1);
    reset(PIPE_SWITCHING);slot_pending=1;render_tick(WAIT_TIMEOUT);
    assert(fallbacks==1 && video_presents==1 && !idle_presents);
    puts("PASS: one Present per wake; cached/legacy timeout races, Guide heartbeat, fallback and no-frame behavior.");
}
'''
Path(sys.argv[1]).write_text(fixture.replace('/* ACTUAL_TICK */', tick))
