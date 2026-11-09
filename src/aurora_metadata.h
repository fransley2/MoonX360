#pragma once
/* Optional per-shortcut Aurora display metadata. The 468-byte Direct Launch
 * link and LaunchData are intentionally unchanged. */
#include "aurora_launch.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#pragma pack(push,1)
struct moonx_aurora_metadata {
    char magic[8];
    uint32_t version;
    char host_id[80];
    int32_t app_id;
    char display_title[128];
    char description[256];
    char source[16]; /* none, manual, or sunshine */
    uint32_t metadata_title_id; /* optional lookup key, never XEX identity */
    uint32_t checksum;
};
#pragma pack(pop)
typedef char moonx_aurora_metadata_size_check[(sizeof(struct moonx_aurora_metadata)==504)?1:-1];
static uint32_t moonx_aurora_metadata_hash(const struct moonx_aurora_metadata *m)
{
    const unsigned char *p=(const unsigned char*)m;uint32_t h=2166136261u;
    for(size_t i=0;i<offsetof(struct moonx_aurora_metadata,checksum);i++)h=(h^p[i])*16777619u;
    return h;
}
static void moonx_aurora_metadata_seal(struct moonx_aurora_metadata *m)
{
    memcpy(m->magic,"MX36META",8);m->version=1;m->checksum=moonx_aurora_metadata_hash(m);
}
static int moonx_aurora_metadata_valid(const struct moonx_aurora_metadata *m,size_t n,const struct moonx_launch_data *link)
{
    return m && link && n==sizeof(*m) && !memcmp(m->magic,"MX36META",8) && m->version==1 &&
        moonx_launch_text(m->host_id,sizeof(m->host_id),1) &&
        m->app_id==link->app_id && !strcmp(m->host_id,link->host_id) &&
        moonx_launch_text(m->display_title,sizeof(m->display_title),1) &&
        moonx_launch_text(m->description,sizeof(m->description),0) &&
        moonx_launch_text(m->source,sizeof(m->source),1) &&
        (!strcmp(m->source,"none") || !strcmp(m->source,"manual") || !strcmp(m->source,"sunshine")) &&
        (!strcmp(m->source,"manual") ? m->metadata_title_id!=0 : m->metadata_title_id==0) &&
        m->checksum==moonx_aurora_metadata_hash(m);
}
