"""Rebuild the bundled procedural dust card; Pillow is an authoring dependency only."""
import math
from pathlib import Path

from PIL import Image


def main():
    size = 128
    image = Image.new("RGBA", (size, size))
    pixels = image.load()
    for y in range(size):
        for x in range(size):
            u, v = (x + 0.5) / size * 2 - 1, (y + 0.5) / size * 2 - 1
            radius = math.sqrt(u * u + v * v)
            edge = max(0, 1 - radius)
            cloud = 0.72 + 0.14 * math.sin(u * 19 + math.sin(v * 9))
            cloud += 0.09 * math.sin(v * 27 + math.cos(u * 15))
            alpha = int(255 * edge ** 1.8 * max(0, cloud))
            pixels[x, y] = (255, 255, 255, alpha)
    image.save(Path(__file__).with_name("dust.tga"))
    lava = Image.new("RGB", (size, size))
    pixels = lava.load()
    for y in range(size):
        for x in range(size):
            u, v = x / size * math.tau, y / size * math.tau
            noise = math.sin(u * 3 + math.sin(v * 4)) * math.cos(v * 5 + math.sin(u * 2))
            heat = max(0, min(1, 0.5 + noise * 0.5))
            pixels[x, y] = (int(100 + 155 * heat), int(10 + 160 * heat ** 2), int(3 + 25 * heat ** 4))
    lava.save(Path(__file__).with_name("lava.tga"))


if __name__ == "__main__":
    main()
