"""Exercise production Home and Library button dispatch with deterministic input."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
app=(root/'src/app.c').read_text()
ui=(root/'src/ui/ui.c').read_text()
def function(source,signature):
    a=source.index(signature);b=source.index('{',a)+1;depth=1
    while depth:
        depth+=(source[b]=='{')-(source[b]=='}');b+=1
    return source[a:b]
fixture=r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <deque>
#include <Limelight.h>
#include "../src/config.h"
#include "../src/app_list_cache.h"
static app_list_cache app_cache;
static uint32_t plat_now_ms();
#include "../src/settings_layout.h"
#include "../src/ui/ui.h"
#include "../src/session_history.h"
#include "../src/library_state.h"
#include "../src/host_cache.h"
#include "../src/home_art.h"
#define MAX_APPS 64
struct Done {};
struct plat_thread {};
static config cfg;
static const char *host_key(){return cfg.host;}
static int selected_host_index();
static bool wake_accept=true;static int wake_calls;
static bool wake_host(){wake_calls++;return wake_accept;}
static struct {int current_game;bool paired;char hostname[64],address[64];} server;
static struct {int id;char name[64];} apps[MAX_APPS];
static uint32_t *app_art[MAX_APPS],art_generation[MAX_APPS];
static int app_count,art_version,art_cancel;
static bool manual_accept,inject_host;
static int manual_calls,host_rows_seen;
static session_history session_history;
static library_favorites favorites;
static host_cache hosts;
static int selected_host_index(){for(int i=0;i<hosts.count;i++)if(!strcmp(hosts.hosts[i].ip,cfg.host))return i;return -1;}
static home_art_cache home_art;
static struct {bool used;} attempts[32];
static bool server_ok=true,stream_return_home,host_cache_save_failed,details_play;
static unsigned next_discovery=99,next_socket_retry=99,discovery_round=3;
static int navigation_pending=-1,settings_initial_group=-1;
static int library_size=1,last_play_id,first_play_id,last_resume_id;static bool add_recent_on_play;
static int plays,details,pauses,library_calls,context_calls,loads,draws,settings_calls,host_panels;
static std::deque<int> keys;
char *storage_read(const char*,size_t*){return NULL;}
bool storage_write(const char*,const void*,size_t){return true;}
static int input_menu_pressed(){if(keys.empty())throw Done();int key=keys.front();keys.pop_front();return key;}
static void plat_sleep_ms(unsigned){}
static const char *plat_console_ip(){return "192.168.1.2";}
static void plat_shutdown_stage(const char*){}
static bool discovery_tick(){
    if(inject_host){inject_host=false;hosts.count=1;strcpy(hosts.hosts[0].name,"Bedroom PC");
        strcpy(hosts.hosts[0].ip,"192.168.1.9");hosts.hosts[0].online=true;return true;}
    return false;
}
static void discovery_pause(){pauses++;}
static void refresh_server(){server_ok=true;}
static void *refresh_task(void*){return NULL;}
static void do_pair(){server.paired=true;}
static void clear_apps(){app_count=0;app_cache.valid=false;}
static void settings_menu(){settings_calls++;}
static void app_menu(){library_calls++;}
static void message(int,const char*,const char*){}
static void host_options_menu(){context_calls++;}
static void hosts_menu(int page){assert(page==UI_HOME);host_panels++;}
static void history_menu(){}
static bool host_details(){details++;return details_play;}
static bool tab_from_host_settings;
static void manage_host(){details++;if(tab_from_host_settings)navigation_pending=UI_STREAM;}
static bool edit_ip(char *ip,size_t cap){manual_calls++;if(manual_accept){snprintf(ip,cap,"192.168.1.10");return true;}return false;}
const char *ui_translate(const char *s){return s;}
static ui_host host_view(){ui_host h={"PC","PC",HOST_PAIRED,NULL,NULL};return h;}
static const char *app_name(int id){for(int i=0;i<app_count;i++)if(apps[i].id==id)return apps[i].name;return NULL;}
static void stream(int id,const char *name){
    assert(id>=42 && id<42+library_size && !strcmp(name,apps[id-42].name));
    if(!plays)first_play_id=id;last_play_id=id;plays++;
    if(add_recent_on_play){session_record r={};strcpy(r.host,cfg.host);strcpy(r.app,"Stale title");r.app_id=43;history_push(&session_history,&r);stream_return_home=true;}
}
static bool app_details(int){details++;return details_play;}
static bool app_details_for(int id,const char *name,int index){assert(index>=0 && index<app_count && apps[index].id==id && !strcmp(name,apps[index].name));details++;return details_play;}
static void library_context(int*){context_calls++;}
static int library_map(int,int *map){map[0]=0;return app_count?1:0;}
static void draw_apps(int,int,const int*,int){draws++;}
static void stop_art_loader(plat_thread **p){*p=NULL;}
static void plat_atomic_store(int *p,int v){*p=v;}
static void *art_loader(void*){return NULL;}
static plat_thread *plat_thread_start(void *(*)(void*),void*){return NULL;}
static void art_update(const int*,int,int,int){}
static void *applist_task(void*){return NULL;}
static void run_busy(const char*,const char*,void *(*)(void*),void *count){app_list_cache_store(&app_cache,host_key(),plat_now_ms());loads++;*(int*)count=library_size;app_count=library_size;for(int i=0;i<library_size;i++){apps[i].id=42+i;sprintf(apps[i].name,"Library game %d",42+i);}}
static const char *gs_error(){return "Error";}
int ui_location_enter(const char*){return 0;}
void ui_location_restore(int){}
void ui_present(){}
bool ui_focus_tick(uint32_t){return false;}
static uint32_t plat_now_ms(){return 0;}
void ui_draw_hosts(const ui_setting *rows,int n,int sel,int page,bool){
    assert(n==hosts.count+2 && sel>=0 && sel<n);assert(page==UI_HOME || page==UI_SETTINGS);
    assert(!strcmp(rows[hosts.count].label,"+ Add host manually"));host_rows_seen=n;
}
int ui_apps_columns(){return 6;}
void ui_art_size(int *w,int *h){*w=150;*h=200;}
void ui_drop_main_cache(){}
void ui_draw_dashboard(const ui_host*,const ui_menu_item*,int n,int sel,const char*,const ui_dashboard *d){
    assert(n==5+d->recent_count && sel>=0 && sel<n && d->recent_count<=4);
    assert(!strcmp(d->profile,"Custom"));
    assert(!strcmp(d->connection,"720p60  \xE2\x80\xA2  30 Mbps"));
    last_resume_id=d->resume_id;
    if(d->resume){bool valid=false;for(int i=0;i<app_count;i++)if(apps[i].id==d->resume_id && !strcmp(apps[i].name,d->resume->name))valid=true;assert(valid);}
    for(int i=0;i<d->recent_count;i++)assert(!d->resume || strcmp(d->recent[i].name,d->resume->name));
    draws++;
}
/* PRODUCTION */
static void reset(){
    app_cache.valid=false;
    wake_accept=true;wake_calls=0;
    library_size=1;last_play_id=first_play_id=last_resume_id=0;add_recent_on_play=false;stream_return_home=false;
    config_defaults(&cfg);strcpy(cfg.host,"PC");server.paired=true;server.current_game=0;
    server_ok=true;navigation_pending=-1;app_count=0;art_version=0;session_history.count=0;
    plays=details=pauses=library_calls=context_calls=loads=draws=settings_calls=0;details_play=false;host_panels=0;hosts.count=0;manual_accept=inject_host=false;manual_calls=0;
    next_discovery=99;next_socket_retry=99;discovery_round=3;
}
static void home(std::initializer_list<int> input){keys=input;try{main_menu();assert(false);}catch(Done&){}}
int main(){
    reset();keys={A_FLAG,B_FLAG};actual_app_menu();assert(plays==1 && details==0);
    reset();keys={X_FLAG,B_FLAG};actual_app_menu();assert(plays==0 && details==1);
    reset();details_play=true;keys={X_FLAG,B_FLAG};actual_app_menu();assert(plays==1 && details==1);
    reset();keys={Y_FLAG,B_FLAG};actual_app_menu();assert(loads==2 && plays==0);
    reset();keys={PLAY_FLAG,B_FLAG};actual_app_menu();assert(context_calls==1 && plays==0);
    reset();home({A_FLAG});assert(library_calls==1 && details==0);
    reset();server.current_game=42;home({A_FLAG});assert(plays==1 && details==0);
    reset();home({X_FLAG});assert(details==1 && !library_calls && !plays);
    reset();home({Y_FLAG,0,0});assert(pauses==1 && draws==2 && host_panels==1);
    reset();home({PLAY_FLAG});assert(context_calls==1 && !library_calls);
    reset();server.current_game=42;home({RB_FLAG});assert(library_calls==1 && plays==0);
    reset();session_history.count=1;strcpy(session_history.records[0].host,"PC");
    strcpy(session_history.records[0].app,"Game");session_history.records[0].app_id=42;
    home({DOWN_FLAG,RIGHT_FLAG,RIGHT_FLAG,A_FLAG});assert(plays==1 && details==0);
    reset();session_history.count=1;strcpy(session_history.records[0].host,"PC");
    strcpy(session_history.records[0].app,"Game");session_history.records[0].app_id=42;
    home({DOWN_FLAG,RIGHT_FLAG,RIGHT_FLAG,X_FLAG});assert(plays==0 && details==1);
    reset();session_history.count=1;strcpy(session_history.records[0].host,"PC");
    strcpy(session_history.records[0].app,"Game");session_history.records[0].app_id=42;
    home({DOWN_FLAG,RIGHT_FLAG,RIGHT_FLAG,A_FLAG});assert(plays==1 && details==0);
    reset();keys={Y_FLAG,0,B_FLAG};actual_hosts_menu(UI_HOME);assert(!pauses && host_rows_seen==2);
    reset();inject_host=true;keys={0,B_FLAG};actual_hosts_menu(UI_HOME);assert(host_rows_seen==3 && !pauses);
    reset();inject_host=true;keys={A_FLAG};actual_hosts_menu(UI_HOME);assert(!strcmp(cfg.host,"192.168.1.9") && pauses==1);
    reset();manual_accept=true;keys={A_FLAG};actual_hosts_menu(UI_SETTINGS);assert(manual_calls==1 && !strcmp(cfg.host,"192.168.1.10"));
    reset();inject_host=true;keys={X_FLAG,B_FLAG};actual_hosts_menu(UI_HOME);assert(details==1 && plays==0);
    reset();inject_host=true;tab_from_host_settings=true;keys={X_FLAG};actual_hosts_menu(UI_SETTINGS);
    assert(keys.empty() && navigation_pending==UI_STREAM);tab_from_host_settings=false;
    // Active session wins over a different most-recent game; all display/action
    // identity comes from the selected host library, not stale TSV names.
    reset();library_size=6;server.current_game=43;
    session_history.count=1;strcpy(session_history.records[0].host,"PC");strcpy(session_history.records[0].app,"GTA stale");session_history.records[0].app_id=42;
    home({DOWN_FLAG,RIGHT_FLAG,RIGHT_FLAG,A_FLAG});assert(last_play_id==43 && last_resume_id==43);
    reset();library_size=6;add_recent_on_play=true;
    session_history.count=1;strcpy(session_history.records[0].host,"PC");strcpy(session_history.records[0].app,"Old title");session_history.records[0].app_id=42;
    home({DOWN_FLAG,RIGHT_FLAG,RIGHT_FLAG,A_FLAG,A_FLAG});assert(plays==2 && first_play_id==42 && last_play_id==43 && last_resume_id==43);
    reset();session_history.count=1;strcpy(session_history.records[0].host,"OTHER PC");session_history.records[0].app_id=42;home({0});assert(!last_resume_id);
    reset();server_ok=false;hosts.count=1;strcpy(hosts.hosts[0].ip,cfg.host);hosts.hosts[0].auto_wake=true;wake_accept=false;
    home_connect(false);assert(wake_calls==1 && !plays && !library_calls && !server_ok);
    wake_accept=true;home_connect(false);assert(wake_calls==2 && server_ok && library_calls==1);
    puts("PASS: production Home/Library A direct play, X details, details Play, Y refresh/search without pause, Start context, Library tab with running game and saved recent launch.");
}
"""
code='\n'.join([
    function(ui,'int ui_dashboard_move_tiles('),
    function(app,'static bool tab_key('),
    function(app,'static void set_item('),
    function(app,'static void home_connect('),
    function(app,'static void app_menu(void)').replace('static void app_menu(void)','static void actual_app_menu(void)'),
    function(app,'static void home_sync_apps(void)'),
    function(app,'static void main_menu(void)'),
    function(app,'static void hosts_menu(int page)\n{').replace('static void hosts_menu(int page)','static void actual_hosts_menu(int page)')])
Path(sys.argv[1]).write_text(fixture.replace('/* PRODUCTION */',code))
