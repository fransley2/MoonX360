#pragma once
#include <stddef.h>
#include <stdbool.h>
#include "../third_party/stb/stb_image.h"
/* Bound the source before STB allocates full RGBA + converted ARGB buffers.
 * A small encoded file alone does not bound a decompressed PNG/JPEG. */
#define COVER_ENCODED_MAX (1024u*1024u)
#define COVER_SOURCE_PIXELS (1024u*1024u)
static bool cover_decode_allowed(const unsigned char *data,size_t len)
{
    int w=0,h=0,n=0;
    return data && len && len<=COVER_ENCODED_MAX &&
        stbi_info_from_memory(data,(int)len,&w,&h,&n) &&
        w>0 && h>0 && w<=2048 && h<=2048 &&
        (size_t)w*h<=COVER_SOURCE_PIXELS;
}
