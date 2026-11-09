#pragma once
/* Small offline library index. Names are non-sensitive Sunshine app metadata;
 * images remain in the separate bounded artwork cache. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "gamestream.h"
#include "config.h"
#include "aurora_launch.h"
#define APP_CATALOG_LIMIT 64
struct app_catalog_file {
    char magic[8];uint32_t version,count;char host[96];
    struct gs_app apps[APP_CATALOG_LIMIT];uint32_t checksum;
};
static uint32_t app_catalog_hash(const struct app_catalog_file *c)
{
    const unsigned char *p=(const unsigned char*)c;uint32_t h=2166136261u;
    for(size_t i=0;i<offsetof(struct app_catalog_file,checksum);i++)h=(h^p[i])*16777619u;
    return h;
}
static bool app_catalog_path(char out[64],const char *host)
{
    if(!host || strncmp(host,"uuid:",5) || !moonx_launch_text(host,96,1))return false;
    uint32_t a=2166136261u,b=0x9e3779b9u;
    for(const unsigned char *p=(const unsigned char*)host;*p;p++){a=(a^*p)*16777619u;b=(b^*p)*2246822519u;}
    return snprintf(out,64,"app-catalog-%08X%08X.bin",a,b)<64;
}
static bool app_catalog_valid(const struct app_catalog_file *c,size_t n,const char *host)
{
    if(!c || n!=sizeof(*c) || memcmp(c->magic,"MX36APP1",8) || c->version!=1 ||
       c->count>APP_CATALOG_LIMIT || !moonx_launch_text(c->host,sizeof(c->host),1) ||
       strcmp(c->host,host) || c->checksum!=app_catalog_hash(c))return false;
    for(unsigned i=0;i<c->count;i++){
        const struct gs_app *a=c->apps+i;
        if(a->id<=0 || !moonx_launch_text(a->name,sizeof(a->name),1) ||
           !moonx_launch_text(a->uuid,sizeof(a->uuid),0))return false;
        for(unsigned j=0;j<i;j++)if(c->apps[j].id==a->id)return false;
    }
    return true;
}
static bool app_catalog_save(const char *host,const struct gs_app *apps,int count)
{
    char path[64];if(!app_catalog_path(path,host) || !apps || count<0 || count>APP_CATALOG_LIMIT)return false;
    struct app_catalog_file *c=(struct app_catalog_file*)calloc(1,sizeof(*c));if(!c)return false;
    memcpy(c->magic,"MX36APP1",8);c->version=1;c->count=(uint32_t)count;
    snprintf(c->host,sizeof(c->host),"%s",host);memcpy(c->apps,apps,(size_t)count*sizeof(apps[0]));
    c->checksum=app_catalog_hash(c);
    bool valid=app_catalog_valid(c,sizeof(*c),host);
    bool saved=valid && storage_write(path,c,sizeof(*c));free(c);return saved;
}
static int app_catalog_load(const char *host,struct gs_app *apps,int capacity)
{
    char path[64];if(!apps || capacity<APP_CATALOG_LIMIT || !app_catalog_path(path,host))return -1;
    size_t n=0;char *raw=storage_read(path,&n);
    if(!raw || !app_catalog_valid((const struct app_catalog_file*)raw,n,host)){
        free(raw);char backup[72];snprintf(backup,sizeof(backup),"%s.bak",path);
        raw=storage_read(backup,&n);
        if(!raw || !app_catalog_valid((const struct app_catalog_file*)raw,n,host)){free(raw);return -1;}
    }
    const struct app_catalog_file *c=(const struct app_catalog_file*)raw;
    int count=(int)c->count;memcpy(apps,c->apps,(size_t)count*sizeof(apps[0]));free(raw);return count;
}
