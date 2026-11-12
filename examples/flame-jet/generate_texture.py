"""Generate the analytic radial alpha texture used by the flame layers."""

from pathlib import Path
from PIL import Image


def main():
    image = Image.new("RGBA", (64, 64))
    for y in range(64):
        for x in range(64):
            radius2 = ((x - 31.5) / 31.5) ** 2 + ((y - 31.5) / 31.5) ** 2
            alpha = int(max(0.0, 1.0 - radius2) ** 2 * 255)
            image.putpixel((x, y), (255, 255, 255, alpha))
    image.save(Path(__file__).parent / "textures" / "soft-plume.png")


if __name__ == "__main__":
    main()
