#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Immutable menu-built atlas; render-time changes select rows and constants. */
#define LAUNCH_TEXT_W 512
#define LAUNCH_TEXT_H 512
#define LAUNCH_STATUS_Y 96
#define LAUNCH_STATUS_H 22
enum launch_status {
    LAUNCH_PREPARING,LAUNCH_CONNECTING,LAUNCH_RESOLVING,LAUNCH_NEGOTIATING,
    LAUNCH_VIDEO,LAUNCH_AUDIO,LAUNCH_CONTROL,LAUNCH_INPUT,LAUNCH_WAITING,
    LAUNCH_READY,LAUNCH_CANCELLING,LAUNCH_RETRYING,LAUNCH_FAILED,LAUNCH_PAIRING,LAUNCH_STATUS_COUNT
};
#define LAUNCH_HINT_Y (LAUNCH_STATUS_Y+LAUNCH_STATUS_COUNT*LAUNCH_STATUS_H)
#define LAUNCH_LOGO_Y (LAUNCH_HINT_Y+48)
static enum launch_status launch_display_status(enum launch_status status,bool first,bool cancelled)
{return cancelled?LAUNCH_CANCELLING:first?LAUNCH_READY:status;}
/* Milestones, not a fabricated time/percentage estimate. Connection setup may
 * initialize audio before RTSP; the owner keeps this bounded progress monotonic. */
static unsigned launch_status_progress(enum launch_status status)
{
    switch(status){
    case LAUNCH_PREPARING:return 1;case LAUNCH_CONNECTING:return 2;
    case LAUNCH_RESOLVING:return 3;case LAUNCH_NEGOTIATING:return 4;
    case LAUNCH_AUDIO:case LAUNCH_VIDEO:case LAUNCH_CONTROL:case LAUNCH_INPUT:return 5;
    case LAUNCH_WAITING:return 6;case LAUNCH_READY:return 7;default:return 0;
    }
}

struct launch_visual {uint32_t started,art_started;float x,y,w,h;bool motion,art,initial_art;};
struct launch_visual_sample {float x,y,w,h,dim,title,art;bool intro;};
static float launch_progress(uint32_t now,uint32_t start,unsigned duration)
{uint32_t elapsed=now-start;return elapsed>=duration?1.0f:(float)elapsed/duration;}
static void launch_visual_begin(struct launch_visual *s,uint32_t now,bool motion,float x,float y,float w,float h,bool art)
{s->started=s->art_started=now;s->motion=motion;s->x=x;s->y=y;s->w=w;s->h=h;s->art=s->initial_art=art;}
static void launch_visual_art_ready(struct launch_visual *s,uint32_t now)
{s->art=true;s->initial_art=false;s->art_started=now;}
static void launch_visual_get(const struct launch_visual *s,uint32_t now,struct launch_visual_sample *out)
{
    float t=s->motion?launch_progress(now,s->started,400):1.0f,e=t*t*(3-2*t);
    out->x=s->x+(514-s->x)*e;out->y=s->y+(122-s->y)*e;
    out->w=s->w+(252-s->w)*e;out->h=s->h+(336-s->h)*e;
    out->dim=e;out->intro=t<1;
    out->title=s->motion?launch_progress(now,s->started,250):1;
    out->art=!s->art?0:!s->motion || s->initial_art?1:launch_progress(now,s->art_started,250);
}

/* Clock supplied by the owner. No queues, waits, allocations or host heuristics. */
struct launch_cover {
    uint32_t first_ms, fade_ms;
    unsigned wait_ms;
    bool active, first, revealing, cancelled, motion;
};
static void launch_cover_begin(struct launch_cover *s,unsigned wait_ms,bool motion)
{
    s->first_ms=s->fade_ms=0;s->wait_ms=wait_ms>10000?10000:wait_ms;
    s->active=true;s->first=s->revealing=s->cancelled=false;s->motion=motion;
}
static void launch_cover_frame(struct launch_cover *s,uint32_t now)
{if(s->active && !s->first){s->first=true;s->first_ms=now;}}
static void launch_cover_cancel(struct launch_cover *s)
{s->cancelled=true; /* Remain opaque until transport and renderer have stopped. */}
static unsigned launch_cover_alpha(struct launch_cover *s,uint32_t now,bool usable,bool reveal)
{
    if(!s->active)return 0;
    if(s->cancelled || !usable || !s->first)return 255;
    if(!s->revealing && (reveal || now-s->first_ms>=s->wait_ms)) {
        s->revealing=true;s->fade_ms=now;
    }
    if(!s->revealing)return 255;
    unsigned elapsed=now-s->fade_ms;
    if(!s->motion || elapsed>=250){s->active=false;return 0;}
    return 255-elapsed*255/250;
}
/* Watchdog is independent of the display delay. A late first frame receives
 * its full configured cover time. Guide use never triggers this watchdog. */
static bool launch_cover_timeout(uint32_t *since,bool *had_video,uint32_t now,
                                 bool covered,bool video,bool guide)
{
    if(!covered || guide || (video && !*had_video))*since=now;
    *had_video=video;
    return covered && !guide && now-*since>=30000;
}

/* XDK: menu prepares a transient backdrop, text and bounded art textures.
 * One render owner continues the same clock and releases resources after handoff. */
#ifdef _XBOX
#ifdef __cplusplus
extern "C" {
#endif
void launch_begin(unsigned wait_ms,bool motion);
void launch_set_wait(unsigned wait_ms);
void launch_retry(void);
bool launch_active(void);
bool launch_has_video(void);
bool launch_cancelled(void);
void launch_command(bool cancel,bool reveal);
void launch_decoded(void);
bool launch_prepare(void);
void launch_present(void); /* menu owner only, before the acknowledged handoff */
void launch_set_status(enum launch_status status); /* atomic callback publication */
bool launch_offer_art(const uint32_t *pixels,int w,int h,uint32_t generation);
void launch_collect(void); /* producer releases its staging after the cover closes */
void launch_end(void);
#ifdef __cplusplus
}
#endif
#endif
