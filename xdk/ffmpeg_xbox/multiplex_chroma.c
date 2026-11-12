/* Adapted from HG-ERIK/multiplex360, commit
 * bda33b8602b9a0b9869effdd5cd7c0fadcb34216 (GPL-3.0 repository).
 * Origin: external/ffmpeg360/libavcodec/xbox360/h264_chroma_xb.c
 * Adaptation: raw pixel function tables for FFmpeg 1.2, exact-width stores,
 * defined NULL handling, and no unnecessary second QPEL8 source vector.
 * No FFmpeg 0.7 DSPContext, init, threading or decoder code is imported.
 * See docs/VMX128_AB_EXPERIMENT.md and THIRD_PARTY_NOTICES.md.
 */
#include <stdint.h>
#include <string.h>
#ifndef VMX_AB_HOST_MODEL
#include <xtl.h>
#define LOADU(p) __vor(__lvlx((p),0),__lvrx((p),16))
#else
#include "vmx_mc_emulation.h"
#endif
#include "vmx_ab.h"
#include "vmx_ab_store.h"
/* v * k for k in 0..7 (16-bit lanes, modular). */
static __inline __vector4 mul07( __vector4 v, int k, __vector4 one, __vector4 two )
{
    __vector4 r = __vxor( v, v );
    if( k & 1 ) r = __vadduhm( r, v );
    if( k & 2 ) r = __vadduhm( r, __vslh( v, one ) );
    if( k & 4 ) r = __vadduhm( r, __vslh( v, two ) );
    return r;
}

/* Horizontal pass for one source row: 8*a + x*(b-a) for the first 8 pixels. */
static __inline __vector4 hpass( const uint8_t* src, int x, __vector4 zero, __vector4 one, __vector4 two,
                                 __vector4 three )
{
    __vector4 v = LOADU( src );
    __vector4 a = __vmrghb( zero, v );                       /* src[0..7] as u16 */
    __vector4 h = __vslh( a, three );
    if( x )
    {
        __vector4 b = __vmrghb( zero, __vsldoi( v, v, 1 ) ); /* src[1..8] */
        h = __vadduhm( h, mul07( __vsubuhm( b, a ), x, one, two ) );
    }
    return h;
}

static __inline void chroma_mc( uint8_t* dst, uint8_t* src, int stride, int h, int x, int y, int width, int avg )
{
    const __vector4 zero = __vspltisb( 0 );
    const __vector4 one = __vspltish( 1 ), two = __vspltish( 2 ), three = __vspltish( 3 ), six = __vspltish( 6 );
    const __vector4 c32 = __vslh( one, __vspltish( 5 ) );
    __vector4 h0 = hpass( src, x, zero, one, two, three );
    int i;
    for( i = 0; i < h; ++i )
    {
        __vector4 v, out, packed;
        if( y )
        {
            __vector4 h1 = hpass( src + stride, x, zero, one, two, three );
            v = __vadduhm( __vslh( h0, three ), mul07( __vsubuhm( h1, h0 ), y, one, two ) );
            h0 = h1;
        }
        else
        {
            v = __vslh( h0, three );
            if( i + 1 < h )
                h0 = hpass( src + stride, x, zero, one, two, three );
        }
        out = __vsrah( __vadduhm( v, c32 ), six );
        packed = __vpkshus( out, out );
        if( avg )
            packed = __vavgub( packed, LOADU(dst) );
        vmx_ab_store(packed,dst,width);
        dst += stride;
        src += stride;
    }
}

static void put_h264_chroma_mc8_xb( uint8_t* dst, uint8_t* src, int stride, int h, int x, int y )
{
    chroma_mc( dst, src, stride, h, x, y, 8, 0 );
}
static void avg_h264_chroma_mc8_xb( uint8_t* dst, uint8_t* src, int stride, int h, int x, int y )
{
    chroma_mc( dst, src, stride, h, x, y, 8, 1 );
}
static void put_h264_chroma_mc4_xb( uint8_t* dst, uint8_t* src, int stride, int h, int x, int y )
{
    chroma_mc( dst, src, stride, h, x, y, 4, 0 );
}
static void avg_h264_chroma_mc4_xb( uint8_t* dst, uint8_t* src, int stride, int h, int x, int y )
{
    chroma_mc( dst, src, stride, h, x, y, 4, 1 );
}


void ff_multiplex_chroma_tables(struct vmx_ab_tables *tables) {
    tables->chroma[0][0]=put_h264_chroma_mc8_xb;tables->chroma[0][1]=avg_h264_chroma_mc8_xb;
    tables->chroma[1][0]=put_h264_chroma_mc4_xb;tables->chroma[1][1]=avg_h264_chroma_mc4_xb;
}
