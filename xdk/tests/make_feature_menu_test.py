from pathlib import Path
import sys
r=Path(__file__).resolve().parents[2];s=(r/'src/app.c').read_text()
def function(sig):
 a=s.index(sig);b=s.index('{',a)+1;d=1
 while d:d+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
fixture=r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <deque>
#include <Limelight.h>
#include "../src/config.h"
#include "../src/input.h"
#include "../src/stick_calibration.h"
#include "../src/ui/ui.h"
static bool tab_key(int,int){return false;}
static config cfg;static std::deque<int> keys;static int saves,pauses,power_calls,messages;
static bool save_ok=true,last_restart,streaming,block_on_confirm,controller=true;
static int wait_button(){assert(!keys.empty());int b=keys.front();keys.pop_front();if(block_on_confirm && keys.empty() && b==A_FLAG)streaming=true;return b;}
int input_menu_pressed(){return wait_button();}
int input_poll(pad *p){memset(p,0,MAX_PADS*sizeof(*p));p[2].present=controller;p[2].lx=12000;p[2].ly=-9000;p[2].rx=-24000;p[2].ry=18000;return controller?4:0;}
void ui_draw_settings_options(const char *title,const ui_setting *rows,const int *kinds,int n,int sel){
 assert(sel>=0 && sel<n);for(int i=0;i<n;i++){assert(rows[i].label && rows[i].value);assert(kinds[i]);}
 if(!strcmp(title,"Stick calibration")) {
  bool right=!strcmp(rows[0].value,"Right");char raw[64],filtered[64];
  int x=controller?(right?-24000:12000):0,y=controller?(right?18000:-9000):0;
  if(controller)snprintf(raw,sizeof(raw),"X %d | Y %d",x,y);else snprintf(raw,sizeof(raw),"No controller");
  snprintf(filtered,sizeof(filtered),"X %d | Y %d",stick_calibrate(x,right?cfg.right_min:cfg.left_min,right?cfg.right_max:cfg.left_max),stick_calibrate(y,right?cfg.right_min:cfg.left_min,right?cfg.right_max:cfg.left_max));
  assert(!strcmp(rows[3].value,raw) && !strcmp(rows[4].value,filtered));
 }
}
void ui_present(){}
static void plat_sleep_ms(unsigned){}
static void discovery_pause(){pauses++;}
bool config_save(const config*){saves++;return save_ok;}
static bool plat_power_action(bool restart){power_calls++;last_restart=restart;return false;}
static bool video_xdk_is_streaming(){return streaming;}
/* POWER_AVAILABLE */
static void message(int,const char*,const char*){messages++;}
/* CODE */
int main(){
 cfg.left_max=cfg.right_max=100;
 keys={DOWN_FLAG,RIGHT_FLAG,RIGHT_FLAG,UP_FLAG,RIGHT_FLAG,DOWN_FLAG,RIGHT_FLAG,DOWN_FLAG,LEFT_FLAG,B_FLAG};
 calibrate_sticks();assert(keys.empty());assert(cfg.left_min==2 && cfg.left_max==100 && cfg.right_min==1 && cfg.right_max==99);
 keys={UP_FLAG,A_FLAG,B_FLAG};calibrate_sticks();assert(cfg.left_min==0 && cfg.right_min==0 && cfg.left_max==100 && cfg.right_max==100);
 keys={A_FLAG};power_menu(true);assert(power_calls==0 && saves==0); // default Cancel
 keys={DOWN_FLAG,B_FLAG};power_menu(false);assert(power_calls==0);
 keys={DOWN_FLAG,A_FLAG};power_menu(true);assert(power_calls==1 && last_restart && saves==1 && pauses==1);
 save_ok=false;keys={DOWN_FLAG,A_FLAG};power_menu(false);assert(power_calls==1 && messages==2);
 save_ok=true;keys={DOWN_FLAG,A_FLAG};power_menu(false);assert(power_calls==2 && !last_restart);
 int old_saves=saves,old_power=power_calls;streaming=true;keys.clear();power_menu(true);assert(saves==old_saves && power_calls==old_power);streaming=false;
 block_on_confirm=true;keys={DOWN_FLAG,A_FLAG};power_menu(false);assert(saves==old_saves && power_calls==old_power);block_on_confirm=streaming=false;
 controller=false;keys={B_FLAG};calibrate_sticks();controller=true;
 keys={DOWN_FLAG};for(int i=0;i<60;i++)keys.push_back(RIGHT_FLAG);keys.push_back(B_FLAG);calibrate_sticks();assert(cfg.left_min==30 && cfg.right_min==0);
 keys={DOWN_FLAG};for(int i=0;i<60;i++)keys.push_back(LEFT_FLAG);keys.push_back(B_FLAG);calibrate_sticks();assert(cfg.left_min==0);
 keys={DOWN_FLAG,DOWN_FLAG};for(int i=0;i<60;i++)keys.push_back(LEFT_FLAG);keys.push_back(B_FLAG);calibrate_sticks();assert(cfg.left_max==50 && cfg.right_max==100);
 keys={DOWN_FLAG,DOWN_FLAG};for(int i=0;i<60;i++)keys.push_back(RIGHT_FLAG);keys.push_back(B_FLAG);calibrate_sticks();assert(cfg.left_max==100);
 puts("PASS: actual calibration page selects independent sticks and reset; power defaults Cancel, B cancels, save failure blocks action, restart/shutdown distinct");
}
"""
platform=(r/'xdk/src/main_xdk.cpp').read_text();start=platform.index('bool plat_power_available(');end=platform.index('\n',start)
fixture=fixture.replace('/* POWER_AVAILABLE */',platform[start:end])
Path(sys.argv[1]).write_text(fixture.replace('/* CODE */',function('static void calibrate_sticks(')+'\n'+function('static void power_menu(')))
