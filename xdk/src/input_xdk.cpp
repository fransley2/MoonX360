#include <stdlib.h>
/*
 * Controllers through XInput: wired and wireless pads, up to four.
 * Disconnected ports are re-probed at most every 100 ms (polling empty XInput
 * ports at 250 Hz stalls the bus on Xbox 360), and the streaming input thread
 * uses hardware thread 0 in Balanced mode or thread 5 in Legacy mode.
 */
#include "xdk.h"
#include "../../src/launch_cover.h"
#include "../../src/performance.h"

#include <string.h>

#include "../../src/input.h"
#include "../../src/platform.h"
#include "../../src/ui/ui.h"
#include "../../src/ui/draw.h"

#include <Limelight.h>

#include "../../src/input_session.h"
#include "../../src/input_shortcut.h"
#include "../../src/input_hotplug.h"
#include "../../src/desktop_input.h"
#include "../../src/quick_menu.h"
#include "../../src/audio.h"
#include "../../src/video.h"
static volatile LONG desktop_snapshot,desktop_available=1;
static struct desktop_options desktop_settings;
static bool desktop_initial;
void input_configure_desktop(const struct desktop_options *options,bool enabled){desktop_settings=*options;desktop_initial=enabled;}
unsigned input_desktop_snapshot(void){return (unsigned)InterlockedCompareExchange(&desktop_snapshot,0,0);}
void input_desktop_ui_unavailable(void){InterlockedExchange(&desktop_available,0);}
static volatile LONG mouse_mode;
bool input_mouse_active(void) {return InterlockedCompareExchange(&mouse_mode,0,0)!=0;}

static struct input_settings stream_settings;
#ifndef LOW_LATENCY_INPUT
#define LOW_LATENCY_INPUT 0
#endif
static DWORD stream_interval_ms=4;
void input_configure_low_latency(bool enabled)
{
    /* The build flag alone is inert for Smooth and normal user sessions. */
    stream_interval_ms=LOW_LATENCY_INPUT && enabled ? 2 : 4;
}
static int pointer_speed=100,pointer_acceleration,pointer_scroll_ms=100;
static bool initial_stats;
static volatile LONG quick_snapshot,mouse_available=1,quick_available=1;
void input_quick_ui_unavailable(void){InterlockedExchange(&quick_available,0);}
unsigned input_quick_snapshot(void){return (unsigned)InterlockedCompareExchange(&quick_snapshot,0,0);}
bool input_stats_visible(void){return (input_quick_snapshot()&QUICK_STATS)!=0;}
void input_mouse_indicator_available(bool available){InterlockedExchange(&mouse_available,available?1:0);}
void input_configure_pointer(int speed,int acceleration,int scroll_ms,bool stats)
{pointer_speed=speed<25?25:speed>200?200:speed;pointer_acceleration=acceleration;pointer_scroll_ms=scroll_ms<50?50:scroll_ms>400?400:scroll_ms;initial_stats=stats;}


void input_calibrate(int left_min,int left_max,int right_min,int right_max)
{
    stream_settings.calibrated=true;
    stream_settings.left_min=left_min;stream_settings.left_max=left_max;
    stream_settings.right_min=right_min;stream_settings.right_max=right_max;
}
void input_configure(int quit_combo, int deadzone)
{
    stream_settings.calibrated=false;
    stream_settings.quit_combo = quit_combo == 1 ? 1 : 0;
    stream_settings.deadzone = deadzone < 0 ? 0 : deadzone > 30 ? 30 : deadzone;
}
#define REPROBE_DISCONNECTED_MS 100

static int menu_prev;
static volatile LONG stream_active,quit_requested,close_app_requested,rumble_enabled,rumble_generation,rumble_connected_mask;
static HANDLE stream_thread;
static DWORD next_probe_ms[MAX_PADS];
static bool physical_connected[MAX_PADS];
int input_connected_count(void){int n=0;for(int i=0;i<MAX_PADS;i++)if(physical_connected[i])n++;return n;}
static bool port_connected[MAX_PADS] = { true, true, true, true };
static struct input_hotplug hotplug[MAX_PADS];
static XINPUT_STATE last_stream_xinput[MAX_PADS];
static bool have_stream_xinput[MAX_PADS];

