/* CPU-cached textures, one producer and one presenter. No per-frame device
 * ownership on the decoder, no waits on in-flight textures, no staging RAM. */
#include "xdk.h"
#include "../../src/video_gpu_policy.h"
#include "../../src/performance.h"
#include <assert.h>
#ifndef OPTIMIZED_YUV_COPY
#define OPTIMIZED_YUV_COPY 0
#endif

static D3DTexture *cached_textures[3][3];
static struct gpu_slot cached_slots[3];
static CRITICAL_SECTION cached_lock;
static bool cached_lock_ready;
static int cached_width[3],cached_height[3];
static uint32_t cached_serial;
static uint64_t cached_bytes;
static uint64_t cached_ready_us[3];
static uint32_t cached_ready_ms[3];
static uint32_t cached_submitted_ms[3];
uint64_t cached_video_ready_time(int slot) {return slot>=0 && slot<3?cached_ready_us[slot]:0;}
uint32_t cached_video_frame_serial(int slot) {return slot>=0 && slot<3?cached_slots[slot].serial:0;}

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
    memset(cached_ready_us,0,sizeof(cached_ready_us));
    memset(cached_ready_ms,0,sizeof(cached_ready_ms));
    memset(cached_submitted_ms,0,sizeof(cached_submitted_ms));
}

bool cached_video_init(int width,int height)
{
    int i,k;
    bool ok=true;
    if (!cached_lock_ready) { InitializeCriticalSection(&cached_lock);cached_lock_ready=true; }
    cached_serial=0;cached_bytes=0;
    memset(cached_slots,0,sizeof(cached_slots));
    memset(cached_ready_us,0,sizeof(cached_ready_us));
    memset(cached_ready_ms,0,sizeof(cached_ready_ms));
    memset(cached_submitted_ms,0,sizeof(cached_submitted_ms));
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
        D3DSURFACE_DESC desc;
        t->GetLevelDesc(0,&desc);
        if (desc.Format!=D3DFMT_LIN_L8 || desc.Width!=(UINT)cached_width[k] ||
            desc.Height!=(UINT)cached_height[k]) { ok=false;break; }
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
            if(cached_submitted_ms[i]) {
                video_progress_gpu_wait(GetTickCount()-cached_submitted_ms[i]);
                cached_submitted_ms[i]=0;
            }
            video_progress_note(VP_FENCE_COMPLETED);
        }
}

int cached_video_publish(const BYTE *const planes[3],const int pitches[3],struct gfx_copy_cost *cost)
{
    int slot,k,free_slots=0;
    bool ok=true;
    EnterCriticalSection(&cached_lock);
    cached_reap();
    if(perf_enabled())for(int i=0;i<3;i++)if(cached_slots[i].state==GPU_FREE)free_slots++;
    slot=gpu_slot_write(cached_slots);
    LeaveCriticalSection(&cached_lock);
    if(perf_enabled())perf_record(PERF_CACHED_FREE_SLOTS,free_slots);
    if (slot<0) return 0; /* presentation starvation: decode must continue */
    video_progress_note(VP_UPLOAD_STARTED);
    uint64_t transferStart=perf_enabled()?perf_clock():0;
    for (k=0;k<3 && ok;k++) {
        uint64_t planeStart=transferStart?perf_clock():0;
        ok=OPTIMIZED_YUV_COPY ?
            gfx_cached_upload_prevalidated(cached_textures[slot][k],planes[k],pitches[k],cached_width[k],cached_height[k],cost) :
            gfx_cached_upload(cached_textures[slot][k],planes[k],pitches[k],cached_width[k],cached_height[k],cost);
        if(planeStart)perf_record(PERF_Y_TRANSFER+k,perf_clock()-planeStart);
    }
    if(transferStart)perf_record(PERF_YUV_TRANSFER,perf_clock()-transferStart);
    EnterCriticalSection(&cached_lock);
    assert(cached_slots[slot].state==GPU_WRITING);
    cached_slots[slot].serial=++cached_serial;
    cached_ready_us[slot]=perf_enabled()?perf_clock():0;
    cached_ready_ms[slot]=GetTickCount();
    cached_slots[slot].state=ok?GPU_READY:GPU_FREE;
    if(ok)video_progress_note(VP_UPLOADED);
    LeaveCriticalSection(&cached_lock);
    return ok?1:-1;
}

int cached_video_take(D3DTexture **planes,unsigned *discarded)
{
    EnterCriticalSection(&cached_lock);
    cached_reap();
    int slot=gpu_slot_latest(cached_slots,discarded);
    if(slot>=0)assert(cached_slots[slot].state==GPU_IN_FLIGHT);
    if(slot>=0)video_progress_queue_age(GetTickCount()-cached_ready_ms[slot]);
    if (slot>=0) for (int k=0;k<3;k++) planes[k]=cached_textures[slot][k];
    LeaveCriticalSection(&cached_lock);
    return slot;
}

void cached_video_submitted(int slot,DWORD fence)
{
    EnterCriticalSection(&cached_lock);
    assert(slot>=0 && slot<3 && cached_slots[slot].state==GPU_IN_FLIGHT);
    cached_slots[slot].fence=fence; /* still IN_FLIGHT; fence 0 is never reusable */
    cached_submitted_ms[slot]=GetTickCount();
    LeaveCriticalSection(&cached_lock);
}
void cached_video_snapshot(unsigned *ready,unsigned *in_flight)
{
    *ready=*in_flight=0;
    if(!cached_lock_ready)return;
    EnterCriticalSection(&cached_lock);
    for(int i=0;i<3;i++) {
        if(cached_slots[i].state==GPU_READY){
            (*ready)++;
            video_progress_queue_age(GetTickCount()-cached_ready_ms[i]);
        }
        if(cached_slots[i].state==GPU_IN_FLIGHT){
            (*in_flight)++;
            if(cached_submitted_ms[i])video_progress_gpu_wait(GetTickCount()-cached_submitted_ms[i]);
        }
    }
    LeaveCriticalSection(&cached_lock);
}
unsigned cached_video_prune_ready(void)
{
    if(!cached_lock_ready)return 0;
    EnterCriticalSection(&cached_lock);
    int newest=-1;unsigned dropped=0;
    for(int i=0;i<3;i++)if(cached_slots[i].state==GPU_READY &&
        (newest<0 || (int32_t)(cached_slots[i].serial-cached_slots[newest].serial)>0))newest=i;
    for(int i=0;i<3;i++)if(cached_slots[i].state==GPU_READY && i!=newest) {
        cached_slots[i].state=GPU_FREE;dropped++;
    }
    LeaveCriticalSection(&cached_lock);
    return dropped;
}
uint64_t cached_video_memory(void) { return cached_bytes; }
