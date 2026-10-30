/*
 * VMX versions of the H.264 decoder's motion compensation and IDCT for the
 * Xbox 360 (Xenon), for XBMC-360's FFmpeg 1.2:
 *   - luma quarter-pel (16x16 and 8x8, all 16 positions, put and avg)
 *   - chroma eighth-pel (8 and 4 wide, with a 0,0 full-pel fast path)
 *   - H.264 IDCT (4x4, 4x4 DC, 8x8, 8x8 DC, and macroblock batch loops)
 *
 * Xenon's VMX has no integer multiplies, so the filters use shifts and adds:
 *   6-tap:   (a+f) - 5(b+e) + 20(c+d),  5x = 4x + x,  20x = 16x + 4x
 *   bilinear: (8-x)a + xb = 8a + x(b-a), with x(b-a) summed from the bits of x
 * The 2-D centre position stays in 16-bit lanes via the exact identity
 *   floor((a - 5b + 20c) / 16) = ((((a - b) >> 2) - b + c) >> 2) + c
 * (as in FFmpeg's x86 code). Everything is bit-exact with the C versions;
 * ff_xbox_dsp_selftest() checks that on the console before anything is used.
 *
 * C89: the SDK's C compiler doesn't take declarations after statements.
 */
#include <xtl.h>
#include <string.h>

#include "libavcodec/dsputil.h"
#include "libavcodec/h264chroma.h"
#include "libavcodec/h264dsp.h"
#include "libavcodec/h264qpel.h"
#include "libavcodec/videodsp.h"
#include "xbox_dsp.h"

int ff_xbox_vmx_flags = 0;
unsigned ff_xbox_loopfilter_mask = XVMX_LF_ALL;

/* Set by the self-test: which functions matched C (bit per qpel position). */
static int tested;
static unsigned qpel_ok[2][2];     /* [size: 0 = 16, 1 = 8][0 = put, 1 = avg] */
static unsigned chroma_ok[2][2];   /* [0 = mc8, 1 = mc4][put, avg]: bit 0 */
static unsigned loopfilter_ok; /* bit per progressive entry, independent fallback */
static unsigned idct_batch_ok;
static unsigned idct_ok;           /* bit 0 = 4x4, 1 = dc4, 2 = 8x8, 3 = dc8 */
static int idct_clears_block = 1;  /* detected against FFmpeg's C IDCT at startup */

#define LOADU(p) __vor(__lvlx((p), 0), __lvrx((p), 16))

/* 16-bit halfword merge masks for __vperm (avoids extra intrinsic dependencies). */
static const __declspec(align(16)) uint8_t perm_mrghh[16] = {
    0, 1, 16, 17, 2, 3, 18, 19, 4, 5, 20, 21, 6, 7, 22, 23
};
static const __declspec(align(16)) uint8_t perm_mrglh[16] = {
    8, 9, 24, 25, 10, 11, 26, 27, 12, 13, 28, 29, 14, 15, 30, 31
};

static const uint8_t xbox_scan8[16 * 3 + 3] = {
    4 +  1 * 8, 5 +  1 * 8, 4 +  2 * 8, 5 +  2 * 8,
    6 +  1 * 8, 7 +  1 * 8, 6 +  2 * 8, 7 +  2 * 8,
    4 +  3 * 8, 5 +  3 * 8, 4 +  4 * 8, 5 +  4 * 8,
    6 +  3 * 8, 7 +  3 * 8, 6 +  4 * 8, 7 +  4 * 8,
    4 +  6 * 8, 5 +  6 * 8, 4 +  7 * 8, 5 +  7 * 8,
    6 +  6 * 8, 7 +  6 * 8, 6 +  7 * 8, 7 +  7 * 8,
    4 +  8 * 8, 5 +  8 * 8, 4 +  9 * 8, 5 +  9 * 8,
    6 +  8 * 8, 7 +  8 * 8, 6 +  9 * 8, 7 +  9 * 8,
    4 + 11 * 8, 5 + 11 * 8, 4 + 12 * 8, 5 + 12 * 8,
    6 + 11 * 8, 7 + 11 * 8, 6 + 12 * 8, 7 + 12 * 8,
    4 + 13 * 8, 5 + 13 * 8, 4 + 14 * 8, 5 + 14 * 8,
    6 + 13 * 8, 7 + 13 * 8, 6 + 14 * 8, 7 + 14 * 8,
    0 +  0 * 8, 0 +  5 * 8, 0 + 10 * 8
};

/* ------------------------------------------------------------ stores */

static __forceinline void store16(__vector4 v, uint8_t *dst)
{
    if (((UINT_PTR)dst & 15) == 0) {
        __stvx(v, dst, 0);
    } else {
        __stvlx(v, dst, 0);
        __stvrx(v, dst, 16);
    }
}

/* Stores lanes 0..n-1 (n = 4 or 8) at a 4-byte-aligned dst. */
static __forceinline void store_words(__vector4 v, uint8_t *dst, const int n)
{
    v = __vperm(v, v, __lvsr(dst, 0));   /* move lane 0 to dst's position */
    __stvewx(v, dst, 0);
    if (n == 8)
        __stvewx(v, dst, 4);
}

static __forceinline void store_n(__vector4 v, uint8_t *dst, const int n)
{
    if (n == 16) {
        store16(v, dst);
    } else if (((UINT_PTR)dst & 3) == 0) {
        store_words(v, dst, n);
    } else {
        __declspec(align(16)) uint8_t tmp[16];
        __stvx(v, tmp, 0);
        memcpy(dst, tmp, n);
    }
}

/* ------------------------------------------------------------ luma */

/* (a+f) - 5(b+e) + 20(c+d) on 8 signed 16-bit lanes. */
static __forceinline __vector4 tap6(__vector4 a, __vector4 b, __vector4 c, __vector4 d,
                                    __vector4 e, __vector4 f, __vector4 v2, __vector4 v4)
{
    __vector4 s20 = __vadduhm(c, d);
    __vector4 s5 = __vadduhm(b, e);
    __vector4 t20 = __vadduhm(__vslh(s20, v4), __vslh(s20, v2));
    __vector4 t5 = __vadduhm(__vslh(s5, v2), s5);
    return __vadduhm(__vsubuhm(t20, t5), __vadduhm(a, f));
}

/*
 * Horizontal 6-tap of one row, unrounded: *hi = pixels 0-7, *lo = 8-15
 * (n == 16 only). *f0 / *f1 get the full-pel pixels at offsets 0 and 1.
 */
static __forceinline void hfilt(const uint8_t *src, const int n, __vector4 *hi, __vector4 *lo,
                                __vector4 *f0, __vector4 *f1, __vector4 v2, __vector4 v4)
{
    __vector4 z = __vzero();
    __vector4 a = LOADU(src - 2);
    __vector4 b = n == 16 ? LOADU(src + 14) : a;
    __vector4 m1 = __vsldoi(a, b, 1);
    __vector4 p0 = __vsldoi(a, b, 2);
    __vector4 p1 = __vsldoi(a, b, 3);
    __vector4 p2 = __vsldoi(a, b, 4);
    __vector4 p3 = __vsldoi(a, b, 5);

    *hi = tap6(__vmrghb(z, a), __vmrghb(z, m1), __vmrghb(z, p0), __vmrghb(z, p1),
               __vmrghb(z, p2), __vmrghb(z, p3), v2, v4);
    if (n == 16)
        *lo = tap6(__vmrglb(z, a), __vmrglb(z, m1), __vmrglb(z, p0), __vmrglb(z, p1),
                   __vmrglb(z, p2), __vmrglb(z, p3), v2, v4);
    *f0 = p0;
    *f1 = p1;
}

/* Vertical 6-tap over six rows of bytes, rounded and packed. */
static __forceinline __vector4 vfilt(__vector4 r0, __vector4 r1, __vector4 r2, __vector4 r3,
                                     __vector4 r4, __vector4 r5, const int n,
                                     __vector4 v2, __vector4 v4, __vector4 v5, __vector4 c16)
{
    __vector4 z = __vzero();
    __vector4 hi = tap6(__vmrghb(z, r0), __vmrghb(z, r1), __vmrghb(z, r2), __vmrghb(z, r3),
                        __vmrghb(z, r4), __vmrghb(z, r5), v2, v4);
    __vector4 lo;

    hi = __vsrah(__vadduhm(hi, c16), v5);
    if (n != 16)
        return __vpkshus(hi, hi);
    lo = tap6(__vmrglb(z, r0), __vmrglb(z, r1), __vmrglb(z, r2), __vmrglb(z, r3),
              __vmrglb(z, r4), __vmrglb(z, r5), v2, v4);
    lo = __vsrah(__vadduhm(lo, c16), v5);
    return __vpkshus(hi, lo);
}

