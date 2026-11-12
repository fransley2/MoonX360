/* Included only by the experimental video module, after its FFmpeg helpers.
 * This diagnostic uses the existing benchmark entry, never the streaming path. */
#include "../ffmpeg_xbox/vmx_ab.h"
struct vmx_ab_native {
    FILE *file;
    volatile LONG cancel,done,rows;
    int result;
    char path[320];
};
static uint64_t vmx_ab_clock(void *)
{
    LARGE_INTEGER value;QueryPerformanceCounter(&value);return (uint64_t)value.QuadPart;
}
static int vmx_ab_cancel(void *context)
{
    return InterlockedCompareExchange(&((vmx_ab_native*)context)->cancel,0,0)!=0;
}
static int vmx_ab_emit(void *context,const char *line)
{
    vmx_ab_native *state=(vmx_ab_native*)context;
    InterlockedIncrement(&state->rows);
    return fputs(line,state->file)>=0;
}
static bool vmx_ab_capture_tile(uint8_t *tile,vmx_ab_native *state)
{
    FILE *report=state->file;
    size_t bytes=0;uint8_t *capture=(uint8_t*)storage_read(CAPTURE_FILE,&bytes);
    if(!capture || !xdk_capture_valid(capture,bytes)){free(capture);return false;}
    uint32_t w,h,count,hash=2166136261u;
    memcpy(&w,capture+4,4);memcpy(&h,capture+8,4);memcpy(&count,capture+12,4);
    for(size_t i=0;i<bytes;i++){hash^=capture[i];hash*=16777619u;}
    int saved=ff_xbox_vmx_flags;ff_xbox_vmx_flags=0;
    AVCodecContext *decoder=open_h264(w,h,1,false);
    AVFrame *frame=avcodec_alloc_frame();
    uint8_t *packet=(uint8_t*)_aligned_malloc(2*1024*1024+FF_INPUT_BUFFER_PADDING_SIZE,128);
    const uint8_t *p=capture+16;bool copied=false;
    if(decoder && frame && packet)for(uint32_t i=0;i<count && !copied;i++) {
        if(vmx_ab_cancel(state))break;
        uint32_t length;memcpy(&length,p,4);p+=4;
        memcpy(packet,p,length);p+=length;memset(packet+length,0,FF_INPUT_BUFFER_PADDING_SIZE);
        AVPacket pkt;av_init_packet(&pkt);pkt.data=packet;pkt.size=length;
        int got=0,ret=avcodec_decode_video2(decoder,frame,&got,&pkt);
        if(ret<0)break;
        if(got && !frame->decode_error_flags && decoder->pix_fmt==PIX_FMT_YUV420P &&
           decoder->width>0 && decoder->height>0 && frame->data[0] && frame->linesize[0]>=decoder->width) {
            int left=decoder->width>128?(decoder->width-128)/2:0;
            int top=decoder->height>48?(decoder->height-48)/2:0;
            for(int y=0;y<48;y++)for(int x=0;x<128;x++) {
                int sy=top+y<decoder->height?top+y:decoder->height-1;
                int sx=left+x<decoder->width?left+x:decoder->width-1;
                tile[y*128+x]=frame->data[0][sy*frame->linesize[0]+sx];
            }
            fprintf(report,"# capture=MLC1,bytes=%u,fnv1a=%08x,decoded_frame=%u,geometry=%dx%d,tile=center_128x48_Y\n",
                (unsigned)bytes,hash,i,decoder->width,decoder->height);
            copied=true;
        }
    }
    close_h264(&decoder);if(frame)avcodec_free_frame(&frame);
    _aligned_free(packet);free(capture);ff_xbox_vmx_flags=saved;
    return copied;
}
static DWORD WINAPI vmx_ab_worker(LPVOID argument)
{
    vmx_ab_native *state=(vmx_ab_native*)argument;
    __declspec(align(128)) uint8_t reference[128*48];
    vmx_ab_callbacks callbacks;memset(&callbacks,0,sizeof(callbacks));
    fprintf(state->file,"# environment=Xbox360_native;thread=HW0;profile=%s;streaming_dispatch=unchanged;loopfilter=C;no_GPU_timestamps\n",XDK_BUILD_PROFILE_LABEL);
    callbacks.context=state;callbacks.clock=vmx_ab_clock;callbacks.frequency=qpc_freq.QuadPart;
    callbacks.emit=vmx_ab_emit;callbacks.cancel=vmx_ab_cancel;callbacks.strict_destination_alignment=1;
    callbacks.reference=vmx_ab_capture_tile(reference,state)?reference:NULL;
    state->result=ff_xbox_vmx_ab_run(&callbacks);
    fprintf(state->file,"# run_complete=1,result=%d;cancelled_reports_are_not_complete_benchmarks\n",state->result);
    if(ferror(state->file) || fflush(state->file))state->result=-1;
    if(fclose(state->file))state->result=-1;
    state->file=NULL;InterlockedExchange(&state->done,1);return 0;
}
static void vmx_ab_screen(void)
{
    if(running || !storage_dir())return;
    vmx_ab_native state;memset(&state,0,sizeof(state));
    /* Avoid overwriting prior observations, even within the same app lifetime. */
    for(unsigned attempt=0;attempt<1000;attempt++) {
        snprintf(state.path,sizeof(state.path),"%s\\vmx-ab-%08x-%03u.csv",storage_dir(),GetTickCount(),attempt);
        FILE *existing=fopen(state.path,"rb");if(existing){fclose(existing);continue;}
        state.file=fopen(state.path,"wb");break;
    }
    if(!state.file){plat_log("VMX A/B: cannot create a separate report\n");return;}
    setvbuf(state.file,NULL,_IOFBF,65536);
    HANDLE thread=CreateThread(NULL,512*1024,vmx_ab_worker,&state,CREATE_SUSPENDED,NULL);
    if(!thread){fclose(state.file);return;}
    XSetThreadProcessor(thread,0);ResumeThread(thread);
    while(!InterlockedCompareExchange(&state.done,0,0)) {
        char message[192];snprintf(message,sizeof(message),"%s | report rows: %ld | B: cancel",
            state.rows<970?"Checking full buffers and guards":"Timing isolated routines",(long)InterlockedCompareExchange(&state.rows,0,0));
        ui_draw_busy("VMX128 A/B experiment",message,GetTickCount());ui_present();
        if((input_menu_pressed()&B_FLAG) || input_close_app_requested())InterlockedExchange(&state.cancel,1);
        Sleep(100);
    }
    WaitForSingleObject(thread,INFINITE);CloseHandle(thread);
    char message[400];snprintf(message,sizeof(message),"%s. %s\nNo candidate was installed for streaming.",
        state.result==0?"Integrity and benchmark completed":state.result==2?"Cancelled":state.result==1?"Mismatches: rejected entries were not timed":"Report or resource failure",state.path);
    ui_draw_message(state.result?ICON_ERROR:ICON_INFO,"VMX128 A/B experiment",message);ui_present();
    while(!(input_menu_pressed()&(A_FLAG|B_FLAG)) && !input_close_app_requested())Sleep(16);
}
