from pathlib import Path
import sys
r=Path(__file__).resolve().parents[2];source=(r/'src/app.c').read_text()
def function(sig):
 a=source.index(sig);b=source.index('{',a)+1;depth=1
 while depth:depth+=(source[b]=='{')-(source[b]=='}');b+=1
 return source[a:b]
fixture=r"""
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <deque>
#include <Limelight.h>
#include <map>
#include <string>
#include "../src/config.h"
#include "../src/input.h"
#include "../src/ui/ui.h"
static config cfg;static bool fail_save;static int messages;static std::deque<int> keys;
static std::map<std::string,std::string> files;
char *storage_read(const char *name,size_t *n){if(!files.count(name)){errno=ENOENT;return NULL;}const std::string &s=files[name];char *p=(char*)malloc(s.size()+1);memcpy(p,s.c_str(),s.size()+1);if(n)*n=s.size();return p;}
bool storage_write(const char *name,const void *p,size_t n){if(fail_save)return false;files[name]=std::string((const char*)p,n);return true;}
static int wait_button(){assert(!keys.empty());int k=keys.front();keys.pop_front();return k;}
void ui_present(){}
const char *ui_translate(const char *s){return !strcmp(s,"My profile %d")?"Meu perfil %d":s;}
void ui_draw_settings_page(const char*,const ui_setting *rows,int count,int sel){assert(sel>=0 && sel<count);for(int i=0;i<count;i++)assert(rows[i].label && rows[i].value);}
int ui_location_enter(const char*){return 0;}void ui_location_restore(int){}
static void message(int,const char*,const char*){messages++;}
/* CODE */
int main(){
 config_defaults(&cfg);cfg.language=2;keys={A_FLAG,B_FLAG};user_profiles_menu();assert(keys.empty());
 user_profiles p={};assert(config_profiles_load(&p) && p.count==1 && !strcmp(p.items[0].name,"Meu perfil 1"));
 config saved=cfg;cfg.bitrate_kbps=25000;
 keys={DOWN_FLAG,A_FLAG,A_FLAG,B_FLAG};user_profiles_menu();assert(cfg.bitrate_kbps==saved.bitrate_kbps && cfg.language==2);
 // Rename is cancellable and controller-only; failed writes leave the profile intact.
 keys={DOWN_FLAG,A_FLAG,DOWN_FLAG,A_FLAG,UP_FLAG,B_FLAG,B_FLAG,B_FLAG};user_profiles_menu();
 assert(config_profiles_load(&p) && !strcmp(p.items[0].name,"Meu perfil 1"));
 keys={DOWN_FLAG,A_FLAG,DOWN_FLAG,A_FLAG,UP_FLAG,A_FLAG,B_FLAG,B_FLAG};user_profiles_menu();
 assert(config_profiles_load(&p) && !strcmp(p.items[0].name,"Neu perfil 1"));
 keys={DOWN_FLAG,A_FLAG,DOWN_FLAG,DOWN_FLAG,A_FLAG,A_FLAG,B_FLAG};user_profiles_menu();assert(config_profiles_load(&p) && p.count==1); // default Cancel
 keys={DOWN_FLAG,A_FLAG,DOWN_FLAG,DOWN_FLAG,A_FLAG,DOWN_FLAG,A_FLAG,B_FLAG};user_profiles_menu();assert(config_profiles_load(&p) && !p.count);
 fail_save=true;keys={A_FLAG,B_FLAG};user_profiles_menu();assert(messages>=2);fail_save=false;
 puts("PASS: actual profile menu saves default name, applies next connection, edits/cancels with controller, Delete defaults Cancel, errors visible");
}
"""
Path(sys.argv[1]).write_text(fixture.replace('/* CODE */',function('static bool edit_profile_name(')+'\n'+function('static void user_profiles_menu(')))
