#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <map>
#include <string>
#include "../../src/config.h"
#include "../../src/video_smoothing.h"
static std::map<std::string,std::string> files;
static unsigned writes;
char *storage_read(const char *name,size_t *n){if(!files.count(name)){errno=ENOENT;return NULL;}if(n)*n=files[name].size();return _strdup(files[name].c_str());}
bool storage_write(const char *name,const void *p,size_t n){files[name]=std::string((const char*)p,n);writes++;return true;}
static void old_v3_profile(){
    std::string p="XENON_PROFILES 3 1 1\nOld CRT\t640\t480\t60\t9000\t1024\t0\t1\t0\t1\t0\t0\t1\t0\t1\t4\t0\t250\t1\t1\n";
    unsigned h=2166136261u;for(size_t i=0;i<p.size();i++)h=(h^(unsigned char)p[i])*16777619u;
    char tail[32];sprintf(tail,"CHECKSUM %08x\n",h);files.clear();files["profiles-a.tsv"]=p+tail;
}
int main(){
    smoothing_transition t={};bool started;int reason;
    for(int i=0;i<60;i++)assert(!smoothing_select(&t,false,true,true,0,&reason,&started) && !started);
    assert(!smoothing_select(&t,true,false,false,0,&reason,&started) && started && reason==SMOOTH_NO_HISTORY);
    assert(smoothing_select(&t,true,true,true,0,&reason,&started)==0.10f && started);
    assert(smoothing_select(&t,false,true,true,0,&reason,&started)==0.04f && !started);
    assert(!smoothing_select(&t,true,true,true,0,&reason,&started) && !started && reason==SMOOTH_DIRECT);
    assert(!smoothing_select(&t,false,true,true,0,&reason,&started));
    assert(smoothing_select(&t,true,true,true,1,&reason,&started)==0.15f);
    assert(smoothing_select(&t,true,true,true,1,&reason,&started)==0.07f && !started);
    assert(!smoothing_select(&t,true,true,true,1,&reason,&started)); // an all-IDR source still gets a direct frame
    assert(!smoothing_select(&t,true,true,false,1,&reason,&started) && reason==SMOOTH_MOTION);
    assert(!t.remaining);
    uint8_t y[16],u[4],v[4];memset(y,100,sizeof(y));memset(u,128,sizeof(u));memset(v,128,sizeof(v));
    const uint8_t *p[]={y,u,v};int pitch[]={4,2,2};smoothing_signature a,b;
    smoothing_signature_capture(&a,p,pitch,4,4);assert(a.valid && y[0]==100);
    memset(y,104,sizeof(y));smoothing_signature_capture(&b,p,pitch,4,4);assert(smoothing_motion_safe(&a,&b));
    y[0]=150;smoothing_signature_capture(&b,p,pitch,4,4);assert(!smoothing_motion_safe(&a,&b));
    memset(y,100,sizeof(y));memset(u,145,sizeof(u));smoothing_signature_capture(&b,p,pitch,4,4);assert(!smoothing_motion_safe(&a,&b));
    pitch[0]=-4;smoothing_signature_capture(&b,p,pitch,4,4);assert(!b.valid && !smoothing_motion_safe(&a,&b));
    // Single-pixel reference of the shader's constants/gate; no CPU image filtering.
    float c[4];smoothing_constants(0.15f,c);float delta=4.0f/255.0f;
    float effective=c[0]*(c[1]-delta)*c[2];assert(effective>0 && effective<=0.15f);
    float current=104.0f/255.0f,previous=100.0f/255.0f;
    assert(current+effective*(previous-current)<current && c[1]-(20.0f/255.0f)<0);
    config cfg;config_defaults(&cfg);assert(!cfg.refresh_smoothing && !cfg.smoothing_strength);
    cfg.refresh_smoothing=1;cfg.smoothing_strength=1;cfg.refresh_interval_ms=250;assert(config_save(&cfg));
    config loaded;config_load(&loaded);assert(loaded.refresh_smoothing==1 && loaded.smoothing_strength==1 && loaded.refresh_interval_ms==250);
    user_profiles profiles={};profiles.slot=-1;assert(config_profiles_create(&profiles,&cfg,"GPU experimental"));
    user_profiles again={};assert(config_profiles_load(&again));config_defaults(&loaded);
    assert(config_profiles_apply(&again,0,&loaded) && loaded.refresh_smoothing==1 && loaded.smoothing_strength==1 && loaded.refresh_interval_ms==250);
    unsigned before=writes;profiles.items[0].options.refresh_smoothing=2;char text[8192];assert(!config_profiles_serialize(&profiles,text,sizeof(text)) && writes==before);
    old_v3_profile();before=writes;again=user_profiles();assert(config_profiles_load(&again) && writes==before);
    config_defaults(&loaded);loaded.refresh_smoothing=1;loaded.smoothing_strength=1;
    assert(config_profiles_apply(&again,0,&loaded));
    assert(loaded.refresh_smoothing==1 && loaded.smoothing_strength==1 && loaded.refresh_interval_ms==250 && loaded.crt_clarity);
    config_select_factory(&loaded,CONFIG_PROFILE_WIRELESS);assert(!loaded.refresh_smoothing && !loaded.smoothing_strength && loaded.refresh_interval_ms==5000);
    puts("PASS: bounded IDR-only smoothing policy, Low/Medium caps, no continuous restart, motion/invalid gates, nonzero pixel-reference arithmetic, Off default, INI/V4 persistence and V3 read-without-migration");
}
