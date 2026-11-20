"""Policy + real bundled H.264 deblocking decision, no pixel/performance emulation."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
s=(root/'xdk/deps/XBMC-360/libraries/ffmpeg/libavcodec/h264.c').read_text()
a=s.index('    if (h->avctx->skip_loop_filter >= AVDISCARD_ALL')
b=s.index('    if (raw_deblocking_idc < 3)',a)
block=s[a:b]
fixture=r'''
#include <assert.h>
#include <stdio.h>
#include "../src/h264_reference_policy.h"
enum {AVDISCARD_DEFAULT=0,AVDISCARD_NONREF=8,AVDISCARD_BIDIR=16,AVDISCARD_NONKEY=32,AVDISCARD_ALL=48,
AV_PICTURE_TYPE_I=1,AV_PICTURE_TYPE_B=3,CODEC_FLAG2_FAST=1,AV_LOG_INFO=0,AV_LOG_ERROR=1};
struct Context {int skip_loop_filter,flags2;};
struct H264 {Context *avctx;int deblocking_filter,slice_type_nos,nal_ref_idc,max_contexts,single_decode_warning;struct {int forced_serial;} xbox_parallel_diagnostic;};
static void av_log(Context*,int,const char*){}
static int actual(H264 *h,H264 *h0){
/* ACTUAL */
return 0;
}
int main(){
for(int build=0;build<3;build++)for(int fast=0;fast<2;fast++)for(int pressure=0;pressure<2;pressure++)for(int ref=0;ref<2;ref++)for(int idc=0;idc<3;idc++){
 int filter=h264_reference_filter(build,fast!=0,pressure!=0,AVDISCARD_DEFAULT,AVDISCARD_NONREF,AVDISCARD_ALL);
 int expected=build==1?AVDISCARD_DEFAULT:build==2?(fast||pressure?AVDISCARD_NONREF:AVDISCARD_DEFAULT):fast?AVDISCARD_ALL:pressure?AVDISCARD_NONREF:AVDISCARD_DEFAULT;
 assert(filter==expected);
 Context c={filter,h264_reference_fast_flag(build,true,fast!=0)?CODEC_FLAG2_FAST:0};
 H264 h={};h.avctx=&c;h.nal_ref_idc=ref;h.slice_type_nos=2;h.max_contexts=4;
 h.deblocking_filter=idc==0?1:idc==1?0:2;
 assert(!actual(&h,&h));
 if(filter==AVDISCARD_ALL || (filter==AVDISCARD_NONREF && !ref))assert(h.deblocking_filter==0);
 else if(idc==0 && !c.flags2)assert(h.deblocking_filter==1 && h.max_contexts==1 && h.xbox_parallel_diagnostic.forced_serial);
 else assert(h.deblocking_filter==(idc==0?2:idc==1?0:2));
}
puts("PASS: actual FFmpeg deblock block: 72 legacy On/Off/overload/reference/IDC cases, including diagnostic restrictions");
}
'''
Path(sys.argv[1]).write_text(fixture.replace('/* ACTUAL */',block))
