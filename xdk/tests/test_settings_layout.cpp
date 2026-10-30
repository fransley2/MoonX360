#include <assert.h>
#include <stdio.h>
#include "../../src/settings_layout.h"
int main(void)
{
    int occurrences[SET_COUNT]={0},children[MENU_COUNT]={0};
    for(int i=0;i<SETTINGS_ENTRY_COUNT;i++) {
        const settings_menu_entry &entry=settings_entries[i];
        assert(entry.page>=0 && entry.page<MENU_COUNT);
        if(entry.setting>=0) {
            assert(entry.setting<SET_COUNT && entry.child==-1);
            occurrences[entry.setting]++;
        } else {
            assert(entry.child>=0 && entry.child<MENU_COUNT);
            assert(settings_pages[entry.child].parent==entry.page);
            children[entry.child]++;
        }
    }
    for(int id=0;id<SET_COUNT;id++)assert(occurrences[id]==1);
    for(int node=MENU_PCS;node<MENU_COUNT;node++)assert(children[node]==1);
    for(int c=0;c<6;c++)assert(settings_pages[MENU_PCS+c].category==c);
    assert(settings_node(SET_PRESET)==MENU_STREAM && settings_node(SET_USER_PROFILES)==MENU_STREAM);
    const int stream[]={SET_RES,SET_FPS,SET_BITRATE,SET_ADAPTIVE,SET_VSYNC,SET_IMAGE_QUALITY,SET_RENDERER};
    for(int id:stream)assert(settings_node(id)==MENU_STREAM_ADJUST);
    assert(settings_node(SET_AUDIO_MODE)==MENU_AUDIO && settings_node(SET_AUDIO)==MENU_AUDIO);
    assert(settings_node(SET_MDNS)==MENU_PCS);
    assert(settings_group(SET_DEADZONE)==SETTINGS_CONTROLS && settings_group(SET_REMOTE_INPUT)==SETTINGS_CONTROLS);
    assert(settings_node(SET_DISPLAY_MODE)==MENU_DISPLAY && settings_node(SET_LANGUAGE)==MENU_APPEARANCE_SCREEN);
    assert(settings_node(SET_HISTORY_VIEW)==MENU_HISTORY && settings_node(SET_OVERLAY)==MENU_HISTORY);
    assert(settings_node(SET_RESTART)==MENU_POWER && settings_node(SET_SHUTDOWN)==MENU_POWER);
    assert(settings_node(SET_FAST)==MENU_DECODER && settings_node(SET_WORKERS)==MENU_DECODER);
    assert(settings_node(SET_PACKET)==MENU_NETWORK && settings_node(SET_SOPS)==MENU_NETWORK && settings_node(SET_TEST_BITRATE)==MENU_NETWORK);
    assert(settings_node(SET_NETWORK_INFO)==MENU_TECHNICAL && settings_node(SET_RECOVERY_INFO)==MENU_TECHNICAL);
    assert(settings_node(SET_LOG_CAPTURE)==MENU_LOGGING && settings_node(SET_REPORT)==MENU_LOGGING);
    assert(settings_kind(SET_PRESET)==UI_OPTION_CYCLE && settings_kind(SET_MDNS)==UI_OPTION_TOGGLE);
    assert(settings_kind(SET_LOG_VIEW)==UI_OPTION_OPEN && settings_kind(SET_BENCH)==UI_OPTION_ACTION);
    assert(settings_kind(SET_RECOVERY_INFO)==UI_OPTION_INFO && settings_kind(SET_DISPLAY_INFO)==UI_OPTION_INFO);
    puts("PASS: all 38 setting IDs have exactly one destination; all submenu links have one parent; six categories and interaction types.");
}
