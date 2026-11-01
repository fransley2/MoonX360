#pragma once
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define GAME_BINDINGS_MAX 32
struct game_binding {char host[96],profile[80];int app;};
struct game_bindings {int count;bool read_only;struct game_binding items[GAME_BINDINGS_MAX];};
static unsigned feature_checksum(const char *s,size_t n) {unsigned h=2166136261u;for(size_t i=0;i<n;i++)h=(h^(unsigned char)s[i])*16777619u;return h;}
/* The Xbox CRT accepts %n but does not store its result. Parse the complete
 * checksum record explicitly, including its length and terminating LF. */
static bool feature_checksum_read(const char *p,size_t bytes,unsigned *hash)
{
    if(bytes!=9 || p[8]!='\n')return false;
    unsigned value=0;
    for(int i=0;i<8;i++) {
        unsigned digit;char c=p[i];
        if(c>='0' && c<='9')digit=c-'0';else if(c>='a' && c<='f')digit=c-'a'+10;
        else if(c>='A' && c<='F')digit=c-'A'+10;else return false;
        value=(value<<4)|digit;
    }
    *hash=value;return true;
}
static bool feature_search(const char *text,const char *query)
{
    if(!*query)return true;
    for(;*text;text++) {
        const unsigned char *a=(const unsigned char*)text,*b=(const unsigned char*)query;
        while(*a && *b) {unsigned x=*a++,y=*b++;if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)break;if(!*b)return true;}
    }
    return false;
}
static int binding_find(const struct game_bindings *s,const char *host,int app)
{for(int i=0;i<s->count;i++)if(s->items[i].app==app && !strcmp(s->items[i].host,host))return i;return -1;}
static bool binding_profile_valid(const char *name)
{
    if(!*name)return true;
    if(!strncmp(name,"user:",5))return config_profile_name_valid(name+5);
    return !strncmp(name,"factory:",8) && strlen(name)==9 && name[8]>='1' && name[8]<'0'+CONFIG_PROFILE_COUNT;
}
static bool binding_set(struct game_bindings *s,const char *host,int app,const char *profile)
{
    if(!binding_profile_valid(profile) || s->read_only || !*host || strlen(host)>=96 || app<=0 || strlen(profile)>=80 || strchr(host,'\t') || strchr(host,'\n'))return false;
    int i=binding_find(s,host,app);
    if(!*profile){if(i>=0){memmove(s->items+i,s->items+i+1,(s->count-i-1)*sizeof(s->items[0]));s->count--;}return true;}
    if(i<0){if(s->count==GAME_BINDINGS_MAX)return false;i=s->count++;}
    strcpy(s->items[i].host,host);strcpy(s->items[i].profile,profile);s->items[i].app=app;return true;
}
static size_t bindings_encode(const struct game_bindings *s,char *out,size_t cap)
{
    if(cap<32 || s->count<0 || s->count>GAME_BINDINGS_MAX)return 0;
    size_t used=0;int n=snprintf(out,cap,"MOONX_BINDINGS 1\n");if(n<0 || (size_t)n>=cap)return 0;used=n;
    static const char hex[]="0123456789abcdef";
    for(int i=0;i<s->count;i++) {
        char name[160];size_t l=strlen(s->items[i].profile);
        for(size_t j=0;j<l;j++){unsigned char c=(unsigned char)s->items[i].profile[j];name[j*2]=hex[c>>4];name[j*2+1]=hex[c&15];}name[l*2]=0;
        n=snprintf(out+used,cap-used,"%s\t%d\t%s\n",s->items[i].host,s->items[i].app,name);
        if(n<0 || (size_t)n>=cap-used)return 0;used+=(size_t)n;
    }
    n=snprintf(out+used,cap-used,"CHECKSUM %08x\n",feature_checksum(out,used));return n>0 && (size_t)n<cap-used?used+n:0;
}
static bool bindings_decode_impl(struct game_bindings *s,const char *text)
{
    memset(s,0,sizeof(*s));if(!text)return true;
    if(strlen(text)>8192 || strncmp(text,"MOONX_BINDINGS 1\n",17)){s->read_only=true;return false;}
    const char *p=text+17;
    while(strncmp(p,"CHECKSUM ",9)) {
        const char *end=strchr(p,'\n');if(!end || end-p>240){s->read_only=true;return false;}
        char line[256],host[96],encoded[160],profile[80],decimal[16],tail;
        memcpy(line,p,end-p);line[end-p]=0;
        if(sscanf(line,"%95[^\t]\t%15[^\t]\t%159s%c",host,decimal,encoded,&tail)!=3 || (strlen(encoded)&1) || strlen(encoded)>158){s->read_only=true;return false;}
        for(const char *d=decimal;*d;d++)if(*d<'0'||*d>'9'){s->read_only=true;return false;}
        errno=0;char *last;unsigned long number=strtoul(decimal,&last,10);
        if(errno==ERANGE || *last || !number || number>2147483647ul){s->read_only=true;return false;}int app=(int)number;
        size_t l=strlen(encoded)/2;
        for(size_t j=0;j<l;j++) {char c[3]={encoded[j*2],encoded[j*2+1],0},*last;long v=strtol(c,&last,16);if(*last || !v){s->read_only=true;return false;}profile[j]=(char)v;}
        profile[l]=0;if(!binding_profile_valid(profile) || binding_find(s,host,app)>=0 || !binding_set(s,host,app,profile)){s->read_only=true;return false;}
        p=end+1;
    }
    unsigned h;if(strlen(p)!=18 || !feature_checksum_read(p+9,9,&h) || h!=feature_checksum(text,p-text)){s->read_only=true;return false;}
    return true;
}
static bool bindings_decode(struct game_bindings *s,const char *text)
{
    struct game_bindings candidate;
    if(!bindings_decode_impl(&candidate,text)){memset(s,0,sizeof(*s));s->read_only=true;return false;}
    *s=candidate;return true;
}
static const char *feature_diagnosis(const char *error,bool online,bool paired)
{
    if(strstr(error,"403") || feature_search(error,"permission") || strstr(error,"Client Management"))return "Check this console's permissions in the host's Client Management. Pairing and launch permission are separate.";
    if(!online)return "Check PC power, selected IP, network cable and host service. Wake-on-LAN requires PC support and configuration.";
    if(!paired)return "Pair this console with the selected host using the PIN. Existing certificates are preserved.";
    return "The host is paired. Check host encoder availability, firewall and streaming ports. Try stereo and a lower stream profile to isolate the failure.";
}
