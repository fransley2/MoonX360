#include "../../src/ui/ui.h"
#include "../../src/ui/draw.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <vector>
void ui_present(){}
static void mark(canvas_t *c,int item)
{
    ui_draw_begin(c);
    ui_fill(c,0,0,c->w,c->h,0xFF121619);
    ui_outline(c,100,100,120,160,6,3,0xFF70B8FF);
    ui_focus_mark(0,item);
}
int main()
{
    canvas_t c={};assert(cv_init(&c,1280,720));ui_draw_enable(true);ui_set_motion(true);
    mark(&c,-1);assert(ui_focus_tick(10));assert(ui_focus_tick(130));assert(!ui_focus_tick(146)); // no-ack presenter is bounded too
    mark(&c,0);int count=0;const ui_draw_command *p=ui_draw_commands(&count);assert(count==2);
    std::vector<ui_draw_command> original(p,p+count);
    uint32_t start=0xFFFFFFCDu;
    assert(ui_focus_tick(start));p=ui_draw_commands(&count);
    assert(count==2 && p[1].x<100 && p[1].w>120 && (p[1].color&0xFFFFFFu)==0x70B8FF);
    assert(!memcmp(&p[0],&original[0],sizeof(p[0])));unsigned initial_alpha=p[1].color>>24;
    ui_focus_presented();assert(!ui_focus_tick(start));
    assert(ui_focus_tick(start+60));p=ui_draw_commands(&count);
    assert(p[1].x>96.1f && p[1].x<100 && (p[1].color>>24)>initial_alpha);
    ui_draw_command middle=p[1];
    mark(&c,0);assert(ui_focus_tick(start+60));p=ui_draw_commands(&count);
    assert(!memcmp(&p[1],&middle,sizeof(middle))); // redraw never restarts or snaps the active focus
    assert(ui_focus_tick(start+120));p=ui_draw_commands(&count);
    assert(!memcmp(original.data(),p,count*sizeof(*p)) && ui_focus_tick(start+136));
    ui_focus_presented();assert(!ui_focus_tick(start+152));
    mark(&c,1);assert(ui_focus_tick(1000));mark(&c,2);assert(ui_focus_tick(1016));
    assert(ui_focus_tick(1136));ui_focus_presented();assert(!ui_focus_tick(1152)); // retarget is bounded, even at the same position
    mark(&c,3);assert(ui_focus_tick(2000));ui_set_motion(false);p=ui_draw_commands(&count);
    assert(!memcmp(original.data(),p,count*sizeof(*p)) && ui_focus_tick(2016));ui_focus_presented();assert(!ui_focus_tick(2016));
    mark(&c,4);assert(!ui_focus_tick(2020));
    ui_set_motion(true);mark(&c,5);assert(ui_focus_tick(3000));
    ui_draw_begin(&c);ui_fill(&c,0,0,10,10,0xFF000000);assert(!ui_focus_tick(3016)); // no stale outline on another screen
    mark(&c,6);assert(ui_focus_tick(4000));ui_draw_enable(false);assert(!ui_focus_tick(4016));
    cv_free(&c);
    ui_draw_enable(true);assert(ui_init(1280,720));
    ui_host host={"PC","10.0.0.1",HOST_PAIRED,NULL,NULL};
    ui_menu_item items[]={{"Play",NULL,ICON_PLAY,true},{"Library",NULL,ICON_PLAY,true},{"Stream",NULL,ICON_PLAY,true},{"Continue",NULL,ICON_PLAY,true},{"Settings",NULL,ICON_PLAY,true}};
    ui_draw_dashboard(&host,items,5,0,NULL,NULL);p=ui_draw_commands(&count);
    original.assign(p,p+count);assert(ui_focus_tick(5000));
    for(uint32_t time=5016;time<5136;time+=16)ui_focus_tick(time);
    p=ui_draw_commands(&count);assert(original.size()==(size_t)count && !memcmp(original.data(),p,count*sizeof(*p)));
    ui_focus_presented();assert(!ui_focus_tick(5152));
    ui_setting rows[]={{"Language","English","Choose language"}};
    ui_draw_settings_page("System",rows,1,0);assert(!ui_focus_tick(5168));
    ui_app games[12]={};for(int i=0;i<12;i++)games[i].name="Demo";
    ui_draw_library(&host,games,12,0,"All Apps");assert(ui_focus_tick(6000));
    assert(ui_focus_tick(6120));ui_focus_presented();assert(!ui_focus_tick(6136));
    ui_draw_library(&host,games,12,6,"All Apps");assert(ui_focus_tick(6200)); // same screen position, different game
    ui_focus_finish();ui_focus_presented();assert(!ui_focus_tick(6216));
    puts("PASS: 120ms focus limit, clock wrap, single-outline edits, terminal-frame retry/ack, same-target redraw, retarget, disable, unrelated screen, CPU fallback and retained Home terminal equality.");
}
