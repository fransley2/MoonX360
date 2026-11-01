#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../src/host_compat.h"
#include "../../src/app_identity.h"
#include "../../src/host_cache.h"
int main()
{
    gs_host_metadata m;
    gs_host_parse(&m,"<hostname>Apollo</hostname><appversion>99.0</appversion>",true);
    assert(m.type==GS_HOST_UNKNOWN && m.caps.baseline && m.caps.app_launch && !m.caps.permissions);
    const char *apollo="<Permission>50331648</Permission><VirtualDisplayCapable>true</VirtualDisplayCapable><VirtualDisplayDriverReady>true</VirtualDisplayDriverReady>";
    gs_host_parse(&m,apollo,true);assert(m.caps.app_list && !m.caps.app_launch && m.caps.permissions && m.caps.virtual_display && m.caps.virtual_display_ready);
    assert(m.caps.resolution_matching && m.caps.refresh_matching);
    gs_host_parse(&m,apollo,false);assert(!m.caps.permissions && m.caps.app_launch && !m.caps.virtual_display);
    gs_host_parse(&m,"<ServerSoftware>Vibepollo</ServerSoftware><Permission>0</Permission>",true);
    assert(m.type==GS_HOST_VIBEPOLLO && !m.caps.app_list && !m.caps.app_launch);
    gs_host_parse(&m,"<ServerSoftware>Apollo</ServerSoftware><ServerVersion>1.2.3</ServerVersion><Unknown>future</Unknown>",true);
    assert(m.type==GS_HOST_APOLLO && !strcmp(m.software_version,"1.2.3") && m.caps.app_launch);
    gs_host_parse(&m,"<ServerSoftware>Sunshine</ServerSoftware>",true);
    assert(m.type==GS_HOST_SUNSHINE && m.caps.baseline && !m.caps.virtual_display && !m.caps.permissions);
    gs_host_parse(&m,"<Permission>-1</Permission><FrameLimiterSupported>1</FrameLimiterSupported><FrameLimiterEnabled>1</FrameLimiterEnabled><FrameLimiterFpsLimitMilliHz>59940</FrameLimiterFpsLimitMilliHz>",true);
    assert(!m.caps.permissions && m.caps.frame_limiter && m.caps.frame_limiter_enabled && gs_refresh_class(m.caps.frame_limit_millihz/1000.0)==60);
    assert(gs_refresh_class(59.94)==60 && gs_refresh_class(60)==60 && gs_refresh_class(29.97)==30 && gs_refresh_class(30)==30 && !gs_refresh_class(120));
    app_identity_map before={},after={};
    assert(app_identity_decode(&before,"uuid:host","MOONX_APP_IDS 1\tuuid:host\n1\tgame-a\n2\tgame-b\n"));
    assert(app_identity_decode(&after,"uuid:host","MOONX_APP_IDS 1\tuuid:host\n2\tgame-a\n1\tgame-b\n"));
    assert(app_identity_resolve(&before,&after,1)==2 && app_identity_resolve(&before,&after,2)==1);
    char encoded[8192];assert(app_identity_encode(&after,"uuid:host",encoded,sizeof(encoded)));
    app_identity_map restored={};assert(app_identity_decode(&restored,"uuid:host",encoded) && restored.count==2);
    assert(!app_identity_decode(&restored,"uuid:another-host",encoded));
    assert(!app_identity_decode(&restored,"uuid:host","MOONX_APP_IDS 1\tuuid:host\n1\tgame-a\n1\tgame-b\n"));
    host_cache cache={};cached_host a={};strcpy(a.uuid,"host-a");strcpy(a.ip,"10.0.0.50");a.port=47989;a.online=a.paired=true;a.last_seen=1793448000;
    assert(host_cache_upsert(&cache,&a)==0);strcpy(cache.hosts[0].alias,"My Gaming PC");
    cached_host b=a;strcpy(b.uuid,"host-b");assert(host_cache_upsert(&cache,&b)==1 && cache.count==2);
    strcpy(a.ip,"10.0.0.87");assert(host_cache_upsert(&cache,&a)==0 && cache.count==2 && !strcmp(cache.hosts[0].alias,"My Gaming PC"));
    cached_host legacy={};strcpy(legacy.ip,"10.0.0.88");legacy.port=47989;host_cache_upsert(&cache,&legacy);
    strcpy(a.ip,"10.0.0.88");assert(host_cache_upsert(&cache,&a)==0 && !strcmp(cache.hosts[0].alias,"My Gaming PC") && cache.count==3);
    assert(host_cache_encode(&cache,encoded,sizeof(encoded)));host_cache_decode(&cache,encoded);
    assert(cache.count==3 && cache.hosts[0].last_seen==a.last_seen && cache.hosts[0].pairing_saved && !cache.hosts[0].online && !cache.hosts[0].paired);
    puts("PASS: capability isolation across Sunshine/Apollo/Vibepollo/unknown metadata, permission scopes, fractional refresh classes, stable app ID swaps, host separation and persistence.");
}
