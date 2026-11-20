#pragma once
#include "input_session.h"

/* One atomic word with disjoint fields. */
enum { QUICK_OPEN=1u, QUICK_ROW_SHIFT=1, QUICK_ROW_MASK=15u,
       QUICK_VOLUME_SHIFT=5, QUICK_VOLUME_MASK=127u,
       QUICK_MUTED=1u<<12, QUICK_STATS=1u<<13,
       QUICK_PAGE_SHIFT=14, QUICK_PAGE_MASK=3u,
       QUICK_DETAILS_SHIFT=16, QUICK_DETAILS_MASK=3u, QUICK_STOPPING=1u<<18,
       QUICK_OVERLAY_SHIFT=19, QUICK_OVERLAY_MASK=3u,
       QUICK_DEV_PAGE_SHIFT=21, QUICK_DEV_PAGE_MASK=1u };
enum { QUICK_MAIN = 0, QUICK_CONFIRM = 1, QUICK_DESKTOP_PAGE = 2 };
enum { QUICK_RESUME = 0, QUICK_VOLUME, QUICK_KEYBOARD, QUICK_DESKTOP,
       QUICK_PERFORMANCE, QUICK_REFRESH, QUICK_DISCONNECT,
       QUICK_CLOSE_APP, QUICK_ROW_COUNT };
