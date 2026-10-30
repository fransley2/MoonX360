#pragma once
#include <string.h>
#include <stdio.h>
#include "session_history.h"
#define LIBRARY_FAVORITES_MAX 32
struct library_favorite { char host[96];int id; };
struct library_favorites { int count;struct library_favorite entries[LIBRARY_FAVORITES_MAX]; };
static bool library_favorite_has(const struct library_favorites *f,const char *host,int id) {
    for(int i=0;i<f->count;i++)if(f->entries[i].id==id && !strcmp(f->entries[i].host,host))return true;
    return false;
}
static bool library_favorite_toggle(struct library_favorites *f,const char *host,int id) {
    for(int i=0;i<f->count;i++)if(f->entries[i].id==id && !strcmp(f->entries[i].host,host)) {
        memmove(f->entries+i,f->entries+i+1,(size_t)(--f->count-i)*sizeof(f->entries[0]));return true;
    }
    if(f->count==LIBRARY_FAVORITES_MAX)return false;
    history_text(f->entries[f->count].host,96,host);f->entries[f->count++].id=id;return true;
}
static size_t library_favorites_encode(const struct library_favorites *f,char *out,size_t cap) {
    int n=snprintf(out,cap,"XENON_FAVORITES 1\n");if(n<0||(size_t)n>=cap)return 0;size_t used=n;
    for(int i=0;i<f->count;i++) {n=snprintf(out+used,cap-used,"%s\t%d\n",f->entries[i].host,f->entries[i].id);
        if(n<0||(size_t)n>=cap-used)return 0;used+=n;}return used;
}
static void library_favorites_decode(struct library_favorites *f,const char *text) {
    memset(f,0,sizeof(*f));if(!text||strncmp(text,"XENON_FAVORITES 1\n",18))return;
    const char *p=text+18;while(*p && f->count<LIBRARY_FAVORITES_MAX) {
        char host[96],line[128];int id;const char *end=strchr(p,'\n');if(!end)break;
        size_t length=(size_t)(end-p);
        if(length<sizeof(line)) {memcpy(line,p,length);line[length]=0;}else line[0]=0;
        if(length<sizeof(line) && sscanf(line,"%95[^\t]\t%d",host,&id)==2 && id>0 &&
           !library_favorite_has(f,host,id)) {history_text(f->entries[f->count].host,96,host);f->entries[f->count++].id=id;}
        p=end+1;
    }
}
static bool library_desktop(const char *name) {
    const char *desktop="desktop";if(!name)return false;
    while(*name && *desktop) {char c=*name++;if(c>='A'&&c<='Z')c+='a'-'A';if(c!=*desktop++)return false;}
    return !*name && !*desktop;
}
