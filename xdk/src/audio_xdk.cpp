/*
 * Opus decode and playback through XAudio2 (48 kHz, 16-bit stereo / optional 5.1, native
 * big-endian samples). Runs on Moonlight's audio thread.
 */
#include "xdk.h"
#include "../../src/launch_cover.h"
#include "../../src/performance.h"

#include <xaudio2.h>
#include <string.h>

#include "../../src/audio.h"
#include "../../src/audio_layout.h"
#include "../../src/platform.h"

#include <opus_multistream.h>

#define MAX_FRAME_SAMPLES 960   /* 20 ms at 48 kHz, the largest Moonlight uses */
#define BUFFERS 16
/* Keep the queue short so audio stays in sync with the picture (~40-80 ms). */
#define MAX_QUEUED 8

static IXAudio2 *xaudio;
static IXAudio2MasteringVoice *master;
static IXAudio2SourceVoice *voice;
/* Menu audio is deliberately independent of the stream decoder/voice. It is
 * prepared once before Home and remains ready across stream disconnects. */
static IXAudio2 *menu_engine;
static IXAudio2MasteringVoice *menu_master;
static IXAudio2SourceVoice *menu_voice;
#define MENU_SAMPLES 2400
static int16_t menu_pcm[4][MENU_SAMPLES*2];
static bool menu_enabled;
static int menu_volume;
static DWORD menu_last_nav;
void audio_menu_shutdown(void)
{
    if(menu_voice){menu_voice->Stop(0);menu_voice->DestroyVoice();menu_voice=NULL;}
    if(menu_master){menu_master->DestroyVoice();menu_master=NULL;}
    if(menu_engine){menu_engine->Release();menu_engine=NULL;}
}
void audio_menu_configure(bool enabled,int volume)
{
    menu_enabled=enabled;menu_volume=volume<0?0:volume>100?100:volume;
    if(!enabled || !menu_volume){if(menu_voice){menu_voice->Stop(0);menu_voice->FlushSourceBuffers();}return;}
    if(menu_voice){menu_voice->SetVolume(menu_volume/100.0f);menu_voice->Start(0);return;}
    if(FAILED(XAudio2Create(&menu_engine,0,xdk_aux_processor()==0?XboxThread0:XboxThread5)) ||
       FAILED(menu_engine->CreateMasteringVoice(&menu_master))){audio_menu_shutdown();return;}
    WAVEFORMATEX format;ZeroMemory(&format,sizeof(format));format.wFormatTag=WAVE_FORMAT_PCM;
    format.nChannels=2;format.nSamplesPerSec=48000;format.wBitsPerSample=16;
    format.nBlockAlign=4;format.nAvgBytesPerSec=192000;
    if(FAILED(menu_engine->CreateSourceVoice(&menu_voice,&format))){audio_menu_shutdown();return;}
    static const int hz[]={520,690,360,220};
    for(int effect=0;effect<4;effect++){
        int phase=0;
        for(int i=0;i<MENU_SAMPLES;i++){
            phase=(phase+hz[effect]*65536/48000)&65535;
            int triangle=phase<32768?phase-16384:49152-phase;
            int envelope=(MENU_SAMPLES-i)*(MENU_SAMPLES-i)/MENU_SAMPLES;
            /* Direction changes must remain audible at the conservative 18%
             * default. The previous /12 gain was barely above room noise. */
            int gain=effect==0?3:effect==3?4:5;
            int sample=triangle*envelope/(MENU_SAMPLES*gain);
            menu_pcm[effect][i*2]=menu_pcm[effect][i*2+1]=(int16_t)sample;
        }
    }
    menu_voice->SetVolume(menu_volume/100.0f);
    if(FAILED(menu_voice->Start(0)))audio_menu_shutdown();
}
void audio_menu_play(int effect)
{
    if(!menu_enabled || !menu_volume || !menu_voice || effect<0 || effect>3)return;
    DWORD now=GetTickCount();if(effect==0 && now-menu_last_nav<80)return;
    XAUDIO2_VOICE_STATE state;menu_voice->GetState(&state);
    if(state.BuffersQueued)return; /* One short sound maximum; no delayed clicks. */
    XAUDIO2_BUFFER buffer;ZeroMemory(&buffer,sizeof(buffer));
    buffer.Flags=XAUDIO2_END_OF_STREAM;
    buffer.AudioBytes=sizeof(menu_pcm[0]);buffer.pAudioData=(const BYTE*)menu_pcm[effect];
    if(SUCCEEDED(menu_voice->SubmitSourceBuffer(&buffer)) && effect==0)menu_last_nav=now;
}
static OpusMSDecoder *decoder;
static int16_t pcm[BUFFERS][MAX_FRAME_SAMPLES * AUDIO_MAX_CHANNELS];
static int channels,output_channels;
static volatile LONG desired_volume=100,effective_channels;
static int applied_volume=-1;
void audio_set_volume(int volume){InterlockedExchange(&desired_volume,volume<0?0:volume>100?100:volume);}
int audio_output_channels(void){return (int)InterlockedCompareExchange(&effective_channels,0,0);}
static const char *fallback_reason;
const char *audio_fallback_reason(void) {return fallback_reason;}
int audio_select_configuration(bool surround)
{
    fallback_reason=NULL;
    if(!surround)return AUDIO_CONFIGURATION_STEREO;
    XAUDIOSPEAKERCONFIG speakers=0;
    if(FAILED(XAudioGetSpeakerConfig(&speakers)))fallback_reason="Stereo fallback: output capability query failed.";
    else if((speakers&XAUDIOSPEAKERCONFIG_ANALOG_MASK)!=XAUDIOSPEAKERCONFIG_ANALOG_SURROUND &&
        !(speakers&(XAUDIOSPEAKERCONFIG_DIGITAL_DOLBYDIGITAL|XAUDIOSPEAKERCONFIG_DIGITAL_WMAPRO)))
        fallback_reason="Stereo fallback: configure a 5.1 output in the Xbox dashboard.";
    return fallback_reason?AUDIO_CONFIGURATION_STEREO:AUDIO_CONFIGURATION_51_SURROUND;
}
static void audio_cleanup(void);
static int next_buffer;
static int nominal_frame_samples,audio_sample_rate;
static bool freeze_first_audio_submit;
static bool thread_pinned;

