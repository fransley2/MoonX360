/*
 * Xbox 360 (Xenon VMX) DSP functions for XBMC-360's FFmpeg 1.2, installed by
 * hooks at the end of FFmpeg's own init functions (see build_ffmpeg.py).
 * Compiled as part of avcodec.lib; the app only uses the functions below.
 */
#ifndef XBOX_DSP_H
#define XBOX_DSP_H

/* Groups of functions the app can switch on (ff_xbox_vmx_flags). */
#define XVMX_QPEL     0x01   /* luma motion compensation (incl. full-pel copy) */
#define XVMX_CHROMA   0x02   /* chroma motion compensation */
#define XVMX_PREFETCH 0x04   /* dcbt prefetch of reference rows (XBMC-360) */
#define XVMX_IDCT     0x08   /* H.264 4x4, 8x8 and DC inverse DCT */
#define XVMX_LOOPFILTER 0x10 /* progressive 8-bit H.264 in-loop deblocking */
#define XVMX_ALL      0x1F
/* Individual loop-filter selection for native profiling. Bit order follows
 * the eight-entry selftest report: V/H luma, V/H intra luma, V/H chroma,
 * V/H intra chroma. Validity testing remains independent of this mask. */
#define XVMX_LF_ALL        0xFFu
#define XVMX_LF_VERTICAL   0x55u
#define XVMX_LF_HORIZONTAL 0xAAu
#define XVMX_LF_NORMAL     0x33u

#ifdef __cplusplus
extern "C" {
#endif

/* Which groups the init hooks install. Change only while no decoder is open. */
extern int ff_xbox_vmx_flags;
extern unsigned ff_xbox_loopfilter_mask; /* change only with no decoder open */

/*
 * Compares every VMX function with FFmpeg's C version on random data and
 * marks the ones that match; only those are ever installed. Must run once
 * before the first decoder is opened. log() receives one line per mismatch
 * and a summary. Returns the number of mismatching functions.
 */
int ff_xbox_dsp_selftest(void (*log)(const char *line));

#ifdef __cplusplus
}
#endif

#endif
