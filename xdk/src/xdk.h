/*
 * Internal interfaces of the Xbox 360 SDK build (xdk/src/*.cpp).
 */
#pragma once

#include <xtl.h>
#include <stdint.h>

/*
 * Hardware thread layout while streaming (three cores, two threads each):
 *   0  main, Moonlight dispatch/network; Balanced: Opus, XAudio2 and input
 *   1,2,4[,3] FFmpeg slice workers after patch_worker_map.py (3 or 4)
 *   5  video presentation; Legacy additionally puts audio/input here
 * Physical pairs are HW0/1, HW2/3, HW4/5 (SDK XSetThreadProcessor).
 * SDK CreateThread inherits the creator processor and starts at NORMAL.
 * Network/VideoDec inherit the connection task's HW0; only VideoRecv is
 * raised to HIGHEST by win_net.c. AudioDec moves to xdk_aux_processor().
 * Legacy changes auxiliary placement only; both modes use the patched workers.
 * xdk_audit_thread records H/priority on actual console entry; do not guess H.
 */
#define HW_THREAD_RENDER 5
/* Normal-priority auxiliary tasks share HW0; render stays alone on HW5. */
int xdk_aux_processor(void);
void xdk_set_thread_layout(int balanced);
#define HW_THREAD_ART    2   /* box art decoding; only runs outside a stream */

/* UI coordinates stay logical. Runtime display/backbuffer sizes are separate;
 * GFX_* are compatibility aliases for the existing UI/fullscreen quad callers. */
#define UI_LOGICAL_WIDTH  1280
#define UI_LOGICAL_HEIGHT 720
#define GFX_WIDTH UI_LOGICAL_WIDTH
#define GFX_HEIGHT UI_LOGICAL_HEIGHT

/* Decoder set-up and the VMX self-test; once at startup, after the network
 * (the self-test reports to the debug log). */
void video_xdk_init(void);
bool video_xdk_is_streaming(void);

bool gfx_init(void);

/* Exclusive use of the D3D device from the calling thread (UI or video). */
D3DDevice *gfx_begin(void);
void gfx_end(void);

/* Draws a textured rectangle (logical UI coordinates) with one of the pixel shaders.
 * The caller sets the textures: RGB uses sampler 0, YUV uses 0 (Y), 1 (U), 2 (V). */
enum gfx_shader { GFX_SHADER_RGB, GFX_SHADER_YUV_FAST, GFX_SHADER_YUV_QUALITY, GFX_SHADER_RGBA };
#define GFX_SHADER_YUV GFX_SHADER_YUV_FAST
void gfx_draw_rect(D3DDevice *dev, int shader, float x0, float y0, float x1, float y1);
/* Straight-alpha HUD draw restores all blend states; white-mask conversion is local to HUDs. */
void gfx_draw_hud_rect(D3DDevice *dev,float x0,float y0,float x1,float y1);
void gfx_hud_pixels(uint32_t *pixels,int width,int height);

/* Copies to write-combined texture memory; false if locking/layout is invalid. */
bool gfx_upload(D3DTexture *tex, const void *src, int src_pitch, int row_bytes, int rows);
/* Optional small translucent telemetry texture. Caller owns gfx_begin/end. */
bool gfx_overlay_set(const char *text);
void gfx_overlay_draw(D3DDevice *d);
void gfx_mouse_draw(D3DDevice *d);
void gfx_stream_menu_draw(D3DDevice *d,unsigned decoded,unsigned shown,unsigned dropped,unsigned decode_us,bool detailed);
void gfx_stream_menu_submitted(DWORD fence);
void gfx_stream_menu_reset(D3DDevice *d);
void gfx_overlay_mark_used(DWORD fence);
void gfx_overlay_clear(D3DDevice *d);
void gfx_release_ui_textures(void);
bool gfx_ui_draw(D3DDevice *d);
void gfx_ui_submitted(DWORD fence);
void gfx_ui_release(D3DDevice *d);
void gfx_system_ui_init(void);
bool gfx_system_ui_active(void);
/* Nonblocking Guide heartbeat; called by the stream's own render thread. */
void gfx_present_system_ui(void);
void gfx_wait_fence(D3DDevice *d,DWORD fence);
bool gfx_ui_ready(void);

/* CPU-cached texture operations never acquire the device from the decoder.
 * XDK LockRect/UnlockRect are valid off-owner on unbound, fenced resources. */
struct gfx_copy_cost { DWORD lock_us, copy_us; };
uint64_t gfx_now_us(void);
void gfx_backbuffer_size(int *w, int *h);
void gfx_display_size(int *w, int *h);
float gfx_display_hz(void);
bool gfx_display_interlaced(void);
bool gfx_quality_available(void);
bool gfx_fence_pending(DWORD fence);
bool gfx_cached_upload(D3DTexture *texture, const BYTE *src, int src_pitch,
                       int width, int height, struct gfx_copy_cost *cost);
bool gfx_cached_selftest(D3DDevice *d);
bool cached_video_init(int width, int height);
int cached_video_publish(const BYTE *const planes[3], const int pitches[3], struct gfx_copy_cost *cost);
int cached_video_take(D3DTexture **planes, unsigned *discarded);
void cached_video_submitted(int slot, DWORD fence);
void cached_video_cleanup(D3DDevice *d);
uint64_t cached_video_memory(void);

bool gfx_upload_timed(D3DTexture *tex,const void *src,int src_pitch,int row_bytes,int rows,struct gfx_copy_cost *cost);

extern "C" void xdk_audit_thread(const char *name);

bool gfx_launch_pending(void);
void gfx_launch_draw(D3DDevice *d,bool usable);

void gfx_launch_idle(D3DDevice *d);
