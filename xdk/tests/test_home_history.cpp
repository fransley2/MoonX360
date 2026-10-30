
#include <assert.h>
#include <stdio.h>
#include "../../src/session_history.h"
static session_record record(const char *host,int id){session_record r={};strcpy(r.host,host);sprintf(r.app,"Old name %d",id);r.app_id=id;r.width=1280;r.height=720;r.fps=60;r.bitrate=18000;return r;}
int main(){
    session_history h={};int ids[]={11,22,33,44,55,66};
    for(int i=0;i<6;i++){session_record r=record("PC-A",ids[i]);history_push(&h,&r);}
    session_record dup=record("PC-A",66);for(int i=0;i<80;i++)history_push(&h,&dup);
    session_record foreign=record("PC-B",11);history_push(&h,&foreign);
    char disk[SESSION_HISTORY_BYTES];size_t n=history_encode(&h,disk,sizeof(disk));assert(n && n<16384);
    session_history restored;history_decode(&restored,disk);
    history_home_targets t=history_home_select(&restored,"PC-A",ids,6,0);
    assert(ids[t.resume]==66 && t.count==4);
    for(int i=0;i<4;i++)assert(ids[t.recent[i]]==ids[4-i]); // 55,44,33,22; no duplicate Continue
    t=history_home_select(&restored,"PC-A",ids,6,11);assert(ids[t.resume]==11 && t.count==4 && ids[t.recent[0]]==66);
    t=history_home_select(&restored,"PC-B",ids,6,0);assert(ids[t.resume]==11 && t.count==0);
    t=history_home_select(&restored,"PC-C",ids,6,0);assert(t.resume==-1 && !t.count);
    int changed_library[]={22,33};t=history_home_select(&restored,"PC-A",changed_library,2,66);
    assert(changed_library[t.resume]==33 && t.count==1 && changed_library[t.recent[0]]==22);
    t=history_home_select(&restored,"PC-A",NULL,0,66);assert(t.resume==-1 && !t.count);
    session_record fresh=record("PC-A",22);history_push(&restored,&fresh);
    t=history_home_select(&restored,"PC-A",ids,6,0);assert(ids[t.resume]==22);
    // Compatibility with existing v1 history; loading preserves repeated sessions.
    session_history old={};history_push(&old,&dup);history_push(&old,&dup);n=history_encode(&old,disk,sizeof(disk));history_decode(&restored,disk);assert(restored.count==2);
    puts("PASS: 80 repeat sessions retain distinct games; four unique host-filtered recents, active/fallback Continue, library removal, immediate update and v1 restart");
}
