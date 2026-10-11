"""Strict, bounded reader for uncompressed DX10 DDS source textures from Unreal.

No GPU format guessing or lossy conversion: HDR bytes remain IEEE binary16.
The returned metadata describes every mip and array/cube surface in file order.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass


class TextureFormatError(ValueError):
    """DDS cannot be consumed without changing its documented interpretation."""


@dataclass(frozen=True)
class DdsTexture:
    width: int
    height: int
    depth: int
    layers: int
    mip_count: int
    dimension: str
    encoding: str
    srgb: bool
    subresources: tuple[dict, ...]
    pixels: bytes


# DXGI_FORMAT value -> canonical encoding, bytes/texel, source sRGB, swizzle BGRA.
FORMATS = {
    2: ("rgba32f", 16, False, False),
    10: ("rgba16f", 8, False, False),
    11: ("rgba16unorm", 8, False, False),
    28: ("rgba8", 4, False, False),
    29: ("rgba8", 4, True, False),
    56: ("r16unorm", 2, False, False),
    61: ("r8", 1, False, False),
    87: ("rgba8", 4, False, True),
    91: ("rgba8", 4, True, True),
}


def decode_dds(data: bytes, maximum_bytes: int = 512 * 1024 * 1024) -> DdsTexture:
    """Validate layout before allocation; retain all mip/surface payloads exactly."""
    if maximum_bytes <= 0 or len(data) > maximum_bytes:
        raise TextureFormatError("DDS exceeds configured input budget")
    if len(data) < 148 or data[:4] != b"DDS ":
        raise TextureFormatError("expected DDS with a complete DX10 header")
    size, flags, height, width, pitch, depth, mip_count = struct.unpack_from("<7I", data, 4)
    pf_size, pf_flags = struct.unpack_from("<2I", data, 76)
    if size != 124 or pf_size != 32 or not pf_flags & 4 or data[84:88] != b"DX10":
        raise TextureFormatError("only DX10 DDS headers are supported")
    required_flags = 0x1 | 0x2 | 0x4 | 0x1000
    if flags & required_flags != required_flags or not width or not height:
        raise TextureFormatError("missing DDS dimensions or required flags")
    dxgi, dimension, misc, array_size, alpha_mode = struct.unpack_from("<5I", data, 128)
    if dxgi not in FORMATS:
        raise TextureFormatError(f"unsupported DXGI source format {dxgi}; no lossy decode")
    if dimension not in (3, 4) or not array_size or misc & ~4 or alpha_mode & ~7:
        raise TextureFormatError("unsupported DDS dimension, flags, or array layout")
    encoding, texel_bytes, srgb, bgra = FORMATS[dxgi]
    if dimension == 4:
        if not depth or not flags & 0x800000 or array_size != 1 or misc:
            raise TextureFormatError("3D textures require one volume, a depth, and no cube flag")
        kind, layers = "3d", 1
    else:
        if depth not in (0, 1) or flags & 0x800000:
            raise TextureFormatError("2D DDS has an incompatible depth")
        depth = 1
        kind, layers = ("cube", array_size * 6) if misc & 4 else ("2d", array_size)
        if kind == "cube" and width != height:
            raise TextureFormatError("cubemap faces must be square")
    mip_count = max(mip_count, 1)
    if mip_count > max(width, height, depth).bit_length():
        raise TextureFormatError("too many mip levels for texture extent")
    if flags & 0x8 and pitch not in (0, width * texel_bytes):
        raise TextureFormatError("padded DDS rows are not supported")
    payload_size = len(data) - 148
    # Bound layer iteration by actual payload before building descriptor objects.
    if layers > payload_size // texel_bytes:
        raise TextureFormatError("DDS surface count exceeds payload")
    offset, subresources = 0, []
    for layer in range(layers):
        for mip in range(mip_count):
            w, h, d = max(1, width >> mip), max(1, height >> mip), max(1, depth >> mip)
            size_bytes = w * h * d * texel_bytes
            if size_bytes > payload_size - offset:
                raise TextureFormatError("DDS payload is truncated")
            subresources.append({"layer": layer, "mip": mip, "width": w, "height": h,
                                 "depth": d, "offset": offset, "byteLength": size_bytes})
            offset += size_bytes
    if offset != payload_size:
        raise TextureFormatError("DDS payload has unrecognized trailing data")
    pixels = data[148:]
    if bgra:
        converted = bytearray(pixels)
        converted[0::4], converted[2::4] = pixels[2::4], pixels[0::4]
        pixels = bytes(converted)
    return DdsTexture(width, height, depth, layers, mip_count, kind, encoding,
                      srgb, tuple(subresources), pixels)
