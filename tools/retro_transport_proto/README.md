# MoonX360 Experimental Low-Res Alternative Transport Prototype

> Status: **Experimental Host-Only Research Prototype** — **NOT integrated into MoonX360**. FFmpeg H.264 remains MoonX360's production decoder/transport.

## Motivation & Bandwidth Reality

- Raw `320x240` RGB888 @ 60 Hz = `110.59 Mbps` (already exceeds Xbox 360's 100 Mbps Fast Ethernet physical wire rate).
- Raw `640x240` RGB888 @ 60 Hz = `221.18 Mbps`.
- Even uncompressed RGB565 (`320x240` = `73.73 Mbps`, `640x240` = `147.46 Mbps`) saturates or exceeds 100 Mbps Fast Ethernet on `640x240` and leaves minimal headroom against packet bursts or audio/control traffic on `320x240`.

## Prototype Design (`retro_proto.h`)

Clean-room C++ prototype evaluating whether a Groovy MiSTer-inspired uncompressed/lightweight pixel pipeline is viable over 100 Mbps Ethernet:

1. **RGB888 -> RGB565** preallocated frame buffer conversion (33% cut before compression).
2. **16x16 Dirty-Tile Detection** skipping unchanged regions.
3. **Clean-Room Fast LZ77 Hash-Table Block Codec** compressing static/UI/tiled pixel runs cleanly without GPL-2.0 or third-party LZ4 code.
4. **Sequenced UDP Framing (`MXR1`) + CRC32 + Keyframe NACK Recovery** simulating packet loss.

## Build & Run (Host MSVC)

```powershell
cl /nologo /O2 /EHsc projeto\tools\retro_transport_proto\retro_bench.cpp /Fe:retro_bench.exe
.\retro_bench.exe
```
