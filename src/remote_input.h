/* Bounded remote input state. Called only by the streaming input owner. */
#pragma once
#include "input_session.h"
struct remote_input {
    bool mouse[MAX_PADS],latch[MAX_PADS],left[MAX_PADS],right[MAX_PADS];
    bool suppress[MAX_PADS]; /* consumed controls must become neutral before game use */
    uint32_t scroll_at[MAX_PADS];
    int speed,acceleration,scroll_ms; /* zero keeps the previous defaults */
    int remainder_x[MAX_PADS],remainder_y[MAX_PADS];
    bool keys[256],blocked[256];
    bool keyboard_ready;
};
static void remote_mouse_release(struct remote_input *r,int i)
{
    if(r->left[i])LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE,BUTTON_LEFT);
    if(r->right[i])LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE,BUTTON_RIGHT);
    r->left[i]=r->right[i]=false;
}
static void remote_keys(struct remote_input *r,const bool down[256],bool enabled)
{
    for(int k=1;k<256;k++) {
        /* Keys already held at stream start or Guide close must be released first. */
        if(!enabled || !r->keyboard_ready)r->blocked[k]=down[k];
        if(!down[k])r->blocked[k]=false;
    }
    bool allowed[256];for(int k=0;k<256;k++)allowed[k]=enabled && down[k] && !r->blocked[k];
    char modifiers=(allowed[0xa0]||allowed[0xa1]?MODIFIER_SHIFT:0)|
        (allowed[0xa2]||allowed[0xa3]?MODIFIER_CTRL:0)|(allowed[0xa4]||allowed[0xa5]?MODIFIER_ALT:0)|
        (allowed[0x5b]||allowed[0x5c]?MODIFIER_META:0);
    for(int k=1;k<256;k++) {
        bool next=allowed[k];
        if(next!=r->keys[k]) {
            char extended=(k>=0x21 && k<=0x2e)||k==0xa3||k==0xa5||k==0x5b||k==0x5c?MODIFIER_EXTENDED:0;
            LiSendKeyboardEvent((short)k,next?KEY_ACTION_DOWN:KEY_ACTION_UP,modifiers|extended);
            r->keys[k]=next;
        }
    }
    r->keyboard_ready=enabled;
}
static void remote_release(struct remote_input *r)
{
    bool empty[256]={false};remote_keys(r,empty,false);
    for(int i=0;i<MAX_PADS;i++){remote_mouse_release(r,i);r->mouse[i]=false;}
}
static void remote_pad(struct remote_input *r,struct pad *p,int i,uint32_t now,bool guide,int exit_combo)
{
    /* Back + R3 conflicts with neither exit combo nor the system Guide. */
    const int shortcut=BACK_FLAG|RS_CLK_FLAG;
    bool toggle=p->present && (p->buttons&shortcut)==shortcut;
    bool exiting=(p->buttons&exit_combo)==exit_combo;
    if(guide || !p->present) {
        if(r->mouse[i])r->suppress[i]=true;
        remote_mouse_release(r,i);r->mouse[i]=false;r->latch[i]=true;
        if(r->suppress[i]){bool present=p->present;memset(p,0,sizeof(*p));p->present=present;}
        return;
    }
    if(r->suppress[i]) {
        if(!p->buttons && !p->lt && !p->rt && p->lx> -8000 && p->lx<8000 &&
            p->ly> -8000 && p->ly<8000 && p->rx> -8000 && p->rx<8000 && p->ry> -8000 && p->ry<8000) {
            r->suppress[i]=false;r->latch[i]=false;
        }
        bool present=p->present;memset(p,0,sizeof(*p));p->present=present;return;
    }
    if(toggle && !r->latch[i] && !exiting) {
        remote_mouse_release(r,i);
        /* Only one mouse owner; other controllers retain normal game input. */
        bool active=r->mouse[i];
        for(int j=0;j<MAX_PADS;j++){
            if(r->mouse[j])r->suppress[j]=true;
            remote_mouse_release(r,j);r->mouse[j]=false;
        }
        r->mouse[i]=!active;
        if(!active)r->suppress[i]=false;
    }
    r->latch[i]=toggle;
    if(exiting)return; /* Let the existing two-second exit policy consume it. */
    if(r->mouse[i] && !toggle) {
        int x_axis=input_axis(p->lx,18),y_axis=-input_axis(p->ly,18),speed=r->speed?r->speed:100;
        if(r->acceleration && (x_axis>20000 || x_axis< -20000 || y_axis>20000 || y_axis< -20000))speed=speed*3/2;
        int dx=x_axis*speed+r->remainder_x[i],dy=y_axis*speed+r->remainder_y[i];
        short x=(short)(dx/409600),y=(short)(dy/409600);r->remainder_x[i]=dx%409600;r->remainder_y[i]=dy%409600;
        if(!x_axis)r->remainder_x[i]=0;if(!y_axis)r->remainder_y[i]=0;
        if(x||y)LiSendMouseMoveEvent(x,y);
        bool left=(p->buttons&A_FLAG)!=0,right=(p->buttons&B_FLAG)!=0;
        if(left!=r->left[i])LiSendMouseButtonEvent(left?BUTTON_ACTION_PRESS:BUTTON_ACTION_RELEASE,BUTTON_LEFT);
        if(right!=r->right[i])LiSendMouseButtonEvent(right?BUTTON_ACTION_PRESS:BUTTON_ACTION_RELEASE,BUTTON_RIGHT);
        r->left[i]=left;r->right[i]=right;
        if((p->ry>16000 || p->ry< -16000) && (uint32_t)(now-r->scroll_at[i])>=(unsigned)(r->scroll_ms?r->scroll_ms:100)) {
            LiSendScrollEvent(p->ry>0?1:-1);r->scroll_at[i]=now;
        }
    }
    if(toggle || r->mouse[i]) {
        bool present=p->present;memset(p,0,sizeof(*p));p->present=present;
    }
}
