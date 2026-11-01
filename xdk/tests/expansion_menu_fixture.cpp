#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <deque>
#include <map>
#include <string>
#include <Limelight.h>
#define MAX_APPS 64
#include "../../../src/config.h"
#include "../../../src/product_features.h"
#include "../../../src/session_history.h"
#include "../../../src/library_state.h"
#include "../../../src/host_cache.h"
#include "../../../src/wake.h"
#include "../../../src/input.h"
#include "../../../src/ui/ui.h"
static struct config cfg;
static struct host_cache hosts;
static struct game_bindings game_bindings;
static struct session_history session_history;
static struct library_favorites favorites;
static char library_query[64];
static struct {int id;char name[64];} apps[64];static int app_count;
static struct {bool paired;int current_game;} server;
static bool server_ok,host_cache_save_failed,accept_edit=true;static char server_error[128];
static unsigned next_discovery,adaptive_reconnects;
static std::deque<int> keys;
static std::map<std::string,std::string> files;
static int messages,writes,stream_calls,used_width,used_audio,wakes;
char *storage_read(const char *name,size_t *len){if(!files.count(name)){errno=ENOENT;return NULL;}const std::string &s=files[name];char *p=(char*)malloc(s.size()+1);memcpy(p,s.c_str(),s.size()+1);if(len)*len=s.size();return p;}
bool storage_write(const char *name,const void *data,size_t n){writes++;files[name]=std::string((const char*)data,n);return true;}
static int wait_button(){assert(!keys.empty());int b=keys.front();keys.pop_front();return b;}
static bool tab_key(int,int){return false;}
static void message(int,const char*,const char*){messages++;}
void ui_draw_settings_page(const char*,const ui_setting *rows,int n,int sel){assert(n>0 && sel>=0 && sel<n);for(int i=0;i<n;i++)assert(rows[i].label && rows[i].value);}
void ui_draw_settings_options(const char *title,const ui_setting *rows,const int *kinds,int n,int sel){for(int i=0;i<n;i++)assert(kinds[i]);ui_draw_settings_page(title,rows,n,sel);}
void ui_present(){}
int ui_location_enter(const char*){return 0;}void ui_location_restore(int){}
static bool edit_profile_name(char *text){if(accept_edit)strcpy(text,"My PC");return accept_edit;}
static void clear_apps(){app_count=0;}
static bool wake_host(){wakes++;return true;}
static void edit_host_mac(){}
static const char *gs_error(){return "Permission denied";}
static void host_options_menu(){}
static bool stream_once(int,const char*){stream_calls++;used_width=cfg.width;used_audio=cfg.audio_mode;return false;}
static void refresh_server(){}
static void remember_played_art(int){}
/* ACTUAL_FUNCTIONS */
int main(){
    config_defaults(&cfg);cfg.language=2;strcpy(cfg.host,"10.1.2.3");hosts.count=1;strcpy(hosts.hosts[0].uuid,"ABC");strcpy(hosts.hosts[0].name,"Server");strcpy(hosts.hosts[0].ip,cfg.host);hosts.hosts[0].port=47989;
    assert(!strcmp(host_key(),"uuid:abc") && !strcmp(host_label("uuid:abc"),"Server"));
    hosts.count=2;hosts.hosts[1]=hosts.hosts[0];strcpy(hosts.hosts[1].uuid,"OTHER");strcpy(hosts.hosts[1].mac,"02:AA:BB:CC:DD:EE");
    strcpy(cfg.host_uuid,"OTHER");assert(selected_host_index()==1 && !strcmp(host_key(),"uuid:other"));
    assert(config_save(&cfg));config rebooted;config_load(&rebooted);assert(!strcmp(rebooted.host_uuid,"OTHER"));
    strcpy(cfg.host_uuid,"ABC");assert(selected_host_index()==0 && !strcmp(host_key(),"uuid:abc"));hosts.count=1;
    session_history.count=1;strcpy(session_history.records[0].host,cfg.host);session_history.records[0].app_id=42;
    assert(library_favorite_toggle(&favorites,cfg.host,42));assert(library_favorite_toggle(&favorites,"uuid:abc",42));
    assert(binding_set(&game_bindings,cfg.host,42,"factory:1"));assert(binding_set(&game_bindings,"uuid:abc",42,"factory:2"));
    migrate_host_records(cfg.host,host_key());
    assert(!strcmp(session_history.records[0].host,"uuid:abc") && favorites.count==1 && game_bindings.count==1);
    assert(!strcmp(game_bindings.items[0].profile,"factory:2"));int before=writes;migrate_host_records(cfg.host,host_key());assert(writes==before);
    puts("PASS: actual stable-key migration, friendly host labels, duplicate favorites/bindings and no repeat writes");

    keys={A_FLAG};game_profile_menu(42);assert(!strcmp(game_bindings.items[0].profile,"factory:2"));assert(keys.empty());
    config current=cfg;config profile=cfg;profile.width=1920;profile.height=1080;profile.fps=60;profile.audio_mode=1;profile.stream_profile=0;
    user_profiles saved={};saved.slot=-1;assert(config_profiles_create(&saved,&profile,"TV"));
    keys={DOWN_FLAG,DOWN_FLAG,DOWN_FLAG,DOWN_FLAG,A_FLAG};game_profile_menu(42);assert(keys.empty());
    assert(!strcmp(game_bindings.items[0].profile,"user:TV"));stream(42,"Game");assert(stream_calls==1 && used_width==1920 && used_audio==1 && !memcmp(&cfg,&current,sizeof(cfg)));
    files.erase("profiles-a.tsv");files.erase("profiles-b.tsv");int notices=messages;stream(42,"Game");assert(used_width==current.width && messages==notices+1 && !memcmp(&cfg,&current,sizeof(cfg)));
    puts("PASS: actual controller per-game factory/saved selection, next-launch apply, missing profile fallback and global restoration");

    keys={A_FLAG,B_FLAG};manage_host();assert(!strcmp(hosts.hosts[0].alias,"My PC") && keys.empty());
    keys={DOWN_FLAG,A_FLAG,B_FLAG};manage_host();assert(hosts.hosts[0].is_default);
    keys={DOWN_FLAG,DOWN_FLAG,A_FLAG,B_FLAG};manage_host();assert(hosts.hosts[0].auto_wake);
    keys={LEFT_FLAG,DOWN_FLAG,A_FLAG,DOWN_FLAG,RIGHT_FLAG,B_FLAG};pointer_settings_menu();assert(cfg.mouse_speed==75 && cfg.mouse_acceleration==1 && cfg.mouse_scroll_ms==150);
    int filter=2;keys={DOWN_FLAG,DOWN_FLAG,A_FLAG,B_FLAG};library_context(&filter);assert(!strcmp(library_query,"My PC"));
    filter=1;keys={DOWN_FLAG,DOWN_FLAG,DOWN_FLAG,A_FLAG,B_FLAG};library_context(&filter);assert(!library_query[0] && filter==1);
    app_count=3;apps[0].id=42;strcpy(apps[0].name,"GTA San Andreas");apps[1].id=43;strcpy(apps[1].name,"Desktop");apps[2].id=44;strcpy(apps[2].name,"Halo");
    strcpy(library_query,"gta");int map[64];assert(library_map(2,map)==1 && map[0]==0);library_query[0]=0;assert(library_map(2,map)==3 && library_map(1,map)==1);
    files["client.pem"]="preserve";keys={UP_FLAG,UP_FLAG,A_FLAG,A_FLAG,B_FLAG};manage_host();assert(hosts.count==1);
    keys={UP_FLAG,UP_FLAG,A_FLAG,DOWN_FLAG,A_FLAG};manage_host();assert(!hosts.count && !cfg.host[0] && files["client.pem"]=="preserve" && files["hosts.tsv"].empty());
    puts("PASS: actual nickname/default/automatic wake and pointer menus, search/filter/clear, removal defaults Cancel and preserves certificates");
}

void ui_set_stream_hud(int,int){}
