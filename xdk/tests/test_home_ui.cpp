#include "../../src/ui/ui.h"
#include "../../src/ui/draw.h"
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
    // Refined Home contains only host, four covers and four quick-access tiles.
    ui_menu_item tiles[9];tiles[0]={"Play / Connect",NULL,ICON_PLAY,true};
    std::vector<uint32_t> art[4];
    const char *game_names[]={"GTA San Andreas","PCSX2 Big Picture","Steam Big Picture","Desktop"};
    for(int i=0;i<4;i++) {
        art[i].resize(150*200,0xFF294553);char path[64];snprintf(path,sizeof(path),"preview-cover-%d.argb",i);
        FILE *f=fopen(path,"rb");if(f){assert(fread(art[i].data(),4,150*200,f)==150*200);fclose(f);}
        recent[i].name=game_names[i];recent[i].art=art[i].data();recent[i].generation=10+i;
        tiles[1+i]={game_names[i],"Recent",ICON_PLAY,true};
    }
    const char *shortcuts[]={"Library","Stream","Continue","Settings"};
    for(int i=0;i<4;i++)tiles[5+i]={shortcuts[i],i==2?"Play now":"Open",ICON_GEAR,true};
    dashboard.profile="Smooth";dashboard.connection="720p60  \xE2\x80\xA2  25 Mbps";
    for(int r=0;r<=4;r++) {
        int total=1+r+4;bool visited[9]={};visited[0]=true;
        for(int pass=0;pass<9;pass++)for(int i=0;i<total;i++)if(visited[i]) {
            visited[ui_dashboard_move_tiles(i,1,0,total,r)]=true;visited[ui_dashboard_move_tiles(i,-1,0,total,r)]=true;
            visited[ui_dashboard_move_tiles(i,0,1,total,r)]=true;visited[ui_dashboard_move_tiles(i,0,-1,total,r)]=true;
        }
        for(int i=0;i<total;i++)assert(visited[i]);
    }
    dashboard.recent_count=4;
    std::vector<uint32_t> resume_art(150*200,0xFF5C3450);
    ui_app resume={"Another game",resume_art.data(),true,false,90};
    dashboard.resume=&resume;dashboard.resume_id=999;
    for(int i=0;i<9;i++) {
        ui_draw_dashboard(&host,tiles,9,i,ip,&dashboard);
        canvas_t *c=ui_canvas();std::vector<uint32_t> pixels(c->px,c->px+c->w*c->h);
        ui_drop_main_cache();ui_draw_dashboard(&host,tiles,9,i,ip,&dashboard);
        assert(!memcmp(pixels.data(),c->px,pixels.size()*4));
    }
    ui_draw_enable(true);
    for(int selection=0;selection<9;selection++) {
        ui_draw_dashboard(&host,tiles,9,selection,ip,&dashboard);
        const ui_draw_command *cmd=ui_draw_commands(&command_count);assert(cmd);
        std::vector<ui_draw_command> cached(cmd,cmd+command_count);
        ui_drop_main_cache();ui_draw_dashboard(&host,tiles,9,selection,ip,&dashboard);
        cmd=ui_draw_commands(&command_count);assert(cmd && cached.size()==(size_t)command_count);
        assert(!memcmp(cached.data(),cmd,cached.size()*sizeof(*cmd)));
    }
    auto gpu_started=std::chrono::steady_clock::now();
    for(int i=0;i<140;i++)ui_draw_dashboard(&host,tiles,9,i%9,ip,&dashboard);
    double gpu_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-gpu_started).count()/140;
    printf("Retained GPU scene CPU recording: %.4f ms/navigation (desktop; excludes GPU/present).\n",gpu_ms);
    ui_draw_dashboard(&host,tiles,9,1,ip,&dashboard);
    const ui_draw_command *cmd=ui_draw_commands(&command_count);assert(cmd);
    int images=0,resume_images=0;for(int i=0;i<command_count;i++) {
        assert(cmd[i].kind!=UI_GRADIENT);
        if(cmd[i].kind==UI_IMAGE && cmd[i].pixels==recent[0].art)images++;
        if(cmd[i].kind==UI_IMAGE && cmd[i].pixels==resume.art)resume_images++;
    }
    assert(images==1 && resume_images==1); // each distinct game uses its own matching cover, once
    printf("Refined Home GPU commands: %d (four covers, Continue, no host list)\n",command_count);
    assert(command_count<400);
    ui_draw_software();save_preview("series-dashboard-refined.ppm");
    ui_draw_dashboard(&host,tiles,9,7,ip,&dashboard);ui_draw_software();save_preview("series-dashboard-continue.ppm");
    // Changing Continue (including identical names with different IDs) and
    // clearing it must invalidate retained content. Compare against fresh UI.
    resume.name="New target";dashboard.resume_id=1000;
    ui_draw_dashboard(&host,tiles,9,7,ip,&dashboard);ui_draw_software();
    canvas_t *changed=ui_canvas();std::vector<uint32_t> target_pixels(changed->px,changed->px+changed->w*changed->h);
    ui_drop_main_cache();ui_draw_dashboard(&host,tiles,9,7,ip,&dashboard);ui_draw_software();
    assert(!memcmp(target_pixels.data(),changed->px,target_pixels.size()*4));
    dashboard.resume=NULL;dashboard.resume_id=0;
    ui_draw_dashboard(&host,tiles,9,7,ip,&dashboard);ui_draw_software();
    assert(memcmp(target_pixels.data(),changed->px,target_pixels.size()*4));
    std::vector<uint32_t> cleared(changed->px,changed->px+changed->w*changed->h);
    ui_drop_main_cache();ui_draw_dashboard(&host,tiles,9,7,ip,&dashboard);ui_draw_software();
    assert(!memcmp(cleared.data(),changed->px,cleared.size()*4));
    dashboard.resume=&resume;dashboard.resume_id=1000;
    // A different screen may replace active commands; the retained Home remains valid.
    ui_draw_library(&host,recent,4,0,"All Apps");
    ui_draw_dashboard(&host,tiles,9,0,ip,&dashboard);assert(ui_draw_commands(&command_count));
    assert(ui_scene_restore(ui_canvas()));int base_count;ui_draw_commands(&base_count);
    assert(base_count<command_count); // focus was appended, never stored in the static scene
    ui_drop_main_cache();assert(!ui_scene_restore(ui_canvas()));
    ui_setting host_rows[]={{"* Gaming PC","Online / Paired","192.168.1.3"},
        {"Bedroom PC","Offline","192.168.1.9"},{"+ Add host manually","IPv4","Known IP"},
        {"Host actions","Open","Refresh / pair / quit / unpair"}};
    ui_draw_hosts(host_rows,4,0,UI_HOME,true);ui_draw_software();save_preview("series-dashboard-hosts.ppm");
    ui_draw_enable(false);ui_drop_main_cache();
    puts("PASS: 0-4 recents, asymmetric two-row navigation, cover/Continue identity, CPU/GPU scene cache equivalence and invalidation.");
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
    ui_draw_app_details(&host,recent,"Smooth",4);ui_draw_software();save_preview("navigation-stop-app.ppm");
    ui_location_restore(root);
    ui_set_page(UI_SETTINGS);root=ui_location_enter("Settings");
    ui_location_path(path,sizeof(path),"Developer","Renderer");
    assert(!strcmp(path,"Settings  >  Developer  >  Renderer"));
    ui_setting renderer_rows[]={{"Renderer","GPU","Choose the renderer for the next session."},
        {"Image quality","Auto Quality","Change the renderer quality policy."},{"V-Sync","On","Synchronize presentation."}};
    int renderer_kinds[]={UI_OPTION_CYCLE,UI_OPTION_CYCLE,UI_OPTION_TOGGLE};
    ui_draw_settings_options("Renderer",renderer_rows,renderer_kinds,3,0);ui_draw_software();save_preview("navigation-renderer-options.ppm");
    // The active tab underline must exactly match its measured label.
    ui_draw_settings_options("Stream",stream_rows,stream_kinds,3,0);
    const ui_draw_command *tab_cmd=ui_draw_commands(&command_count);assert(tab_cmd);
    bool underline=false;
    for(int i=0;i<command_count;i++)if(tab_cmd[i].kind==UI_RECT && tab_cmd[i].y==123 && tab_cmd[i].h==2) {
        assert(tab_cmd[i].w>40 && tab_cmd[i].w<120);underline=true;
    }
    assert(underline);
    ui_set_page(UI_SETTINGS);
    int section=ui_location_enter("Developer");ui_location_enter("Network");
    ui_stat net_rows[]={{"Incoming payload","12 Mbps"},{"RTT","8 ms"},{"Drops","0"}};
    ui_draw_stats("Network diagnostics",net_rows,3,"Last stopped session. No live stream is running.");
    ui_draw_software();save_preview("navigation-network-details.ppm");
    ui_location_restore(section);ui_location_restore(root);ui_draw_enable(false);ui_drop_main_cache();
    puts("PASS: nested breadcrumb restore, explicit option kinds, Stop app details and label-width tab underline.");
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