/* Second (vertical) pass of the centre position on unrounded 6-tap rows. */
static __forceinline __vector4 hv2(__vector4 tB, __vector4 tA, __vector4 t0, __vector4 t1,
                                   __vector4 t2, __vector4 t3, __vector4 v2, __vector4 v6, __vector4 c32)
{
    __vector4 a = __vadduhm(tB, t3);
    __vector4 b = __vadduhm(tA, t2);
    __vector4 c = __vadduhm(t0, t1);
    __vector4 x = __vsrah(__vsubuhm(a, b), v2);

    x = __vsubuhm(x, b);
    x = __vaddshs(x, c);   /* saturation only where the result clips anyway */
    x = __vsrah(x, v2);
    x = __vadduhm(x, c);   /* = floor((a - 5b + 20c) / 16) */
    return __vsrah(__vadduhm(x, c32), v6);
}

static __forceinline __vector4 round5(__vector4 x, __vector4 v5, __vector4 c16)
{
    return __vsrah(__vadduhm(x, c16), v5);
}

/*
 * One n x n block (n = 16 or 8) at quarter-pel position (mx, my); avg
 * averages the result into dst. With constant arguments the compiler keeps
 * only the paths a position needs.
 */
static __forceinline void qpel(uint8_t *dst, const uint8_t *src, int stride,
                               const int n, const int mx, const int my, const int avg)
{
    const int need_h = mx != 0 && my != 2;
    const int need_v = my != 0 && mx != 2;
    const int need_hv = (mx == 2 && my != 0) || (my == 2 && mx != 0);
    const int h_row = my == 3;          /* H filter on row y or y + 1 */
    const int v_col = mx == 3;          /* V filter on column 0 or 1 */
    __vector4 v2 = __vspltish(2), v4 = __vspltish(4), v5 = __vspltish(5), v6 = __vspltish(6);
    __vector4 c16 = __vslh(__vspltish(1), v4);
    __vector4 c32 = __vadduhm(c16, c16);
    __vector4 r0, r1, r2, r3, r4, r5;                 /* V window: rows y-2 .. y+3 */
    __vector4 t0h, t1h, t2h, t3h, t4h, t5h;           /* HV window, unrounded 6-tap */
    __vector4 t0l, t1l, t2l, t3l, t4l, t5l;
    __vector4 hh, hl, f0, f1, res, hv, h, v;
    const uint8_t *vs = src + v_col;
    int y;

    r0 = r1 = r2 = r3 = r4 = r5 = __vzero();
    t0h = t1h = t2h = t3h = t4h = t5h = __vzero();
    t0l = t1l = t2l = t3l = t4l = t5l = __vzero();
    hh = hl = f0 = f1 = __vzero();

    if (need_v) {
        r0 = LOADU(vs - 2 * stride);
        r1 = LOADU(vs - stride);
        r2 = LOADU(vs);
        r3 = LOADU(vs + stride);
        r4 = LOADU(vs + 2 * stride);
    }
    if (need_hv) {
        hfilt(src - 2 * stride, n, &t0h, &t0l, &f0, &f1, v2, v4);
        hfilt(src - stride, n, &t1h, &t1l, &f0, &f1, v2, v4);
        hfilt(src, n, &t2h, &t2l, &f0, &f1, v2, v4);
        hfilt(src + stride, n, &t3h, &t3l, &f0, &f1, v2, v4);
        hfilt(src + 2 * stride, n, &t4h, &t4l, &f0, &f1, v2, v4);
    }

    for (y = 0; y < n; y++) {
        if (need_v)
            r5 = LOADU(vs + 3 * stride);
        if (need_hv)
            hfilt(src + 3 * stride, n, &t5h, &t5l, &f0, &f1, v2, v4);

        if (mx == 0 && my == 0) {
            res = LOADU(src);
        } else {
            if (need_h) {
                if (need_hv) {
                    hh = h_row ? t3h : t2h;
                    hl = h_row ? t3l : t2l;
                } else {
                    hfilt(src + h_row * stride, n, &hh, &hl, &f0, &f1, v2, v4);
                }
                hh = round5(hh, v5, c16);
                if (n == 16) {
                    hl = round5(hl, v5, c16);
                    h = __vpkshus(hh, hl);
                } else {
                    h = __vpkshus(hh, hh);
                }
            } else {
                h = __vzero();
            }
            v = need_v ? vfilt(r0, r1, r2, r3, r4, r5, n, v2, v4, v5, c16) : __vzero();
            if (need_hv) {
                hv = hv2(t0h, t1h, t2h, t3h, t4h, t5h, v2, v6, c32);
                hv = __vpkshus(hv, n == 16 ? hv2(t0l, t1l, t2l, t3l, t4l, t5l, v2, v6, c32) : hv);
            } else {
                hv = __vzero();
            }

            if (my == 0)
                res = mx == 2 ? h : __vavgub(h, mx == 3 ? f1 : f0);
            else if (mx == 0)
                res = my == 2 ? v : __vavgub(v, my == 3 ? r3 : r2);
            else if (need_hv)
                res = need_h ? __vavgub(hv, h) : need_v ? __vavgub(hv, v) : hv;
            else
                res = __vavgub(h, v);
        }

        if (avg)
            res = __vavgub(res, LOADU(dst));
        store_n(res, dst, n);

        r0 = r1; r1 = r2; r2 = r3; r3 = r4; r4 = r5;
        t0h = t1h; t1h = t2h; t2h = t3h; t3h = t4h; t4h = t5h;
        t0l = t1l; t1l = t2l; t2l = t3l; t3l = t4l; t4l = t5l;
        src += stride;
        vs += stride;
        dst += stride;
    }
}

#define QPEL_FN(OP, AVG, N, X, Y) \
static void OP ## _qpel ## N ## _mc ## X ## Y ## _vmx(uint8_t *dst, uint8_t *src, int stride) \
{ \
    qpel(dst, src, stride, N, X, Y, AVG); \
}

#define QPEL_ROW(OP, AVG, N, Y) \
    QPEL_FN(OP, AVG, N, 0, Y) QPEL_FN(OP, AVG, N, 1, Y) QPEL_FN(OP, AVG, N, 2, Y) QPEL_FN(OP, AVG, N, 3, Y)

#define QPEL_ALL(OP, AVG, N) \
    QPEL_ROW(OP, AVG, N, 0) QPEL_ROW(OP, AVG, N, 1) QPEL_ROW(OP, AVG, N, 2) QPEL_ROW(OP, AVG, N, 3)

QPEL_ALL(put, 0, 16)
QPEL_ALL(avg, 1, 16)
QPEL_ALL(put, 0, 8)
QPEL_ALL(avg, 1, 8)

/* FFmpeg's table order: index = x + 4 * y. */
#define QPEL_TAB(OP, N) { \
    OP ## _qpel ## N ## _mc00_vmx, OP ## _qpel ## N ## _mc10_vmx, OP ## _qpel ## N ## _mc20_vmx, OP ## _qpel ## N ## _mc30_vmx, \
    OP ## _qpel ## N ## _mc01_vmx, OP ## _qpel ## N ## _mc11_vmx, OP ## _qpel ## N ## _mc21_vmx, OP ## _qpel ## N ## _mc31_vmx, \
    OP ## _qpel ## N ## _mc02_vmx, OP ## _qpel ## N ## _mc12_vmx, OP ## _qpel ## N ## _mc22_vmx, OP ## _qpel ## N ## _mc32_vmx, \
    OP ## _qpel ## N ## _mc03_vmx, OP ## _qpel ## N ## _mc13_vmx, OP ## _qpel ## N ## _mc23_vmx, OP ## _qpel ## N ## _mc33_vmx }

