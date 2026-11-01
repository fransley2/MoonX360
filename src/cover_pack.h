#pragma once
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
/* Bounded PackBits-style native ARGB thumbnail representation. Literal packets
 * cap at 128 pixels; incompressible thumbnails use raw storage instead. */
static size_t cover_pack(const uint32_t *p,size_t count,FILE *file)
{
    size_t i=0,bytes=0;
    while(i<count) {
        size_t run=1;while(run<128 && i+run<count && p[i+run]==p[i])run++;
        size_t n=run;
        if(run<3) {
            n=0;
            while(n<128 && i+n<count) {
                if(n && i+n+2<count && p[i+n]==p[i+n+1] && p[i+n]==p[i+n+2])break;
                n++;
            }
        }
        unsigned char control=(unsigned char)((run>=3?128:0)|(n-1));
        size_t payload=run>=3?4:n*4;
        if(file && (fwrite(&control,1,1,file)!=1 || fwrite(p+i,1,payload,file)!=payload))return 0;
        bytes+=1+payload;i+=n;
    }
    return bytes;
}
static bool cover_unpack(FILE *file,uint32_t *out,const uint32_t *expected,size_t count,bool packed)
{
    size_t i=0;uint32_t block[128];
    while(i<count) {
        int control=packed?fgetc(file):0;if(control<0)return false;
        size_t n=packed?(control&127)+1:count-i;if(n>128 && !packed)n=128;
        if(n>count-i)return false;
        if(packed && (control&128)) {
            if(fread(block,4,1,file)!=1)return false;
            for(size_t j=1;j<n;j++)block[j]=block[0];
        } else if(fread(block,4,n,file)!=n)return false;
        if(expected && memcmp(block,expected+i,n*4))return false;
        if(out)memcpy(out+i,block,n*4);i+=n;
    }
    return fgetc(file)==EOF && !ferror(file);
}