/* Source compatibility for existing fixtures; there is only one input mode row. */
enum { QUICK_MOUSE=QUICK_DESKTOP };
struct quick_menu {
    bool open,latch,neutral,muted,stats,refresh_request;
    int desktop_request;
    int owner,row,page,details_section,previous,volume,repeat;
    int overlay_mode,dev_page;
    uint32_t held_at,repeated_at;
};
static int quick_overlay_mode(unsigned state)
{
    int mode=(int)((state>>QUICK_OVERLAY_SHIFT)&QUICK_OVERLAY_MASK);
    if(mode==0 && (state&QUICK_STATS))return 1;
    return mode;
}
static int quick_dev_page(unsigned state)
{
    return (int)((state>>QUICK_DEV_PAGE_SHIFT)&QUICK_DEV_PAGE_MASK);
}
static unsigned quick_pack(const struct quick_menu *q)
{
    int om=q->overlay_mode&3;
    if(om==0 && q->stats)om=1;
    return (q->open?QUICK_OPEN:0u)|((unsigned)(q->row&15)<<QUICK_ROW_SHIFT)|
        ((unsigned)(q->volume&127)<<QUICK_VOLUME_SHIFT)|
        (q->muted?QUICK_MUTED:0u)|((om!=0)?QUICK_STATS:0u)|
        ((unsigned)(q->page&3)<<QUICK_PAGE_SHIFT)|
        ((unsigned)(q->details_section&3)<<QUICK_DETAILS_SHIFT)|
        ((unsigned)om<<QUICK_OVERLAY_SHIFT)|
        ((unsigned)(q->dev_page&1)<<QUICK_DEV_PAGE_SHIFT);
}
static bool quick_step(struct quick_menu *q,struct pad pads[MAX_PADS],bool guide,int exit_mask,
                       bool *quit,bool *close_app,int *mouse_action,uint32_t clock)
{
    const int combo=BACK_FLAG|LS_CLK_FLAG;int owner=-1;
    q->refresh_request=false;q->desktop_request=0;
    /* The full exit chord remains authoritative on every page. */
    for(int i=0;i<MAX_PADS;i++)if(pads[i].present && !guide &&
        (pads[i].buttons&exit_mask)==exit_mask){q->open=false;q->neutral=false;q->latch=true;return false;}
    for(int i=0;i<MAX_PADS;i++)if(pads[i].present &&
        (pads[i].buttons&combo)==combo){owner=i;break;}
    if(guide){if(q->open)q->neutral=true;q->open=false;}
    if(q->open && !pads[q->owner].present){q->open=false;q->neutral=true;}
    if(owner>=0 && !q->latch && !guide && !q->neutral){
        q->open=!q->open;q->owner=owner;q->row=QUICK_RESUME;q->page=QUICK_MAIN;
        q->previous=~0;q->repeat=0;if(!q->open)q->neutral=true;
    }
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
        else if(direction && (uint32_t)(clock-q->held_at)>=320 &&
            (uint32_t)(clock-q->repeated_at)>=95){pressed|=direction;q->repeated_at=clock;}

        if(q->page==QUICK_DESKTOP_PAGE){
            if(pressed&UP_FLAG)q->row=(q->row+6)%7;
            if(pressed&DOWN_FLAG)q->row=(q->row+1)%7;
        } else if(q->page==QUICK_MAIN){
            if(pressed&UP_FLAG)q->row=(q->row+QUICK_ROW_COUNT-1)%QUICK_ROW_COUNT;
            if(pressed&DOWN_FLAG)q->row=(q->row+1)%QUICK_ROW_COUNT;
            if(q->row==QUICK_VOLUME && (pressed&(LEFT_FLAG|RIGHT_FLAG))){
                q->volume+=(pressed&LEFT_FLAG)?-5:5;
                if(q->volume<0)q->volume=0;if(q->volume>100)q->volume=100;
            }
            if(q->row==QUICK_PERFORMANCE && (pressed&(LEFT_FLAG|RIGHT_FLAG))){
                int cur=q->overlay_mode&3;
                if(cur==0 && q->stats)cur=1;
                q->overlay_mode=(cur+((pressed&LEFT_FLAG)?3:1))&3;
                q->stats=q->overlay_mode!=0;
            }
        }
        if(pressed&B_FLAG){
            if(q->page!=QUICK_MAIN){if(q->page==QUICK_DESKTOP_PAGE)q->row=QUICK_DESKTOP;q->page=QUICK_MAIN;}
            else {q->open=false;q->neutral=true;}
        } else if(q->page==QUICK_MAIN && q->row==QUICK_DESKTOP && (pressed&X_FLAG)) {
            q->page=QUICK_DESKTOP_PAGE;q->row=0;
        } else if(q->page==QUICK_MAIN && q->row==QUICK_PERFORMANCE && (q->overlay_mode&3)==3 && (pressed&X_FLAG)) {
            q->dev_page^=1;
        } else if(q->page==QUICK_MAIN && (pressed&Y_FLAG)) {
            /* Open Smart Virtual Keyboard directly over streaming (DESK_SHOW_KEYBOARD=3). */
            q->desktop_request=3;q->open=false;q->neutral=true;
        } else if(pressed&A_FLAG) {
            if(q->page==QUICK_DESKTOP_PAGE){q->desktop_request=q->row+1;q->open=false;q->neutral=true;}
            else if(q->page==QUICK_CONFIRM){*quit=true;*close_app=true;q->open=false;q->neutral=true;}
            else if(q->page==QUICK_MAIN) {
                switch(q->row){
                case QUICK_DESKTOP:q->desktop_request=1;q->open=false;q->neutral=true;break;
                case QUICK_KEYBOARD:q->desktop_request=3;q->open=false;q->neutral=true;break;
                case QUICK_RESUME:q->open=false;q->neutral=true;break;
                case QUICK_REFRESH:q->refresh_request=true;break;
                case QUICK_VOLUME:q->muted=!q->muted;break;
                case QUICK_PERFORMANCE: {
                    int cur=q->overlay_mode&3;
                    if(cur==0 && q->stats)cur=1;
                    q->overlay_mode=(cur+1)&3;
                    q->stats=q->overlay_mode!=0;
                    break;
                }
                case QUICK_DISCONNECT:*quit=true;q->open=false;q->neutral=true;break;
                case QUICK_CLOSE_APP:q->page=QUICK_CONFIRM;break;
                }
            }
        }
    }
    if(!q->open)q->repeat=0;
    if(q->neutral && !guide){
        bool released=true;for(int i=0;i<MAX_PADS;i++)if(pads[i].present &&
            (pads[i].buttons || pads[i].lt || pads[i].rt))released=false;
        if(released)q->neutral=false;
    }
    return consumed;
}
