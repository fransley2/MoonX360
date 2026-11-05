#pragma once
/* Fixed wire format shared by moonx360.link and Xbox launch data. No credentials. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MOONX_LAUNCH_VERSION 1
#pragma pack(push,1)
struct moonx_launch_data {
    char magic[4];
    uint16_t version, size;
    char host_id[80];
    int32_t app_id;
    char profile_id[80];
    char title[128];
    char main_xex[160];
    uint32_t flags;
    uint32_t checksum;
};
#pragma pack(pop)
typedef char moonx_launch_wire_size_must_be_468[(sizeof(struct moonx_launch_data)==468)?1:-1];

static uint32_t moonx_launch_checksum(const struct moonx_launch_data *v)
{
    const unsigned char *p=(const unsigned char*)v;
    uint32_t h=2166136261u;
    for(size_t i=0;i<offsetof(struct moonx_launch_data,checksum);i++)h=(h^p[i])*16777619u;
    return h;
}
static int moonx_launch_text(const char *s,size_t cap,int required)
{
    if(!s)return 0;
    size_t n=0;while(n<cap && s[n])n++;
    if(n==cap || (required && !n))return 0;
    for(const unsigned char *p=(const unsigned char*)s;p<(const unsigned char*)s+n;p++)
        if(*p<32 || *p==127)return 0;
    return 1;
}
static int moonx_launch_path(const char *s,size_t cap)
{
    if(!moonx_launch_text(s,cap,1))return 0;
    size_t n=strlen(s);
    const char *colon=strchr(s,':');
    if(n<9 || s[0]=='\\' || s[0]=='/' || !colon || strstr(s,"..") ||
       strchr(s,'/') || strstr(s,"\\\\") || colon[1]!='\\' ||
       strchr(colon+1,':') || strpbrk(colon+1,"<>\"|?*"))return 0;
    if(colon-s==4 && (s[0]=='g'||s[0]=='G') && (s[1]=='a'||s[1]=='A') &&
       (s[2]=='m'||s[2]=='M') && (s[3]=='e'||s[3]=='E'))return 0;
    for(const char *p=s;p<colon;p++)
        if(!((*p>='A'&&*p<='Z')||(*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')))return 0;
    return n>=4 && s[n-4]=='.' && (s[n-3]=='x'||s[n-3]=='X') &&
           (s[n-2]=='e'||s[n-2]=='E') && (s[n-1]=='x'||s[n-1]=='X');
}
static int moonx_launch_folder(const char *s,size_t cap)
{
    if(!moonx_launch_text(s,cap,1))return 0;
    size_t n=strlen(s);const char *colon=strchr(s,':');
    if(n<4 || n>=cap || !colon || colon==s || colon[1]!='\\' ||
       strstr(s,"..") || strchr(s,'/') || strstr(s,"\\\\") || strchr(colon+1,':') ||
       strpbrk(colon+1,"<>\"|?*"))return 0;
    if(colon-s==4 && (s[0]=='g'||s[0]=='G') && (s[1]=='a'||s[1]=='A') &&
       (s[2]=='m'||s[2]=='M') && (s[3]=='e'||s[3]=='E'))return 0;
    for(const char *p=s;p<colon;p++)
        if(!((*p>='A'&&*p<='Z')||(*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')))return 0;
    return 1;
}
static int moonx_launch_valid(const struct moonx_launch_data *v,size_t n)
{
    if(!v || n!=sizeof(*v) || memcmp(v->magic,"MX36",4) ||
       v->version!=MOONX_LAUNCH_VERSION || v->size!=sizeof(*v) ||
       v->app_id<=0 || v->flags!=0 ||
       !moonx_launch_text(v->host_id,sizeof(v->host_id),1) ||
       !moonx_launch_text(v->profile_id,sizeof(v->profile_id),0) ||
       !moonx_launch_text(v->title,sizeof(v->title),1) ||
       !moonx_launch_path(v->main_xex,sizeof(v->main_xex)))return 0;
    for(const unsigned char *p=(const unsigned char*)v->host_id;*p;p++)if(*p>126)return 0;
    return v->checksum==moonx_launch_checksum(v);
}
static void moonx_launch_seal(struct moonx_launch_data *v)
{
    memcpy(v->magic,"MX36",4);v->version=MOONX_LAUNCH_VERSION;
    v->size=(uint16_t)sizeof(*v);v->checksum=moonx_launch_checksum(v);
}
/* Directory name only; the link keeps the original display title. */
static void moonx_shortcut_leaf(char out[64],const char *name,const char *host,int app)
{
    size_t used=0;uint32_t h=2166136261u;
    for(const unsigned char *p=(const unsigned char*)name;*p && used<48;p++) {
        char c=(char)*p;
        if((unsigned char)c<32 || (unsigned char)c>=127 || strchr("<>:\"/\\|?*",c))c='_';
        out[used++]=c;
    }
    while(used && (out[used-1]==' ' || out[used-1]=='.'))used--;
    if(!used){memcpy(out,"Game",4);used=4;}
    for(const unsigned char *p=(const unsigned char*)host;*p;p++) {
        unsigned char c=*p;if(c>='A'&&c<='Z')c=(unsigned char)(c+32);
        h=(h^c)*16777619u;
    }
    char number[16];
    unsigned value=(unsigned)app;int digits=0;
    do{number[digits++]=(char)('0'+value%10);value/=10;}while(value && digits<15);
    while(digits)h=(h^(unsigned char)number[--digits])*16777619u;
    static const char hex[]="0123456789ABCDEF";
    out[used++]='-';for(int shift=28;shift>=0;shift-=4)out[used++]=hex[(h>>shift)&15];
    out[used]=0;
}
