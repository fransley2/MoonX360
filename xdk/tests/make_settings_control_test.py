"""Navigate the real settings state machine solely with controller events."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
s=(root/'src/app.c').read_text()
def function(sig):
    a=s.index(sig);b=s.index('{',a)+1;depth=1
    while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
    return s[a:b]
fixture=r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <deque>
#include <string>
#include <set>
#include <Limelight.h>
#include "../src/config.h"
#include "../src/video.h"
#include "../src/settings_layout.h"
#include "../src/ui/ui.h"
static config cfg;
static int settings_initial_group=-1,navigation_pending=-1,settings_navigation_page=-1;
static struct {int count;} session_history;
static const int resolutions[][2]={{854,480},{1280,720},{1920,1080}};
static std::deque<int> keys;
static std::set<std::string> pages,options;
static bool allow_write=true;static unsigned messages,restarts;
static unsigned saves,hosts_calls;static int selected_language=-1;
static int input_menu_pressed(){assert(!keys.empty());int key=keys.front();keys.pop_front();return key;}
static void plat_sleep_ms(unsigned){}
static int context_page=-1,context_category=-1,depth;
static std::string ancestry[8];
struct Draw {std::string title,label,path;int selection,page,category;std::string value;};
static std::deque<Draw> draws;
static bool profile_editor_active,profile_editor_save;
static bool server_ok=true;
static bool host_details(){hosts_calls++;return false;}
static bool recover_host_ui(){return true;}
static void refresh_server(){hosts_calls++;}
void ui_set_settings_context(int page,int category){context_page=page;context_category=category;}

void ui_draw_settings_page(const char *title,const ui_setting *rows,int n,int sel){
    assert(n>0 && sel>=0 && sel<n);pages.insert(title);
    for(int i=0;i<n;i++){assert(rows[i].label && rows[i].value);options.insert(rows[i].label);}
    std::string path;for(int i=0;i<depth;i++){if(i)path+=" > ";path+=ancestry[i];}
    draws.push_back({title,rows[sel].label,path,sel,context_page,context_category,rows[sel].value});
}
int ui_location_enter(const char *s){int token=depth;if(s && (depth==0 || ancestry[depth-1]!=s)){assert(depth<8);ancestry[depth++]=s;}return token;}
void ui_location_restore(int token){assert(token>=0 && token<=depth);depth=token;}
void ui_draw_settings_options(const char *title,const ui_setting *rows,const int *kinds,int n,int sel){
    for(int i=0;i<n;i++)assert(kinds[i]>=UI_OPTION_OPEN && kinds[i]<=UI_OPTION_INFO);
    ui_draw_settings_page(title,rows,n,sel);
}
void ui_present(){}
void ui_set_exit_shortcut(int){}
bool storage_write(const char*,const void*,size_t){saves++;return allow_write;}
char *storage_read(const char*,size_t*){errno=ENOENT;return NULL;}
static bool profile_links_pending(){return false;}
static void message(int,const char*,const char*){messages++;}
static bool plat_has_renderer_choice(){return false;}
static bool plat_benchmark_available(){return true;}
static bool plat_has_diagnostics(){return true;}
static void plat_diagnostics_configure(bool){}
static void plat_run_benchmark(){}
static int wait_button();
static void save_current_profile(){}
static const char *selected_profile_name(){return config_preset_name(config_video_preset(&cfg));}
static void stream_profile_menu(){assert(context_page==UI_STREAM || context_page==UI_SETTINGS);while(!(wait_button()&B_FLAG)){};}
static void pointer_settings_menu(){}
static void launch_defaults_menu(){while(!(wait_button()&B_FLAG)){};}
static void ui_memory_menu(){}
static void network_diagnostics_menu(){}
static void log_viewer(){}
static void history_menu(){}
static void advisor_menu(){}
static void host_options_menu(){hosts_calls++;}
static void hosts_menu(int){hosts_calls++;}
static void calibrate_sticks(){}
static void power_menu(bool){}
static bool plat_power_available(){return true;}
static void plat_display_info(char *out,size_t n){snprintf(out,n,"1920x1080p 59.94 Hz");}
void ui_set_language(int language){selected_language=language;}
void ui_set_appearance(int,int){}
void ui_set_motion(bool){}
void ui_set_stream_hud(int,int){}
static bool plat_restart_available(){return true;}
static bool plat_restart_app(){restarts++;return false;}
const char *ui_translate(const char *s){return s;}
/* CODE */

static void begin(bool stream=false){
    assert(keys.empty());draws.clear();depth=0;navigation_pending=-1;
    settings_initial_group=stream?SETTINGS_STREAM:-1;
}
static void finish(){settings_menu();assert(keys.empty() && depth==0 && context_page==-1);}
static void down(int n){while(n-->0)keys.push_back(DOWN_FLAG);}
static void open_category(int c){down(c);keys.push_back(A_FLAG);}
static void back(int n){while(n-->0)keys.push_back(B_FLAG);}
int main(){
    config_defaults(&cfg);config_apply_preset(&cfg,CONFIG_PROFILE_STANDARD);
    config original=cfg;begin();keys={B_FLAG};finish();assert(!memcmp(&cfg,&original,sizeof(cfg)));
    assert(draws.front().title=="Settings");
    const int destinations[]={MENU_PCS,MENU_CONTROLS,MENU_APPEARANCE_SCREEN,MENU_ADVANCED,MENU_SYSTEM};
    for(int c=0;c<5;c++){begin();down(c);keys.push_back(A_FLAG);back(2);finish();assert(draws.front().title=="Settings");bool found=false;for(auto d:draws)if(d.title==settings_pages[destinations[c]].title)found=true;assert(found);}
    // Distinct four-row Stream root, direct profile selection, Video and Audio.
    cfg=original;begin(true);keys={DOWN_FLAG,A_FLAG,DOWN_FLAG,RIGHT_FLAG,B_FLAG,B_FLAG};finish();
    assert(cfg.fps==config_cycle_fps(original.fps,1) && cfg.stream_profile==0);
    begin(true);keys={DOWN_FLAG,A_FLAG,DOWN_FLAG,DOWN_FLAG,RIGHT_FLAG,B_FLAG,B_FLAG};finish();assert(cfg.bitrate_kbps==original.bitrate_kbps+1000);
    cfg=original;begin(true);keys={RIGHT_FLAG,B_FLAG};finish();assert(cfg.fps==24 && cfg.width==1920);
    for(int c=0;c<5;c++)for(int key:{LB_FLAG,RB_FLAG}){begin();down(c);keys.push_back(A_FLAG);keys.push_back(key);finish();assert(navigation_pending==(key==LB_FLAG?UI_STREAM:UI_HOME));}
    cfg=original;begin();down(2);keys.push_back(A_FLAG);keys.push_back(A_FLAG);keys.push_back(RIGHT_FLAG);down(1);keys.push_back(RIGHT_FLAG);back(3);finish();assert(cfg.ui_background==1 && cfg.ui_accent==1);
    unsigned prior=hosts_calls;begin();keys.push_back(A_FLAG);keys.push_back(A_FLAG);down(1);keys.push_back(A_FLAG);down(1);keys.push_back(A_FLAG);back(2);finish();assert(hosts_calls==prior+3);
    bool mdns=cfg.mdns_discovery;begin();keys.push_back(A_FLAG);down(3);keys.push_back(RIGHT_FLAG);back(2);finish();assert(cfg.mdns_discovery!=mdns);
    cfg=original;config_apply_preset(&cfg,CONFIG_PROFILE_SMOOTH);
    begin(true);keys.push_back(DOWN_FLAG);keys.push_back(A_FLAG);down(7);keys.push_back(A_FLAG);keys.push_back(A_FLAG);back(3);finish();assert(!cfg.fast_decode && cfg.stream_profile==0 && cfg.image_quality==1);
    // The reused editor omits selection/coverage, saves only on explicit Save, and never writes global config.
    unsigned prior_saves=saves;profile_editor_active=true;profile_editor_save=false;
    cfg=original;begin(true);keys={A_FLAG,DOWN_FLAG,DOWN_FLAG,RIGHT_FLAG,B_FLAG,B_FLAG};finish();assert(!profile_editor_save && saves==prior_saves && cfg.bitrate_kbps==original.bitrate_kbps+1000);
    begin(true);keys={DOWN_FLAG,DOWN_FLAG,A_FLAG};finish();assert(profile_editor_save && saves==prior_saves);profile_editor_active=profile_editor_save=false;
    // Destructive preferences require the existing explicit confirmation; failed writes preserve data.
    keys={A_FLAG};maintenance_menu(0);assert(keys.empty() && !restarts);
    cfg.bitrate_kbps=70000;strcpy(cfg.host,"PC");strcpy(cfg.host_uuid,"uuid");
    keys={DOWN_FLAG,A_FLAG};maintenance_menu(0);assert(cfg.bitrate_kbps==12000 && !strcmp(cfg.host,"PC") && !strcmp(cfg.host_uuid,"uuid"));
    cfg.bitrate_kbps=50000;allow_write=false;keys={DOWN_FLAG,A_FLAG};maintenance_menu(0);assert(cfg.bitrate_kbps==50000);
    keys={DOWN_FLAG,A_FLAG};maintenance_menu(1);assert(!restarts);allow_write=true;
    keys={DOWN_FLAG,A_FLAG};maintenance_menu(1);assert(restarts==1);
    puts("PASS: actual full-width Settings / Stream separation, A/B/Left/Right, inline fields, Stream tab, nested tabs, host details/test, appearance, decoder and safe maintenance.");
}

'''
code=function('static void cycle_stream_profile(')+'\n'+function('static bool tab_key(')+'\n'+function('static int wait_button(void)\n{')+'\n'+function('static void overlay_options_menu(void)\n{')+'\n'+function('static void maintenance_menu(int selected)\n{')+'\n'+function('static void appearance_menu(void)\n{')+'\n'+function('static void settings_menu(void)\n{')
Path(sys.argv[1]).write_text(fixture.replace('/* CODE */',code))
