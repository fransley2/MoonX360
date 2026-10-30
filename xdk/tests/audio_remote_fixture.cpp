#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <Limelight.h>
#include "../../../src/audio_layout.h"
#include "../../../src/performance.h"
typedef unsigned char BYTE;typedef unsigned short WORD;typedef unsigned DWORD;
typedef long LONG;
static LONG InterlockedExchange(volatile LONG *p,LONG v){LONG old=*p;*p=v;return old;}
static LONG InterlockedCompareExchange(volatile LONG *p,LONG v,LONG old){LONG n=*p;if(n==old)*p=v;return n;}
typedef int HRESULT;typedef unsigned XAUDIOSPEAKERCONFIG;
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
#define ZeroMemory(p,n) memset(p,0,n)
#define XboxThread0 0
#define XboxThread5 5
#define WAVE_FORMAT_PCM 1
#define WAVE_FORMAT_EXTENSIBLE 0xfffe
#define SPEAKER_5POINT1 0x3f
#define KSDATAFORMAT_SUBTYPE_PCM 1
#define XAUDIOSPEAKERCONFIG_ANALOG_MASK 0xffff
#define XAUDIOSPEAKERCONFIG_ANALOG_SURROUND 1
#define XAUDIOSPEAKERCONFIG_DIGITAL_DOLBYDIGITAL 0x10000
#define XAUDIOSPEAKERCONFIG_DIGITAL_WMAPRO 0x20000
struct WAVEFORMATEX {WORD wFormatTag,nChannels;DWORD nSamplesPerSec,nAvgBytesPerSec;WORD nBlockAlign,wBitsPerSample,cbSize;};
struct WAVEFORMATEXTENSIBLE {WAVEFORMATEX Format;union {WORD wValidBitsPerSample;} Samples;DWORD dwChannelMask;int SubFormat;};
struct XAUDIO2_VOICE_DETAILS {int InputChannels;};
struct XAUDIO2_VOICE_STATE {unsigned BuffersQueued;};
struct XAUDIO2_BUFFER {unsigned AudioBytes;const BYTE *pAudioData;};
struct XAUDIO2_PERFORMANCE_DATA {unsigned GlitchesSinceEngineStarted;};
static unsigned speaker_config=1,queued,last_bytes,submissions,source_channels,decoded_channels;
static bool fail_query,fail_six,fail_all_sources,fail_engine,fail_master,fail_start,fail_decoder;
static int master_channels=6,decoded_samples=960,decode_count,destroyed,engine_releases;
static unsigned char last_mapping[8];
static HRESULT XAudioGetSpeakerConfig(XAUDIOSPEAKERCONFIG *out){*out=speaker_config;return fail_query?-1:0;}
struct IXAudio2MasteringVoice {
    void GetVoiceDetails(XAUDIO2_VOICE_DETAILS *out){out->InputChannels=master_channels;}
    void DestroyVoice(){destroyed++;}
};
struct IXAudio2SourceVoice {
    int SetVolume(float value){assert(value>=0 && value<=1);return 0;}
    int Start(int){return fail_start?-1:0;}void Stop(int){}void DestroyVoice(){destroyed++;}
    void GetState(XAUDIO2_VOICE_STATE *out){out->BuffersQueued=queued;}
    int SubmitSourceBuffer(XAUDIO2_BUFFER *buf){last_bytes=buf->AudioBytes;submissions++;assert(last_bytes<=11520);return 0;}
};
static IXAudio2SourceVoice fake_voice;static IXAudio2MasteringVoice fake_master;
struct IXAudio2 {
    void GetPerformanceData(XAUDIO2_PERFORMANCE_DATA *data){data->GlitchesSinceEngineStarted=0;}
    int CreateMasteringVoice(IXAudio2MasteringVoice **out){if(fail_master)return -1;*out=&fake_master;return 0;}
    int CreateSourceVoice(IXAudio2SourceVoice **out,WAVEFORMATEX *f){
        if(fail_all_sources || (fail_six && f->nChannels==6))return -1;
        assert(f->wBitsPerSample==16 && f->nSamplesPerSec==48000 && f->nBlockAlign==f->nChannels*2);
        assert(f->nAvgBytesPerSec==48000*f->nBlockAlign);
        if(f->nChannels==6){WAVEFORMATEXTENSIBLE *s=(WAVEFORMATEXTENSIBLE*)f;assert(f->wFormatTag==WAVE_FORMAT_EXTENSIBLE && s->dwChannelMask==SPEAKER_5POINT1 && s->Samples.wValidBitsPerSample==16);}
        source_channels=f->nChannels;*out=&fake_voice;return 0;
    }
    void Release(){engine_releases++;}
};
static IXAudio2 fake_engine;
static int XAudio2Create(IXAudio2 **out,int,int){if(fail_engine)return -1;*out=&fake_engine;return 0;}
struct OpusMSDecoder{};static OpusMSDecoder fake_decoder;
static OpusMSDecoder *opus_multistream_decoder_create(int rate,int channels,int,int,const unsigned char *map,int *error){assert(rate==48000);decoded_channels=channels;memcpy(last_mapping,map,channels);*error=0;return fail_decoder?NULL:&fake_decoder;}
static void opus_multistream_decoder_destroy(OpusMSDecoder*){destroyed++;}
static int opus_multistream_decode(OpusMSDecoder*,const unsigned char*,int,int16_t *out,int max_samples,int){assert(max_samples==960);decode_count++;for(int i=0;i<max_samples*(int)decoded_channels;i++)out[i]=(int16_t)i;return decoded_samples;}
static int xdk_aux_processor(){return 0;}static void *GetCurrentThread(){return NULL;}
static void XSetThreadProcessor(void*,int){}static void xdk_audit_thread(const char*){}
static void plat_shutdown_stage(const char*){}static void plat_log(const char*){}
void LiInitializeAudioCallbacks(PAUDIO_RENDERER_CALLBACKS cb){memset(cb,0,sizeof(*cb));}
/* ACTUAL_AUDIO */
struct connect_job {int ret;};
static bool ui_suspended,connection_terminated;static int termination_error,retries,retry_result;
static char stage_text[128];
#define ICON_INFO 1
static void message(int,const char*,const char *text){assert(strstr(text,"stereo"));}
static void *connect_task(void*){return NULL;}
static void draw_connecting(void*,uint32_t){}
static void run_task(void *(*)(void*),connect_job *cj,void(*)(void*,uint32_t),char*){retries++;cj->ret=retry_result;}
static void run_retry(int result,int config){
    struct {STREAM_CONFIGURATION sc;} lj;lj.sc.audioConfiguration=config;connect_job cj={result};char title[16]="Host";
    ui_suspended=connection_terminated=true;termination_error=99;retries=0;
    /* ACTUAL_RETRY */
    if(result && config==AUDIO_CONFIGURATION_51_SURROUND)assert(retries==1 && !ui_suspended && !connection_terminated && !termination_error && lj.sc.audioConfiguration==AUDIO_CONFIGURATION_STEREO);
    else assert(!retries);
}
int main(){
    run_retry(-1,AUDIO_CONFIGURATION_51_SURROUND);retry_result=-1;run_retry(-1,AUDIO_CONFIGURATION_51_SURROUND);
    run_retry(-1,AUDIO_CONFIGURATION_STEREO);run_retry(0,AUDIO_CONFIGURATION_51_SURROUND);
    assert(audio_select_configuration(false)==AUDIO_CONFIGURATION_STEREO && !audio_fallback_reason());
    speaker_config=3;assert(audio_select_configuration(true)==AUDIO_CONFIGURATION_STEREO && audio_fallback_reason());
    fail_query=true;assert(audio_select_configuration(true)==AUDIO_CONFIGURATION_STEREO);fail_query=false;
    speaker_config=0x10003;assert(audio_select_configuration(true)==AUDIO_CONFIGURATION_51_SURROUND);
    OPUS_MULTISTREAM_CONFIGURATION o={};o.sampleRate=48000;o.channelCount=6;o.samplesPerFrame=960;o.streams=4;o.coupledStreams=2;
    unsigned char mapping[]={0,4,1,5,2,3};memcpy(o.mapping,mapping,6);
    assert(audio_init(AUDIO_CONFIGURATION_51_SURROUND,&o,NULL,0)==0 && source_channels==6 && !memcmp(last_mapping,mapping,6));
    assert(sizeof(pcm[0])==11520);audio_decode_and_play(NULL,0);assert(last_bytes==11520 && submissions==1);
    queued=8;audio_decode_and_play(NULL,0);assert(submissions==1 && decode_count==2);queued=0;
    audio_cleanup();assert(!voice&&!master&&!xaudio&&!decoder);
    fail_six=true;assert(audio_init(AUDIO_CONFIGURATION_51_SURROUND,&o,NULL,0)==0 && output_channels==2 && audio_fallback_reason());
    audio_decode_and_play(NULL,0);assert(last_bytes==3840);audio_cleanup();fail_six=false;
    master_channels=2;assert(audio_init(AUDIO_CONFIGURATION_51_SURROUND,&o,NULL,0)==0 && output_channels==2);audio_cleanup();master_channels=6;
    fail_all_sources=true;assert(audio_init(AUDIO_CONFIGURATION_51_SURROUND,&o,NULL,0)<0 && !decoder&&!xaudio&&!voice);fail_all_sources=false;
    fail_engine=true;assert(audio_init(AUDIO_CONFIGURATION_51_SURROUND,&o,NULL,0)<0 && !decoder&&!xaudio);fail_engine=false;
    fail_master=true;assert(audio_init(AUDIO_CONFIGURATION_51_SURROUND,&o,NULL,0)<0 && !decoder&&!xaudio);fail_master=false;
    fail_start=true;assert(audio_init(AUDIO_CONFIGURATION_51_SURROUND,&o,NULL,0)<0 && !decoder&&!xaudio&&!voice);fail_start=false;
    fail_decoder=true;assert(audio_init(AUDIO_CONFIGURATION_51_SURROUND,&o,NULL,0)<0 && !decoder);fail_decoder=false;
    o.channelCount=2;o.streams=o.coupledStreams=1;o.mapping[0]=0;o.mapping[1]=1;audio_select_configuration(false);
    assert(audio_init(AUDIO_CONFIGURATION_STEREO,&o,NULL,0)==0);audio_decode_and_play(NULL,0);assert(last_bytes==3840);audio_cleanup();
    o.channelCount=8;assert(audio_init(0,&o,NULL,0)<0);o.channelCount=6;o.samplesPerFrame=961;assert(audio_init(0,&o,NULL,0)<0);
    puts("PASS: actual XDK audio init/decode/cleanup with mocks, speaker negotiation, WAVE mask/block/rate, Opus map, 6/2-channel bytes, queue decoding, stereo downmix fallback and failure cleanup");
}
