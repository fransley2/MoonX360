#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../src/video_session.h"
static video_options options;
static bool running;
static int session_slices=8;
static int video_setup(int,int,int,int,void*,int) { return 0; }
static void video_start() {}
static void video_stop() {}
static void video_cleanup() {}
static int video_submit(PDECODE_UNIT) { return 0; }
void LiInitializeVideoCallbacks(PDECODER_RENDERER_CALLBACKS cb) { memset(cb,0,sizeof(*cb)); }
static void xdk_set_thread_layout(int){}
/* ACTUAL_SETUP */
/* ACTUAL_SHADER */
#define D3DRS_PRESENTINTERVAL 0
#define D3DPRESENT_INTERVAL_ONE 1
#define D3DPRESENT_INTERVAL_IMMEDIATE 0
static float uploaded[4];
struct D3DDevice {
    void SetPixelShaderConstantF(int first,const float *coeff,int count) {
        assert(first==0 && count==1); memcpy(uploaded,coeff,sizeof(uploaded));
    }
    void SetRenderState(int,int) {}
};
static D3DDevice device;
static D3DDevice *gfx_begin() { return &device; }
static void gfx_end() {}
static void actual_render_setup() {
    D3DDevice *d;
/* ACTUAL_RENDER_SETUP */
}
static void check_matrix(int space)
{
    options.color_space=space;actual_render_setup();
    double kr=space==COLORSPACE_REC_709 ? .2126 : .299;
    double kb=space==COLORSPACE_REC_709 ? .0722 : .114;
    double kg=1-kr-kb;
    double reference[]={2*(1-kr)*255/224, -2*kb*(1-kb)/kg*255/224,
                        -2*kr*(1-kr)/kg*255/224, 2*(1-kb)*255/224};
    for(int i=0;i<4;i++) assert(fabs(uploaded[i]-reference[i])<.000002);
    const int samples[]={0,16,64,128,235,240,255};
    for(int a=0;a<7;a++) for(int b=0;b<7;b++) for(int c=0;c<7;c++) {
        double y=(samples[a]-16)/219.0, u=(samples[b]-128)/255.0, v=(samples[c]-128)/255.0;
        double shader_y=1.164383*(samples[a]/255.0-.062745);
        double shader_u=samples[b]/255.0-.501961, shader_v=samples[c]/255.0-.501961;
        assert(fabs(shader_y+uploaded[0]*shader_v-(y+reference[0]*v))<.000003);
        assert(fabs(shader_y+uploaded[1]*shader_u+uploaded[2]*shader_v-(y+reference[1]*u+reference[2]*v))<.000003);
        assert(fabs(shader_y+uploaded[3]*shader_u-(y+reference[3]*u))<.000003);
    }
}
int main()
{
    assert(strstr(ps_yuv_src,"float4 chroma : register(c0)"));
    assert(strstr(ps_yuv_src,"y + chroma.y * u + chroma.z * v"));
    assert(video_colorspace(1280,720)==COLORSPACE_REC_709);
    assert(video_colorspace(1920,1080)==COLORSPACE_REC_709);
    assert(video_colorspace(854,480)==COLORSPACE_REC_601);
    DECODER_RENDERER_CALLBACKS cb;
    for(int mode=0;mode<2;mode++) {
        video_options vo={};vo.width=mode?1920:1280;vo.height=mode?1080:720;vo.target_fps=60;
        const int requests[]={0,8,12,16,7};
        for(int i=0;i<5;i++) {
            vo.slice_count=requests[i];video_set_options(&vo);video_init_callbacks(&cb);
            int expected=i==0||i==4 ? (mode?12:8) : requests[i];
            assert((unsigned)cb.capabilities>>24==(unsigned)expected);
            assert(cb.capabilities & CAPABILITY_REFERENCE_FRAME_INVALIDATION_AVC);
            assert(!(cb.capabilities & CAPABILITY_DIRECT_SUBMIT));
            assert(cb.setup==video_setup && cb.submitDecodeUnit==video_submit && cb.cleanup==video_cleanup);
            running=true;vo.slice_count=16;vo.width=854;video_set_options(&vo);
            assert(options.width==(mode?1920:1280) && session_slices==expected);
            running=false;vo.width=mode?1920:1280;
        }
    }
    video_options vo={};vo.width=1920;vo.height=1080;vo.target_fps=30;
    video_set_options(&vo);video_init_callbacks(&cb);assert((unsigned)cb.capabilities>>24==8);
    check_matrix(COLORSPACE_REC_601);check_matrix(COLORSPACE_REC_709);
    puts("PASS: real options/callback negotiation: Auto 720p60=8 / 1080p60=12, all overrides, frozen while running, RFI retained.");
    puts("PASS: host matrix selection + actual renderer constant setup; 686 limited-range YUV reference samples for 601/709.");
}