static const qpel_mc_func vmx_qpel[2][2][16] = {
    { QPEL_TAB(put, 16), QPEL_TAB(avg, 16) },
    { QPEL_TAB(put, 8), QPEL_TAB(avg, 8) },
};

/* ------------------------------------------------------------ chroma */

/* m * d for m in 0..7, given masks of m's bits (all ones or zero). */
static __forceinline __vector4 mul_small(__vector4 d, __vector4 b1, __vector4 b2, __vector4 b4,
                                         __vector4 s1, __vector4 s2)
{
    return __vadduhm(__vadduhm(__vand(d, b1), __vand(__vslh(d, s1), b2)), __vand(__vslh(d, s2), b4));
}

/* (8-x)a + xb for one row of n (<= 8) pixels, as 16-bit lanes. */
static __forceinline __vector4 chroma_row(const uint8_t *src, __vector4 xb1, __vector4 xb2, __vector4 xb4,
                                          __vector4 s1, __vector4 s2, __vector4 s3)
{
    __vector4 z = __vzero();
    __vector4 p = LOADU(src);
    __vector4 a = __vmrghb(z, p);
    __vector4 b = __vmrghb(z, __vsldoi(p, p, 1));
    return __vadduhm(__vslh(a, s3), mul_small(__vsubuhm(b, a), xb1, xb2, xb4, s1, s2));
}

static __forceinline void chroma_mc(uint8_t *dst, const uint8_t *src, int stride, int h, int x, int y,
                                    const int n, const int avg)
{
    __vector4 ones, z, s1, s2, s3, s6, c32;
    __vector4 xb1, xb2, xb4, yb1, yb2, yb4;
    __vector4 h0, h1, r;
    int i;

    /* Fast path for full-pel chroma motion vectors (x == 0, y == 0). */
    if (x == 0 && y == 0) {
        for (i = 0; i < h; i++) {
            r = LOADU(src);
            if (avg)
                r = __vavgub(r, LOADU(dst));
            store_n(r, dst, n);
            src += stride;
            dst += stride;
        }
        return;
    }

    ones = __vspltish(-1);
    z = __vzero();
    s1 = __vspltish(1);
    s2 = __vspltish(2);
    s3 = __vspltish(3);
    s6 = __vspltish(6);
    c32 = __vslh(s1, __vspltish(5));
    xb1 = (x & 1) ? ones : z;
    xb2 = (x & 2) ? ones : z;
    xb4 = (x & 4) ? ones : z;
    yb1 = (y & 1) ? ones : z;
    yb2 = (y & 2) ? ones : z;
    yb4 = (y & 4) ? ones : z;
    h0 = chroma_row(src, xb1, xb2, xb4, s1, s2, s3);

    for (i = 0; i < h; i++) {
        src += stride;
        h1 = chroma_row(src, xb1, xb2, xb4, s1, s2, s3);
        /* 8 h0 + y (h1 - h0), then (+32) >> 6 */
        r = __vadduhm(__vslh(h0, s3), mul_small(__vsubuhm(h1, h0), yb1, yb2, yb4, s1, s2));
        r = __vsrah(__vadduhm(r, c32), s6);
        r = __vpkshus(r, r);
        if (avg)
            r = __vavgub(r, LOADU(dst));
        store_n(r, dst, n);
        h0 = h1;
        dst += stride;
    }
}

static void put_chroma_mc8_vmx(uint8_t *dst, uint8_t *src, int stride, int h, int x, int y)
{
    chroma_mc(dst, src, stride, h, x, y, 8, 0);
}

static void avg_chroma_mc8_vmx(uint8_t *dst, uint8_t *src, int stride, int h, int x, int y)
{
    chroma_mc(dst, src, stride, h, x, y, 8, 1);
}

static void put_chroma_mc4_vmx(uint8_t *dst, uint8_t *src, int stride, int h, int x, int y)
{
    chroma_mc(dst, src, stride, h, x, y, 4, 0);
}

static void avg_chroma_mc4_vmx(uint8_t *dst, uint8_t *src, int stride, int h, int x, int y)
{
    chroma_mc(dst, src, stride, h, x, y, 4, 1);
}

static const h264_chroma_mc_func vmx_chroma[2][2] = {
    { put_chroma_mc8_vmx, avg_chroma_mc8_vmx },
    { put_chroma_mc4_vmx, avg_chroma_mc4_vmx },
};

/* ------------------------------------------------------------ H.264 IDCT */

static __forceinline void clear_block16(int16_t *block)
{
    __vector4 z = __vzero();
    store16(z, (uint8_t *)block);
    store16(z, (uint8_t *)(block + 8));
}

static __forceinline void clear_block64(int16_t *block)
{
    __vector4 z = __vzero();
    int i;
    for (i = 0; i < 8; i++)
        store16(z, (uint8_t *)(block + i * 8));
}

/*
 * 4x4 DC-only IDCT: splats ((block[0] + 32) >> 6) across 8 halfword lanes in
 * vector registers without a scalar-to-vector Load-Hit-Store stall.
 */
static void idct_dc_add_vmx(uint8_t *dst, int16_t *block, int stride)
{
    __vector4 z = __vzero();
    __vector4 s1 = __vspltish(1);
    __vector4 b0 = LOADU((const uint8_t *)block);
    /* __vspltish(1) has bytes 0x00, 0x01 in every halfword: splats lane 0. */
    __vector4 dc16 = __vperm(b0, b0, s1);
    __vector4 sign = __vsrah(dc16, __vspltish(15));
    __vector4 dc32 = __vperm(sign, dc16, LOADU(perm_mrghh));
    __vector4 c32 = __vslw(__vspltisw(1), __vspltisw(5));
    __vector4 shifted = __vsraw(__vadduwm(dc32, c32), __vspltisw(6));
    __vector4 vdc = __vpkswss(shifted, shifted);
    int i;

    if (idct_clears_block)
        block[0] = 0;
    for (i = 0; i < 4; i++) {
        __vector4 d = __vmrghb(z, LOADU(dst));
        __vector4 o = __vpkshus(__vaddshs(d, vdc), z);
        store_n(o, dst, 4);
        dst += stride;
    }
}

/* 8x8 DC-only IDCT. */
static void idct8_dc_add_vmx(uint8_t *dst, int16_t *block, int stride)
{
    __vector4 z = __vzero();
    __vector4 s1 = __vspltish(1);
    __vector4 b0 = LOADU((const uint8_t *)block);
    __vector4 dc16 = __vperm(b0, b0, s1);
    __vector4 sign = __vsrah(dc16, __vspltish(15));
    __vector4 dc32 = __vperm(sign, dc16, LOADU(perm_mrghh));
    __vector4 c32 = __vslw(__vspltisw(1), __vspltisw(5));
    __vector4 shifted = __vsraw(__vadduwm(dc32, c32), __vspltisw(6));
    __vector4 vdc = __vpkswss(shifted, shifted);
    int i;

    if (idct_clears_block)
        block[0] = 0;
    for (i = 0; i < 8; i++) {
        __vector4 d = __vmrghb(z, LOADU(dst));
        __vector4 o = __vpkshus(__vaddshs(d, vdc), z);
        store_n(o, dst, 8);
        dst += stride;
    }
}

/* Full-width intermediate arithmetic, matching FFmpeg's int expressions.
 * The first pass explicitly truncates to int16_t, as the C coefficient buffer does.
 * The final pass stays at 32 bits until rounding and pixel saturation. */
static __forceinline __vector4 coeff_hi(__vector4 v)
{
    return __vperm(__vsrah(v, __vspltish(15)), v, LOADU(perm_mrghh));
}

static __forceinline __vector4 coeff_lo(__vector4 v)
{
    return __vperm(__vsrah(v, __vspltish(15)), v, LOADU(perm_mrglh));
}

static __forceinline __vector4 truncate16(__vector4 v)
{
    return __vsraw(__vslw(v, __vspltisw(-16)), __vspltisw(-16));
}

static __forceinline void idct4_1d32(__vector4 r[4])
{
    __vector4 z0 = __vadduwm(r[0], r[2]);
    __vector4 z1 = __vsubuwm(r[0], r[2]);
    __vector4 z2 = __vsubuwm(__vsraw(r[1], __vspltisw(1)), r[3]);
    __vector4 z3 = __vadduwm(r[1], __vsraw(r[3], __vspltisw(1)));
    r[0] = __vadduwm(z0, z3);
    r[1] = __vadduwm(z1, z2);
    r[2] = __vsubuwm(z1, z2);
    r[3] = __vsubuwm(z0, z3);
}

