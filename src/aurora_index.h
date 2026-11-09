#pragma once
#include "aurora_shortcuts.h"
#include "wake.h"
#define MOONX_INDEX_MAGIC "MX36IDX1"
struct aurora_index_file {
    char magic[8];uint32_t version,count;
    struct aurora_shortcut_info items[AURORA_SHORTCUT_MAX];
    uint32_t checksum;
};
static uint32_t moonx_index_hash(const struct aurora_index_file *v)
{
    const unsigned char *p=(const unsigned char*)v;uint32_t h=2166136261u;
    for(size_t i=0;i<offsetof(struct aurora_index_file,checksum);i++)h=(h^p[i])*16777619u;
    return h;
}
static int moonx_index_find(const struct aurora_index_file *v,const char *host,int app)
{
    if(!v || !host || app<=0)return -1;
    for(unsigned i=0;i<v->count && i<AURORA_SHORTCUT_MAX;i++)
        if(v->items[i].link.app_id==app && wake_identity(v->items[i].link.host_id,host))return (int)i;
    return -1;
}
static int moonx_index_valid(const struct aurora_index_file *v,size_t n)
{
    if(!v || n!=sizeof(*v) || memcmp(v->magic,MOONX_INDEX_MAGIC,8) ||
       v->version!=1 || v->count>AURORA_SHORTCUT_MAX || v->checksum!=moonx_index_hash(v))return 0;
    for(unsigned i=0;i<v->count;i++) {
        const struct aurora_shortcut_info *r=v->items+i;
        if(!moonx_launch_valid(&r->link,sizeof(r->link)) ||
           !moonx_launch_folder(r->folder,sizeof(r->folder)) || strlen(r->folder)>220)return 0;
        for(unsigned j=0;j<i;j++)if(!strcmp(v->items[j].folder,r->folder) ||
            (v->items[j].link.app_id==r->link.app_id &&
             wake_identity(v->items[j].link.host_id,r->link.host_id)))return 0;
    }
    return 1;
}
static void moonx_index_remove(struct aurora_index_file *v,unsigned i)
{
    if(!v || i>=v->count || v->count>AURORA_SHORTCUT_MAX)return;
    for(unsigned j=i+1;j<v->count;j++)v->items[j-1]=v->items[j];
    memset(&v->items[--v->count],0,sizeof(v->items[0]));
}
