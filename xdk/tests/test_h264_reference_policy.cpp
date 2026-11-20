#include <assert.h>
#include "../../src/h264_reference_policy.h"

int main()
{
    const int normal=0, nonref=8, all=48;
    assert(h264_reference_fast_flag(0,true,true));
    assert(!h264_reference_fast_flag(0,true,false));
    assert(!h264_reference_fast_flag(0,false,true));
    assert(!h264_reference_fast_flag(1,true,true));
    assert(!h264_reference_fast_flag(2,true,true));
    assert(h264_reference_filter(0,true,false,normal,nonref,all)==all);
    assert(h264_reference_filter(0,false,true,normal,nonref,all)==nonref);
    assert(h264_reference_filter(1,true,true,normal,nonref,all)==normal);
    assert(h264_reference_filter(2,true,false,normal,nonref,all)==nonref);
    assert(h264_reference_filter(2,false,true,normal,nonref,all)==nonref);
    assert(h264_reference_filter(2,false,false,normal,nonref,all)==normal);
    return 0;
}
