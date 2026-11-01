"""Run the actual XDK audio callback with a controlled sink and Opus decoder."""
from pathlib import Path
import sys
r=Path(__file__).resolve().parents[2]
s=(r/'xdk/src/audio_xdk.cpp').read_text()
a=s.index('static void audio_decode_and_play(');b=s.index('\nvoid audio_init_callbacks(',a)
fixture=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int LONG;typedef unsigned char BYTE;
static bool covered,thread_pinned;
static LONG desired_volume=37;static int applied_volume=-1;
static int decoded,submitted,next_buffer,channels=2,output_channels=2;
enum {MAX_FRAME_SAMPLES=960,MAX_QUEUED=3,BUFFERS=8};
static int16_t pcm[BUFFERS][MAX_FRAME_SAMPLES*6];
struct XAUDIO2_VOICE_STATE {unsigned BuffersQueued;};
struct XAUDIO2_BUFFER {unsigned AudioBytes;const BYTE *pAudioData;};
struct Voice {
    float volume;unsigned queued;
    int SetVolume(float v){volume=v;return 0;}
    void GetState(XAUDIO2_VOICE_STATE *s){s->BuffersQueued=queued;}
    int SubmitSourceBuffer(XAUDIO2_BUFFER *b){assert(b->AudioBytes==480*2*2);assert(queued<MAX_QUEUED);queued++;submitted++;return 0;}
};
static Voice sink,*voice=&sink;static int decoder;
static bool launch_active(){return covered;}
static LONG InterlockedCompareExchange(volatile LONG *p,LONG,LONG){return *p;}
static void XSetThreadProcessor(int,int){}static int GetCurrentThread(){return 0;}
static int xdk_aux_processor(){return 0;}static void xdk_audit_thread(const char*){}
static int opus_multistream_decode(int,const unsigned char*,int,int16_t*,int,int){decoded++;return 480;}
static void audio_downmix(int16_t*,int){assert(false);}
static unsigned audio_pcm_bytes(int samples,int count){return samples*count*2;}
#define SUCCEEDED(x) ((x)>=0)
#define ZeroMemory(p,n) memset(p,0,n)
/* CODE */
int main(){
    for(int session=0;session<10;session++) {
        sink.queued=0;covered=true;int before=decoded,queued=submitted;
        for(int frame=0;frame<1000;frame++)audio_decode_and_play(NULL,0);
        assert(decoded==before+1000 && submitted==queued && sink.queued==0 && sink.volume==0);
        covered=false;desired_volume=session*10;audio_decode_and_play(NULL,0);
        assert(sink.queued==1 && submitted==queued+1 && sink.volume==desired_volume/100.0f);
        sink.queued=MAX_QUEUED;before=decoded;audio_decode_and_play(NULL,0);
        assert(decoded==before+1 && sink.queued==MAX_QUEUED && submitted==queued+1);
    }
    puts("PASS: actual audio callback continuously decodes covered packets, queues no covered PCM, restores requested volume and keeps existing queue bound");
}
'''
Path(sys.argv[1]).write_text(fixture.replace('/* CODE */',s[a:b]))
