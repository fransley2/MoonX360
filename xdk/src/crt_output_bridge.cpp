#include "crt_output_bridge.h"
#include "xdk.h"
#include "../../src/platform.h"
#include <stdarg.h>

static void bridge_log(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';
    plat_log(buf);
}

/*
 * Isolated S240 v3 IPC beacon placed in .data via non-zero initialization so
 * an external t240.xex DashLaunch plugin can discover and respond to it without
 * MoonX360 touching hardware registers, kernel memory, or DashLaunch settings.
 */
volatile struct crt_s240_beacon g_moonx_s240_beacon = {
    CRT_S240_MAGIC,
    CRT_S240_VERSION,
    1u,
    640u,
    480u,
    0u,
    CRT_S240_CMD_AUTO,
    0u,
    0u
};

static CRITICAL_SECTION bridge_lock;
static bool bridge_lock_ready = false;
static bool bridge_240p_active = false;
static bool bridge_suspended_for_ui = false;
static int bridge_sticky_reason = 0;
static int bridge_orig_bb_w = 640;
static int bridge_orig_bb_h = 480;
static unsigned int bridge_error_count = 0;

static void ensure_lock(void)
{
    if (!bridge_lock_ready) {
        InitializeCriticalSection(&bridge_lock);
        bridge_lock_ready = true;
    }
}

void crt_bridge_init(int backbuffer_w, int backbuffer_h)
{
    ensure_lock();
    EnterCriticalSection(&bridge_lock);
    bridge_orig_bb_w = backbuffer_w > 0 ? backbuffer_w : 640;
    bridge_orig_bb_h = backbuffer_h > 0 ? backbuffer_h : 480;
    bridge_240p_active = false;
    bridge_suspended_for_ui = false;
    bridge_sticky_reason = 0;
    g_moonx_s240_beacon.magic = CRT_S240_MAGIC;
    g_moonx_s240_beacon.version = CRT_S240_VERSION;
    g_moonx_s240_beacon.is_menu = 1u;
    g_moonx_s240_beacon.width = (unsigned int)bridge_orig_bb_w;
    g_moonx_s240_beacon.height = (unsigned int)bridge_orig_bb_h;
    g_moonx_s240_beacon.in_game = 0u;
    g_moonx_s240_beacon.cmd = CRT_S240_CMD_AUTO;
    LeaveCriticalSection(&bridge_lock);
}

void crt_bridge_query(struct crt_output_status *out)
{
    XVIDEO_MODE mode;
    unsigned int pver;
    unsigned int pmode;
    unsigned int last_cmd;
    int sticky;
    unsigned int err_cnt;

    if (!out) return;
    ZeroMemory(out, sizeof(*out));
    ZeroMemory(&mode, sizeof(mode));
    XGetVideoMode(&mode);

    ensure_lock();
    EnterCriticalSection(&bridge_lock);
    pver = g_moonx_s240_beacon.plugin_ver;
    pmode = g_moonx_s240_beacon.plugin_mode;
    last_cmd = g_moonx_s240_beacon.cmd;
    sticky = bridge_sticky_reason;
    err_cnt = bridge_error_count;
    LeaveCriticalSection(&bridge_lock);

    out->display_width = (int)mode.dwDisplayWidth;
    out->display_height = (int)mode.dwDisplayHeight;
    out->interlaced = mode.fIsInterlaced != FALSE ? 1 : 0;
    out->hidef = mode.fIsHiDef != FALSE ? 1 : 0;
    out->widescreen = mode.fIsWideScreen != FALSE ? 1 : 0;
    out->refresh_hz_x100 = (mode.RefreshRate >= 20.0f && mode.RefreshRate <= 240.0f)
        ? (int)(mode.RefreshRate * 100.0f + 0.5f) : 0;
    out->plugin_ver = pver;
    out->plugin_mode = (int)pmode;
    out->backbuffer_240p = gfx_crt_240p_backbuffer_active() ? 1 : 0;
    out->last_cmd = (int)last_cmd;
    out->last_error_count = err_cnt;

    crt_output_evaluate(out, 1);

    if (out->active_240p && !bridge_240p_active) {
        out->active_240p = 0;
    }

    if (sticky != 0 && (out->reason == CRT_REASON_READY || sticky == CRT_REASON_SYSTEM_UI ||
                        sticky == CRT_REASON_BACKBUFFER_FAILED || sticky == CRT_REASON_TIMEOUT ||
                        sticky == CRT_REASON_RESTORE_FAILED)) {
        out->reason = sticky;
    }
}

