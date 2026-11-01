
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include "../../src/config.h"
#include <stdarg.h>
static int xbox_scan(const char *text,const char *format,...) {
    assert(!strstr(format,"%n"));va_list args;va_start(args,format);int rc=vsscanf(text,format,args);va_end(args);return rc;
}
#define sscanf xbox_scan
#include "../../src/profile_links.h"
#include "../../src/launch_settings.h"
static std::map<std::string,std::string> files;
static bool fail_read,fail_write,short_write;
static int write_step,fail_step;
char *storage_read(const char *name,size_t *n){
    if(fail_read){errno=EIO;return NULL;}if(!files.count(name)){errno=ENOENT;return NULL;}
    const std::string &s=files[name];char *p=(char*)malloc(s.size()+1);memcpy(p,s.c_str(),s.size()+1);if(n)*n=s.size();return p;
}
bool storage_write(const char *name,const void *data,size_t n){
    assert(!strcmp(name,"profiles-a.tsv") || !strcmp(name,"profiles-b.tsv") || !strcmp(name,"moonlight.ini") || !strcmp(name,"game-bindings.tsv") || !strcmp(name,"profile-link-pending.tsv") || !strcmp(name,"launch-settings.tsv"));
    if(fail_write || (++write_step==fail_step))return false;files[name]=std::string((const char*)data,short_write?n/2:n);return !short_write;
}
int main(){
    config c;config_defaults(&c);strcpy(c.host,"MY HOST");c.language=2;c.left_min=17;c.stats_overlay=true;c.session_report=true;c.ui_background=2;c.ui_accent=3;c.ui_motion=false;c.mouse_hud=2;c.stream_menu_style=1;
    user_profiles p={};p.slot=-1;assert(!config_profiles_load(&p) && !p.count);
    assert(config_profiles_create(&p,&c,"Meu perfil 1"));assert(p.count==1 && p.generation==1);
    assert(config_profiles_rename(&p,0,"Sala \xC3\xA0 noite"));
    user_profiles loaded={};loaded.slot=-1;assert(config_profiles_load(&loaded));assert(loaded.count==1 && !strcmp(loaded.items[0].name,"Sala \xC3\xA0 noite"));
    config other=c;other.width=1920;other.height=1080;other.fps=60;other.bitrate_kbps=20000;other.packet_size=1200;other.slice_count=12;
    other.fast_decode=false;other.renderer=2;other.vsync=false;other.sops=false;other.local_audio=true;other.image_quality=2;
    other.adaptive_quality=1;other.thread_layout=0;other.decode_workers=3;other.stream_profile=0;
    assert(config_profiles_create(&p,&other,"TV"));
    config applied=c;assert(config_profiles_apply(&p,1,&applied));stream_options actual;config_profile_capture(&actual,&applied);
    assert(!memcmp(&actual,&p.items[1].options,sizeof(actual)));
    assert(!strcmp(applied.host,c.host) && applied.language==c.language && applied.left_min==17 && applied.stats_overlay && applied.session_report);
    assert(applied.ui_background==2 && applied.ui_accent==3 && !applied.ui_motion && applied.mouse_hud==2 && applied.stream_menu_style==1);
    assert(config_save(&applied));config restarted;config_load(&restarted);stream_options reloaded;
    config_profile_capture(&reloaded,&restarted);assert(!memcmp(&actual,&reloaded,sizeof(actual)));
    assert(!memcmp(&p.items[1].options,&actual,sizeof(actual))); // applying never mutates saved data
    user_profiles before=p;
    const char *invalid[]={""," leading","trailing ","bad\tname","bad\nname","bad\rname","\xC0\xAF","\xED\xA0\x80","\xF4\x90\x80\x80","\xE2\x82"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++){assert(!config_profiles_create(&p,&c,invalid[i]));assert(!config_profiles_rename(&p,0,invalid[i]));}
    char long_name[65];memset(long_name,'x',64);long_name[64]=0;assert(!config_profile_name_valid(long_name));
    assert(!config_profiles_rename(&p,0,"TV"));assert(!config_profiles_delete(&p,99));assert(!memcmp(&before,&p,sizeof(p)));
    fail_write=true;assert(!config_profiles_rename(&p,0,"Changed"));assert(!config_profiles_delete(&p,0));assert(!memcmp(&before,&p,sizeof(p)));fail_write=false;
    short_write=true;assert(!config_profiles_rename(&p,0,"Partial"));short_write=false;
    loaded={};assert(config_profiles_load(&loaded));assert(loaded.count==2 && !strcmp(loaded.items[0].name,p.items[0].name)); // previous good slot survives
    fail_read=true;loaded=p;assert(!config_profiles_load(&loaded));assert(loaded.read_only && loaded.count==p.count && !memcmp(loaded.items,p.items,sizeof(p.items)));assert(!config_profiles_rename(&p,0,"Unverified"));fail_read=false;
    // Reload after an unverified write, then delete and restart.
    assert(config_profiles_load(&p));assert(config_profiles_delete(&p,1));loaded={};assert(config_profiles_load(&loaded) && loaded.count==1);
    for(int i=loaded.count;i<CONFIG_USER_PROFILE_MAX;i++){char n[32];sprintf(n,"Profile %d",i);assert(config_profiles_create(&loaded,&c,n));}
    assert(!config_profiles_create(&loaded,&c,"Full"));
    files[loaded.slot?"profiles-b.tsv":"profiles-a.tsv"]+="junk";
    user_profiles fallback={};assert(config_profiles_load(&fallback) && fallback.count==CONFIG_USER_PROFILE_MAX-1);
    files["profiles-a.tsv"]="XENON_PROFILES 3 90 0\n";assert(!config_profiles_load(&fallback) && fallback.read_only);
    assert(!config_profiles_create(&fallback,&c,"Unknown version"));
    // Explicit factory presets retain their four parameters and effective Fast overrides.
    for(int i=1;i<CONFIG_PROFILE_COUNT;i++) {config factory=c;config_apply_preset(&factory,i);config_profile_capture(&actual,&factory);
        assert(actual.width==factory.width && actual.height==factory.height && actual.fps==factory.fps && actual.bitrate_kbps==factory.bitrate_kbps);
        if(i==CONFIG_PROFILE_SMOOTH || i==CONFIG_PROFILE_1080_SMOOTH || i==CONFIG_PROFILE_PERFORMANCE)assert(actual.fast_decode && actual.image_quality==1);
    }
    // Malformed but correctly checksummed snapshots must still be rejected.
    files.clear();user_profiles clean={};clean.slot=-1;assert(config_profiles_create(&clean,&c,"Valid"));
    std::string good=files["profiles-a.tsv"];
    const char *bad_rows[]={"\t99999\t720", "\t1280\t99999", "\t1280\t720\t2147483648", "\t1280\t720\t-30"};
    for(unsigned i=0;i<sizeof(bad_rows)/sizeof(bad_rows[0]);i++) {
        std::string payload=good.substr(0,good.find("CHECKSUM "));
        size_t start=payload.find("\t1280\t720\t60");assert(start!=std::string::npos);
        const char *old=i<2?"\t1280\t720":"\t1280\t720\t60";
        payload.replace(start,strlen(old),bad_rows[i]);unsigned hash=2166136261u;
        for(size_t j=0;j<payload.size();j++)hash=(hash^(unsigned char)payload[j])*16777619u;
        char tail[40];sprintf(tail,"CHECKSUM %08x\n",hash);files["profiles-a.tsv"]=payload+tail;
        user_profiles invalid={};invalid.slot=-1;assert(!config_profiles_load(&invalid) && !invalid.count);
    }
    files["profiles-a.tsv"]=std::string(CONFIG_PROFILES_BYTES+1,'x');assert(!config_profiles_load(&clean));
    // A named selection survives restart; every owned option participates in matching.
    files.clear();fail_step=0;user_profiles selected={};selected.slot=-1;config_defaults(&c);
    assert(config_profiles_create(&selected,&c,"Personal"));assert(config_profiles_apply(&selected,0,&c));
    assert(config_profiles_selected(&selected,&c)==0 && config_save(&c));config_load(&restarted);
    assert(config_profiles_selected(&selected,&restarted)==0);
    for(int field=0;field<16;field++) {
        config changed=restarted;int *option=NULL;
        switch(field){case 0:changed.width=854;break;case 1:changed.height=480;break;case 2:changed.fps=24;break;
        case 3:changed.bitrate_kbps++;break;case 4:changed.packet_size++;break;case 5:changed.slice_count=8;break;
        case 6:changed.fast_decode=!changed.fast_decode;break;case 7:changed.renderer=1;break;case 8:changed.vsync=!changed.vsync;break;
        case 9:changed.sops=!changed.sops;break;case 10:changed.local_audio=!changed.local_audio;break;case 11:changed.image_quality=2;break;
        case 12:changed.adaptive_quality=1;break;case 13:changed.thread_layout=0;break;case 14:changed.decode_workers=3;break;case 15:changed.audio_mode=1;break;}
        assert(config_profiles_selected(&selected,&changed)==-1);
    }
    // Saved identity wins over a stale/missing factory key; never force Fast on a named profile.
    config named=c;config_customize_stream(&named);named.fast_decode=false;named.image_quality=2;
    assert(config_profiles_create(&selected,&named,"Smooth"));assert(config_profiles_apply(&selected,1,&named));
    assert(config_save(&named));std::string named_ini=files["moonlight.ini"];
    for(int stale=0;stale<2;stale++) {
        files["moonlight.ini"]=named_ini;
        size_t key=files["moonlight.ini"].find("stream_profile=custom\n");assert(key!=std::string::npos);
        files["moonlight.ini"].replace(key,strlen("stream_profile=custom\n"),stale?"stream_profile=smooth\n":"");
        config_load(&restarted);assert(!restarted.fast_decode && restarted.image_quality==2 && restarted.stream_profile==0);
        assert(config_profiles_selected(&selected,&restarted)==1 && config_video_preset(&restarted)==0);
    }
    // Names are resolved by effective options, even after manual edits return
    // to a known profile. Canonical factory choice preserves global fields.
    config custom=named;config_customize_stream(&custom);
    assert(config_profiles_selected(&selected,&custom)==1);
    strcpy(custom.user_profile,"Deleted");assert(config_profiles_selected(&selected,&custom)==1);
    user_profiles choices={};choices.slot=-1;files.clear();config_defaults(&c);
    for(int i=0;i<CONFIG_USER_PROFILE_MAX;i++) {
        char name[32];sprintf(name,"Cycle %d",i);config entry=c;config_customize_stream(&entry);
        entry.fps=i%2?30:60;entry.bitrate_kbps=13000+i*1000;entry.audio_mode=i%2;
        assert(config_profiles_create(&choices,&entry,name));
    }
    std::map<std::string,std::string> disk_before=files;
    c.language=2;c.ui_accent=3;c.left_min=17;strcpy(c.host,"kept-host");c.bitrate_kbps=12345;config_customize_stream(&c);
    for(int i=1;i<CONFIG_PROFILE_COUNT+CONFIG_USER_PROFILE_MAX;i++) {
        assert(config_cycle_profile(&choices,&c,1));
        assert(i<CONFIG_PROFILE_COUNT?config_match_factory(&c)==i:config_profiles_selected(&choices,&c)==i-CONFIG_PROFILE_COUNT);
        assert(c.language==2 && c.ui_accent==3 && c.left_min==17 && !strcmp(c.host,"kept-host"));
        assert(config_save(&c));config_load(&restarted);
        stream_options x,y;config_profile_capture(&x,&c);config_profile_capture(&y,&restarted);assert(!memcmp(&x,&y,sizeof(x)));
    }
    assert(config_cycle_profile(&choices,&c,1) && config_match_factory(&c)==CONFIG_PROFILE_SMOOTH);
    for(int i=CONFIG_PROFILE_COUNT+CONFIG_USER_PROFILE_MAX-1;i>0;i--) {
        assert(config_cycle_profile(&choices,&c,-1));
        assert(i<CONFIG_PROFILE_COUNT?config_match_factory(&c)==i:config_profiles_selected(&choices,&c)==i-CONFIG_PROFILE_COUNT);
    }
    choices.items[0].options.fps=90;memset(choices.items[1].name,'x',sizeof(choices.items[1].name));
    config_select_factory(&c,CONFIG_PROFILE_1080_SMOOTH);assert(config_cycle_profile(&choices,&c,1));
    assert(config_profiles_selected(&choices,&c)==2);
    config invalid_before=c;assert(!config_profiles_apply(&choices,1,&c) && !memcmp(&c,&invalid_before,sizeof(c)));
    choices.count=CONFIG_USER_PROFILE_MAX+1;assert(config_profiles_selected(&choices,&c)==-1);
    assert(!config_profiles_apply(&choices,0,&c));c.bitrate_kbps=12345;config_customize_stream(&c);
    assert(config_cycle_profile(&choices,&c,-1) && config_match_factory(&c)==CONFIG_PROFILE_1080_SMOOTH);
    user_profiles empty={};c.bitrate_kbps=12345;config_customize_stream(&c);
    assert(config_cycle_profile(&empty,&c,1) && config_match_factory(&c)==CONFIG_PROFILE_SMOOTH);
    config_customize_stream(&c);assert(config_match_factory(&c)==CONFIG_PROFILE_SMOOTH);
    c.vsync=false;assert(!config_match_factory(&c));
    assert(config_cycle_profile(&empty,&c,-1) && config_match_factory(&c)==CONFIG_PROFILE_1080_SMOOTH);
    puts("PASS: inline cycle 0/8 saved, full effective matching, invalid entries, canonical factory/global isolation and FPS restart persistence");
    // Failure at every durable write boundary: memory unchanged, intent recovers
    // renamed profile, current selection and ALL per-game references together.
    for(int failure=0;failure<=5;failure++) {
        files.clear();write_step=fail_step=0;user_profiles linked={};linked.slot=-1;config_defaults(&c);
        assert(config_profiles_create(&linked,&c,"Old"));assert(config_profiles_apply(&linked,0,&c));assert(config_save(&c));
        game_bindings links={};assert(binding_set(&links,"host-a",42,"user:Old"));assert(binding_set(&links,"host-b",84,"user:Old"));
        assert(binding_set(&links,"host-a",99,"factory:1"));char data[8192];size_t bytes=bindings_encode(&links,data,sizeof(data));assert(storage_write("game-bindings.tsv",data,bytes));
        user_profiles previous=linked;config settings=c;game_bindings previous_links=links;
        fail_step=failure;write_step=0;bool ok=profile_links_change(&linked,&links,&c,0,"New");fail_step=0;
        if(failure){assert(!ok && !memcmp(&linked,&previous,sizeof(linked)) && !memcmp(&links,&previous_links,sizeof(links)) && !memcmp(&c,&settings,sizeof(c)));}
        else assert(ok);
        user_profiles disk={};assert(config_profiles_load(&disk));game_bindings disk_links={};assert(bindings_decode(&disk_links,files["game-bindings.tsv"].c_str()));config_load(&settings);
        assert(profile_links_recover(&disk,&disk_links,&settings));
        const char *expected=failure?"Old":"New";
        assert(!strcmp(disk.items[0].name,expected) && !strcmp(settings.user_profile,expected));
        char expected_token[80];sprintf(expected_token,"user:%s",expected);assert(!strcmp(disk_links.items[0].profile,expected_token) && !strcmp(disk_links.items[1].profile,expected_token));
        assert(!strcmp(disk_links.items[2].profile,"factory:1"));
        config effective=settings;stream_options keep;config_profile_capture(&keep,&settings);
        assert(profile_links_change(&disk,&disk_links,&settings,0,""));assert(!disk.count && disk_links.count==1 && !settings.user_profile[0]);
        stream_options after;config_profile_capture(&after,&settings);assert(config_match_factory(&settings)==CONFIG_PROFILE_SMOOTH);
        assert(config_profiles_selected(&disk,&settings)==-1 && config_video_preset(&settings)==CONFIG_PROFILE_SMOOTH);
    }
    // Simulated process death after profile publication: a v2 undo record restores ALL preimages.
    files.clear();write_step=fail_step=0;user_profiles original_profiles={};original_profiles.slot=-1;config_defaults(&c);
    assert(config_profiles_create(&original_profiles,&c,"Before"));assert(config_profiles_apply(&original_profiles,0,&c));assert(config_save(&c));
    game_bindings original_bindings={};assert(binding_set(&original_bindings,"host",42,"user:Before"));char encoded[8192];size_t encoded_n=bindings_encode(&original_bindings,encoded,sizeof(encoded));assert(storage_write("game-bindings.tsv",encoded,encoded_n));
    size_t undo_n=0;char *undo=profile_txn_snapshot(&c,&undo_n);assert(undo && profile_link_write("profile-link-pending.tsv",undo,undo_n));
    user_profiles partial=original_profiles,removed=original_profiles;removed.count=0;assert(config_profiles_save(&partial,&removed));
    config broken=c;config_select_factory(&broken,CONFIG_PROFILE_SMOOTH);assert(config_save(&broken));
    user_profiles cold={};assert(config_profiles_load(&cold) && !cold.count);game_bindings cold_links=original_bindings;
    fail_step=write_step+1;assert(!profile_links_recover(&cold,&cold_links,&broken));fail_step=0;
    assert(profile_links_pending());assert(profile_links_recover(&cold,&cold_links,&broken));
    assert(cold.count==1 && !strcmp(cold.items[0].name,"Before") && !strcmp(broken.user_profile,"Before") && cold_links.count==1 && !profile_links_pending());
    // Corruption is detected before ANY restore writes; the checksum parser is strict on Xbox too.
    undo[undo_n-3]^=1;assert(storage_write("profile-link-pending.tsv",undo,undo_n));free(undo);
    std::map<std::string,std::string> corrupted=files;assert(!profile_links_recover(&cold,&cold_links,&broken) && files==corrupted);
    unsigned checksum;assert(!feature_checksum_read("1234567g\n",9,&checksum));assert(!feature_checksum_read("12345678x",9,&checksum));
    puts("PASS: Xbox-compatible checksum parsing without %n; rollback at each commit write and cold interrupted-transaction recovery; corrupt journal blocked before writes");
    files.clear();launch_settings prefs;launch_settings_load(&prefs);assert(!prefs.count && !prefs.read_only);
    assert(launch_settings_save(&prefs,"uuid-a",42,5,0));assert(launch_settings_save(&prefs,"uuid-b",42,0,1));
    launch_settings restarted_prefs;launch_settings_load(&restarted_prefs);assert(restarted_prefs.count==2 && restarted_prefs.items[1].desktop);
    launch_settings old_prefs=prefs;fail_write=true;assert(!launch_settings_save(&prefs,"uuid-a",42,10,0));assert(old_prefs.count==prefs.count && !memcmp(old_prefs.items,prefs.items,sizeof(prefs.items)));fail_write=false;
    assert(!launch_settings_save(&prefs,"uuid-a",42,11,0));files["launch-settings.tsv"]+="junk";launch_settings_load(&prefs);assert(prefs.read_only);
    puts("PASS: linked rename/delete transaction recovery at every write, persisted personal identity, all owned options invalidate selection, per-host/app opening preferences");
    puts("PASS: profiles CRUD/restart, effective stream options/global isolation, UTF-8/names/duplicates/limits, read/write/partial failures, checksum fallback and unknown-version preservation");
}
