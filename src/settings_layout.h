#pragma once
#include "ui/navigation.h"

enum { SET_RES, SET_FPS, SET_BITRATE, SET_FAST, SET_RENDERER, SET_VSYNC, SET_SOPS, SET_AUDIO, SET_BENCH,
       SET_PRESET, SET_EXIT, SET_DEADZONE, SET_REPORT, SET_PACKET, SET_OVERLAY,
       SET_LOG_CAPTURE, SET_LOG_VIEW, SET_SLICES, SET_TEST_BITRATE, SET_IMAGE_QUALITY, SET_HISTORY, SET_HISTORY_VIEW, SET_ADVISOR, SET_MDNS, SET_MEMORY, SET_RECOVERY_INFO, SET_NETWORK_INFO, SET_ADAPTIVE, SET_DISPLAY_INFO, SET_DISPLAY_MODE, SET_LANGUAGE, SET_CPU_LAYOUT, SET_WORKERS, SET_RESTART, SET_SHUTDOWN, SET_USER_PROFILES, SET_AUDIO_MODE, SET_REMOTE_INPUT, SET_SAVE_PROFILE, SET_LAUNCH_COVER, SET_COUNT };
/* Category IDs are internal context; Home/Library/Stream/Settings own navigation. */
enum { SETTINGS_PCS, SETTINGS_CONTROLS, SETTINGS_APPEARANCE, SETTINGS_HISTORY,
       SETTINGS_SYSTEM, SETTINGS_DIAGNOSTICS, SETTINGS_STREAM, SETTINGS_VIDEO, SETTINGS_AUDIO, SETTINGS_ABOUT, SETTINGS_GROUP_COUNT };
enum { MENU_STREAM, MENU_SETTINGS, MENU_PCS, MENU_CONTROLS, MENU_APPEARANCE_SCREEN,
       MENU_HISTORY, MENU_SYSTEM, MENU_ADVANCED, MENU_STREAM_ADJUST, MENU_AUDIO,
       MENU_HOSTS, MENU_APPEARANCE, MENU_STREAM_UI, MENU_DISPLAY, MENU_POWER,
       MENU_DECODER, MENU_NETWORK, MENU_TECHNICAL, MENU_LOGGING,
       MENU_RESTART_APP, MENU_RESET_APP, MENU_HOST_DETAILS, MENU_CONNECTION_TEST, MENU_ABOUT_INFO, MENU_COUNT };
