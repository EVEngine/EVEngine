"""Generate original isometric garden props; Pillow only, no external assets."""

from pathlib import Path
from PIL import Image, ImageDraw
import random


def generate(destination):
    image = Image.new("RGBA", (384, 128))
    d = ImageDraw.Draw(image)
    # All four props share a feet anchor at (48,116) in their 96x128 cell.
    d.ellipse((19, 105, 78, 122), fill=(19, 35, 24, 65))
    d.rectangle((43, 60, 52, 116), fill=(91, 62, 34, 255))
    for y, w in ((9, 25), (34, 34), (61, 42)):
        d.polygon([(48, y), (48 - w, y + 40), (48 + w, y + 40)], fill=(35, 74 + y // 3, 51, 255))
        d.polygon([(48, y), (48, y + 40), (48 + w, y + 40)], fill=(24, 61 + y // 3, 43, 255))
        d.line([(48 - w + 7, y + 36), (48, y + 6)], fill=(69, 115, 62, 255), width=2)
    rng = random.Random(41)
    for i in range(22):
        x = 96 + rng.randrange(22, 76)
        y = rng.randrange(77, 111)
        radius = rng.randrange(7, 14)
        d.ellipse(
            (x - radius, y - radius, x + radius, y + radius),
            fill=(46 + rng.randrange(25), 91 + rng.randrange(32), 44, 255),
        )
    for i in range(8):
        x = 96 + rng.randrange(23, 74)
        y = rng.randrange(82, 102)
        d.ellipse((x - 2, y - 2, x + 2, y + 2), fill=(219, 178, 93, 255))
    d.polygon(
        [(211, 111), (217, 88), (246, 81), (270, 99), (266, 116), (231, 122)],
        fill=(83, 97, 91, 255),
    )
    d.polygon([(217, 88), (246, 81), (245, 104), (211, 111)], fill=(139, 149, 127, 255))
    d.polygon([(246, 81), (270, 99), (245, 104)], fill=(164, 165, 140, 255))
    d.polygon([(303, 107), (303, 66), (310, 63), (316, 67), (316, 112)], fill=(104, 72, 39, 255))
    d.polygon([(357, 118), (357, 77), (364, 74), (371, 78), (371, 123)], fill=(104, 72, 39, 255))
    for y in (78, 95):
        d.polygon([(308, y), (365, y + 11), (365, y + 18), (308, y + 7)], fill=(161, 119, 63, 255))
        d.line([(309, y), (364, y + 11)], fill=(199, 154, 84, 255), width=2)
    # Fine pigment variation keeps the props close to the existing textured tiles.
    pixels = image.load()
    for y in range(128):
        for x in range(384):
            r, g, b, a = pixels[x, y]
            if a != 255:
                continue
            noise = rng.randrange(-12, 13)
            highlight = 4 if (x * 3 + y * 7) % 13 < 3 else 0
            pixels[x, y] = (
                max(0, min(255, r + noise + highlight)),
                max(0, min(255, g + noise + highlight)),
                max(0, min(255, b + noise)),
                a,
            )
    destination.parent.mkdir(parents=True, exist_ok=True)
    image.save(destination)


if __name__ == "__main__":
    generate(Path(__file__).parent / "assets/garden.png")
