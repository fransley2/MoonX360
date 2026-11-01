#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Client-side protocol metadata only. Optional extensions never select a
 * different transport, decoder or renderer. No hostname-based decisions. */
enum gs_host_type { GS_HOST_UNKNOWN, GS_HOST_SUNSHINE, GS_HOST_APOLLO, GS_HOST_VIBEPOLLO };
enum gs_error_code { GS_ERROR_NONE, GS_ERROR_UNREACHABLE, GS_ERROR_CANCELLED,
    GS_ERROR_PAIRING, GS_ERROR_PERMISSION, GS_ERROR_APP_MISSING,
    GS_ERROR_IDENTITY, GS_ERROR_PROTOCOL };
struct gs_capabilities {
    bool baseline, app_list, app_launch, cover_art;
    bool virtual_display, virtual_display_ready, stable_app_uuid, permissions;
    bool resolution_matching, refresh_matching;
    uint32_t permission_mask;
    bool frame_limiter, frame_limiter_enabled;
    uint32_t frame_limit_millihz; /* optional configured cap, never negotiated FPS */
};
#define GS_PERM_LIST (1u << 24)
#define GS_PERM_VIEW (1u << 25)
#define GS_PERM_LAUNCH (1u << 26)
#define GS_PERM_CONTROLLER (1u << 8)
/* The wire's appversion is a compatibility version, not the software release.
 * Branding may be absent (including stock Apollo). Keep that case unknown. */
struct gs_host_metadata {
    enum gs_host_type type;
    struct gs_capabilities caps;
    char software_version[64];
};
static bool gs_field(const char *xml,const char *tag,char *out,size_t cap)
{
    char open[80],close[80];if(!xml || !cap)return false;out[0]=0;
    snprintf(open,sizeof(open),"<%s>",tag);snprintf(close,sizeof(close),"</%s>",tag);
    const char *s=strstr(xml,open);if(!s)return false;s+=strlen(open);
    const char *e=strstr(s,close);if(!e)return false;
    size_t n=(size_t)(e-s);if(n>=cap)return false;memcpy(out,s,n);out[n]=0;return true;
}
static bool gs_contains(const char *s,const char *word)
{
    for(;*s;s++) {const char *a=s,*b=word;while(*a && *b) {
        char c=*a++;if(c>='A' && c<='Z')c+='a'-'A';if(c!=*b++)break;
        if(!*b)return true;
    }}return false;
}
static bool gs_true(const char *s){return !strcmp(s,"1") || !strcmp(s,"true");}
static bool gs_u32_text(const char *text,uint32_t *out)
{
    uint64_t n=0;if(!*text)return false;
    for(const char *p=text;*p;p++){if(*p<'0'||*p>'9')return false;n=n*10+(*p-'0');if(n>0xffffffffu)return false;}
    *out=(uint32_t)n;return true;
}
static void gs_host_parse(struct gs_host_metadata *m,const char *xml,bool authenticated)
{
    char value[128];memset(m,0,sizeof(*m));
    m->caps.baseline=true;m->caps.app_list=true;m->caps.app_launch=true;m->caps.cover_art=true;
    /* Explicit software fields only; user-chosen PC names are never branding. */
    if(gs_field(xml,"ServerSoftware",value,sizeof(value)) || gs_field(xml,"software",value,sizeof(value))) {
        if(gs_contains(value,"vibepollo"))m->type=GS_HOST_VIBEPOLLO;
        else if(gs_contains(value,"apollo"))m->type=GS_HOST_APOLLO;
        else if(gs_contains(value,"sunshine"))m->type=GS_HOST_SUNSHINE;
    }
    gs_field(xml,"ServerVersion",m->software_version,sizeof(m->software_version));
    if(authenticated && gs_field(xml,"Permission",value,sizeof(value))) {
        uint32_t mask;
        if(gs_u32_text(value,&mask)) {
            m->caps.permissions=true;m->caps.permission_mask=(uint32_t)mask;
            m->caps.app_list=(mask&GS_PERM_LIST)!=0;m->caps.app_launch=(mask&GS_PERM_LAUNCH)!=0;
        }
    }
    if(authenticated && gs_field(xml,"VirtualDisplayCapable",value,sizeof(value))) {
        m->caps.virtual_display=gs_true(value);
        m->caps.resolution_matching=m->caps.refresh_matching=m->caps.virtual_display;
    }
    if(authenticated && gs_field(xml,"VirtualDisplayDriverReady",value,sizeof(value)))m->caps.virtual_display_ready=gs_true(value);
    if(authenticated && gs_field(xml,"FrameLimiterSupported",value,sizeof(value)))m->caps.frame_limiter=gs_true(value);
    if(m->caps.frame_limiter) {
        if(gs_field(xml,"FrameLimiterEnabled",value,sizeof(value)))m->caps.frame_limiter_enabled=gs_true(value);
        if(gs_field(xml,"FrameLimiterFpsLimitMilliHz",value,sizeof(value)))gs_u32_text(value,&m->caps.frame_limit_millihz);
    }
}
static const char *gs_host_type_name(enum gs_host_type t)
{
    switch(t){case GS_HOST_SUNSHINE:return "Sunshine";case GS_HOST_APOLLO:return "Apollo";
    case GS_HOST_VIBEPOLLO:return "Vibepollo";default:return "Unknown compatible host";}
}
/* Display refresh classes only. Never halve fractional rates or feed this
 * approximation back into decoder/presentation timing. */
static int gs_refresh_class(double hz)
{return hz>=59.0 && hz<=61.0?60:hz>=29.0 && hz<=31.0?30:0;}
