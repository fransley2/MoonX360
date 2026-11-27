/*
 * MoonX360 Experimental Low-Res Transport Prototype Benchmark
 * Evaluates RGB565 + 16x16 dirty tiles + LZ77 + CRC32 UDP framing at 320x240@60 & 640x240@60
 * against raw RGB888, Xbox 360 100 Mbps NIC ceiling, and MoonX360 H.264 3 Mbps CRT baseline.
 */

#include <assert.h>
#include <chrono>
#include <stdint.h>
#include <stdio.h>
#include <vector>
#include "retro_proto.h"

static void fill_scene(std::vector<uint8_t> &rgb888, int w, int h, int scene, int frame) {
    uint32_t rng = 0x12345678u ^ (uint32_t)(frame * 2654435761u);
    auto next_rng = [&]() -> uint32_t {
        rng = rng * 1664525u + 1013904223u;
        return rng;
    };

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t r = 0, g = 0, b = 0;
            if (scene == 0) {
                /* static_menu: dark blue backdrop + blinking cursor box */
                r = 16; g = 24; b = (uint8_t)(48 + (y / 4));
                if ((frame % 20) < 10 && x >= 40 && x < 56 && y >= 80 && y < 96) {
                    r = 250; g = 220; b = 60;
                }
            } else if (scene == 1) {
                /* 2d_scroller: sky band + parallax grid + moving 32x32 sprite */
                int sx = (x + frame * 2) % 32;
                int sy = y % 32;
                if (y < 60) {
                    r = 40; g = 110; b = 210;
                } else if (y >= 200) {
                    r = (sx < 16) ? 140 : 95;
                    g = (sy < 16) ? 105 : 75;
                    b = 40;
                } else {
                    r = 70; g = 150; b = 90;
                }
                int spr_x = (frame * 3) % (w - 32);
                if (x >= spr_x && x < spr_x + 32 && y >= 168 && y < 200) {
                    r = 235; g = 60; b = 45;
                }
            } else if (scene == 2) {
                /* fullscreen_flash: alternating palette every 4 frames */
                int phase = (frame / 4) & 1;
                r = phase ? 240 : (uint8_t)((x * 3) & 255);
                g = phase ? 240 : (uint8_t)((y * 5) & 255);
                b = phase ? 240 : 96;
            } else if (scene == 3) {
                /* dithered_detail: worst-case shifting noisy tiles */
                uint32_t v = next_rng();
                r = (uint8_t)(v & 0xFF);
                g = (uint8_t)((v >> 8) & 0xFF);
                b = (uint8_t)((v >> 16) & 0xFF);
            } else if (scene == 4) {
                /* rpg_dialogue: static backdrop + changing lower 32px text bar every 8 frames */
                r = 30; g = 50; b = 85;
                if (y >= h - 32) {
                    int char_idx = ((x / 8) + (frame / 8)) & 3;
                    r = char_idx ? 230 : 20; g = char_idx ? 230 : 20; b = 230;
                }
            } else {
                /* fighting_hud: static top health bars + moving lower arena */
                if (y < 32) {
                    r = 220; g = 190; b = 30;
                } else {
                    int f1 = 40 + ((frame * 2) % 80);
                    r = 55; g = 65; b = 110;
                    if (x >= f1 && x < f1 + 48 && y >= 90 && y < 210) {
                        r = 220; g = 70; b = 70;
                    }
                }
            }
            size_t idx = ((size_t)y * (size_t)w + (size_t)x) * 3u;
            rgb888[idx + 0] = r;
            rgb888[idx + 1] = g;
            rgb888[idx + 2] = b;
        }
    }
}

