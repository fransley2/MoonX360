#ifndef MOONX_CRT_OUTPUT_H
#define MOONX_CRT_OUTPUT_H

#include <stdio.h>
#include <string.h>

#if defined(_MSC_VER) && !defined(__cplusplus)
#define CRT_INLINE static __inline
#else
#define CRT_INLINE static inline
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CRT_S240_MAGIC 0x53323430u /* 'S240' */
#define CRT_S240_VERSION 3u
#define CRT_S240_MIN_PLUGIN_VER 0x00020000u /* v2.0+ scans v2/v3 beacon */

enum crt_s240_cmd {
    CRT_S240_CMD_AUTO = 0,
    CRT_S240_CMD_FORCE_240P = 1,
    CRT_S240_CMD_FORCE_480I = 2
};

enum crt_s240_plugin_mode {
    CRT_PLUGIN_MODE_480I = 0,
    CRT_PLUGIN_MODE_240P = 1,
    CRT_PLUGIN_MODE_1280X240 = 2,
    CRT_PLUGIN_MODE_1920X240 = 3
};

enum crt_console_output_kind {
    CRT_OUTPUT_HIDEF = 0,
    CRT_OUTPUT_480I = 1,
    CRT_OUTPUT_480P = 2,
    CRT_OUTPUT_PAL50_OR_OTHER = 3
};

enum crt_bridge_reason {
    CRT_REASON_READY = 0,
    CRT_REASON_ACTIVE_240P = 1,
    CRT_REASON_NO_PLUGIN = 2,
    CRT_REASON_PLUGIN_VERSION = 3,
    CRT_REASON_HIDEF_MODE = 4,
    CRT_REASON_NOT_INTERLACED = 5,
    CRT_REASON_NOT_60HZ = 6,
    CRT_REASON_SYSTEM_UI = 7,
    CRT_REASON_BACKBUFFER_FAILED = 8,
    CRT_REASON_TIMEOUT = 9,
    CRT_REASON_HOST_ONLY = 10,
    CRT_REASON_RESTORE_FAILED = 11
};

/* Clean-room S240 v3 memory beacon ABI shared with external t240.xex plugins. */
struct crt_s240_beacon {
    unsigned int magic;
    unsigned int version;
    unsigned int is_menu;
    unsigned int width;
    unsigned int height;
    unsigned int in_game;
    unsigned int cmd;
    unsigned int plugin_ver;
    unsigned int plugin_mode;
};

struct crt_output_status {
    int console_output_kind;   /* enum crt_console_output_kind */
    int display_width;
    int display_height;
    int interlaced;
    int hidef;
    int widescreen;
    int refresh_hz_x100;       /* e.g. 5994 or 6000 */
    int plugin_detected;
    unsigned int plugin_ver;
    int plugin_mode;           /* enum crt_s240_plugin_mode */
    int backbuffer_240p;
    int retro_240p_available;
    int active_240p;
    int reason;                /* enum crt_bridge_reason */
    int last_cmd;
    unsigned int last_error_count;
};

CRT_INLINE int crt_output_is_240p_mode(int plugin_mode) {
    return plugin_mode == CRT_PLUGIN_MODE_240P ||
           plugin_mode == CRT_PLUGIN_MODE_1280X240 ||
           plugin_mode == CRT_PLUGIN_MODE_1920X240;
}

