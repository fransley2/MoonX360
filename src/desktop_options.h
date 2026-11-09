/* Global desktop preferences, deliberately outside saved stream profiles. */
#pragma once
#define DESKTOP_BINDINGS 12
enum desktop_action {DA_LEFT_CLICK,DA_RIGHT_CLICK,DA_MIDDLE_CLICK,DA_KEYBOARD,
 DA_UP,DA_DOWN,DA_LEFT,DA_RIGHT,DA_BACK,DA_FORWARD,DA_ENTER,DA_ESCAPE,DA_COUNT};
struct desktop_options {int speed,deadzone,acceleration,precision,scroll_speed,keyboard;
 int mapping[DESKTOP_BINDINGS];};
static void desktop_defaults(struct desktop_options *o) {
 o->speed=100;o->deadzone=18;o->acceleration=1;o->precision=25;o->scroll_speed=100;o->keyboard=0;
 for(int i=0;i<DESKTOP_BINDINGS;i++)o->mapping[i]=i;
}
static const char *desktop_action_name(int a) {
 static const char *names[]={"Left click","Right click","Middle click","Keyboard","Arrow Up","Arrow Down",
 "Arrow Left","Arrow Right","Browser Back","Browser Forward","Enter","Escape"};
 return a>=0 && a<DA_COUNT?names[a]:"Invalid";
}
/* Remapping swaps the occupied action; every action remains reachable. */
static void desktop_remap(struct desktop_options *o,int slot,int action) {
 if(slot<0 || slot>=DESKTOP_BINDINGS || action<0 || action>=DA_COUNT)return;
 for(int i=0;i<DESKTOP_BINDINGS;i++)if(o->mapping[i]==action){o->mapping[i]=o->mapping[slot];break;}
 o->mapping[slot]=action;
}
