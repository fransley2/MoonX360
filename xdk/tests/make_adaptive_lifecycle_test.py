from pathlib import Path
import sys
r=Path(__file__).resolve().parents[2];s=(r/'src/app.c').read_text()
def function(sig):
 a=s.index(sig);b=s.index('{',a)+1;d=1
 while d:d+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
once=function('static bool stream_once(')
# Exercise the ACTUAL connected-session loop/teardown; transport setup is stubbed.
once=once[once.index('    input_configure('):]
once='static bool stream_once(int app_id,const char *app_title) {\n'+r"""
    bool local_exit,allow_adapt=false;int adapt_reason=0,video_error=VIDEO_STREAM_OK;
    uint32_t started,elapsed,last_network_ms;
    adaptive_stream adaptation={};config adapted=cfg;video_options vo={};
    assert(sessions<4);heights[sessions]=cfg.height;bitrates[sessions]=cfg.bitrate_kbps;
    connection_terminated=false;session_start=tick;live=true;connection_poor=1;sessions++;
"""+once
fixture=r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Limelight.h>
#include "../src/config.h"
#include "../src/product_features.h"
#include "../src/video.h"
#include "../src/adaptive_stream.h"
#include "../src/ui/ui.h"
static config cfg;static game_bindings game_bindings;static const char *host_key(){return cfg.host;}
static unsigned adaptive_reconnects;
static int connection_poor,scenario,sessions,stops,reports;
static int heights[4],bitrates[4];static uint32_t durations[4];
static uint32_t tick,session_start;static bool connection_terminated,ui_suspended,stream_return_home,live;
static struct {bool paired;int current_game;} server={true,42};static bool server_ok=true;static char server_error[32];
static uint32_t plat_now_ms(){return tick;}
static void plat_sleep_ms(unsigned ms){tick+=ms;}
static int plat_atomic_load(volatile int *v){return *v;}
static bool plat_system_ui_active(){return scenario==2;}
static void plat_shutdown_stage(const char*){}
static void plat_diagnostics_flush(){}
static void plat_connection_trace_flush(){}
static void plat_diagnostics_checkpoint(const char*){}
static void input_configure(int,int){}
static void input_calibrate(int,int,int,int){}
static void input_stream_start(){}
static void input_stream_stop(){assert(live);}
static bool input_quit_requested(){return tick-session_start>=(scenario==4?22000u:25000u) && (scenario!=0 || adaptive_reconnects>=3 || cfg.height==480);}
// These signatures come from config.h, the actual storage API in this tree.
char *storage_read(const char*,size_t*){assert(false && "unexpected storage read");abort();}
bool storage_write(const char*,const void*,size_t){assert(false && "temporary adaptation must not be persisted");abort();}
int video_get_stream_error(){return VIDEO_STREAM_OK;}
void video_get_stats(video_stats *p){memset(p,0,sizeof(*p));p->avg_decode_us=scenario==3?19000:5000;}
void video_set_network_stats(uint32_t,uint32_t,bool,int,int){}
void LiStopConnection(){assert(live);durations[stops]=tick-session_start;live=false;stops++;connection_terminated=true;} // simulate callback during intentional stop
bool LiGetEstimatedRttInfo(uint32_t *r,uint32_t *v){*r=10;*v=0;return true;}
int LiGetPendingVideoFrames(){return scenario==3?4:0;}
int LiGetPendingAudioDuration(){return 0;}
static void save_session_report(uint32_t){assert(!live);reports++;}
static void record_session(int,const char*,uint32_t){assert(!live);}
static void message(int,const char*,const char*){assert(false);}
static bool show_stream_stats(uint32_t){return false;}
void ui_draw_busy(const char*,const char*,uint32_t){assert(!live);}
void ui_present(){}
static void refresh_server(){assert(!live);}
static void history_text(char *out,size_t n,const char *in){snprintf(out,n,"%s",in);}
/* CODE */
static void run_case(int which,bool automatic){
 scenario=which;sessions=stops=reports=0;tick=100;stream_return_home=false;
 config_defaults(&cfg);config_apply_preset(&cfg,CONFIG_PROFILE_1080_SMOOTH);cfg.adaptive_quality=automatic;
 config saved=cfg;stream(42,"Test game");assert(!memcmp(&saved,&cfg,sizeof(cfg)));
 assert(stops==sessions && reports==sessions);
 assert(stream_return_home && !live);
}
int main(){
 run_case(0,true);assert(sessions==4 && adaptive_reconnects==3); // bounded network adaptation
 run_case(1,false);assert(sessions==1); // manual mode never changes
 run_case(2,true);assert(sessions==1); // Guide suppresses false degradation
 run_case(3,true);assert(sessions==3 && adaptive_reconnects==2);
 // Warmup is 15 s; eight 1 Hz pressure samples end at 22 s in each session.
 // Decode pressure lowers 1080 -> 720 -> 480. At the 480 floor no further
 // reduction is possible, so the third session reaches the 25 s local exit.
 assert(heights[0]==1080 && heights[1]==720 && heights[2]==480);
 assert(bitrates[0]==20000 && bitrates[1]==15000 && bitrates[2]==8000);
 assert(durations[0]==22000 && durations[1]==22000 && durations[2]==25000);
 run_case(4,true);assert(sessions==1 && adaptive_reconnects==0); // local quit at adaptation threshold wins
 puts("PASS: actual stream loop/teardown/resume, 3-reconnect cap, original settings restored, no live persistence, manual/Guide suppression, stop callback tolerance");
}
"""
Path(sys.argv[1]).write_text(fixture.replace('/* CODE */',once+'\n'+function('static void stream(')))
