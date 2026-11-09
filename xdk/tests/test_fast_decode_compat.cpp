#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include "../../src/config.h"
static std::map<std::string,std::string> files;
static bool fail_backup;
static unsigned writes;
char *storage_read(const char *name,size_t *n) {
    if(!files.count(name)){errno=ENOENT;return NULL;}
    const std::string &s=files[name];if(n)*n=s.size();
    char *p=(char*)malloc(s.size()+1);memcpy(p,s.c_str(),s.size()+1);return p;
}
bool storage_write(const char *name,const void *p,size_t n) {
    if(fail_backup && strstr(name,".fastdecode.bak"))return false;
    writes++;files[name]=std::string((const char*)p,n);return true;
}
static std::string compat_profile(int version,int fast) {
    const int counts[]={0,15,16,19,22,22};
    int values[]={1280,720,60,12000,1024,0,fast,0,1,1,0,1,0,1,4,0,0,0,1,0,0,1};
    std::string text="XENON_PROFILES "+std::to_string(version)+" 1 1\nSaved";
    for(int i=0;i<counts[version];i++)text+="\t"+std::to_string(values[i]);
    text+='\n';unsigned hash=2166136261u;
    for(size_t i=0;i<text.size();i++)hash=(hash^(unsigned char)text[i])*16777619u;
    char tail[32];sprintf(tail,"CHECKSUM %08x\n",hash);return text+tail;
}
int main() {
    config c;config_defaults(&c);assert(c.fast_decode==1 && c.stream_profile==CONFIG_PROFILE_SMOOTH);
    for(int value=0;value<=2;value++) {
        files.clear();files["moonlight.ini"]="stream_profile=custom\nfast_decode="+std::to_string(value)+"\ncrt_clarity=1\n";
        std::string before=files["moonlight.ini"];unsigned old_writes=writes;
        config_load(&c);assert(c.fast_decode==(value!=0) && c.crt_clarity);assert(writes==old_writes);
        fail_backup=value==2;
        if(fail_backup){assert(!config_save(&c));assert(files["moonlight.ini"]==before);}
        fail_backup=false;assert(config_save(&c));
        if(value==2)assert(files["moonlight.ini.fastdecode.bak"]==before);
        config restarted;config_load(&restarted);assert(restarted.fast_decode==(value!=0) && restarted.crt_clarity);
        std::string backup=files["moonlight.ini.fastdecode.bak"];
        c.fast_decode=0;c.crt_clarity=false;assert(config_save(&c));config_load(&restarted);assert(!restarted.fast_decode && !restarted.crt_clarity);
        assert(files["moonlight.ini.fastdecode.bak"]==backup);
    }
    for(int version=1;version<=5;version++)for(int value=0;value<=(version==5?2:1);value++) {
        files.clear();files["profiles-a.tsv"]=compat_profile(version,value);
        std::string before=files["profiles-a.tsv"];unsigned old_writes=writes;
        user_profiles profiles={};assert(config_profiles_load(&profiles));assert(writes==old_writes);
        assert(profiles.items[0].options.fast_decode==(value!=0));
        assert(config_profiles_apply(&profiles,0,&c));assert(c.fast_decode==(value!=0));
        user_profiles next=profiles;next.items[0].options.fast_decode=!c.fast_decode;
        fail_backup=value==2 || version<5;
        if(fail_backup){assert(!config_profiles_save(&profiles,&next));assert(files["profiles-a.tsv"]==before && !files.count("profiles-b.tsv"));}
        fail_backup=false;assert(config_profiles_save(&profiles,&next));
        if(value==2 || version<5)assert(files["profiles-a.tsv.fastdecode.bak"]==before);
        user_profiles restarted={};assert(config_profiles_load(&restarted));
        assert(restarted.items[0].options.fast_decode==!c.fast_decode);
        assert(files["profiles-b.tsv"].find("XENON_PROFILES 5 ")==0);
        // Subsequent publication must never replace the original backup.
        std::string backup=files["profiles-a.tsv.fastdecode.bak"];
        assert(config_profiles_save(&restarted,&restarted));
        assert(files["profiles-a.tsv.fastdecode.bak"]==backup);
    }
    puts("PASS: legacy defaults, INI 0/1/2 and profile formats 1-5; read-only load, Boolean runtime, verified pre-conversion backups, failed-backup rollback and immutable backup");
}
