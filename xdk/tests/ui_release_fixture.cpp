#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
typedef uint32_t DWORD;
static bool owned, idle;
static int fences, released, expected;
struct D3DTexture { void Release() { assert(owned && idle && fences == expected); released++; } };
struct D3DDevice {
    void BlockOnFence(DWORD f) { assert(owned && f); fences++; }
    void BlockUntilIdle() { assert(owned && fences == expected); idle = true; }
};
static D3DDevice device;
static D3DDevice *gfx_begin() { assert(!owned); owned = true; return &device; }
static void gfx_end() { assert(owned); owned = false; }
static void gfx_ui_release(D3DDevice*) {assert(owned && idle);}
static void gfx_stream_menu_reset(D3DDevice*) {assert(owned && idle);}
static D3DTexture *mouse_indicator;
static bool mouse_indicator_failed;
static void gfx_wait_fence(D3DDevice *d,DWORD f){d->BlockOnFence(f);}
/* ACTUAL_UI_RELEASE */
int main(void)
{
    D3DTexture objects[2];
    for (int pass=0; pass<50; pass++) {
        idle = owned = false; fences = released = 0;
        expected = pass % 3;
        for (int k=0;k<2;k++) {
            ui_tex[k] = k < expected ? &objects[k] : NULL;
            ui_fence[k] = k < expected ? (DWORD)(k+1) : 0;
        }
        ui_next = 1;
        gfx_release_ui_textures();
        assert(!owned && released == expected && ui_next == 0);
        for (int k=0;k<2;k++) assert(!ui_tex[k] && !ui_fence[k]);
        expected = fences = 0;
        gfx_release_ui_textures(); assert(!owned);
    }
    puts("PASS: actual UI texture release waits GPU fences/idle, clears slots and supports empty/partial/repeated cleanup.");
}