CRT_INLINE void crt_output_evaluate(struct crt_output_status *st, int is_xbox_hardware) {
    if (!st) return;
    if (!is_xbox_hardware) {
        st->console_output_kind = CRT_OUTPUT_HIDEF;
        st->retro_240p_available = 0;
        st->active_240p = 0;
        st->reason = CRT_REASON_HOST_ONLY;
        return;
    }

    if (st->hidef || st->display_width <= 0 || st->display_height <= 0 ||
        st->display_width > 720 || st->display_height > 576) {
        st->console_output_kind = CRT_OUTPUT_HIDEF;
    } else if (st->display_height > 480 ||
               (st->refresh_hz_x100 > 0 && (st->refresh_hz_x100 < 5700 || st->refresh_hz_x100 > 6300))) {
        st->console_output_kind = CRT_OUTPUT_PAL50_OR_OTHER;
    } else if (!st->interlaced) {
        st->console_output_kind = CRT_OUTPUT_480P;
    } else {
        st->console_output_kind = CRT_OUTPUT_480I;
    }

    st->plugin_detected = st->plugin_ver != 0u ? 1 : 0;
    st->active_240p = (st->backbuffer_240p && st->plugin_detected &&
                       st->plugin_mode == CRT_PLUGIN_MODE_240P) ? 1 : 0;

    if (st->console_output_kind == CRT_OUTPUT_HIDEF) {
        st->retro_240p_available = 0;
        st->active_240p = 0;
        st->reason = CRT_REASON_HIDEF_MODE;
    } else if (st->console_output_kind == CRT_OUTPUT_PAL50_OR_OTHER) {
        st->retro_240p_available = 0;
        st->active_240p = 0;
        st->reason = CRT_REASON_NOT_60HZ;
    } else if (st->console_output_kind == CRT_OUTPUT_480P && !st->active_240p) {
        st->retro_240p_available = 0;
        st->reason = CRT_REASON_NOT_INTERLACED;
    } else if (!st->plugin_detected) {
        st->retro_240p_available = 0;
        st->active_240p = 0;
        st->reason = CRT_REASON_NO_PLUGIN;
    } else if (st->plugin_ver < CRT_S240_MIN_PLUGIN_VER) {
        st->retro_240p_available = 0;
        st->active_240p = 0;
        st->reason = CRT_REASON_PLUGIN_VERSION;
    } else {
        st->retro_240p_available = 1;
        st->reason = st->active_240p ? CRT_REASON_ACTIVE_240P : CRT_REASON_READY;
    }
}

CRT_INLINE void crt_output_format_version(unsigned int ver, char *out, size_t out_sz) {
    unsigned int major, minor, patch;
    if (!out || out_sz == 0) return;
    if (ver == 0u) {
        snprintf(out, out_sz, "None");
        out[out_sz - 1] = '\0';
        return;
    }
    major = (ver >> 16) & 0xFFu;
    minor = (ver >> 8) & 0xFFu;
    patch = ver & 0xFFu;
    if (patch != 0u) {
        snprintf(out, out_sz, "v%u.%02u.%u", major, minor, patch);
    } else {
        snprintf(out, out_sz, "v%u.%02u", major, minor);
    }
    out[out_sz - 1] = '\0';
}

CRT_INLINE const char *crt_output_reason_text(int reason) {
    switch (reason) {
        case CRT_REASON_READY:
            return "Ready (480i + t240.xex verified)";
        case CRT_REASON_ACTIVE_240P:
            return "240p progressive active";
        case CRT_REASON_NO_PLUGIN:
            return "t240.xex plugin not detected";
        case CRT_REASON_PLUGIN_VERSION:
            return "Incompatible t240.xex version (requires v2.0+)";
        case CRT_REASON_HIDEF_MODE:
            return "Console is set to HD video mode (480i SD required)";
        case CRT_REASON_NOT_INTERLACED:
            return "Console is set to 480p progressive (480i required)";
        case CRT_REASON_NOT_60HZ:
            return "Console is in 50Hz PAL mode (60Hz required for 240p)";
        case CRT_REASON_SYSTEM_UI:
            return "Xbox Guide open (480i safely restored)";
        case CRT_REASON_BACKBUFFER_FAILED:
            return "Failed to configure 640x240 D3D9 backbuffer";
        case CRT_REASON_TIMEOUT:
            return "t240.xex did not confirm 240p mode switch";
        case CRT_REASON_HOST_ONLY:
            return "Xbox 360 analog output unavailable on host build";
        case CRT_REASON_RESTORE_FAILED:
            return "Warning: timed out waiting for 480i restore confirmation";
        default:
            return "Unknown CRT output state";
    }
}

CRT_INLINE const char *crt_output_mode_short(const struct crt_output_status *st) {
    if (!st) return "Unknown";
    if (st->active_240p) return "240p";
    switch (st->console_output_kind) {
        case CRT_OUTPUT_480I: return "480i";
        case CRT_OUTPUT_480P: return "480p";
        case CRT_OUTPUT_PAL50_OR_OTHER: return st->display_height > 480 ? "576i/50Hz" : "50Hz/Other";
        default: return "HD";
    }
}

#ifdef __cplusplus
}
#endif

#endif /* MOONX_CRT_OUTPUT_H */