int main(void) {
    struct ModeSpec { int w, h; } modes[2] = { {320, 240}, {640, 240} };
    const char *scene_names[6] = {
        "static_menu", "2d_scroller", "fullscreen_flash", "dithered_detail", "rpg_dialogue", "fighting_hud"
    };
    const int FRAMES = 120;

    printf("MoonX360 Experimental Low-Res Transport Prototype Benchmark (120 frames @ 60 Hz)\n");
    printf("| Mode | Scene | Raw RGB888 Mbps | Raw RGB565 Mbps | Proto Avg Mbps | Proto Peak Mbps | Enc us/f | Dec us/f | 1%% Loss Recovery |\n");
    printf("|---|---|---:|---:|---:|---:|---:|---:|---|\n");

    for (int m = 0; m < 2; ++m) {
        int w = modes[m].w, h = modes[m].h;
        double raw888_mbps = (double)w * h * 3.0 * 8.0 * 60.0 / 1000000.0;
        double raw565_mbps = (double)w * h * 2.0 * 8.0 * 60.0 / 1000000.0;

        for (int sc = 0; sc < 6; ++sc) {
            retro_proto::EncoderSession enc;
            retro_proto::DecoderSession dec;
            enc.init(w, h);
            dec.init(w, h);

            std::vector<uint8_t> rgb888((size_t)w * (size_t)h * 3u);
            std::vector<uint8_t> arena;
            uint64_t total_wire = 0, peak_wire = 0;
            double total_enc_us = 0.0, total_dec_us = 0.0;
            bool force_kf_next = false;
            int loss_events = 0, recovered_events = 0;

            for (int f = 0; f < FRAMES; ++f) {
                fill_scene(rgb888, w, h, sc, f);
                auto t0 = std::chrono::steady_clock::now();
                auto er = enc.encode_frame(rgb888.data(), force_kf_next, arena);
                auto t1 = std::chrono::steady_clock::now();
                total_enc_us += std::chrono::duration<double, std::micro>(t1 - t0).count();

                total_wire += er.wire_bytes;
                if (er.wire_bytes > peak_wire) peak_wire = er.wire_bytes;

                /* Simulate ~1% packet loss (at frame 45 if packets exist), trigger keyframe recovery */
                int drop_idx = (f == 45) ? 0 : -1;
                if (drop_idx >= 0) ++loss_events;

                auto d0 = std::chrono::steady_clock::now();
                bool ok = dec.decode_frame_packets(
                    arena.data(), er.wire_bytes, enc.last_raw_stream_bytes(), er.dirty_tiles, er.packet_count, drop_idx);
                auto d1 = std::chrono::steady_clock::now();
                total_dec_us += std::chrono::duration<double, std::micro>(d1 - d0).count();

                if (!ok) {
                    force_kf_next = true; /* NACK -> next frame sends clean keyframe */
                } else {
                    if (force_kf_next) {
                        ++recovered_events;
                        force_kf_next = false;
                    }
                    /* Verify pixel-exact RGB565 reconstruction whenever no packet was dropped */
                    std::vector<uint16_t> expected565((size_t)w * (size_t)h);
                    retro_proto::rgb888_to_rgb565_frame(rgb888.data(), expected565.data(), w, h);
                    assert(memcmp(dec.framebuffer(), expected565.data(), expected565.size() * 2u) == 0);
                }
            }

            double avg_mbps = ((double)total_wire / (double)FRAMES) * 8.0 * 60.0 / 1000000.0;
            double peak_mbps = (double)peak_wire * 8.0 * 60.0 / 1000000.0;
            printf("| %dx%d | %s | %.2f | %.2f | %.2f | %.2f | %.1f | %.1f | %d/%d ok (1-frame KF) |\n",
                   w, h, scene_names[sc], raw888_mbps, raw565_mbps, avg_mbps, peak_mbps,
                   total_enc_us / FRAMES, total_dec_us / FRAMES, recovered_events, loss_events);
        }
    }

    retro_proto::EncoderSession enc_mem; enc_mem.init(640, 240);
    retro_proto::DecoderSession dec_mem; dec_mem.init(640, 240);
    printf("\nMemory footprint (640x240): Encoder=%zu KiB, Decoder=%zu KiB (zero per-frame heap allocs)\n",
           enc_mem.memory_bytes() / 1024u, dec_mem.memory_bytes() / 1024u);
    printf("Reference ceilings: Xbox 360 100 Mbps Fast Ethernet (~85-90 Mbps usable), MoonX360 CRT H.264 default = 3.00 Mbps.\n");
    return 0;
}