static const struct {
    WORD xinput;
    int flag;
} button_map[] = {
    { XINPUT_GAMEPAD_A, A_FLAG },
    { XINPUT_GAMEPAD_B, B_FLAG },
    { XINPUT_GAMEPAD_X, X_FLAG },
    { XINPUT_GAMEPAD_Y, Y_FLAG },
    { XINPUT_GAMEPAD_DPAD_UP, UP_FLAG },
    { XINPUT_GAMEPAD_DPAD_DOWN, DOWN_FLAG },
    { XINPUT_GAMEPAD_DPAD_LEFT, LEFT_FLAG },
    { XINPUT_GAMEPAD_DPAD_RIGHT, RIGHT_FLAG },
    { XINPUT_GAMEPAD_LEFT_SHOULDER, LB_FLAG },
    { XINPUT_GAMEPAD_RIGHT_SHOULDER, RB_FLAG },
    { XINPUT_GAMEPAD_START, PLAY_FLAG },
    { XINPUT_GAMEPAD_BACK, BACK_FLAG },
    { XINPUT_GAMEPAD_LEFT_THUMB, LS_CLK_FLAG },
    { XINPUT_GAMEPAD_RIGHT_THUMB, RS_CLK_FLAG },
};

int input_poll(struct pad pads[MAX_PADS])
{
    DWORD now_ms = GetTickCount();
    int i, mask = 0;
    for(i=0;i<MAX_PADS;i++)perf_input_sample(i,0);

    for (i = 0; i < MAX_PADS; i++) {
        struct pad *p = &pads[i];
        XINPUT_STATE s;
        unsigned b;

        memset(p, 0, sizeof(*p));
        if (!port_connected[i] && (int)(now_ms - next_probe_ms[i]) < 0)
            continue;
        if (XInputGetState(i, &s) != ERROR_SUCCESS) {
            physical_connected[i]=false;
            if(input_hotplug_sample(hotplug+i,p,false,now_ms)){mask|=1<<i;continue;}
            if(port_connected[i])input_rumble((unsigned short)i,0,0);
            port_connected[i]=false;next_probe_ms[i]=now_ms+REPROBE_DISCONNECTED_MS;
            continue;
        }
        physical_connected[i]=true;
        if(perf_enabled() && (!have_stream_xinput[i] ||
           memcmp(&last_stream_xinput[i].Gamepad,&s.Gamepad,sizeof(s.Gamepad))!=0)) {
            uint64_t detected=perf_clock();
            perf_input_sample(i,detected);
        }
        last_stream_xinput[i]=s;have_stream_xinput[i]=true;
        port_connected[i] = true;
        p->present = true;
        mask |= 1 << i;
        for (b = 0; b < sizeof(button_map) / sizeof(button_map[0]); b++)
            if (s.Gamepad.wButtons & button_map[b].xinput)
                p->buttons |= button_map[b].flag;
        p->lt = s.Gamepad.bLeftTrigger;
        p->rt = s.Gamepad.bRightTrigger;
        p->lx = s.Gamepad.sThumbLX;
        p->ly = s.Gamepad.sThumbLY;
        p->rx = s.Gamepad.sThumbRX;
        p->ry = s.Gamepad.sThumbRY;
        input_hotplug_sample(hotplug+i,p,true,now_ms);
    }
    return mask;
}

