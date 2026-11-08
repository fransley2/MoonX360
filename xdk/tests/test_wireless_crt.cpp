#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <map>
#include <string>
#include <Limelight.h>
#include "../../src/config.h"
#include "../../src/console_link.h"
#include "../../src/display_policy.h"
#include "../../src/ui_layout_geometry.h"
#include "../../src/video_session.h"
#include "../../src/refresh_trace.h"
#include "../../src/product_features.h"
#include "../../src/rtp_diagnostics.h"
static std::map<std::string,std::string> files;
static bool fail_write;
char *storage_read(const char *name,size_t *bytes){
    if(!files.count(name)){errno=ENOENT;return NULL;}
    if(bytes)*bytes=files[name].size();return _strdup(files[name].c_str());
}
bool storage_write(const char *name,const void *p,size_t n){if(fail_write)return false;files[name]=std::string((const char*)p,n);return true;}
static bool same_config(const config &a,const config &b){char x[4096],y[4096];size_t nx=config_serialize(&a,x,sizeof(x)),ny=config_serialize(&b,y,sizeof(y));return nx && nx==ny && !memcmp(x,y,nx);}
int main(){
    config c;config_defaults(&c);config smooth=c;
    c.language=2;c.ui_layout=0;c.display_mode=2;c.ui_safe_area=2;c.left_min=17;strcpy(c.host,"original-host");
    config_select_factory(&c,CONFIG_PROFILE_WIRELESS);
    assert(c.width==1280 && c.height==720 && c.fps==30 && c.bitrate_kbps==6000 && c.refresh_interval_ms==5000);
    assert(c.fast_decode && c.vsync && c.packet_size==1024 && c.renderer==0);
    assert(c.language==2 && c.ui_layout==0 && c.display_mode==2 && c.ui_safe_area==2 && c.left_min==17 && !strcmp(c.host,"original-host"));
    assert(config_save(&c));config loaded;config_load(&loaded);assert(same_config(c,loaded));
    config_apply_preset(&smooth,CONFIG_PROFILE_SMOOTH);assert(smooth.width==1280 && smooth.height==720 && smooth.fps==60 && smooth.bitrate_kbps==12000 && smooth.refresh_interval_ms==0);
    const int rates[]={9000,10000,8000,12000};
    for(int i=0;i<4;i++){
        c.refresh_interval_ms=2000;config_select_factory(&c,CONFIG_PROFILE_CRT+i);
        assert(config_crt_profile(&c) && c.width==640 && c.height==480 && c.fps==(i==2?30:60) && c.bitrate_kbps==rates[i]);
        assert(c.refresh_interval_ms==2000 && c.image_quality==1 && c.fast_decode && c.renderer==0 && c.vsync && !c.audio_mode && !c.slice_count && c.packet_size==1024);
        assert(!c.sops && !c.crt_clarity && c.ui_layout==0 && c.display_mode==2);
        assert(config_save(&c));config_load(&loaded);assert(same_config(c,loaded));
    }
    assert(video_hd_colorspace(640,480)==COLORSPACE_REC_601);
    float coefficients[4];video_yuv_coefficients(COLORSPACE_REC_601,coefficients);assert(coefficients[0]>1.59f && coefficients[0]<1.60f);
    config original=c;fail_write=true;assert(!config_crt_display_commit(&c,true) && !memcmp(&c,&original,sizeof(c)));fail_write=false;
    assert(config_crt_display_commit(&c,true));assert(c.display_mode==4 && c.ui_layout==1 && c.ui_safe_area==5 && c.crt_display_backup);
    assert(config_crt_display_commit(&c,true));assert(c.previous_display_mode==2 && c.previous_ui_layout==0 && c.previous_safe_area==2);
    config_load(&loaded);assert(loaded.crt_display_backup && loaded.display_mode==4);
    loaded.language=1;fail_write=true;config before=loaded;
    assert(!config_crt_display_commit(&loaded,false) && !memcmp(&before,&loaded,sizeof(before)));fail_write=false;
    assert(config_crt_display_commit(&loaded,false));assert(loaded.display_mode==2 && loaded.ui_layout==0 && loaded.ui_safe_area==2 && loaded.language==1);
    int l,t,r,b;ui_layout_safe_viewport(960,720,640,480,5,&l,&t,&r,&b);assert(l==32 && t==24 && r==608 && b==456);
    video_fit_rect(1920,1080,640,480,&l,&t,&r,&b);assert(l==0 && r==640 && t==60 && b==420); // gameplay is not safe-area cropped
    ui_layout_canvas_size(1,&l,&t);assert(l==960 && t==720);
    display_render_size(4,1920,1080,&l,&t);assert(l==640 && t==480); // render target says nothing about scanout
    assert(display_rate_x100(59.94f)==5994 && display_rate_x100(60)==6000);
    assert(console_link_classify(0,1,32)==CONSOLE_LINK_UNKNOWN && console_link_classify(32,1,32)==CONSOLE_LINK_UNKNOWN);
    assert(console_link_classify(1,1,32)==CONSOLE_LINK_WIRED && console_link_classify(33,1,32)==CONSOLE_LINK_WIRELESS);
    assert(binding_profile_valid("factory:10") && !binding_profile_valid("factory:11") && !binding_profile_valid("factory::") && !binding_profile_valid("factory:01"));
    files.clear();user_profiles profiles={};profiles.slot=-1;c.refresh_interval_ms=1000;c.crt_clarity=true;
    assert(config_profiles_create(&profiles,&c,"CRT personal"));user_profiles restarted={};assert(config_profiles_load(&restarted));
    config custom=original;assert(config_profiles_apply(&restarted,0,&custom));assert(custom.refresh_interval_ms==1000 && custom.crt_clarity && custom.fps==c.fps && custom.bitrate_kbps==c.bitrate_kbps);
    // A real old-format record must retain its original semantics for newly owned options.
    files.clear();std::string payload="XENON_PROFILES 2 1 1\nLegacy\t1280\t720\t60\t12000\t1024\t0\t1\t0\t1\t1\t0\t1\t0\t1\t4\t0\n";
    unsigned checksum=2166136261u;for(size_t i=0;i<payload.size();i++)checksum=(checksum^(unsigned char)payload[i])*16777619u;
    char tail[32];sprintf(tail,"CHECKSUM %08x\n",checksum);files["profiles-a.tsv"]=payload+tail;
    restarted=user_profiles();assert(config_profiles_load(&restarted));custom.refresh_interval_ms=125;custom.crt_clarity=true;
    assert(config_profiles_apply(&restarted,0,&custom) && custom.refresh_interval_ms==125 && custom.crt_clarity);
    assert(config_profiles_selected(&restarted,&custom)==0);
    RTP_VIDEO_STATS net={};rtp_diagnostic_expected(&net,10,4,2,true);
    rtp_diagnostic_expected(&net,10,2,0,true); // second FEC block belongs to same frame
    rtp_diagnostic_accepted(&net,10,false,1024,1000,true);rtp_diagnostic_accepted(&net,10,true,1024,1500,true);
    rtp_diagnostic_accepted(&net,10,false,1024,9000,true);
    RTP_VIDEO_FRAME_STATS *nf=&net.frames[10%RTP_VIDEO_FRAME_SAMPLES];
    assert(net.packetCountVideo==6 && net.acceptedData==2 && net.packetCountFec==2 && net.acceptedFec==1);
    assert(nf->expectedData==6 && nf->expectedFec==2 && nf->acceptedData==2 && nf->acceptedFec==1 && nf->maxGapUs==7500 && nf->bytes==3072);
    assert(rtp_diagnostic_deficit(6,2)==4 && !rtp_diagnostic_deficit(2,6)); // no parity condition: deficit is still recorded
    rtp_diagnostic_expected(&net,10+RTP_VIDEO_FRAME_SAMPLES,3,0,true);assert(nf->expectedData==3 && !nf->acceptedData && !nf->firstUs);
    rtp_diagnostic_expected(&net,99,7,3,false);assert(!net.frames[99%RTP_VIDEO_FRAME_SAMPLES].valid);
    refresh_trace trace={};unsigned char y[64];memset(y,80,sizeof(y));
    for(unsigned i=0;i<100;i++){refresh_trace_frame *f=refresh_trace_begin(&trace,i,i%10?1000:4000,i%10==0,i*10000,300);refresh_trace_luma(f,y,8,8,8);assert(f->mean_y==80 && f->min_y==80 && f->max_y==80);}
    assert(trace.count==64 && trace.frames==100 && trace.idrs==10 && trace.idr_bytes==40000 && trace.all_bytes==130000);
    assert(gpu_time_percentile(&trace.idr_sizes,99)==4000);
    puts("PASS: Wireless/CRT presets, Smooth preservation, persistence, old v2 options, CRT display transaction rollback, independent 4:3 safe area, Rec.601, binding #10, link Unknown and bounded refresh diagnostics");
}
