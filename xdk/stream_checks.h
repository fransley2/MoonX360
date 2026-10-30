/* Platform-independent bounds and policy checks, shared with host tests. */
#ifndef XDK_STREAM_CHECKS_H
#define XDK_STREAM_CHECKS_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

static __inline int xdk_video_receiver_name(const char *name)
{
    return name != NULL && strcmp(name, "VideoRecv") == 0;
}

static __inline int xdk_udp_receive_buffer(int requested, int is_udp)
{
    return is_udp && requested > 255 * 1024 ? 255 * 1024 : requested;
}

static __inline int xdk_fragment_fits(int total, int offset, int length)
{
    return total > 0 && offset >= 0 && offset <= total && length > 0 && length <= total - offset;
}

static __inline uint32_t xdk_read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static __inline int xdk_capture_valid(const uint8_t *data, size_t size)
{
    uint32_t width, height, count, i;
    size_t pos = 16;
    if (!data || size < 16 || memcmp(data, "MLC1", 4))
        return 0;
    width = xdk_read_be32(data + 4);
    height = xdk_read_be32(data + 8);
    count = xdk_read_be32(data + 12);
    if (!width || width > 1920 || !height || height > 1080 || !count || count > 600)
        return 0;
    for (i = 0; i < count; i++) {
        uint32_t length;
        if (size - pos < 4)
            return 0;
        length = xdk_read_be32(data + pos);
        pos += 4;
        if (!length || length > 2 * 1024 * 1024 || length > size - pos)
            return 0;
        pos += length;
    }
    return pos == size;
}

/* Capture sidecar binding; not a cryptographic integrity/security primitive. */
static __inline uint32_t xdk_capture_fingerprint(const uint8_t *data,size_t size)
{
    uint32_t hash=2166136261u;size_t i;
    for(i=0;i<size;i++) hash=(hash^data[i])*16777619u;
    return hash;
}

struct xdk_capture_info { int fps,bitrate,slices; };
static __inline int xdk_capture_info_parse(const char *text,const uint8_t *data,size_t size,struct xdk_capture_info *out)
{
    unsigned bytes=0,hash=0;int w=0,h=0,fps=0,bitrate=0,slices=0;
    memset(out,0,sizeof(*out));
    if(!text || !xdk_capture_valid(data,size)) return 0;
    if(sscanf(text,"capture_bytes=%u\nfingerprint=%x\nwidth=%d\nheight=%d\ntarget_fps=%d\nbitrate_kbps=%d\nslices_requested=%d",
        &bytes,&hash,&w,&h,&fps,&bitrate,&slices)!=7 || bytes!=size ||
        w!=(int)xdk_read_be32(data+4) || h!=(int)xdk_read_be32(data+8) ||
        hash!=xdk_capture_fingerprint(data,size) || (slices!=8 && slices!=12 && slices!=16) ||
        (fps!=24 && fps!=30 && fps!=40 && fps!=50 && fps!=60) || bitrate<1000 || bitrate>70000) return 0;
    out->fps=fps;out->bitrate=bitrate;out->slices=slices;return 1;
}

#endif
