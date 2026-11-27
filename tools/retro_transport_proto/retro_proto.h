#pragma once
/*
 * MoonX360 Experimental Low-Res Alternative Transport Prototype (Host-Only)
 * Clean-room implementation for CPU/bandwidth/loss feasibility analysis vs 100 Mbps Ethernet.
 * NOT integrated into MoonX360 console production pipeline (FFmpeg H.264 remains default).
 */

#include <stdint.h>
#include <string.h>
#include <vector>

namespace retro_proto {

static const uint32_t MAGIC = 0x4D585231u; /* 'MXR1' */
static const uint16_t FLAG_KEYFRAME = 0x0001u;
static const uint16_t FLAG_LZ77     = 0x0002u;
static const size_t TILE_SIZE       = 16u;
static const size_t MTU_PAYLOAD     = 1200u;

struct PacketHeader {
    uint32_t magic;
    uint32_t frame_id;
    uint16_t flags;
    uint16_t width;
    uint16_t height;
    uint16_t packet_idx;
    uint16_t packet_count;
    uint16_t payload_bytes;
    uint32_t crc32;
};

struct DirtyTileHeader {
    uint16_t tx;
    uint16_t ty;
};

static inline uint32_t calc_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static inline void rgb888_to_rgb565_frame(const uint8_t *rgb888, uint16_t *rgb565, int width, int height) {
    const size_t pixels = (size_t)width * (size_t)height;
    for (size_t i = 0; i < pixels; ++i) {
        uint16_t r = (uint16_t)(rgb888[i * 3 + 0] >> 3);
        uint16_t g = (uint16_t)(rgb888[i * 3 + 1] >> 2);
        uint16_t b = (uint16_t)(rgb888[i * 3 + 2] >> 3);
        rgb565[i] = (uint16_t)((r << 11) | (g << 5) | b);
    }
}

/*
 * Clean-room fast byte-stream LZ77 codec:
 * Format: control byte per token group OR typed chunks:
 * - Tag byte b:
 *   - b < 128 : literal run of (b + 1) raw bytes follow
 *   - b >= 128: back-reference match: len = (b - 128) + 3 (3..130 bytes), followed by 2-byte LE distance (1..65535)
 */
static inline size_t lz77_compress(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap, uint32_t *hash_tab) {
    if (src_len == 0 || dst_cap < 4) return 0;
    const size_t HASH_BITS = 12;
    const size_t HASH_SIZE = (1u << HASH_BITS);
    for (size_t i = 0; i < HASH_SIZE; ++i) hash_tab[i] = 0xFFFFFFFFu;

    size_t sp = 0;
    size_t dp = 0;
    size_t lit_start = 0;

    auto flush_literals = [&](size_t upto) -> bool {
        while (lit_start < upto) {
            size_t run = upto - lit_start;
            if (run > 128) run = 128;
            if (dp + 1 + run > dst_cap) return false;
            dst[dp++] = (uint8_t)(run - 1);
            memcpy(dst + dp, src + lit_start, run);
            dp += run;
            lit_start += run;
        }
        return true;
    };

    while (sp + 3 <= src_len) {
        uint32_t v = (uint32_t)src[sp] | ((uint32_t)src[sp + 1] << 8) | ((uint32_t)src[sp + 2] << 16);
        uint32_t h = (v * 2654435761u) >> (32 - HASH_BITS);
        uint32_t ref = hash_tab[h];
        hash_tab[h] = (uint32_t)sp;

        if (ref != 0xFFFFFFFFu && sp > ref && (sp - ref) <= 65535u &&
            memcmp(src + ref, src + sp, 3) == 0) {
            size_t dist = sp - ref;
            size_t mlen = 3;
            size_t max_len = src_len - sp;
            if (max_len > 130) max_len = 130;
            while (mlen < max_len && src[ref + mlen] == src[sp + mlen]) {
                ++mlen;
            }
            if (!flush_literals(sp)) return 0;
            if (dp + 3 > dst_cap) return 0;
            dst[dp++] = (uint8_t)(128u + (mlen - 3u));
            dst[dp++] = (uint8_t)(dist & 0xFFu);
            dst[dp++] = (uint8_t)((dist >> 8) & 0xFFu);
            sp += mlen;
            lit_start = sp;
        } else {
            ++sp;
        }
    }

    if (!flush_literals(src_len)) return 0;
    return dp;
}

static inline bool lz77_decompress(const uint8_t *src, size_t src_len, uint8_t *dst, size_t expected_len) {
    size_t sp = 0;
    size_t dp = 0;
    while (sp < src_len && dp < expected_len) {
        uint8_t tag = src[sp++];
        if (tag < 128u) {
            size_t run = (size_t)tag + 1u;
            if (sp + run > src_len || dp + run > expected_len) return false;
            memcpy(dst + dp, src + sp, run);
            sp += run;
            dp += run;
        } else {
            size_t mlen = (size_t)(tag - 128u) + 3u;
            if (sp + 2 > src_len) return false;
            size_t dist = (size_t)src[sp] | ((size_t)src[sp + 1] << 8);
            sp += 2;
            if (dist == 0 || dist > dp || dp + mlen > expected_len) return false;
            for (size_t i = 0; i < mlen; ++i) {
                dst[dp + i] = dst[dp - dist + i];
            }
            dp += mlen;
        }
    }
    return dp == expected_len && sp == src_len;
}

class EncoderSession {
public:
    void init(int w, int h) {
        width = w;
        height = h;
        tiles_x = (w + (int)TILE_SIZE - 1) / (int)TILE_SIZE;
        tiles_y = (h + (int)TILE_SIZE - 1) / (int)TILE_SIZE;
        size_t px = (size_t)w * (size_t)h;
        curr565.assign(px, 0);
        prev565.assign(px, 0);
        /* Preallocated working buffers -> zero per-frame heap allocations during stream. */
        size_t max_tile_stream = (size_t)tiles_x * (size_t)tiles_y * (4u + TILE_SIZE * TILE_SIZE * 2u) + 64u;
        raw_stream.resize(max_tile_stream);
        comp_stream.resize(max_tile_stream + max_tile_stream / 8u + 128u);
        hash_tab.resize(1u << 12);
        frame_counter = 0;
    }

