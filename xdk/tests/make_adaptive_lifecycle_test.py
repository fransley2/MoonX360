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
    bool local_exit,allow_adapt=false,watchdog_reconnect=false;int adapt_reason=0,video_error=VIDEO_STREAM_OK;
    uint32_t started,elapsed,last_network_ms;
    adaptive_stream adaptation={};perf_pressure pressure_previous={};perf_pressure_read(&pressure_previous);video_options vo={};
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
#include "../src/aurora_launch.h"
#include "../src/ui/ui.h"
static config cfg;static game_bindings game_bindings;static const char *host_key(){return cfg.host;}
static bool aurora_direct_active;static moonx_launch_data aurora_direct_data;
static unsigned adaptive_reconnects;
static unsigned video_reconnects;
static int connection_poor,scenario,sessions,stops,reports,recommendations;
static int heights[4],bitrates[4];static uint32_t durations[4];
static uint32_t tick,session_start;static bool connection_terminated,ui_suspended,stream_return_home,live;
static struct {bool paired;int current_game;} server={true,42};static bool server_ok=true;static char server_error[32];
static uint32_t plat_now_ms(){return tick;}
static void plat_sleep_ms(unsigned ms){tick+=ms;}
static int plat_atomic_load(volatile int *v){return *v;}
static bool plat_system_ui_active(){return scenario==2;}
static void plat_shutdown_stage(const char*){}
static void cl_log(const char*,...){}
static void plat_log(const char*){}
static void plat_diagnostics_flush(){}
static void plat_connection_trace_flush(){}
static void plat_diagnostics_checkpoint(const char*){}
static void input_configure(int,int){}
static void input_calibrate(int,int,int,int){}
static void input_stream_start(){}
static void input_stream_stop(){assert(live);}
static bool input_quit_requested(){return tick-session_start>=(scenario==4?22000u:25000u);}
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
static void save_video_stall_report(int,const char*){assert(!live);}
static void record_session(int,const char*,uint32_t){assert(!live);}
static void message(int,const char*,const char*){assert(!live);recommendations++;}
static bool confirm_action(const char*,const char*,const char*){assert(!live);return false;}
static bool show_stream_stats(uint32_t){return false;}
void ui_draw_busy(const char*,const char*,uint32_t){assert(!live);}
void ui_present(){}
static void refresh_server(){assert(!live);}
static void history_text(char *out,size_t n,const char *in){snprintf(out,n,"%s",in);}
// Switch preflight is exercised by switch-app; unrelated lifecycle fixture.
static bool prepare_selected_app(int){return true;}
static void remember_played_art(int){}
/* CODE */
static void run_case(int which,bool automatic){
 scenario=which;sessions=stops=reports=recommendations=0;tick=100;stream_return_home=false;
 config_defaults(&cfg);config_apply_preset(&cfg,CONFIG_PROFILE_1080_SMOOTH);cfg.fps=60;cfg.bitrate_kbps=20000;cfg.stream_profile=0;cfg.adaptive_quality=automatic;
 config saved=cfg;stream(42,"Test game");assert(!memcmp(&saved,&cfg,sizeof(cfg)));
 assert(stops==sessions && reports==sessions);
 assert(!stream_return_home && !live); // Clean disconnect returns to the calling gallery/Home.
}
int main(){
 for(int which=0;which<5;which++) {
     run_case(which,which!=1);
     assert(sessions==1 && adaptive_reconnects==0 && heights[0]==1080 && bitrates[0]==20000);
     assert(durations[0]==(which==4?22000u:25000u));
     assert(recommendations==(which==3?1:0));
 }
 puts("PASS: actual connected loop never adapts/reconnects mid-game; sustained decoder advice after teardown; poor hint/Guide/manual suppression; preferences restored and no live persistence");
}
"""
Path(sys.argv[1]).write_text(fixture.replace('/* CODE */',once+'\n'+function('static void stream(').replace('stream_session(app_id, stable_title)','stream_once(app_id, stable_title)')))
