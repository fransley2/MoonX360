#include <assert.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef void *HANDLE;
typedef uint32_t DWORD;typedef void *LPVOID;
typedef long LONG;
static volatile LONG stream_error;
enum { VIDEO_STREAM_OK, VIDEO_STREAM_UNSUPPORTED_SIZE, VIDEO_STREAM_RESOURCE_ERROR };
static LONG InterlockedExchange(volatile LONG *p,LONG n) {LONG old=*p;*p=n;return old;}
#define WINAPI
#define CREATE_SUSPENDED 4
#define HW_THREAD_RENDER 5
static uint64_t streaming_start_us;
static bool fail_thread;
static DWORD render_main(LPVOID) {return 0;}
static void xdk_audit_thread(const char*) {}
#define INFINITE 0xffffffff
#define FRAME_SLOTS 3
static uint64_t streaming_end_us;
static uint64_t now_us() {return 1000;}
static bool running, renderer_joined, signaled, owned, idle;
static int waited_fences, expected_fences, released;
static HANDLE frame_event = (void *)1, render_thread;
static int mailbox_lock;
static int slot_pending, slot_rendering;
static int *codec_ctx, *dec_frame;
static unsigned char *pkt_buf;
struct staging { unsigned char *plane[3]; };
static staging slots[FRAME_SLOTS];
static unsigned long tex_fence[2];
struct D3DTexture {
    void Release() {
        if (!owned || !idle || !renderer_joined) {
            puts("GPU lifetime regression: texture released before CPU/GPU finished.");
            exit(7);
        }
        released++;
    }
};
static D3DTexture objects[2][3], *tex[2][3];
struct D3DDevice {
    void SetTexture(int, void *) { assert(owned); }
    void BlockOnFence(unsigned long) { assert(owned); waited_fences++; }
    void BlockUntilIdle() { assert(owned && waited_fences == expected_fences); idle = true; }
};
static D3DDevice device;
static D3DDevice *gfx_begin() { assert(!owned); owned = true; return &device; }
static void gfx_end() { assert(owned); owned = false; }
static void cached_video_cleanup(D3DDevice*) { assert(owned && renderer_joined); }
static void gfx_overlay_clear(D3DDevice *) { assert(owned && idle); }
static void plat_shutdown_stage(const char *) {}
static void close_h264(int **p) { assert(renderer_joined); free(*p); *p = NULL; }
static void avcodec_free_frame(int **p) { free(*p); *p = NULL; }
static void EnterCriticalSection(int *) {}
static void LeaveCriticalSection(int *) {}
static HANDLE CreateThread(void*,int,DWORD (*)(LPVOID),void*,int flags,void*) {
    assert(flags==CREATE_SUSPENDED);
    if(fail_thread) {renderer_joined=true;return NULL;}
    return (void*)2;
}
static void XSetThreadProcessor(HANDLE,int hw) {assert(hw==HW_THREAD_RENDER);}
static void ResumeThread(HANDLE) {}
static void SetEvent(HANDLE) { signaled = true; }
static void WaitForSingleObject(HANDLE, unsigned long) { assert(!running && signaled); renderer_joined = true; }
static void CloseHandle(HANDLE) { assert(renderer_joined); }

static void gfx_wait_fence(D3DDevice *d,DWORD f){d->BlockOnFence(f);}
/* ACTUAL_FUNCTIONS */

static void setup(bool partial)
{
    running = true; renderer_joined = signaled = owned = idle = false;
    waited_fences = released = 0;
    render_thread = (void *)2;
    codec_ctx = (int *)malloc(sizeof(int));
    dec_frame = (int *)malloc(sizeof(int));
    pkt_buf = (unsigned char *)_aligned_malloc(128, 128);
    slot_pending = 1; slot_rendering = 2;
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 3; k++)
            slots[i].plane[k] = (unsigned char *)_aligned_malloc(128, 128);
    for (int i = 0; i < 2; i++) {
        tex_fence[i] = partial ? 0 : (unsigned long)(i+1);
        for (int k = 0; k < 3; k++) tex[i][k] = partial && i == 1 ? NULL : &objects[i][k];
    }
    expected_fences = partial ? 0 : 2;
}
int main(void)
{
    for (int pass = 0; pass < 50; pass++) {
        bool partial = (pass & 1) != 0;
        setup(partial);render_thread=NULL;
        fail_thread=false;video_start();assert(running && render_thread && streaming_start_us==1000);
        video_stop();
        assert(render_thread == NULL && renderer_joined && !running);
        free_resources();
        assert(released == (partial ? 3 : 6));
        assert(!owned && !codec_ctx && !dec_frame && !pkt_buf);
        assert(slot_pending == -1 && slot_rendering == -1);
        for (int i = 0; i < 3; i++) for (int k = 0; k < 3; k++) assert(!slots[i].plane[k]);
        waited_fences = expected_fences = 0;
        video_stop(); free_resources(); // idempotent cleanup
        assert(released == (partial ? 3 : 6));
    }
    setup(false);render_thread=NULL;fail_thread=true;video_start();
    assert(!render_thread);video_stop();free_resources();assert(!owned && !codec_ctx);
    puts("PASS: renderer start failure cleanup; repeated start/stop/second sessions.");
    puts("PASS: 50 full/partial teardown cycles, GPU fences/idle before release, CPU join, repeated cleanup.");
}
