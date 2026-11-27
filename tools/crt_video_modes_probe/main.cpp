/*
 * MoonX360 Standalone CRT Video Modes Probe (Xbox 360 XDK)
 *
 * Independent diagnostic XEX to enumerate XGetVideoMode / XGetVideoFlags,
 * safely probe D3D9 backbuffer modes (240p, 480p, Super Resolutions),
 * measure VSync Present stability (p50/p95/p99), display pixel-exact CRT
 * test patterns, and automatically restore a safe mode on failure or exit.
 *
 * Never performs raw Xenos MMIO writes or kernel patches. External t240.xex
 * interoperability uses only the clean-room S240 v3 .data beacon.
 */
#include <xtl.h>
#include <d3dx9shader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifndef snprintf
#define snprintf _snprintf
#endif
#include "../../src/crt_output.h"

volatile struct crt_s240_beacon g_probe_s240_beacon = {
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

struct probe_mode_spec {
    const char *name;
    int width;
    int height;
    bool use_scaler;
};

static const struct probe_mode_spec g_modes[] = {
    { "640x480 (Safe SD)",       640,  480, false },
    { "640x240 (CRT 240p)",      640,  240, true  },
    { "320x240 (Native 240p)",   320,  240, true  },
    { "1280x240 (SuperRes 2x)", 1280,  240, true  },
    { "1920x240 (SuperRes 3x)", 1920,  240, true  },
    { "1280x720 (HD 720p)",     1280,  720, false }
};
#define NUM_MODES ((int)(sizeof(g_modes) / sizeof(g_modes[0])))

enum probe_pattern {
    PAT_CHECKERBOARD = 0,
    PAT_H_LINES,
    PAT_V_LINES,
    PAT_PIXEL_GRID,
    PAT_GRADIENT,
    PAT_SCROLL_H,
    PAT_SCROLL_V,
    PAT_STATIC_OVERSCAN,
    PAT_COUNT
};

static const char *g_pattern_names[PAT_COUNT] = {
    "1x1 Checkerboard",
    "Horizontal 1px Lines",
    "Vertical 1px Lines",
    "Pixel Art 16x16 Grid",
    "Luma + RGB Gradients",
    "Horizontal Motion Bar",
    "Vertical Motion Bar",
    "Static Overscan Frame"
};

struct mode_probe_result {
    const char *name;
    int width, height;
    bool reset_ok;
    HRESULT hr;
    uint32_t p50_us, p95_us, p99_us, max_us;
};

static D3DDevice *g_dev = NULL;
static D3DVertexDeclaration *g_decl = NULL;
static D3DVertexShader *g_vs = NULL;
static D3DPixelShader *g_ps = NULL;
static D3DTexture *g_pat_tex = NULL;
static int g_tex_w = 0, g_tex_h = 0;
static int g_cur_w = 640, g_cur_h = 480;
static int g_safe_w = 640, g_safe_h = 480;
static LARGE_INTEGER g_qpc_freq;

static const char g_vs_src[] =
    "struct V { float4 pos : POSITION; float2 uv : TEXCOORD0; };\n"
    "V main(V v) { return v; }\n";
static const char g_ps_src[] =
    "sampler tex0 : register(s0);\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR { return float4(tex2D(tex0, uv).rgb, 1); }\n";

static uint64_t now_us(void) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    if (g_qpc_freq.QuadPart <= 0) return 0;
    return (uint64_t)((double)t.QuadPart * 1000000.0 / (double)g_qpc_freq.QuadPart);
}

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x > y ? 1 : (x < y ? -1 : 0);
}

static bool init_d3d(int w, int h) {
    Direct3D *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) return false;
    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = (UINT)w;
    pp.BackBufferHeight = (UINT)h;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.MultiSampleType = D3DMULTISAMPLE_NONE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    if (FAILED(d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &g_dev))) {
        pp.BackBufferWidth = 640;
        pp.BackBufferHeight = 480;
        if (FAILED(d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &g_dev))) {
            d3d->Release();
            return false;
        }
        g_cur_w = 640; g_cur_h = 480;
    } else {
        g_cur_w = w; g_cur_h = h;
    }
    d3d->Release();

    static const D3DVERTEXELEMENT9 elems[] = {
        { 0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 8, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
        D3DDECL_END()
    };
    if (FAILED(g_dev->CreateVertexDeclaration(elems, &g_decl))) return false;
    ID3DXBuffer *code = NULL;
    if (FAILED(D3DXCompileShader(g_vs_src, (UINT)strlen(g_vs_src), NULL, NULL, "main", "vs_3_0", 0, &code, NULL, NULL))) return false;
    g_dev->CreateVertexShader((const DWORD *)code->GetBufferPointer(), &g_vs);
    code->Release();
    if (FAILED(D3DXCompileShader(g_ps_src, (UINT)strlen(g_ps_src), NULL, NULL, "main", "ps_3_0", 0, &code, NULL, NULL))) return false;
    g_dev->CreatePixelShader((const DWORD *)code->GetBufferPointer(), &g_ps);
    code->Release();
    g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_HALFPIXELOFFSET, TRUE);
    g_dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    g_dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    return true;
}