    size_t memory_bytes() const {
        return curr565.capacity() * sizeof(uint16_t) +
               prev565.capacity() * sizeof(uint16_t) +
               raw_stream.capacity() +
               comp_stream.capacity() +
               hash_tab.capacity() * sizeof(uint32_t);
    }

    struct EncodeResult {
        size_t wire_bytes;
        uint16_t packet_count;
        uint16_t dirty_tiles;
        bool keyframe;
        bool used_lz77;
    };

    EncodeResult encode_frame(const uint8_t *rgb888, bool force_keyframe, std::vector<uint8_t> &packet_arena) {
        ++frame_counter;
        rgb888_to_rgb565_frame(rgb888, curr565.data(), width, height);

        size_t raw_pos = 0;
        uint16_t dirty = 0;
        const bool is_kf = force_keyframe || (frame_counter == 1);

        for (int ty = 0; ty < tiles_y; ++ty) {
            for (int tx = 0; tx < tiles_x; ++tx) {
                bool tile_dirty = is_kf;
                if (!tile_dirty) {
                    for (int y = 0; y < (int)TILE_SIZE && !tile_dirty; ++y) {
                        int py = ty * (int)TILE_SIZE + y;
                        if (py >= height) break;
                        int px = tx * (int)TILE_SIZE;
                        int cw = (px + (int)TILE_SIZE <= width) ? (int)TILE_SIZE : (width - px);
                        size_t off = (size_t)py * (size_t)width + (size_t)px;
                        if (memcmp(&curr565[off], &prev565[off], (size_t)cw * 2u) != 0) {
                            tile_dirty = true;
                        }
                    }
                }
                if (!tile_dirty) continue;

                DirtyTileHeader th;
                th.tx = (uint16_t)tx;
                th.ty = (uint16_t)ty;
                memcpy(raw_stream.data() + raw_pos, &th, sizeof(th));
                raw_pos += sizeof(th);

                for (int y = 0; y < (int)TILE_SIZE; ++y) {
                    int py = ty * (int)TILE_SIZE + y;
                    uint16_t *dst_row = (uint16_t *)(raw_stream.data() + raw_pos);
                    if (py < height) {
                        int px = tx * (int)TILE_SIZE;
                        int cw = (px + (int)TILE_SIZE <= width) ? (int)TILE_SIZE : (width - px);
                        memcpy(dst_row, &curr565[(size_t)py * (size_t)width + (size_t)px], (size_t)cw * 2u);
                        if (cw < (int)TILE_SIZE) memset(dst_row + cw, 0, (size_t)(TILE_SIZE - cw) * 2u);
                    } else {
                        memset(dst_row, 0, TILE_SIZE * 2u);
                    }
                    raw_pos += TILE_SIZE * 2u;
                }
                ++dirty;
            }
        }

        memcpy(prev565.data(), curr565.data(), curr565.size() * sizeof(uint16_t));

        uint16_t flags = is_kf ? FLAG_KEYFRAME : 0u;
        const uint8_t *send_buf = raw_stream.data();
        size_t send_len = raw_pos;

        if (raw_pos > 0) {
            size_t comp_len = lz77_compress(raw_stream.data(), raw_pos, comp_stream.data(), comp_stream.size(), hash_tab.data());
            if (comp_len > 0 && comp_len < raw_pos) {
                flags |= FLAG_LZ77;
                send_buf = comp_stream.data();
                send_len = comp_len;
            }
        }

        /* Serialize at least 1 tiny heartbeat/meta packet when 0 dirty tiles, or MTU chunks. */
        uint16_t pkt_cnt = (send_len == 0) ? 1u : (uint16_t)((send_len + MTU_PAYLOAD - 1u) / MTU_PAYLOAD);
        size_t needed = (size_t)pkt_cnt * sizeof(PacketHeader) + send_len + 4u;
        if (packet_arena.size() < needed) packet_arena.resize(needed);

        size_t arena_pos = 0;
        size_t src_off = 0;
        for (uint16_t p = 0; p < pkt_cnt; ++p) {
            size_t chunk = (send_len > src_off) ? (send_len - src_off) : 0u;
            if (chunk > MTU_PAYLOAD) chunk = MTU_PAYLOAD;
            PacketHeader hdr{};
            hdr.magic = MAGIC;
            hdr.frame_id = frame_counter;
            hdr.flags = flags;
            hdr.width = (uint16_t)width;
            hdr.height = (uint16_t)height;
            hdr.packet_idx = p;
            hdr.packet_count = pkt_cnt;
            hdr.payload_bytes = (uint16_t)chunk;
            /* Prepend 4-byte uncompressed raw_pos in packet 0 payload or encode directly */
            hdr.crc32 = chunk ? calc_crc32(send_buf + src_off, chunk) : 0u;
            memcpy(packet_arena.data() + arena_pos, &hdr, sizeof(hdr));
            arena_pos += sizeof(hdr);
            if (chunk) {
                memcpy(packet_arena.data() + arena_pos, send_buf + src_off, chunk);
                arena_pos += chunk;
            }
            src_off += chunk;
        }

        EncodeResult res{};
        res.wire_bytes = arena_pos;
        res.packet_count = pkt_cnt;
        res.dirty_tiles = dirty;
        res.keyframe = is_kf;
        res.used_lz77 = (flags & FLAG_LZ77) != 0;
        last_raw_bytes = raw_pos;
        return res;
    }