static bool wait_for_plugin_mode(int expected_mode, int timeout_ms)
{
    int elapsed = 0;
    const int step_ms = 25;
    while (elapsed < timeout_ms) {
        unsigned int pver = g_moonx_s240_beacon.plugin_ver;
        unsigned int pmode = g_moonx_s240_beacon.plugin_mode;
        if (pver >= CRT_S240_MIN_PLUGIN_VER && (int)pmode == expected_mode) {
            return true;
        }
        if (expected_mode == CRT_PLUGIN_MODE_240P && gfx_system_ui_active()) {
            return false;
        }
        Sleep(step_ms);
        elapsed += step_ms;
    }
    return false;
}

bool crt_bridge_enter_240p(int width, int height)
{
    struct crt_output_status st;
    int bw = width > 0 ? width : 640;
    int bh = height > 0 ? height : 240;

    if (bw != 640 || bh != 240) {
        ensure_lock();
        EnterCriticalSection(&bridge_lock);
        bridge_sticky_reason = CRT_REASON_BACKBUFFER_FAILED;
        bridge_error_count++;
        LeaveCriticalSection(&bridge_lock);
        bridge_log("[CRT-BRIDGE] Rejected non-640x240 request (%dx%d)\n", bw, bh);
        return false;
    }

    crt_bridge_query(&st);
    if (!st.retro_240p_available) {
        bridge_log("[CRT-BRIDGE] 240p unavailable: %s\n", crt_output_reason_text(st.reason));
        return false;
    }

    if (gfx_system_ui_active()) {
        ensure_lock();
        EnterCriticalSection(&bridge_lock);
        bridge_sticky_reason = CRT_REASON_SYSTEM_UI;
        LeaveCriticalSection(&bridge_lock);
        bridge_log("[CRT-BRIDGE] Deferred 240p entry while Xbox Guide is active\n");
        return false;
    }

    /* Step 1: Switch D3D9 backbuffer & scaler parameters first. */
    if (!gfx_set_crt_240p_backbuffer(true)) {
        ensure_lock();
        EnterCriticalSection(&bridge_lock);
        bridge_sticky_reason = CRT_REASON_BACKBUFFER_FAILED;
        bridge_error_count++;
        LeaveCriticalSection(&bridge_lock);
        bridge_log("[CRT-BRIDGE] Failed to reset D3D9 device to 640x240 backbuffer\n");
        return false;
    }

    /* Step 2: Request real 240p via S240 v3 beacon and wait for explicit plugin confirmation. */
    ensure_lock();
    EnterCriticalSection(&bridge_lock);
    bridge_sticky_reason = 0;
    bridge_suspended_for_ui = false;
    g_moonx_s240_beacon.is_menu = 0u;
    g_moonx_s240_beacon.width = 640u;
    g_moonx_s240_beacon.height = 240u;
    g_moonx_s240_beacon.in_game = 1u;
    g_moonx_s240_beacon.cmd = CRT_S240_CMD_FORCE_240P;
    LeaveCriticalSection(&bridge_lock);

    if (!wait_for_plugin_mode(CRT_PLUGIN_MODE_240P, 1500)) {
        int fail_reason = gfx_system_ui_active() ? CRT_REASON_SYSTEM_UI : CRT_REASON_TIMEOUT;
        bridge_log("[CRT-BRIDGE] 240p confirmation failed (%s); restoring 480i\n",
                   crt_output_reason_text(fail_reason));
        crt_bridge_exit_240p();
        EnterCriticalSection(&bridge_lock);
        bridge_sticky_reason = fail_reason;
        if (fail_reason == CRT_REASON_TIMEOUT) bridge_error_count++;
        LeaveCriticalSection(&bridge_lock);
        return false;
    }

    EnterCriticalSection(&bridge_lock);
    bridge_240p_active = true;
    bridge_sticky_reason = 0;
    LeaveCriticalSection(&bridge_lock);

    {
        char vbuf[24];
        crt_output_format_version(g_moonx_s240_beacon.plugin_ver, vbuf, sizeof(vbuf));
        bridge_log("[CRT-BRIDGE] Active 240p confirmed (t240 %s, plugin_mode=%u, backbuffer=640x240)\n",
                   vbuf, (unsigned)g_moonx_s240_beacon.plugin_mode);
    }
    return true;
}