static bool reset_mode(int w, int h, bool use_scaler, HRESULT *out_hr) {
    if (!g_dev) return false;
    g_dev->BlockUntilIdle();
    g_dev->SetTexture(0, NULL);
    if (g_pat_tex) { g_pat_tex->Release(); g_pat_tex = NULL; g_tex_w = g_tex_h = 0; }

    XVIDEO_MODE xm;
    ZeroMemory(&xm, sizeof(xm));
    XGetVideoMode(&xm);

    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = (UINT)w;
    pp.BackBufferHeight = (UINT)h;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.MultiSampleType = D3DMULTISAMPLE_NONE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    if (use_scaler) {
        pp.Flags = D3DPRESENTFLAG_NO_LETTERBOX;
        pp.VideoScalerParameters.ScalerSourceRect.x1 = 0;
        pp.VideoScalerParameters.ScalerSourceRect.y1 = 0;
        pp.VideoScalerParameters.ScalerSourceRect.x2 = (LONG)w;
        pp.VideoScalerParameters.ScalerSourceRect.y2 = (LONG)h;
        pp.VideoScalerParameters.ScaledOutputWidth = xm.dwDisplayWidth ? xm.dwDisplayWidth : 640;
        pp.VideoScalerParameters.ScaledOutputHeight = xm.dwDisplayHeight ? xm.dwDisplayHeight : 480;
    }

    HRESULT hr = g_dev->Reset(&pp);
    if (FAILED(hr) && use_scaler) {
        ZeroMemory(&pp.VideoScalerParameters, sizeof(pp.VideoScalerParameters));
        hr = g_dev->Reset(&pp);
    }
    if (out_hr) *out_hr = hr;
    if (SUCCEEDED(hr)) {
        g_cur_w = w; g_cur_h = h;
        g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        g_dev->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_dev->SetRenderState(D3DRS_HALFPIXELOFFSET, TRUE);
        g_dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        g_dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        return true;
    }
    return false;
}

