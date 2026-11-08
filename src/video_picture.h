/* Metadata follows the decoded picture, including FFmpeg output reordering.
 * No pixels, pointers or GPU ownership are carried in this value. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

static uint64_t video_picture_tag(uint32_t frame, uint32_t request, bool idr)
{
    return ((uint64_t)request << 32) | ((uint64_t)(frame & 0x7fffffffU) << 1) | (idr ? 1 : 0);
}
static bool video_picture_idr(uint64_t tag, bool key_frame)
{
    /* A key picture alone can also be a non-IDR I picture. */
    return key_frame && (tag & 1) != 0;
}
static uint32_t video_picture_request(uint64_t tag)
{
    return (uint32_t)(tag >> 32);
}
