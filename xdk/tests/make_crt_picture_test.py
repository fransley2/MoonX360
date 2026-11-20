"""Verify real CRT Picture submenu navigation, hiding rules, Screen Adjustment, and Output Status."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
s = (root / 'src/app.c').read_text(encoding='utf-8')

def function(sig):
    a = s.index(sig)
    b = s.index('{', a) + 1
    depth = 1
    while depth:
        depth += (s[b] == '{') - (s[b] == '}')
        b += 1
    return s[a:b]

fixture = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <deque>
#include <string>
#include <vector>
#include <Limelight.h>
#include "../src/config.h"
#include "../src/crt_output.h"
#include "../src/video.h"
#include "../src/settings_layout.h"
#include "../src/ui/ui.h"

static config cfg;
static int settings_initial_group=-1,navigation_pending=-1,settings_navigation_page=-1;
static struct {int count;} session_history;
static const int resolutions[][2]={{640,480},{854,480},{960,540},{1280,720},{1920,1080}};
static std::deque<int> keys;
static bool profile_editor_active,profile_editor_save,server_ok=true;
static unsigned messages,saves;
static int context_page=-1,context_category=-1,depth;
static int crt_status_val=1,crt_plugin_ver=0x00021700u,crt_active=0;
struct Draw {std::string title; std::vector<std::string> labels, values; int sel;};
static std::vector<Draw> draws;

static int input_menu_pressed(){assert(!keys.empty());int k=keys.front();keys.pop_front();return k;}
static void plat_sleep_ms(unsigned){}
static bool host_details(){return false;}
static bool recover_host_ui(){return true;}
static void refresh_server(){}
void ui_set_settings_context(int page,int category){context_page=page;context_category=category;}
void ui_draw_settings_page(const char *title,const ui_setting *rows,int n,int sel){
    Draw d; d.title=title; d.sel=sel;
    for(int i=0;i<n;i++){d.labels.push_back(rows[i].label); d.values.push_back(rows[i].value);}
    draws.push_back(d);
}
void ui_draw_settings_options(const char *title,const ui_setting *rows,const int *kinds,int n,int sel){
    (void)kinds; ui_draw_settings_page(title,rows,n,sel);
}
int ui_location_enter(const char*){return depth++;}
void ui_location_restore(int t){depth=t;}
void ui_present(){}
void ui_set_exit_shortcut(int){}
bool storage_write(const char*,const void*,size_t){saves++;return true;}
char *storage_read(const char*,size_t*){errno=ENOENT;return NULL;}
static bool profile_links_pending(){return false;}
static void message(int,const char*,const char*){messages++;}
static bool plat_has_renderer_choice(){return false;}
static bool plat_benchmark_available(){return true;}
static bool plat_has_diagnostics(){return true;}
static void plat_diagnostics_configure(bool){}
static void plat_run_benchmark(){}
static int wait_button();
void ui_draw_confirm(const char*,const char*,const char*){}
static void save_current_profile(){}
static const char *selected_profile_name(){return config_preset_name(config_video_preset(&cfg));}
static void stream_profile_menu(){}
static void pointer_settings_menu(){}
static void launch_defaults_menu(){}
static void ui_memory_menu(){}
static void network_diagnostics_menu(){}
static void latency_diagnostics_menu(){}
static void log_viewer(){}
static void history_menu(){}
static void advisor_menu(){}
static void host_options_menu(){}
static void hosts_menu(int){}
static void calibrate_sticks(){}
static void power_menu(bool){}
static bool plat_power_available(){return true;}
static void plat_display_info(char *out,size_t n){snprintf(out,n,"640x480i 59.94 Hz");}
static int plat_crt_output_status(){return crt_status_val;}
static void plat_crt_output_query(struct crt_output_status *st){
    memset(st,0,sizeof(*st));
    st->display_width=crt_status_val==1?640:1920;
    st->display_height=crt_status_val==1?480:1080;
    st->interlaced=crt_status_val==1?1:0;
    st->hidef=crt_status_val==1?0:1;
    st->refresh_hz_x100=crt_status_val==1?5994:6000;
    st->plugin_ver=(unsigned)crt_plugin_ver;
    st->plugin_mode=crt_active;
    st->backbuffer_240p=crt_active;
    crt_output_evaluate(st,1);
}
void ui_set_language(int){}
static void choose_wallpaper(){}
void ui_set_appearance(int,int){}
void ui_set_motion(bool){}
void ui_set_stream_hud(int,int){}
static bool plat_restart_available(){return true;}
static bool plat_restart_app(){return false;}
const char *ui_translate(const char *s){return s;}

/* CODE */

static int crt_picture_row_in_video(){
    int pos=0;
    for(int i=0;i<SETTINGS_ENTRY_COUNT;i++)if(settings_entries[i].page==MENU_STREAM_ADJUST){
        if(settings_entries[i].setting>=0 && (settings_item_hidden(settings_entries[i].setting) || (settings_entries[i].setting==SET_RENDERER && !plat_has_renderer_choice())))continue;
        if(settings_entries[i].child>=0 && settings_child_hidden(settings_entries[i].child))continue;
        if(settings_entries[i].child==MENU_CRT_PICTURE)return pos;
        pos++;
    }
    return -1;
}

int main(){
    // 1. Non-CRT preset hides CRT Picture from Stream -> Video.
    config_defaults(&cfg); config_select_factory(&cfg, CONFIG_PROFILE_SMOOTH);
    assert(crt_picture_row_in_video()==-1);
    draws.clear(); settings_initial_group=SETTINGS_STREAM;
    keys={DOWN_FLAG,A_FLAG,B_FLAG,B_FLAG};
    settings_menu();
    assert(keys.empty());
    bool saw_crt=false;
    for(auto &d:draws) for(auto &l:d.labels) if(l=="CRT Picture") saw_crt=true;
    assert(!saw_crt);

    // 2. CRT preset shows CRT Picture; Scaling Filter, Clarity (480i only), Aspect Ratio, Screen Adjustment, Output Status, Reset work end-to-end.
    config_select_factory(&cfg, CONFIG_PROFILE_CRT_DISPLAY);
    int crt_row=crt_picture_row_in_video(); assert(crt_row>=0);
    draws.clear(); settings_initial_group=SETTINGS_STREAM;
    keys={DOWN_FLAG, A_FLAG}; // Enter Stream -> Video
    for(int i=0;i<crt_row;i++) keys.push_back(DOWN_FLAG);
    keys.push_back(A_FLAG);      // Enter CRT Picture
    keys.push_back(LEFT_FLAG);   // Scaling Filter Auto -> Sharp-Bilinear
    keys.push_back(DOWN_FLAG);   // -> CRT Clarity
    keys.push_back(RIGHT_FLAG);  // Toggle On (480i = 1)
    keys.push_back(DOWN_FLAG);   // -> Aspect Ratio
    keys.push_back(RIGHT_FLAG);  // 4:3 -> Original
    keys.push_back(DOWN_FLAG);   // -> Screen Adjustment
    keys.push_back(A_FLAG);      // Open Screen Adjustment
    keys.push_back(RIGHT_FLAG);  // pos_x = +1
    keys.push_back(DOWN_FLAG);   // -> Vertical position
    keys.push_back(LEFT_FLAG);   // pos_y = -1
    keys.push_back(B_FLAG);      // Exit Screen Adjustment
    keys.push_back(DOWN_FLAG);   // -> Output Status
    keys.push_back(A_FLAG);      // Open Output Status
    keys.push_back(B_FLAG);      // Exit Output Status
    keys.push_back(B_FLAG);      // Exit CRT Picture
    keys.push_back(B_FLAG);      // Exit Video
    keys.push_back(B_FLAG);      // Exit Stream
    settings_menu();
    assert(keys.empty());
    assert(cfg.crt_scaling==CRT_SCALE_SHARP_BILINEAR && cfg.crt_clarity && cfg.crt_aspect==CRT_ASPECT_ORIGINAL);
    assert(cfg.crt_pos_x==1 && cfg.crt_pos_y==-1 && config_crt_profile(&cfg));

    bool saw_output_status=false;
    for(auto &d:draws) if(d.title=="Output Status" && d.values.size()==5 && d.values[2]=="Ready") saw_output_status=true;
    assert(saw_output_status);

    // 3. Clarity refused when not on 480i, and CRT Picture Reset restores defaults.
    crt_status_val=-1; cfg.crt_clarity=false;
    settings_initial_group=SETTINGS_STREAM;
    keys={DOWN_FLAG, A_FLAG};
    for(int i=0;i<crt_row;i++) keys.push_back(DOWN_FLAG);
    keys.push_back(A_FLAG);      // Enter CRT Picture
    keys.push_back(DOWN_FLAG);   // -> CRT Clarity
    keys.push_back(RIGHT_FLAG);  // Should refuse (remain false, increment messages)
    keys.push_back(DOWN_FLAG);   // -> Aspect Ratio
    keys.push_back(DOWN_FLAG);   // -> Screen Adjustment
    keys.push_back(DOWN_FLAG);   // -> Output Status
    keys.push_back(DOWN_FLAG);   // -> Reset CRT Settings
    keys.push_back(A_FLAG);      // Open Reset CRT Settings confirmation
    keys.push_back(A_FLAG);      // Confirm Reset inside confirm_action()
    keys.push_back(B_FLAG); keys.push_back(B_FLAG); keys.push_back(B_FLAG);
    unsigned msg_before=messages;
    settings_menu();
    assert(keys.empty() && messages==msg_before+1 && !cfg.crt_clarity);
    assert(cfg.crt_scaling==CRT_SCALE_AUTO && cfg.crt_aspect==CRT_ASPECT_4_3 && cfg.crt_pos_x==0 && cfg.crt_pos_y==0);

    // 4. CRT Retro (240p) shows CRT Picture and HIDES 480i CRT Clarity option.
    crt_status_val=1;
    config_select_factory(&cfg, CONFIG_PROFILE_CRT_RETRO_240P);
    crt_row=crt_picture_row_in_video(); assert(crt_row>=0);
    draws.clear(); settings_initial_group=SETTINGS_STREAM;
    keys={DOWN_FLAG, A_FLAG};
    for(int i=0;i<crt_row;i++) keys.push_back(DOWN_FLAG);
    keys.push_back(A_FLAG); keys.push_back(B_FLAG); keys.push_back(B_FLAG); keys.push_back(B_FLAG);
    settings_menu();
    assert(keys.empty());
    bool retro_saw_scaling=false, retro_saw_clarity=false;
    for(auto &d:draws) if(d.title=="CRT Picture") {
        for(auto &l:d.labels){
            if(l=="Scaling Filter") retro_saw_scaling=true;
            if(l=="CRT Interlace Clarity (Experimental)") retro_saw_clarity=true;
        }
    }
    assert(retro_saw_scaling && !retro_saw_clarity);
    puts("PASS: CRT Picture submenu context visibility, Scaling Filter, 480i-gated Clarity (hidden in 240p), Aspect Ratio, Screen Adjustment, Output Status and Reset.");
    return 0;
}
'''

code = '\n'.join(function(sig) for sig in [
    'static void cycle_stream_profile(',
    'static bool tab_key(',
    'static int wait_button(void)\n{',
    'static bool confirm_action(',
    'static void overlay_options_menu(void)\n{',
    'static void maintenance_menu(int selected)\n{',
    'static void appearance_menu(void)\n{',
    'static void crt_screen_adjustment_menu(',
    'static void crt_output_status_menu(',
    'static bool settings_child_hidden(',
    'static bool settings_item_hidden(',
    'static void settings_menu(void)\n{',
])
Path(sys.argv[1]).write_text(fixture.replace('/* CODE */', code), encoding='utf-8')
