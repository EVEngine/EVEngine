"""Compose unretouched, fixed-camera engine captures next to the AI target."""

import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--reference", type=Path, required=True)
parser.add_argument("--before", type=Path, required=True)
parser.add_argument("--after", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
reference = Image.open(args.reference).convert("RGB")
before = Image.open(args.before).convert("RGB")
after = Image.open(args.after).convert("RGB")
if before.size != (1200, 1280) or after.size != before.size:
    raise ValueError("Comparison requires the fixed 1200x1280 studio camera captures")
args.output.mkdir(parents=True, exist_ok=True)
font_files = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
]


def font(size):
    for path in font_files:
        if Path(path).is_file():
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


names = [
    "OAK WOOD",
    "BRUSHED METAL",
    "RUBBER",
    "WOVEN CLOTH",
    "PLASTIC",
    "STEEL",
    "GLASS",
    "PAPER",
    "ALUMINUM",
    "CAST IRON",
    "CEMENT",
    "PAINT",
    "MARBLE",
]
rows = [174, 439, 705, 940]


def render_tile(im, i):
    x, y = 137 + (i % 4) * 235, rows[i // 4]
    return im.crop((x, y, x + 225, y + 225))


def reference_tile(i):
    x, y = (i % 4) * reference.width / 4, (i // 4) * reference.height / 4
    return reference.crop(
        (round(x), round(y), round(x + reference.width / 4), round(y + reference.height / 4))
    )


selected = [0, 1, 6, 12]
board = Image.new("RGB", (1000, len(selected) * 350 + 105), (22, 24, 28))
draw = ImageDraw.Draw(board)
for x, text in [(15, "AI REFERENCE"), (345, "FIXED STAGE / BEFORE"), (675, "FIXED STAGE / AFTER")]:
    draw.text((x, 20), text, font=font(19), fill="white")
for row, i in enumerate(selected):
    y = 60 + row * 350
    for x, tile in [
        (15, reference_tile(i)),
        (345, render_tile(before, i)),
        (675, render_tile(after, i)),
    ]:
        board.paste(tile.resize((300, 300), Image.Resampling.LANCZOS), (x, y))
    draw.text((675, y + 310), names[i], font=font(20), fill="white")
draw.text(
    (15, board.height - 35),
    "GPU columns share camera, studio and geometry. Reference uses separate lighting.",
    font=font(16),
    fill=(170, 178, 190),
)
board.save(args.output / "fixed-stage-comparison.png")

all_materials = Image.new("RGB", (2588, 1394), (22, 24, 28))
draw = ImageDraw.Draw(all_materials)
draw.text((24, 22), "AI REFERENCE", font=font(32), fill="white")
draw.text((1310, 22), "EVENGINE / RTX 3090 / FIXED STUDIO", font=font(29), fill="white")
all_materials.paste(reference.resize((1254, 1254), Image.Resampling.LANCZOS), (24, 108))
for i, name in enumerate(names):
    x, y = 1310 + round((i % 4) * 313.5), 108 + round((i // 4) * 313.5)
    all_materials.paste(
        render_tile(after, i).resize((274, 274), Image.Resampling.LANCZOS), (x + 20, y + 7)
    )
    draw.text((x + 157, y + 292), name, font=font(19), anchor="mm", fill="white")
draw.text(
    (24, 1370),
    "Engine screenshots: crop, resize and labels only. No material retouching.",
    font=font(17),
    fill=(170, 178, 190),
)
all_materials.save(args.output / "reference-vs-fixed-stage.png")