    size_t last_raw_stream_bytes() const { return last_raw_bytes; }

private:
    int width = 0, height = 0, tiles_x = 0, tiles_y = 0;
    uint32_t frame_counter = 0;
    size_t last_raw_bytes = 0;
    std::vector<uint16_t> curr565, prev565;
    std::vector<uint8_t> raw_stream, comp_stream;
    std::vector<uint32_t> hash_tab;
};

class DecoderSession {
public:
    void init(int w, int h) {
        width = w;
        height = h;
        fb565.assign((size_t)w * (size_t)h, 0);
        size_t max_bytes = (size_t)((w + 15) / 16) * (size_t)((h + 15) / 16) * (4u + TILE_SIZE * TILE_SIZE * 2u) + 64u;
        rx_buf.resize(max_bytes);
        decomp_buf.resize(max_bytes);
    }

    size_t memory_bytes() const {
        return fb565.capacity() * sizeof(uint16_t) + rx_buf.capacity() + decomp_buf.capacity();
    }

    bool decode_frame_packets(const uint8_t *arena, size_t arena_bytes, size_t expected_raw_bytes, uint16_t dirty_tiles,
                              uint16_t packet_count, int drop_packet_idx) {
        if (drop_packet_idx >= 0 && drop_packet_idx < (int)packet_count) {
            return false; /* Simulated UDP packet loss triggers NACK/next-keyframe recovery. */
        }
        size_t pos = 0;
        size_t rx_len = 0;
        uint16_t flags = 0;
        for (uint16_t p = 0; p < packet_count; ++p) {
            if (pos + sizeof(PacketHeader) > arena_bytes) return false;
            PacketHeader hdr;
            memcpy(&hdr, arena + pos, sizeof(hdr));
            pos += sizeof(hdr);
            if (hdr.magic != MAGIC || pos + hdr.payload_bytes > arena_bytes) return false;
            flags = hdr.flags;
            if (hdr.payload_bytes > 0) {
                if (calc_crc32(arena + pos, hdr.payload_bytes) != hdr.crc32) return false;
                memcpy(rx_buf.data() + rx_len, arena + pos, hdr.payload_bytes);
                rx_len += hdr.payload_bytes;
                pos += hdr.payload_bytes;
            }
        }
        if (dirty_tiles == 0) return true;

        const uint8_t *tiles_ptr = rx_buf.data();
        if (flags & FLAG_LZ77) {
            if (!lz77_decompress(rx_buf.data(), rx_len, decomp_buf.data(), expected_raw_bytes)) return false;
            tiles_ptr = decomp_buf.data();
        } else if (rx_len != expected_raw_bytes) {
            return false;
        }

        const size_t stride = sizeof(DirtyTileHeader) + TILE_SIZE * TILE_SIZE * 2u;
        for (uint16_t i = 0; i < dirty_tiles; ++i) {
            DirtyTileHeader th;
            memcpy(&th, tiles_ptr + i * stride, sizeof(th));
            const uint16_t *src_tile = (const uint16_t *)(tiles_ptr + i * stride + sizeof(th));
            for (int y = 0; y < (int)TILE_SIZE; ++y) {
                int py = (int)th.ty * (int)TILE_SIZE + y;
                if (py >= height) break;
                int px = (int)th.tx * (int)TILE_SIZE;
                int cw = (px + (int)TILE_SIZE <= width) ? (int)TILE_SIZE : (width - px);
                memcpy(&fb565[(size_t)py * (size_t)width + (size_t)px], src_tile + y * TILE_SIZE, (size_t)cw * 2u);
            }
        }
        return true;
    }

    const uint16_t *framebuffer() const { return fb565.data(); }

private:
    int width = 0, height = 0;
    std::vector<uint16_t> fb565;
    std::vector<uint8_t> rx_buf, decomp_buf;
};

} /* namespace retro_proto */
