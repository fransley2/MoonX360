#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "../../../src/config.h"
#include "../../../src/safe_storage.h"
static bool storage_ok;static char storage_root[128]=".";
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
    for(unsigned i=0;i<sizeof(names)/sizeof(*names);i++)remove(names[i]);
    puts("PASS: actual XDK storage read/write, verified replacement, missing-primary backup recovery, unavailable storage and no certificate backup substitution");
}
