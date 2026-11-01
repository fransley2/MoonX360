#pragma once
#include <stdio.h>
#include <string.h>
/* Cold-path recoverable replacement. A verified temporary is promoted; retain
 * the preceding file as .bak. No claim of power-loss atomicity on untested FATX. */
static bool storage_recoverable_name(const char *name)
{return !strcmp(name,"moonlight.ini") || !strcmp(name,"hosts.tsv") || !strcmp(name,"sessions.tsv") || !strcmp(name,"favorites.tsv") || !strcmp(name,"game-bindings.tsv") || !strcmp(name,"launch-settings.tsv") || !strcmp(name,"profile-link-pending.tsv");}

#include <errno.h>
#ifndef STORAGE_OS_ERROR
#define STORAGE_OS_ERROR() 0u
#endif
struct storage_write_diagnostic {const char *stage;int error;unsigned os_error;size_t expected,actual;};
static struct storage_write_diagnostic storage_write_diagnostic;
static bool storage_write_failed(const char *stage,int error,unsigned os_error,size_t expected,size_t actual)
{
    storage_write_diagnostic.stage=stage;storage_write_diagnostic.error=error;
    storage_write_diagnostic.os_error=os_error;storage_write_diagnostic.expected=expected;
    storage_write_diagnostic.actual=actual;errno=error;return false;
}
static bool storage_replace_file(const char *path,const void *data,size_t bytes)
{
    memset(&storage_write_diagnostic,0,sizeof(storage_write_diagnostic));
    char temp[160],backup[160];
    if(strlen(path)>150)return storage_write_failed("path",EINVAL,0,bytes,0);
    snprintf(temp,sizeof(temp),"%s.new",path);snprintf(backup,sizeof(backup),"%s.bak",path);
    errno=0;FILE *f=fopen(temp,"wb");int e=errno;unsigned os=STORAGE_OS_ERROR();
    if(!f)return storage_write_failed("open-temporary",e,os,bytes,0);
    errno=0;size_t written=fwrite(data,1,bytes,f);e=errno;os=STORAGE_OS_ERROR();
    if(written!=bytes){fclose(f);return storage_write_failed("write-temporary",e,os,bytes,written);}
    errno=0;int flushed=fflush(f);e=errno;os=STORAGE_OS_ERROR();
    if(flushed){fclose(f);return storage_write_failed("flush-temporary",e,os,bytes,written);}
    errno=0;int rc=fclose(f);e=errno;os=STORAGE_OS_ERROR();
    if(rc)return storage_write_failed("close-temporary",e,os,bytes,written);
    errno=0;f=fopen(temp,"rb");e=errno;os=STORAGE_OS_ERROR();
    if(!f)return storage_write_failed("open-verification",e,os,bytes,0);
    unsigned char block[512];size_t offset=0;
    while(offset<bytes) {
        size_t n=bytes-offset;if(n>sizeof(block))n=sizeof(block);
        errno=0;size_t got=fread(block,1,n,f);e=errno;os=STORAGE_OS_ERROR();
        if(got!=n){fclose(f);return storage_write_failed("read-verification",e,os,bytes,offset+got);}
        if(memcmp(block,(const unsigned char*)data+offset,n)){fclose(f);return storage_write_failed("compare-verification",EIO,0,bytes,offset+got);}
        offset+=n;
    }
    int extra=fgetc(f);e=errno;os=STORAGE_OS_ERROR();
    if(extra!=EOF){fclose(f);return storage_write_failed("verification-length",EIO,os,bytes,offset+1);}
    if(ferror(f)){fclose(f);return storage_write_failed("verification-eof",e,os,bytes,offset);}
    errno=0;rc=fclose(f);e=errno;os=STORAGE_OS_ERROR();
    if(rc)return storage_write_failed("close-verification",e,os,bytes,offset);
    errno=0;f=fopen(path,"rb");e=errno;os=STORAGE_OS_ERROR();bool existed=f!=NULL;
    if(f)fclose(f);else if(e!=ENOENT)return storage_write_failed("open-previous",e,os,bytes,written);
    if(existed) {
        errno=0;rc=remove(backup);e=errno;os=STORAGE_OS_ERROR();
        if(rc && e!=ENOENT)return storage_write_failed("remove-backup",e,os,bytes,written);
        errno=0;rc=rename(path,backup);e=errno;os=STORAGE_OS_ERROR();
        if(rc)return storage_write_failed("retain-previous",e,os,bytes,written);
    }
    errno=0;rc=rename(temp,path);e=errno;os=STORAGE_OS_ERROR();
    if(rc){if(existed)rename(backup,path);return storage_write_failed("promote-temporary",e,os,bytes,written);}
    storage_write_diagnostic.stage="complete";storage_write_diagnostic.expected=storage_write_diagnostic.actual=bytes;
    return true;
}
