"""Exercise real post-stop history, explicit Apply and reconnect title lifetime."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
source=(root/'src/app.c').read_text()
def function(signature):
    start=source.index(signature);end=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
fixture=r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <string>
#include <deque>
#include "../src/config.h"
#include "../src/product_features.h"
#include "../src/launch_settings.h"
#include "../src/video.h"
#include "../src/ui/ui.h"
#include "../src/session_history.h"
#include "../src/library_state.h"
#define MAX_APPS 64
static struct {int id;char name[64],uuid[80];} apps[MAX_APPS];
static uint32_t *app_art[MAX_APPS],art_generation[MAX_APPS];
static int app_count;
static library_favorites favorites;
static config cfg;
static const char *host_key(){return cfg.host;}
static game_bindings game_bindings;
static char library_query[64];
static session_history session_history;
static bool history_save_failed,stopped=true,fail_save,server_ok=true;
static performance_sample sample;
static video_stats stats;
static int writes,details,messages,reconnects,stream_calls;
static bool connection_terminated;static int termination_error;static std::string last_report;
static unsigned adaptive_reconnects;
static struct {bool paired;int current_game;} server={true,0};
static char server_error[80],unstable_title[64];
static std::deque<int> buttons;
char *storage_read(const char*,size_t*) {return NULL;}
bool storage_write(const char *name,const void *data,size_t length) {assert(stopped);writes++;if(!strcmp(name,"last-session.txt"))last_report.assign((const char*)data,length);return !fail_save;}
void video_get_stats(video_stats *out) {assert(stopped);*out=stats;}
void video_get_session_performance(performance_sample *out) {assert(stopped);*out=sample;}
static void plat_log(const char*) {}
static int wait_button() {assert(!buttons.empty());int b=buttons.front();buttons.pop_front();return b;}
static void message(int,const char*,const char*) {messages++;}
static void session_details(const session_record*) {details++;}
void ui_draw_advisor(const ui_stat*,int,const char*,bool) {}
void ui_draw_performance(const char*,const char*,float,const char*,const char*,const char*,const char*,bool) {}
static int navigation_pending=-1;
static bool tab_key(int key,int page);
static int settings_initial_group,settings_calls,history_calls;
static void settings_menu() {settings_calls++;}
static void launch_settings_menu(int){settings_calls++;}
static void game_profile_menu(int){settings_calls++;}
static void cycle_game_profile(int,int){}
static const char *game_profile_label(int){return "Profile";}
static void history_menu() {history_calls++;}
static ui_host host_view() {ui_host h={"PC","PC",HOST_PAIRED,NULL,NULL};return h;}
void ui_draw_app_details(const ui_host*,const ui_app *a,const char*,const char*,int selected) {assert(a->name && selected>=0 && selected<6);}
void ui_set_page(int) {}
int ui_location_enter(const char*){return 0;}
void ui_location_restore(int){}
void ui_present() {}
static void refresh_server() {reconnects++;}
static bool stream_once(int,const char *title) {
    assert(!strcmp(title,"Original title"));stream_calls++;strcpy(unstable_title,"Overwritten by history push");
    return stream_calls==1;
}
static bool stop_ok=true;
static int stop_requests;
static void *quit_task(void *p){*(bool*)p=stop_ok;stop_requests++;return NULL;}
static void run_busy(const char*,const char*,void *(*task)(void*),void *p){task(p);}
static const char *gs_error(){return "Stop failed";}
const char *ui_translate(const char *s){return s;}
int ui_location_enter_raw(const char *s){return ui_location_enter(s);}
static void remember_played_art(int){}
static bool stream_session(int id,const char *title){return stream_once(id,title);}
/* ACTUAL_FUNCTIONS */
int main() {
    config_defaults(&cfg);strcpy(cfg.host,"PC");
    sample.frames=600;sample.decode_avg_us=33000;sample.queue_peak_us=300000;
    sample.shown_fps=27;sample.tail_valid=true;sample.decode_p99_us=41000;
    record_session(42,"Game",10000);
    assert(writes==1 && session_history.count==1 && session_history.records[0].cause==PERF_CPU);
    assert(session_history.records[0].app_id==42);
    cfg.history_enabled=false;record_session(43,"Not recorded",10000);
    assert(writes==1 && session_history.count==1);cfg.history_enabled=true;
    fail_save=true;record_session(44,"RAM only",10000);
    assert(history_save_failed && session_history.records[0].app_id==44);fail_save=false;
    record_session(45,"Saved later",10000);assert(!history_save_failed);
    config_apply_preset(&cfg,CONFIG_PROFILE_SMOOTH);config before=cfg;
    buttons.push_back(B_FLAG);advisor_menu();assert(!memcmp(&before,&cfg,sizeof(cfg)));
    buttons.push_back(X_FLAG);buttons.push_back(B_FLAG);advisor_menu();assert(details==1 && !memcmp(&before,&cfg,sizeof(cfg)));
    buttons.push_back(A_FLAG);advisor_menu();assert(config_video_preset(&cfg)==CONFIG_PROFILE_STANDARD);
    assert(cfg.bitrate_kbps==12000 && cfg.fast_decode && cfg.vsync);
    session_history.records[0].grade=PERF_GOOD;config_apply_preset(&cfg,CONFIG_PROFILE_SMOOTH);before=cfg;
    buttons.push_back(A_FLAG);buttons.push_back(B_FLAG);advisor_menu();assert(!memcmp(&before,&cfg,sizeof(cfg)));
    app_count=3;apps[0].id=45;strcpy(apps[0].name,"Desktop");apps[1].id=46;strcpy(apps[1].name,"Game");apps[2].id=47;strcpy(apps[2].name,"Other");
    int map[MAX_APPS];assert(library_map(2,map)==3 && map[0]==0 && map[2]==2);
    assert(library_map(3,map)==1 && map[0]==0);
    assert(library_map(0,map)==1 && map[0]==0); // only recorded app still offered by this host
    assert(library_favorite_toggle(&favorites,"PC",46));assert(library_map(1,map)==1 && map[0]==1);
    for(int page=0;page<UI_PAGE_COUNT;page++) {
        assert(tab_key(LB_FLAG,page) && navigation_pending==(page+3)%4);
        assert(tab_key(RB_FLAG,page) && navigation_pending==(page+1)%4);
    }
    navigation_pending=-1;assert(!tab_key(A_FLAG,UI_HOME));
    for(int i=0;i<40;i++) {
        buttons.push_back(B_FLAG);assert(!app_details(0));
        buttons.push_back(A_FLAG);assert(app_details(0));
    }
    buttons.push_back(DOWN_FLAG);buttons.push_back(A_FLAG);buttons.push_back(B_FLAG);assert(!app_details(0));assert(settings_calls==1);
    buttons.push_back(DOWN_FLAG);buttons.push_back(DOWN_FLAG);buttons.push_back(A_FLAG);buttons.push_back(B_FLAG);assert(!app_details(0));assert(library_favorite_has(&favorites,"PC",45));
    buttons.push_back(DOWN_FLAG);buttons.push_back(DOWN_FLAG);buttons.push_back(DOWN_FLAG);buttons.push_back(A_FLAG);buttons.push_back(B_FLAG);assert(!app_details(0));assert(details==2);
    server.current_game=45;stop_ok=true;
    buttons={UP_FLAG,UP_FLAG,A_FLAG,B_FLAG};assert(!app_details(0));assert(stop_requests==1 && server.current_game==0);
    server.current_game=45;stop_ok=false;
    buttons={UP_FLAG,UP_FLAG,A_FLAG,B_FLAG};assert(!app_details(0));assert(stop_requests==2 && server.current_game==45);
    server.current_game=0;
    buttons={UP_FLAG,UP_FLAG,A_FLAG,B_FLAG};assert(!app_details(0));assert(stop_requests==2);
    reconnects=0;
    strcpy(cfg.host,"Another PC");assert(library_map(0,map)==0 && library_map(1,map)==0);strcpy(cfg.host,"PC");
    strcpy(unstable_title,"Original title");stream(7,unstable_title);assert(stream_calls==2 && reconnects==0);
    stream_calls=0;reconnects=0;server_ok=false;strcpy(unstable_title,"Original title");
    stream(7,unstable_title);assert(stream_calls==2 && reconnects==0); // preflight belongs to stream_once, covered by launch-flow
    // Reports must record the effective decoder flag, including factory overrides.
    cfg.fast_decode=false;cfg.image_quality=2;cfg.session_report=true;config_apply_preset(&cfg,CONFIG_PROFILE_SMOOTH);
    save_session_report(10000);assert(last_report.find("fast_decode=1\nfast_decode_saved=0\nimage_quality=1\n")!=std::string::npos);
    config_customize_stream(&cfg);cfg.fast_decode=false;save_session_report(10000);
    assert(last_report.find("fast_decode=0\nfast_decode_saved=0\nimage_quality=1\n")!=std::string::npos);
    unsigned previous_writes=writes;cfg.session_report=false;save_session_report(10000);assert(writes==previous_writes);
    puts("PASS: actual post-stop history/report I/O, effective Fast Decode, disabled/full-write-error cases, explicit Apply/B/Details, stable reconnect title and failed reconnect.");
}
'''
functions='\n'.join(function(name) for name in ['static void save_session_report(','static void record_session(',
    'static const struct session_record *latest_session(', 'static void advisor_menu(void)\n{',
    'static int library_map(',
    'static bool tab_key(', 'static bool app_details_for(', 'static bool app_details(',
    'static void stream(int app_id,'])
assert source.index('    record_session(app_id,app_title,elapsed);')>source.index('    LiStopConnection();',source.index('static bool stream_once('))
Path(sys.argv[1]).write_text(fixture.replace('/* ACTUAL_FUNCTIONS */',functions))

