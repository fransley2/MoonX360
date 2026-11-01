"""Drive actual Launch Coverage Save buttons, then reopen and restart the config reader."""
from pathlib import Path
import sys
r=Path(__file__).resolve().parents[2];s=(r/'src/app.c').read_text()
def fn(sig):
 a=s.index(sig);b=s.index('{',a)+1;d=1
 while d:d+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
fixture=r'''
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <deque>
#include <Limelight.h>
#include <map>
#include <string>
#include "../src/launch_settings.h"
#include "../src/ui/ui.h"
static config cfg;static std::deque<int> keys;static std::map<std::string,std::string> files;
static bool fail;static int errors,writes;static std::string displayed,error_text;
char *storage_read(const char *name,size_t *n){if(!files.count(name)){errno=ENOENT;return NULL;}std::string &s=files[name];char *p=(char*)malloc(s.size()+1);memcpy(p,s.c_str(),s.size()+1);if(n)*n=s.size();return p;}
bool storage_write(const char *name,const void *p,size_t n){writes++;if(fail){errno=EACCES;return false;}files[name]=std::string((const char*)p,n);return true;}
static const char *host_key(){return "uuid:host";}
static int wait_button(){assert(!keys.empty());int n=keys.front();keys.pop_front();return n;}
void ui_draw_settings_page(const char*,const ui_setting *rows,int n,int){assert(n==3);displayed=rows[0].value;}
void ui_present(){}
const char *ui_translate(const char *s){return s;}
static void message(int,const char*,const char *s){errors++;error_text=s;}
static void plat_log(const char *s){assert(strstr(s,"STORAGE"));}
static void plat_diagnostics_flush(){}
/* CODE */
int main(){
 config_defaults(&cfg);assert(cfg.launch_animation && !cfg.ui_motion);
 keys={LEFT_FLAG,LEFT_FLAG,LEFT_FLAG,LEFT_FLAG,LEFT_FLAG,DOWN_FLAG,A_FLAG,DOWN_FLAG,A_FLAG};launch_defaults_menu();
 assert(keys.empty() && cfg.launch_cover_ms==0 && !cfg.launch_animation);
 keys={B_FLAG};launch_defaults_menu();assert(displayed=="0 s");
 config_load(&cfg);assert(cfg.launch_cover_ms==0 && !cfg.launch_animation);
 keys={RIGHT_FLAG,DOWN_FLAG,DOWN_FLAG,A_FLAG};launch_settings_menu(42);assert(keys.empty());
 keys={B_FLAG};launch_settings_menu(42);assert(displayed=="0 s");
 launch_settings disk;launch_settings_load(&disk);assert(disk.count==1 && disk.items[0].seconds==0);
 keys={LEFT_FLAG,DOWN_FLAG,DOWN_FLAG,A_FLAG};launch_settings_menu(42);
 keys={B_FLAG};launch_settings_menu(42);assert(displayed.find("Inherit")==0);
 launch_settings_load(&disk);assert(disk.items[0].seconds==-1);
 std::string previous=files["launch-settings.tsv"];fail=true;
 keys={RIGHT_FLAG,DOWN_FLAG,DOWN_FLAG,A_FLAG,B_FLAG};launch_settings_menu(42);
 assert(errors==1 && error_text.find("Access denied")!=std::string::npos && files["launch-settings.tsv"]==previous);
 config old=cfg;previous=files["moonlight.ini"];
 keys={RIGHT_FLAG,DOWN_FLAG,DOWN_FLAG,A_FLAG,B_FLAG};launch_defaults_menu();
 assert(errors==2 && !memcmp(&old,&cfg,sizeof(cfg)) && files["moonlight.ini"]==previous);fail=false;
 keys={B_FLAG};launch_settings_menu(42);assert(displayed.find("Inherit")==0);
 config_load(&cfg);assert(cfg.launch_cover_ms==0 && !cfg.launch_animation);
 puts("PASS: actual global/per-app Save buttons, screen reopen, restart readers, 0 and inherit, animation independent of old focus, useful write error, unchanged memory/disk on failed save");
}
'''
code='\n'.join(fn(x) for x in ['static void log_launch_settings_failure(', 'static void launch_defaults_menu(void)\n{','static void launch_settings_menu(int id)\n{'])
Path(sys.argv[1]).write_text(fixture.replace('/* CODE */',code))
