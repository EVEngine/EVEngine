"""Build the example-owned pixel masonry and layered landscape assets."""

from pathlib import Path
import random
from PIL import Image, ImageDraw


def main():
    root = Path(__file__).parent / "assets"
    rng = random.Random(20261111)
    atlas = Image.new("RGBA", (128, 128))
    d = ImageDraw.Draw(atlas)
    colors = [(49, 65, 66), (36, 45, 53), (73, 69, 65), (46, 47, 55), (58, 57, 64), (34, 37, 47)]
    for tile in range(16):
        ox, oy = tile % 4 * 32, tile // 4 * 32
        color = colors[tile % 6]
        d.rectangle((ox, oy, ox + 31, oy + 31), fill=(*color, 255))
        for row in range(4):
            offset = 0 if row % 2 else 8
            for col in range(-1, 3):
                x, y = ox + col * 16 + offset, oy + row * 8
                d.line((max(x, ox), y, min(x + 15, ox + 31), y), fill=(24, 31, 40, 255))
                if ox <= x <= ox + 31:
                    d.line((x, y, x, y + 7), fill=(24, 31, 40, 255))
        for _ in range(24):
            x, y = ox + rng.randrange(32), oy + rng.randrange(32)
            d.point((x, y), fill=tuple(min(255, c + rng.randrange(-9, 12)) for c in color) + (255,))
        if tile in (0, 2, 4):
            d.rectangle((ox, oy, ox + 31, oy + 3), fill=(56, 89, 69, 255))
            for x in range(ox, ox + 32, 3):
                d.line((x, oy + 3, x, oy + rng.randrange(5, 10)), fill=(44, 72, 58, 255))
    atlas.save(root / "tiles-detail.png")
    for name, base in [("forest", (35, 60, 76)), ("ruins", (47, 48, 68)), ("mine", (41, 36, 51))]:
        im = Image.new("RGB", (1280, 720))
        d = ImageDraw.Draw(im)
        for y in range(720):
            d.line((0, y, 1279, y), fill=tuple(int(c * (0.65 + 0.35 * y / 720)) for c in base))
        for layer in range(3):
            color = tuple(int(c * (0.8 - layer * 0.16)) for c in base)
            points = [(0, 720)]
            for x in range(-80, 1360, 80):
                points.append((x, 300 + layer * 80 + rng.randrange(-80, 70)))
            points.append((1280, 720))
            d.polygon(points, fill=color)
            for x in range(25 + layer * 40, 1280, 110 - layer * 15):
                y = 250 + layer * 85 + rng.randrange(50)
                d.rectangle((x, y, x + 12 + layer * 3, 720), fill=color)
                d.polygon([(x - 35, y + 85), (x + 6, y - 80), (x + 48, y + 85)], fill=color)
        im.save(root / ("background-" + name + "-detail.png"))


if __name__ == "__main__":
    main()
