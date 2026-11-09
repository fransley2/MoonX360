/* CPU-cached textures, one producer and one presenter. No per-frame device
 * ownership on the decoder, no waits on in-flight textures, no staging RAM. */
#include "xdk.h"
#include "../../src/video_gpu_policy.h"
#include "../../src/performance.h"
#include "../../src/video_smoothing.h"
#include <assert.h>
#ifndef OPTIMIZED_YUV_COPY
#define OPTIMIZED_YUV_COPY 0
#endif

static D3DTexture *cached_textures[4][3];
static struct gpu_slot cached_slots[4];
static CRITICAL_SECTION cached_lock;
static bool cached_lock_ready;
static int cached_width[3],cached_height[3];
static uint32_t cached_serial;
static uint64_t cached_bytes;
static uint64_t cached_ready_us[4];
static uint32_t cached_ready_ms[4];
static uint32_t cached_submitted_ms[4];
static uint64_t cached_picture[4];
static int cached_count=3,cached_history=-1,cached_borrow=-1;
static uint32_t cached_epoch,cached_frame_epoch[4];
static bool cached_history_valid,cached_drew_blend;
static uint32_t cached_history_present_ms;
static smoothing_signature cached_signature[4];
static smoothing_transition cached_transition;
static smoothing_diagnostics cached_smoothing;
void cached_video_reset_smoothing_diagnostics(void)
{
    if(cached_lock_ready)EnterCriticalSection(&cached_lock);
    memset(&cached_smoothing,0,sizeof(cached_smoothing));
    if(cached_lock_ready)LeaveCriticalSection(&cached_lock);
}
uint64_t cached_video_picture(int slot) {return slot>=0 && slot<cached_count?cached_picture[slot]:0;}
uint64_t cached_video_ready_time(int slot) {return slot>=0 && slot<cached_count?cached_ready_us[slot]:0;}
uint32_t cached_video_frame_serial(int slot) {return slot>=0 && slot<cached_count?cached_slots[slot].serial:0;}

void cached_video_cleanup(D3DDevice *d)
{
    int i,k;
    if (!cached_lock_ready) return;
    bool any=false;
    for (i=0;i<4;i++) for(k=0;k<3;k++) if(cached_textures[i][k]) any=true;
    if (!any) return;
    for(i=0;i<4;i++) assert(cached_slots[i].state!=GPU_WRITING);
    /* Caller has joined producer/render before final cleanup, or is the
     * render thread after producer requested a permanent session fallback. */
    for (k=0;k<6;k++) d->SetTexture(k,NULL);
    for (i=0;i<4;i++) if (cached_slots[i].fence) gfx_wait_fence(d,cached_slots[i].fence);
    d->BlockUntilIdle();
    for (i=0;i<4;i++) for (k=0;k<3;k++) {
        if (cached_textures[i][k]) cached_textures[i][k]->Release();
        cached_textures[i][k]=NULL;
    }
    EnterCriticalSection(&cached_lock);
    memset(cached_slots,0,sizeof(cached_slots));cached_bytes=0;cached_count=3;cached_history=cached_borrow=-1;cached_history_valid=false;cached_smoothing.enabled=false;
    memset(cached_ready_us,0,sizeof(cached_ready_us));
    memset(cached_ready_ms,0,sizeof(cached_ready_ms));
    memset(cached_submitted_ms,0,sizeof(cached_submitted_ms));
    memset(&cached_transition,0,sizeof(cached_transition));
    LeaveCriticalSection(&cached_lock);
}