static void idct_add_vmx(uint8_t *dst, int16_t *block, int stride)
{
    static const __declspec(align(16)) uint8_t hi[16] = {
        0,1,2,3,16,17,18,19,4,5,6,7,20,21,22,23 };
    static const __declspec(align(16)) uint8_t lo[16] = {
        8,9,10,11,24,25,26,27,12,13,14,15,28,29,30,31 };
    __vector4 z = __vzero(), r[4], a, b, c, d;
    __vector4 c32_0 = __vsldoi(__vslh(__vspltish(1), __vspltish(5)), z, 14);
    int i;
    r[0] = coeff_hi(__vadduhm(LOADU(block), c32_0));
    r[1] = coeff_hi(LOADU(block + 4));
    r[2] = coeff_hi(LOADU(block + 8));
    r[3] = coeff_hi(LOADU(block + 12));
    if (idct_clears_block) clear_block16(block);
    idct4_1d32(r);
    for (i = 0; i < 4; i++) r[i] = truncate16(r[i]);
    a = __vperm(r[0], r[2], LOADU(hi));
    b = __vperm(r[1], r[3], LOADU(hi));
    c = __vperm(r[0], r[2], LOADU(lo));
    d = __vperm(r[1], r[3], LOADU(lo));
    r[0] = __vperm(a, b, LOADU(hi)); r[1] = __vperm(a, b, LOADU(lo));
    r[2] = __vperm(c, d, LOADU(hi)); r[3] = __vperm(c, d, LOADU(lo));
    idct4_1d32(r);
    for (i = 0; i < 4; i++) {
        __vector4 dc = __vsraw(r[i], __vspltisw(6));
        __vector4 pixels = __vmrghb(z, LOADU(dst + i * stride));
        __vector4 residual = __vpkswss(dc, dc);
        store_n(__vpkshus(__vaddshs(pixels, residual), z), dst + i * stride, 4);
    }
}

static __forceinline void idct8_1d32(__vector4 r[8])
{
    __vector4 s1 = __vspltisw(1), s2 = __vspltisw(2);
    __vector4 a0 = __vadduwm(r[0], r[4]);
    __vector4 a2 = __vsubuwm(r[0], r[4]);
    __vector4 a4 = __vsubuwm(__vsraw(r[2], s1), r[6]);
    __vector4 a6 = __vadduwm(__vsraw(r[6], s1), r[2]);
    __vector4 b0 = __vadduwm(a0, a6), b2 = __vadduwm(a2, a4);
    __vector4 b4 = __vsubuwm(a2, a4), b6 = __vsubuwm(a0, a6);
    __vector4 r1_3 = __vadduwm(r[1], __vsraw(r[1], s1));
    __vector4 r3_3 = __vadduwm(r[3], __vsraw(r[3], s1));
    __vector4 r5_3 = __vadduwm(r[5], __vsraw(r[5], s1));
    __vector4 r7_3 = __vadduwm(r[7], __vsraw(r[7], s1));
    __vector4 a1 = __vsubuwm(__vsubuwm(r[5], r[3]), r7_3);
    __vector4 a3 = __vsubuwm(__vadduwm(r[1], r[7]), r3_3);
    __vector4 a5 = __vadduwm(__vsubuwm(r[7], r[1]), r5_3);
    __vector4 a7 = __vadduwm(__vadduwm(r[3], r[5]), r1_3);
    __vector4 b1 = __vadduwm(__vsraw(a7, s2), a1);
    __vector4 b3 = __vadduwm(a3, __vsraw(a5, s2));
    __vector4 b5 = __vsubuwm(__vsraw(a3, s2), a5);
    __vector4 b7 = __vsubuwm(a7, __vsraw(a1, s2));
    r[0] = __vadduwm(b0, b7); r[1] = __vadduwm(b2, b5);
    r[2] = __vadduwm(b4, b3); r[3] = __vadduwm(b6, b1);
    r[4] = __vsubuwm(b6, b1); r[5] = __vsubuwm(b4, b3);
    r[6] = __vsubuwm(b2, b5); r[7] = __vsubuwm(b0, b7);
}

static void idct8_add_vmx(uint8_t *dst, int16_t *block, int stride)
{
    __vector4 z = __vzero(), rh[8], rl[8], r[8];
    __vector4 a0,a1,a2,a3,a4,a5,a6,a7,b0,b1,b2,b3,b4,b5,b6,b7;
    __vector4 mh = LOADU(perm_mrghh), ml = LOADU(perm_mrglh);
    __vector4 c32_0 = __vsldoi(__vslh(__vspltish(1), __vspltish(5)), z, 14);
    int i;
    for (i = 0; i < 8; i++) {
        __vector4 v = LOADU(block + i * 8);
        if (i == 0) v = __vadduhm(v, c32_0);
        rh[i] = coeff_hi(v); rl[i] = coeff_lo(v);
    }
    if (idct_clears_block) clear_block64(block);
    idct8_1d32(rh); idct8_1d32(rl);
    for (i = 0; i < 8; i++) r[i] = __vpkuwum(rh[i], rl[i]);
    a0=__vperm(r[0],r[4],mh); a1=__vperm(r[0],r[4],ml);
    a2=__vperm(r[1],r[5],mh); a3=__vperm(r[1],r[5],ml);
    a4=__vperm(r[2],r[6],mh); a5=__vperm(r[2],r[6],ml);
    a6=__vperm(r[3],r[7],mh); a7=__vperm(r[3],r[7],ml);
    b0=__vperm(a0,a4,mh); b1=__vperm(a0,a4,ml);
    b2=__vperm(a1,a5,mh); b3=__vperm(a1,a5,ml);
    b4=__vperm(a2,a6,mh); b5=__vperm(a2,a6,ml);
    b6=__vperm(a3,a7,mh); b7=__vperm(a3,a7,ml);
    r[0]=__vperm(b0,b4,mh); r[1]=__vperm(b0,b4,ml);
    r[2]=__vperm(b1,b5,mh); r[3]=__vperm(b1,b5,ml);
    r[4]=__vperm(b2,b6,mh); r[5]=__vperm(b2,b6,ml);
    r[6]=__vperm(b3,b7,mh); r[7]=__vperm(b3,b7,ml);
    for (i = 0; i < 8; i++) { rh[i]=coeff_hi(r[i]); rl[i]=coeff_lo(r[i]); }
    idct8_1d32(rh); idct8_1d32(rl);
    for (i = 0; i < 8; i++) {
        __vector4 dc=__vpkswss(__vsraw(rh[i],__vspltisw(6)),__vsraw(rl[i],__vspltisw(6)));
        __vector4 pixels=__vmrghb(z,LOADU(dst+i*stride));
        store_n(__vpkshus(__vaddshs(pixels,dc),z),dst+i*stride,8);
    }
}

/* Macroblock batch wrappers so inter/intra 16x16 blocks dispatch to VMX IDCT. */
static void idct_add16_vmx(uint8_t *dst, const int *block_offset, int16_t *block,
                           int stride, const uint8_t nnzc[15 * 8])
{
    int i;
    for (i = 0; i < 16; i++) {
        int nnz = nnzc[xbox_scan8[i]];
        if (nnz) {
            if (nnz == 1 && block[i * 16])
                idct_dc_add_vmx(dst + block_offset[i], block + i * 16, stride);
            else
                idct_add_vmx(dst + block_offset[i], block + i * 16, stride);
        }
    }
}

static void idct_add16intra_vmx(uint8_t *dst, const int *block_offset, int16_t *block,
                                int stride, const uint8_t nnzc[15 * 8])
{
    int i;
    for (i = 0; i < 16; i++) {
        if (nnzc[xbox_scan8[i]])
            idct_add_vmx(dst + block_offset[i], block + i * 16, stride);
        else if (block[i * 16])
            idct_dc_add_vmx(dst + block_offset[i], block + i * 16, stride);
    }
}

