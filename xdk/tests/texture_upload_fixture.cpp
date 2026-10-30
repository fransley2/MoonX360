#include <assert.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char BYTE;
typedef uintptr_t UINT_PTR;
#define FAILED(hr) ((hr) < 0)

struct D3DLOCKED_RECT { int Pitch; void *pBits; };
struct D3DTexture {
    BYTE *memory;
    int pitch, rows, locks, unlocks;
    bool fail, null_bits;
    D3DTexture(int p, int h) : pitch(p), rows(h), locks(0), unlocks(0), fail(false), null_bits(false)
    {
        memory = (BYTE *)_aligned_malloc((size_t)p * h + 128, 128);
        assert(memory);
        memset(memory, 0xCC, (size_t)p * h + 128);
    }
    ~D3DTexture() { _aligned_free(memory); }
    int LockRect(int, D3DLOCKED_RECT *r, const void *, int)
    {
        locks++;
        if (fail) return -1;
        r->Pitch = pitch;
        r->pBits = null_bits ? NULL : memory;
        return 0;
    }
    int UnlockRect(int) { unlocks++; return 0; }
};

bool video_xdk_is_streaming() { return true; }
void XMemCpy(void *, const void *, size_t)
{
    fputs("FAIL: cached-only XMemCpy called for GPU texture memory\n", stderr);
    exit(7);
}

/* ACTUAL_UPLOAD_FUNCTION */

static void test_layout(int sp, int dp, int width, int height, int offset)
{
    BYTE *allocation = (BYTE *)_aligned_malloc((size_t)sp * height + 128, 128);
    BYTE *src = allocation + offset;
    D3DTexture tex(dp, height);
    int x, y;
    assert(allocation);
    for (y = 0; y < height; y++)
        for (x = 0; x < sp; x++)
            src[(size_t)y * sp + x] = (BYTE)(x * 3 + y);
    assert(gfx_upload(&tex, src, sp, width, height));
    assert(tex.locks == 1 && tex.unlocks == 1);
    for (y = 0; y < height; y++) {
        assert(memcmp(tex.memory + (size_t)y * dp, src + (size_t)y * sp, width) == 0);
        for (x = width; x < dp; x++)
            assert(tex.memory[(size_t)y * dp + x] == 0xCC);
    }
    for (x = 0; x < 128; x++)
        assert(tex.memory[(size_t)dp * height + x] == 0xCC);
    _aligned_free(allocation);
}

int main()
{
    BYTE src[64] = { 0 };
    D3DTexture tex(64, 1), narrow(16, 1);
    test_layout(1280, 1280, 1280, 720, 0);
    test_layout(1280, 1536, 1280, 720, 0);
    test_layout(640, 640, 640, 360, 0);
    test_layout(856, 896, 854, 480, 17);
    test_layout(5120, 5120, 5120, 720, 0);
    test_layout(2048, 4096, 1920, 1080, 0);
    test_layout(2048, 1920, 1920, 1080, 3); /* only source has padding */
    test_layout(1920, 1920, 1920, 1080, 0);
    test_layout(960, 1024, 960, 540, 0); /* runtime chroma destination pitch */
    assert(!gfx_upload(NULL, src, 64, 64, 1));
    assert(!gfx_upload(&tex, NULL, 64, 64, 1));
    assert(!gfx_upload(&tex, src, 64, 0, 1));
    assert(!gfx_upload(&tex, src, 64, 64, 0));
    assert(!gfx_upload(&tex, src, 32, 64, 1));
    assert(!gfx_upload(&tex, src, -1, 64, 1));
    assert(tex.locks == 0);
    tex.fail = true;
    assert(!gfx_upload(&tex, src, 64, 64, 1));
    assert(tex.unlocks == 0);
    assert(!gfx_upload(&narrow, src, 64, 64, 1));
    assert(narrow.unlocks == 1 && narrow.memory[0] == 0xCC);
    tex.fail = false;
    tex.null_bits = true;
    assert(!gfx_upload(&tex, src, 64, 64, 1));
    assert(tex.unlocks == 1);
    tex.null_bits = false;
    tex.pitch = -1;
    assert(!gfx_upload(&tex, src, 64, 64, 1));
    assert(tex.unlocks == 2);
    puts("PASS: 9 texture layouts, invalid pitches/failed locks; exact row content and guards, no GPU XMemCpy");
    return 0;
}
