/* Single input-thread owner; no waits, allocations, UI calls or host helpers. */
#pragma once
#include "remote_input.h"
#include "desktop_options.h"
#include "desktop_keyboard_layout.h"
#include <stdlib.h>
enum { DESK_TOGGLE=1,DESK_SHOW_TOOLBAR,DESK_SHOW_KEYBOARD,DESK_OSK,DESK_TOUCH,DESK_SWITCH,DESK_EXPLORER };
struct desktop_input {
    struct desktop_options options;
    bool enabled,neutral,back_used,mouse_latch,overlay_latch;
    bool held[3];
    int owner,previous,view,selected,keyboard_page;
    bool caps,text_error;
    uint32_t clock;
    int move_x,move_y,wheel_x,wheel_y;
};
static const int desktop_buttons[DESKTOP_BINDINGS]={A_FLAG,B_FLAG,X_FLAG,Y_FLAG,UP_FLAG,DOWN_FLAG,
 LEFT_FLAG,RIGHT_FLAG,LB_FLAG,RB_FLAG,PLAY_FLAG,BACK_FLAG};
/* Explicit modifiers go down first, key up first, then modifiers in reverse.
 * Send matching modifier masks even on releases; never retain a modifier. */
static void desktop_chord(int key,int mods) {
    if(mods&MODIFIER_META)LiSendKeyboardEvent(0x5b,KEY_ACTION_DOWN,MODIFIER_META|MODIFIER_EXTENDED);
    if(mods&MODIFIER_CTRL)LiSendKeyboardEvent(0xa2,KEY_ACTION_DOWN,(char)(mods&(MODIFIER_META|MODIFIER_CTRL)));
    if(mods&MODIFIER_ALT)LiSendKeyboardEvent(0xa4,KEY_ACTION_DOWN,(char)mods);
    bool extended=(key>=0x21 && key<=0x2e) || key==0x5b;
    LiSendKeyboardEvent((short)key,KEY_ACTION_DOWN,(char)(mods|(extended?MODIFIER_EXTENDED:0)));
    LiSendKeyboardEvent((short)key,KEY_ACTION_UP,(char)(mods|(extended?MODIFIER_EXTENDED:0)));
    if(mods&MODIFIER_ALT)LiSendKeyboardEvent(0xa4,KEY_ACTION_UP,(char)(mods&~MODIFIER_ALT));
    if(mods&MODIFIER_CTRL)LiSendKeyboardEvent(0xa2,KEY_ACTION_UP,(char)(mods&MODIFIER_META));
    if(mods&MODIFIER_META)LiSendKeyboardEvent(0x5b,KEY_ACTION_UP,MODIFIER_EXTENDED);
}
static void desktop_release(struct desktop_input *d) {
    const int buttons[]={BUTTON_LEFT,BUTTON_RIGHT,BUTTON_MIDDLE};
    for(int i=0;i<3;i++){if(d->held[i])LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE,buttons[i]);d->held[i]=false;}
    d->move_x=d->move_y=d->wheel_x=d->wheel_y=0;
}
static void desktop_suspend(struct desktop_input *d) {
    desktop_release(d);d->neutral=true;d->view=0;d->previous=0;d->back_used=true;
}
static bool desktop_neutral(const struct pad *p) {
    return !p->buttons && p->lt<20 && p->rt<20 && abs(p->lx)<8000 && abs(p->ly)<8000 && abs(p->rx)<8000 && abs(p->ry)<8000;
}
static void desktop_keyboard(struct desktop_input *d) {
    desktop_release(d);d->neutral=true;d->view=0;d->selected=0;d->text_error=false;
    if(d->options.keyboard==1)desktop_chord('O',MODIFIER_META|MODIFIER_CTRL);
    /* Optional taskbar integration: user enables the Windows touch keyboard icon.
     * Win+B focuses notification area; no undocumented TabTip activation. */
    else if(d->options.keyboard==2)desktop_chord('B',MODIFIER_META);
    else d->view=DESK_KEYBOARD;
}
/* All strings use explicit UTF-8 bytes so the XDK compiler's codepage is irrelevant. */
static unsigned desktop_pack(const struct desktop_input *d) {
    return (unsigned)d->view|((unsigned)d->selected<<2)|((unsigned)d->keyboard_page<<8)|
        (d->caps?1u<<10:0)|(d->enabled?1u<<11:0)|(d->text_error?1u<<12:0);
}
static void desktop_command(struct desktop_input *d,int command) {
    desktop_release(d);d->neutral=true;
    switch(command) {
    case DESK_TOGGLE:d->enabled=!d->enabled;d->view=0;break;
    case DESK_SHOW_TOOLBAR:d->enabled=true;d->view=DESK_TOOLBAR;d->selected=0;break;
    case DESK_SHOW_KEYBOARD:d->enabled=true;desktop_keyboard(d);break;
    case DESK_OSK:d->view=0;desktop_chord('O',MODIFIER_META|MODIFIER_CTRL);break;
    case DESK_TOUCH:d->view=0;desktop_chord('B',MODIFIER_META);break;
    case DESK_SWITCH:d->view=0;desktop_chord(0x09,MODIFIER_ALT);break;
    case DESK_EXPLORER:d->view=0;desktop_chord('E',MODIFIER_META);break;
    }
}
static void desktop_toolbar_action(struct desktop_input *d) {
    switch(d->selected) {
    case 0:desktop_chord(0x25,MODIFIER_ALT);break;
    case 1:desktop_chord(0x27,MODIFIER_ALT);break;
    case 2:desktop_chord(0x74,0);break; /* F5, no video refresh */
    case 3:desktop_chord('L',MODIFIER_CTRL);desktop_keyboard(d);return;
    case 4:desktop_keyboard(d);return;
    case 5:desktop_chord(0x24,MODIFIER_ALT);break;
    case 6:desktop_chord(0x5b,0);break;
    case 7:desktop_chord(0x09,MODIFIER_CTRL);break;
    case 8:desktop_chord(0x7a,0);break;
    }
    d->view=0;d->neutral=true;
}
static void desktop_action(struct desktop_input *d,int action) {
    if(action>=0 && action<3){const int buttons[]={BUTTON_LEFT,BUTTON_RIGHT,BUTTON_MIDDLE};LiSendMouseButtonEvent(BUTTON_ACTION_PRESS,buttons[action]);LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE,buttons[action]);return;}
    switch(action) {
    case DA_KEYBOARD:desktop_keyboard(d);break;
    case DA_UP:desktop_chord(0x26,0);break;case DA_DOWN:desktop_chord(0x28,0);break;
    case DA_LEFT:desktop_chord(0x25,0);break;case DA_RIGHT:desktop_chord(0x27,0);break;
    case DA_BACK:desktop_chord(0x25,MODIFIER_ALT);break;case DA_FORWARD:desktop_chord(0x27,MODIFIER_ALT);break;
    case DA_ENTER:desktop_chord(0x0d,0);break;case DA_ESCAPE:desktop_chord(0x1b,0);break;
    }
}
static int desktop_motion(int axis,int speed,unsigned elapsed,int *remainder) {
    /* 800 pixels/sec at full scale and 100%. int bounds: 32768*400*20. */
    if(!axis){*remainder=0;return 0;}
    int amount=axis*speed*(int)elapsed+*remainder;
    int result=amount/4096000;*remainder=amount%4096000;return result;
}
static bool desktop_step(struct desktop_input *d,struct pad *p,uint32_t now,int exit_mask) {
    unsigned elapsed=now-d->clock;d->clock=now;if(elapsed>20)elapsed=20;
    if(!d->enabled)return false;
    if(!p->present){desktop_suspend(d);return true;}
    int buttons=p->buttons,pressed=buttons&~d->previous,released=d->previous&~buttons;
    d->previous=buttons;
    if(d->neutral){if(desktop_neutral(p)){d->neutral=false;d->previous=0;d->back_used=false;}return true;}
    if((buttons&exit_mask)==exit_mask){desktop_suspend(d);return true;}
    /* Defer ALL Back-bound actions until release. No click/key can leak from a chord. */
    if(buttons&BACK_FLAG){
        if(buttons&~BACK_FLAG)d->back_used=true;
        desktop_release(d);return true;
    }
    if(released&BACK_FLAG){
        if(!d->back_used){if(d->view)d->view=0;else desktop_action(d,d->options.mapping[11]);}
        d->back_used=false;d->neutral=true;return true;
    }
    if(d->view){
        int count=d->view==DESK_KEYBOARD?40:9,cols=d->view==DESK_KEYBOARD?10:3;
        if(pressed&LEFT_FLAG)d->selected=(d->selected+count-1)%count;
        if(pressed&RIGHT_FLAG)d->selected=(d->selected+1)%count;
        if(pressed&UP_FLAG)d->selected=(d->selected+count-cols)%count;
        if(pressed&DOWN_FLAG)d->selected=(d->selected+cols)%count;
        if(pressed&B_FLAG){d->view=0;d->neutral=true;return true;}
        if(pressed&A_FLAG){
            if(d->view==DESK_TOOLBAR)desktop_toolbar_action(d);
            else {
                int key=d->selected;
                if(key<30 || key==30){const char *text=key==30?" ":desktop_key_label(d->keyboard_page,d->caps,key);d->text_error=LiSendUtf8TextEvent(text,(unsigned)strlen(text))!=0;}
                else if(key==31)desktop_chord(8,0);
                else if(key==32){desktop_chord(13,0);d->view=0;d->neutral=true;}
                else if(key==33)d->caps=!d->caps;
                else if(key==34)d->keyboard_page=(d->keyboard_page+1)%3;
                else if(key==35)desktop_command(d,DESK_OSK);
                else if(key==36)desktop_command(d,DESK_TOUCH);
                else if(key==37)desktop_chord(9,0);
                else if(key==38)desktop_chord(0x5b,0);
                else {d->view=0;d->neutral=true;}
            }
        }
        return true;
    }
    /* R3 alone exposes toolbar. Back+R3 is handled before this policy. */
    if(pressed&RS_CLK_FLAG){desktop_release(d);d->view=DESK_TOOLBAR;d->selected=0;d->neutral=true;return true;}
    bool clicks[3]={false};
    for(int i=0;i<DESKTOP_BINDINGS-1;i++) {
        int action=d->options.mapping[i];
        if(action<3){if(buttons&desktop_buttons[i])clicks[action]=true;}
        else if(pressed&desktop_buttons[i])desktop_action(d,action);
        if(d->view || d->neutral)return true;
    }
    const int mouse_buttons[]={BUTTON_LEFT,BUTTON_RIGHT,BUTTON_MIDDLE};
    for(int i=0;i<3;i++){if(clicks[i]!=d->held[i])LiSendMouseButtonEvent(clicks[i]?BUTTON_ACTION_PRESS:BUTTON_ACTION_RELEASE,mouse_buttons[i]);d->held[i]=clicks[i];}
    int x=input_axis(p->lx,d->options.deadzone),y=-input_axis(p->ly,d->options.deadzone);
    int speed=d->options.speed;
    if(d->options.acceleration){int magnitude=abs(x)>abs(y)?abs(x):abs(y);speed+=speed*magnitude/32768;}
    if(p->lt>30)speed=speed*d->options.precision/100;
    else if(p->rt>30)speed=speed*(255+p->rt)/255;
    int dx=desktop_motion(x,speed,elapsed,&d->move_x),dy=desktop_motion(y,speed,elapsed,&d->move_y);
    if(dx || dy)LiSendMouseMoveEvent((short)dx,(short)dy);
    int sx=desktop_motion(input_axis(p->rx,d->options.deadzone),d->options.scroll_speed,elapsed,&d->wheel_x);
    int sy=desktop_motion(input_axis(p->ry,d->options.deadzone),d->options.scroll_speed,elapsed,&d->wheel_y);
    if(sx)LiSendHighResHScrollEvent((short)sx);if(sy)LiSendHighResScrollEvent((short)sy);
    return true;
}