static void idct8_add4_vmx(uint8_t *dst, const int *block_offset, int16_t *block,
                           int stride, const uint8_t nnzc[15 * 8])
{
    int i;
    for (i = 0; i < 16; i += 4) {
        int nnz = nnzc[xbox_scan8[i]];
        if (nnz) {
            if (nnz == 1 && block[i * 16])
                idct8_dc_add_vmx(dst + block_offset[i], block + i * 16, stride);
            else
                idct8_add_vmx(dst + block_offset[i], block + i * 16, stride);
        }
    }
}

static void idct_add8_vmx(uint8_t **dest, const int *block_offset, int16_t *block,
                          int stride, const uint8_t nnzc[15 * 8])
{
    int i, j;
    for (j = 1; j < 3; j++) {
        for (i = j * 16; i < j * 16 + 4; i++) {
            if (nnzc[xbox_scan8[i]])
                idct_add_vmx(dest[j - 1] + block_offset[i], block + i * 16, stride);
            else if (block[i * 16])
                idct_dc_add_vmx(dest[j - 1] + block_offset[i], block + i * 16, stride);
        }
    }
}

/* ------------------------------------------------------------ loop filter */
/* Exact 8-bit h264dsp_template.c arithmetic, eight signed halfword lanes.
 * All sums/differences fit int16 (largest weighted sum is 2044). Signed
 * comparisons use the sign of a-b, whose range here cannot overflow.
 * Only existing Xenon intrinsics are used; no float approximations.
 *
 * Fetch/store exactly eight bytes along an edge. Horizontal entrypoints
 * gather/scatter across stride; vertical ones copy contiguous eight bytes.
 * In particular chroma never does a 16-byte load from an 8-byte row.
 * No reads beyond p3..q3 (intra luma), p2..q2 (inter luma), p1..q1
 * (chroma), and no writes beyond the normative output footprint. */
#define LF_ADD(a,b) __vadduhm((a),(b))
#define LF_SUB(a,b) __vsubuhm((a),(b))
#define LF_SHR(a,n) __vsrah((a),__vspltish(n))
#define LF_TWO(a) LF_ADD((a),(a))

static __forceinline __vector4 lf_splat(int n)
{
    __declspec(align(16)) uint8_t b[16] = {0};
    b[0] = (uint8_t)(n >> 8); b[1] = (uint8_t)n;
    return __vsplth(LOADU(b), 0);
}
static __forceinline __vector4 lf_lt(__vector4 a, __vector4 b)
{
    return LF_SHR(LF_SUB(a,b),15);
}
static __forceinline __vector4 lf_absdiff(__vector4 a, __vector4 b)
{
    return __vsel(LF_SUB(a,b), LF_SUB(b,a), lf_lt(a,b));
}
static __forceinline int lf_any(__vector4 mask)
{
    unsigned cr;
    __vcmpequwR(mask, __vzero(), &cr);
    /* XDK xnamath.h: CR6TRUE (0x80) means all four word compares true. */
    return (cr & 0x80u) == 0;
}
static __forceinline __vector4 lf_clip(__vector4 a, __vector4 limit)
{
    __vector4 lo = LF_SUB(__vzero(),limit);
    a = __vsel(a,lo,lf_lt(a,lo));
    return __vsel(a,limit,lf_lt(limit,a));
}
static __forceinline __vector4 lf_load(const uint8_t *p, int step)
{
    __declspec(align(16)) uint8_t b[16] = {0};
    int i;
    if (step == 1) memcpy(b,p,8);
    else for (i=0;i<8;i++) b[i]=p[i*step];
    return __vmrghb(__vzero(),LOADU(b));
}
static __forceinline void lf_store(__vector4 v, uint8_t *p, int step)
{
    __declspec(align(16)) uint8_t b[16];
    int i;
    v = __vpkshus(v,__vzero());
    if (step == 1) store_n(v,p,8);
    else {
        store16(v,b);
        for (i=0;i<8;i++) p[i*step]=b[i];
    }
}
static __forceinline void lf_edge(uint8_t *pix, int xs, int ys,
                                  int alpha, int beta, int8_t *tc0,
                                  int luma, int intra)
{
    __vector4 a=lf_splat(alpha), b=lf_splat(beta), one=__vspltish(1);
    __vector4 two=__vspltish(2), four=__vspltish(4), z=__vzero();
    int offset, count=luma?16:8;
    if (!alpha || !beta) return;
    for (offset=0;offset<count;offset+=8) {
        uint8_t *p=pix+offset*ys;
        __vector4 p1, p0, q0, q1, mask, np0, nq0;
        /* Match C's tc0 skip before fetching/scattering an inactive group.
         * Luma tc0==0 remains active: ap/aq may still increase its limit. */
        if (!intra) {
            if (luma) {
                int g=offset/4;
                if (tc0[g]<0 && tc0[g+1]<0) continue;
            } else if (tc0[0]<=0 && tc0[1]<=0 && tc0[2]<=0 && tc0[3]<=0) {
                continue;
            }
        }
        p1=lf_load(p-2*xs,ys); p0=lf_load(p-xs,ys);
        q0=lf_load(p,ys); q1=lf_load(p+xs,ys);
        mask=__vand(lf_lt(lf_absdiff(p0,q0),a),
                   __vand(lf_lt(lf_absdiff(p1,p0),b),lf_lt(lf_absdiff(q1,q0),b)));
        /* One branch for eight pixels, not per pixel. Native measurements
         * showed unconditional residual math/stores losing to C's early exit. */
        if (!lf_any(mask)) continue;
        if (intra) {
            np0=LF_SHR(LF_ADD(LF_ADD(LF_TWO(p1),p0),LF_ADD(q1,two)),2);
            nq0=LF_SHR(LF_ADD(LF_ADD(LF_TWO(q1),q0),LF_ADD(p1,two)),2);
            if (luma) {
                __vector4 strong=__vand(mask,lf_lt(lf_absdiff(p0,q0),lf_splat((alpha>>2)+2)));
                if (lf_any(strong)) {
                    __vector4 p2=lf_load(p-3*xs,ys), q2=lf_load(p+2*xs,ys);
                    __vector4 p3=lf_load(p-4*xs,ys), q3=lf_load(p+3*xs,ys);
                    __vector4 pm=__vand(strong,lf_lt(lf_absdiff(p2,p0),b));
                    __vector4 qm=__vand(strong,lf_lt(lf_absdiff(q2,q0),b));
                    __vector4 pair=LF_ADD(p0,q0);
                    __vector4 pp0=LF_SHR(LF_ADD(LF_ADD(p2,LF_TWO(LF_ADD(p1,pair))),LF_ADD(q1,four)),3);
                    __vector4 qq0=LF_SHR(LF_ADD(LF_ADD(q2,LF_TWO(LF_ADD(q1,pair))),LF_ADD(p1,four)),3);
                    __vector4 pp1=LF_SHR(LF_ADD(LF_ADD(p2,p1),LF_ADD(pair,two)),2);
                    __vector4 qq1=LF_SHR(LF_ADD(LF_ADD(q2,q1),LF_ADD(pair,two)),2);
                    __vector4 pp2=LF_SHR(LF_ADD(LF_ADD(LF_TWO(p3),LF_ADD(LF_TWO(p2),p2)),LF_ADD(LF_ADD(p1,pair),four)),3);
                    __vector4 qq2=LF_SHR(LF_ADD(LF_ADD(LF_TWO(q3),LF_ADD(LF_TWO(q2),q2)),LF_ADD(LF_ADD(q1,pair),four)),3);
                    np0=__vsel(np0,pp0,pm); nq0=__vsel(nq0,qq0,qm);
                    lf_store(__vsel(p1,pp1,pm),p-2*xs,ys);
                    lf_store(__vsel(q1,qq1,qm),p+xs,ys);
                    lf_store(__vsel(p2,pp2,pm),p-3*xs,ys);
                    lf_store(__vsel(q2,qq2,qm),p+2*xs,ys);
                }
            }
        } else {
            __declspec(align(16)) uint8_t limits[16];
            __vector4 tc, limit, delta;
            int i;
            for (i=0;i<8;i++) {
                int t=tc0[(offset+i)/(luma?4:2)];
                limits[2*i]=(uint8_t)(t>>8); limits[2*i+1]=(uint8_t)t;
            }
            tc=LOADU(limits);
            mask=__vand(mask,lf_lt(luma?__vspltish(-1):z,tc));
            if (!lf_any(mask)) continue;
            /* Negative tc lanes are disabled; keep all arithmetic well bounded. */
            tc=__vsel(z,tc,mask); limit=tc;
            if (luma) {
                __vector4 p2=lf_load(p-3*xs,ys), q2=lf_load(p+2*xs,ys);
                __vector4 pm=__vand(mask,lf_lt(lf_absdiff(p2,p0),b));
                __vector4 qm=__vand(mask,lf_lt(lf_absdiff(q2,q0),b));
                __vector4 avg=LF_SHR(LF_ADD(LF_ADD(p0,q0),one),1);
                __vector4 dp=lf_clip(LF_SUB(LF_SHR(LF_ADD(p2,avg),1),p1),tc);
                __vector4 dq=lf_clip(LF_SUB(LF_SHR(LF_ADD(q2,avg),1),q1),tc);
                limit=LF_ADD(tc,LF_ADD(__vand(pm,one),__vand(qm,one)));
                lf_store(__vsel(p1,LF_ADD(p1,dp),pm),p-2*xs,ys);
                lf_store(__vsel(q1,LF_ADD(q1,dq),qm),p+xs,ys);
            }
            delta=lf_clip(LF_SHR(LF_ADD(LF_ADD(LF_TWO(LF_TWO(LF_SUB(q0,p0))),LF_SUB(p1,q1)),four),3),limit);
            np0=LF_ADD(p0,delta); nq0=LF_SUB(q0,delta);
        }
        lf_store(__vsel(p0,np0,mask),p-xs,ys);
        lf_store(__vsel(q0,nq0,mask),p,ys);
    }
}

