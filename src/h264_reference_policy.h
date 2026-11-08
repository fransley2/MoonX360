/* Compile-time H.264 decoder A/B policy. Mode 0 preserves the current path. */
#pragma once
#include <stdbool.h>

static bool h264_reference_fast_flag(int mode, bool configured_fast_flag, bool fast_decode)
{
    return mode == 0 && configured_fast_flag && fast_decode;
}

static int h264_reference_filter(int mode, bool fast_decode, bool deadline_skip,
                                 int discard_default, int discard_nonref, int discard_all)
{
    if (mode == 1) return discard_default;
    if (mode == 2) return fast_decode || deadline_skip ? discard_nonref : discard_default;
    return fast_decode ? discard_all : deadline_skip ? discard_nonref : discard_default;
}
