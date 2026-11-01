#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include "../../src/launch_settings.h"
static std::map<std::string,std::string> files;
static bool fail_write,fail_read,partial;
static unsigned writes;
char *storage_read(const char *name,size_t *n)
{
    if(fail_read){errno=EIO;return NULL;}
    if(!files.count(name)){errno=ENOENT;return NULL;}
    const std::string &s=files[name];char *p=(char*)malloc(s.size()+1);
    memcpy(p,s.c_str(),s.size()+1);if(n)*n=s.size();return p;
}
bool storage_write(const char *name,const void *p,size_t n)
{
    writes++;if(fail_write){errno=EACCES;return false;}
    if(partial){errno=EIO;return false;}
    files[name]=std::string((const char*)p,n);return true;
}
static std::string checked(const std::string &payload)
{char tail[40];sprintf(tail,"CHECKSUM %08x\n",feature_checksum(payload.c_str(),payload.size()));return payload+tail;}
int main()
{
    // The actual Save path begins by loading this file, before calling save.
    files["launch-settings.tsv"]=checked("MOONX_LAUNCH 1\nuuid:pc\t42\t5\t0\n");
    launch_settings p;launch_settings_load(&p);unsigned before=writes;
    bool saved=launch_settings_save(&p,"uuid:pc",42,0,0);
    printf("Canonical v1 -> load read_only=%d -> Save=%d -> writes=%u\n",p.read_only,saved,writes-before);fflush(stdout);
    assert(saved && !p.read_only && p.count==1 && p.items[0].seconds==0);
    assert(files["launch-settings.tsv"].find("MOONX_LAUNCH 1\n")==0);
    for(int seconds=-1;seconds<=10;seconds++) {
        assert(launch_settings_save(&p,"uuid:pc",42,seconds,seconds==0));
        launch_settings reopened;launch_settings_load(&reopened);
        assert(!reopened.read_only && reopened.count==1 && reopened.items[0].seconds==seconds);
        assert(!strcmp(reopened.items[0].host,"uuid:pc"));
        p=reopened;
    }
    // Accept the malformed no-newline header written by the previous build,
    // migrate to canonical bytes on Save, and never duplicate its stable key.
    files["launch-settings.tsv"]=checked("MOONX_LAUNCH 1uuid:pc\t42\t-1\t0\n");
    launch_settings_load(&p);assert(!p.read_only && p.count==1);
    assert(launch_settings_save(&p,"uuid:pc",42,0,0) && p.count==1);
    files["launch-settings.tsv"]=checked("MOONX_LAUNCH 1192.168.1.2\t42\t5\t0\n");
    launch_settings_load(&p);assert(!p.read_only && !strcmp(p.items[0].host,"192.168.1.2"));
    assert(launch_settings_save(&p,"192.168.1.2",42,0,0));
    launch_settings old=p;std::string previous=files["launch-settings.tsv"];
    fail_write=true;assert(!launch_settings_save(&p,"uuid:pc",42,10,0));fail_write=false;
    assert(p.count==old.count && !memcmp(p.items,old.items,sizeof(p.items)) && files["launch-settings.tsv"]==previous);
    partial=true;assert(!launch_settings_save(&p,"uuid:pc",42,10,0));partial=false;
    assert(!memcmp(p.items,old.items,sizeof(p.items)));
    files["launch-settings.tsv"]="MOONX_LAUNCH 99\n";launch_settings_load(&p);
    before=writes;assert(p.read_only && !launch_settings_save(&p,"uuid:pc",42,5,0) && writes==before);
    files["launch-settings.tsv"]="MOONX_LAUNCH 10\n";launch_settings_load(&p);assert(p.read_only && p.error==LAUNCH_SETTINGS_VERSION);
    files.erase("launch-settings.tsv");launch_settings_load(&p);assert(!p.read_only);
    assert(launch_settings_save(&p,"uuid:other",42,0,1));
    files["moonlight.ini"]="ui_motion=0\nlaunch_cover_ms=0\n";
    config migrated;config_load(&migrated);assert(!migrated.ui_motion && migrated.launch_animation && !migrated.launch_cover_ms);
    config c;config_defaults(&c);assert(c.launch_animation);c.launch_animation=false;c.launch_cover_ms=0;assert(config_save(&c));
    config restarted;config_load(&restarted);assert(restarted.launch_cover_ms==0 && !restarted.launch_animation);
    c.launch_cover_ms=10000;assert(config_save(&c));config_load(&restarted);assert(restarted.launch_cover_ms==10000);
    puts("PASS: exact load/Save/reopen path, canonical and legacy header, 0/inherit/10, host isolation, write failure, unknown version and global INI restart");
}
