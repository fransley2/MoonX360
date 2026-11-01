"""Actual launch preparation with cover replacement during entry."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
s=(root/'src/app.c').read_text()
a=s.index('    launch_begin(cover_delay,cfg.launch_animation)')
b=s.index('    art_launch_begin(app_id);art_launch_tick();',a)+len('    art_launch_begin(app_id);art_launch_tick();')
fixture=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../src/ui/ui.h"
static ui_app launch_app;
static bool launch_connecting,commands,frozen,prepared,had_art;
static volatile int launch_handoff;
static uint32_t launch_ui_started,*app_art[1],art_generation[1],*offered;
static struct {int id;} apps[1]={{42}};
static const int app_count=1,app_id=42,cover_delay=5000;
static const char *app_title="Game";
static char launch_pair_pin[5];
static struct {bool launch_animation;int width,height,fps,bitrate_kbps;} cfg={true,1280,720,60,12000};
static void crypto_random(void *p,size_t n){memset(p,1,n);}
void ui_launch_pair_pin(const char*){}
static struct {struct {int width,height,fps,bitrate;} sc;} lj={{1280,720,60,12000}};
static int home_art;
static void plat_atomic_store(volatile int *p,int n){*p=n;}
static uint32_t plat_now_ms(){return 100;}
static const char *home_key(){return "PC";}
static const uint32_t *home_art_get(int*,const char*,int,uint32_t*){return NULL;}
static const char *selected_profile_name(){return "Smooth";}
static void launch_begin(unsigned,bool){offered=NULL;}
void ui_launch_begin(const ui_app *a,uint32_t,bool){assert(commands && a->art==app_art[0]);}
void ui_launch_session(const char*,int,int,int,int){}
static void ui_draw_software(){assert(commands && !frozen);if(had_art)assert(app_art[0][0]==123);commands=false;frozen=true;}
void ui_art_size(int *w,int *h){*w=*h=1;}
static bool launch_offer_art(const uint32_t *p,int,int,uint32_t){offered=(uint32_t*)p;return p!=NULL;}
static void art_launch_begin(int){
 assert(frozen && !commands);free(app_art[0]);app_art[0]=(uint32_t*)malloc(4);assert(app_art[0]);app_art[0][0]=456;art_generation[0]++;
}
static void art_launch_tick(){launch_offer_art(app_art[0],1,1,art_generation[0]);}
static bool launch_prepare(){assert(frozen && offered==app_art[0]);prepared=true;return true;}
static void draw_launch(void*,uint32_t){assert(prepared);}
static void launch_present(){assert(prepared);}
static void actual_prepare(){
/* PREPARATION */
}
int main(){for(int i=0;i<20;i++){
 had_art=i%2!=0;app_art[0]=had_art?(uint32_t*)malloc(4):NULL;if(had_art)app_art[0][0]=123;
 commands=true;frozen=prepared=false;actual_prepare();assert(prepared && offered[0]==456);free(app_art[0]);app_art[0]=NULL;
}puts("PASS: actual launch snapshots borrowed commands before cache eviction, late replacement and missing art across 20 launches");}
'''
Path(sys.argv[1]).write_text(fixture.replace('/* PREPARATION */',s[a:b]))