struct settings_page_spec { int parent, category; const char *title, *help; };
static const char *settings_categories[]={
    "Network", "Controller", "Interface", "Developer", "About", "Developer",
    "Stream", "Video", "Audio", "About"
};
static const struct settings_page_spec settings_pages[MENU_COUNT]={
    {-1,SETTINGS_STREAM,"Stream","Prepare the next session: profile, video and audio."},
    {-1,-1,"Settings","Choose an app settings category."},
    {MENU_SETTINGS,SETTINGS_PCS,"Network","Saved PCs, host actions and automatic discovery."},
    {MENU_SETTINGS,SETTINGS_CONTROLS,"Controller","Calibration, disconnect shortcut and remote mouse."},
    {MENU_SETTINGS,SETTINGS_APPEARANCE,"Interface","Appearance, streaming interface, app display and language."},
    {MENU_ADVANCED,SETTINGS_HISTORY,"History and performance","Recent sessions, recording, performance analysis and on-screen statistics."},
    {MENU_SETTINGS,SETTINGS_SYSTEM,"About","Restart the app, reset preferences or manage Xbox power."},
    {MENU_SETTINGS,SETTINGS_DIAGNOSTICS,"Developer","Session history, performance and technical diagnostics."},
    {MENU_STREAM,SETTINGS_VIDEO,"Video","Video resolution, frame rate, bitrate, adaptation, V-Sync and image quality. Applies next connection."},
    {MENU_STREAM,SETTINGS_AUDIO,"Audio","Requested Xbox audio output and simultaneous playback on the PC."},
    {MENU_PCS,SETTINGS_PCS,"Saved and discovered PCs","Choose a saved/discovered PC, add an IP or open host actions."},
    {MENU_APPEARANCE_SCREEN,SETTINGS_APPEARANCE,"Appearance","Background, accent color and focus animation."},
    {MENU_APPEARANCE_SCREEN,SETTINGS_APPEARANCE,"Streaming interface","Mouse indicator and quick menu style. Applies next session."},
    {MENU_APPEARANCE_SCREEN,SETTINGS_APPEARANCE,"App display","App rendering resolution and detected physical output are separate."},
    {MENU_SYSTEM,SETTINGS_SYSTEM,"Console power","Restart or shut down the Xbox after confirmation."},
    {MENU_STREAM_ADJUST,SETTINGS_VIDEO,"Decoding","Existing fast decoding, H.264 slices, CPU layout and worker settings."},
    {MENU_STREAM_ADJUST,SETTINGS_VIDEO,"Stream compatibility","RTP packet size, PC game optimization and bitrate comparisons."},
    {MENU_ADVANCED,SETTINGS_DIAGNOSTICS,"Technical diagnostics","Network metrics, logs, decoder test, UI resources and recovery information."},
    {MENU_TECHNICAL,SETTINGS_DIAGNOSTICS,"Logs and report","Diagnostic capture, log viewer and post-session report."},
    {MENU_SYSTEM,SETTINGS_SYSTEM,"Restart app","Save preferences and reopen this app. Does not restart the Xbox."},
    {MENU_SYSTEM,SETTINGS_SYSTEM,"Reset app settings","Reset preferences; keep saved PCs, pairing and named profiles."},
    {MENU_PCS,SETTINGS_PCS,"Host details","Name, software, endpoint, pairing and advertised capabilities."},
    {MENU_PCS,SETTINGS_PCS,"Connection test","Validate the endpoint and recover the same PC on the LAN."},
    {MENU_SYSTEM,SETTINGS_ABOUT,"Credits and licenses","MoonX360, upstream credits and bundled open-source components."}
};
/* SET_USER_PROFILES is retired. IDs/formats remain compatible. */
struct settings_menu_entry { int page, setting, child; };
static const struct settings_menu_entry settings_entries[]={
    {MENU_STREAM,SET_PRESET,-1}, {MENU_STREAM,-1,MENU_STREAM_ADJUST},
    {MENU_STREAM,-1,MENU_AUDIO},
    {MENU_SETTINGS,-1,MENU_PCS}, {MENU_SETTINGS,-1,MENU_CONTROLS},
    {MENU_SETTINGS,-1,MENU_APPEARANCE_SCREEN}, {MENU_ADVANCED,-1,MENU_HISTORY},
    {MENU_SETTINGS,-1,MENU_ADVANCED}, {MENU_SETTINGS,-1,MENU_SYSTEM},
    {MENU_STREAM_ADJUST,SET_RES,-1}, {MENU_STREAM_ADJUST,SET_FPS,-1},
    {MENU_STREAM_ADJUST,SET_BITRATE,-1}, {MENU_STREAM_ADJUST,SET_ADAPTIVE,-1},
    {MENU_STREAM_ADJUST,SET_VSYNC,-1}, {MENU_STREAM_ADJUST,SET_IMAGE_QUALITY,-1},
    {MENU_STREAM_ADJUST,SET_RENDERER,-1}, /* Hidden when backend has no renderer choice. */
    {MENU_STREAM_ADJUST,SET_SAVE_PROFILE,-1}, {MENU_STREAM,SET_LAUNCH_COVER,-1},
    {MENU_AUDIO,SET_AUDIO_MODE,-1}, {MENU_AUDIO,SET_AUDIO,-1},
    {MENU_PCS,-1,MENU_HOST_DETAILS}, {MENU_PCS,-1,MENU_CONNECTION_TEST},
    {MENU_PCS,-1,MENU_HOSTS}, {MENU_PCS,SET_MDNS,-1},
    {MENU_CONTROLS,SET_DEADZONE,-1}, {MENU_CONTROLS,SET_EXIT,-1}, {MENU_CONTROLS,SET_REMOTE_INPUT,-1},
    {MENU_APPEARANCE_SCREEN,-1,MENU_APPEARANCE}, {MENU_APPEARANCE_SCREEN,-1,MENU_STREAM_UI},
    {MENU_APPEARANCE_SCREEN,-1,MENU_DISPLAY}, {MENU_APPEARANCE_SCREEN,SET_LANGUAGE,-1},
    {MENU_DISPLAY,SET_DISPLAY_MODE,-1}, {MENU_DISPLAY,SET_DISPLAY_INFO,-1},
    {MENU_HISTORY,SET_HISTORY_VIEW,-1}, {MENU_HISTORY,SET_HISTORY,-1},
    {MENU_HISTORY,SET_ADVISOR,-1}, {MENU_HISTORY,SET_OVERLAY,-1},
    {MENU_SYSTEM,-1,MENU_ABOUT_INFO}, {MENU_SYSTEM,-1,MENU_RESTART_APP}, {MENU_SYSTEM,-1,MENU_RESET_APP}, {MENU_SYSTEM,-1,MENU_POWER},
    {MENU_POWER,SET_RESTART,-1}, {MENU_POWER,SET_SHUTDOWN,-1},
    {MENU_STREAM_ADJUST,-1,MENU_DECODER}, {MENU_STREAM_ADJUST,-1,MENU_NETWORK}, {MENU_ADVANCED,-1,MENU_TECHNICAL},
    {MENU_DECODER,SET_FAST,-1}, {MENU_DECODER,SET_SLICES,-1}, {MENU_DECODER,SET_CPU_LAYOUT,-1}, {MENU_DECODER,SET_WORKERS,-1},
    {MENU_NETWORK,SET_PACKET,-1}, {MENU_NETWORK,SET_SOPS,-1}, {MENU_NETWORK,SET_TEST_BITRATE,-1},
    {MENU_TECHNICAL,SET_NETWORK_INFO,-1}, {MENU_TECHNICAL,-1,MENU_LOGGING},
    {MENU_TECHNICAL,SET_BENCH,-1}, {MENU_TECHNICAL,SET_MEMORY,-1}, {MENU_TECHNICAL,SET_RECOVERY_INFO,-1},
    {MENU_LOGGING,SET_LOG_CAPTURE,-1}, {MENU_LOGGING,SET_LOG_VIEW,-1}, {MENU_LOGGING,SET_REPORT,-1}
};
#define SETTINGS_ENTRY_COUNT ((int)(sizeof(settings_entries)/sizeof(settings_entries[0])))
static int settings_kind(int id)
{
    switch(id) {
    case SET_PRESET:case SET_LAUNCH_COVER:return UI_OPTION_CYCLE; /* Left/Right inline; A opens details. */
    case SET_USER_PROFILES:case SET_HISTORY_VIEW:case SET_ADVISOR:case SET_LOG_VIEW:case SET_MEMORY:case SET_NETWORK_INFO:return UI_OPTION_OPEN;
    case SET_SAVE_PROFILE:case SET_BENCH:case SET_RESTART:case SET_SHUTDOWN:return UI_OPTION_ACTION;
    case SET_REMOTE_INPUT:case SET_DEADZONE:return UI_OPTION_OPEN;
    case SET_RECOVERY_INFO:case SET_DISPLAY_INFO:return UI_OPTION_INFO;
    case SET_FAST:case SET_VSYNC:case SET_SOPS:case SET_AUDIO:case SET_REPORT:
    case SET_LOG_CAPTURE:case SET_HISTORY:case SET_MDNS:return UI_OPTION_TOGGLE;
    default:return UI_OPTION_CYCLE;
    }
}

static int settings_node(int id)
{
    for(int i=0;i<SETTINGS_ENTRY_COUNT;i++)if(settings_entries[i].setting==id)return settings_entries[i].page;
    return -1;
}
static int settings_group(int id)
{
    int node=settings_node(id);
    return node<0?-1:settings_pages[node].category<0?SETTINGS_STREAM:settings_pages[node].category;
}
