#include "../../src/ui/ui.h"
#include "../../src/ui/draw.h"
#include "../../src/launch_cover.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <chrono>

void ui_present(void) {}
static ui_host host = { "Gaming PC", "192.168.1.3", HOST_PAIRED, "Desktop", "Sunshine" };
static ui_menu_item items[] = {
    { "Start streaming", "Choose a game or app", ICON_PLAY, true },
    { "Pair with host", "Already paired", ICON_LINK, false },
    { "Host address", "192.168.1.3", ICON_MONITOR, true },
    { "Settings", "1280x720 - 60 FPS - 30 Mbps", ICON_GEAR, true },
    { "Quit running app", "Desktop is running", ICON_STOP, true },
    { "Refresh", "Check the host's status again", ICON_REFRESH, true },
    { "Unpair", "Forget this console on the host", ICON_UNLINK, true }
};
static const char *ip = "192.168.1.5";
#ifndef BASELINE
static void compare_fresh(int sel)
{
    ui_draw_main(&host, items, 7, sel, ip, 0);
    canvas_t *cv = ui_canvas();
    std::vector<uint32_t> cached(cv->px, cv->px + cv->w * cv->h);
    ui_drop_main_cache();
    ui_draw_main(&host, items, 7, sel, ip, 0);
    assert(memcmp(cached.data(), cv->px, cached.size() * sizeof(uint32_t)) == 0);
}
#endif
static void save_preview(const char *name) {
    FILE *f=fopen(name,"wb");assert(f);canvas_t *cv=ui_canvas();fprintf(f,"P6\n%d %d\n255\n",cv->w,cv->h);
    for(int i=0;i<cv->w*cv->h;i++) {unsigned char rgb[]={(unsigned char)(cv->px[i]>>16),(unsigned char)(cv->px[i]>>8),(unsigned char)cv->px[i]};fwrite(rgb,1,3,f);}fclose(f);
}
int main(void)
{
    assert(ui_init(1280, 720));
    ui_host original_host = host;
    ui_menu_item original_items[7];
    memcpy(original_items, items, sizeof(items));
#ifndef BASELINE
    for (int i = 0; i < 7; i++) compare_fresh(i);
    host.name = "New PC"; compare_fresh(1);
    host.address = "192.168.1.9"; compare_fresh(2);
    host.state = HOST_OFFLINE; compare_fresh(3);
    host.running_app = NULL; compare_fresh(4);
    host.detail = "Connection refused"; compare_fresh(5);
    items[0].subtitle = "Host not reachable"; compare_fresh(6);
    items[0].enabled = false; compare_fresh(0);
    items[1].icon = ICON_ERROR; compare_fresh(1);
    ip = "192.168.1.6"; compare_fresh(2);
    ui_set_exit_shortcut(1); compare_fresh(3);
    ui_set_exit_shortcut(0); compare_fresh(4);
    puts("PASS: cached and fresh home pixels match for all selections and changed host, items, IP and shortcut.");
#endif
    host=original_host;memcpy(items,original_items,sizeof(items));ip="192.168.1.5";
    ui_app recent[4]={{"Desktop",NULL,false,false},{"Racing Game",NULL,false,true},{"Adventure",NULL,false,false},{"Platform Game",NULL,false,false}};
    ui_menu_item dashboard_items[14];memcpy(dashboard_items,items,sizeof(items));
    dashboard_items[0].title="CONTINUE";dashboard_items[0].subtitle="Desktop is running on your PC";
    dashboard_items[7]={"Sessions","Recent sessions",ICON_INFO,true};
    dashboard_items[8]={"Performance Advisor","Good",ICON_INFO,true};
    dashboard_items[9]={"All Apps","Open library",ICON_PLAY,true};
    for(int i=0;i<4;i++)dashboard_items[10+i]={recent[i].name,"Recent",ICON_PLAY,true};
    ui_dashboard dashboard={"720p60 Quality","Good",recent,4};
    assert(ui_dashboard_move(0,-1,0,13)==0);
    assert(ui_dashboard_move(9,1,0,13)==10);
    bool reached[14]={};reached[0]=true;
    for(int pass=0;pass<14;pass++)for(int i=0;i<14;i++)if(reached[i]) {
        reached[ui_dashboard_move(i,1,0,14)]=true;reached[ui_dashboard_move(i,-1,0,14)]=true;
        reached[ui_dashboard_move(i,0,1,14)]=true;reached[ui_dashboard_move(i,0,-1,14)]=true;
    }
    for(int i=0;i<14;i++)assert(reached[i]);

    for(int selection=0;selection<14;selection++) {
        ui_draw_dashboard(&host,dashboard_items,14,selection,ip,&dashboard);
        canvas_t *c=ui_canvas();std::vector<uint32_t> cached(c->px,c->px+c->w*c->h);
        ui_drop_main_cache();ui_draw_dashboard(&host,dashboard_items,14,selection,ip,&dashboard);
        assert(!memcmp(cached.data(),c->px,cached.size()*sizeof(uint32_t)));
    }
    ui_draw_dashboard(&host,dashboard_items,14,0,ip,&dashboard);save_preview("dashboard-fixture.ppm");
    ui_draw_enable(true);ui_draw_dashboard(&host,dashboard_items,14,0,ip,&dashboard);
    int command_count;assert(ui_draw_commands(&command_count) && command_count>0);
    ui_draw_software();save_preview("gpu-command-home-fixture.ppm");ui_draw_enable(false);ui_drop_main_cache();
    ui_draw_library(&host,recent,3,1,"All Apps");save_preview("library-fixture.ppm");
    ui_stat advice[3]={{"Connection","Unstable"},{"Likely bottleneck","CPU video decoding"},{"Recommended profile","720p60 Quality / 30 Mbps"}};
    ui_draw_advisor(advice,3,"Apply changes only the next connection. Your current session is already stopped.",true);save_preview("advisor-fixture.ppm");
    ui_setting sessions[2]={{"Racing Game","720p60 | 30 Mbps | 59 FPS | Good","2026-10-25 UTC | Gaming PC | Tail metrics available"},
        {"Desktop","1080p60 | 60 Mbps | 27 FPS | Unstable","CPU video decoding"}};
    ui_draw_list("Recent Sessions",sessions,2,0);save_preview("sessions-fixture.ppm");
    puts("PASS: dashboard cache across all actions; library favorite tile, history and explicit Advisor previews (synthetic fixtures).");
    ui_menu_item simple[]={
        {"Resume","Desktop",ICON_PLAY,true},
        {"Pair with host","Paired",ICON_LINK,false},
        {"GAMING-PC","192.168.1.100 | Online / Paired",ICON_MONITOR,true},
        {"SECOND-PC","192.168.1.101 | Offline",ICON_MONITOR,true},
        {"Add Host Manually","IPv4 address",ICON_MONITOR,true}
    };
    ui_host simple_host={"GAMING-PC","192.168.1.100",HOST_PAIRED,"Desktop",NULL};
    ui_dashboard simple_dashboard={"1280x720 | 60 FPS | 30 Mbps",NULL,NULL,0};
    ui_draw_enable(true);ui_draw_dashboard(&simple_host,simple,5,0,NULL,&simple_dashboard);
    ui_draw_commands(&command_count);printf("Simple Home GPU commands: %d\n",command_count);
    assert(command_count<400);ui_draw_software();save_preview("simple-home.ppm");ui_draw_enable(false);ui_drop_main_cache();
    // Native Series layout uses four destinations, real recent covers and four shortcuts.
    ui_menu_item tiles[13];
    const char *navigation[]={"Home","Library","Computers","Settings"};
    for(int i=0;i<4;i++)tiles[i]={navigation[i],NULL,ICON_INFO,true};
    tiles[4]={"Continue stream","Desktop",ICON_PLAY,true};
    std::vector<uint32_t> art[4];
    const char *game_names[]={"GTA San Andreas","PCSX2 Big Picture","Steam Big Picture","Desktop"};
    for(int i=0;i<4;i++) {
        art[i].resize(150*200,0xFF294553);char path[64];snprintf(path,sizeof(path),"preview-cover-%d.argb",i);
        FILE *f=fopen(path,"rb");if(f){assert(fread(art[i].data(),4,150*200,f)==150*200);fclose(f);}
        recent[i].name=game_names[i];recent[i].art=art[i].data();recent[i].generation=10+i;
        tiles[5+i]={game_names[i],"Recent",ICON_PLAY,true};
    }
    const char *shortcuts[]={"My Library","Stream Settings","Recent Sessions","Stop app"};
    const int shortcut_icons[]={ICON_PLAY,ICON_MONITOR,ICON_INFO,ICON_STOP};
    for(int i=0;i<4;i++)tiles[9+i]={shortcuts[i],i==3?"Desktop":NULL,shortcut_icons[i],true};
    dashboard.profile="Smooth";dashboard.connection="720p60  \xE2\x80\xA2  25 Mbps";
    for(int r=0;r<=4;r++) {
        int total=9+r;bool visited[13]={};visited[0]=true;
        for(int pass=0;pass<13;pass++)for(int i=0;i<total;i++)if(visited[i]) {
            visited[ui_dashboard_move_tiles(i,1,0,total,r)]=true;visited[ui_dashboard_move_tiles(i,-1,0,total,r)]=true;
            visited[ui_dashboard_move_tiles(i,0,1,total,r)]=true;visited[ui_dashboard_move_tiles(i,0,-1,total,r)]=true;
        }
        for(int i=0;i<total;i++)assert(visited[i]);
    }
    ui_app running=recent[3];running.running=true;
    dashboard.recent_count=4;dashboard.resume=&running;dashboard.resume_id=45;
    dashboard.featured=&running;dashboard.featured_id=45;
    ui_set_motion(false);
    for(int i=0;i<13;i++) {
        ui_draw_dashboard(&host,tiles,13,i,ip,&dashboard);
        canvas_t *c=ui_canvas();std::vector<uint32_t> pixels(c->px,c->px+c->w*c->h);
        ui_drop_main_cache();ui_draw_dashboard(&host,tiles,13,i,ip,&dashboard);
        assert(!memcmp(pixels.data(),c->px,pixels.size()*4));
    }
    ui_draw_enable(true);
    for(int selection=0;selection<13;selection++) {
        ui_draw_dashboard(&host,tiles,13,selection,ip,&dashboard);
        const ui_draw_command *cmd=ui_draw_commands(&command_count);assert(cmd);
        std::vector<ui_draw_command> cached(cmd,cmd+command_count);
        ui_drop_main_cache();ui_draw_dashboard(&host,tiles,13,selection,ip,&dashboard);
        cmd=ui_draw_commands(&command_count);assert(cmd && cached.size()==(size_t)command_count);
        assert(!memcmp(cached.data(),cmd,cached.size()*sizeof(*cmd)));
    }
    auto gpu_started=std::chrono::steady_clock::now();
    for(int i=0;i<140;i++)ui_draw_dashboard(&host,tiles,13,i%13,ip,&dashboard);
    double gpu_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-gpu_started).count()/140;
    printf("Retained GPU scene CPU recording: %.4f ms/navigation (desktop; excludes GPU/present).\n",gpu_ms);
    for(int selected=0;selected<4;selected++) {
        ui_draw_dashboard(&host,tiles,13,5+selected,ip,&dashboard);
        const ui_draw_command *cmd=ui_draw_commands(&command_count);assert(cmd);
        for(int j=0;j<4;j++) {float size=0;
            for(int k=0;k<command_count;k++)if(cmd[k].kind==UI_IMAGE && cmd[k].pixels==recent[j].art)size=cmd[k].w;
            assert(size==(j==selected?164.0f:106.0f));}
    }
    ui_app all_recent[8];ui_menu_item all_tiles[17];std::vector<uint32_t> extra[4];
    const char *extra_names[]={"Game five","Game six","Game seven","Game eight"};
    for(int i=0;i<5;i++)all_tiles[i]=tiles[i];
    for(int i=0;i<8;i++) {
        if(i<4)all_recent[i]=recent[i];
        else {extra[i-4].resize(150*200,0xFF3B5260);all_recent[i]={extra_names[i-4],extra[i-4].data(),false,false,(uint32_t)(20+i)};}
        all_tiles[5+i]={all_recent[i].name,NULL,ICON_PLAY,true};
    }
    for(int i=0;i<4;i++)all_tiles[13+i]=tiles[9+i];
    ui_dashboard full_dashboard=dashboard;full_dashboard.recent=all_recent;full_dashboard.recent_count=8;
    for(int selected=0;selected<8;selected++) {
        ui_draw_dashboard(&host,all_tiles,17,5+selected,ip,&full_dashboard);
        const ui_draw_command *cmd=ui_draw_commands(&command_count);assert(cmd);
        float last_right=64;
        for(int j=0;j<8;j++) {const ui_draw_command *image=NULL;
            for(int k=0;k<command_count;k++)if(cmd[k].kind==UI_IMAGE && cmd[k].pixels==all_recent[j].art)image=cmd+k;
            assert(image && image->w==(j==selected?164.0f:106.0f));
            assert(image->x>=last_right && image->x+image->w<=1216);
            last_right=image->x+image->w;
        }
    }
    ui_draw_dashboard(&host,tiles,13,4,ip,&dashboard);
    const ui_draw_command *cmd=ui_draw_commands(&command_count);assert(cmd && command_count<400);
    ui_draw_software();save_preview("series-dashboard-refined.ppm");
    ui_draw_dashboard(&host,tiles,13,5,ip,&dashboard);ui_draw_software();save_preview("series-dashboard-recent.ppm");
    ui_draw_dashboard(&host,tiles,13,6,ip,&dashboard);ui_draw_software();save_preview("series-dashboard-tile2.ppm");
    dashboard.featured=recent;dashboard.featured_id=42;dashboard.resume=NULL;dashboard.resume_id=0;
    tiles[4]={"Start playing","GTA San Andreas",ICON_PLAY,true};
    tiles[12]={"My Computers",NULL,ICON_MONITOR,true};
    ui_draw_dashboard(&host,tiles,13,4,ip,&dashboard);ui_draw_software();save_preview("series-dashboard-last-played.ppm");
    dashboard.featured=&running;dashboard.featured_id=45;dashboard.resume=&running;dashboard.resume_id=45;
    tiles[4]={"Continue stream","Desktop",ICON_PLAY,true};
    tiles[12]={"Stop app","Desktop",ICON_STOP,true};
    ui_set_appearance(4,0);auto wall_started=std::chrono::steady_clock::now();double max_slice_ms=0;
    for(int i=0;i<30;i++){
        auto slice=std::chrono::steady_clock::now();
        bool ready=ui_home_prepare_cover("uuid:PC",recent+1,1000);
        double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-slice).count();
        if(ms>max_slice_ms)max_slice_ms=ms;
        assert(ready==(i==29));
    }
    printf("Cover wallpaper CPU preparation: %.3f ms total, %.3f ms largest slice (desktop; excludes GPU upload).\n",
        std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-wall_started).count(),max_slice_ms);
    ui_draw_dashboard(&host,tiles,13,6,ip,&dashboard);ui_draw_software();save_preview("series-dashboard-wallpaper.ppm");
    assert(!ui_home_prepare_cover("uuid:PC",recent+1,1200));
    ui_app missing={"Missing",NULL,false,false,0};assert(!ui_home_prepare_cover("uuid:PC",&missing,1250));
    assert(!ui_home_prepare_cover("uuid:PC",recent+2,1300));
    ui_draw_dashboard(&host,tiles,13,7,ip,&dashboard);
    cmd=ui_draw_commands(&command_count);int before_ready=0;
    for(int i=0;i<command_count;i++)if(cmd[i].kind==UI_IMAGE && cmd[i].iw==1280 && cmd[i].ih==720)before_ready++;
    assert(before_ready==1); /* Old wallpaper stays complete during preparation. */
    for(int i=1;i<30;i++)assert(ui_home_prepare_cover("uuid:PC",recent+2,1300)==(i==29));
    ui_set_motion(true);ui_home_tick(1300);
    ui_draw_dashboard(&host,tiles,13,7,ip,&dashboard);
    cmd=ui_draw_commands(&command_count);int backgrounds=0;
    for(int i=0;i<command_count;i++)if(cmd[i].kind==UI_IMAGE && cmd[i].iw==1280 && cmd[i].ih==720)backgrounds++;
    assert(backgrounds==2);ui_home_tick(1500);ui_draw_dashboard(&host,tiles,13,7,ip,&dashboard);
    cmd=ui_draw_commands(&command_count);backgrounds=0;
    for(int i=0;i<command_count;i++)if(cmd[i].kind==UI_IMAGE && cmd[i].iw==1280 && cmd[i].ih==720)backgrounds++;
    assert(backgrounds==1);
    std::vector<uint32_t> custom(1280*720);
    for(int y=0;y<720;y++)for(int x=0;x<1280;x++)custom[y*1280+x]=(x&1)?0xFFF4A431u:0xFF1768C1u;
    ui_home_set_user_wallpaper(custom.data(),1280,720);ui_set_appearance(3,0);
    ui_draw_library(&host,recent,4,0,"All Apps");
    cmd=ui_draw_commands(&command_count);bool retained_custom=false;
    for(int i=0;i<command_count;i++)if(cmd[i].kind==UI_IMAGE && cmd[i].iw==1280 && cmd[i].ih==720) {
        const uint32_t *wall=(const uint32_t*)cmd[i].pixels;
        assert(wall[100*1280+100]!=wall[100*1280+101]);retained_custom=true;
    }
    assert(retained_custom); /* Library shares the sharp custom wallpaper. */
    ui_set_appearance(0,0);
    // A different screen may replace active commands; the retained Home remains valid.
    ui_draw_library(&host,recent,4,0,"All Apps");
    ui_draw_dashboard(&host,tiles,13,0,ip,&dashboard);assert(ui_draw_commands(&command_count));
    assert(ui_scene_restore(ui_canvas()));int base_count;ui_draw_commands(&base_count);
    assert(base_count<command_count); // focus was appended, never stored in the static scene
    ui_drop_main_cache();assert(!ui_scene_restore(ui_canvas()));
    ui_setting host_rows[]={{"* Gaming PC","Online / Paired","192.168.1.3"},
        {"Bedroom PC","Offline","192.168.1.9"},{"+ Add host manually","IPv4","Known IP"},
        {"Host actions","Open","Refresh / pair / quit / unpair"}};
    ui_draw_hosts(host_rows,4,0,UI_HOME,true);ui_draw_software();save_preview("series-dashboard-hosts.ppm");
    ui_draw_enable(false);ui_drop_main_cache();
    puts("PASS: four destinations, 0-8 recents, every selected tile growth without overlap, wallpaper replacement and CPU/GPU cache equivalence.");
    ui_draw_enable(true);ui_set_page(UI_LIBRARY);
    int root=ui_location_enter("GTA San Andreas");
    char path[800];ui_location_path(path,sizeof(path),NULL,NULL);
    assert(!strcmp(path,"Library  >  GTA San Andreas"));
    int child=ui_location_enter("Stream");
    ui_location_path(path,sizeof(path),NULL,NULL);
    assert(!strcmp(path,"Library  >  GTA San Andreas  >  Stream"));
    ui_setting stream_rows[]={{"Stream Profile","Smooth","Applied on next connection."},
        {"Optimize game settings","On","Allow the host to optimize the game."},{"Audio","Off","Also play on the PC."}};
    int stream_kinds[]={UI_OPTION_CYCLE,UI_OPTION_TOGGLE,UI_OPTION_TOGGLE};
    ui_draw_settings_options("Stream",stream_rows,stream_kinds,3,1);ui_draw_software();save_preview("navigation-stream-options.ppm");
    ui_location_restore(child);ui_location_path(path,sizeof(path),NULL,NULL);
    assert(!strcmp(path,"Library  >  GTA San Andreas"));
    recent[0].running=true;
    ui_draw_app_details(&host,recent,"Smooth","5 s",4);ui_draw_software();save_preview("navigation-stop-app.ppm");
    ui_location_restore(root);
    ui_set_page(UI_SETTINGS);root=ui_location_enter("Settings");
    ui_location_path(path,sizeof(path),"Developer","Renderer");
    assert(!strcmp(path,"Settings  >  Developer  >  Renderer"));
    ui_setting renderer_rows[]={{"Renderer","GPU","Choose the renderer for the next session."},
        {"Image quality","Auto Quality","Change the renderer quality policy."},{"V-Sync","On","Synchronize presentation."}};
    int renderer_kinds[]={UI_OPTION_CYCLE,UI_OPTION_CYCLE,UI_OPTION_TOGGLE};
    ui_draw_settings_options("Renderer",renderer_rows,renderer_kinds,3,0);ui_draw_software();save_preview("navigation-renderer-options.ppm");
    ui_launch_begin(recent,1000,true);ui_launch_session("Smooth",1280,720,60,12000);
    ui_draw_software();save_preview("launch-native-preview.ppm");
    // Nested screens retain the same compact top navigation as Home.
    ui_draw_settings_options("Stream",stream_rows,stream_kinds,3,0);
    const ui_draw_command *tab_cmd=ui_draw_commands(&command_count);assert(tab_cmd);
    bool underline=false;
    for(int i=0;i<command_count;i++)if(tab_cmd[i].kind==UI_RECT && tab_cmd[i].y==101 && tab_cmd[i].h==2) {
        assert(tab_cmd[i].w==20);underline=true;
    }
    assert(underline);
    ui_set_page(UI_SETTINGS);
    int section=ui_location_enter("Developer");ui_location_enter("Network");
    ui_stat net_rows[]={{"Incoming payload","12 Mbps"},{"RTT","8 ms"},{"Drops","0"}};
    ui_draw_stats("Network diagnostics",net_rows,3,"Last stopped session. No live stream is running.");
    ui_draw_software();save_preview("navigation-network-details.ppm");
    ui_location_restore(section);ui_location_restore(root);ui_draw_enable(false);ui_drop_main_cache();
    puts("PASS: nested breadcrumb restore, explicit option kinds, Stop app details and shared top navigation.");
    // Same workload is also compiled against the preceding UI for comparison.
    host = original_host;
    memcpy(items, original_items, sizeof(items));
    ip = "192.168.1.5";
    ui_draw_main(&host, items, 7, 0, ip, 0);
    auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < 140; i++) ui_draw_main(&host, items, 7, i % 7, ip, 0);
    auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    printf("Desktop CPU draw: %.3f ms per navigation redraw (140 redraws; excludes GPU/present).\n", elapsed / 140);
#ifndef BASELINE
    ui_drop_main_cache();
#endif
}
