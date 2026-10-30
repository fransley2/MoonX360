/* Four local thumbnails, copied from real, already-decoded Library box art.
 * No network work; loads once and writes only when a new source generation arrives.
 * The payload uses the console's native ARGB format and is validated before use.
 */
#pragma once
#include "config.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define HOME_ART_LIMIT 4
struct home_art_header {char magic[8],host[64];int id,width,height;};
struct home_art_entry {bool pinned;struct home_art_header *blob;uint32_t source_generation,generation;};
struct home_art_cache {bool loaded;unsigned cursor;uint32_t clock;struct home_art_entry entries[HOME_ART_LIMIT];};
static void home_art_load(struct home_art_cache *cache,int width,int height)
{
    if(cache->loaded)return;
    cache->loaded=true;
    for(int i=0;i<HOME_ART_LIMIT;i++) {
        char name[32];snprintf(name,sizeof(name),"home-art-%d.bin",i);
        size_t len=0;struct home_art_header *h=(struct home_art_header*)storage_read(name,&len);
        if(!h)continue;
        size_t expected=sizeof(*h)+(size_t)width*height*4;
        if(len!=expected || memcmp(h->magic,"XHART01",8) || !memchr(h->host,0,sizeof(h->host)) ||
           h->width!=width || h->height!=height || h->id<=0) {free(h);continue;}
        cache->entries[i].blob=h;cache->entries[i].generation=0x80000000u|++cache->clock;
    }
}
static int home_art_slot(struct home_art_cache *cache,const char *host,int id)
{
    for(int i=0;i<HOME_ART_LIMIT;i++) {
        const struct home_art_header *h=cache->entries[i].blob;
        if(h && h->id==id && !strcmp(h->host,host))return i;
    }
    return -1;
}
static const uint32_t *home_art_get(struct home_art_cache *cache,const char *host,int id,uint32_t *generation)
{
    int i=home_art_slot(cache,host,id);if(i<0)return NULL;
    *generation=cache->entries[i].generation;
    return (const uint32_t*)(cache->entries[i].blob+1);
}
static void home_art_remember(struct home_art_cache *cache,const char *host,int id,
                              const uint32_t *pixels,int width,int height,uint32_t generation)
{
    if(!pixels || id<=0)return;
    int slot=home_art_slot(cache,host,id);
    if(slot>=0 && cache->entries[slot].source_generation==generation)return;
    if(slot<0) {
        for(int i=0;i<HOME_ART_LIMIT;i++)if(!cache->entries[i].blob){slot=i;break;}
        if(slot<0)for(int i=0;i<HOME_ART_LIMIT;i++) {
            int next=(int)(cache->cursor++%HOME_ART_LIMIT);
            if(!cache->entries[next].pinned){slot=next;break;}
        }
        if(slot<0)return;
    }
    size_t size=sizeof(struct home_art_header)+(size_t)width*height*4;
    struct home_art_header *h=(struct home_art_header*)malloc(size);if(!h)return;
    memset(h,0,sizeof(*h));memcpy(h->magic,"XHART01",8);
    snprintf(h->host,sizeof(h->host),"%s",host);h->id=id;h->width=width;h->height=height;
    memcpy(h+1,pixels,(size_t)width*height*4);
    free(cache->entries[slot].blob);cache->entries[slot].blob=h;
    cache->entries[slot].pinned=true;cache->entries[slot].source_generation=generation;cache->entries[slot].generation=0x80000000u|++cache->clock;
    char name[32];snprintf(name,sizeof(name),"home-art-%d.bin",slot);
    storage_write(name,h,size); /* Failure keeps the usable RAM thumbnail; no per-frame retries. */
}
