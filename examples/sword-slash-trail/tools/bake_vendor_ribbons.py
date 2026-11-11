#!/usr/bin/env python3
"""Bake downloaded crescent slash PNGs into tip-aligned ribbon strips.

Ribbon layout matches the example trail mesh:
  U = arc progress (tail → head)
  V = blade root → tip  (tip stays hot)

Requires Pillow. Sources stay in fx/assets/vendor/; outputs are *-ribbon.png.
"""

from __future__ import annotations

from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1] / "fx" / "assets" / "vendor"
JOBS = [
    ("oga-slash-blue.png", "oga-slash-blue-ribbon.png"),
    ("oga-slash-gray.png", "oga-slash-gray-ribbon.png"),
    ("fgs-slash-trail.png", "fgs-slash-trail-ribbon.png"),
    ("fgs-sword-slash.png", "fgs-sword-slash-ribbon.png"),
]


def longest_run(mask_col: np.ndarray) -> tuple[int, int] | None:
    best = None
    start = None
    for index, value in enumerate(mask_col.tolist() + [False]):
        if value and start is None:
            start = index
        elif not value and start is not None:
            run = (start, index - 1)
            if best is None or (run[1] - run[0]) > (best[1] - best[0]):
                best = run
            start = None
    return best


def bake(src_name: str, out_name: str, width: int = 512, height: int = 128) -> None:
    image = np.asarray(Image.open(ROOT / src_name).convert("RGBA")).astype(np.float32) / 255.0
    rows, cols, _ = image.shape
    alpha = image[:, :, 3]
    luminance = image[:, :, :3].max(axis=2)
    mask = alpha > 0.08
    out = np.zeros((height, width, 4), dtype=np.float32)

    for xi in range(width):
        x = int(xi / max(width - 1, 1) * (cols - 1))
        run = longest_run(mask[:, x])
        if run is None:
            continue
        y0, y1 = run
        if y1 <= y0:
            continue
        top = float(luminance[y0 : min(y0 + 2, y1 + 1), x].mean())
        bot = float(luminance[max(y1 - 1, y0) : y1 + 1, x].mean())
        tip_is_top = top >= bot
        for vi in range(height):
            t = vi / max(height - 1, 1)
            y = y1 + (y0 - y1) * t if tip_is_top else y0 + (y1 - y0) * t
            y = float(np.clip(y, 0.0, rows - 1.001))
            y0i = int(y)
            y1i = min(y0i + 1, rows - 1)
            frac = y - y0i
            pixel = image[y0i, x] * (1.0 - frac) + image[y1i, x] * frac
            pixel = pixel.copy()
            gain = 0.4 + 0.9 * t
            pixel[:3] = np.clip(pixel[:3] * gain * 1.15, 0.0, 1.0)
            pixel[3] = float(np.clip(pixel[3] * gain, 0.0, 1.0))
            out[vi, xi] = pixel

    Image.fromarray((out * 255.0).astype(np.uint8), "RGBA").save(ROOT / out_name)
    print(f"wrote {out_name}")


def main() -> None:
    for src, dst in JOBS:
        bake(src, dst)


if __name__ == "__main__":
    main()