#define LF_WRAPPERS(plane,luma) \
static void lf_v_##plane(uint8_t *p,int s,int a,int b,int8_t *tc) { lf_edge(p,s,1,a,b,tc,luma,0); } \
static void lf_h_##plane(uint8_t *p,int s,int a,int b,int8_t *tc) { lf_edge(p,1,s,a,b,tc,luma,0); } \
static void lf_vi_##plane(uint8_t *p,int s,int a,int b) { lf_edge(p,s,1,a,b,0,luma,1); } \
static void lf_hi_##plane(uint8_t *p,int s,int a,int b) { lf_edge(p,1,s,a,b,0,luma,1); }
LF_WRAPPERS(luma,1)
LF_WRAPPERS(chroma,0)
#undef LF_WRAPPERS
#undef LF_ADD
#undef LF_SUB
#undef LF_SHR
#undef LF_TWO

/* ------------------------------------------------------------ init hooks */

void ff_h264qpel_init_xbox(H264QpelContext *c, int bit_depth)
{
    int s, p;

    if (!tested || bit_depth != 8 || !(ff_xbox_vmx_flags & XVMX_QPEL))
        return;
    for (s = 0; s < 2; s++)
        for (p = 0; p < 16; p++) {
            if (qpel_ok[s][0] & (1u << p))
                c->put_h264_qpel_pixels_tab[s][p] = vmx_qpel[s][0][p];
            if (qpel_ok[s][1] & (1u << p))
                c->avg_h264_qpel_pixels_tab[s][p] = vmx_qpel[s][1][p];
        }
}

void ff_h264chroma_init_xbox(H264ChromaContext *c, int bit_depth)
{
    int s;

    if (!tested || bit_depth != 8 || !(ff_xbox_vmx_flags & XVMX_CHROMA))
        return;
    for (s = 0; s < 2; s++) {
        if (chroma_ok[s][0])
            c->put_h264_chroma_pixels_tab[s] = vmx_chroma[s][0];
        if (chroma_ok[s][1])
            c->avg_h264_chroma_pixels_tab[s] = vmx_chroma[s][1];
    }
}

void ff_h264dsp_init_xbox(H264DSPContext *c, const int bit_depth, const int chroma_format_idc)
{
    unsigned enabled_lf = loopfilter_ok & ff_xbox_loopfilter_mask;
    if (!tested || bit_depth != 8) return;
    if (ff_xbox_vmx_flags & XVMX_LOOPFILTER) {
        if (enabled_lf & 1) c->h264_v_loop_filter_luma = lf_v_luma;
        if (enabled_lf & 2) c->h264_h_loop_filter_luma = lf_h_luma;
        if (enabled_lf & 4) c->h264_v_loop_filter_luma_intra = lf_vi_luma;
        if (enabled_lf & 8) c->h264_h_loop_filter_luma_intra = lf_hi_luma;
        /* Horizontal chroma has different geometry for 4:2:2. */
        if (chroma_format_idc == 1) {
            if (enabled_lf & 16) c->h264_v_loop_filter_chroma = lf_v_chroma;
            if (enabled_lf & 32) c->h264_h_loop_filter_chroma = lf_h_chroma;
            if (enabled_lf & 64) c->h264_v_loop_filter_chroma_intra = lf_vi_chroma;
            if (enabled_lf & 128) c->h264_h_loop_filter_chroma_intra = lf_hi_chroma;
        }
    }
    if (!(ff_xbox_vmx_flags & XVMX_IDCT)) return;
    if (idct_ok & 1)
        c->h264_idct_add = idct_add_vmx;
    if (idct_ok & 2)
        c->h264_idct_dc_add = idct_dc_add_vmx;
    if (idct_ok & 4)
        c->h264_idct8_add = idct8_add_vmx;
    if (idct_ok & 8)
        c->h264_idct8_dc_add = idct8_dc_add_vmx;
    if ((idct_ok & 3) == 3) {
        if (idct_batch_ok & 1) c->h264_idct_add16 = idct_add16_vmx;
        if (idct_batch_ok & 2) c->h264_idct_add16intra = idct_add16intra_vmx;
        if (chroma_format_idc <= 1 && (idct_batch_ok & 4))
            c->h264_idct_add8 = idct_add8_vmx;
    }
    if ((idct_ok & 12) == 12 && (idct_batch_ok & 8))
        c->h264_idct8_add4 = idct8_add4_vmx;
}

void ff_videodsp_init_xbox(VideoDSPContext *ctx, int bpc)
{
    /* XBMC-360's dcbt prefetch: only a cache hint, it can't change results. */
    if (ff_xbox_vmx_flags & XVMX_PREFETCH)
        ff_videodsp_init_ppc(ctx, bpc);
}

/* ------------------------------------------------------------ self-test */

#define T_STRIDE 64
#define T_ROWS   48

static unsigned rng_state = 0x12345678;

static unsigned rng(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 8;
}

static void fill(uint8_t *p, int len)
{
    int i;
    unsigned mode = rng() % 4;

    for (i = 0; i < len; i++) {
        /* Mostly random, sometimes extremes, which exercise clipping. */
        if (mode == 0)
            p[i] = (uint8_t)((rng() & 1) ? 255 : 0);
        else
            p[i] = (uint8_t)rng();
    }
}

static void fill_coeffs(int16_t *b, int count)
{
    int i, mode = rng() % 3;
    for (i = 0; i < count; i++) {
        unsigned value = rng();
        b[i] = mode == 0 ? (int16_t)((int)(value & 1023) - 512) :
               mode == 1 ? (int16_t)((int)(value & 4095) - 2048) : (int16_t)value;
    }
}

static void report(void (*log)(const char *), const char *what, int n, int pos,
                   const uint8_t *ref, const uint8_t *got, int x, int y)
{
    char line[160];

    if (log == NULL)
        return;
    _snprintf(line, sizeof(line), "vmx selftest: %s%d pos %d MISMATCH at (%d,%d): C %d, VMX %d\n",
              what, n, pos, x, y, ref[y * T_STRIDE + x], got[y * T_STRIDE + x]);
    line[sizeof(line) - 1] = '\0';
    log(line);
}

/* Compares an n x n (w x h) region; returns 1 if equal. */
static int same(const uint8_t *a, const uint8_t *b, int w, int h, int *fx, int *fy)
{
    int x, y;

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            if (a[y * T_STRIDE + x] != b[y * T_STRIDE + x]) {
                *fx = x;
                *fy = y;
                return 0;
            }
    return 1;
}

