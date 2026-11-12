"""Exercise the real XDK input loop with deterministic simulated XInput samples."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
fixture=(root/'xdk/tests/controller_hotplug_fixture.cpp').read_text()
source=(root/'xdk/src/input_xdk.cpp').read_text()
source=source[source.index('static struct input_settings'):]
fixture=fixture[:fixture.index('int main(){')]
fixture=fixture.replace('static DWORD tick;', 'static bool legacy_scenario,port_scenario;static DWORD tick;')
a=fixture.index('static void Sleep(DWORD ms)');b=fixture.index('{',a)+1;depth=1
while depth:
    depth+=(fixture[b]=='{')-(fixture[b]=='}');b+=1
fixture=fixture[:a]+r'''
static void Sleep(DWORD ms){assert(ms==4);tick+=ms;sleep_count++;
    samples[0].Gamepad.wButtons=0;samples[0].Gamepad.sThumbLX=0;
    if(port_scenario){
        for(int i=0;i<MAX_PADS;i++)samples[i].Gamepad.wButtons=0;
        if(sleep_count==3 || sleep_count==4)samples[2].Gamepad.wButtons=A_FLAG;
        if(sleep_count==5){connected[2]=false;connected[3]=true;}
        if(sleep_count==6)assert(mouse_up==1);
        if(sleep_count>=32 && sleep_count<=34)samples[3].Gamepad.wButtons=A_FLAG;
        if(sleep_count==35)guide=true;
        if(sleep_count==37)guide=false;
        if(sleep_count>=42)stream_active=0;
        return;
    }
    if(legacy_scenario){
        if(sleep_count==3 || sleep_count==4 || sleep_count==17 || sleep_count==18 || sleep_count==25)samples[0].Gamepad.wButtons=A_FLAG;
        if(sleep_count==5 || sleep_count==6 || sleep_count==9)samples[0].Gamepad.wButtons=BACK_FLAG;
        if(sleep_count==7 || sleep_count==8)samples[0].Gamepad.wButtons=BACK_FLAG|RS_CLK_FLAG;
        if(sleep_count==8)assert(!(desktop_snapshot&(1<<11)) && !mouse_mode);
        if(sleep_count==11)samples[0].Gamepad.wButtons=BACK_FLAG|LS_CLK_FLAG;
        if(sleep_count==12)assert(quick_snapshot&QUICK_OPEN);
        if(sleep_count==13)samples[0].Gamepad.wButtons=B_FLAG;
        if(sleep_count==15 || sleep_count==23)samples[0].Gamepad.wButtons=BACK_FLAG|RS_CLK_FLAG;
        if(sleep_count==17)assert(mouse_mode && (desktop_snapshot&(1<<11)));
        if(sleep_count==19)guide=true;
        if(sleep_count==21)guide=false;
        if(sleep_count>=27)stream_active=0;
        return;
    }
    if(sleep_count==3 || sleep_count==4)samples[0].Gamepad.wButtons=A_FLAG;
    if(sleep_count==5 || sleep_count==6){samples[0].Gamepad.wButtons=A_FLAG;samples[0].Gamepad.sThumbLX=25000;}
    if(sleep_count==7)samples[0].Gamepad.wButtons=Y_FLAG|A_FLAG;
    if(sleep_count==11)samples[0].Gamepad.wButtons=A_FLAG;
    if(sleep_count==15)samples[0].Gamepad.wButtons=B_FLAG;
    if(sleep_count==19)samples[0].Gamepad.wButtons=BACK_FLAG|LB_FLAG|RB_FLAG|X_FLAG;
    if(sleep_count==23)samples[0].Gamepad.wButtons=BACK_FLAG|LS_CLK_FLAG;
    if(sleep_count>=27 && sleep_count<=35 && (sleep_count-27)%4==0)samples[0].Gamepad.wButtons=DOWN_FLAG;
    if(sleep_count==39)samples[0].Gamepad.wButtons=X_FLAG;
    if(sleep_count==43 || sleep_count==47)samples[0].Gamepad.wButtons=A_FLAG;
    // Assert the input owner's published snapshots, not a separate policy model.
    if(sleep_count==8)assert((desktop_snapshot&3)==DESK_KEYBOARD);
    if(sleep_count==20)assert(quick_snapshot&QUICK_STATS);
    if(sleep_count==24)assert(quick_snapshot&QUICK_OPEN);
    if(sleep_count==40)assert(((quick_snapshot>>QUICK_PAGE_SHIFT)&3)==QUICK_DESKTOP_PAGE);
    if(sleep_count==46)assert(!(desktop_snapshot&(1<<11)));
    if(sleep_count>=50)stream_active=0;
}
''' +fixture[b:]
fixture=fixture.replace('/* ACTUAL_INPUT */',source)
fixture=fixture.replace('static std::vector<event> events;', 'static std::vector<event> events;static unsigned moves,mouse_down,mouse_up,utf8;')
fixture=fixture.replace('events.push_back(e);return 0;', r'if((port_scenario || (!legacy_scenario && tick<184)) && (buttons || lt || lx)){printf("LEAK tick=%u poll=%d port_scenario=%d port=%d buttons=%x lt=%u lx=%d\n",tick,sleep_count,port_scenario,port,buttons,lt,lx);fflush(stdout);assert(false);}events.push_back(e);return 0;',1)
fixture=fixture.replace('int LiSendMouseMoveEvent(short,short){return 0;}', 'int LiSendMouseMoveEvent(short,short){moves++;return 0;}')
fixture=fixture.replace('int LiSendMouseButtonEvent(char,int){return 0;}', 'int LiSendMouseButtonEvent(char action,int){if(action==BUTTON_ACTION_PRESS)mouse_down++;else mouse_up++;return 0;}')
fixture=fixture.replace('int LiSendUtf8TextEvent(const char*,unsigned){return 0;}', 'int LiSendUtf8TextEvent(const char *s,unsigned n){assert(n==1 && *s==\'q\');utf8++;return 0;}')
fixture+=r'''
int main(){
    connected[0]=true;desktop_defaults(&desktop_settings);desktop_initial=true;
    input_stream_start();input_stream_main(NULL);input_stream_stop();
    assert(moves && mouse_down==1 && mouse_up==1 && utf8==1);
    bool game=false;for(size_t i=0;i<events.size();i++)if(events[i].buttons==A_FLAG)game=true;
    assert(game && !desktop_snapshot && keyboard_down==keyboard_up);
    legacy_scenario=true;tick=sleep_count=0;events.clear();mouse_down=mouse_up=0;guide=false;
    memset(samples,0,sizeof(samples));input_stream_start();input_stream_main(NULL);input_stream_stop();
    assert(mouse_down==2 && mouse_up==2); // Both drags use the unified mode; Guide releases the second
    game=false;for(size_t i=0;i<events.size();i++)if(events[i].buttons==A_FLAG)game=true;
    assert(game);
    port_scenario=true;legacy_scenario=false;tick=sleep_count=0;events.clear();mouse_down=mouse_up=0;guide=false;
    memset(connected,0,sizeof(connected));memset(samples,0,sizeof(samples));memset(next_probe_ms,0,sizeof(next_probe_ms));
    connected[2]=true;input_stream_start();input_stream_main(NULL);input_stream_stop();
    assert(mouse_down==2 && mouse_up==2 && !desktop_snapshot);
    puts("PASS: real XDK input loop Desktop drag/keyboard/UTF8/overlay/Quick Menu, zero gamepad leakage, Desktop-to-game transition and modifier cleanup (simulated devices).");
}
'''
Path(sys.argv[1]).write_text(fixture)
