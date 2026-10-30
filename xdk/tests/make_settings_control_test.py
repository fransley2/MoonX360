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
void ui_set_settings_context(int page,int category){context_page=page;context_category=category;}

void ui_draw_settings_page(const char *title,const ui_setting *rows,int n,int sel){
    assert(n>0 && sel>=0 && sel<n && strcmp(title,"Video"));pages.insert(title);
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
char *storage_read(const char*,size_t*){return NULL;}
static void message(int,const char*,const char*){messages++;}
static bool plat_has_renderer_choice(){return false;}
static bool plat_benchmark_available(){return true;}
static bool plat_has_diagnostics(){return true;}
static void plat_diagnostics_configure(bool){}
static void plat_run_benchmark(){}
static int wait_button();
static void user_profiles_menu(){assert(context_page==UI_STREAM);while(!(wait_button()&B_FLAG)){};}
static void pointer_settings_menu(){}
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
    begin();keys={PLAY_FLAG,B_FLAG,B_FLAG};finish();assert(draws[1].title=="PCs and connection");
    // Six categories open with A; B restores the exact opener, including rail selection.
    for(int c=0;c<6;c++){
        begin();open_category(c);back(2);finish();
        assert(draws.back().title=="Settings" && draws.back().selection==c);
        assert(draws[draws.size()-2].category==c);
    }
    // The complete tree has controller paths. External leaves are exercised separately below.
    for(int node=MENU_PCS;node<MENU_COUNT;node++){
        if(node==MENU_HOSTS || node==MENU_APPEARANCE || node==MENU_STREAM_UI || node==MENU_RESTART_APP || node==MENU_RESET_APP)continue;
        std::deque<int> path;int p=node;
        while(settings_pages[p].parent>=0){path.push_front(p);p=settings_pages[p].parent;}
        bool stream=p==MENU_STREAM;begin(stream);
        for(int target:path){
            int index=0;for(int i=0;i<SETTINGS_ENTRY_COUNT;i++)if(settings_entries[i].page==settings_pages[target].parent){
                if(settings_entries[i].child==target)break;index++;
            }
            down(index);keys.push_back(A_FLAG);
        }
        back((int)path.size()+1);finish();assert(pages.count(settings_pages[node].title));
    }
    assert(!options.count("Renderer"));
    // Stream profile remains the original selector and changes only its original fields.
    cfg=original;cfg.fast_decode=false;cfg.vsync=false;cfg.image_quality=2;cfg.slice_count=16;
    begin(true);keys={A_FLAG,B_FLAG};finish();assert(cfg.fps==60 && cfg.bitrate_kbps==18000);
    assert(!cfg.fast_decode && !cfg.vsync && cfg.image_quality==2 && cfg.slice_count==16);
    begin(true);keys={DOWN_FLAG,A_FLAG,RIGHT_FLAG,B_FLAG,B_FLAG};finish();
    assert(cfg.stream_profile==0 && cfg.width==1920 && cfg.bitrate_kbps==18000);
    assert(cfg.fast_decode && cfg.image_quality==1); // preset speed settings survive a manual resolution change
    assert(draws.back().selection==1 && draws.back().page==UI_STREAM);
    // All six stream adjustments and Audio keep STREAM selected and their ancestry.
    for(const auto &draw:draws)assert(draw.page==UI_STREAM && draw.category==-1);
    // Actual controller path: bitrate-only edit keeps the effective smooth preset settings.
    cfg=original;cfg.fast_decode=false;cfg.image_quality=2;config_apply_preset(&cfg,CONFIG_PROFILE_SMOOTH);
    begin(true);keys={DOWN_FLAG,A_FLAG,DOWN_FLAG,DOWN_FLAG,RIGHT_FLAG,B_FLAG,B_FLAG};finish();
    assert(cfg.bitrate_kbps==19000 && cfg.fast_decode && cfg.image_quality==1 && cfg.stream_profile==0);
    // The displayed value matches playback, and one A really turns forced Fast Decode off.
    cfg=original;cfg.fast_decode=false;cfg.image_quality=2;config_apply_preset(&cfg,CONFIG_PROFILE_SMOOTH);
    begin();open_category(SETTINGS_DIAGNOSTICS);keys.push_back(A_FLAG);keys.push_back(A_FLAG);back(3);finish();
    bool saw_on=false,saw_off=false;for(const auto &draw:draws)if(draw.page==UI_SETTINGS && draw.selection==0 && draw.title=="Decoding"){
        saw_on|=draw.value=="On";saw_off|=draw.value=="Off";
    }
    assert(saw_on && saw_off && !cfg.fast_decode && cfg.stream_profile==0 && cfg.image_quality==1);
    begin(true);keys={DOWN_FLAG,DOWN_FLAG,A_FLAG,A_FLAG,DOWN_FLAG,A_FLAG,B_FLAG,B_FLAG};finish();
    assert(cfg.audio_mode==1 && cfg.local_audio);
    // LB/RB from stream root, adjustments, audio and nested profiles unwind without extra B.
    for(int row=0;row<4;row++)for(int key:{LB_FLAG,RB_FLAG}){
        begin(true);down(row);if(row)keys.push_back(A_FLAG);keys.push_back(key);finish();
        assert(navigation_pending==(key==LB_FLAG?UI_LIBRARY:UI_SETTINGS));
    }
    begin();open_category(SETTINGS_APPEARANCE);keys.push_back(A_FLAG);
    keys.push_back(RIGHT_FLAG);keys.push_back(DOWN_FLAG);keys.push_back(RIGHT_FLAG);back(3);finish();
    assert(cfg.ui_background==1 && cfg.ui_accent==1 && pages.count("Appearance"));
    // Streaming interface is discoverable with A, preserves the opener and changes the existing fields.
    begin();open_category(SETTINGS_APPEARANCE);down(1);keys.push_back(A_FLAG);
    keys.push_back(RIGHT_FLAG);keys.push_back(DOWN_FLAG);keys.push_back(RIGHT_FLAG);back(3);finish();
    assert(cfg.mouse_hud==1 && cfg.stream_menu_style==1);
    assert(draws[draws.size()-2].selection==1);
    // Appearance X and the historical overlay X use the same implementation.
    begin();open_category(SETTINGS_APPEARANCE);keys.push_back(X_FLAG);back(3);finish();
    begin();open_category(SETTINGS_HISTORY);keys.push_back(X_FLAG);keys.push_back(RB_FLAG);finish();
    assert(navigation_pending==UI_HOME && draws.back().category==SETTINGS_APPEARANCE);
    // App resolution and read-only detected output stay separate.
    begin();open_category(SETTINGS_APPEARANCE);down(2);keys.push_back(A_FLAG);keys.push_back(RIGHT_FLAG);
    keys.push_back(DOWN_FLAG);keys.push_back(A_FLAG);back(3);int old_width=cfg.width;finish();
    assert(cfg.display_mode==1 && cfg.width==old_width);
    for(int i=0;i<3;i++){
        begin();open_category(SETTINGS_APPEARANCE);down(3);keys.push_back(RIGHT_FLAG);back(2);finish();
        assert(cfg.language==(i+1)%3 && selected_language==cfg.language);
    }
    begin();open_category(SETTINGS_PCS);keys.push_back(A_FLAG);back(2);finish();assert(hosts_calls==1);
    bool mdns=cfg.mdns_discovery;begin();open_category(SETTINGS_PCS);down(1);keys.push_back(A_FLAG);back(2);finish();assert(cfg.mdns_discovery!=mdns);
    // Confirmation is the existing handler: Cancel/B do nothing, failed save blocks reset/restart.
    keys={A_FLAG};maintenance_menu(0);assert(keys.empty() && !restarts);
    cfg.bitrate_kbps=70000;strcpy(cfg.host,"PC");strcpy(cfg.host_uuid,"uuid");
    keys={DOWN_FLAG,A_FLAG};maintenance_menu(0);assert(cfg.bitrate_kbps==30000 && !strcmp(cfg.host,"PC") && !strcmp(cfg.host_uuid,"uuid"));
    cfg.bitrate_kbps=50000;allow_write=false;keys={DOWN_FLAG,A_FLAG};maintenance_menu(0);assert(cfg.bitrate_kbps==50000);
    keys={DOWN_FLAG,A_FLAG};maintenance_menu(1);assert(!restarts);allow_write=true;
    keys={DOWN_FLAG,A_FLAG};maintenance_menu(1);assert(restarts==1);
    begin();open_category(SETTINGS_SYSTEM);keys.push_back(A_FLAG);keys.push_back(RB_FLAG);finish();assert(restarts==1 && navigation_pending==UI_HOME);
    // Technical info is read-only, and all three logging fields stay independently scoped.
    original=cfg;begin();open_category(SETTINGS_DIAGNOSTICS);down(2);keys.push_back(A_FLAG);down(4);keys.push_back(A_FLAG);back(3);finish();assert(!memcmp(&cfg,&original,sizeof(cfg)));
    begin();open_category(SETTINGS_DIAGNOSTICS);down(2);keys.push_back(A_FLAG);down(1);keys.push_back(A_FLAG);
    keys.push_back(A_FLAG);down(2);keys.push_back(A_FLAG);back(4);finish();assert(cfg.diagnostic_log!=original.diagnostic_log && cfg.session_report!=original.session_report);
    puts("PASS: real menu tree A/B, six categories, focus, ancestry, Stream tab in descendants, nested LB/RB, discoverable appearance/HUD, scoped fields and safe maintenance.");
}

'''
code=function('static bool tab_key(')+'\n'+function('static int wait_button(void)\n{')+'\n'+function('static void overlay_options_menu(void)\n{')+'\n'+function('static void maintenance_menu(int selected)\n{')+'\n'+function('static void appearance_menu(void)\n{')+'\n'+function('static void settings_menu(void)\n{')
Path(sys.argv[1]).write_text(fixture.replace('/* CODE */',code))
