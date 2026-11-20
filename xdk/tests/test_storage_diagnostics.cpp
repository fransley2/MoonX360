#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <string>
static int failure;
static FILE *tested_open(const char *p,const char *m){if(failure==1 && strstr(p,".new") && !strcmp(m,"wb")){errno=EACCES;return NULL;}return fopen(p,m);}
static size_t tested_write(const void *p,size_t a,size_t n,FILE *f){if(failure==2){size_t done=fwrite(p,a,n-1,f);errno=ENOSPC;return done;}return fwrite(p,a,n,f);}
static size_t tested_read(void *p,size_t a,size_t n,FILE *f){if(failure==3){size_t got=fread(p,a,n-1,f);errno=EIO;return got;}return fread(p,a,n,f);}
static int tested_close(FILE *f){int n=fclose(f);if(failure==4){errno=EIO;return EOF;}return n;}
static int tested_flush(FILE *f){if(failure==6){errno=ENOSPC;return EOF;}return fflush(f);}
static int tested_rename(const char *a,const char *b){if(failure==5 && strstr(a,".new")){errno=EACCES;return -1;}return rename(a,b);}
#define fopen tested_open
#define fwrite tested_write
#define fread tested_read
#define fclose tested_close
#define rename tested_rename
#define fflush tested_flush
#include "../../src/safe_storage.h"
#undef fopen
#undef fwrite
#undef fread
#undef fclose
#undef rename
#undef fflush
static std::string read(const char *name){FILE *f=fopen(name,"rb");assert(f);char data[512];size_t n=fread(data,1,sizeof(data),f);fclose(f);return std::string(data,n);}
int main(){
 const char *path="coverfix-storage-test.tsv";remove(path);remove("coverfix-storage-test.tsv.new");remove("coverfix-storage-test.tsv.bak");
 assert(storage_replace_file(path,"previous",8));
 const char *stages[]={"","open-temporary","write-temporary","read-verification","close-temporary","promote-temporary","flush-temporary"};
 for(failure=1;failure<=6;failure++){
  assert(!storage_replace_file(path,"replacement",11));
  assert(!strcmp(storage_write_diagnostic.stage,stages[failure]));
  assert(storage_write_diagnostic.expected==11 && storage_write_diagnostic.error!=0 && errno==storage_write_diagnostic.error);
  if(failure==2)assert(storage_write_diagnostic.error==ENOSPC && storage_write_diagnostic.actual==10);
  if(failure==3)assert(storage_write_diagnostic.actual==10);
  assert(read(path)=="previous");
 }
 failure=0;assert(storage_replace_file(path,"replacement",11));assert(read(path)=="replacement" && read("coverfix-storage-test.tsv.bak")=="previous");
 char long_path[152];memset(long_path,'x',151);long_path[151]=0;assert(!storage_replace_file(long_path,"",0) && !strcmp(storage_write_diagnostic.stage,"path"));
 remove(path);remove("coverfix-storage-test.tsv.new");remove("coverfix-storage-test.tsv.bak");
 puts("PASS: real storage replacement, immediate stage/errno/byte diagnostics for open/short-write/read/close/promotion failures, previous-file preservation, backup and bounded paths");
}
