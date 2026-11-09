/* Session-only H.264 A/B switches. Absent file preserves release behavior. */
#pragma once
#include <stdio.h>
#include <string.h>

struct video_h264_diagnostic {
    int fast_flag;
    int workers;
    int rfi;
    int capture_yuv;
};

static void video_h264_diagnostic_parse(const char *text, struct video_h264_diagnostic *d)
{
    d->fast_flag = 1;
    d->workers = 0;
    d->rfi = 1;
    d->capture_yuv = 0;
    while (text && *text) {
        char line[128], key[40], value[40];
        unsigned n = 0;
        while (*text && *text != '\n') {
            if (n + 1 < sizeof(line)) line[n++] = *text;
            text++;
        }
        if (*text == '\n') text++;
        line[n] = 0;
        if (sscanf(line, " %39[^= ] = %39s", key, value) != 2) continue;
        if (!strcmp(key, "fast_flag")) {
            if (!strcmp(value, "0")) d->fast_flag = 0;
            else if (!strcmp(value, "1")) d->fast_flag = 1;
        } else if (!strcmp(key, "workers")) {
            if (!strcmp(value, "1")) d->workers = 1;
            else if (!strcmp(value, "default")) d->workers = 0;
        } else if (!strcmp(key, "rfi")) {
            if (!strcmp(value, "0")) d->rfi = 0;
            else if (!strcmp(value, "1")) d->rfi = 1;
        } else if (!strcmp(key, "capture_yuv")) {
            if (!strcmp(value, "1")) d->capture_yuv = 1;
            else if (!strcmp(value, "0")) d->capture_yuv = 0;
        }
    }
}
