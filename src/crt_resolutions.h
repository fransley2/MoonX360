#ifndef MOONX_CRT_RESOLUTIONS_H
#define MOONX_CRT_RESOLUTIONS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum crt_resolution_tier {
    CRT_RES_TIER_STABLE = 0,
    CRT_RES_TIER_EXPERIMENTAL_240P = 1,
    CRT_RES_TIER_EXPERIMENTAL_SUPERRES = 2,
    CRT_RES_TIER_UNSUPPORTED_LIMIT = 3
};

/*
 * Explicit 4-layer resolution model for CRT streaming:
 *   A. game_w/h   : internal game or emulator render resolution on PC
 *   B. capture_w/h: desktop/window region prepared on host
 *   C. stream_w/h : H.264 encoded stream transmitted to Xbox 360
 *   D. output_w/h : Xbox 360 D3D9 backbuffer / physical analog output
 */
struct crt_resolution_spec {
    const char *label;
    int game_w, game_h;
    int capture_w, capture_h;
    int stream_w, stream_h;
    int output_w, output_h;
    int dar_num, dar_den;
    int tier;
    const char *notes;
};

static const struct crt_resolution_spec crt_community_resolutions[] = {
    {
        "256x240 (NES/SNES 240p)",
        256, 240, 256, 240, 256, 240, 640, 240, 4, 3,
        CRT_RES_TIER_EXPERIMENTAL_240P,
        "Even H.264 dimensions; integer 2x horizontal (512x240) or 4:3 aspect inside 640x240."
    },
    {
        "320x240 (Genesis/PS1/Arcade 240p)",
        320, 240, 320, 240, 320, 240, 640, 240, 4, 3,
        CRT_RES_TIER_EXPERIMENTAL_240P,
        "Exact 2x1 integer scale into 640x240 physical 4:3 scanout; 2x2 into 640x480."
    },
    {
        "400x240 (Widescreen 240p / Arcade)",
        400, 240, 400, 240, 400, 240, 640, 240, 5, 3,
        CRT_RES_TIER_EXPERIMENTAL_240P,
        "Even H.264 dimensions; 400x240 fits 1:1 centered in 640x240 or 5:3 DAR."
    },
    {
        "426x240 (16:9 240p)",
        426, 240, 426, 240, 426, 240, 640, 240, 16, 9,
        CRT_RES_TIER_EXPERIMENTAL_240P,
        "Even luma dimensions. Host encoder and texture pitch support require testing."
    },
    {
        "432x240 (16:9 240p mod16)",
        432, 240, 432, 240, 432, 240, 640, 240, 16, 9,
        CRT_RES_TIER_EXPERIMENTAL_240P,
        "432 and 240 are mod-16 (27x15 macroblocks) with even 216x120 chroma."
    },
    {
        "640x480 (Standard SD CRT)",
        640, 480, 640, 480, 640, 480, 640, 480, 4, 3,
        CRT_RES_TIER_STABLE,
        "Validated stable baseline for 480i/480p CRT output without plugins."
    },
    {
        "640x240 (CRT Retro 240p)",
        640, 240, 640, 240, 640, 240, 640, 240, 4, 3,
        CRT_RES_TIER_EXPERIMENTAL_240P,
        "Physical 240p requires 480i 60Hz console + verified t240.xex plugin."
    },
    {
        "1280x240 (240p Super Resolution)",
        1280, 240, 1280, 240, 1280, 240, 640, 240, 4, 3,
        CRT_RES_TIER_EXPERIMENTAL_SUPERRES,
        "Decoder accepts 1280x240; physical 1280x240 DAC scanout unverified on hardware."
    },
    {
        "1920x240 (240p Super Resolution)",
        1920, 240, 1920, 240, 1920, 240, 640, 240, 4, 3,
        CRT_RES_TIER_EXPERIMENTAL_SUPERRES,
        "Decoder accepts 1920x240; physical 1920x240 DAC scanout unverified on hardware."
    },
    {
        "2560x240 (Ultra-Wide Super Resolution)",
        2560, 240, 2560, 240, 2560, 240, 640, 240, 4, 3,
        CRT_RES_TIER_EXPERIMENTAL_SUPERRES,
        "Experimental 2560x240 stream scaled for the existing 4:3 CRT output."
    }
};

#define CRT_COMMUNITY_RESOLUTION_COUNT ((int)(sizeof(crt_community_resolutions) / sizeof(crt_community_resolutions[0])))

/* Keep the 1080p allocation envelope and allow wider 240p streams for
 * horizontal super resolutions. Decoded sizes may include odd crops. */
static __inline bool crt_video_dimensions_fit(int w, int h)
{
    return w > 0 && h > 0 && h <= 1080 && (w <= 1920 || (h <= 256 && w <= 2560));
}

/* Request even visible dimensions for H.264 4:2:0. */
static __inline bool crt_stream_resolution_supported(int w, int h)
{
    return w >= 16 && h >= 16 && !(w & 1) && !(h & 1) && crt_video_dimensions_fit(w, h);
}

static __inline const char *crt_stream_resolution_rejection_reason(int w, int h)
{
    if (crt_stream_resolution_supported(w, h)) return NULL;
    if (w < 16 || h < 16) return "Dimensions must be at least 16x16.";
    if ((w & 1) || (h & 1)) return "Width and height must be even for H.264 4:2:0.";
    if (h > 1080) return "Height exceeds the 1080-line decoder limit.";
    if (w > 2560) return "Width exceeds the 2560-pixel decoder limit.";
    if (w > 1920 && h > 256) return "Widths above 1920 are limited to streams up to 256 lines.";
    return "Unsupported stream dimensions.";
}

static __inline bool crt_parse_stream_resolution(const char *text, int *width, int *height)
{
    int values[2] = { 0, 0 };
    int i;
    if (!text || !width || !height) return false;

    for (i = 0; i < 2; ++i) {
        unsigned digits = 0;
        while (*text >= '0' && *text <= '9' && digits < 4) {
            values[i] = values[i] * 10 + (*text++ - '0');
            ++digits;
        }
        if (!digits) return false;
        if (i == 0) {
            if (*text != 'x' && *text != 'X' && *text != '*') return false;
            ++text;
        }
    }
    if (*text || !crt_stream_resolution_supported(values[0], values[1])) return false;
    *width = values[0];
    *height = values[1];
    return true;
}

static __inline bool crt_resolution_is_240p_class(int h) {
    return h > 0 && h <= 256;
}

static __inline bool crt_resolution_is_superres(int w, int h) {
    return h == 240 && (w == 640 || w == 1280 || w == 1920 || w == 2560);
}

static __inline void crt_source_dar(int w, int h, int *num, int *den) {
    if (!num || !den) return;
    if (w <= 0 || h <= 0) { *num = 4; *den = 3; return; }
    /* Super-wide 240p modes encode non-square horizontal pixel clocks for 4:3 CRTs. */
    if (crt_resolution_is_superres(w, h)) {
        *num = 4; *den = 3;
        return;
    }
    *num = w;
    *den = h;
}

#ifdef __cplusplus
}
#endif

#endif /* MOONX_CRT_RESOLUTIONS_H */
