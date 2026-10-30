/* Native preview of settings scrolling and the reconnect screen. */
#include "../../src/ui/ui.h"
#include <assert.h>
#include <stdio.h>

void ui_present(void) {}
static void save(const char *name)
{
    canvas_t *cv = ui_canvas();
    FILE *f = fopen(name, "wb");
    assert(f);
    fprintf(f, "P6\n%d %d\n255\n", cv->w, cv->h);
    for (int i = 0; i < cv->w * cv->h; i++) {
        unsigned char rgb[3] = { (unsigned char)(cv->px[i] >> 16),
                                (unsigned char)(cv->px[i] >> 8), (unsigned char)cv->px[i] };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
int main(void)
{
    assert(ui_init(1280, 720));
    const ui_setting rows[] = {
        { "Resolution", "1280x720", "Host resolution." },
        { "Frame rate", "60 FPS", "Frame rate." },
        { "Bitrate", "30 Mbps", "Bitrate." },
        { "Fast decode", "On", "Skips H.264's deblocking filter: much faster, slightly blockier picture." },
        { "V-Sync", "On", "V-Sync." },
        { "Optimize game settings", "On", "Host settings." },
        { "Play audio on PC", "Off", "Local audio." },
        { "Decoder benchmark", "Press A", "Benchmark." },
        { "Video preset", "720p60 Quality", "720p60 Quality: 30 Mbps. 1080p60 Max: 60 Mbps. Both use Fast decode, GPU and V-Sync. Edit values for Custom." },
        { "Exit shortcut", "LB+RB+L3+R3", "Hold for 2 seconds on one controller. L3/R3 mean clicking the sticks. The full shortcut is handled locally; the app keeps running on the PC." },
        { "Stick deadzone", "0%", "Filters small stick movements during streaming and rescales the remaining range. 0% preserves the original input. Does not affect menu navigation." },
        { "Session report", "Off", "Saves last-session.txt in the moonlight folder after the stream stops. No disk writes or extra drawing while streaming; overwrites the previous report." }
    };
    ui_draw_settings(rows, 12, 0); save("settings-top.ppm");
    ui_draw_settings(rows, 12, 8); save("settings-preset.ppm");
    ui_draw_settings(rows, 12, 11); save("settings-bottom.ppm");
    ui_stat stats[8] = {};
    for (int i = 0; i < 8; i++) { stats[i].label = "Session metric"; snprintf(stats[i].value, 64, "%d", i); }
    ui_draw_stream_stats("Connection lost (error -1)", stats, 8,
                         "Press A to reconnect or B to return. The app stays open on the PC.", true);
    save("reconnect.ppm");
    puts("PASS: generated settings top/preset/bottom and reconnect previews.");
}
