/* Host arithmetic model only; does not emulate Xenon execution or timing. */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
typedef struct { uint8_t b[16]; } __vector4;
static uint16_t h(__vector4 v,int i){return (uint16_t)((v.b[i*2]<<8)|v.b[i*2+1]);}
static int16_t sh(__vector4 v,int i){return (int16_t)h(v,i);}
static uint32_t w(__vector4 v,int i){int n=i*4;return ((uint32_t)v.b[n]<<24)|((uint32_t)v.b[n+1]<<16)|((uint32_t)v.b[n+2]<<8)|v.b[n+3];}
static void ph(__vector4 *v,int i,int n){v->b[i*2]=(uint8_t)(n>>8);v->b[i*2+1]=(uint8_t)n;}
static void pw(__vector4 *v,int i,uint32_t n){int x=i*4;v->b[x]=(uint8_t)(n>>24);v->b[x+1]=(uint8_t)(n>>16);v->b[x+2]=(uint8_t)(n>>8);v->b[x+3]=(uint8_t)n;}
static int clip(int x,int lo,int hi){return x<lo?lo:x>hi?hi:x;}
static __vector4 __vzero(void){__vector4 r={{0}};return r;}
static __vector4 load(const void *p){__vector4 r;memcpy(r.b,p,16);return r;}
#define LOADU(p) load(p)
static __vector4 __vspltish(int n){__vector4 r;int i;for(i=0;i<8;i++)ph(&r,i,n);return r;}
static __vector4 __vspltisw(int n){__vector4 r;int i;for(i=0;i<4;i++)pw(&r,i,(uint32_t)n);return r;}
static __vector4 __vsldoi(__vector4 a,__vector4 b,int n){__vector4 r;int i;for(i=0;i<16;i++)r.b[i]=i+n<16?a.b[i+n]:b.b[i+n-16];return r;}
static __vector4 __vperm(__vector4 a,__vector4 b,__vector4 m){__vector4 r;int i;for(i=0;i<16;i++){int n=m.b[i]&31;r.b[i]=n<16?a.b[n]:b.b[n-16];}return r;}
static __vector4 __vadduhm(__vector4 a,__vector4 b){__vector4 r;int i;for(i=0;i<8;i++)ph(&r,i,h(a,i)+h(b,i));return r;}
static __vector4 __vsubuhm(__vector4 a,__vector4 b){__vector4 r;int i;for(i=0;i<8;i++)ph(&r,i,h(a,i)-h(b,i));return r;}
static __vector4 __vaddshs(__vector4 a,__vector4 b){__vector4 r;int i;for(i=0;i<8;i++)ph(&r,i,clip(sh(a,i)+sh(b,i),-32768,32767));return r;}
static __vector4 __vsrah(__vector4 a,__vector4 b){__vector4 r;int i;for(i=0;i<8;i++)ph(&r,i,sh(a,i)>>(h(b,i)&15));return r;}
static __vector4 __vslh(__vector4 a,__vector4 b){__vector4 r;int i;for(i=0;i<8;i++)ph(&r,i,h(a,i)<<(h(b,i)&15));return r;}
static __vector4 __vadduwm(__vector4 a,__vector4 b){__vector4 r;int i;for(i=0;i<4;i++)pw(&r,i,w(a,i)+w(b,i));return r;}
static __vector4 __vslw(__vector4 a,__vector4 b){__vector4 r;int i;for(i=0;i<4;i++)pw(&r,i,w(a,i)<<(w(b,i)&31));return r;}
static __vector4 __vsraw(__vector4 a,__vector4 b){__vector4 r;int i;for(i=0;i<4;i++)pw(&r,i,(uint32_t)((int32_t)w(a,i)>>(w(b,i)&31)));return r;}
static __vector4 __vmrghb(__vector4 a,__vector4 b){__vector4 r;int i;for(i=0;i<8;i++){r.b[2*i]=a.b[i];r.b[2*i+1]=b.b[i];}return r;}
static __vector4 __vpkshus(__vector4 a,__vector4 b){__vector4 r;int i;for(i=0;i<8;i++){r.b[i]=(uint8_t)clip(sh(a,i),0,255);r.b[i+8]=(uint8_t)clip(sh(b,i),0,255);}return r;}
static __vector4 __vpkswss(__vector4 a,__vector4 b){__vector4 r;int i;for(i=0;i<4;i++){ph(&r,i,clip((int32_t)w(a,i),-32768,32767));ph(&r,i+4,clip((int32_t)w(b,i),-32768,32767));}return r;}
static void store16(__vector4 v,uint8_t *p){memcpy(p,v.b,16);}
static void store_n(__vector4 v,uint8_t *p,int n){memcpy(p,v.b,n);}
static __vector4 __vsubuwm(__vector4 a,__vector4 b){__vector4 r;int i;for(i=0;i<4;i++)pw(&r,i,w(a,i)-w(b,i));return r;}
static __vector4 __vpkuwum(__vector4 a,__vector4 b){__vector4 r;int i;for(i=0;i<4;i++){ph(&r,i,(int)w(a,i));ph(&r,i+4,(int)w(b,i));}return r;}
static int idct_clears_block=1;
#define FUNCC(x) x ## _reference
#define pixel uint8_t
#define dctcoef int16_t
#define av_clip_pixel(x) clip(x,0,255)
#define scan8 xbox_scan8
