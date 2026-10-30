#pragma once
#include <stdint.h>
#include <stdbool.h>
#define AUDIO_MAX_CHANNELS 6
#define AUDIO_MAX_FRAME_SAMPLES 960
static bool audio_layout_valid(int channels,int rate,int samples)
{ return (channels==2 || channels==6) && rate==48000 && samples>0 && samples<=AUDIO_MAX_FRAME_SAMPLES; }
static int audio_pcm_bytes(int samples,int channels)
{ return samples>0 && samples<=AUDIO_MAX_FRAME_SAMPLES && (channels==2 || channels==6)?samples*channels*2:0; }
static int16_t audio_clip(int n) {return (int16_t)(n>32767?32767:n< -32768?-32768:n);}
/* Moonlight's supplied Opus map already yields FL FR FC LFE BL BR (WAVE order).
 * Normalized fixed-point stereo fallback; omit LFE, retain center and surrounds. */
static void audio_downmix(int16_t *pcm,int samples)
{
    for(int i=0;i<samples;i++) {
        int16_t *s=pcm+i*6;int left=(s[0]*13573+s[2]*9598+s[4]*9598)/32768;
        int right=(s[1]*13573+s[2]*9598+s[5]*9598)/32768;
        pcm[i*2]=audio_clip(left);pcm[i*2+1]=audio_clip(right);
    }
}