/* Compare coefficient side effects as well as pixels before enabling a kernel. */
static int same_idct(const uint8_t *ref, const uint8_t *got, int rows,
                     const int16_t *br, const int16_t *bv, int count, int *fx, int *fy)
{
    if (!same(ref, got, T_STRIDE, rows, fx, fy))
        return 0;
    *fx = *fy = 0;
    return memcmp(br, bv, count * sizeof(int16_t)) == 0;
}

static int test_idct_batches(H264DSPContext *dc, void (*log)(const char *))
{
    static __declspec(align(16)) uint8_t ref[T_STRIDE * T_ROWS], got[T_STRIDE * T_ROWS];
    static __declspec(align(16)) int16_t br[48 * 16], bv[48 * 16];
    int offsets[48];
    uint8_t nnzc[15 * 8];
    int op, trial, i, failures = 0;

    memset(offsets, 0, sizeof(offsets));
    for (i = 0; i < 16; i++)
        offsets[i] = ((i & 1) * 4 + ((i >> 2) & 1) * 8) +
                     (((i >> 1) & 1) * 4 + ((i >> 3) & 1) * 8) * T_STRIDE;
    for (i = 0; i < 4; i++)
        offsets[16 + i] = offsets[32 + i] = (i & 1) * 4 + (i >> 1) * 4 * T_STRIDE;
    idct_batch_ok = 15;
    for (op = 0; op < 4; op++) {
        for (trial = 0; trial < 24; trial++) {
            uint8_t *dr[2] = { ref + 20 * T_STRIDE, ref + 32 * T_STRIDE };
            uint8_t *dv[2] = { got + 20 * T_STRIDE, got + 32 * T_STRIDE };
            fill(ref, sizeof(ref));
            memcpy(got, ref, sizeof(ref));
            memset(br, 0, sizeof(br));
            memset(nnzc, 0, sizeof(nnzc));
            for (i = 0; i < 36; i++) {
                int mode, count;
                if (op == 2 ? !(i >= 16 && i < 20 || i >= 32 && i < 36) : i >= 16)
                    continue;
                if (op == 3 && (i & 3))
                    continue;
                count = op == 3 ? 64 : 16;
                mode = (trial + i) % 3;
                if (mode == 1) {
                    br[i * 16] = (int16_t)rng();
                    nnzc[xbox_scan8[i]] = 1;
                } else if (mode == 2) {
                    fill_coeffs(br + i * 16, count);
                    nnzc[xbox_scan8[i]] = (uint8_t)count;
                }
            }
            memcpy(bv, br, sizeof(br));
            if (op == 0) {
                dc->h264_idct_add16(ref, offsets, br, T_STRIDE, nnzc);
                idct_add16_vmx(got, offsets, bv, T_STRIDE, nnzc);
            } else if (op == 1) {
                dc->h264_idct_add16intra(ref, offsets, br, T_STRIDE, nnzc);
                idct_add16intra_vmx(got, offsets, bv, T_STRIDE, nnzc);
            } else if (op == 2) {
                dc->h264_idct_add8(dr, offsets, br, T_STRIDE, nnzc);
                idct_add8_vmx(dv, offsets, bv, T_STRIDE, nnzc);
            } else {
                dc->h264_idct8_add4(ref, offsets, br, T_STRIDE, nnzc);
                idct8_add4_vmx(got, offsets, bv, T_STRIDE, nnzc);
            }
            if (memcmp(ref, got, sizeof(ref)) || memcmp(br, bv, sizeof(br))) {
                idct_batch_ok &= ~(1u << op);
                failures++;
                if (log) log("vmx selftest: IDCT batch mismatch; keeping C batch implementation\n");
                break;
            }
        }
    }
    return failures;
}

/* LF_TEST_BEGIN: also extracted unchanged into the host reference fixture. */
static int test_loopfilters(H264DSPContext *dc, void (*log)(const char *))
{
    static __declspec(align(16)) uint8_t ref[97*32+64], got[97*32+64];
    static const int strides[] = {32,47,64,80,97,-32,-47,-64,-80,-97};
    static const int thresholds[] = {0,1,2,4,8,16,32,64,128,255};
    void (*cf[4])(uint8_t*,int,int,int,int8_t*);
    void (*vf[4])(uint8_t*,int,int,int,int8_t*);
    void (*ci[4])(uint8_t*,int,int,int);
    void (*vi[4])(uint8_t*,int,int,int);
    int op, trial, i, failures=0;
    cf[0]=dc->h264_v_loop_filter_luma; cf[1]=dc->h264_h_loop_filter_luma;
    cf[2]=dc->h264_v_loop_filter_chroma; cf[3]=dc->h264_h_loop_filter_chroma;
    vf[0]=lf_v_luma; vf[1]=lf_h_luma; vf[2]=lf_v_chroma; vf[3]=lf_h_chroma;
    ci[0]=dc->h264_v_loop_filter_luma_intra; ci[1]=dc->h264_h_loop_filter_luma_intra;
    ci[2]=dc->h264_v_loop_filter_chroma_intra; ci[3]=dc->h264_h_loop_filter_chroma_intra;
    vi[0]=lf_vi_luma; vi[1]=lf_hi_luma; vi[2]=lf_vi_chroma; vi[3]=lf_hi_chroma;
    loopfilter_ok=255;
    for (op=0;op<8;op++) {
        if (log) {
            char line[96];
            _snprintf(line,sizeof(line),"vmx selftest: loopfilter entry %d begin\n",op);
            line[sizeof(line)-1]=0; log(line);
        }
        for (trial=0;trial<8000;trial++) {
            int stride=strides[(trial/16)%10];
            int offset=(stride>0?8*stride:-24*stride)+16+(trial%16);
            int alpha=thresholds[(trial/80)%10], beta=thresholds[(trial/800)%10];
            int kind=(trial/100)%8, intra=(op%4)>=2;
            int index=(op/4)*2+(op&1), xs=(op&1)?1:stride, ys=(op&1)?stride:1;
            int8_t tc[4];
            for (i=0;i<(int)sizeof(ref);i++) {
                unsigned r=rng();
                ref[i]=kind==0?0:kind==1?255:kind==2?(uint8_t)i:
                       kind==3?(uint8_t)(r%9+120):kind==4?(uint8_t)(r&1?0:255):(uint8_t)r;
            }
            /* Deliberately weak/strong p/q boundaries: random-only testing
             * rarely enters the strong intra branch at small beta. */
            if (kind>=5) {
                int lane, k;
                for (lane=0;lane<(op<4?16:8);lane++) {
                    int base=(trial&1)?240:8;
                    for (k=-4;k<=3;k++)
                        ref[offset+lane*ys+k*xs]=(uint8_t)(base+(k>=0?(kind==5?1:kind==6?12:0):0)+(rng()%3));
                }
            }
            for (i=0;i<4;i++) tc[i]=(int8_t)(((trial+i)%7)==0?-1:((trial+i)%7)==1?0:(trial+i)%26);
            memcpy(got,ref,sizeof(ref));
            if (intra) {
                ci[index](ref+offset,stride,alpha,beta);
                vi[index](got+offset,stride,alpha,beta);
            } else {
                cf[index](ref+offset,stride,alpha,beta,tc);
                vf[index](got+offset,stride,alpha,beta,tc);
            }
            /* Whole allocation includes padding on every row and outer guards. */
            if (memcmp(ref,got,sizeof(ref))) {
                loopfilter_ok &= ~(1u<<op); failures++;
                if (log) {
                    char line[128];
                    _snprintf(line,sizeof(line),"vmx selftest: loopfilter entry %d trial %d MISMATCH; keeping C\n",op,trial);
                    line[sizeof(line)-1]=0; log(line);
                }
                break;
            }
        }
    }
    if (log) {
        char line[128];
        static const char *names[8] = {
            "V luma", "H luma", "V luma intra", "H luma intra",
            "V chroma", "H chroma", "V chroma intra", "H chroma intra"
        };
        for (op=0;op<8;op++) {
            _snprintf(line,sizeof(line),"vmx selftest: %s: %s\n",names[op],
                      (loopfilter_ok & (1u<<op)) ? "VMX PASS" : "C FALLBACK");
            line[sizeof(line)-1]=0; log(line);
        }
        _snprintf(line,sizeof(line),"vmx selftest: loopfilter %02x, %d mismatches (8000 cases/entry plus guards)\n",loopfilter_ok,failures);
        line[sizeof(line)-1]=0; log(line);
    }
    return failures;
}
/* LF_TEST_END */

