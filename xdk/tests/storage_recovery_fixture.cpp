#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "../../../src/config.h"
#include "../../../src/safe_storage.h"
static void plat_log(const char*){}
static bool storage_ok;static char storage_root[128]=".";
static const char *storage_dir(void){return storage_ok?storage_root:NULL;}
/* ACTUAL_STORAGE */
int main(){
    const char *names[]={"hosts.tsv","hosts.tsv.bak","hosts.tsv.new","client.pem","client.pem.bak"};
    for(unsigned i=0;i<sizeof(names)/sizeof(*names);i++){FILE *f=fopen(names[i],"rb");assert(!f);}
    errno=0;assert(!storage_read("hosts.tsv",NULL) && errno==EIO);storage_ok=true;
    assert(storage_write("hosts.tsv","first",5));assert(storage_write("hosts.tsv","second",6));
    size_t bytes=0;char *text=storage_read("hosts.tsv",&bytes);assert(text && bytes==6 && !strcmp(text,"second"));free(text);
    assert(remove("hosts.tsv")==0);text=storage_read("hosts.tsv",&bytes);assert(text && bytes==5 && !strcmp(text,"first"));free(text);
    FILE *f=fopen("client.pem.bak","wb");assert(f);fputs("not-a-real-certificate",f);fclose(f);
    errno=0;assert(!storage_read("client.pem",NULL) && errno==ENOENT);
    assert(storage_write("launch-settings.tsv","0/inherit",9));
    text=storage_read("launch-settings.tsv",&bytes);assert(text && bytes==9 && !memcmp(text,"0/inherit",9));free(text);
    strcpy(storage_root,"does-not-exist-coverfix");errno=0;
    assert(!storage_write("launch-settings.tsv","new",3) && !strcmp(storage_write_diagnostic.stage,"storage-directory") && storage_write_diagnostic.expected==3);
    strcpy(storage_root,".");remove("launch-settings.tsv");remove("launch-settings.tsv.bak");remove("launch-settings.tsv.new");
    for(unsigned i=0;i<sizeof(names)/sizeof(*names);i++)remove(names[i]);
    const char *reports[]={"last-session.txt","decoder-session.txt","performance-session.txt",
        "present-trace.txt","last-log.txt","h264-parallel-session.txt",
        "h264-parallel-trace.txt","h264-stage-session.txt",
        "latency-capture.txt","frame-cadence.txt"};
    assert(storage_write("client.pem","untouched",9));
    for(int session=1;session<=6;session++) {
        char value[16];int length=snprintf(value,sizeof(value),"session%d",session);
        for(unsigned i=0;i<sizeof(reports)/sizeof(*reports);i++)
            assert(storage_write(reports[i],value,length));
        plat_archive_session_reports();
    }
    for(int slot=1;slot<=5;slot++) {
        char filename[96],expected[16];
        snprintf(filename,sizeof(filename),"session-archive-%d-index.txt",slot);
        char *manifest=storage_read(filename,NULL);
        assert(manifest && strstr(manifest,"status=complete") && strstr(manifest,"reports=10"));free(manifest);
        snprintf(filename,sizeof(filename),"session-archive-%d-last-session.txt",slot);
        char *archived=storage_read(filename,NULL);
        snprintf(expected,sizeof(expected),"session%d",slot==1?6:slot);
        assert(archived && !strcmp(archived,expected));free(archived);
        snprintf(filename,sizeof(filename),"session-archive-%d-index.txt",slot);remove(filename);
        for(unsigned i=0;i<sizeof(reports)/sizeof(*reports);i++) {
            snprintf(filename,sizeof(filename),"session-archive-%d-%s",slot,reports[i]);remove(filename);
        }
    }
    char *state=storage_read("session-archive-next.txt",NULL);
    assert(state && !strcmp(state,"2\n"));free(state);
    char *credential=storage_read("client.pem",NULL);
    assert(credential && !strcmp(credential,"untouched"));free(credential);
    for(unsigned i=0;i<sizeof(reports)/sizeof(*reports);i++)remove(reports[i]);
    remove("session-archive-next.txt");remove("client.pem");
    puts("PASS: XDK storage and five-slot session archive: six sessions rotate, reports stay readable, unrelated credential stays untouched");
}