bool cached_video_init(int width,int height)
{
    int i,k;
    bool ok=true;
    if (!cached_lock_ready) { InitializeCriticalSection(&cached_lock);cached_lock_ready=true; }
    cached_serial=0;cached_bytes=0;cached_count=3;cached_history=cached_borrow=-1;
    cached_epoch=0;cached_history_valid=cached_drew_blend=false;
    cached_smoothing.enabled=false;memset(&cached_transition,0,sizeof(cached_transition));
    memset(cached_signature,0,sizeof(cached_signature));memset(cached_frame_epoch,0,sizeof(cached_frame_epoch));
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

bool cached_video_enable_history(void)
{
    if(!cached_lock_ready || cached_count!=3)return cached_count==4;
    uint64_t rows=0,allocation=0;bool ok=true;D3DDevice *d=gfx_begin();
    for(int k=0;k<3 && ok;k++) {
        D3DTexture *t=NULL;
        ok=SUCCEEDED(d->CreateTexture(cached_width[k],cached_height[k],1,D3DUSAGE_CPU_CACHED_MEMORY,D3DFMT_LIN_L8,0,&t,NULL));
        cached_textures[3][k]=t;
        if(!ok || !t || !(t->Common&D3DCOMMON_CPU_CACHED_MEMORY)){ok=false;break;}
        D3DSURFACE_DESC desc;t->GetLevelDesc(0,&desc);
        if(desc.Format!=D3DFMT_LIN_L8 || desc.Width!=(UINT)cached_width[k] || desc.Height!=(UINT)cached_height[k]){ok=false;break;}
        D3DLOCKED_RECT lr;ZeroMemory(&lr,sizeof(lr));
        if(FAILED(t->LockRect(0,&lr,NULL,D3DLOCK_NOOVERWRITE))){ok=false;break;}
        ok=lr.pBits && lr.Pitch>=cached_width[k] && (uint64_t)(unsigned)lr.Pitch*cached_height[k]<=0x7fffffffU;
        if(ok)rows+=(uint64_t)lr.Pitch*cached_height[k];
        if(FAILED(t->UnlockRect(0)))ok=false;
        uint64_t size=gfx_texture_allocation_bytes(t);if(!size)ok=false;allocation+=size;
    }
    if(ok){cached_count=4;cached_bytes+=rows;cached_smoothing.enabled=true;
        cached_smoothing.additional_gpu_history_bytes=allocation;cached_smoothing.additional_history_row_bytes=rows;}
    else {for(int k=0;k<3;k++){if(cached_textures[3][k])cached_textures[3][k]->Release();cached_textures[3][k]=NULL;}
        cached_smoothing.history_allocation_failed++;}
    gfx_end();return ok;
}

static void cached_reap(void)
{
    for(int i=0;i<cached_count;i++)
        if((cached_slots[i].state==GPU_IN_FLIGHT || cached_slots[i].state==GPU_HISTORY) &&
           cached_slots[i].fence && !gfx_fence_pending(cached_slots[i].fence)) {
            if(cached_submitted_ms[i]) {
                video_progress_gpu_wait(GetTickCount()-cached_submitted_ms[i]);cached_submitted_ms[i]=0;
                video_progress_note(VP_FENCE_COMPLETED);
            }
            if(cached_slots[i].state==GPU_IN_FLIGHT){cached_slots[i].state=GPU_FREE;cached_slots[i].fence=0;}
            // Retained history keeps its completed nonzero fence. Zero always
            // means unsubmitted/unknown GPU completion, never a reusable slot.
        }
}

int cached_video_publish_picture(const BYTE *const planes[3],const int pitches[3],struct gfx_copy_cost *cost,uint64_t picture)
{
    int slot,k,free_slots=0;
    bool ok=true;
    EnterCriticalSection(&cached_lock);
    cached_reap();
    if(perf_enabled())for(int i=0;i<cached_count;i++)if(cached_slots[i].state==GPU_FREE)free_slots++;
    slot=gpu_slot_write_count(cached_slots,cached_count);
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
    if(ok && cached_count==4)smoothing_signature_capture(&cached_signature[slot],planes,pitches,cached_width[0],cached_height[0]);
    EnterCriticalSection(&cached_lock);
    assert(cached_slots[slot].state==GPU_WRITING);
    cached_frame_epoch[slot]=cached_epoch;
    cached_slots[slot].serial=++cached_serial;
    cached_picture[slot]=picture;
    cached_ready_us[slot]=perf_enabled()?perf_clock():0;
    cached_ready_ms[slot]=GetTickCount();
    cached_slots[slot].state=ok?GPU_READY:GPU_FREE;
    if(ok)video_progress_note(VP_UPLOADED);
    LeaveCriticalSection(&cached_lock);
    return ok?1:-1;
}

int cached_video_publish(const BYTE *const planes[3],const int pitches[3],struct gfx_copy_cost *cost)
{
    return cached_video_publish_picture(planes,pitches,cost,0);
}

bool cached_video_has_ready(void)
{
    if(!cached_lock_ready)return false;
    bool ready=false;
    EnterCriticalSection(&cached_lock);
    for(int i=0;i<cached_count;i++)if(cached_slots[i].state==GPU_READY){ready=true;break;}
    LeaveCriticalSection(&cached_lock);
    return ready;
}
int cached_video_take(D3DTexture **planes,unsigned *discarded)
{
    EnterCriticalSection(&cached_lock);
    cached_reap();
    int slot=gpu_slot_latest_count(cached_slots,cached_count,discarded);
    if(slot>=0)assert(cached_slots[slot].state==GPU_IN_FLIGHT);
    if(slot>=0)video_progress_queue_age(GetTickCount()-cached_ready_ms[slot]);
    if (slot>=0) for (int k=0;k<3;k++) planes[k]=cached_textures[slot][k];
    LeaveCriticalSection(&cached_lock);
    return slot;
}

void cached_video_submitted(int slot,DWORD fence)
{
    EnterCriticalSection(&cached_lock);
    assert(slot>=0 && slot<cached_count && cached_slots[slot].state==GPU_IN_FLIGHT);
    if(cached_count==4) {
        if(cached_history>=0) {
            int old=cached_history;
            assert(old!=slot && cached_slots[old].state==GPU_HISTORY);
            cached_slots[old].state=GPU_IN_FLIGHT;
            if(cached_borrow==old){cached_slots[old].fence=fence;cached_submitted_ms[old]=GetTickCount();} // fence covers BOTH sampled pictures
        }
        cached_history=slot;cached_history_valid=false;cached_slots[slot].state=GPU_HISTORY;
        cached_smoothing.history_slot_pinned++;
    }
    cached_slots[slot].fence=fence; /* fence 0 is never reusable */
    cached_submitted_ms[slot]=GetTickCount();
    LeaveCriticalSection(&cached_lock);
}
void cached_video_snapshot(unsigned *ready,unsigned *in_flight)
{
    *ready=*in_flight=0;
    if(!cached_lock_ready)return;
    EnterCriticalSection(&cached_lock);
    for(int i=0;i<cached_count;i++) {
        if(cached_slots[i].state==GPU_READY){
            (*ready)++;
            video_progress_queue_age(GetTickCount()-cached_ready_ms[i]);
        }
        if(cached_slots[i].state==GPU_IN_FLIGHT || (cached_slots[i].state==GPU_HISTORY && (!cached_slots[i].fence || gfx_fence_pending(cached_slots[i].fence)))){
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
    for(int i=0;i<cached_count;i++)if(cached_slots[i].state==GPU_READY &&
        (newest<0 || (int32_t)(cached_slots[i].serial-cached_slots[newest].serial)>0))newest=i;
    for(int i=0;i<cached_count;i++)if(cached_slots[i].state==GPU_READY && i!=newest) {
        cached_slots[i].state=GPU_FREE;dropped++;
    }
    LeaveCriticalSection(&cached_lock);
    return dropped;
}
uint64_t cached_video_memory(void) { return cached_bytes; }

/* Called only by the renderer with a newly selected IN_FLIGHT current picture. */
int cached_video_prepare_smoothing(int current,int strength,D3DTexture **previous,float *alpha)
{
    *alpha=0;EnterCriticalSection(&cached_lock);cached_borrow=-1;cached_drew_blend=false;
    if(cached_count!=4){LeaveCriticalSection(&cached_lock);return -1;}
    assert(current>=0 && current<cached_count && cached_slots[current].state==GPU_IN_FLIGHT);
    bool valid=cached_history>=0 && cached_history!=current && cached_history_valid &&
        cached_slots[cached_history].state==GPU_HISTORY &&
        ((uint32_t)cached_picture[current]>>1)!=((uint32_t)cached_picture[cached_history]>>1) && (uint32_t)(GetTickCount()-cached_history_present_ms)<=150 && cached_frame_epoch[current]==cached_epoch &&
        cached_frame_epoch[cached_history]==cached_epoch && cached_signature[current].valid && cached_signature[cached_history].valid;
    bool motion=valid && smoothing_motion_safe(&cached_signature[current],&cached_signature[cached_history]);
    int reason=SMOOTH_NONE;bool started=false;
    *alpha=smoothing_select(&cached_transition,(cached_picture[current]&1)!=0,valid,motion,strength,&reason,&started);
    if(started)cached_smoothing.idr_transition_started++;
    if(reason==SMOOTH_NO_HISTORY){cached_smoothing.history_slot_bypassed++;cached_smoothing.transition_bypassed_invalid_history++;}
    if(reason==SMOOTH_MOTION){cached_smoothing.history_slot_bypassed++;cached_smoothing.transition_bypassed_motion++;}
    int old=(reason==SMOOTH_NONE || reason==SMOOTH_DIRECT)?-2:-1;
    if(*alpha>0){old=cached_borrow=cached_history;for(int k=0;k<3;k++)previous[k]=cached_textures[old][k];}
    LeaveCriticalSection(&cached_lock);return old;
}
void cached_video_smoothing_drawn(void)
{
    EnterCriticalSection(&cached_lock);assert(cached_count==4 && cached_borrow>=0);cached_drew_blend=true;LeaveCriticalSection(&cached_lock);
}
void cached_video_presented(int current,bool shown)
{
    if(!cached_lock_ready)return;
    EnterCriticalSection(&cached_lock);
    if(cached_count==4) {
        assert(cached_history==current && cached_slots[current].state==GPU_HISTORY);
        if(shown)cached_history_present_ms=GetTickCount();
        cached_history_valid=shown && cached_frame_epoch[current]==cached_epoch && cached_signature[current].valid;
        if(shown && cached_drew_blend){cached_smoothing.actual_blended_present_calls++;
            if(cached_picture[current]&1)cached_smoothing.idr_transition_applied++;}
        if(!shown){cached_history_valid=false;memset(&cached_transition,0,sizeof(cached_transition));}
        cached_borrow=-1;cached_drew_blend=false;
    }
    LeaveCriticalSection(&cached_lock);
}
/* Decoder invalidation never releases a GPU-borrowed pin. Only the render owner
 * swaps it at submission; an already recorded draw is never retroactively edited. */
void cached_video_invalidate_history(void)
{
    if(!cached_lock_ready)return;
    EnterCriticalSection(&cached_lock);cached_epoch++;cached_history_valid=false;
    memset(&cached_transition,0,sizeof(cached_transition));LeaveCriticalSection(&cached_lock);
}
void cached_video_smoothing_snapshot(struct smoothing_diagnostics *out)
{
    memset(out,0,sizeof(*out));if(!cached_lock_ready)return;
    EnterCriticalSection(&cached_lock);*out=cached_smoothing;
    out->previous_frame_available=cached_history_valid && cached_history>=0 && cached_frame_epoch[cached_history]==cached_epoch &&
        (uint32_t)(GetTickCount()-cached_history_present_ms)<=150;
    out->history_currently_pinned=cached_history>=0;
    LeaveCriticalSection(&cached_lock);
}
