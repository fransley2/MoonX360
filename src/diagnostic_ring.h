/* Fixed memory, platform-independent log buffer. Caller provides synchronization. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DIAG_LOG_LINES 64
#define DIAG_LOG_WIDTH 160
#define DIAG_SNAPSHOT_BYTES 16384
struct diagnostic_ring {
    char lines[DIAG_LOG_LINES][DIAG_LOG_WIDTH];
    unsigned next, count;
};
static void diagnostic_ring_add(struct diagnostic_ring *ring, uint32_t ms, const char *text)
{
    while (text && *text) {
        char *dst = ring->lines[ring->next];
        int prefix = snprintf(dst, DIAG_LOG_WIDTH, "%u ", ms);
        size_t n = prefix > 0 ? (size_t)prefix : 0;
        while (*text && *text != '\n') {
            unsigned char c = (unsigned char)*text++;
            if (c == '\r') continue;
            if (n + 1 < DIAG_LOG_WIDTH) dst[n++] = c < 32 ? ' ' : (char)c;
        }
        dst[n] = '\0';
        if (*text == '\n') text++;
        ring->next = (ring->next + 1) % DIAG_LOG_LINES;
        if (ring->count < DIAG_LOG_LINES) ring->count++;
    }
}
static size_t diagnostic_ring_read(const struct diagnostic_ring *ring, char *out, size_t capacity)
{
    size_t used = 0;
    unsigned i;
    if (!capacity) return 0;
    for (i = 0; i < ring->count; i++) {
        const char *line = ring->lines[(ring->next + DIAG_LOG_LINES - ring->count + i) % DIAG_LOG_LINES];
        size_t n = strlen(line);
        if (used + n + 1 >= capacity) break;
        memcpy(out + used, line, n);
        used += n;
        out[used++] = '\n';
    }
    out[used] = '\0';
    return used;
}
