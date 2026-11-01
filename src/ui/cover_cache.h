#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define UI_COVER_LIMIT 12
#define UI_COVER_VALID_MS 86400000u /* host appasset exposes no revision: daily revalidation */
#define UI_COVER_RETRY_MS 10000u
#define UI_COVER_FAILURE_LIMIT 128
struct ui_cover_key {char host[96];int app,w,h;};
/* Retry identity outlives decoded-pixel eviction. Never displace a live cooldown. */
struct ui_cover_failure {struct ui_cover_key key;uint32_t attempted;bool used;};
struct ui_cover_entry {
    struct ui_cover_key key;
    uint32_t *pixels,generation,updated,attempted,lru;
    bool used,valid,attempt,refresh,inflight,pinned,disk_checked;
};
struct ui_cover_cache {struct ui_cover_entry items[UI_COVER_LIMIT];uint32_t clock,generation;struct ui_cover_failure failures[UI_COVER_FAILURE_LIMIT];};
static bool ui_cover_same(const struct ui_cover_key *a,const struct ui_cover_key *b)
{return a->app==b->app && a->w==b->w && a->h==b->h && !strcmp(a->host,b->host);}
static int ui_cover_find(const struct ui_cover_cache *c,const struct ui_cover_key *key)
{for(int i=0;i<UI_COVER_LIMIT;i++)if(c->items[i].used && ui_cover_same(&c->items[i].key,key))return i;return -1;}
static bool ui_cover_retry(const struct ui_cover_cache *c,const struct ui_cover_key *key,uint32_t now)
{
    bool space=false;
    for(int i=0;i<UI_COVER_FAILURE_LIMIT;i++) {
        const struct ui_cover_failure *f=c->failures+i;
        if(f->used && ui_cover_same(&f->key,key))return now-f->attempted>=UI_COVER_RETRY_MS;
        if(!f->used || now-f->attempted>=UI_COVER_RETRY_MS)space=true;
    }
    return space; /* Saturation delays new HTTP rather than forgetting a failure. */
}
static void ui_cover_attempt(struct ui_cover_cache *c,const struct ui_cover_key *key,uint32_t now,bool success)
{
    int slot=-1;
    for(int i=0;i<UI_COVER_FAILURE_LIMIT;i++) {
        struct ui_cover_failure *f=c->failures+i;
        if(f->used && ui_cover_same(&f->key,key)){slot=i;break;}
        if(slot<0 && (!f->used || now-f->attempted>=UI_COVER_RETRY_MS))slot=i;
    }
    if(slot<0)return;
    struct ui_cover_failure *f=c->failures+slot;
    if(success){if(f->used && ui_cover_same(&f->key,key))f->used=false;return;}
    f->used=true;f->key=*key;f->attempted=now;
}
static int ui_cover_slot(struct ui_cover_cache *c,const struct ui_cover_key *key,bool evict)
{
    int i=ui_cover_find(c,key);if(i>=0)return i;
    int oldest=-1;
    for(i=0;i<UI_COVER_LIMIT;i++) {
        if(!c->items[i].used){oldest=i;break;}
        if(evict && !c->items[i].pinned && !c->items[i].inflight &&
           (oldest<0 || (int32_t)(c->items[i].lru-c->items[oldest].lru)<0))oldest=i;
    }
    if(oldest<0)return -1;
    struct ui_cover_entry *e=c->items+oldest;free(e->pixels);memset(e,0,sizeof(*e));e->key=*key;e->used=true;e->refresh=true;e->lru=++c->clock;
    return oldest;
}
static bool ui_cover_due(const struct ui_cover_entry *e,uint32_t now)
{
    if(e->inflight || (e->attempt && now-e->attempted<UI_COVER_RETRY_MS))return false;
    return !e->valid || e->refresh || now-e->updated>=UI_COVER_VALID_MS;
}
static bool ui_cover_publish(struct ui_cover_cache *c,int slot,const struct ui_cover_key *key,uint32_t *pixels,uint32_t now)
{
    if(slot<0 || slot>=UI_COVER_LIMIT || !c->items[slot].used || !ui_cover_same(&c->items[slot].key,key)){free(pixels);return false;}
    struct ui_cover_entry *e=c->items+slot;e->inflight=false;e->attempt=true;e->attempted=now;
    if(!pixels)return false; /* Stale pixels survive network/decode failure. */
    bool changed=!e->pixels || memcmp(e->pixels,pixels,(size_t)key->w*key->h*4)!=0;
    if(changed){free(e->pixels);e->pixels=pixels;e->generation=++c->generation;if(!e->generation)e->generation=++c->generation;}
    else free(pixels);
    e->attempt=false;e->updated=now;e->valid=true;e->refresh=false;e->lru=++c->clock;return changed;
}
static void ui_cover_invalidate(struct ui_cover_cache *c,const char *host)
{for(int i=0;i<UI_COVER_LIMIT;i++)if(c->items[i].used && !strcmp(c->items[i].key.host,host))c->items[i].refresh=true;}
static void ui_cover_dispose(struct ui_cover_cache *c)
{for(int i=0;i<UI_COVER_LIMIT;i++)free(c->items[i].pixels);memset(c,0,sizeof(*c));}
/* At most two visible rows plus a small prefetch margin. No entire-library load. */
static int ui_cover_window(int selected,int count,int columns,int *first) {
    int row=selected/columns;
    *first=row*columns;
    int n=count-*first;if(n>UI_COVER_LIMIT)n=UI_COVER_LIMIT;
    return n>0?n:0;
}

static int ui_cover_visible_window(int selected,int count,int columns,int rows,int *first)
{
    int row=selected/columns-rows+1;if(row<0)row=0;*first=row*columns;
    int n=count-*first,maximum=columns*rows;if(maximum>UI_COVER_LIMIT)maximum=UI_COVER_LIMIT;
    if(n>maximum)n=maximum;return n>0?n:0;
}
