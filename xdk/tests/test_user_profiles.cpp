
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include "../../src/config.h"
static std::map<std::string,std::string> files;
static bool fail_read,fail_write,short_write;
char *storage_read(const char *name,size_t *n){
    if(fail_read){errno=EIO;return NULL;}if(!files.count(name)){errno=ENOENT;return NULL;}
    const std::string &s=files[name];char *p=(char*)malloc(s.size()+1);memcpy(p,s.c_str(),s.size()+1);if(n)*n=s.size();return p;
}
bool storage_write(const char *name,const void *data,size_t n){
    assert(!strcmp(name,"profiles-a.tsv") || !strcmp(name,"profiles-b.tsv") || !strcmp(name,"moonlight.ini"));
    if(fail_write)return false;files[name]=std::string((const char*)data,short_write?n/2:n);return !short_write;
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
    puts("PASS: profiles CRUD/restart, effective stream options/global isolation, UTF-8/names/duplicates/limits, read/write/partial failures, checksum fallback and unknown-version preservation");
}