int input_menu_pressed(void)
{
    const int repeatable = UP_FLAG | DOWN_FLAG | LEFT_FLAG | RIGHT_FLAG | LB_FLAG | RB_FLAG;
    static DWORD held_since, last_repeat, last_poll;
    static int held;
    static bool guide_seen;
    struct pad pads[MAX_PADS];
    int i, now = 0, pressed, dir;
    DWORD t = GetTickCount();
    bool gap;

    if(gfx_system_ui_active()) {
        guide_seen=true;menu_prev=~0;held=0;last_poll=t;
        return 0; /* Guide navigation must never select/quit a Moonlight menu. */
    }
    if(guide_seen) {
        guide_seen=false;
        ui_present(); /* Restore a static menu after the Guide's black backdrop. */
    }

    input_poll(pads);
    for (i = 0; i < MAX_PADS; i++) {
        if (!pads[i].present)
            continue;
        now |= pads[i].buttons;
        /* Treat stick flicks as d-pad presses in menus. */
        if (pads[i].ly > 20000) now |= UP_FLAG;
        if (pads[i].ly < -20000) now |= DOWN_FLAG;
        if (pads[i].lx < -20000) now |= LEFT_FLAG;
        if (pads[i].lx > 20000) now |= RIGHT_FLAG;
    }
    pressed = now & ~menu_prev;

    /*
     * Holding a direction (or LB/RB) repeats it. If the previous poll was
     * long ago (the screen was busy), restart the hold timer instead, so a
     * slow redraw can't turn one press into endless scrolling.
     */
    dir = now & repeatable;
    gap = t - last_poll > 100;
    last_poll = t;
    if (dir != held || gap) {
        held = dir;
        held_since = last_repeat = t;
    } else if (dir && t - held_since > 400 && t - last_repeat > 110) {
        pressed |= dir;
        last_repeat = t;
    }

    menu_prev = now;
    if(!pressed && ui_transition_active())ui_present();
    if(pressed&B_FLAG)audio_menu_play(2);
    else if(pressed&(A_FLAG|X_FLAG|Y_FLAG|PLAY_FLAG))audio_menu_play(1);
    else if(pressed&repeatable)audio_menu_play(0);
    return pressed;
}

