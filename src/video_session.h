/* Pure session policy for the XDK backend. No per-frame negotiation. */
#pragma once
#include "video.h"
static int video_hd_colorspace(int width, int height)
{
    return width >= 1280 && height >= 720 ? COLORSPACE_REC_709 : COLORSPACE_REC_601;
}
static int video_session_slices(const struct video_options *opts)
{
    if (opts->slice_count == 8 || opts->slice_count == 12 || opts->slice_count == 16)
        return opts->slice_count;
    return opts->width >= 1920 && opts->height >= 1080 && opts->target_fps >= 60 ? 12 : 8;
}
/* Limited-range, normalized YUV. Y gain and offsets stay in the shader.
 * Chroma coefficients: Rv, Gu, Gv, Bu (Kr/Kb = .2126/.0722 for Rec.709). */
static void video_yuv_coefficients(int colorspace, float coeff[4])
{
    if (colorspace == COLORSPACE_REC_709) {
        coeff[0] = 1.792741f; coeff[1] = -0.213249f;
        coeff[2] = -0.532909f; coeff[3] = 2.112402f;
    } else {
        coeff[0] = 1.596027f; coeff[1] = -0.391762f;
        coeff[2] = -0.812968f; coeff[3] = 2.017232f;
    }
}

/* Mode family keeps a useful label during manual bitrate sweeps. UI preset
 * matching remains exact in config.c; no settings are changed here. */
static const char *video_mode_name(int width,int height,int fps)
{
    if(fps==60 && width==1280 && height==720) return "720p60";
    if(fps==60 && width==1920 && height==1080) return "1080p60";
    if(fps==60 && width==854 && height==480)return "480p60";
    if(fps==30 && width==1920 && height==1080)return "1080p30";
    return "Custom";
}
