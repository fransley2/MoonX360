/* Arithmetic/access model only. Never a Xenon timing measurement. */
#pragma once
#include "vmx_idct_emulation.h"
#include <stddef.h>
extern uintptr_t mc_source,mc_destination;
extern int mc_stride,mc_width,mc_height,mc_variant;
extern unsigned long long mc_reads[3],mc_writes[3],mc_scope_calls[3];
void mc_scope(uint8_t *dst,uint8_t *src,int stride,int width,int height,int variant);
static void mc_check_write(uint8_t *p,int count)
{
    uintptr_t address=(uintptr_t)p;
    if(address>=mc_destination && address<mc_destination+(uintptr_t)mc_height*mc_stride+32) {
        int i;
        for(i=0;i<count;i++) {
            uintptr_t relative=address+i-mc_destination;
            if(relative/(unsigned)mc_stride>=(unsigned)mc_height || relative%(unsigned)mc_stride>=(unsigned)mc_width) {
                fprintf(stderr,"model out-of-block store: width=%d height=%d stride=%d offset=%llu bytes=%d\n",mc_width,mc_height,mc_stride,(unsigned long long)relative,count);abort();
            }
        }
    }
    mc_writes[mc_variant]+=count;
}
static __vector4 mc_load(const void *p)
{
    uintptr_t address=(uintptr_t)p;
    /* Conservative row padding includes aligned VMX partial-load footprint.
     * Local temporaries are separately allocated, outside this source region. */
    if(address>=mc_source-4*mc_stride-64 && address<mc_source+(mc_height+5)*mc_stride+64) {
        uintptr_t first=address&~(uintptr_t)15,last=(address+15)&~(uintptr_t)15;
        if(first<mc_source-2*mc_stride-32 || last+16>mc_source+(mc_height+3)*mc_stride+64)abort();
    }
    mc_reads[mc_variant]+=16;return load(p);
}
#undef LOADU
#define LOADU(p) mc_load(p)
static __vector4 __vspltisb(int n){__vector4 v;memset(v.b,n,16);return v;}
static __vector4 __vmrglb(__vector4 a,__vector4 b){__vector4 r;int i;for(i=0;i<8;i++){r.b[2*i]=a.b[i+8];r.b[2*i+1]=b.b[i+8];}return r;}
static __vector4 __vavgub(__vector4 a,__vector4 b){__vector4 r;int i;for(i=0;i<16;i++)r.b[i]=(uint8_t)((a.b[i]+b.b[i]+1)>>1);return r;}
static __vector4 __vand(__vector4 a,__vector4 b){__vector4 r;int i;for(i=0;i<16;i++)r.b[i]=a.b[i]&b.b[i];return r;}
static __vector4 __vor(__vector4 a,__vector4 b){__vector4 r;int i;for(i=0;i<16;i++)r.b[i]=a.b[i]|b.b[i];return r;}
static __vector4 __vxor(__vector4 a,__vector4 b){__vector4 r;int i;for(i=0;i<16;i++)r.b[i]=a.b[i]^b.b[i];return r;}
static __vector4 __vsel(__vector4 a,__vector4 b,__vector4 m){__vector4 r;int i;for(i=0;i<16;i++)r.b[i]=(a.b[i]&~m.b[i])|(b.b[i]&m.b[i]);return r;}
static __vector4 __vupkhsh(__vector4 a){__vector4 r;int i;for(i=0;i<4;i++)pw(&r,i,(uint32_t)(int32_t)sh(a,i));return r;}
static __vector4 __vupklsh(__vector4 a){__vector4 r;int i;for(i=0;i<4;i++)pw(&r,i,(uint32_t)(int32_t)sh(a,i+4));return r;}
static __vector4 __lvx(const void *p,int off){return mc_load((const void*)(((uintptr_t)p+off)&~(uintptr_t)15));}
static void __stvx(__vector4 v,void *p,int off){uint8_t *dst=(uint8_t*)(((uintptr_t)p+off)&~(uintptr_t)15);mc_check_write(dst,16);memcpy(dst,v.b,16);}
static void __stvlx(__vector4 v,void *p,int off){uint8_t *dst=(uint8_t*)p+off;int n=16-((uintptr_t)dst&15);mc_check_write(dst,n);memcpy(dst,v.b,n);}
static void __stvrx(__vector4 v,void *p,int off){uint8_t *dst=(uint8_t*)p+off;int n=(uintptr_t)dst&15;dst-=n;mc_check_write(dst,n);memcpy(dst,v.b+16-n,n);}
static __vector4 __lvsr(const void *p,int off){__vector4 v;int i,a=((uintptr_t)p+off)&15;for(i=0;i<16;i++)v.b[i]=(uint8_t)(16-a+i);return v;}
static void __stvewx(__vector4 v,void *p,int off){uintptr_t addr=(uintptr_t)p+off;int lane=(addr&15)&~3;uint8_t *dst=(uint8_t*)(addr&~(uintptr_t)3);mc_check_write(dst,4);memcpy(dst,v.b+lane,4);}