static void draw_pattern(int pat, int frame) {
    if (!g_dev) return;
    if (!g_pat_tex || g_tex_w != g_cur_w || g_tex_h != g_cur_h) {
        if (g_pat_tex) g_pat_tex->Release();
        if (FAILED(g_dev->CreateTexture((UINT)g_cur_w, (UINT)g_cur_h, 1, 0, D3DFMT_LIN_X8R8G8B8, 0, &g_pat_tex, NULL))) {
            g_pat_tex = NULL;
            return;
        }
        g_tex_w = g_cur_w; g_tex_h = g_cur_h;
    }
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(g_pat_tex->LockRect(0, &lr, NULL, 0)) && lr.pBits) {
        int bar_x = (frame * 4) % g_cur_w;
        int bar_y = (frame * 2) % g_cur_h;
        for (int y = 0; y < g_cur_h; y++) {
            uint32_t *row = (uint32_t *)((uint8_t *)lr.pBits + (size_t)y * (size_t)lr.Pitch);
            for (int x = 0; x < g_cur_w; x++) {
                uint32_t c = 0xFF000000u;
                switch (pat) {
                case PAT_CHECKERBOARD:
                    c = ((x ^ y) & 1) ? 0xFFFFFFFFu : 0xFF000000u;
                    break;
                case PAT_H_LINES:
                    c = (y & 1) ? 0xFFFFFFFFu : 0xFF000000u;
                    break;
                case PAT_V_LINES:
                    c = (x & 1) ? 0xFFFFFFFFu : 0xFF000000u;
                    break;
                case PAT_PIXEL_GRID: {
                    bool border = (x % 16 == 0) || (y % 16 == 0);
                    int tile = ((x / 16) + (y / 16)) & 1;
                    c = border ? 0xFF72C840u : (tile ? 0xFF203040u : 0xFF101820u);
                    break;
                }
                case PAT_GRADIENT: {
                    uint8_t v = (uint8_t)((x * 255) / (g_cur_w > 1 ? g_cur_w - 1 : 1));
                    int band = (y * 4) / g_cur_h;
                    if (band == 0) c = 0xFF000000u | (v << 16) | (v << 8) | v;
                    else if (band == 1) c = 0xFF000000u | (v << 16);
                    else if (band == 2) c = 0xFF000000u | (v << 8);
                    else c = 0xFF000000u | v;
                    break;
                }
                case PAT_SCROLL_H:
                    c = (x >= bar_x && x < bar_x + 24) ? 0xFFFFFFFFu : 0xFF182028u;
                    break;
                case PAT_SCROLL_V:
                    c = (y >= bar_y && y < bar_y + 12) ? 0xFF72C840u : 0xFF182028u;
                    break;
                case PAT_STATIC_OVERSCAN:
                default: {
                    bool edge = (x == 0 || y == 0 || x == g_cur_w - 1 || y == g_cur_h - 1);
                    bool box5 = (x == g_cur_w / 20 || x == g_cur_w - 1 - g_cur_w / 20 ||
                                 y == g_cur_h / 20 || y == g_cur_h - 1 - g_cur_h / 20);
                    bool cross = (abs(x - g_cur_w / 2) <= 1 && abs(y - g_cur_h / 2) <= 16) ||
                                 (abs(y - g_cur_h / 2) <= 1 && abs(x - g_cur_w / 2) <= 16);
                    c = edge ? 0xFFFF4040u : box5 ? 0xFF72C840u : cross ? 0xFFFFFFFFu : 0xFF121820u;
                    break;
                }
                }
                row[x] = c;
            }
        }
        g_pat_tex->UnlockRect(0);
    }

    struct { float x, y, u, v; } quad[4] = {
        { -1.0f,  1.0f, 0.0f, 0.0f },
        {  1.0f,  1.0f, 1.0f, 0.0f },
        { -1.0f, -1.0f, 0.0f, 1.0f },
        {  1.0f, -1.0f, 1.0f, 1.0f }
    };
    g_dev->Clear(0, NULL, D3DCLEAR_TARGET, 0xFF000000u, 1.0f, 0);
    g_dev->SetTexture(0, g_pat_tex);
    g_dev->SetVertexDeclaration(g_decl);
    g_dev->SetVertexShader(g_vs);
    g_dev->SetPixelShader(g_ps);
    g_dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0]));
    g_dev->SetTexture(0, NULL);
    g_dev->Present(NULL, NULL, NULL, NULL);
}

static void write_probe_report(const XVIDEO_MODE &xm, DWORD vflags, const struct mode_probe_result *res, int count) {
    FILE *f = fopen("game:\\crt-modes-probe.txt", "wb");
    if (!f) return;
    fprintf(f, "schema=1\nconsole_display=%ux%u\ninterlaced=%d\nhidef=%d\nwidescreen=%d\nrefresh_hz=%.3f\nvideo_flags=0x%08lX\n",
            (unsigned)xm.dwDisplayWidth, (unsigned)xm.dwDisplayHeight,
            xm.fIsInterlaced ? 1 : 0, xm.fIsHiDef ? 1 : 0, xm.fIsWideScreen ? 1 : 0,
            xm.RefreshRate, (unsigned long)vflags);
    fprintf(f, "t240_plugin_ver=0x%08X\nt240_plugin_mode=%u\n",
            (unsigned)g_probe_s240_beacon.plugin_ver, (unsigned)g_probe_s240_beacon.plugin_mode);
    fprintf(f, "mode,width,height,reset_ok,hr,present_p50_us,present_p95_us,present_p99_us,present_max_us\n");
    for (int i = 0; i < count; i++) {
        fprintf(f, "%s,%d,%d,%d,0x%08lX,%u,%u,%u,%u\n",
                res[i].name, res[i].width, res[i].height, res[i].reset_ok ? 1 : 0,
                (unsigned long)res[i].hr, res[i].p50_us, res[i].p95_us, res[i].p99_us, res[i].max_us);
    }
    fclose(f);
}

