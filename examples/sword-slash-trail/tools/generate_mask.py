#!/usr/bin/env python3
"""Rebuild the example-owned RGBA slash mask as an uncompressed TGA (no Pillow)."""

from __future__ import annotations

import math
import struct
from pathlib import Path


def alpha_at(u: float, v: float) -> float:
    center = 0.5 + 0.08 * math.sin(u * math.tau) + 0.03 * math.sin(u * math.tau * 4.0)
    width = 0.28 + 0.18 * (math.sin(math.pi * u) ** 0.55)
    edge = abs(v - center) / max(width, 1e-4)
    alpha = max(0.0, min(1.0, (1.0 - edge) * 9.0))
    # Long bright streaks along the arc; sparse cuts keep the ribbon from reading as a solid sheet.
    streak = 0.55 + 0.45 * max(0.0, math.sin(u * 54.0 + v * 9.0))
    cut = math.sin(v * 120.0 - u * 17.0) > 0.93 and math.sin(u * 29.0) > 0.2
    if cut:
        alpha *= 0.08
    alpha *= streak
    alpha *= min(1.0, u * 14.0, (1.0 - u) * 10.0)
    return max(0.0, min(1.0, alpha))


def write_tga(path: Path, width: int, height: int) -> None:
    # Uncompressed true-color TGA with alpha (type 2, 32 bpp).
    header = struct.pack(
        "<BBBHHBHHHHBB",
        0,  # id length
        0,  # no color map
        2,  # uncompressed true-color
        0,
        0,
        0,
        0,
        0,  # x/y origin
        width,
        height,
        32,  # bpp
        0x28,  # top-left origin + 8-bit alpha
    )
    raw = bytearray()
    for y in range(height):
        v = y / max(height - 1, 1)
        for x in range(width):
            u = x / max(width - 1, 1)
            a = round(alpha_at(u, v) * 255)
            raw.extend((255, 255, 255, a))  # TGA B,G,R,A
    path.write_bytes(header + raw)


def main() -> None:
    out = Path(__file__).resolve().parents[1] / "fx" / "assets" / "slash-mask.tga"
    out.parent.mkdir(parents=True, exist_ok=True)
    write_tga(out, 512, 128)
    print(f"wrote {out} ({out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
