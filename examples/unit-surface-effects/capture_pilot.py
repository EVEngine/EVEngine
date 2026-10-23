#!/usr/bin/env python3
"""Capture the five acceptance representatives using normal engine input and PNG readback."""

from __future__ import annotations
import argparse
import hashlib
import json
import sys
import time
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageOps
from capture_reference import Engine, preset_ids, HERE

PILOTS = ["shield", "hit", "shock", "bark", "teleport"]
CROP = (340, 70, 940, 670)


def portrait(path, size):
    with Image.open(path) as image:
        if image.size != (1280, 800):
            raise ValueError("Expected 1280x800 engine frame")
        return ImageOps.contain(image.convert("RGB").crop(CROP), size, Image.Resampling.LANCZOS)


def compose(root, records):
    width = 2160
    tile = 360
    header = 52
    board = Image.new("RGB", (1536, 1040), (7, 13, 24))
    draw = ImageDraw.Draw(board)
    font = ImageFont.truetype(str(HERE / "assets/fonts/DejaVuSans-Bold.ttf"), 20)
    title = ImageFont.truetype(str(HERE / "assets/fonts/DejaVuSans-Bold.ttf"), 26)
    draw.text((12, 10), "SAME-MESH PILOT / ENGINE OUTPUT", font=title, fill="#cceaff")
    temporal = Image.new("RGB", (width, header + 390), (7, 13, 24))
    td = ImageDraw.Draw(temporal)
    td.text((12, 10), "ENGINE TIME SAMPLES / EFFECT-SPECIFIC TIMING", font=title, fill="#cceaff")
    for column, record in enumerate(records):
        x = column * tile
        bx = (column % 3) * 512
        by = 52 + (column // 3) * 494
        chosen = record["samples"][record["selectedSample"]]
        draw.text(
            (bx + 16, by + 458),
            f"{record['id']:02d} {record['preset'].upper()}  {chosen['timeSeconds']:.3f}s",
            font=font,
            fill="#dcefff",
        )
        board.paste(portrait(root / chosen["path"], (444, 444)), (bx + 34, by + 6))
        for index, sample in enumerate(record["samples"]):
            sx = x + index % 2 * 180
            sy = header + index // 2 * 192
            temporal.paste(portrait(root / sample["path"], (176, 166)), (sx + 2, sy))
            td.text(
                (sx + 5, sy + 166),
                f"{record['preset']} {sample['timeSeconds']:.3f}s",
                font=font,
                fill="#b3cce4",
            )
    neutral = root / "captures/base.png"
    draw.text((1040, 1004), "27 NEUTRAL MATERIAL", font=font, fill="#dcefff")
    board.paste(portrait(neutral, (444, 444)), (1058, 552))
    temporal.paste(portrait(neutral, (310, 310)), (5 * tile + 25, header + 36))
    board.save(root / "pilot-rendered.png")
    temporal.save(root / "pilot-timeline.png")
    reference_path = HERE.parents[1] / "assets/styles/unit-surface/reference/pilot-same-mesh.png"
    with Image.open(reference_path) as image:
        reference = ImageOps.contain(image.convert("RGB"), board.size, Image.Resampling.LANCZOS)
    pair = Image.new("RGB", (3072, 1080), (7, 13, 24))
    pd = ImageDraw.Draw(pair)
    pd.text((12, 5), "AI TARGET / IDENTICAL MESH", font=title, fill="#cceaff")
    pd.text((1548, 5), "ACTUAL ENGINE OUTPUT", font=title, fill="#cceaff")
    pair.paste(reference, ((1536 - reference.width) // 2, 40 + (1040 - reference.height) // 2))
    pair.paste(board, (1536, 40))
    pair.save(root / "pilot-reference-vs-render.png")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=17529)
    parser.add_argument("--output", type=Path, default=HERE / "comparison-v2")
    parser.add_argument(
        "--prepared", action="store_true", help="Solo index 0, paused, HUD hidden, overlay enabled"
    )
    parser.add_argument("--compose-only", action="store_true")
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    if args.compose_only:
        compose(
            root, json.loads((root / "pilot-manifest.json").read_text(encoding="utf-8"))["effects"]
        )
        return
    engine = Engine(args.port)
    ids = preset_ids()
    if not args.prepared:
        for key in ("1", "v", "h", "space"):
            engine.key(key)
    engine.key("o")
    engine.capture(root / "captures/base.png")
    engine.key("o")
    current = 0
    records = []
    for name in PILOTS:
        target = ids.index(name)
        for _ in range(target - current):
            engine.key("right")
        current = target
        engine.key("space")
        time.sleep(1.0)
        engine.key("space")
        engine.key("r")
        if name == "hit":
            increments = [0, 2, 3, 4]
            key = "b"
            step = 1 / 60
        elif name == "shock":
            increments = [0, 5, 5, 5]
            key = "b"
            step = 1 / 60
        elif name == "teleport":
            increments = [0, 2, 1, 3]
            key = "n"
            step = 0.25
        else:
            increments = [0, 1, 1, 1]
            key = "n"
            step = 0.25
        seconds = 0.0
        samples = []
        for index, count in enumerate(increments):
            for _ in range(count):
                engine.key(key)
            seconds += count * step
            path = root / "captures" / f"{target + 1:02d}-{name}" / f"frame-{index}.png"
            engine.capture(path)
            samples.append(
                {
                    "timeSeconds": seconds,
                    "path": path.relative_to(root).as_posix(),
                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                }
            )
        records.append(
            {
                "id": target + 1,
                "preset": name,
                "selectedSample": 2,
                "samples": samples,
            }
        )
        print(
            f"Captured {target + 1:02d} {name}: {[round(s['timeSeconds'], 3) for s in samples]}",
            flush=True,
        )
    manifest = {
        "schema": "eve.example.surface-capture",
        "version": 2,
        "viewport": [1280, 800],
        "crop": list(CROP),
        "continuousPlaybackBeforeSamples": True,
        "effects": records,
        "baseline": "captures/base.png",
    }
    (root / "pilot-manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    compose(root, records)
    print(f"Pilot frames and sheets saved to {root}", flush=True)


if __name__ == "__main__":
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")
    main()