bool crt_bridge_exit_240p(void)
{
    bool need_restore;
    bool plugin_present;
    bool plugin_restored = true;
    bool bb_restored = true;

    ensure_lock();
    EnterCriticalSection(&bridge_lock);
    need_restore = bridge_240p_active ||
                   gfx_crt_240p_backbuffer_active() ||
                   g_moonx_s240_beacon.in_game != 0u ||
                   g_moonx_s240_beacon.cmd == CRT_S240_CMD_FORCE_240P;
    plugin_present = g_moonx_s240_beacon.plugin_ver != 0u;

    g_moonx_s240_beacon.is_menu = 1u;
    g_moonx_s240_beacon.width = (unsigned int)bridge_orig_bb_w;
    g_moonx_s240_beacon.height = (unsigned int)bridge_orig_bb_h;
    g_moonx_s240_beacon.in_game = 0u;
    g_moonx_s240_beacon.cmd = need_restore ? CRT_S240_CMD_FORCE_480I : CRT_S240_CMD_AUTO;
    bridge_240p_active = false;
    LeaveCriticalSection(&bridge_lock);

    if (!need_restore) {
        return true;
    }

    if (plugin_present) {
        if (!wait_for_plugin_mode(CRT_PLUGIN_MODE_480I, 1000)) {
            plugin_restored = false;
            bridge_log("[CRT-BRIDGE] Timed out waiting for plugin_mode=0 (480i restore)\n");
        }
    }

    if (gfx_crt_240p_backbuffer_active()) {
        bb_restored = gfx_set_crt_240p_backbuffer(false);
        if (!bb_restored) {
            bridge_log("[CRT-BRIDGE] Failed to restore standard UI backbuffer after 240p\n");
        }
    }

    EnterCriticalSection(&bridge_lock);
    g_moonx_s240_beacon.cmd = CRT_S240_CMD_AUTO;
    if (!plugin_restored) {
        bridge_sticky_reason = CRT_REASON_RESTORE_FAILED;
        bridge_error_count++;
    } else if (!bridge_suspended_for_ui) {
        bridge_sticky_reason = 0;
    }
    LeaveCriticalSection(&bridge_lock);

    bridge_log("[CRT-BRIDGE] Restored 480i/UI output (plugin=%s backbuffer=%s)\n",
               plugin_restored ? "OK" : "TIMEOUT", bb_restored ? "OK" : "FAIL");
    return plugin_restored && bb_restored;
}

void crt_bridge_on_system_ui(bool active)
{
    /*
     * Runs from XNotify thread: never call IDirect3DDevice9::Reset() here.
     * Instead switch the S240 beacon to 480i menu mode immediately so t240.xex
     * restores 480i scanout before/while the Xbox Guide overlays the screen.
     */
    ensure_lock();
    EnterCriticalSection(&bridge_lock);
    if (active) {
        if (bridge_240p_active || g_moonx_s240_beacon.in_game != 0u ||
            g_moonx_s240_beacon.cmd == CRT_S240_CMD_FORCE_240P) {
            g_moonx_s240_beacon.is_menu = 1u;
            g_moonx_s240_beacon.width = (unsigned int)bridge_orig_bb_w;
            g_moonx_s240_beacon.height = (unsigned int)bridge_orig_bb_h;
            g_moonx_s240_beacon.in_game = 0u;
            g_moonx_s240_beacon.cmd = CRT_S240_CMD_FORCE_480I;
            bridge_240p_active = false;
            bridge_suspended_for_ui = true;
            bridge_sticky_reason = CRT_REASON_SYSTEM_UI;
            bridge_log("[CRT-BRIDGE] Xbox Guide opened: switched S240 beacon to 480i menu mode\n");
        }
    } else if (bridge_suspended_for_ui) {
        g_moonx_s240_beacon.cmd = CRT_S240_CMD_AUTO;
        bridge_suspended_for_ui = false;
        if (bridge_sticky_reason == CRT_REASON_SYSTEM_UI) {
            bridge_sticky_reason = 0;
        }
        bridge_log("[CRT-BRIDGE] Xbox Guide closed: keeping safe 480i/UI state\n");
    }
    LeaveCriticalSection(&bridge_lock);
}

bool crt_bridge_is_240p_active(void)
{
    bool active;
    ensure_lock();
    EnterCriticalSection(&bridge_lock);
    active = bridge_240p_active &&
             gfx_crt_240p_backbuffer_active() &&
             g_moonx_s240_beacon.plugin_ver >= CRT_S240_MIN_PLUGIN_VER &&
             g_moonx_s240_beacon.plugin_mode == CRT_PLUGIN_MODE_240P;
    LeaveCriticalSection(&bridge_lock);
    return active;
}

int crt_bridge_last_reason(void)
{
    struct crt_output_status st;
    crt_bridge_query(&st);
    return st.reason;
}

const volatile struct crt_s240_beacon *crt_bridge_beacon_ptr(void)
{
    return &g_moonx_s240_beacon;
}
