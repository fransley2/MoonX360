#pragma once
#include "input_session.h"
/* Single input-owner state, published as a packed atomic snapshot to the renderer. */
struct quick_menu {bool open,latch,neutral;int owner,row,previous,volume;bool muted,stats;int repeat;uint32_t held_at,repeated_at;};
static unsigned quick_pack(const struct quick_menu *q)
{return (q->open?1u:0u)|((unsigned)q->row<<1)|((unsigned)q->volume<<4)|(q->muted?2048u:0u)|(q->stats?4096u:0u);}
static bool quick_step(struct quick_menu *q,struct pad pads[MAX_PADS],bool guide,int exit_mask,bool *quit,int *mouse_action,uint32_t clock)
{
    const int combo=BACK_FLAG|LS_CLK_FLAG;int owner=-1;
    for(int i=0;i<MAX_PADS;i++)if(pads[i].present && !guide && (pads[i].buttons&exit_mask)==exit_mask){q->open=false;q->neutral=false;q->latch=true;return false;}
    for(int i=0;i<MAX_PADS;i++)if(pads[i].present && (pads[i].buttons&combo)==combo && (pads[i].buttons&exit_mask)!=exit_mask){owner=i;break;}
    if(guide){if(q->open)q->neutral=true;q->open=false;}
    if(q->open && !pads[q->owner].present){q->open=false;q->neutral=true;}
    if(owner>=0 && !q->latch && !guide && !q->neutral){q->open=!q->open;q->owner=owner;q->row=0;q->previous=~0;q->repeat=0;if(!q->open)q->neutral=true;}
    q->latch=owner>=0;
    bool consumed=q->open || q->neutral || owner>=0;
    if(q->open && !guide) {
        int now=pads[q->owner].buttons;
        if(pads[q->owner].ly>20000)now|=UP_FLAG;if(pads[q->owner].ly<-20000)now|=DOWN_FLAG;
        if(pads[q->owner].lx>20000)now|=RIGHT_FLAG;if(pads[q->owner].lx<-20000)now|=LEFT_FLAG;
        int pressed=now&~q->previous;q->previous=now;
        int direction=now&(UP_FLAG|DOWN_FLAG|LEFT_FLAG|RIGHT_FLAG);
        if(direction&(UP_FLAG|DOWN_FLAG))direction&=(UP_FLAG|DOWN_FLAG);
        if(direction==(UP_FLAG|DOWN_FLAG) || direction==(LEFT_FLAG|RIGHT_FLAG))direction=0;
        if(direction!=q->repeat){q->repeat=direction;q->held_at=q->repeated_at=clock;}
        else if(direction && (uint32_t)(clock-q->held_at)>=320 && (uint32_t)(clock-q->repeated_at)>=95){pressed|=direction;q->repeated_at=clock;}

        if(pressed&UP_FLAG)q->row=(q->row+5)%6;if(pressed&DOWN_FLAG)q->row=(q->row+1)%6;
        if(q->row==1 && (pressed&(LEFT_FLAG|RIGHT_FLAG))){q->volume+=(pressed&LEFT_FLAG)?-5:5;if(q->volume<0)q->volume=0;if(q->volume>100)q->volume=100;}
        if(pressed&B_FLAG){q->open=false;q->neutral=true;}
        if(pressed&A_FLAG) {
            if(q->row==0){q->open=false;q->neutral=true;}
            if(q->row==1)q->muted=!q->muted;
            if(q->row==2){*mouse_action=q->owner;q->open=false;q->neutral=true;}
            if(q->row==3)q->stats=!q->stats;
            if(q->row==5)*quit=true;
        }
    }
    if(!q->open)q->repeat=0;
    if(q->neutral && !guide) {
        bool released=true;for(int i=0;i<MAX_PADS;i++)if(pads[i].present && (pads[i].buttons || pads[i].lt || pads[i].rt))released=false;
        if(released)q->neutral=false;
    }
    return consumed;
}
