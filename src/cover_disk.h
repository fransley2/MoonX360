#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "ui/cover_cache.h"
#include "cover_pack.h"
/* Worker-owned disposable cache. Fixed names, no directory crawl. Compressed
 * native ARGB thumbnails, raw fallback, one decode allocation on cache reads. */
#define COVER_DISK_SLOTS 32
#define COVER_DISK_BUDGET (4u*1024u*1024u)
#define COVER_DISK_PAYLOAD_MAX (512u*1024u)
struct cover_disk_header {char magic[8];struct ui_cover_key key;uint32_t bytes,checksum,serial,stored,packed,saved_at;};
struct cover_disk_item {struct cover_disk_header header;uint32_t touched,fetched;bool used,seen;};
struct cover_disk_cache {bool loaded,write_failed;uint32_t clock,failed_at;size_t bytes;struct cover_disk_item items[COVER_DISK_SLOTS];};
static bool cover_disk_path(char *out,size_t size,const char *dir,int slot)
{
    if(!dir || !*dir)return false;
#ifdef _XBOX
    const char *separator="\\";
#else
    const char *separator="/";
#endif
    int n=slot<0?snprintf(out,size,"%s%scover-stage.tmp",dir,separator):
        snprintf(out,size,"%s%scover-%02d.bin",dir,separator,slot);
    return n>=0 && (size_t)n<size;
}
static uint32_t cover_disk_hash(const void *data,size_t size)
{
    const unsigned char *p=(const unsigned char*)data;uint32_t hash=2166136261u;
    for(size_t i=0;i<size;i++)hash=(hash^p[i])*16777619u;return hash;
}
static bool cover_disk_header_valid(const struct cover_disk_header *h)
{
    return !memcmp(h->magic,"XCVR003",8) && memchr(h->key.host,0,sizeof(h->key.host)) &&
        h->key.app>0 && h->key.w>0 && h->key.h>0 && h->key.w<=512 && h->key.h<=512 &&
        h->bytes==(size_t)h->key.w*h->key.h*4 && h->bytes<=COVER_DISK_PAYLOAD_MAX &&
        h->stored>0 && h->stored<=h->bytes && h->packed<=1 && (h->packed || h->stored==h->bytes);
}
static void cover_disk_init(struct cover_disk_cache *cache,const char *dir)
{
    if(!dir || !*dir || cache->loaded)return;cache->loaded=true;
    char path[256];
    for(int i=0;i<COVER_DISK_SLOTS;i++) {
        if(!cover_disk_path(path,sizeof(path),dir,i))return;
        FILE *f=fopen(path,"rb");if(!f)continue;
        struct cover_disk_header h;bool ok=fread(&h,1,sizeof(h),f)==sizeof(h) && cover_disk_header_valid(&h);
        if(ok)ok=!fseek(f,0,SEEK_END) && ftell(f)==(long)(sizeof(h)+h.stored);
        fclose(f);
        if(!ok){remove(path);continue;}
        struct cover_disk_item *e=cache->items+i;e->used=true;e->header=h;
        e->touched=h.serial;if((int32_t)(h.serial-cache->clock)>0)cache->clock=h.serial;
        cache->bytes+=sizeof(h)+h.stored;
    }
}
static int cover_disk_find(const struct cover_disk_cache *cache,const struct ui_cover_key *key)
{for(int i=0;i<COVER_DISK_SLOTS;i++)if(cache->items[i].used && ui_cover_same(&cache->items[i].header.key,key))return i;return -1;}
static bool cover_disk_remove(struct cover_disk_cache *cache,const char *dir,int slot)
{
    char path[256];if(!cover_disk_path(path,sizeof(path),dir,slot) || remove(path))return false;
    cache->bytes-=sizeof(struct cover_disk_header)+cache->items[slot].header.stored;
    memset(cache->items+slot,0,sizeof(cache->items[slot]));return true;
}
static uint32_t *cover_disk_read(struct cover_disk_cache *cache,const char *dir,const struct ui_cover_key *key,uint32_t now,uint32_t *fetched)
{
    cover_disk_init(cache,dir);int slot=cover_disk_find(cache,key);if(slot<0)return NULL;
    struct cover_disk_item *e=cache->items+slot;char path[256];
    if(!cover_disk_path(path,sizeof(path),dir,slot))return NULL;
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    struct cover_disk_header h;uint32_t *pixels=NULL;
    if(fread(&h,1,sizeof(h),f)==sizeof(h) && cover_disk_header_valid(&h) && ui_cover_same(&h.key,key)) {
        pixels=(uint32_t*)malloc(h.bytes);
        if(pixels && (!cover_unpack(f,pixels,NULL,h.bytes/4,h.packed!=0) || cover_disk_hash(pixels,h.bytes)!=h.checksum)) {free(pixels);pixels=NULL;}
    }
    fclose(f);
    if(!pixels)return NULL; /* A corrupt/missing file falls through to HTTP. */
    e->touched=++cache->clock;if(!e->seen){
        e->seen=true;time_t epoch=time(NULL);uint32_t age=0;
        if(epoch>0 && h.saved_at && (uint64_t)epoch>=h.saved_at) {
            uint64_t seconds=(uint64_t)epoch-h.saved_at;
            age=seconds>=UI_COVER_VALID_MS/1000?UI_COVER_VALID_MS:(uint32_t)seconds*1000;
        }
        e->fetched=now-age;
    }
    *fetched=e->fetched;return pixels;
}
static bool cover_disk_write(struct cover_disk_cache *cache,const char *dir,const struct ui_cover_key *key,const uint32_t *pixels,uint32_t now)
{
    cover_disk_init(cache,dir);if(!pixels)return false;
    if(cache->write_failed && now-cache->failed_at<30000u)return false;
    cache->write_failed=false;
    struct cover_disk_header h;memset(&h,0,sizeof(h));memcpy(h.magic,"XCVR003",8);h.key=*key;
    if(key->w<=0 || key->h<=0 || key->w>512 || key->h>512)return false;
    h.bytes=(uint32_t)((size_t)key->w*key->h*4);
    h.stored=(uint32_t)cover_pack(pixels,h.bytes/4,NULL);h.packed=h.stored<h.bytes;
    if(!h.packed)h.stored=h.bytes;
    time_t epoch=time(NULL);h.saved_at=epoch>0?(uint32_t)epoch:0;
    if(!cover_disk_header_valid(&h))return false;
    h.checksum=cover_disk_hash(pixels,h.bytes);h.serial=++cache->clock;
    char temp[256],path[256];if(!cover_disk_path(temp,sizeof(temp),dir,-1))return false;
    FILE *f=fopen(temp,"wb");if(!f){cache->write_failed=true;cache->failed_at=now;return false;}
    bool ok=fwrite(&h,1,sizeof(h),f)==sizeof(h);
    if(ok)ok=h.packed?cover_pack(pixels,h.bytes/4,f)==h.stored:fwrite(pixels,1,h.bytes,f)==h.bytes;
    if(fclose(f))ok=false;
    /* Verify with a fixed 512-byte buffer before replacing a usable slot. */
    if(ok) {
        f=fopen(temp,"rb");struct cover_disk_header check;ok=f!=NULL;
        if(f){ok=fread(&check,1,sizeof(check),f)==sizeof(check) && !memcmp(&check,&h,sizeof(h)) && cover_unpack(f,NULL,pixels,h.bytes/4,h.packed!=0);if(fclose(f))ok=false;}
    }
    int slot=cover_disk_find(cache,key);
    if(ok && slot>=0 && !cover_disk_remove(cache,dir,slot))ok=false;
    while(ok) {
        int free_slot=-1,oldest=-1;
        for(int i=0;i<COVER_DISK_SLOTS;i++) {
            if(!cache->items[i].used){if(free_slot<0)free_slot=i;}
            else if(oldest<0 || (int32_t)(cache->items[i].touched-cache->items[oldest].touched)<0)oldest=i;
        }
        if(free_slot>=0 && cache->bytes+sizeof(h)+h.stored<=COVER_DISK_BUDGET){slot=free_slot;break;}
        if(oldest<0 || !cover_disk_remove(cache,dir,oldest)){ok=false;break;}
    }
    if(ok)ok=cover_disk_path(path,sizeof(path),dir,slot) && rename(temp,path)==0;
    if(!ok){remove(temp);cache->write_failed=true;cache->failed_at=now;return false;}
    struct cover_disk_item *e=cache->items+slot;e->used=e->seen=true;e->header=h;e->fetched=now;e->touched=h.serial;
    cache->bytes+=sizeof(h)+h.stored;return true;
}