static int audio_init(int config, const POPUS_MULTISTREAM_CONFIGURATION opus, void *ctx, int flags)
{
    plat_diagnostics_checkpoint("[FREEZE] audio init entered\n");
    WAVEFORMATEX wfx;
    WAVEFORMATEXTENSIBLE surround;
    int err;
    (void)config;
    (void)ctx;
    (void)flags;

    if (!audio_layout_valid(opus->channelCount,opus->sampleRate,opus->samplesPerFrame))
        return -1;
    channels=output_channels=opus->channelCount;
    nominal_frame_samples=opus->samplesPerFrame;audio_sample_rate=opus->sampleRate;
    if(config==AUDIO_CONFIGURATION_51_SURROUND && channels==2)fallback_reason="Stereo fallback: host negotiated stereo.";
    decoder = opus_multistream_decoder_create(opus->sampleRate, opus->channelCount, opus->streams,
                                              opus->coupledStreams, opus->mapping, &err);
    if (decoder == NULL)
        return -1;

    if (xaudio == NULL) {
        if (FAILED(XAudio2Create(&xaudio, 0, xdk_aux_processor()==0?XboxThread0:XboxThread5)) ||
            FAILED(xaudio->CreateMasteringVoice(&master))) {
            if (xaudio)
                xaudio->Release();
            xaudio = NULL;
            audio_cleanup();return -1;
        }
    }

    XAUDIO2_VOICE_DETAILS details;master->GetVoiceDetails(&details);
    if(channels==6 && details.InputChannels<6) {
        output_channels=2;fallback_reason="Stereo fallback: XAudio2 output has fewer than six channels.";
    }
    ZeroMemory(&wfx, sizeof(wfx));
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = (WORD)output_channels;
    wfx.nSamplesPerSec = opus->sampleRate;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = (WORD)(output_channels*2);
    wfx.nAvgBytesPerSec = opus->sampleRate * wfx.nBlockAlign;
    WAVEFORMATEX *format=&wfx;
    if(output_channels==6) {
        ZeroMemory(&surround,sizeof(surround));surround.Format=wfx;
        surround.Format.wFormatTag=WAVE_FORMAT_EXTENSIBLE;
        surround.Format.cbSize=sizeof(surround)-sizeof(WAVEFORMATEX);
        surround.Samples.wValidBitsPerSample=16;surround.dwChannelMask=SPEAKER_5POINT1;
        surround.SubFormat=KSDATAFORMAT_SUBTYPE_PCM;format=&surround.Format;
    }
    HRESULT result=xaudio->CreateSourceVoice(&voice, format);
    if(FAILED(result) && output_channels==6) {
        output_channels=2;wfx.nChannels=2;wfx.nBlockAlign=4;wfx.nAvgBytesPerSec=opus->sampleRate*4;
        fallback_reason="Stereo fallback: XAudio2 could not create a 5.1 source.";
        result=xaudio->CreateSourceVoice(&voice,&wfx);
    }
    if(FAILED(result) || FAILED(voice->Start(0))) {audio_cleanup();return -1;}
    if(fallback_reason)plat_log(fallback_reason);
    InterlockedExchange(&effective_channels,output_channels);
    applied_volume=-1;
    next_buffer = 0;
    thread_pinned = false;
    freeze_first_audio_submit=false;
    plat_diagnostics_checkpoint("[FREEZE] audio init ready\n");
    return 0;
}

