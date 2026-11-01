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
#include "../src/profile_links.h"
#include "../src/input.h"
#include "../src/ui/ui.h"
#include "../src/settings_layout.h"
static int settings_initial_group,settings_navigation_page,navigation_pending=-1;
static bool profile_editor_active,profile_editor_save;
static void settings_menu();
static unsigned profile_label_revision;
static void plat_log(const char*){}
static void plat_diagnostics_flush(){}
static game_bindings game_bindings;static config cfg;static bool fail_save;static int messages;static std::deque<int> keys;
static unsigned reads;
static std::map<std::string,std::string> files;
char *storage_read(const char *name,size_t *n){reads++;if(!files.count(name)){errno=ENOENT;return NULL;}const std::string &s=files[name];char *p=(char*)malloc(s.size()+1);memcpy(p,s.c_str(),s.size()+1);if(n)*n=s.size();return p;}
bool storage_write(const char *name,const void *p,size_t n){if(fail_save)return false;files[name]=std::string((const char*)p,n);return true;}
static int wait_button(){assert(!keys.empty());int k=keys.front();keys.pop_front();return k;}
void ui_present(){}
void ui_set_settings_context(int,int){}
void ui_draw_settings_page(const char*,const ui_setting*,int,int);
void ui_draw_profiles(const ui_setting *rows,int n,int sel,int){ui_draw_settings_page("Stream Profile",rows,n,sel);}
static int modals;
void ui_draw_confirm(const char*,const char*,const char*){modals++;}
static void settings_menu(){config_customize_stream(&cfg);cfg.bitrate_kbps=23000;profile_editor_save=wait_button()==A_FLAG;}

const char *ui_translate(const char *s){return !strcmp(s,"My profile %d")?"Meu perfil %d":s;}
void ui_draw_settings_page(const char*,const ui_setting *rows,int count,int sel){assert(sel>=0 && sel<count);for(int i=0;i<count;i++)assert(rows[i].label && rows[i].value);}
void ui_draw_settings_options(const char *title,const ui_setting *rows,const int *kinds,int count,int sel){ui_draw_settings_page(title,rows,count,sel);for(int i=0;i<count;i++)assert(kinds[i]);}
int ui_location_enter(const char*){return 0;}void ui_location_restore(int){}
static void message(int,const char*,const char*){messages++;}
/* CODE */
int main(){
 config_defaults(&cfg);cfg.language=2;keys={B_FLAG};save_current_profile();assert(files.empty() && keys.empty());
 keys={A_FLAG};save_current_profile();assert(keys.empty());
 user_profiles p={};assert(config_profiles_load(&p) && p.count==1 && !strcmp(p.items[0].name,"Meu perfil 1"));
 assert(config_profiles_selected(&p,&cfg)==0);config saved=cfg;
 config_customize_stream(&cfg);cfg.bitrate_kbps=25000;assert(config_profiles_selected(&p,&cfg)==-1);
 keys={DOWN_FLAG,DOWN_FLAG,DOWN_FLAG,DOWN_FLAG,DOWN_FLAG,A_FLAG};stream_profile_menu();
 assert(keys.empty() && cfg.bitrate_kbps==saved.bitrate_kbps && cfg.language==2 && config_profiles_selected(&p,&cfg)==0);
 // Current saved profile opens selected. Rename cancellation and successful rename.
 keys={Y_FLAG,DOWN_FLAG,DOWN_FLAG,A_FLAG,UP_FLAG,B_FLAG,B_FLAG};stream_profile_menu();
 assert(keys.empty() && config_profiles_load(&p) && !strcmp(p.items[0].name,"Meu perfil 1"));
 keys={Y_FLAG,DOWN_FLAG,DOWN_FLAG,A_FLAG,UP_FLAG,A_FLAG,B_FLAG};stream_profile_menu();
 assert(keys.empty() && config_profiles_load(&p) && !strcmp(p.items[0].name,"Neu perfil 1") && !strcmp(cfg.user_profile,"Neu perfil 1"));
 config restarted;config_load(&restarted);assert(!strcmp(restarted.user_profile,"Neu perfil 1"));
 // Editing is transactional; B keeps all old values, A updates active saved settings.
 keys={X_FLAG,B_FLAG,B_FLAG};stream_profile_menu();assert(cfg.bitrate_kbps==saved.bitrate_kbps);
 keys={X_FLAG,A_FLAG,B_FLAG};stream_profile_menu();assert(cfg.bitrate_kbps==23000);
 assert(config_profiles_load(&p) && p.items[0].options.bitrate_kbps==23000);
 keys={Y_FLAG,DOWN_FLAG,DOWN_FLAG,DOWN_FLAG,A_FLAG,B_FLAG,B_FLAG};stream_profile_menu();
 assert(keys.empty() && config_profiles_load(&p) && p.count==1 && modals==1);
 keys={Y_FLAG,DOWN_FLAG,DOWN_FLAG,DOWN_FLAG,A_FLAG,A_FLAG,B_FLAG};stream_profile_menu();
 assert(keys.empty() && config_profiles_load(&p) && !p.count && !cfg.user_profile[0] && modals==2);
 config_load(&restarted);assert(!restarted.user_profile[0]);user_profiles reboot={};assert(config_profiles_load(&reboot) && !reboot.count);
 // Factory management has exactly Use / Edit / Save as. No delete action or modal.
 config_select_factory(&cfg,CONFIG_PROFILE_SMOOTH);keys={Y_FLAG,UP_FLAG,B_FLAG,B_FLAG};stream_profile_menu();assert(modals==2);
 // Effective matching can select a saved profile even without an explicit saved-name reference.
 assert(config_profiles_create(&p,&cfg,"Right"));profile_label_revision++;
 assert(!strcmp(selected_profile_name(),"Right"));unsigned cached_reads=reads;assert(!strcmp(selected_profile_name(),"Right") && reads==cached_reads);
 keys={Y_FLAG,DOWN_FLAG,DOWN_FLAG,A_FLAG,UP_FLAG,A_FLAG,B_FLAG};stream_profile_menu();
 assert(!strcmp(cfg.user_profile,"Sight") && !strcmp(selected_profile_name(),"Sight"));
 keys={X_FLAG,A_FLAG,B_FLAG};stream_profile_menu();assert(cfg.bitrate_kbps==23000 && !strcmp(cfg.user_profile,"Sight"));
 fail_save=true;keys={A_FLAG};save_current_profile();assert(messages>=1 && keys.empty());fail_save=false;
 puts("PASS: actual adjustments Save/cancel, unique editable default name, combined selector applies every saved option, management rename/cancel/delete/confirmation and failure reporting");
}

"""
Path(sys.argv[1]).write_text(fixture.replace('/* CODE */','\n'.join(function(sig) for sig in ['static bool edit_profile_name(', 'static void save_current_profile(', 'static bool confirm_action(', 'static void edit_stream_profile(', 'static void stream_profile_menu(', 'static const char *selected_profile_name(void)\n{'])))
