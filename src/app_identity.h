#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "wake.h"
#define APP_IDENTITY_MAX 64
#define APP_IDENTITY_BYTES 8192
struct app_identity_entry {int id;char uuid[80];};
struct app_identity_map {int count;struct app_identity_entry items[APP_IDENTITY_MAX];};
static void app_identity_path(char out[64],const char *host)
{
    uint32_t a=2166136261u,b=0x9e3779b9u;
    for(const unsigned char *p=(const unsigned char*)host;*p;p++){a=(a^*p)*16777619u;b=(b^*p)*2246822519u;}
    snprintf(out,64,"app-ids-%08x%08x.tsv",a,b);
}
static bool app_identity_decode(struct app_identity_map *m,const char *host,const char *text)
{
    memset(m,0,sizeof(*m));if(!text || strlen(text)>APP_IDENTITY_BYTES)return false;
    char prefix[128];snprintf(prefix,sizeof(prefix),"MOONX_APP_IDS 1\t%s\n",host);
    if(strncmp(text,prefix,strlen(prefix)))return false;
    const char *p=text+strlen(prefix);
    while(*p) {
        const char *end=strchr(p,'\n'),*tab=strchr(p,'\t');if(!end || !tab || tab>=end || m->count>=APP_IDENTITY_MAX)return false;
        char number[16];size_t n=tab-p;if(!n || n>=sizeof(number))return false;memcpy(number,p,n);number[n]=0;
        char *last;long id=strtol(number,&last,10);if(*last || id<=0 || id>2147483647)return false;
        struct app_identity_entry *e=m->items+m->count;e->id=(int)id;n=end-tab-1;
        if(!n || n>=sizeof(e->uuid))return false;memcpy(e->uuid,tab+1,n);e->uuid[n]=0;
        if(!wake_identity_valid(e->uuid))return false;
        for(int i=0;i<m->count;i++)if(m->items[i].id==e->id || wake_identity(m->items[i].uuid,e->uuid))return false;
        m->count++;p=end+1;
    }return true;
}
static int app_identity_resolve(const struct app_identity_map *before,const struct app_identity_map *after,int id)
{
    for(int i=0;i<before->count;i++)if(before->items[i].id==id)
        for(int j=0;j<after->count;j++)if(wake_identity(before->items[i].uuid,after->items[j].uuid))return after->items[j].id;
    return id;
}
static size_t app_identity_encode(const struct app_identity_map *m,const char *host,char *out,size_t cap)
{
    int n=snprintf(out,cap,"MOONX_APP_IDS 1\t%s\n",host);if(n<0 || (size_t)n>=cap)return 0;
    size_t used=(size_t)n;
    for(int i=0;i<m->count;i++) {
        n=snprintf(out+used,cap-used,"%d\t%s\n",m->items[i].id,m->items[i].uuid);
        if(n<0 || (size_t)n>=cap-used)return 0;used+=(size_t)n;
    }return used;
}
