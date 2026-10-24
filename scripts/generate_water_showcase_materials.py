#!/usr/bin/env python3
"""Bake deterministic, seamless surface textures for the two water examples."""

import math
from pathlib import Path
import random

from PIL import Image

import utf8_stdio

ROOT = Path(__file__).resolve().parents[1]
SIZE = 256


def main() -> None:
    for kind, base in (("sand", (143, 126, 93)), ("stone", (83, 88, 81)),
                       ("grass", (43, 61, 27)), ("wood", (104, 79, 48))):
        rng = random.Random(2718)
        colors = Image.new("RGB", (SIZE, SIZE))
        heights = []
        for y in range(SIZE):
            for x in range(SIZE):
                u, v = x / SIZE * math.tau, y / SIZE * math.tau
                large = (math.sin(u * 3 + v * 2 + 1.7) + math.sin(u * 2 - v * 5 + 2.1)
                         + math.sin(u * 7 + v * 3 - 0.6)) / 3
                medium = (math.sin(u * 17 + v * 11 + 2.9) + math.sin(u * 23 - v * 19)) / 2
                grain = rng.uniform(-1, 1)
                h = large * 0.35 + medium * 0.22 + grain * 0.18
                if kind == "wood":
                    h = math.sin(u * 31 + math.sin(v) * 2 + math.sin(u * 3)) * 0.26 + grain * 0.08
                if kind == "sand" and grain > 0.94:
                    h -= 0.45
                heights.append(h)
                colors.putpixel((x, y), tuple(max(0, min(255, round(c + h * 38))) for c in base))
        normals = Image.new("RGB", (SIZE, SIZE))
        for y in range(SIZE):
            for x in range(SIZE):
                dx = (heights[y * SIZE + (x + 1) % SIZE] - heights[y * SIZE + (x - 1) % SIZE]) * 0.12
                dy = (heights[((y + 1) % SIZE) * SIZE + x] - heights[((y - 1) % SIZE) * SIZE + x]) * 0.12
                length = math.sqrt(dx * dx + dy * dy + 1)
                normals.putpixel((x, y), (round(127.5 * (1 - dx / length)),
                                          round(127.5 * (1 - dy / length)), round(127.5 * (1 + 1 / length))))
        for name in ("interactive-shallow-water", "stylized-water-shore"):
            target = ROOT / "examples" / name / "assets"
            target.mkdir(exist_ok=True)
            colors.save(target / f"{kind}.png")
            normals.save(target / f"{kind}-normal.png")
    print("Baked deterministic water-scene material textures")
    atlas = Image.new("RGB", (4, 1))
    for x, color in enumerate(((92, 61, 32), (111, 80, 42), (46, 68, 24), (66, 88, 32))):
        atlas.putpixel((x, 0), color)
    for name in ("interactive-shallow-water", "stylized-water-shore"):
        atlas.save(ROOT / "examples" / name / "assets" / "tree-atlas.png")
    scene = ROOT / "examples" / "stylized-water-shore" / "scene.nut"
    generated = ROOT / "examples" / "interactive-shallow-water" / "scene.nut"
    generated.write_text("// Generated shared scene helpers; edit stylized-water-shore/scene.nut.\n"
                         + scene.read_text(encoding="utf-8"), encoding="utf-8")


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    main()