int ff_xbox_dsp_selftest(void (*log)(const char *line))
{
    static __declspec(align(16)) uint8_t src[T_STRIDE * T_ROWS];
    static __declspec(align(16)) uint8_t dref[T_STRIDE * 20], dvmx[T_STRIDE * 20];
    static __declspec(align(16)) int16_t bref[64], bvmx[64];
    H264QpelContext qc;
    H264ChromaContext cc;
    H264DSPContext dc;
    int saved = ff_xbox_vmx_flags, failures = 0, s, op, p, trial, fx, fy;
    char line[160];

    /* C reference tables: the hooks don't touch them while flags are 0. */
    ff_xbox_vmx_flags = 0;
    ff_h264qpel_init(&qc, 8);
    ff_h264chroma_init(&cc, 8);
    ff_h264dsp_init(&dc, 8, 1);
    ff_xbox_vmx_flags = saved;

    if (log) log("vmx selftest: C reference init complete; QPEL begin\n");
    for (s = 0; s < 2; s++) {
        int n = s ? 8 : 16;
        for (op = 0; op < 2; op++) {
            qpel_ok[s][op] = 0;
            for (p = 0; p < 16; p++) {
                qpel_mc_func cf = op ? qc.avg_h264_qpel_pixels_tab[s][p] : qc.put_h264_qpel_pixels_tab[s][p];
                qpel_mc_func vf = vmx_qpel[s][op][p];
                int ok = 1;
                if (log) {
                    _snprintf(line,sizeof(line),"vmx selftest: QPEL size %d op %d pos %d begin\n",n,op,p);
                    line[sizeof(line)-1]=0; log(line);
                }

                for (trial = 0; trial < 64 && ok; trial++) {
                    /* Rows -2..n+3 and columns -2..n+3 (+16 over-read) must stay inside src. */
                    uint8_t *sp = src + 3 * T_STRIDE + 3 + (trial & 15);
                    int doff = (n == 8 && (trial & 1)) ? 8 : 0;

                    fill(src, sizeof(src));
                    fill(dref, sizeof(dref));
                    memcpy(dvmx, dref, sizeof(dref));
                    cf(dref + doff, sp, T_STRIDE);
                    vf(dvmx + doff, sp, T_STRIDE);
                    if (!same(dref, dvmx, T_STRIDE, n, &fx, &fy)) {
                        report(log, op ? "avg_qpel" : "put_qpel", n, p, dref, dvmx, fx, fy);
                        ok = 0;
                    }
                }
                if (ok)
                    qpel_ok[s][op] |= 1u << p;
                else
                    failures++;
            }
        }
    }

    for (s = 0; s < 2; s++) {
        int n = s ? 4 : 8;
        for (op = 0; op < 2; op++) {
            h264_chroma_mc_func cf = op ? cc.avg_h264_chroma_pixels_tab[s] : cc.put_h264_chroma_pixels_tab[s];
            h264_chroma_mc_func vf = vmx_chroma[s][op];
            int ok = 1;

            for (trial = 0; trial < 64 * 3 && ok; trial++) {
                uint8_t *sp = src + 2 * T_STRIDE + (trial & 15);
                int h = (trial % 3 == 0) ? 2 : (trial % 3 == 1) ? 4 : 8;
                int x = (trial / 3) & 7, y = (trial / 24) & 7;
                int doff = (rng() & 1) ? n : 0;

                fill(src, sizeof(src));
                fill(dref, sizeof(dref));
                memcpy(dvmx, dref, sizeof(dref));
                cf(dref + doff, sp, T_STRIDE, h, x, y);
                vf(dvmx + doff, sp, T_STRIDE, h, x, y);
                if (!same(dref, dvmx, T_STRIDE, h, &fx, &fy)) {
                    report(log, op ? "avg_chroma" : "put_chroma", n, x + 8 * y, dref, dvmx, fx, fy);
                    ok = 0;
                }
            }
            chroma_ok[s][op] = ok;
            if (!ok)
                failures++;
        }
    }

    if (log) log("vmx selftest: chroma complete; IDCT begin\n");
    /* Detect whether FFmpeg's C IDCT clears block[0] itself. */
    memset(dref, 0, sizeof(dref));
    memset(bref, 0, sizeof(bref));
    bref[0] = 64;
    dc.h264_idct_dc_add(dref, bref, T_STRIDE);
    idct_clears_block = (bref[0] == 0);

    idct_ok = 0x0F;
    for (trial = 0; trial < 64; trial++) {
        int doff4 = (trial & 3) * 4;
        int doff8 = (trial & 1) * 8;

        /* 4x4 IDCT */
        fill(dref, sizeof(dref));
        memcpy(dvmx, dref, sizeof(dref));
        fill_coeffs(bref, 16);
        memcpy(bvmx, bref, 16 * sizeof(int16_t));
        dc.h264_idct_add(dref + doff4, bref, T_STRIDE);
        idct_add_vmx(dvmx + doff4, bvmx, T_STRIDE);
        if ((idct_ok & 1) && !same_idct(dref, dvmx, 4, bref, bvmx, 16, &fx, &fy)) {
            report(log, "idct_add", 4, 0, dref, dvmx, fx, fy);
            idct_ok &= ~1u;
            failures++;
        }

        /* 4x4 DC IDCT */
        fill(dref, sizeof(dref));
        memcpy(dvmx, dref, sizeof(dref));
        fill_coeffs(bref, 16);
        memcpy(bvmx, bref, 16 * sizeof(int16_t));
        dc.h264_idct_dc_add(dref + doff4, bref, T_STRIDE);
        idct_dc_add_vmx(dvmx + doff4, bvmx, T_STRIDE);
        if ((idct_ok & 2) && !same_idct(dref, dvmx, 4, bref, bvmx, 16, &fx, &fy)) {
            report(log, "idct_dc_add", 4, 0, dref, dvmx, fx, fy);
            idct_ok &= ~2u;
            failures++;
        }

        /* 8x8 IDCT */
        fill(dref, sizeof(dref));
        memcpy(dvmx, dref, sizeof(dref));
        fill_coeffs(bref, 64);
        memcpy(bvmx, bref, 64 * sizeof(int16_t));
        dc.h264_idct8_add(dref + doff8, bref, T_STRIDE);
        idct8_add_vmx(dvmx + doff8, bvmx, T_STRIDE);
        if ((idct_ok & 4) && !same_idct(dref, dvmx, 8, bref, bvmx, 64, &fx, &fy)) {
            report(log, "idct8_add", 8, 0, dref, dvmx, fx, fy);
            idct_ok &= ~4u;
            failures++;
        }

        /* 8x8 DC IDCT */
        fill(dref, sizeof(dref));
        memcpy(dvmx, dref, sizeof(dref));
        fill_coeffs(bref, 64);
        memcpy(bvmx, bref, 64 * sizeof(int16_t));
        dc.h264_idct8_dc_add(dref + doff8, bref, T_STRIDE);
        idct8_dc_add_vmx(dvmx + doff8, bvmx, T_STRIDE);
        if ((idct_ok & 8) && !same_idct(dref, dvmx, 8, bref, bvmx, 64, &fx, &fy)) {
            report(log, "idct8_dc_add", 8, 0, dref, dvmx, fx, fy);
            idct_ok &= ~8u;
            failures++;
        }
    }

    if (log) log("vmx selftest: IDCT batches begin\n");
    failures += test_idct_batches(&dc, log);
    if (log) log("vmx selftest: IDCT batches complete; LOOPFILTER begin\n");
    failures += test_loopfilters(&dc, log);
    tested = 1;
    if (log) {
        _snprintf(line, sizeof(line), "vmx selftest: qpel16 put %04x avg %04x, qpel8 put %04x avg %04x, "
                  "chroma8 %d/%d, chroma4 %d/%d, idct %x batch %x, %d mismatches\n",
                  qpel_ok[0][0], qpel_ok[0][1], qpel_ok[1][0], qpel_ok[1][1],
                  chroma_ok[0][0], chroma_ok[0][1], chroma_ok[1][0], chroma_ok[1][1],
                  idct_ok, idct_batch_ok, failures);
        line[sizeof(line) - 1] = '\0';
        log(line);
    }
    return failures;
}
