"""Render the implemented menu map, translations and real auxiliary menus at 480p/720p."""
from pathlib import Path
import re,sys
root=Path(__file__).resolve().parents[2]
source=(root/'src/app.c').read_text(encoding='utf-8')
def initializer(name):
    return re.search(r'static const [^;]+\b'+name+r'\[[^]]+\]\s*=\s*(\{.*?\n\s*\});',source,re.S)[1]
def function(sig):
    a=source.index(sig);b=source.index('{',a)+1;depth=1
    while depth:depth+=(source[b]=='{')-(source[b]=='}');b+=1
    return source[a:b]
output=r'''
#include "../src/ui/ui.h"
#include "../src/ui/draw.h"
#include "../src/settings_layout.h"
#include "../src/config.h"
#include "../src/input.h"
#include <Limelight.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb/stb_image_write.h"
static config cfg;
static char filename[160];static int width,height,language;
void ui_present(void) {
    ui_draw_software();canvas_t *cv=ui_canvas();
    unsigned char *rgb=(unsigned char*)malloc(cv->w*cv->h*3);
    for(int i=0;i<cv->w*cv->h;i++){rgb[i*3]=(unsigned char)(cv->px[i]>>16);rgb[i*3+1]=(unsigned char)(cv->px[i]>>8);rgb[i*3+2]=(unsigned char)cv->px[i];}
    assert(stbi_write_png(filename,cv->w,cv->h,3,rgb,cv->w*3));free(rgb);
}
char *storage_read(const char*,size_t*){return NULL;}
bool storage_write(const char*,const void*,size_t){return true;}
static int wait_button(){return B_FLAG;}
'''
for name in ('labels','helps'):output+='static const char *'+name+'[]='+initializer(name)+';\n'
output+='\n'.join(function(sig) for sig in ('static void appearance_menu(void)\n{','static void overlay_options_menu(void)\n{','static void pointer_settings_menu(void)\n{'))
output+=r'''
static void name(const char *page,int selected) {
    snprintf(filename,sizeof(filename),"menu-%dx%d-lang%d-%s-%d.png",width,height,language,page,selected);
}
int main(void) {
    const int sizes[][2]={{640,480},{1280,720}};
    const char *values[]={"1280x720","30 FPS","12 Mbps","On","GPU","On","On","Off","Run test","Standard","LB+RB+L3+R3","L 0-100% | R 0-100%","Off","1024 bytes","Off","Off","View logs","Auto","12 Mbps","Auto Quality","On","12 sessions","View analysis","On","View usage","Automatic (current)","View metrics","Manual","1280x720 / 60 Hz","Auto","Portuguese (Brazil)","Balanced","4","Confirm...","Confirm...","Manage","Stereo","Back + R3","Save","5 s"};
    static_assert(sizeof(values)/sizeof(values[0])==SET_COUNT,"Preview needs every setting value");
    for(int size=0;size<2;size++)for(language=0;language<CONFIG_LANGUAGE_COUNT;language++) {
        width=sizes[size][0];height=sizes[size][1];assert(ui_init(width,height));ui_draw_enable(true);ui_set_language(language);
        config_defaults(&cfg);config_apply_preset(&cfg,CONFIG_PROFILE_STANDARD);
        values[SET_LANGUAGE]=language==0?"English":language==1?"Spanish":"Portuguese (Brazil)";
        for(int page=0;page<MENU_COUNT;page++) {
            ui_setting rows[SET_COUNT];int kinds[SET_COUNT],n=0;
            for(int i=0;i<SETTINGS_ENTRY_COUNT;i++) {
                const settings_menu_entry &entry=settings_entries[i];if(entry.page!=page)continue;
                if(entry.setting==SET_RENDERER)continue;
                if(entry.setting>=0) {
                    int id=entry.setting;rows[n]={labels[id],values[id],helps[id]};kinds[n++]=settings_kind(id);
                } else {
                    int child=entry.child;rows[n]={settings_pages[child].title,"Open",settings_pages[child].help};
                    if(child==MENU_RESTART_APP || child==MENU_RESET_APP)rows[n].value="Confirm...";
                    kinds[n++]=child==MENU_RESTART_APP || child==MENU_RESET_APP?UI_OPTION_ACTION:UI_OPTION_OPEN;
                }
            }
            if(!n)continue;
            int ancestors[MENU_COUNT],depth=0;
            for(int p=page;p>=0;p=settings_pages[p].parent)ancestors[depth++]=p;
            int tab=ancestors[depth-1]==MENU_STREAM?UI_STREAM:UI_SETTINGS;
            ui_set_settings_context(tab,settings_pages[page].category);ui_set_page(tab);
            int token=ui_location_enter(NULL);
            while(depth)ui_location_enter(settings_pages[ancestors[--depth]].title);
            for(int sel=0;sel<n;sel++) {
                name(settings_pages[page].title,sel);ui_draw_settings_options(settings_pages[page].title,rows,kinds,n,sel);ui_present();
            }
            ui_location_restore(token);
        }
        ui_set_settings_context(UI_SETTINGS,SETTINGS_APPEARANCE);ui_set_page(UI_SETTINGS);
        int token=ui_location_enter("Settings");ui_location_enter("Appearance and display");
        name("appearance",0);appearance_menu();name("stream-ui",0);overlay_options_menu();ui_location_restore(token);
        ui_set_settings_context(UI_SETTINGS,SETTINGS_CONTROLS);token=ui_location_enter("Settings");ui_location_enter("Controllers and mouse");
        name("remote-input",0);pointer_settings_menu();ui_location_restore(token);
        ui_set_settings_context(-1,-1);
    }
    puts("PASS: every implemented tree row plus real Appearance, Streaming interface and Remote input previews; 640x480 / 1280x720 in EN/ES/PT-BR.");
}
'''
Path(sys.argv[1]).write_text(output,encoding='utf-8')
