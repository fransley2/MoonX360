#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "cover_decode.h"
#include "ui/image.h"
#define WALLPAPER_ENCODED_MAX (8u*1024u*1024u)
static bool wallpaper_allowed(const unsigned char *data,size_t len)
{
    int w=0,h=0,n=0;
    bool png=data && len>=8 && !memcmp(data,"\x89PNG\r\n\x1a\n",8);
    bool jpeg=data && len>=3 && data[0]==255 && data[1]==216 && data[2]==255;
    return (png || jpeg) && len<=WALLPAPER_ENCODED_MAX &&
        stbi_info_from_memory(data,(int)len,&w,&h,&n) && w>0 && h>0 && w<=2048 && h<=2048;
}
static uint32_t *wallpaper_decode(const unsigned char *data,size_t len,int *w,int *h)
{
    if(!wallpaper_allowed(data,len))return NULL;
    return image_decode_argb(data,len,w,h);
}
static unsigned char *wallpaper_read_file(const char *path,size_t *bytes)
{
    *bytes=0;FILE *f=fopen(path,"rb");if(!f)return NULL;
    if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}
    long n=ftell(f);
    if(n<=0 || (unsigned long)n>WALLPAPER_ENCODED_MAX || fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
    unsigned char *data=(unsigned char*)malloc((size_t)n);
    bool ok=data && fread(data,1,(size_t)n,f)==(size_t)n;
    if(fclose(f))ok=false;
    if(!ok){free(data);return NULL;}
    *bytes=(size_t)n;return data;
}
