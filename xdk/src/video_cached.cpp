/* CPU-cached textures, one producer and one presenter. No per-frame device
 * ownership on the decoder, no waits on in-flight textures, no staging RAM. */
#include "xdk.h"
#include "../../src/video_gpu_policy.h"
#include <assert.h>

static D3DTexture *cached_textures[3][3];
static struct gpu_slot cached_slots[3];
static CRITICAL_SECTION cached_lock;
static bool cached_lock_ready;
static int cached_width[3],cached_height[3];
static uint32_t cached_serial;
static uint64_t cached_bytes;

void cached_video_cleanup(D3DDevice *d)
{
    int i,k;
    if (!cached_lock_ready) return;
    bool any=false;
    for (i=0;i<3;i++) for(k=0;k<3;k++) if(cached_textures[i][k]) any=true;
    if (!any) return;
    for(i=0;i<3;i++) assert(cached_slots[i].state!=GPU_WRITING);
    /* Caller has joined producer/render before final cleanup, or is the
     * render thread after producer requested a permanent session fallback. */
    for (k=0;k<3;k++) d->SetTexture(k,NULL);
    for (i=0;i<3;i++) if (cached_slots[i].fence) gfx_wait_fence(d,cached_slots[i].fence);
    d->BlockUntilIdle();
    for (i=0;i<3;i++) for (k=0;k<3;k++) {
        if (cached_textures[i][k]) cached_textures[i][k]->Release();
        cached_textures[i][k]=NULL;
    }
    memset(cached_slots,0,sizeof(cached_slots));cached_bytes=0;
}

bool cached_video_init(int width,int height)
{
    int i,k;
    bool ok=true;
    if (!cached_lock_ready) { InitializeCriticalSection(&cached_lock);cached_lock_ready=true; }
    cached_serial=0;cached_bytes=0;
    memset(cached_slots,0,sizeof(cached_slots));
    cached_width[0]=width;cached_height[0]=height;
    cached_width[1]=cached_width[2]=(width+1)/2;
    cached_height[1]=cached_height[2]=(height+1)/2;
    D3DDevice *d=gfx_begin();
    if (!gfx_cached_selftest(d)) ok=false;
    for (i=0;ok && i<3;i++) for (k=0;ok && k<3;k++) {
        D3DTexture *t=NULL;
        ok=SUCCEEDED(d->CreateTexture(cached_width[k],cached_height[k],1,
            D3DUSAGE_CPU_CACHED_MEMORY,D3DFMT_LIN_L8,0,&t,NULL));
        cached_textures[i][k]=t;
        if (!ok || !t) { ok=false;break; }
        if (!(t->Common & D3DCOMMON_CPU_CACHED_MEMORY)) { ok=false;break; }
        D3DLOCKED_RECT lr;ZeroMemory(&lr,sizeof(lr));
        if (FAILED(t->LockRect(0,&lr,NULL,D3DLOCK_NOOVERWRITE))) { ok=false;break; }
        ok=lr.pBits && lr.Pitch>=cached_width[k] &&
            (uint64_t)(unsigned)lr.Pitch*cached_height[k]<=0x7fffffffU;
        if (ok) cached_bytes+=(uint64_t)lr.Pitch*cached_height[k];
        if (FAILED(t->UnlockRect(0))) ok=false;
    }
    if (!ok) cached_video_cleanup(d);
    gfx_end();return ok;
}

static void cached_reap(void)
{
    for (int i=0;i<3;i++)
        if (cached_slots[i].state==GPU_IN_FLIGHT && cached_slots[i].fence &&
            !gfx_fence_pending(cached_slots[i].fence)) {
            cached_slots[i].state=GPU_FREE;cached_slots[i].fence=0;
        }
}

int cached_video_publish(const BYTE *const planes[3],const int pitches[3],struct gfx_copy_cost *cost)
{
    int slot,k;
    bool ok=true;
    EnterCriticalSection(&cached_lock);
    cached_reap();slot=gpu_slot_write(cached_slots);
    LeaveCriticalSection(&cached_lock);
    if (slot<0) return 0; /* presentation starvation: decode must continue */
    for (k=0;k<3 && ok;k++)
        ok=gfx_cached_upload(cached_textures[slot][k],planes[k],pitches[k],cached_width[k],cached_height[k],cost);
    EnterCriticalSection(&cached_lock);
    cached_slots[slot].serial=++cached_serial;
    cached_slots[slot].state=ok?GPU_READY:GPU_FREE;
    LeaveCriticalSection(&cached_lock);
    return ok?1:-1;
}

int cached_video_take(D3DTexture **planes,unsigned *discarded)
{
    EnterCriticalSection(&cached_lock);
    cached_reap();
    int slot=gpu_slot_latest(cached_slots,discarded);
    if (slot>=0) for (int k=0;k<3;k++) planes[k]=cached_textures[slot][k];
    LeaveCriticalSection(&cached_lock);
    return slot;
}

void cached_video_submitted(int slot,DWORD fence)
{
    EnterCriticalSection(&cached_lock);
    cached_slots[slot].fence=fence; /* still IN_FLIGHT; fence 0 is never reusable */
    LeaveCriticalSection(&cached_lock);
}
uint64_t cached_video_memory(void) { return cached_bytes; }
