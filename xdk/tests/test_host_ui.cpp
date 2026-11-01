/* Render the production UI. Synthetic art is original fixture data only. */
#include "../../src/ui/ui.h"
#include "../../src/ui/draw.h"
#include "../../src/settings_layout.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
void ui_present(){}
static void save(const char *name)
{
    ui_draw_software();canvas_t *c=ui_canvas();FILE *f=fopen(name,"wb");assert(f);
    fprintf(f,"P6\n%d %d\n255\n",c->w,c->h);
    for(int i=0;i<c->w*c->h;i++){unsigned char rgb[]={(unsigned char)(c->px[i]>>16),(unsigned char)(c->px[i]>>8),(unsigned char)c->px[i]};fwrite(rgb,1,3,f);}fclose(f);
}
static int art_commands(const uint32_t *pixels)
{
    int n,images=0;const ui_draw_command *c=ui_draw_commands(&n);assert(n<1024);
    for(int i=0;i<n;i++)if(c[i].kind==UI_IMAGE && c[i].pixels==pixels)images++;
    return images;
}
int main()
{
    for(int size=0;size<2;size++) {
        int w=size?1280:640,h=size?720:480;assert(ui_init(w,h));ui_draw_enable(true);ui_set_motion(false);ui_set_language(2);
        int aw,ah;ui_art_size(&aw,&ah);std::vector<uint32_t> art[6];
        const unsigned colors[]={0x355A4D,0x405C78,0x765B48,0x62587B,0x426B75,0x637044};
        for(int a=0;a<6;a++){art[a].resize(aw*ah);for(int y=0;y<ah;y++)for(int x=0;x<aw;x++)art[a][y*aw+x]=0xff000000u|colors[a]+((x/24+y/32)%2?0x080808:0);}
        ui_host host={"Gaming PC","192.168.1.87",HOST_PAIRED,NULL,NULL};
        const char *names[]={"Adventure One","Racing Night","Puzzle World","Arcade Two","Desktop","Exploration"};
        ui_app games[24];for(int i=0;i<24;i++)games[i]={names[i%6],art[i%6].data(),false,i==2,(unsigned)i+1};
        ui_menu_item items[9]={{"Play / Connect",NULL,ICON_PLAY,true}};
        for(int i=0;i<4;i++)items[i+1]={names[i+1],"Recent app",ICON_PLAY,true};
        items[5]={"Library","Games & apps",ICON_PLAY,true};items[6]={"Stream","Profiles & audio",ICON_MONITOR,true};
        items[7]={"Continue","Play now",ICON_PLAY,true};items[8]={"Settings","Hosts & advanced",ICON_GEAR,true};
        ui_dashboard dashboard={"Smooth","720p60",games+1,4,games,42};
        ui_draw_dashboard(&host,items,9,0,"192.168.1.90",&dashboard);assert(art_commands(art[0].data())==1);
        char path[100];snprintf(path,sizeof(path),"host-ui-%d-home.ppm",w);save(path);
        ui_draw_dashboard(&host,items,9,4,"192.168.1.90",&dashboard);snprintf(path,sizeof(path),"host-ui-%d-home-scroll.ppm",w);save(path);
        // Empty, single and long rails, missing/late/cached artwork and offline/online hosts.
        for(int scenario=0;scenario<6;scenario++) {
            int recents=scenario==0?0:scenario==1?1:8;
            ui_app rail[8];ui_menu_item home[13];home[0]=items[0];
            for(int i=0;i<recents;i++){rail[i]=games[i+1];if(scenario==3)rail[i].art=NULL;home[i+1]={rail[i].name,"Recent app",ICON_PLAY,true};}
            for(int i=0;i<4;i++)home[recents+1+i]=items[5+i];
            ui_host state=host;if(scenario==4)state.state=HOST_OFFLINE;
            ui_dashboard variant={"Bedroom TV","720p60 | 12 Mbps",rail,recents,scenario==0?NULL:games,42};
            int focus=0;
            ui_draw_dashboard(&state,home,recents+5,focus,"",&variant);
            int commands,images=0,peek=0;const ui_draw_command *draw=ui_draw_commands(&commands);assert(draw && commands<512);
            for(int j=0;j<commands;j++)if(draw[j].kind==UI_IMAGE && draw[j].iw==aw && draw[j].ih==ah) {
                images++;assert(draw[j].w>0 && draw[j].h>0);float aspect=draw[j].w/draw[j].h;assert(aspect>0.749f && aspect<0.751f);
                if(draw[j].x<draw[j].clip[2] && draw[j].x+draw[j].w>draw[j].clip[2])peek++;
            }
            assert(images<=7);if(scenario==2){
                assert(peek==1);snprintf(path,sizeof(path),"home-case-%d-peek.ppm",w);save(path);
                ui_draw_dashboard(&state,home,recents+5,8,"",&variant);
                float focus_x,focus_y,focus_w,focus_h;assert(ui_focus_bounds(&focus_x,&focus_y,&focus_w,&focus_h));assert(focus_x>=0 && focus_x+focus_w<=w);
            }
            snprintf(path,sizeof(path),"home-case-%d-%d.ppm",w,scenario);save(path);
        }
        ui_setting profiles[]={{"Smooth","1280x720 | 60 FPS | 12 Mbps","Next connection"},
            {"Performance","1280x720 | 60 FPS | 12 Mbps","Next connection"},
            {"Maximum Quality","1920x1080 | 30 FPS | 20 Mbps","Next connection"},
            {"Standard","1280x720 | 30 FPS | 12 Mbps","Next connection"},
            {"1080p (Experimental)","1920x1080 | 24 FPS | 12 Mbps","Experimental"},
            {"Bedroom TV","1280x720 | 60 FPS | 12 Mbps","Next connection"},
            {"Wi-Fi","1280x720 | 60 FPS | 10 Mbps","Next connection"},
            {"Save current as new profile","Save","Save all streaming parameters"}};
        ui_draw_profiles(profiles,8,5,5);snprintf(path,sizeof(path),"profiles-%d.ppm",w);save(path);
        ui_draw_confirm("Delete profile?","\"Bedroom TV\" will be permanently deleted.","Delete");snprintf(path,sizeof(path),"profile-delete-%d.ppm",w);save(path);
        ui_draw_library(&host,games,24,0,"All Apps");
        int n;const ui_draw_command *c=ui_draw_commands(&n);int covers=0,partial=0;
        for(int i=0;i<n;i++)if(c[i].kind==UI_IMAGE && c[i].iw==aw && c[i].ih==ah) {
            covers++;if(c[i].x<c[i].clip[2] && c[i].x+c[i].w>c[i].clip[2])partial++;
        }
        assert(covers<=8 && partial==1);snprintf(path,sizeof(path),"host-ui-%d-library.ppm",w);save(path);
        ui_set_motion(true);ui_library_tick(1000);ui_draw_library(&host,games,24,6,"All Apps");
        assert(ui_library_tick(1075));ui_draw_library(&host,games,24,6,"All Apps");
        assert(ui_library_tick(1150));ui_draw_library(&host,games,24,6,"All Apps");assert(!ui_library_tick(1151));
        float x,y,fw,fh;assert(ui_focus_bounds(&x,&y,&fw,&fh));assert(x>=0 && x+fw<=w);
        ui_set_motion(false);ui_set_settings_context(UI_STREAM,SETTINGS_VIDEO);
        ui_setting rows[]={{"Video resolution","1280x720","Requested host resolution."},
            {"Frames per second (FPS)","60 FPS","Smooth defaults to 60 FPS."},{"Video bitrate (Mbps)","12 Mbps",NULL},
            {"Automatic quality adjustment","Manual",NULL},{"V-Sync","On",NULL},{"Image quality","Fast",NULL},
            {"Save current profile","Save",NULL}};
        int kinds[]={UI_OPTION_CYCLE,UI_OPTION_CYCLE,UI_OPTION_CYCLE,UI_OPTION_CYCLE,UI_OPTION_TOGGLE,UI_OPTION_CYCLE,UI_OPTION_ACTION};
        ui_draw_settings_options("Video",rows,kinds,7,0);snprintf(path,sizeof(path),"host-ui-%d-settings-fullwidth.ppm",w);save(path);
        ui_draw_settings_options("Video",rows,kinds,7,1);snprintf(path,sizeof(path),"host-ui-%d-settings-values.ppm",w);save(path);
        ui_set_settings_context(-1,-1);char unavailable[128];snprintf(unavailable,sizeof(unavailable),ui_translate("%s unavailable"),"Gaming PC");ui_draw_host_failure(unavailable,"We couldn't find this PC on the local network.");
        snprintf(path,sizeof(path),"host-ui-%d-unavailable.ppm",w);save(path);
    }
    puts("PASS: production Home hero shares cached texture, real partial next card, bounded Library rendering, 150ms container interpolation, focused geometry, full-width settings and failure previews at 480p/720p.");
}
