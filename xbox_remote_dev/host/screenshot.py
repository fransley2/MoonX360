"""Xenos tiled BGRA to PNG, only for a complete validated raw surface.

Tiling follows OXDK xbox360/tools/xbdm/xbdm.py (public domain).
"""
import struct
import zlib


def tiled_offset(x: int, y: int, width: int) -> int:
    bpp = 2
    macro = ((x >> 5) + (y >> 5) * (((width + 31) & ~31) >> 5)) << (bpp + 7)
    micro = ((x & 7) + ((y & 6) << 2)) << bpp
    offset = macro + ((micro & ~15) << 1) + (micro & 15)
    offset += ((y & 8) << (3 + bpp)) + ((y & 1) << 4)
    return ((((offset & ~511) << 3) + ((offset & 448) << 2) + (offset & 63)
             + ((y & 16) << 7) + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)) >> bpp)


def _chunk(tag: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)


def convert(raw: bytes, metadata: dict) -> bytes:
    width, height = metadata["width"], metadata["height"]
    if len(raw) != metadata["framebuffersize"]:
        raise ValueError("incomplete screenshot; PNG conversion refused")
    if not metadata["format"] & 0x100:
        raise ValueError("unsupported non-tiled screenshot")
    texels = len(raw) // 4
    scanlines = bytearray()
    for y in range(height):
        scanlines.append(0)
        for x in range(width):
            offset = tiled_offset(x, y, width)
            if offset >= texels:
                raise ValueError("tiled address outside framebuffer")
            i = offset * 4
            scanlines.extend((raw[i + 2], raw[i + 1], raw[i]))
    return (b"\x89PNG\r\n\x1a\n" +
            _chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            _chunk(b"IDAT", zlib.compress(scanlines, 6)) + _chunk(b"IEND", b""))
