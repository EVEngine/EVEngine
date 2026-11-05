#!/usr/bin/env python3
"""Rebuild the example-owned RGBA slash mask as an uncompressed TGA (no Pillow).

V=0 is the blade root (guard); V=1 is the tip. Energy is biased toward the tip
so the outer arc of the fan ribbon stays visible on the sword point.
"""

from __future__ import annotations

import math
import struct
from pathlib import Path


def alpha_at(u: float, v: float) -> float:
    # Tip-biased ribbon: dense bright band near V=1, soft falloff toward the guard.
    tip = v**1.35
    center = 0.72 + 0.10 * math.sin(u * math.tau) + 0.04 * math.sin(u * math.tau * 3.0)
    width = 0.34 + 0.22 * tip + 0.10 * (math.sin(math.pi * u) ** 0.5)
    edge = abs(v - center) / max(width, 1e-4)
    alpha = max(0.0, min(1.0, (1.0 - edge) * (7.0 + 6.0 * tip)))
    # Hot tip filament that reaches V=1 (do not leave the outer arc empty).
    filament = max(0.0, 1.0 - abs(v - 1.0) / 0.18) ** 1.2
    alpha = max(alpha, filament * (0.7 + 0.3 * tip))
    streak = 0.45 + 0.55 * max(0.0, math.sin(u * 58.0 + v * 7.0))
    cut = math.sin(v * 110.0 - u * 17.0) > 0.94 and math.sin(u * 29.0) > 0.25 and v < 0.85
    if cut:
        alpha *= 0.12
    alpha *= streak
    alpha *= min(1.0, u * 16.0, (1.0 - u) * 12.0)
    alpha *= 0.25 + 0.75 * tip
    return max(0.0, min(1.0, alpha))


def write_tga(path: Path, width: int, height: int) -> None:
    header = struct.pack(
        "<BBBHHBHHHHBB",
        0,
        0,
        2,
        0,
        0,
        0,
        0,
        0,
        width,
        height,
        32,
        0x28,
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
