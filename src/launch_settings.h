#pragma once
#include "product_features.h"
#define LAUNCH_SETTINGS_BYTES 8192
/* Independent bounded per-host/app launch preferences. Never profile content. */
struct launch_setting {char host[96];int app,seconds,desktop;};
enum launch_settings_error { LAUNCH_SETTINGS_OK,LAUNCH_SETTINGS_READ,LAUNCH_SETTINGS_FORMAT,
    LAUNCH_SETTINGS_VERSION,LAUNCH_SETTINGS_ARGUMENT,LAUNCH_SETTINGS_FULL,
    LAUNCH_SETTINGS_SERIALIZE,LAUNCH_SETTINGS_WRITE,LAUNCH_SETTINGS_VERIFY };
struct launch_settings {int count;bool read_only;struct launch_setting items[32];int error,system_error;size_t expected,actual;};
static int launch_setting_find(const struct launch_settings *s,const char *host,int app)
{for(int i=0;i<s->count;i++)if(s->items[i].app==app && !strcmp(s->items[i].host,host))return i;return -1;}
static bool launch_settings_number(const char *p,const char *end,int low,int high,int *value)
{
    if(p==end || end-p>11)return false;
    for(const char *s=p;s<end;s++)if((*s<'0'||*s>'9') && !(s==p && *s=='-'))return false;
    errno=0;char *last;long n=strtol(p,&last,10);int e=errno;
    if(e || last!=end || n<low || n>high)return false;*value=(int)n;return true;
}
static bool launch_settings_decode(struct launch_settings *s,const char *text,size_t n)
{
    static const char header[]="MOONX_LAUNCH 1\n";
    memset(s,0,sizeof(*s));s->error=LAUNCH_SETTINGS_FORMAT;
    if(!text || n>LAUNCH_SETTINGS_BYTES || n<sizeof(header)-2 || memchr(text,0,n))return false;
    size_t prefix=sizeof(header)-1;
    if(n<prefix || memcmp(text,header,prefix)) {
        /* Compatibility: former writer omitted the terminating newline (14 bytes).
         * Only that exact known prefix is accepted. Unknown versions stay protected. */
        if(strncmp(text,header,prefix-1)) {if(!strncmp(text,"MOONX_LAUNCH ",13))s->error=LAUNCH_SETTINGS_VERSION;return false;}
        const char *version_tail=text+prefix-1;while(version_tail<text+n && *version_tail>='0' && *version_tail<='9')version_tail++;
        if(version_tail<text+n && (*version_tail=='\n' || *version_tail=='\r')){s->error=LAUNCH_SETTINGS_VERSION;return false;}
        prefix--;
    }
    const char *p=text+prefix,*limit=text+n;
    while(limit-p>=9 && strncmp(p,"CHECKSUM ",9)) {
        if(s->count==32)return false;
        const char *line=(const char*)memchr(p,'\n',limit-p);if(!line)return false;
        const char *a=(const char*)memchr(p,'\t',line-p);if(!a || a==p || a-p>=96)return false;
        const char *b=(const char*)memchr(a+1,'\t',line-a-1);if(!b)return false;
        const char *c=(const char*)memchr(b+1,'\t',line-b-1);if(!c)return false;
        struct launch_setting r;memset(&r,0,sizeof(r));memcpy(r.host,p,a-p);
        if(strchr(r.host,'\r') || strchr(r.host,'\n') ||
           !launch_settings_number(a+1,b,1,2147483647,&r.app) ||
           !launch_settings_number(b+1,c,-1,10,&r.seconds) ||
           !launch_settings_number(c+1,line,0,1,&r.desktop) || launch_setting_find(s,r.host,r.app)>=0)return false;
        s->items[s->count++]=r;p=line+1;
    }
    if(limit-p!=18 || strncmp(p,"CHECKSUM ",9) || p[17]!='\n')return false;
    unsigned hash=0;
    for(int i=9;i<17;i++){char v=p[i];int digit=v>='0'&&v<='9'?v-'0':v>='a'&&v<='f'?v-'a'+10:v>='A'&&v<='F'?v-'A'+10:-1;if(digit<0)return false;hash=(hash<<4)|(unsigned)digit;}
    if(hash!=feature_checksum(text,p-text))return false;
    s->error=LAUNCH_SETTINGS_OK;return true;
}
static void launch_settings_load(struct launch_settings *s)
{
    memset(s,0,sizeof(*s));size_t n=0;errno=0;char *text=storage_read("launch-settings.tsv",&n);int e=errno;
    if(!text){s->read_only=e!=ENOENT;s->error=s->read_only?LAUNCH_SETTINGS_READ:LAUNCH_SETTINGS_OK;s->system_error=e;return;}
    struct launch_settings next;bool ok=launch_settings_decode(&next,text,n);free(text);
    if(!ok && next.error!=LAUNCH_SETTINGS_VERSION) {
        size_t backup_n=0;char *backup=storage_read("launch-settings.tsv.bak",&backup_n);
        struct launch_settings recovered;
        if(backup && launch_settings_decode(&recovered,backup,backup_n)){next=recovered;ok=true;}
        free(backup);
    }
    if(ok)*s=next;else {s->read_only=true;s->error=next.error;s->actual=n;}
}
static bool launch_settings_save(struct launch_settings *s,const char *host,int app,int seconds,int desktop)
{
    s->expected=s->actual=0;s->system_error=0;
    if(s->read_only)return false;
    if(!host || strlen(host)>=96 || !*host || strchr(host,'\t') || strchr(host,'\n') || strchr(host,'\r') ||
       app<=0 || seconds< -1 || seconds>10 || desktop<0 || desktop>1){s->error=LAUNCH_SETTINGS_ARGUMENT;return false;}
    struct launch_settings next=*s;int i=launch_setting_find(&next,host,app);
    if(i<0){if(next.count==32){s->error=LAUNCH_SETTINGS_FULL;return false;}i=next.count++;}
    struct launch_setting *r=next.items+i;strcpy(r->host,host);r->app=app;r->seconds=seconds;r->desktop=desktop;
    static const char header[]="MOONX_LAUNCH 1\n";
    char text[LAUNCH_SETTINGS_BYTES];size_t n=sizeof(header)-1;memcpy(text,header,n);
    for(int j=0;j<next.count;j++) {
        r=next.items+j;int k=snprintf(text+n,sizeof(text)-n,"%s\t%d\t%d\t%d\n",r->host,r->app,r->seconds,r->desktop);
        if(k<0 || (size_t)k>=sizeof(text)-n){s->error=LAUNCH_SETTINGS_SERIALIZE;return false;}n+=k;
    }
    unsigned hash=feature_checksum(text,n);int k=snprintf(text+n,sizeof(text)-n,"CHECKSUM %08x\n",hash);
    if(k<=0 || (size_t)k>=sizeof(text)-n){s->error=LAUNCH_SETTINGS_SERIALIZE;return false;}
    n+=k;s->expected=n;errno=0;
    if(!storage_write("launch-settings.tsv",text,n)){s->system_error=errno;s->error=LAUNCH_SETTINGS_WRITE;return false;}
    /* storage_write uses the same verified temporary + backup promotion as
     * config_save. Publish only its completed durable replacement result. */
    next.error=LAUNCH_SETTINGS_OK;next.system_error=0;next.expected=next.actual=n;*s=next;return true;
}
static const char *launch_settings_problem(const struct launch_settings *s)
{
    switch(s->error) {
    case LAUNCH_SETTINGS_READ:return "Could not read launch-settings.tsv. Check the selected storage device.";
    case LAUNCH_SETTINGS_FORMAT:return "launch-settings.tsv has invalid data or checksum. Previous settings were preserved.";
    case LAUNCH_SETTINGS_VERSION:return "launch-settings.tsv uses an unsupported version. The file was preserved.";
    case LAUNCH_SETTINGS_FULL:return "Launch coverage has reached its limit of 32 applications.";
    case LAUNCH_SETTINGS_ARGUMENT:return "Invalid host, application ID or coverage value.";
    case LAUNCH_SETTINGS_SERIALIZE:return "Launch coverage could not be serialized within its buffer limit.";
    case LAUNCH_SETTINGS_VERIFY:return "Launch coverage write could not be verified. Check the storage diagnostics.";
    case LAUNCH_SETTINGS_WRITE:
        if(s->system_error==ENOSPC)return "The storage device reported insufficient space. Previous settings were preserved.";
        if(s->system_error==EACCES)return "Access denied while saving launch-settings.tsv. Check device access.";
        return "Could not write launch-settings.tsv. See storage diagnostics for the failing operation.";
    default:return "Launch coverage could not be saved. Previous settings were preserved.";
    }
}