static DWORD WINAPI input_stream_main(LPVOID arg)
{
    struct pad pads[MAX_PADS];
    struct input_session session;
    struct desktop_input desktop;memset(&desktop,0,sizeof(desktop));desktop.options=desktop_settings;desktop.enabled=desktop_initial;desktop.neutral=true;desktop.owner=-1;
    struct input_shortcut shortcut;memset(&shortcut,0,sizeof(shortcut));
    struct remote_input remote;memset(&remote,0,sizeof(remote));
    remote.desktop_only=true;
    remote.speed=pointer_speed;remote.acceleration=pointer_acceleration;remote.scroll_ms=pointer_scroll_ms;
    struct quick_menu quick;memset(&quick,0,sizeof(quick));quick.volume=100;quick.stats=initial_stats;
    XINPUT_KEYBOARDSTATE keyboards[MAX_PADS];memset(keyboards,0,sizeof(keyboards));
    DWORD keyboard_probe[MAX_PADS]={0};
    bool keyboard_present[MAX_PADS]={false};
    for(int i=0;i<MAX_PADS;i++)keyboard_probe[i]=GetTickCount();
    int desktop_pending=0;
    bool guide_seen=false,launch_neutral=true; /* also swallow the menu Play button if a zero-delay cover already ended */
    uint64_t previous_poll=0;
    (void)arg;
    xdk_audit_thread("InputPoll");
    memset(&session, 0, sizeof(session));
    while (stream_active) {
        uint64_t poll=perf_enabled()?perf_clock():0;
        if(poll && previous_poll)perf_record(PERF_INPUT_INTERVAL,poll-previous_poll);
        previous_poll=poll;
        bool desktop_was_enabled=desktop.enabled;
        DWORD started = GetTickCount();
        int mask = input_poll(pads);
        video_progress_note(VP_INPUT);
        bool guide=gfx_system_ui_active();
        bool shortcut_consumed[MAX_PADS]={false};
        if(guide)memset(&shortcut,0,sizeof(shortcut));
        bool shortcut_fired=!guide && input_shortcut_step(&shortcut,pads,input_exit_mask(stream_settings.quit_combo),started,shortcut_consumed);
        if(shortcut_fired){InterlockedExchange(&quit_requested,1);plat_log("[INPUT] Exit combo detected and consumed\n");}
        InterlockedExchange(&rumble_connected_mask,mask);
        bool cover_input=launch_active();
        bool any=false,cancel=false,reveal=false;
        for(int i=0;i<MAX_PADS;i++)if(pads[i].present) {
            any=any || pads[i].buttons || pads[i].lt>30 || pads[i].rt>30 || abs(pads[i].lx)>8000 || abs(pads[i].ly)>8000 || abs(pads[i].rx)>8000 || abs(pads[i].ry)>8000;
            cancel=cancel || (pads[i].buttons&B_FLAG)!=0;reveal=reveal || (pads[i].buttons&X_FLAG)!=0;
        }
        if(cover_input){launch_command(cancel,reveal);launch_neutral=true;}
        bool block_launch=cover_input || launch_neutral;
        if(guide && !guide_seen)for(int i=0;i<MAX_PADS;i++)input_rumble((unsigned short)i,0,0);
        struct pad quick_pads[MAX_PADS];memcpy(quick_pads,pads,sizeof(pads));
        for(int i=0;i<MAX_PADS;i++)if(!physical_connected[i])quick_pads[i].present=false;
        if(desktop.enabled)desktop_select_owner(&desktop,quick_pads);
        bool quick_quit=false,quick_close_app=false;int mouse_action=-1;
        bool quick_was_open=quick.open;
        const int overlay_combo=BACK_FLAG|LB_FLAG|RB_FLAG|X_FLAG;
        bool overlay=false;for(int i=0;i<MAX_PADS;i++)if(quick_pads[i].present && (quick_pads[i].buttons&overlay_combo)==overlay_combo)overlay=true;
        if(!guide && !block_launch && overlay && !desktop.overlay_latch)quick.stats=!quick.stats;
        desktop.overlay_latch=overlay;
        if(!InterlockedCompareExchange(&quick_available,0,0)){if(quick.open){quick.open=false;quick.neutral=true;}quick.latch=true;}
        bool local=block_launch;
        if(!block_launch && !shortcut_fired)local=quick_step(&quick,quick_pads,guide,input_exit_mask(stream_settings.quit_combo),&quick_quit,&quick_close_app,&mouse_action,started);
        if(quick.desktop_request){
            remote_release(&remote);desktop.owner=quick.owner;desktop_pending=quick.desktop_request;
        }
        if(quick.refresh_request)video_request_picture_refresh();
        video_refresh_tick(started);
        if(!quick_was_open && quick.open)plat_log("[INPUT] Quick Menu combo detected\n");
        if(quick_quit)InterlockedExchange(&quit_requested,1);
        if(quick_close_app)InterlockedExchange(&close_app_requested,1);
        if(quick_quit)plat_log("[INPUT] Quick Menu action consumed\n");
        for(int i=0;i<MAX_PADS;i++)if(shortcut_consumed[i]){remote_mouse_release(&remote,i);bool present=pads[i].present;memset(pads+i,0,sizeof(pads[i]));pads[i].present=present;}
        if(guide || local || overlay || shortcut_fired){desktop_suspend(&desktop);for(int i=0;i<MAX_PADS;i++)remote_mouse_release(&remote,i);}
        if(!InterlockedCompareExchange(&desktop_available,0,0) && desktop.view){desktop_suspend(&desktop);}
        if(desktop_pending && !local && !guide && !overlay){desktop_command(&desktop,desktop_pending);desktop_pending=0;}
        /* One state for the Quick Menu and Back+R3. The exit chord wins. */
        bool mouse_chord=false;
        int mouse_owner=-1;
        for(int i=0;i<MAX_PADS;i++)if(quick_pads[i].present &&
            (quick_pads[i].buttons&(BACK_FLAG|RS_CLK_FLAG))==(BACK_FLAG|RS_CLK_FLAG) &&
            (quick_pads[i].buttons&input_exit_mask(stream_settings.quit_combo))!=input_exit_mask(stream_settings.quit_combo)){mouse_chord=true;mouse_owner=i;break;}
        if(mouse_chord && !desktop.mouse_latch && !local && !guide && !overlay && !shortcut_fired){
            remote_release(&remote);desktop.owner=mouse_owner;desktop_command(&desktop,DESK_TOGGLE);
            for(int i=0;i<MAX_PADS;i++)remote.suppress[i]=true;
        }
        desktop.mouse_latch=mouse_chord;
        if(guide){for(int i=0;i<MAX_PADS;i++)if(remote.mouse[i])remote.suppress[i]=true;remote_release(&remote);}
        audio_set_volume(quick.muted?0:quick.volume);
        unsigned packed=quick_pack(&quick);
        if(InterlockedCompareExchange(&quit_requested,0,0))packed|=QUICK_STOPPING;
        unsigned previous=(unsigned)InterlockedExchange(&quick_snapshot,(LONG)packed);
        if(packed!=previous && ((packed|previous)&(QUICK_OPEN|QUICK_STOPPING)) && perf_enabled()) {
            char trace[128];snprintf(trace,sizeof(trace),"[UX] quick input_ms=%u state=%u\n",started,packed);plat_log(trace);
        }
        bool keys[256]={false};
        /* Snapshot avoids repeat duplication and releases vanished devices. Bound: four
         * users, six keys each. State is aggregated before sending to handle shared keys. */
        for(DWORD user=0;user<MAX_PADS;user++) {
            if((int)(started-keyboard_probe[user])>=0) {
                keyboard_present[user]=XInputGetKeyboardState(user,&keyboards[user])==ERROR_SUCCESS;
                keyboard_probe[user]=started+(keyboard_present[user]?16:100);
            }
            if(!keyboard_present[user])continue;
            const XINPUT_KEYBOARDSTATE &keyboard=keyboards[user];
            for(int k=0;k<XINPUT_MAX_KEYBOARD_STATE;k++) {
                WORD vk=keyboard.VKCodes[k];
                /* Sided modifier bits below are authoritative; avoid duplicate generic VKs. */
                if(vk>0 && vk<256 && vk!=0x10 && vk!=0x11 && vk!=0x12)keys[vk]=true;
            }
            const BYTE bits[]={XINPUT_MODIFIER_LSHIFT,XINPUT_MODIFIER_RSHIFT,XINPUT_MODIFIER_LCONTROL,XINPUT_MODIFIER_RCONTROL,XINPUT_MODIFIER_LALT,XINPUT_MODIFIER_RALT,XINPUT_MODIFIER_LGUI,XINPUT_MODIFIER_RGUI};
            const BYTE vk[]={0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0x5b,0x5c};
            for(int k=0;k<8;k++)if(keyboard.bModifiers&bits[k])keys[vk[k]]=true;
        }
        for(int k=0;k<256;k++)any=any || keys[k];
        if(launch_neutral && !cover_input && !any)launch_neutral=false;
        if(overlay)for(int i=0;i<MAX_PADS;i++)if((pads[i].buttons&overlay_combo)==overlay_combo){remote.suppress[i]=true;bool present=pads[i].present;memset(pads+i,0,sizeof(pads[i]));pads[i].present=present;}
        if(desktop.enabled && !local && !guide){
            if(desktop.owner>=0){
                struct pad owner=pads[desktop.owner];if(!physical_connected[desktop.owner])owner.present=false;
                if(owner.buttons || desktop.view || desktop.neutral || !owner.present)remote_keys(&remote,keys,false);
                desktop_step(&desktop,&owner,started,input_exit_mask(stream_settings.quit_combo));
            }
            /* All controllers neutral in Desktop Mode: no Steam double input. */
            for(int i=0;i<MAX_PADS;i++){bool present=pads[i].present;memset(pads+i,0,sizeof(pads[i]));pads[i].present=present;}
        }
        remote_keys(&remote,keys,!guide && !local && !desktop.view && (!desktop.enabled || (desktop.owner>=0 && !desktop.neutral && !quick_pads[desktop.owner].buttons && physical_connected[desktop.owner])) && !overlay);
        if(desktop_was_enabled && !desktop.enabled)for(int i=0;i<MAX_PADS;i++)remote.suppress[i]=true;
        if(local)for(int i=0;i<MAX_PADS;i++){remote_mouse_release(&remote,i);bool present=pads[i].present;memset(pads+i,0,sizeof(pads[i]));pads[i].present=present;}
        for(int i=0;i<MAX_PADS;i++) {
            bool present=pads[i].present;
            if(!physical_connected[i])pads[i].present=false;
            if(!desktop.enabled && !overlay && !local && !shortcut_consumed[i])remote_pad(&remote,pads+i,i,started,guide,input_exit_mask(stream_settings.quit_combo));
            if(!InterlockedCompareExchange(&mouse_available,0,0) && remote.mouse[i]){remote_mouse_release(&remote,i);remote.mouse[i]=false;remote.suppress[i]=true;}
            pads[i].present=present;
        }
        InterlockedExchange(&desktop_snapshot,(LONG)desktop_pack(&desktop));
        InterlockedExchange(&mouse_mode,desktop.enabled?1:0);
        guide_seen=guide;
        if(guide)for(int i=0;i<MAX_PADS;i++) {
            bool present=pads[i].present;memset(pads+i,0,sizeof(pads[i]));pads[i].present=present;
        }
        uint64_t send=perf_enabled()?perf_clock():0;
        input_session_step(&session, pads, mask, GetTickCount(), &stream_settings);
        if(send)perf_record(PERF_INPUT_SEND,perf_clock()-send);
        if(poll)perf_record(PERF_INPUT_WORK,perf_clock()-poll);
        /* Include polling/sending in the selected period instead of adding it
         * to every sleep. After an overrun yield for 1 ms; never burst polls
         * or busy-spin on the hardware thread shared with presentation. */
        DWORD elapsed = GetTickCount() - started;
        Sleep(elapsed < stream_interval_ms ? stream_interval_ms - elapsed : 1);
    }
    desktop_release(&desktop);remote_release(&remote);
    InterlockedExchange(&desktop_snapshot,0);
    InterlockedExchange(&mouse_mode,0);
    InterlockedExchange(&quick_snapshot,InterlockedCompareExchange(&quit_requested,0,0)?QUICK_STOPPING:0);
    input_session_release(&session);
    return 0;
}

