#include "../stream_checks.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

static void be32(uint8_t *p, uint32_t n)
{
    p[0] = (uint8_t)(n >> 24); p[1] = (uint8_t)(n >> 16);
    p[2] = (uint8_t)(n >> 8); p[3] = (uint8_t)n;
}

int main(void)
{
    uint8_t recording[24] = { 'M', 'L', 'C', '1' };
    size_t i;
    assert(xdk_video_receiver_name("VideoRecv"));
    assert(!xdk_video_receiver_name(NULL));
    assert(!xdk_video_receiver_name("VideoDec"));
    assert(!xdk_video_receiver_name("AudioRecv"));
    assert(!xdk_video_receiver_name("main"));
    assert(!xdk_video_receiver_name("VideoRecv-extra"));
    assert(xdk_udp_receive_buffer(512 * 1024, 1) == 255 * 1024);
    assert(xdk_udp_receive_buffer(512 * 1024, 0) == 512 * 1024);
    assert(xdk_udp_receive_buffer(128 * 1024, 1) == 128 * 1024);
    assert(xdk_udp_receive_buffer(-1, 1) == -1);
    assert(xdk_fragment_fits(1024, 0, 1024));
    assert(xdk_fragment_fits(INT_MAX, INT_MAX - 1, 1));
    assert(!xdk_fragment_fits(1024, 1024, 1));
    assert(!xdk_fragment_fits(1024, -1, 1));
    assert(!xdk_fragment_fits(1024, 0, -1));
    assert(!xdk_fragment_fits(1024, 0, 1025));
    assert(!xdk_fragment_fits(INT_MAX, 1, INT_MAX));
    be32(recording + 4, 1280); be32(recording + 8, 720);
    be32(recording + 12, 1); be32(recording + 16, 4);
    assert(xdk_capture_valid(recording, sizeof(recording)));
    {
        char meta[256];struct xdk_capture_info info;
        sprintf(meta,"capture_bytes=24\nfingerprint=%08x\nwidth=1280\nheight=720\ntarget_fps=60\nbitrate_kbps=30000\nslices_requested=8\n",
            xdk_capture_fingerprint(recording,sizeof(recording)));
        assert(xdk_capture_info_parse(meta,recording,sizeof(recording),&info));
        assert(info.slices==8 && info.fps==60 && info.bitrate==30000);
        recording[23]^=1;assert(!xdk_capture_info_parse(meta,recording,sizeof(recording),&info));
        assert(!info.slices);recording[23]^=1;
        assert(!xdk_capture_info_parse(NULL,recording,sizeof(recording),&info));
    }
    for (i = 0; i < sizeof(recording); i++)
        assert(!xdk_capture_valid(recording, i));
    assert(!xdk_capture_valid(NULL, sizeof(recording)));
    be32(recording + 16, UINT32_MAX);
    assert(!xdk_capture_valid(recording, sizeof(recording)));
    be32(recording + 16, 0);
    assert(!xdk_capture_valid(recording, sizeof(recording)));
    be32(recording + 16, 4); be32(recording + 12, 601);
    assert(!xdk_capture_valid(recording, sizeof(recording)));
    be32(recording + 12, 1); be32(recording + 4, 0);
    assert(!xdk_capture_valid(recording, sizeof(recording)));
    puts("PASS: receiver isolation, UDP/TCP limits, fragment overflow and capture truncation");
    return 0;
}
