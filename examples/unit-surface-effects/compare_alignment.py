#!/usr/bin/env python3
"""Compose concept, previous render and current render without altering their colors."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, ImageOps

HERE = Path(__file__).resolve().parent


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, default=HERE / "comparison-overall-before")
    parser.add_argument("--after", type=Path, default=HERE / "comparison-current")
    parser.add_argument(
        "--presets", nargs="+", default=["charge", "overheat", "metallize", "bark"]
    )
    args = parser.parse_args()
    before = json.loads((args.before / "manifest.json").read_text(encoding="utf-8"))
    after = json.loads((args.after / "manifest.json").read_text(encoding="utf-8"))
    font_path = HERE / "assets/fonts/DejaVuSans-Bold.ttf"
    title = ImageFont.truetype(str(font_path), 26)
    label = ImageFont.truetype(str(font_path), 19)
    small = ImageFont.truetype(str(font_path), 14)
    cell_width, cell_height, left, top = 360, 340, 130, 90
    board = Image.new(
        "RGB", (left + len(args.presets) * cell_width, top + cell_height * 3), "#09101c"
    )
    draw = ImageDraw.Draw(board)
    draw.text((15, 13), "SAME-MESH ALIGNMENT / CONCEPT, BEFORE, AFTER", font=title, fill="#daedff")
    draw.text(
        (15, 48),
        "Raw engine frames: same camera, light, simulation time and crop. No color correction.",
        font=small,
        fill="#9fb7ce",
    )
    for row, text in enumerate(["CONCEPT", "BEFORE", "AFTER"]):
        draw.text((10, top + row * cell_height + 140), text, font=label, fill="#a9cce9")
    reference_path = HERE.parents[1] / "assets/styles/unit-surface/reference/same-mesh-26.png"
    with Image.open(reference_path) as original:
        reference = original.convert("RGB")
    # The generated concept sheet has unequal cells; these are its measured borders.
    if reference.size != (1659, 948):
        raise ValueError("Concept sheet changed: remeasure its panel borders")
    columns = [
        (9, 256),
        (261, 487),
        (492, 716),
        (721, 944),
        (950, 1174),
        (1180, 1403),
        (1409, 1651),
    ]
    rows = [(46, 278), (283, 510), (515, 741), (745, 942)]
    for column, preset in enumerate(args.presets):
        old = next(x for x in before["effects"] if x["preset"] == preset)
        new = next(x for x in after["effects"] if x["preset"] == preset)
        index = new["id"] - 1
        x = left + column * cell_width
        draw.text((x + 10, top + 2), f"{new['id']:02d} {preset}", font=label, fill="#d3ebff")
        x0, x1 = columns[index % 7]
        y0, y1 = rows[index // 7]
        bounds = (x0 + 2, y0 + 2, x1 - 2, y1 - 2)
        crop = ImageOps.contain(
            reference.crop(tuple(round(v) for v in bounds)), (330, 300), Image.Resampling.LANCZOS
        )
        board.paste(crop, (x + (cell_width - crop.width) // 2, top + 30))
        for row, root, manifest, record in [
            (1, args.before, before, old),
            (2, args.after, after, new),
        ]:
            sample = record["samples"][record["selectedSample"]]
            with Image.open(root / sample["path"]) as raw:
                if raw.size != (1280, 800):
                    raise ValueError("Unexpected source viewport")
                crop = ImageOps.contain(
                    raw.convert("RGB").crop(tuple(after["crop"])),
                    (330, 300),
                    Image.Resampling.LANCZOS,
                )
            y = top + row * cell_height
            board.paste(crop, (x + (cell_width - crop.width) // 2, y + 12))
            draw.text(
                (x + 15, y + 315), f"t = {sample['timeSeconds']:.3f} s", font=small, fill="#a9cce9"
            )
    destination = args.after / "alignment-before-after.png"
    board.save(destination)
    print(destination)


if __name__ == "__main__":
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")
    main()