void input_stream_start(void)
{
    if (stream_thread) return;
    plat_log(stream_interval_ms==2 ? "[INPUT] polling target 2 ms; disconnected reprobe 100 ms\n" :
             "[INPUT] polling target 4 ms; disconnected reprobe 100 ms\n");
    InterlockedExchange(&desktop_available,1);
    InterlockedExchange(&desktop_snapshot,0);
    InterlockedExchange(&mouse_available,1);
    InterlockedExchange(&quick_available,1);
    InterlockedExchange(&quick_snapshot,0);
    InterlockedExchange(&quit_requested,0);
    InterlockedExchange(&close_app_requested,0);
    InterlockedExchange(&stream_active,1);
    InterlockedIncrement(&rumble_generation);
    InterlockedExchange(&rumble_connected_mask,0);
    InterlockedExchange(&rumble_enabled,1);
    stream_thread = CreateThread(NULL, 64 * 1024, input_stream_main, NULL, CREATE_SUSPENDED, NULL);
    if (stream_thread) {
        XSetThreadProcessor(stream_thread, xdk_aux_processor());
        ResumeThread(stream_thread);
    } else {
        InterlockedExchange(&rumble_enabled,0);
        InterlockedExchange(&stream_active,0);
        InterlockedExchange(&quit_requested,1);
    }
}

