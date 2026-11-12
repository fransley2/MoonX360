#pragma once

#include <string.h>

/* Session preference, independent of the YUV renderer and stream profiles.
 * No private system ABI is declared here. System GPU is deliberately absent
 * from the compiled backend registry until its complete contract is verified. */
enum video_decoder {
    VIDEO_DECODER_FFMPEG,
    VIDEO_DECODER_SYSTEM_GPU,
    VIDEO_DECODER_AUTO,
    VIDEO_DECODER_COUNT
};

static inline int video_decoder_validate(int decoder)
{
    return decoder >= 0 && decoder < VIDEO_DECODER_COUNT ? decoder : VIDEO_DECODER_FFMPEG;
}

static inline const char *video_decoder_key(int decoder)
{
    switch (video_decoder_validate(decoder)) {
    case VIDEO_DECODER_SYSTEM_GPU: return "system_gpu";
    case VIDEO_DECODER_AUTO: return "auto";
    default: return "ffmpeg";
    }
}

static inline int video_decoder_parse(const char *key)
{
    if (key && !strcmp(key, "system_gpu")) return VIDEO_DECODER_SYSTEM_GPU;
    if (key && !strcmp(key, "auto")) return VIDEO_DECODER_AUTO;
    return VIDEO_DECODER_FFMPEG;
}

static inline const char *video_decoder_name(int decoder)
{
    switch (video_decoder_validate(decoder)) {
    case VIDEO_DECODER_SYSTEM_GPU: return "System GPU (Experimental)";
    case VIDEO_DECODER_AUTO: return "Auto";
    default: return "FFmpeg (Stable)";
    }
}

static inline const char *video_decoder_unavailable_reason(void)
{
    return "System GPU is unavailable in this build. The private H.264 ABI, motor lock and safe GPU drain contract are missing. Select FFmpeg (Stable) or Auto.";
}

/* A negative result rejects an explicit unavailable choice. Auto can use only
 * a backend actually implemented by this build; it currently resolves to FFmpeg.
 * Module presence alone cannot establish ABI or runtime compatibility. */
static inline int video_decoder_resolve(int requested)
{
    return video_decoder_validate(requested) == VIDEO_DECODER_SYSTEM_GPU ? -1 : VIDEO_DECODER_FFMPEG;
}
