"""Build preview and EVEngine terrain atlas from the supplied C14 45-degree set."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw


TILE_WIDTH = 80
TILE_HEIGHT = 40


def numeric_pngs(root: Path) -> list[Path]:
    return sorted(
        (path for path in root.glob("*.png") if path.stem.isdigit()),
        key=lambda path: int(path.stem),
    )


def make_contact_sheet(files: list[Path], output: Path) -> None:
    thumb_width, thumb_height = 160, 120
    columns = 6
    rows = (len(files) + columns - 1) // columns
    sheet = Image.new("RGB", (columns * thumb_width, rows * (thumb_height + 20)), "#181a1d")
    draw = ImageDraw.Draw(sheet)
    for index, path in enumerate(files):
        image = Image.open(path).convert("RGB")
        image.thumbnail((thumb_width, thumb_height), Image.Resampling.LANCZOS)
        x = index % columns * thumb_width + (thumb_width - image.width) // 2
        y = index // columns * (thumb_height + 20)
        sheet.paste(image, (x, y))
        draw.text((index % columns * thumb_width + 5, y + thumb_height + 2), path.stem, fill="white")
    output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output)


def find_diamond_bbox(image: Image.Image) -> tuple[int, int, int, int]:
    # All source previews are 800x600 and place the material diamond around the
    # same centre. Sample slightly inside the visible diamond: scaling that
    # inner region back to the tile mask overscans the antialiased grey edge and
    # prevents hairline seams between neighbouring isometric cells.
    return (
        round(image.width * 0.06),
        round(image.height * 0.085),
        round(image.width * 0.94),
        round(image.height * 0.915),
    )


def extract_tile(path: Path) -> Image.Image:
    image = Image.open(path).convert("RGB")
    sampled = image.crop(find_diamond_bbox(image)).resize(
        (TILE_WIDTH, TILE_HEIGHT), Image.Resampling.LANCZOS
    ).convert("RGB")
    source_pixels = sampled.load()
    tile = Image.new("RGBA", (TILE_WIDTH, TILE_HEIGHT), (0, 0, 0, 0))
    pixels = tile.load()
    centre_x = (TILE_WIDTH - 1) * 0.5
    centre_y = (TILE_HEIGHT - 1) * 0.5
    for y in range(TILE_HEIGHT):
        for x in range(TILE_WIDTH):
            distance = abs((x + 0.5 - TILE_WIDTH * 0.5) / (TILE_WIDTH * 0.5))
            distance += abs((y + 0.5 - TILE_HEIGHT * 0.5) / (TILE_HEIGHT * 0.5))
            # One-pixel overlap hides rasterization cracks between neighbouring
            # 80x40 diamonds while still leaving the atlas corners transparent.
            if distance <= 1.06:
                sample_x, sample_y = x, y
                if distance > 0.90:
                    inward = 0.90 / distance
                    sample_x = round(centre_x + (x - centre_x) * inward)
                    sample_y = round(centre_y + (y - centre_y) * inward)
                r, g, b = source_pixels[sample_x, sample_y]
                pixels[x, y] = (r, g, b, 255)
    return tile


def build_atlas(root: Path, selections: list[str], output: Path) -> None:
    atlas = Image.new("RGBA", (TILE_WIDTH * len(selections), TILE_HEIGHT), (0, 0, 0, 0))
    for index, stem in enumerate(selections):
        atlas.alpha_composite(extract_tile(root / f"{stem}.png"), (index * TILE_WIDTH, 0))
    output.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source_dir", type=Path)
    parser.add_argument("--contact-sheet", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--select", nargs="+", help="Four numeric file stems")
    args = parser.parse_args()
    files = numeric_pngs(args.source_dir)
    if args.contact_sheet:
        make_contact_sheet(files, args.contact_sheet)
        print(f"wrote contact sheet with {len(files)} candidates: {args.contact_sheet}")
    if args.output:
        if not args.select or len(args.select) != 4:
            parser.error("--output requires exactly four --select values")
        build_atlas(args.source_dir, args.select, args.output)
        print(f"wrote terrain atlas from {args.select}: {args.output}")


if __name__ == "__main__":
    main()
