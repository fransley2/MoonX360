#include <stdint.h>
#include <string.h>
#include <assert.h>
static unsigned menu_restores;
static void ui_present(){menu_restores++;}
#include <stdio.h>
#include <vector>
#include "../src/input_session.h"
#include "../src/input_hotplug.h"
#include "../src/remote_input.h"
#include "../src/quick_menu.h"
#include "../src/system_ui.h"
typedef uint32_t DWORD;typedef int32_t LONG;typedef uint16_t WORD;typedef void *HANDLE,*LPVOID;
#define WINAPI
#define ERROR_SUCCESS 0
#define ERROR_DEVICE_NOT_CONNECTED 1167
#define CREATE_SUSPENDED 4
#define INFINITE 0xffffffff
#define HW_THREAD_RENDER 5
#define XINPUT_GAMEPAD_A 0x1000
#define XINPUT_GAMEPAD_B 0x2000
#define XINPUT_GAMEPAD_X 0x4000
#define XINPUT_GAMEPAD_Y 0x8000
#define XINPUT_GAMEPAD_DPAD_UP 1
#define XINPUT_GAMEPAD_DPAD_DOWN 2
#define XINPUT_GAMEPAD_DPAD_LEFT 4
#define XINPUT_GAMEPAD_DPAD_RIGHT 8
#define XINPUT_GAMEPAD_START 16
#define XINPUT_GAMEPAD_BACK 32
#define XINPUT_GAMEPAD_LEFT_THUMB 64
#define XINPUT_GAMEPAD_RIGHT_THUMB 128
#define XINPUT_GAMEPAD_LEFT_SHOULDER 256
#define XINPUT_GAMEPAD_RIGHT_SHOULDER 512
struct XINPUT_GAMEPAD {WORD wButtons;uint8_t bLeftTrigger,bRightTrigger;int16_t sThumbLX,sThumbLY,sThumbRX,sThumbRY;};
struct XINPUT_STATE {DWORD dwPacketNumber;XINPUT_GAMEPAD Gamepad;};
struct XINPUT_VIBRATION {WORD wLeftMotorSpeed,wRightMotorSpeed;};
static DWORD tick;static bool connected[4],guide,inject_stop;static XINPUT_STATE samples[4];
static int probes[4],arrivals[4],rumble_calls,sleep_count,join_count;static WORD motor[4];
struct event {short port,mask;int buttons;unsigned lt;int lx;};static std::vector<event> events;
int LiSendControllerArrivalEvent(uint8_t port,uint16_t,uint8_t,uint32_t,uint16_t){arrivals[port]++;return 0;}
int LiSendMultiControllerEvent(short port,short mask,int buttons,unsigned char lt,unsigned char,short lx,short,short,short){event e={port,mask,buttons,lt,lx};events.push_back(e);return 0;}
static DWORD GetTickCount(){return tick;}
static LONG InterlockedExchange(volatile LONG *p,LONG n){LONG old=*p;*p=n;return old;}
static LONG InterlockedCompareExchange(volatile LONG *p,LONG n,LONG old){LONG value=*p;if(value==old)*p=n;return value;}
static LONG InterlockedIncrement(volatile LONG *p){return ++*p;}
static bool gfx_system_ui_active(){return guide;}
static void xdk_audit_thread(const char*){}
static DWORD XInputGetState(int i,XINPUT_STATE *out){probes[i]++;if(!connected[i])return ERROR_DEVICE_NOT_CONNECTED;*out=samples[i];return ERROR_SUCCESS;}
static DWORD XInputSetState(int,XINPUT_VIBRATION*);
static void Sleep(DWORD);
static HANDLE CreateThread(void*,unsigned,DWORD (*)(LPVOID),void*,unsigned,void*){return (HANDLE)1;}
static void XSetThreadProcessor(HANDLE,int){}
static void ResumeThread(HANDLE){}
static DWORD WaitForSingleObject(HANDLE,DWORD){join_count++;return 0;}
static void CloseHandle(HANDLE){}
void input_rumble(unsigned short,unsigned short,unsigned short);
static int xdk_aux_processor(){return HW_THREAD_RENDER;}
typedef unsigned char BYTE;
#define XINPUT_MAX_KEYBOARD_STATE 6
#define XINPUT_MODIFIER_LSHIFT 2
#define XINPUT_MODIFIER_RSHIFT 32
#define XINPUT_MODIFIER_LCONTROL 1
#define XINPUT_MODIFIER_RCONTROL 16
#define XINPUT_MODIFIER_LALT 4
#define XINPUT_MODIFIER_RALT 64
#define XINPUT_MODIFIER_LGUI 8
#define XINPUT_MODIFIER_RGUI 128
struct XINPUT_KEYBOARDSTATE { BYTE bModifiers; WORD VKCodes[6]; };
static int keyboard_calls[4],keyboard_down,keyboard_up;
static DWORD XInputGetKeyboardState(DWORD user,XINPUT_KEYBOARDSTATE *out){
 keyboard_calls[user]++;memset(out,0,sizeof(*out));if(user || sleep_count>=16)return ERROR_DEVICE_NOT_CONNECTED;
 if(sleep_count>=10)out->VKCodes[0]='A';return ERROR_SUCCESS;
}
int LiSendMouseMoveEvent(short,short){return 0;}
int LiSendMouseButtonEvent(char,int){return 0;}
int LiSendScrollEvent(signed char){return 0;}
int LiSendKeyboardEvent(short k,char action,char){assert(k=='A');if(action==KEY_ACTION_DOWN)keyboard_down++;else keyboard_up++;return 0;}
void audio_set_volume(int){}
static volatile LONG mouse_mode;
static bool launch_active(){return false;}
static void launch_command(bool,bool){}
/* ACTUAL_INPUT */
static DWORD XInputSetState(int i,XINPUT_VIBRATION *v){rumble_calls++;if(!connected[i])return ERROR_DEVICE_NOT_CONNECTED;motor[i]=v->wLeftMotorSpeed;
 if(inject_stop){inject_stop=false;InterlockedExchange(&rumble_enabled,0);InterlockedIncrement(&rumble_generation);}return ERROR_SUCCESS;}
