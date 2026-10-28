#!/usr/bin/env python3
"""Generate a high-contrast grid decal texture (stretch is obvious on sides)."""

from __future__ import annotations

from pathlib import Path

try:
    from PIL import Image, ImageDraw
except ImportError:
    raise SystemExit("need Pillow: pip install pillow")

SIZE = 256
CELLS = 8


def main() -> None:
    out = Path(__file__).resolve().parents[1] / "assets" / "grid_decal.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    img = Image.new("RGBA", (SIZE, SIZE), (20, 24, 32, 0))
    draw = ImageDraw.Draw(img)
    cell = SIZE // CELLS
    for y in range(CELLS):
        for x in range(CELLS):
            dark = (x + y) % 2 == 0
            color = (220, 80, 40, 230) if dark else (240, 210, 60, 210)
            x0, y0 = x * cell, y * cell
            draw.rectangle([x0, y0, x0 + cell - 1, y0 + cell - 1], fill=color)
    # Bold cross so axis orientation is readable after projection.
    mid = SIZE // 2
    draw.rectangle([mid - 6, 16, mid + 6, SIZE - 17], fill=(30, 200, 255, 255))
    draw.rectangle([16, mid - 6, SIZE - 17, mid + 6], fill=(30, 200, 255, 255))
    draw.ellipse([mid - 18, mid - 18, mid + 18, mid + 18], fill=(255, 255, 255, 255))
    img.save(out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