void __cdecl main() {
    QueryPerformanceFrequency(&g_qpc_freq);
    XVIDEO_MODE xm;
    ZeroMemory(&xm, sizeof(xm));
    XGetVideoMode(&xm);
    DWORD vflags = (DWORD)xm.VideoStandard;

    g_safe_w = xm.fIsHiDef && xm.dwDisplayWidth >= 1280 ? 1280 : 640;
    g_safe_h = xm.fIsHiDef && xm.dwDisplayHeight >= 720 ? 720 : 480;

    if (!init_d3d(g_safe_w, g_safe_h)) {
        OutputDebugStringA("[PROBE] Failed to create D3D9 device\n");
        XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP, 0);
        return;
    }

    /* Automated safe mode sweep: measure 90 VSync presents per passing mode, then restore safe mode. */
    struct mode_probe_result results[NUM_MODES];
    ZeroMemory(results, sizeof(results));
    for (int i = 0; i < NUM_MODES; i++) {
        results[i].name = g_modes[i].name;
        results[i].width = g_modes[i].width;
        results[i].height = g_modes[i].height;
        HRESULT hr = S_OK;
        results[i].reset_ok = reset_mode(g_modes[i].width, g_modes[i].height, g_modes[i].use_scaler, &hr);
        results[i].hr = hr;
        if (results[i].reset_ok) {
            uint32_t intervals[90];
            uint64_t prev = 0;
            int n = 0;
            for (int f = 0; f < 92; f++) {
                uint64_t t = now_us();
                draw_pattern(PAT_STATIC_OVERSCAN, f);
                if (prev > 0 && t > prev && n < 90) {
                    intervals[n++] = (uint32_t)(t - prev);
                }
                prev = t;
            }
            if (n > 0) {
                qsort(intervals, (size_t)n, sizeof(uint32_t), cmp_u32);
                results[i].p50_us = intervals[(n * 50 + 99) / 100 - 1];
                results[i].p95_us = intervals[(n * 95 + 99) / 100 - 1];
                results[i].p99_us = intervals[(n * 99 + 99) / 100 - 1];
                results[i].max_us = intervals[n - 1];
            }
        }
    }
    reset_mode(g_safe_w, g_safe_h, false, NULL);
    write_probe_report(xm, vflags, results, NUM_MODES);

    /* Interactive viewer: D-Pad L/R cycles test patterns, A cycles modes, B restores safe & exits. */
    int mode_idx = 0, pat_idx = 0, frame = 0;
    WORD prev_btns = 0;
    for (;;) {
        draw_pattern(pat_idx, frame++);
        XINPUT_STATE xi;
        ZeroMemory(&xi, sizeof(xi));
        if (XInputGetState(0, &xi) == ERROR_SUCCESS) {
            WORD pressed = xi.Gamepad.wButtons & (WORD)(~prev_btns);
            prev_btns = xi.Gamepad.wButtons;
            if (pressed & XINPUT_GAMEPAD_B) break;
            if (pressed & XINPUT_GAMEPAD_DPAD_RIGHT) pat_idx = (pat_idx + 1) % PAT_COUNT;
            if (pressed & XINPUT_GAMEPAD_DPAD_LEFT) pat_idx = (pat_idx + PAT_COUNT - 1) % PAT_COUNT;
            if (pressed & XINPUT_GAMEPAD_A) {
                mode_idx = (mode_idx + 1) % NUM_MODES;
                if (!reset_mode(g_modes[mode_idx].width, g_modes[mode_idx].height, g_modes[mode_idx].use_scaler, NULL)) {
                    reset_mode(g_safe_w, g_safe_h, false, NULL);
                    mode_idx = 0;
                }
            }
            if (pressed & XINPUT_GAMEPAD_Y) {
                /* Opt-in S240 beacon toggle only if external t240.xex is detected on 480i. */
                if (!xm.fIsHiDef && xm.fIsInterlaced && g_probe_s240_beacon.plugin_ver >= CRT_S240_MIN_PLUGIN_VER) {
                    bool want_240p = (g_probe_s240_beacon.cmd != CRT_S240_CMD_FORCE_240P);
                    g_probe_s240_beacon.is_menu = want_240p ? 0u : 1u;
                    g_probe_s240_beacon.width = (unsigned)g_cur_w;
                    g_probe_s240_beacon.height = (unsigned)g_cur_h;
                    g_probe_s240_beacon.in_game = want_240p ? 1u : 0u;
                    g_probe_s240_beacon.cmd = want_240p ? CRT_S240_CMD_FORCE_240P : CRT_S240_CMD_FORCE_480I;
                }
            }
        }
    }

    g_probe_s240_beacon.is_menu = 1u;
    g_probe_s240_beacon.in_game = 0u;
    g_probe_s240_beacon.cmd = CRT_S240_CMD_FORCE_480I;
    reset_mode(g_safe_w, g_safe_h, false, NULL);
    write_probe_report(xm, vflags, results, NUM_MODES);
    XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP, 0);
}
