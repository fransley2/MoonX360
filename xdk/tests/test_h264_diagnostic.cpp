#include <assert.h>
#include "../../src/video_h264_diagnostic.h"

int main()
{
    video_h264_diagnostic d;
    video_h264_diagnostic_parse(0,&d);
    assert(d.fast_flag==1 && d.workers==0 && d.rfi==1 && d.capture_yuv==0);
    video_h264_diagnostic_parse("# comparison\nfast_flag = 0\nworkers = 1\nrfi = 0\ncapture_yuv = 1\n",&d);
    assert(d.fast_flag==0 && d.workers==1 && d.rfi==0 && d.capture_yuv==1);
    video_h264_diagnostic_parse("fast_flag = 7\nworkers = 99\nrfi = no\ncapture_yuv = 4\n",&d);
    assert(d.fast_flag==1 && d.workers==0 && d.rfi==1 && d.capture_yuv==0);
    return 0;
}
