#pragma once
#include <stdio.h>
#include <string.h>
/* Cold-path recoverable replacement. A verified temporary is promoted; retain
 * the preceding file as .bak. No claim of power-loss atomicity on untested FATX. */
static bool storage_recoverable_name(const char *name)
{return !strcmp(name,"moonlight.ini") || !strcmp(name,"hosts.tsv") || !strcmp(name,"sessions.tsv") || !strcmp(name,"favorites.tsv") || !strcmp(name,"game-bindings.tsv");}
static bool storage_replace_file(const char *path,const void *data,size_t bytes)
{
    char temp[160],backup[160];if(strlen(path)>150)return false;
    snprintf(temp,sizeof(temp),"%s.new",path);snprintf(backup,sizeof(backup),"%s.bak",path);
    FILE *f=fopen(temp,"wb");if(!f)return false;
    bool ok=fwrite(data,1,bytes,f)==bytes;if(fclose(f)!=0)ok=false;if(!ok)return false;
    f=fopen(temp,"rb");if(!f)return false;unsigned char block[512];size_t offset=0;
    while(offset<bytes && ok){size_t n=bytes-offset;if(n>sizeof(block))n=sizeof(block);if(fread(block,1,n,f)!=n || memcmp(block,(const unsigned char*)data+offset,n))ok=false;offset+=n;}
    if(fgetc(f)!=EOF)ok=false;if(fclose(f)!=0)ok=false;if(!ok)return false;
    f=fopen(path,"rb");bool existed=f!=NULL;if(f)fclose(f);
    if(existed){remove(backup);if(rename(path,backup)!=0)return false;}
    if(rename(temp,path)!=0){if(existed)rename(backup,path);return false;}
    return true;
}