static void Sleep(DWORD ms){assert(ms>=1&&ms<=4);tick+=ms;sleep_count++;
 if(sleep_count==1)guide=true;if(sleep_count==3)connected[0]=false;
 if(sleep_count==8)samples[1].Gamepad.wButtons=0; // release the initial menu button before new keyboard input
 if(sleep_count==10)guide=false;if(sleep_count==12)connected[0]=true;
 if(sleep_count>=40)stream_active=0;}
int main(){
 input_hotplug h={};pad p={};p.present=true;p.buttons=A_FLAG;p.lx=32767;p.lt=255;
 assert(input_hotplug_sample(&h,&p,true,UINT32_MAX-5));
 memset(&p,0,sizeof(p));assert(input_hotplug_sample(&h,&p,false,UINT32_MAX-4));assert(p.buttons==A_FLAG);
 memset(&p,0,sizeof(p));assert(input_hotplug_sample(&h,&p,false,10));assert(p.lx==32767);
 assert(!input_hotplug_sample(&h,&p,false,16));assert(!p.present&&!p.buttons&&!p.lx&&!p.lt);
 connected[0]=connected[1]=true;samples[0].Gamepad.wButtons=XINPUT_GAMEPAD_A;samples[0].Gamepad.sThumbLX=32767;samples[0].Gamepad.bLeftTrigger=255;
 samples[1].Gamepad.wButtons=XINPUT_GAMEPAD_B;input_stream_start();input_stream_main(NULL);
 assert(arrivals[0]==2 && arrivals[1]==1);bool removal=false,neutral=false;
 for(size_t i=0;i<events.size();i++)if(events[i].port==0 && events[i].mask==2 && !events[i].buttons && !events[i].lt && !events[i].lx)removal=true;
 for(size_t i=0;i<events.size();i++)if(events[i].port==0 && events[i].mask==3 && !events[i].buttons && !events[i].lt && !events[i].lx)neutral=true;
 assert(removal&&neutral && !events.front().buttons);assert(events.back().mask==0);assert(sleep_count==40);assert(keyboard_down==1 && keyboard_up==1 && keyboard_calls[0]<=6 && keyboard_calls[1]<=2);
 int before=rumble_calls;input_rumble(2,100,100);assert(rumble_calls==before); // disconnected slot
 input_rumble(1,100,100);assert(motor[1]==100);connected[1]=false;input_rumble(1,200,200);assert(!quit_requested);
 connected[1]=true;guide=true;before=rumble_calls;input_rumble(1,300,300);assert(before==rumble_calls);guide=false;
 inject_stop=true;input_rumble(1,400,400);assert(motor[1]==0); // callback raced stop
 input_stream_stop();before=rumble_calls;input_rumble(1,500,500);assert(before==rumble_calls&&join_count==1);
 input_session session={};pad pads[4]={};pads[0].present=true;pads[0].buttons=A_FLAG;
 input_session_step(&session,pads,1,0,&stream_settings);pads[0].present=false;
 input_session_step(&session,pads,0,4,&stream_settings);assert(!session.announced[0]&&!session.last[0].buttons&&!session.held_since[0]);
 input_session_release(&session);assert(!session.mask);
 system_ui_state ui={};for(int i=0;i<100;i++){assert(system_ui_change(&ui,true));assert(!system_ui_change(&ui,true));assert(system_ui_change(&ui,false));}
 guide=true;assert(input_menu_pressed()==0&&menu_restores==0);
 guide=false;input_menu_pressed();assert(menu_restores==1);input_menu_pressed();assert(menu_restores==1);
 puts("PASS: actual 4 ms input loop, Guide neutralization, 20 ms debounce/wrap, held button/axis/trigger unplug, masks, two controllers, reconnect arrival, rumble/unplug/stop race and repeated Guide transitions.");
}
