"""Rebuild example-owned, replaceable RGBA blade masks. Requires Pillow."""

import math
from pathlib import Path

from PIL import Image


def generate() -> None:
    output = Path(__file__).resolve().parents[1] / "fx" / "assets"
    for name, ink in [("wind-blade", False), ("ink-streak", True)]:
        image = Image.new("RGBA", (512, 128))
        pixels = image.load()
        for x in range(512):
            u = x / 511
            for y in range(128):
                v = y / 127
                center = 0.5 + 0.13 * math.sin(u * math.tau) + 0.035 * math.sin(u * math.tau * 5)
                width = 0.22 + 0.14 * math.sin(math.pi * u) ** 0.5
                edge = abs(v - center) / width
                alpha = max(0, min(1, (1 - edge) * 8))
                if ink:
                    grain = (math.sin(u * 103 + v * 91) + math.sin(u * 211 - v * 31)) * 0.022
                    alpha = max(0, min(1, (1 - edge + grain) * 16))
                    cut = math.sin(v * 145 + u * 13) > 0.82 and math.sin(u * 37 + v * 9) > 0.15
                else:
                    cut = math.sin(v * 92 - u * 23) > 0.95 and math.sin(u * 19) > 0.05
                if cut:
                    alpha *= 0.12
                alpha *= min(1, u * 12, (1 - u) * 12)
                pixels[x, y] = (255, 255, 255, round(alpha * 255))
        image.save(output / f"{name}.tga")


if __name__ == "__main__":
    generate()