static void audio_cleanup(void)
{
    if(xaudio && perf_enabled()) {
        XAUDIO2_PERFORMANCE_DATA data;xaudio->GetPerformanceData(&data);
        perf_record(PERF_AUDIO_GLITCHES,data.GlitchesSinceEngineStarted);
    }
    plat_shutdown_stage("Stopping XAudio2 voice\n");
    if (voice) {
        voice->Stop(0);
        voice->DestroyVoice();
        voice = NULL;
    }
    if (master) {master->DestroyVoice();master=NULL;}
    if (xaudio) {xaudio->Release();xaudio=NULL;}
    if (decoder) {
        opus_multistream_decoder_destroy(decoder);
        decoder = NULL;
    }
    InterlockedExchange(&effective_channels,0);
    plat_shutdown_stage("Audio resources released\n");
}

static void audio_decode_and_play(char *data, int len)
{
    video_progress_note(VP_AUDIO);
    PerfScope callback(PERF_AUDIO_CALLBACK);
    XAUDIO2_VOICE_STATE state;
    XAUDIO2_BUFFER buf;
    int16_t *out = pcm[next_buffer];
    int samples;

    if (!thread_pinned) {
        XSetThreadProcessor(GetCurrentThread(), xdk_aux_processor());
        thread_pinned = true;
        xdk_audit_thread("AudioDec/Opus");
    }

    int volume=(int)InterlockedCompareExchange(&desired_volume,0,0);
    if(launch_active())volume=0;
    if(volume!=applied_volume && SUCCEEDED(voice->SetVolume(volume/100.0f)))applied_volume=volume;
    {PerfScope decode(PERF_AUDIO_DECODE);
    samples = opus_multistream_decode(decoder, (const unsigned char *)data, len, out, MAX_FRAME_SAMPLES, 0);}
    if (samples <= 0 || launch_active())
        return; /* Keep Opus history, discard covered PCM rather than queueing it. */

    /* Decode every packet to preserve Opus state, even when playback is behind. */
    voice->GetState(&state);
    if(perf_enabled()) {
        perf_record(PERF_AUDIO_BUFFERS,state.BuffersQueued);
        if(audio_sample_rate>0)perf_record(PERF_AUDIO_QUEUED_MS,
            (uint64_t)state.BuffersQueued*nominal_frame_samples*1000/audio_sample_rate);
    }
    if (state.BuffersQueued >= MAX_QUEUED) {
        perf_record(PERF_AUDIO_QUEUE_FULL,1);
        return;
    }

    ZeroMemory(&buf, sizeof(buf));
    if(channels==6 && output_channels==2)audio_downmix(out,samples);
    buf.AudioBytes = audio_pcm_bytes(samples,output_channels);
    buf.pAudioData = (const BYTE *)out;
    if (SUCCEEDED(voice->SubmitSourceBuffer(&buf))) {
        next_buffer = (next_buffer + 1) % BUFFERS;
        if(!freeze_first_audio_submit) {
            freeze_first_audio_submit=true;
            plat_diagnostics_checkpoint("[FREEZE] first audio submit\n");
        }
    } else perf_record(PERF_AUDIO_SUBMIT_FAIL,1);
}

void audio_init_callbacks(AUDIO_RENDERER_CALLBACKS *cb)
{
    LiInitializeAudioCallbacks(cb);
    cb->init = audio_init;
    cb->cleanup = audio_cleanup;
    cb->decodeAndPlaySample = audio_decode_and_play;
    /* Use Moonlight's separate decoder worker, with the 20 ms audio preset. */
    cb->capabilities = CAPABILITY_SLOW_OPUS_DECODER;
}
