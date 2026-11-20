from pathlib import Path
import sys
r=Path(__file__).resolve().parents[2];s=(r/'src/app.c').read_text(encoding='utf-8-sig')
a=s.index('static bool aurora_browse_path(');b=s.index('static void choose_wallpaper(void)\n{',a)
f=r"""
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <deque>
#include <Limelight.h>
#include "../src/ui/ui.h"
#include "../src/aurora_shortcuts.h"
#include "../src/aurora_paths.h"
static std::deque<int> keys;
static int errors,image_rows,unavailable;
static int wait_button(){assert(!keys.empty());int k=keys.front();keys.pop_front();return k;}
static void message(int,const char*,const char*){errors++;}
static void ui_draw_settings_page(const char*,const ui_setting*,int,int){}
static void ui_draw_settings_options(const char*,const ui_setting *rows,const int*,int n,int){for(int i=0;i<n;i++)if(!strcmp(rows[i].value,"Select image"))image_rows++;}
void ui_present(){}
bool aurora_shortcut_folder_ready(const char*){return true;}
bool aurora_shortcut_valid_aurora(const char*){return true;}
bool aurora_shortcut_valid_xex(const char*){assert(false);return false;}
bool aurora_shortcut_is_current_xex(const char*){assert(false);return false;}
int aurora_browser_edit_name(char*,size_t){assert(false);return -1;}
bool aurora_browser_create_folder(const char*,const char*,char*,size_t){assert(false);return false;}
int aurora_browser_roots(aurora_browser_entry *items,int){strcpy(items[0].name,"Hdd1");items[0].folder=1;return 1;}
int aurora_browser_list(const char *folder,int mode,aurora_browser_entry *items,int){assert(mode==AURORA_BROWSER_IMAGE);if(unavailable){unavailable=0;return -1;}bool root=!strcmp(folder,"Hdd1:\\");strcpy(items[0].name,root?"Pictures":"game.JPEG");items[0].folder=root?1:0;return 1;}
/* BROWSER */
int main(){
 char path[160]="";keys={A_FLAG,A_FLAG,A_FLAG};assert(aurora_browse_path("Choose wallpaper",path,AURORA_BROWSER_IMAGE));
 assert(!strcmp(path,"Hdd1:\\Pictures\\game.JPEG") && keys.empty() && image_rows && !errors);
 path[0]=0;keys={B_FLAG};assert(!aurora_browse_path("Choose wallpaper",path,AURORA_BROWSER_IMAGE) && !path[0]);
 path[0]=0;keys={A_FLAG,Y_FLAG,B_FLAG};assert(!aurora_browse_path("Choose wallpaper",path,AURORA_BROWSER_IMAGE));
 path[0]=0;unavailable=1;keys={A_FLAG,B_FLAG};assert(!aurora_browse_path("Choose wallpaper",path,AURORA_BROWSER_IMAGE) && errors==1);
 puts("PASS: actual image browser root/folder/file selection, cancel, roots, inaccessible device, and no XEX validation or folder creation");
}
"""
Path(sys.argv[1]).write_text(f.replace('/* BROWSER */',s[a:b]))
