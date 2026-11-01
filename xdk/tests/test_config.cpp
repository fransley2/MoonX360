#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <string>
#include "../../src/config.h"

static std::string ini;
static int writes;static bool allow_write=true;
char *storage_read(const char *, size_t *)
{
    if (ini.empty()) return NULL;
    char *p = (char *)malloc(ini.size()+1);
    memcpy(p, ini.c_str(), ini.size()+1);
    return p;
}
bool storage_write(const char *, const void *data, size_t n)
{
    if(!allow_write)return false;
    writes++;
    ini.assign((const char *)data, n);
    return true;
}

int main(void)
{
    config c, loaded;
    config_load(&c);
    assert(c.width == 1280 && c.height == 720 && c.fps == 60 && c.bitrate_kbps == 12000);
    assert(writes == 0 && !c.ui_motion);
    assert(c.quit_combo == 0 && c.stick_deadzone == 0 && !c.session_report);
    ini = "host=192.168.1.3\nwidth=1920\nheight=1080\nfps=60\nbitrate=14000\npacket_size=1392\nfast_decode=0\nrenderer=cpu\nvsync=0\nsops=0\nlocal_audio=1\n";
    config_load(&c);
    assert(c.width == 1920 && c.bitrate_kbps == 14000 && c.packet_size == 1392);
    assert(c.quit_combo == 0 && c.stick_deadzone == 0 && !c.session_report);
    assert(!c.fast_decode && !c.vsync && !c.sops && c.local_audio && c.renderer == 1);
    c.quit_combo = 1; c.stick_deadzone = 13; c.session_report = true;
    c.stats_overlay = true; c.overlay_mode=3; c.diagnostic_log = true;
    assert(config_save(&c));
    config_load(&loaded);
    assert(memcmp(&c, &loaded, sizeof(c)) == 0);
    for(int bg=0;bg<3;bg++)for(int accent=0;accent<4;accent++){
        c.ui_background=bg;c.ui_accent=accent;assert(config_save(&c));config_load(&loaded);
        assert(loaded.ui_background==bg && loaded.ui_accent==accent);
        config_apply_preset(&loaded,1);assert(loaded.ui_background==bg && loaded.ui_accent==accent);
    }
    ini="ui_background=-1\nui_accent=999\n";config_load(&loaded);assert(!loaded.ui_background && !loaded.ui_accent);
    ini="host=192.168.1.3\n";config_load(&loaded);assert(!loaded.ui_background && !loaded.ui_accent);
    for(int motion=0;motion<2;motion++){
        c.ui_motion=motion!=0;assert(config_save(&c));config_load(&loaded);assert(loaded.ui_motion==c.ui_motion);
    }
    ini="ui_motion=invalid\n";config_load(&loaded);assert(!loaded.ui_motion);
    ini="host=192.168.1.3\n";config_load(&loaded);assert(!loaded.ui_motion);
    config reset=c;strcpy(reset.host_uuid,"stable-uuid");reset.mouse_hud=2;reset.stream_menu_style=1;reset.ui_motion=true;
    config before_reset=reset;std::string before_ini=ini;allow_write=false;
    assert(!config_reset_preferences(&reset) && !memcmp(&reset,&before_reset,sizeof(reset)) && ini==before_ini);allow_write=true;
    assert(config_reset_preferences(&reset) && reset.bitrate_kbps==12000 && !reset.mouse_hud && !reset.stream_menu_style && !reset.ui_motion);
    assert(!strcmp(reset.host,before_reset.host) && !strcmp(reset.host_uuid,"stable-uuid"));
    for(int hud=0;hud<3;hud++)for(int style=0;style<2;style++){
        reset.mouse_hud=hud;reset.stream_menu_style=style;assert(config_save(&reset));config_load(&loaded);
        assert(loaded.mouse_hud==hud && loaded.stream_menu_style==style);
    }
    int packet = c.packet_size;
    for (int i=1; i<CONFIG_PROFILE_COUNT; i++) {
        config_apply_preset(&c, i);
        assert(config_video_preset(&c) == i);
        assert(c.quit_combo == 1 && c.stick_deadzone == 13 && c.session_report);
        assert(c.stats_overlay && c.diagnostic_log);
        assert(c.packet_size == packet && c.renderer == 1 && !c.vsync && !c.sops && c.local_audio);
        assert(strcmp(c.host, "192.168.1.3") == 0);
    }
    c.bitrate_kbps++;
    assert(config_video_preset(&c) == 0);
    config old = c;
    config_apply_preset(&c, -1);
    assert(memcmp(&old, &c, sizeof(c)) == 0);
    ini = "quit_combo=99\nstick_deadzone=99\n";
    config_load(&c);
    assert(c.quit_combo == 0 && c.stick_deadzone == 30);
    ini = "quit_combo=1\nstick_deadzone=-5\n";
    config_load(&c);
    assert(c.quit_combo == 1 && c.stick_deadzone == 0);
    static const int fps[] = {24, 30, 40, 50, 60};
    for (int i=0; i<5; i++) {
        assert(config_fps_supported(fps[i]));
        assert(config_cycle_fps(fps[i], 1) == fps[(i+1)%5]);
        assert(config_cycle_fps(fps[i], -1) == fps[(i+4)%5]);
        config_defaults(&c); c.fps = fps[i];
        assert(config_save(&c)); config_load(&loaded);
        assert(loaded.fps == fps[i] && loaded.bitrate_kbps == 12000);
        assert(!loaded.stats_overlay && !loaded.diagnostic_log);
    }
    for(const char *bad:{"fps=0\n","fps=90\n","fps=invalid\n"}) {
        ini=bad;int before=writes;config_load(&c);assert(c.fps==60 && writes==before);
        assert(config_save(&c));config_load(&loaded);assert(loaded.fps==60);
    }
    assert(!config_fps_supported(25) && config_cycle_fps(25,1) == 60 && config_cycle_fps(25,-1)==60);
    ini="host=old-pc\n";config_load(&c);assert(c.fps==60 && config_video_preset(&c)==CONFIG_PROFILE_SMOOTH);
    assert(config_save(&c));config_load(&loaded);assert(loaded.fps==60 && loaded.stream_profile==CONFIG_PROFILE_SMOOTH);
    ini.clear(); config_load(&c);
    assert(config_video_preset(&c) == CONFIG_PROFILE_SMOOTH);
    assert(c.fast_decode && c.renderer == 0 && c.vsync && c.slice_count == 0);
    static const int modes[6][4]={{0,0,0,0},{1280,720,60,12000},{854,480,60,25000},{1280,720,60,35000},{1280,720,30,12000},{1920,1080,24,8000}};
    for (int preset=1;preset<CONFIG_PROFILE_COUNT;preset++) {
        c.fast_decode=false;c.renderer=2;c.vsync=false;c.image_quality=2;c.slice_count=16;
        config expected=c;
        config_apply_preset(&c,preset);
        expected.width=modes[preset][0];expected.height=modes[preset][1];
        expected.fps=modes[preset][2];expected.bitrate_kbps=modes[preset][3];expected.stream_profile=preset;
        assert(!memcmp(&c,&expected,sizeof(c))); // all advanced/input/network options preserved
        assert(config_video_preset(&c)==preset);
        assert(config_save(&c));config_load(&loaded);
        assert(!memcmp(&c,&loaded,sizeof(c)));
        c.vsync=true;assert(config_video_preset(&c)==preset);c.vsync=false;
        c.renderer=0;assert(config_video_preset(&c)==preset);c.renderer=2;
        c.fast_decode=true;assert(config_video_preset(&c)==preset);c.fast_decode=false;
        c.bitrate_kbps++;assert(config_video_preset(&c)==0);c.bitrate_kbps--;
        config_apply_preset(&c,0);assert(config_video_preset(&c)==0);
        assert(c.width==modes[preset][0] && c.bitrate_kbps==modes[preset][3]);
        assert(config_save(&c));config_load(&loaded);assert(!memcmp(&c,&loaded,sizeof(c)));
    }
    // Editing a preset must keep its effective decoder and GPU settings.
    // Global input/appearance/diagnostics and all other options stay byte-identical.
    for(int preset=0;preset<CONFIG_PROFILE_COUNT;preset++)for(int fast=0;fast<2;fast++)for(int image=0;image<3;image++) {
        config_defaults(&c);c.fast_decode=fast!=0;c.image_quality=image;
        c.language=2;c.mouse_hud=1;c.diagnostic_log=true;strcpy(c.host,"preserved-pc");
        config_apply_preset(&c,preset);config before=c;
        stream_options effective,custom;config_profile_capture(&effective,&c);
        config_customize_stream(&c);config_profile_capture(&custom,&c);
        assert(!memcmp(&effective,&custom,sizeof(effective)) && c.stream_profile==0);
        before.stream_profile=0;before.fast_decode=effective.fast_decode!=0;before.image_quality=effective.image_quality;
        assert(!memcmp(&before,&c,sizeof(c)));
        c.bitrate_kbps=config_step_bitrate(c.bitrate_kbps,1);
        assert(c.fast_decode==before.fast_decode && c.image_quality==before.image_quality);
        assert(config_save(&c));config_load(&loaded);assert(!memcmp(&c,&loaded,sizeof(c)));
    }
    assert(!strcmp(config_preset_name(1),"Smooth"));
    assert(!strcmp(config_preset_name(2),"Performance"));
    assert(!strcmp(config_preset_name(4),"Standard"));
    assert(!strcmp(config_preset_name(0),"Custom"));
    static const int slices[]={0,8,12,16};
    for (int i=0;i<4;i++) {
        c.slice_count=slices[i];c.bitrate_kbps=70000;
        assert(config_save(&c));config_load(&loaded);
        assert(loaded.slice_count==slices[i] && loaded.bitrate_kbps==70000);
        assert(config_cycle_slices(slices[i],1)==slices[(i+1)%4]);
        assert(config_cycle_slices(slices[i],-1)==slices[(i+3)%4]);
    }
    ini="width=854\nheight=480\nfps=60\nbitrate=5000\nfast_decode=0\n";
    int before=writes;config_load(&c);
    assert(c.width==854 && c.height==480 && c.bitrate_kbps==5000 && !c.fast_decode);
    assert(writes==before && config_video_preset(&c)==0 && c.slice_count==0);
    ini="width=1920\nheight=1080\nfps=40\nbitrate=37654\nslices=16\n";
    config_load(&c);assert(c.fps==40 && c.bitrate_kbps==37654 && c.slice_count==16);
    assert(config_video_preset(&c)==0 && config_save(&c));config_load(&loaded);
    assert(memcmp(&c,&loaded,sizeof(c))==0);
    const char *invalid[]={"bitrate=70001\nslices=7\n","bitrate=100000\nslices=4294967304\n","bitrate=99999999999999999999999999\n"};
    for(int i=0;i<3;i++) { ini=invalid[i];config_load(&c);assert(c.bitrate_kbps==70000 && c.slice_count==0); }
    ini="bitrate=-100\n";config_load(&c);assert(c.bitrate_kbps==1000);
    assert(config_step_bitrate(29000,1)==30000);
    assert(config_step_bitrate(30000,1)==35000);
    assert(config_step_bitrate(35000,-1)==30000);
    assert(config_step_bitrate(30000,-1)==29000);
    assert(config_step_bitrate(70000,1)==70000);
    assert(config_step_bitrate(1000,-1)==1000);
    assert(config_step_bitrate(32000,-1)==30000);
    for(int rate=1000;rate<=70000;rate+=1000) {
        assert(config_step_bitrate(rate,1)>=1000 && config_step_bitrate(rate,1)<=70000);
        assert(config_step_bitrate(rate,-1)>=1000 && config_step_bitrate(rate,-1)<=70000);
    }
    config_defaults(&c);assert(!c.stats_overlay && c.fps==60 && c.bitrate_kbps==12000);
    static const int hd_rates[]={10000,15000,18000,25000},max_rates[]={12000,16000,20000,25000};
    for(int family=0;family<2;family++) {
        config_apply_preset(&c,1);if(family){c.width=1920;c.height=1080;}
        const int *rates=family?max_rates:hd_rates;
        for(int i=0;i<4;i++) {
            c.bitrate_kbps=rates[i];config original=c;
            assert(config_test_bitrate(&c,1)==rates[(i+1)%4]);
            assert(config_test_bitrate(&c,-1)==rates[(i+3)%4]);
            assert(!memcmp(&original,&c,sizeof(c))); // selector does not connect or mutate config
        }
    }
    ini="stats_overlay=1\n";config_load(&loaded);assert(config_overlay_mode(&loaded)==3);
    for(int mode=0;mode<4;mode++) {
        loaded.overlay_mode=mode;loaded.stats_overlay=mode!=0;loaded.image_quality=2;loaded.history_enabled=false;
        assert(config_save(&loaded));config_load(&c);
        assert(config_overlay_mode(&c)==mode && c.image_quality==2 && !c.history_enabled);
    }
    ini="overlay_mode=99\nimage_quality=99\n";config_load(&loaded);
    assert(!loaded.stats_overlay && loaded.image_quality==0 && loaded.history_enabled);
    config_defaults(&c);assert(c.history_enabled && config_overlay_mode(&c)==0 && c.image_quality==0);
    c.image_quality=1;assert(config_video_preset(&c)==CONFIG_PROFILE_SMOOTH);config_apply_preset(&c,CONFIG_PRESET_720_60);
    assert(c.image_quality==1 && config_video_preset(&c)==CONFIG_PRESET_720_60);
    assert(c.mdns_discovery && !strcmp(config_profile_key(c.stream_profile),"smooth"));
    ini="width=1280\nheight=720\nfps=30\nbitrate=30000\nfast_decode=0\n";config_load(&c);
    assert(c.fps==30 && c.bitrate_kbps==30000 && !c.fast_decode && c.stream_profile==0 && c.mdns_discovery);
    ini="stream_profile=balanced\nwidth=854\nheight=480\nfps=60\nbitrate=40000\nmdns_discovery=false\nperformance_overlay=off\n";
    config_load(&c);assert(c.width==854 && c.height==480 && c.bitrate_kbps==40000 && c.stream_profile==0 && !c.mdns_discovery && !c.stats_overlay);
    ini="stream_profile=performance\nwidth=854\nheight=480\nfps=60\nbitrate=25000\nperformance_overlay=detailed\n";
    config_load(&c);assert(c.stream_profile==2 && config_overlay_mode(&c)==2);
    assert(config_save(&c));assert(ini.find("stream_profile=performance")!=std::string::npos);
    assert(ini.find("mdns_discovery=true")!=std::string::npos && ini.find("performance_overlay=detailed")!=std::string::npos);
    config_load(&loaded);assert(!memcmp(&c,&loaded,sizeof(c)));
    ini="stick_deadzone=12\nwidth=1280\nheight=720\nfps=60\nbitrate=25000\nstream_profile=smooth\n";
    config_load(&c);assert(c.left_min==12 && c.right_min==12 && c.left_max==100);
    assert(c.bitrate_kbps==25000 && c.stream_profile==0); // old saved settings never overwritten
    ini="stick_deadzone=12\nleft_min=3\nleft_max=95\n";
    config_load(&c);assert(c.left_min==3 && c.right_min==12 && c.left_max==95 && c.right_max==100);
    ini="stick_deadzone=14\nright_min=0\nright_max=90\n";
    config_load(&c);assert(c.left_min==14 && c.right_min==0 && c.left_max==100 && c.right_max==90);
    ini="stick_deadzone=9\nleft_max=80\nright_max=75\n";
    config_load(&c);assert(c.left_min==9 && c.right_min==9 && c.left_max==80 && c.right_max==75);
    ini="left_min=-1\nright_min=80\nleft_max=0\nright_max=200\ndisplay_mode=99\ndecode_workers=99\n";
    config_load(&c);assert(c.left_min==0 && c.right_min==30 && c.left_max==50 && c.right_max==100);
    assert(c.display_mode==0 && c.decode_workers==4);
    c.language=1;c.adaptive_quality=1;c.decode_workers=3;c.display_mode=2;c.thread_layout=0;
    c.left_min=8;c.left_max=93;c.right_min=14;c.right_max=88;
    assert(config_save(&c));config_load(&loaded);assert(!memcmp(&c,&loaded,sizeof(c)));
    for(int language=0;language<CONFIG_LANGUAGE_COUNT;language++) {
        c.language=language;assert(config_save(&c));config_load(&loaded);
        assert(loaded.language==language && !memcmp(&c,&loaded,sizeof(c)));
    }
    ini="language=1\n";config_load(&c);assert(c.language==CONFIG_LANGUAGE_SPANISH);
    ini="language=2\n";config_load(&c);assert(c.language==CONFIG_LANGUAGE_PT_BR);
    ini="language=-1\n";config_load(&c);assert(c.language==CONFIG_LANGUAGE_ENGLISH);
    ini="language=3\n";config_load(&c);assert(c.language==CONFIG_LANGUAGE_ENGLISH);
    ini="stream_profile=1080_smooth\nwidth=1920\nheight=1080\nfps=60\nbitrate=20000\nfast_decode=0\nimage_quality=2\n";
    config_load(&c);assert(c.fps==60 && c.bitrate_kbps==20000 && !c.fast_decode && c.image_quality==2 && config_video_preset(&c)==0);
    config_apply_preset(&c,CONFIG_PROFILE_1080_SMOOTH);assert(c.fps==24 && c.bitrate_kbps==8000);
    assert(!strcmp(config_profile_key(CONFIG_PROFILE_1080_SMOOTH),"1080_smooth"));
    assert(!strcmp(config_preset_name(CONFIG_PROFILE_1080_SMOOTH),"1080p (Experimental)"));
    assert(config_save(&c));config_load(&loaded);assert(config_video_preset(&loaded)==CONFIG_PROFILE_1080_SMOOTH);
    ini="stream_profile=smooth\nwidth=1280\nheight=720\nfps=60\nbitrate=18000\n";config_load(&c);assert(c.bitrate_kbps==18000 && config_video_preset(&c)==0);
    config_apply_preset(&c,CONFIG_PROFILE_SMOOTH);assert(c.bitrate_kbps==12000 && c.fps==60);
    puts("PASS: old INI preservation, new defaults, save/load, bounds, preset isolation and custom values.");
}