void input_stream_stop(void)
{
    int i;

    InterlockedExchange(&rumble_enabled,0);
    InterlockedIncrement(&rumble_generation);
    InterlockedExchange(&stream_active,0);
    if (stream_thread) {
        WaitForSingleObject(stream_thread, INFINITE);
        CloseHandle(stream_thread);
        stream_thread = NULL;
    }
    for (i = 0; i < MAX_PADS; i++)
        input_rumble((unsigned short)i, 0, 0);
    /* Swallow buttons still held from the quit combo. */
    menu_prev = ~0;
}

bool input_quit_requested(void)
{
    return InterlockedCompareExchange(&quit_requested,0,0)!=0;
}
bool input_close_app_requested(void){return InterlockedCompareExchange(&close_app_requested,0,0)!=0;}

void input_rumble(unsigned short controller, unsigned short low, unsigned short high)
{
    XINPUT_VIBRATION v;
    LONG generation=InterlockedCompareExchange(&rumble_generation,0,0);

    if (controller >= MAX_PADS)
        return;
    v.wLeftMotorSpeed = low;
    v.wRightMotorSpeed = high;
    if((low||high) && (!InterlockedCompareExchange(&rumble_enabled,0,0)||
        !(InterlockedCompareExchange(&rumble_connected_mask,0,0)&(1<<controller))||gfx_system_ui_active()))return;
    DWORD result=XInputSetState(controller,&v);
    if(result==ERROR_DEVICE_NOT_CONNECTED)return; /* normal hot-unplug, never terminate transport */
    /* A callback may have passed the gate immediately before stop/Guide open. */
    if((low||high) && (generation!=InterlockedCompareExchange(&rumble_generation,0,0)||
        !InterlockedCompareExchange(&rumble_enabled,0,0)||gfx_system_ui_active())) {
        v.wLeftMotorSpeed=v.wRightMotorSpeed=0;XInputSetState(controller,&v);
    }
}
