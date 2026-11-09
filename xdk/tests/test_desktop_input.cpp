#include <assert.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "../../src/desktop_input.h"
#include "../../src/quick_menu.h"
struct key_event {int key,action,mods;};
struct mouse_event {int action,button;};
static std::vector<key_event> keys;
static std::vector<mouse_event> clicks;
static std::string text;
static int movement,vertical,horizontal,text_result;
int LiSendKeyboardEvent(short key,char action,char mods){key_event e={key,action,(unsigned char)mods};keys.push_back(e);return 0;}
int LiSendMouseButtonEvent(char action,int button){mouse_event e={action,button};clicks.push_back(e);return 0;}
int LiSendMouseMoveEvent(short x,short y){movement+=abs(x)+abs(y);return 0;}
int LiSendScrollEvent(signed char){return 0;}
int LiSendHighResScrollEvent(short amount){vertical+=amount;return 0;}
int LiSendHighResHScrollEvent(short amount){horizontal+=amount;return 0;}
int LiSendUtf8TextEvent(const char *value,unsigned size){text.append(value,size);return text_result;}
static unsigned tick;
static void step(desktop_input &d,pad &p,int buttons){p.buttons=buttons;tick+=4;desktop_step(&d,&p,tick,input_exit_mask(0));}
static void press(desktop_input &d,pad &p,int buttons){step(d,p,0);step(d,p,buttons);step(d,p,0);}
static desktop_input fresh(){desktop_input d={};desktop_defaults(&d.options);d.enabled=true;d.clock=tick;return d;}
static void paired(){bool held[256]={};for(size_t i=0;i<keys.size();i++){key_event e=keys[i];assert(e.key>0 && e.key<256);if(e.action==KEY_ACTION_DOWN){assert(!held[e.key]);held[e.key]=true;}else{assert(held[e.key]);held[e.key]=false;}}for(int i=0;i<256;i++)assert(!held[i]);}
int main(){
    desktop_input d=fresh();pad p={};p.present=true;
    desktop_chord('O',MODIFIER_META|MODIFIER_CTRL);
    assert(keys.size()==6 && keys[0].key==0x5b && keys[1].key==0xa2 && keys[2].key=='O');
    assert(keys[2].action==KEY_ACTION_DOWN && keys[3].key=='O' && keys[3].action==KEY_ACTION_UP);
    assert(keys[4].key==0xa2 && keys[5].key==0x5b && !(keys[5].mods&MODIFIER_META));paired();keys.clear();
    step(d,p,A_FLAG);assert(clicks.size()==1 && d.held[0]);
    p.lx=32767;for(int i=0;i<50;i++)step(d,p,A_FLAG);
    assert(clicks.size()==1 && movement>0);p.lx=0;step(d,p,0);assert(clicks.size()==2 && !d.held[0]);
    press(d,p,B_FLAG);press(d,p,X_FLAG);assert(clicks.size()==6 && clicks[2].button==BUTTON_RIGHT && clicks[4].button==BUTTON_MIDDLE);
    movement=0;p.lx=3000;for(int i=0;i<100;i++)step(d,p,0);assert(!movement);p.lx=32767;
    d.options.acceleration=0;for(int i=0;i<100;i++)step(d,p,0);int normal=movement;assert(normal>0);
    movement=0;p.lt=255;for(int i=0;i<100;i++)step(d,p,0);assert(movement<normal/2);
    movement=0;p.lt=0;p.rt=255;for(int i=0;i<100;i++)step(d,p,0);assert(movement>normal*3/2);
    p.rt=0;p.lx=0;p.rx=22000;p.ry=-22000;for(int i=0;i<100;i++)step(d,p,0);assert(horizontal>0 && vertical<0);p.rx=p.ry=0;
    // Mapped shortcuts are edges, never one event per 4 ms poll.
    step(d,p,LB_FLAG);for(int i=0;i<100;i++)step(d,p,LB_FLAG);assert(keys.size()==4);step(d,p,0);paired();keys.clear();
    press(d,p,RB_FLAG);press(d,p,UP_FLAG);press(d,p,DOWN_FLAG);press(d,p,LEFT_FLAG);press(d,p,RIGHT_FLAG);press(d,p,PLAY_FLAG);paired();keys.clear();
    step(d,p,BACK_FLAG);assert(keys.empty());step(d,p,0);assert(keys.size()==2 && keys[0].key==0x1b);step(d,p,0);keys.clear();
    const int chords[]={BACK_FLAG|RS_CLK_FLAG,BACK_FLAG|LS_CLK_FLAG,BACK_FLAG|LB_FLAG|RB_FLAG|X_FLAG};
    for(int i=0;i<3;i++){step(d,p,BACK_FLAG);step(d,p,chords[i]);step(d,p,BACK_FLAG);step(d,p,0);step(d,p,0);assert(keys.empty());}
    // Opening keyboard releases a drag before any subsequent character.
    step(d,p,A_FLAG);size_t before=clicks.size();step(d,p,Y_FLAG|A_FLAG);assert(clicks.size()==before+1 && !d.held[0] && d.view==DESK_KEYBOARD);
    step(d,p,0);d.keyboard_page=1;d.selected=0;press(d,p,A_FLAG);assert(text=="\xc3\xa1");
    d.selected=19;press(d,p,A_FLAG);assert(text=="\xc3\xa1\xc3\x8d");
    d.selected=30;press(d,p,A_FLAG);assert(text.back()==' ');
    d.selected=31;press(d,p,A_FLAG);assert(keys.back().key==8);
    d.selected=33;press(d,p,A_FLAG);assert(d.caps);
    d.selected=34;press(d,p,A_FLAG);assert(d.keyboard_page==2);
    d.selected=1;press(d,p,A_FLAG);assert(text.back()=='2');
    text_result=-1;press(d,p,A_FLAG);assert(d.text_error);text_result=0;
    press(d,p,B_FLAG);assert(!d.view);step(d,p,0);
    step(d,p,RS_CLK_FLAG);assert(d.view==DESK_TOOLBAR);step(d,p,0);
    for(int i=0;i<9;i++){d.view=DESK_TOOLBAR;d.neutral=false;d.selected=i;desktop_toolbar_action(&d);paired();}
    // Disconnect, Guide/Quick Menu suspension, mode changes release held buttons.
    d=fresh();step(d,p,A_FLAG|B_FLAG|X_FLAG);assert(d.held[0] && d.held[1] && d.held[2]);
    p.present=false;step(d,p,0);assert(!d.held[0] && !d.held[1] && !d.held[2] && d.neutral);
    p.present=true;step(d,p,A_FLAG);assert(!d.held[0]);step(d,p,0);step(d,p,A_FLAG);assert(d.held[0]);
    desktop_suspend(&d);assert(!d.held[0] && d.neutral && !d.view);
    desktop_command(&d,DESK_TOGGLE);assert(!d.enabled);assert(!desktop_step(&d,&p,tick,input_exit_mask(0)));
    d=fresh();d.view=DESK_TOOLBAR;d.options.keyboard=1;desktop_keyboard(&d);assert(!d.view && d.neutral);paired();
    d.view=DESK_TOOLBAR;d.options.keyboard=2;desktop_keyboard(&d);assert(!d.view && d.neutral && keys[keys.size()-2].key=='B');paired();
    // Every remap remains a permutation; Back click is a paired tap on release.
    d=fresh();desktop_remap(&d.options,11,DA_LEFT_CLICK);bool seen[DA_COUNT]={};
    for(int i=0;i<DESKTOP_BINDINGS;i++){assert(!seen[d.options.mapping[i]]);seen[d.options.mapping[i]]=true;}
    p.buttons=0;before=clicks.size();press(d,p,BACK_FLAG);assert(clicks.size()==before+2);
    // Menu exposes Desktop without changing the old row values or chord behavior.
    quick_menu q={};pad pads[MAX_PADS]={};pads[0].present=true;pads[0].buttons=BACK_FLAG|LS_CLK_FLAG;
    bool quit=false,close=false;int mouse=-1;
    assert(quick_step(&q,pads,false,input_exit_mask(0),&quit,&close,&mouse,0) && q.open);
    pads[0].buttons=0;quick_step(&q,pads,false,input_exit_mask(0),&quit,&close,&mouse,4);
    q.row=QUICK_DESKTOP;pads[0].buttons=A_FLAG;quick_step(&q,pads,false,input_exit_mask(0),&quit,&close,&mouse,8);assert(q.page==QUICK_DESKTOP_PAGE);
    pads[0].buttons=0;quick_step(&q,pads,false,input_exit_mask(0),&quit,&close,&mouse,12);
    pads[0].buttons=A_FLAG;quick_step(&q,pads,false,input_exit_mask(0),&quit,&close,&mouse,16);assert(q.desktop_request==DESK_TOGGLE && !q.open);
    puts("PASS: Desktop mouse/drag/deadzone/precision/boost/two-axis scrolling, paired OSK/modifiers, edge shortcuts, reserved Back chords, UTF-8 accents/symbols/caps/cancel, toolbar, disconnect and mode neutralization, mapping conflicts and Quick Menu.");
}
