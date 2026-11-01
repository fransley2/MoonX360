#include "host_cache.h"
#include "wake.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static bool same_text(const char *a,const char *b)
{
    while(*a && *b) {
        unsigned char x=(unsigned char)*a++,y=(unsigned char)*b++;
        if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;
        if(x!=y)return false;
    }
    return *a==*b;
}
static bool same_host(const struct cached_host *a,const struct cached_host *b)
{
    return a->uuid[0] && b->uuid[0] && same_text(a->uuid,b->uuid);
}
int host_cache_upsert(struct host_cache *c,const struct cached_host *h)
{
    if(!h->ip[0] || !h->port)return -1;
    int index=-1;
    /* Prefer the protocol identity before considering a legacy endpoint. */
    if(h->uuid[0])for(int i=0;i<c->count;i++)if(c->hosts[i].uuid[0] && same_text(c->hosts[i].uuid,h->uuid)){index=i;break;}
    if(index<0)for(int i=0;i<c->count;i++)if(!c->hosts[i].uuid[0] && !strcmp(c->hosts[i].ip,h->ip)){index=i;break;}
    if(index<0) {if(c->count>=HOST_CACHE_MAX)return -1;index=c->count++;memset(c->hosts+index,0,sizeof(*c->hosts));}
    struct cached_host next=*h;
    strcpy(next.alias,c->hosts[index].alias);next.auto_wake=c->hosts[index].auto_wake;next.is_default=c->hosts[index].is_default;
    unsigned char mac[6];
    if(!wake_mac(next.mac,mac)){next.mac[0]=0;next.mac_manual=false;}
    if(c->hosts[index].mac[0] && (!next.mac[0] || c->hosts[index].mac_manual)) {
        strcpy(next.mac,c->hosts[index].mac);next.mac_manual=c->hosts[index].mac_manual;
    }
    if(!next.uuid[0])snprintf(next.uuid,sizeof(next.uuid),"%s",c->hosts[index].uuid);
    if(!next.name[0])snprintf(next.name,sizeof(next.name),"%s",c->hosts[index].name);
    if(!next.last_seen)next.last_seen=c->hosts[index].last_seen;
    if(!next.online)next.pairing_saved=next.pairing_saved || c->hosts[index].pairing_saved;
    else next.pairing_saved=next.paired;
    c->hosts[index]=next;
    /* Only matching stable identities justify merging permanent records. */
    for(int i=c->count-1;i>=0;i--)if(i!=index && next.uuid[0] && c->hosts[i].uuid[0] && same_host(c->hosts+i,&next)) {
        memmove(c->hosts+i,c->hosts+i+1,(size_t)(c->count-i-1)*sizeof(*c->hosts));
        c->count--;if(i<index)index--;
    }
    return index;
}
void host_cache_decode(struct host_cache *c,char *text)
{
    memset(c,0,sizeof(*c));if(!text)return;
    char *line=text;
    while(*line && c->count<HOST_CACHE_MAX) {
        char *end=strchr(line,'\n');if(end)*end=0;
        size_t length=strlen(line);if(length && line[length-1]=='\r')line[length-1]=0;
        char *parts[4];parts[0]=line;bool valid=true;
        for(int i=1;i<4;i++) {char *tab=strchr(parts[i-1],'\t');if(!tab){valid=false;break;}*tab=0;parts[i]=tab+1;}
        if(valid) {
            struct cached_host h;memset(&h,0,sizeof(h));
            char *extra[7]={0};char *tail_field=strchr(parts[3],'\t');
            if(tail_field){*tail_field++=0;for(int i=0;i<7 && tail_field;i++) {
                extra[i]=tail_field;tail_field=strchr(tail_field,'\t');if(tail_field)*tail_field++=0;
            }}
            if(extra[0]){unsigned char bytes[6];if(wake_mac(extra[0],bytes)){snprintf(h.mac,sizeof(h.mac),"%s",extra[0]);h.mac_manual=extra[1] && !strcmp(extra[1],"1");}}
            if(extra[2])snprintf(h.alias,sizeof(h.alias),"%s",extra[2]);
            h.auto_wake=extra[3] && !strcmp(extra[3],"1");h.is_default=extra[4] && !strcmp(extra[4],"1");
            h.last_seen=extra[5]?(uint32_t)strtoul(extra[5],NULL,10):0;
            h.pairing_saved=extra[6] && !strcmp(extra[6],"1");
            unsigned int a,b,d,e;char tail;long port=strtol(parts[3],NULL,10);
            if(sscanf(parts[2],"%u.%u.%u.%u%c",&a,&b,&d,&e,&tail)==4 && a>0 && a<224 && a!=127 && b<256 && d<256 && e<256 && port>0 && port<=65535) {
                snprintf(h.uuid,sizeof(h.uuid),"%s",parts[0]);snprintf(h.name,sizeof(h.name),"%s",parts[1]);
                snprintf(h.ip,sizeof(h.ip),"%s",parts[2]);h.port=(unsigned short)port;int slot=host_cache_upsert(c,&h);if(slot>=0){strcpy(c->hosts[slot].alias,h.alias);c->hosts[slot].auto_wake=h.auto_wake;c->hosts[slot].is_default=h.is_default;}
            }
        }
        if(!end)break;line=end+1;
    }
}
static void safe_text(char *out,size_t cap,const char *in)
{
    size_t n=0;while(*in && n+1<cap) {unsigned char x=(unsigned char)*in++;out[n++]=x>=32 && x!=127?(char)x:' ';}out[n]=0;
}
size_t host_cache_encode(const struct host_cache *c,char *out,size_t cap)
{
    size_t used=0;if(!cap)return 0;out[0]=0;
    for(int i=0;i<c->count;i++) {
        const struct cached_host *h=c->hosts+i;char uuid[80],name[64],alias[64];
        safe_text(uuid,sizeof(uuid),h->uuid);safe_text(name,sizeof(name),h->name);safe_text(alias,sizeof(alias),h->alias);
        int n=snprintf(out+used,cap-used,"%s\t%s\t%s\t%u\t%s\t%d\t%s\t%d\t%d\t%u\t%d\n",uuid,name,h->ip,h->port,h->mac,h->mac_manual,alias,h->auto_wake,h->is_default,h->last_seen,h->pairing_saved);
        if(n<0 || (size_t)n>=cap-used){out[0]=0;return 0;}used+=(size_t)n;
    }
    return used;
}
