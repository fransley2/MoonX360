#include "mdns.h"
#include <string.h>
#include <stdio.h>

static unsigned short read16(const unsigned char *p) { return (unsigned short)((p[0]<<8)|p[1]); }
bool mdns_name(const unsigned char *p,size_t n,size_t *offset,char *out,size_t cap)
{
    size_t pos=*offset,used=0,next=0;unsigned int hops=0;
    if(!cap)return false;
    while(pos<n && ++hops<=128) {
        unsigned int len=p[pos++];
        if((len&0xc0)==0xc0) {
            if(pos>=n)return false;
            size_t target=((len&63)<<8)|p[pos++];
            /* RFC compression points to a prior name; reject loops/forward pointers. */
            if(target>=pos-2)return false;
            if(!next)next=pos;
            pos=target;continue;
        }
        if(len&0xc0)return false;
        if(!len) {out[used]=0;*offset=next?next:pos;return true;}
        if(len>n-pos || used+len+1>=cap || used+len+1>254)return false;
        for(unsigned int i=0;i<len;i++) {
            unsigned char c=p[pos++];
            if(c<32 || c==127 || c=='.')return false;
            out[used++]=(char)(c>='A'&&c<='Z'?c+32:c);
        }
        out[used++]='.';
    }
    return false;
}
static bool instance_name(const char *name)
{
    size_t a=strlen(name),b=strlen(MDNS_SERVICE);
    return a>b+1 && name[a-b-1]=='.' && !strcmp(name+a-b,MDNS_SERVICE);
}
static struct mdns_service *service(struct mdns_state *s,const char *name)
{
    int free_slot=-1;
    for(int i=0;i<MDNS_MAX;i++) {
        if(!strcmp(s->services[i].instance,name))return s->services+i;
        if(!s->services[i].instance[0] && free_slot<0)free_slot=i;
    }
    if(free_slot<0)return NULL;
    snprintf(s->services[free_slot].instance,MDNS_NAME,"%s",name);
    return s->services+free_slot;
}
static struct mdns_address *address(struct mdns_state *s,const char *name)
{
    int free_slot=-1;
    for(int i=0;i<MDNS_MAX;i++) {
        if(!strcmp(s->addresses[i].name,name))return s->addresses+i;
        if(!s->addresses[i].name[0] && free_slot<0)free_slot=i;
    }
    if(free_slot<0)return NULL;
    snprintf(s->addresses[free_slot].name,MDNS_NAME,"%s",name);
    return s->addresses+free_slot;
}
bool mdns_parse(struct mdns_state *state,const unsigned char *p,size_t n)
{
    size_t pos=12;char owner[MDNS_NAME],target[MDNS_NAME];
    if(n<12 || n>8192 || !(p[2]&0x80) || (p[2]&0x7a) || (p[3]&15))return false;
    unsigned int questions=read16(p+4),records=(unsigned int)read16(p+6)+read16(p+8)+read16(p+10);
    if(questions>64 || records>128)return false;
    struct mdns_state next=*state; /* Malformed packets cannot partially publish records. */
    for(unsigned int i=0;i<questions;i++) {
        if(!mdns_name(p,n,&pos,owner,sizeof(owner)) || n-pos<4)return false;
        pos+=4;
    }
    for(unsigned int i=0;i<records;i++) {
        if(!mdns_name(p,n,&pos,owner,sizeof(owner)) || n-pos<10)return false;
        unsigned short type=read16(p+pos),cls=read16(p+pos+2)&0x7fff,len=read16(p+pos+8);
        bool alive=(p[pos+4]|p[pos+5]|p[pos+6]|p[pos+7])!=0;
        pos+=10;if(len>n-pos)return false;size_t end=pos+len,r=pos;
        if(cls==1 && type==12) {
            if(!mdns_name(p,n,&r,target,sizeof(target)) || r!=end)return false;
            if(!strcmp(owner,MDNS_SERVICE) && instance_name(target)) {
                struct mdns_service *s=service(&next,target);if(s)s->ptr=alive;
            }
        } else if(cls==1 && type==33) {
            if(len<7)return false;r+=6;
            if(!mdns_name(p,n,&r,target,sizeof(target)) || r!=end)return false;
            if(instance_name(owner)) {
                struct mdns_service *s=service(&next,owner);
                if(s) {snprintf(s->target,sizeof(s->target),"%s",target);s->port=alive?read16(p+pos+4):0;}
            }
        } else if(cls==1 && type==1) {
            if(len!=4)return false;
            struct mdns_address *a=address(&next,owner);
            if(a) {memcpy(a->ip,p+pos,4);a->valid=alive && p[pos]>0 && p[pos]<224 && p[pos]!=127;}
        } else if(cls==1 && type==16) {
            struct mdns_service *s=instance_name(owner)?service(&next,owner):NULL;
            if(s)s->txt[0]=0;
            while(r<end) {
                unsigned int l=p[r++];if(l>end-r)return false;
                if(s && !s->txt[0]) {
                    size_t copy=l<sizeof(s->txt)-1?l:sizeof(s->txt)-1;
                    for(size_t j=0;j<copy;j++)s->txt[j]=p[r+j]>=32 && p[r+j]<127?(char)p[r+j]:'?';
                    s->txt[copy]=0;
                }
                r+=l;
            }
        }
        pos=end;
    }
    *state=next;return true;
}
int mdns_candidates(const struct mdns_state *s,struct mdns_candidate *out,int cap)
{
    int count=0;
    for(int i=0;i<MDNS_MAX && count<cap;i++) {
        const struct mdns_service *v=s->services+i;if(!v->ptr || !v->port || !v->target[0])continue;
        for(int j=0;j<MDNS_MAX && count<cap;j++) {
            const struct mdns_address *a=s->addresses+j;
            if(!a->valid || strcmp(v->target,a->name))continue;
            char ip[16];snprintf(ip,sizeof(ip),"%u.%u.%u.%u",a->ip[0],a->ip[1],a->ip[2],a->ip[3]);
            bool duplicate=false;for(int k=0;k<count;k++)if(!strcmp(out[k].ip,ip))duplicate=true;
            if(duplicate)continue;
            snprintf(out[count].ip,sizeof(out[count].ip),"%s",ip);out[count++].port=v->port;
        }
    }
    return count;
}
size_t mdns_query(unsigned char *out,size_t cap,const char *name,unsigned short type,bool unicast)
{
    size_t pos=12;const char *p=name;
    if(cap<18)return 0;memset(out,0,12);out[5]=1;
    while(*p) {
        const char *end=strchr(p,'.');size_t len=end?(size_t)(end-p):strlen(p);
        if(!len || len>63 || pos+len+6>cap)return 0;
        out[pos++]=(unsigned char)len;memcpy(out+pos,p,len);pos+=len;
        p+=len;if(*p=='.')p++;
    }
    out[pos++]=0;out[pos++]=(unsigned char)(type>>8);out[pos++]=(unsigned char)type;
    out[pos++]=unicast?0x80:0;out[pos++]=1;return pos;
}
