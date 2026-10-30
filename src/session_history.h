#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "performance_advisor.h"

#define SESSION_HISTORY_MAX 32
#define SESSION_HISTORY_BYTES 16384
struct session_record {
    unsigned timestamp;
    char host[96], app[64];
    int app_id, width, height, fps, bitrate;
    unsigned shown_milli, decode_avg_us, decode_p99_us, rtt_ms;
    int grade, cause, rtt_valid, tail_valid;
};
struct session_history { int count; struct session_record records[SESSION_HISTORY_MAX]; };
static void history_text(char *dst,size_t cap,const char *src) {
    size_t n=0;if(!src || !*src)src="Unknown";
    while(*src && n+1<cap) {char c=*src++;dst[n++]=(c=='\t'||c=='\r'||c=='\n')?' ':c;}dst[n]=0;
}
static void history_push(struct session_history *h,const struct session_record *r) {
    struct session_record copy=*r;r=&copy;
    /* Keep session summaries, but when full discard an old duplicate before
     * discarding the only occurrence of a different host/game. */
    if(h->count==SESSION_HISTORY_MAX) {
        int remove=SESSION_HISTORY_MAX-1;
        for(int i=SESSION_HISTORY_MAX-1;i>=0;i--) {
            bool duplicate=!strcmp(h->records[i].host,r->host) && h->records[i].app_id==r->app_id;
            for(int j=0;j<i && !duplicate;j++)duplicate=h->records[j].app_id==h->records[i].app_id && !strcmp(h->records[j].host,h->records[i].host);
            if(duplicate){remove=i;break;}
        }
        memmove(h->records+remove,h->records+remove+1,(h->count-remove-1)*sizeof(*r));h->count--;
    }
    int count=h->count;
    memmove(h->records+1,h->records,(size_t)count*sizeof(*r));h->records[0]=*r;
    history_text(h->records[0].host,sizeof(r->host),r->host);
    history_text(h->records[0].app,sizeof(r->app),r->app);
    if(h->count<SESSION_HISTORY_MAX)h->count++;
}
static size_t history_encode(const struct session_history *h,char *out,size_t cap) {
    size_t used=0;int n=snprintf(out,cap,"XENON_SESSIONS 1\n");
    if(n<0 || (size_t)n>=cap)return 0;used=(size_t)n;
    for(int i=0;i<h->count && i<SESSION_HISTORY_MAX;i++) {
        const struct session_record *r=&h->records[i];char host[96],app[64];
        history_text(host,sizeof(host),r->host);history_text(app,sizeof(app),r->app);
        n=snprintf(out+used,cap-used,"%u\t%s\t%d\t%s\t%d\t%d\t%d\t%d\t%u\t%u\t%u\t%u\t%d\t%d\t%d\t%d\n",
            r->timestamp,host,r->app_id,app,r->width,r->height,r->fps,r->bitrate,
            r->shown_milli,r->decode_avg_us,r->decode_p99_us,r->rtt_ms,r->grade,r->cause,r->rtt_valid,r->tail_valid);
        if(n<0 || (size_t)n>=cap-used)return 0;used+=(size_t)n;
    }return used;
}
static void history_decode(struct session_history *h,const char *text) {
    memset(h,0,sizeof(*h));if(!text || strlen(text)>SESSION_HISTORY_BYTES || strncmp(text,"XENON_SESSIONS 1\n",17))return;
    const char *p=text+17;
    while(*p && h->count<SESSION_HISTORY_MAX) {
        char line[512];size_t n=0;bool long_line=false;
        while(*p && *p!='\n') {if(n+1<sizeof(line))line[n++]=*p;else long_line=true;p++;}
        if(*p)p++;line[n]=0;if(long_line)continue;
        struct session_record r;memset(&r,0,sizeof(r));
        int fields=sscanf(line,"%u\t%95[^\t]\t%d\t%63[^\t]\t%d\t%d\t%d\t%d\t%u\t%u\t%u\t%u\t%d\t%d\t%d\t%d",
            &r.timestamp,r.host,&r.app_id,r.app,&r.width,&r.height,&r.fps,&r.bitrate,
            &r.shown_milli,&r.decode_avg_us,&r.decode_p99_us,&r.rtt_ms,&r.grade,&r.cause,&r.rtt_valid,&r.tail_valid);
        if(fields==16 && r.app_id>0 && r.shown_milli<=240000 && r.width>0 && r.width<=1920 && r.height>0 && r.height<=1080 &&
           r.fps>=24 && r.fps<=60 && r.bitrate>=1000 && r.bitrate<=70000 &&
           r.grade>=PERF_UNKNOWN && r.grade<=PERF_UNSTABLE && r.cause>=0 && r.cause<=PERF_GPU &&
           (r.rtt_valid==0||r.rtt_valid==1) && (r.tail_valid==0||r.tail_valid==1))h->records[h->count++]=r;
    }
}


/* Library indices, never unverified history titles/IDs. Continue owns its
 * cover/name/action; the recent strip excludes that same target. */
struct history_home_targets { int resume,count,recent[4]; };
static struct history_home_targets history_home_select(const struct session_history *h,const char *host,
    const int *library_ids,int library_count,int running_id)
{
    struct history_home_targets out;memset(&out,0,sizeof(out));out.resume=-1;
    for(int i=0;i<library_count;i++)if(running_id>0 && library_ids[i]==running_id){out.resume=i;break;}
    for(int i=0;i<h->count;i++) {
        const struct session_record *r=h->records+i;if(strcmp(r->host,host))continue;
        int index=-1;for(int j=0;j<library_count;j++)if(library_ids[j]==r->app_id){index=j;break;}
        if(index<0)continue;
        if(out.resume<0){out.resume=index;continue;}
        if(library_ids[out.resume]==r->app_id)continue;
        bool duplicate=false;for(int j=0;j<out.count;j++)if(library_ids[out.recent[j]]==r->app_id)duplicate=true;
        if(!duplicate && out.count<4)out.recent[out.count++]=index;
    }
    return out;
}
