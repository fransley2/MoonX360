"""Compare actual VMX filter math + startup selftest against this tree's C.
Host arithmetic model, not a Xenon instruction or performance emulator.
Usage: python xdk/tests/make_loopfilter_reference_test.py output.c
"""
from pathlib import Path
import sys

tests = Path(__file__).resolve().parent
source = (tests.parent / 'ffmpeg_xbox/h264_vmx.c').read_text()
reference = (tests.parent / 'deps/XBMC-360/libraries/ffmpeg/libavcodec/h264dsp_template.c').read_text()
header = (tests.parent / 'deps/XBMC-360/libraries/ffmpeg/libavcodec/h264dsp.h').read_text()
model = (tests / 'vmx_idct_emulation.h').read_text()
model += '''
static __vector4 __vand(__vector4 a,__vector4 b){__vector4 r;int i;for(i=0;i<16;i++)r.b[i]=a.b[i]&b.b[i];return r;}
static __vector4 __vsel(__vector4 a,__vector4 b,__vector4 m){__vector4 r;int i;for(i=0;i<16;i++)r.b[i]=(a.b[i]&~m.b[i])|(b.b[i]&m.b[i]);return r;}
static __vector4 __vsplth(__vector4 a,int n){__vector4 r;int i;for(i=0;i<8;i++)ph(&r,i,h(a,n));return r;}
static __vector4 __vcmpequwR(__vector4 a,__vector4 b,unsigned *cr){__vector4 r;int i,all=1;for(i=0;i<4;i++){int eq=w(a,i)==w(b,i);pw(&r,i,eq?0xffffffffu:0);if(!eq)all=0;}*cr=all?0x80u:0;return r;}
#define FFABS(x) abs(x)
#define av_clip(x,lo,hi) clip(x,lo,hi)
#define BIT_DEPTH 8
#define av_always_inline __forceinline
#define av_flatten
static unsigned loopfilter_ok;
static unsigned ff_xbox_loopfilter_mask=255;
static unsigned idct_ok, idct_batch_ok;
static int tested, ff_xbox_vmx_flags;
#define XVMX_IDCT 8
#define XVMX_LOOPFILTER 16
'''
model += header[header.index('typedef void (*h264_weight_func)'):header.index('void ff_h264dsp_init(')]
# Include the real init hook and IDCT dependencies so individual fallback
# gating is tested, not a rewritten imitation of the install policy.
model += source[source.index('static const __declspec(align(16)) uint8_t perm_mrghh'):
                source.index('/* ------------------------------------------------------------ stores */')]
model += source[source.index('static __forceinline void clear_block16'):
                source.index('/* ------------------------------------------------------------ loop filter */')]
model += source[source.index('/* ------------------------------------------------------------ loop filter */'):
                source.index('/* ------------------------------------------------------------ init hooks */')]
model += source[source.index('void ff_h264dsp_init_xbox('):source.index('void ff_videodsp_init_xbox(')]
model += reference[reference.index('static av_always_inline av_flatten void FUNCC(h264_loop_filter_luma)'):]
model += source[source.index('static unsigned rng_state'):source.index('static void fill(')]
model += source[source.index('/* LF_TEST_BEGIN'):source.index('/* LF_TEST_END */')]
model += '''
int main(void) {
    H264DSPContext dc, original, candidate;
    int bit;
    memset(&dc,0,sizeof(dc));
#define SET(name) dc.h264_##name=h264_##name##_reference
    SET(v_loop_filter_luma); SET(h_loop_filter_luma);
    SET(v_loop_filter_luma_intra); SET(h_loop_filter_luma_intra);
    SET(v_loop_filter_chroma); SET(h_loop_filter_chroma);
    SET(v_loop_filter_chroma_intra); SET(h_loop_filter_chroma_intra);
    original=dc;
    if(test_loopfilters(&dc, (void (*)(const char *))0) || loopfilter_ok!=255) {
        printf("FAIL: loopfilter mask %02x\\n",loopfilter_ok); return 1;
    }
    puts("PASS: all 8 loopfilters, 64000 cases against actual FFmpeg C; whole-buffer guards");
    ff_xbox_vmx_flags=XVMX_LOOPFILTER;
    ff_h264dsp_init_xbox(&dc,8,1);
    if(memcmp(&dc,&original,sizeof(dc))) return 2; /* not tested yet */
    tested=1; ff_xbox_vmx_flags=0;
    ff_h264dsp_init_xbox(&dc,8,1);
    if(memcmp(&dc,&original,sizeof(dc))) return 3; /* disabled group */
    ff_xbox_vmx_flags=XVMX_LOOPFILTER;
    ff_h264dsp_init_xbox(&dc,10,1);
    if(memcmp(&dc,&original,sizeof(dc))) return 4; /* bit depth guard */
    for(bit=0;bit<8;bit++) {
        candidate=original; loopfilter_ok=1u<<bit;
        ff_h264dsp_init_xbox(&candidate,8,1);
#define CHECK(field,idx,vmx) if(candidate.field != (bit==idx ? vmx : original.field)) return 5
        CHECK(h264_v_loop_filter_luma,0,lf_v_luma);
        CHECK(h264_h_loop_filter_luma,1,lf_h_luma);
        CHECK(h264_v_loop_filter_luma_intra,2,lf_vi_luma);
        CHECK(h264_h_loop_filter_luma_intra,3,lf_hi_luma);
        CHECK(h264_v_loop_filter_chroma,4,lf_v_chroma);
        CHECK(h264_h_loop_filter_chroma,5,lf_h_chroma);
        CHECK(h264_v_loop_filter_chroma_intra,6,lf_vi_chroma);
        CHECK(h264_h_loop_filter_chroma_intra,7,lf_hi_chroma);
        if(candidate.h264_h_loop_filter_luma_mbaff != original.h264_h_loop_filter_luma_mbaff ||
           candidate.h264_loop_filter_strength != original.h264_loop_filter_strength) return 6;
    }
    candidate=original; loopfilter_ok=255;
    ff_h264dsp_init_xbox(&candidate,8,2);
    if(candidate.h264_h_loop_filter_chroma != original.h264_h_loop_filter_chroma ||
       candidate.h264_h_loop_filter_chroma_intra != original.h264_h_loop_filter_chroma_intra) return 7;
    candidate=original; loopfilter_ok=255; ff_xbox_loopfilter_mask=0x55;
    ff_h264dsp_init_xbox(&candidate,8,1);
    if(candidate.h264_v_loop_filter_luma!=lf_v_luma ||
       candidate.h264_h_loop_filter_luma!=original.h264_h_loop_filter_luma ||
       candidate.h264_v_loop_filter_luma_intra!=lf_vi_luma ||
       candidate.h264_h_loop_filter_luma_intra!=original.h264_h_loop_filter_luma_intra ||
       candidate.h264_v_loop_filter_chroma!=lf_v_chroma ||
       candidate.h264_h_loop_filter_chroma!=original.h264_h_loop_filter_chroma ||
       candidate.h264_v_loop_filter_chroma_intra!=lf_vi_chroma ||
       candidate.h264_h_loop_filter_chroma_intra!=original.h264_h_loop_filter_chroma_intra) return 8;
    candidate=original; ff_xbox_loopfilter_mask=0;
    ff_h264dsp_init_xbox(&candidate,8,1);
    if(memcmp(&candidate,&original,sizeof(candidate))) return 9;
    puts("PASS: actual init hook: tested/flags/depth/4:2:2 guards, independent validity and profiling mask C fallback");
    return 0;
}
'''
Path(sys.argv[1]).write_text(model, encoding='utf-8', newline='\n')
