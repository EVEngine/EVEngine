#!/usr/bin/env python3
"""Capture each effect through engine MCP and compose an honest reference comparison.

Run against a fresh instance of this example. Requires Pillow. No eve_eval or
framebuffer grabs: every source PNG comes from eve_screenshot.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import socket
import sys
import time
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, ImageOps

HERE = Path(__file__).resolve().parent
CANONICAL = HERE.parents[1] / "assets/styles/unit-surface"
CURRENT_CROP = (330, 60, 950, 700)


class Engine:
    def __init__(self, port: int):
        self.port = port
        self.sequence = 0

    def call(self, name: str, arguments: dict) -> dict:
        self.sequence += 1
        request = {
            "jsonrpc": "2.0",
            "id": self.sequence,
            "method": "tools/call",
            "params": {"name": name, "arguments": arguments},
        }
        with socket.create_connection(("127.0.0.1", self.port), timeout=45) as connection:
            connection.sendall((json.dumps(request) + "\n").encode("utf-8"))
            packet = json.loads(connection.makefile(encoding="utf-8").readline())
        if "error" in packet:
            raise RuntimeError(packet)
        payload = json.loads(packet["result"]["content"][0]["text"])
        if payload.get("ok") is False and not payload.get("retryable"):
            raise RuntimeError(payload)
        return payload

    def wait(self, receipt: dict) -> None:
        for _ in range(120):
            result = self.call("eve_input_poll", {"id": receipt["id"]})
            if result.get("status") == "ready":
                return
            time.sleep(0.1)
        raise TimeoutError("Engine input batch did not complete")

    def key(self, name: str) -> None:
        # Press/release in separate frames. One action batch does not sequence
        # SDL key state changes around embedded wait operations.
        self.wait(
            self.call(
                "eve_input_batch",
                {"actions": [{"op": "key", "key": name, "down": True}], "waitFrames": 3},
            )
        )
        self.wait(
            self.call(
                "eve_input_batch",
                {"actions": [{"op": "key", "key": name, "down": False}], "waitFrames": 2},
            )
        )

    def capture(self, destination: Path) -> None:
        destination.parent.mkdir(parents=True, exist_ok=True)
        for _ in range(8):
            result = self.call("eve_screenshot", {"path": str(destination.resolve())})
            if result.get("ok"):
                return
            time.sleep(0.25)
        raise RuntimeError(result)


def preset_ids() -> list[str]:
    text = (CANONICAL / "style.nut").read_text(encoding="utf-8")
    records = re.findall(r"(\w+)\s*=\s*\{[^{}]*?\bmode\s*=\s*(\d+)\.0", text)
    records.sort(key=lambda record: int(record[1]))
    if len(records) != 26 or [int(mode) for _, mode in records] != list(range(26)):
        raise ValueError("Canonical preset catalogue must contain consecutive modes 0..25")
    return [name for name, _ in records]


def subject(path: Path, size: tuple[int, int]) -> Image.Image:
    with Image.open(path) as image:
        if image.size != (1280, 800):
            raise ValueError("Capture requires the example's default 1280x800 viewport")
        # Identical source rectangle for every effect, no per-effect reframing or
        # color/contrast edits. Full unmodified source PNGs are retained.
        crop = image.convert("RGB").crop(CURRENT_CROP)
    return ImageOps.contain(crop, size, Image.Resampling.LANCZOS)


def compose(output: Path, records: list[dict], neutral: Path) -> None:
    font_path = HERE / "assets/fonts/DejaVuSans-Bold.ttf"
    font = ImageFont.truetype(str(font_path), 17)
    small = ImageFont.truetype(str(font_path), 13)
    title = ImageFont.truetype(str(font_path), 27)
    width, header, cell_width, cell_height = 2100, 60, 300, 280
    board = Image.new("RGB", (width, header + cell_height * 4), (12, 20, 34))
    timeline = board.copy()
    draw, temporal_draw = ImageDraw.Draw(board), ImageDraw.Draw(timeline)
    draw.text((14, 12), "ENGINE RENDER / 26 INDIVIDUAL SURFACE EFFECTS", font=title, fill="#cceaff")
    temporal_draw.text(
        (14, 12),
        "ENGINE TIME SAMPLES / FOUR MILESTONES FROM EACH FULL SEQUENCE",
        font=title,
        fill="#cceaff",
    )
    for index, record in enumerate(records):
        x, y = index % 7 * cell_width, header + index // 7 * cell_height
        selected = record["selectedSample"]
        name = re.sub(r"([a-z])([A-Z])", r"\1 \2", record["preset"]).upper()
        draw.text((x + 8, y + 5), f"{index + 1:02d} {name}", font=font, fill="#e0f0ff")
        draw.text(
            (x + 235, y + 8),
            f"{record['samples'][selected]['timeSeconds']:.2f}s",
            font=small,
            fill="#93adc3",
        )
        image = subject(output / record["samples"][selected]["path"], (246, 246))
        board.paste(image, (x + (cell_width - image.width) // 2, y + 30))
        temporal_draw.text((x + 8, y + 5), f"{index + 1:02d} {name}", font=font, fill="#e0f0ff")
        visible_indices = sorted(
            set(
                [
                    0,
                    len(record["samples"]) // 3,
                    2 * len(record["samples"]) // 3,
                    len(record["samples"]) - 1,
                ]
            )
        )
        for sample_index, frame_index in enumerate(visible_indices):
            sample = record["samples"][frame_index]
            frame = subject(output / sample["path"], (122, 110))
            sx, sy = x + 15 + sample_index % 2 * 142, y + 31 + sample_index // 2 * 122
            timeline.paste(frame, (sx, sy))
            temporal_draw.text(
                (sx + 87, sy + 94), f"{sample['timeSeconds']:.2f}", font=small, fill="#edf6ff"
            )
    x, y = 5 * cell_width, header + 3 * cell_height
    for target, painter in [(board, draw), (timeline, temporal_draw)]:
        painter.text((x + 8, y + 5), "27 BASE MATERIAL", font=font, fill="#e0f0ff")
        target.paste(subject(neutral, (246, 246)), (x + 27, y + 30))
        lx = 6 * cell_width + 16
        for row, line in enumerate(
            [
                "28 CAPTURE LEGEND",
                "Same mesh / camera / light",
                "Engine-owned PNG readback",
                "Fixed local-space geometry",
                "Play + full time sequence",
                "No color or exposure retouch",
                "Crop + resize + labels only",
                "Concept art is a target",
                "This sheet is current output",
            ]
        ):
            painter.text(
                (lx, y + 8 + row * 28), line, font=font if row == 0 else small, fill="#bcd8ef"
            )
    sequence_dir = output / "sequences"
    sequence_dir.mkdir(exist_ok=True)
    for record in records:
        count = len(record["samples"])
        rows = (count + 3) // 4
        strip = Image.new("RGB", (1000, rows * 270 + 46), (7, 13, 24))
        sd = ImageDraw.Draw(strip)
        sd.text(
            (10, 8),
            f"{record['id']:02d} {record['preset']} / ENGINE TIME SEQUENCE",
            font=font,
            fill="#cceaff",
        )
        for index, frame in enumerate(record["samples"]):
            x = index % 4 * 250
            y = 46 + index // 4 * 270
            strip.paste(subject(output / frame["path"], (240, 240)), (x + 5, y))
            sd.text(
                (x + 8, y + 242), f"{frame['timeSeconds']:.3f} seconds", font=font, fill="#bcd8ef"
            )
        strip.save(sequence_dir / f"{record['id']:02d}-{record['preset']}.png")
    board.save(output / "rendered-26.png")
    timeline.save(output / "timeline-26.png")
    comparison = Image.new("RGB", (width * 2, board.height + 42), (7, 13, 24))
    comparison_draw = ImageDraw.Draw(comparison)
    comparison_draw.text((14, 4), "LEFT: AI CONCEPT TARGET", font=title, fill="#cceaff")
    comparison_draw.text((width + 14, 4), "RIGHT: ACTUAL ENGINE OUTPUT", font=title, fill="#cceaff")
    reference_path = CANONICAL / "reference/same-mesh-26.png"
    if not reference_path.exists():
        reference_path = CANONICAL / "reference/concept-26.png"
    with Image.open(reference_path) as reference:
        fitted = ImageOps.contain(
            reference.convert("RGB"), (width, board.height), Image.Resampling.LANCZOS
        )
    comparison.paste(
        fitted, ((width - fitted.width) // 2, 42 + (board.height - fitted.height) // 2)
    )
    comparison.paste(board, (width, 42))
    comparison.save(output / "reference-vs-render.png")


def main() -> None:
    global CURRENT_CROP
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=17529)
    parser.add_argument("--output", type=Path, default=HERE / "comparison-current")
    parser.add_argument(
        "--prepared", action="store_true", help="Already solo index 0, HUD off, paused"
    )
    parser.add_argument("--compose-only", action="store_true")
    parser.add_argument(
        "--only", nargs="+", help="Recapture named presets into an existing complete manifest"
    )
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    manifest_path = output / "manifest.json"
    if args.compose_only:
        stored = json.loads(manifest_path.read_text(encoding="utf-8"))
        CURRENT_CROP = tuple(stored["crop"])
        records = stored["effects"]
        compose(output, records, output / "captures/base.png")
        return
    previous = None
    if args.only:
        if not set(args.only) <= set(preset_ids()):
            raise ValueError("Unknown preset in --only")
        previous = json.loads(manifest_path.read_text(encoding="utf-8"))
        if len(previous["effects"]) != 26:
            raise ValueError("Selective recapture requires a complete 26-preset manifest")
    engine = Engine(args.port)
    if not args.prepared:
        engine.key("1")
        engine.key("v")
        engine.key("h")
        engine.key("space")
    records = []
    for index, preset in enumerate(preset_ids()):
        if args.only and preset not in args.only:
            records.append(previous["effects"][index])
            if index < 25:
                engine.key("right")
            continue
        engine.key("space")  # Real continuous playback before controlled sampling.
        time.sleep(1.0)
        engine.key("space")
        engine.key("r")
        samples = []
        if preset == "hit":
            ticks = [0, 1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 24]
            step = 1 / 60
            key = "b"
        elif preset == "shock":
            ticks = list(range(0, 24, 2))
            step = 1 / 60
            key = "b"
        elif preset == "slowTime":
            ticks = list(range(0, 24, 2))
            step = 0.25
            key = "n"
        else:
            ticks = list(range(12))
            step = 0.25
            key = "n"
        previous_tick = 0
        for sample_index, tick in enumerate(ticks):
            for _ in range(tick - previous_tick):
                engine.key(key)
            previous_tick = tick
            path = (
                output / "captures" / f"{index + 1:02d}-{preset}" / f"frame-{sample_index:02d}.png"
            )
            engine.capture(path)
            samples.append(
                {
                    "timeSeconds": tick * step,
                    "path": path.relative_to(output).as_posix(),
                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                }
            )
        selected = {"hit": 3, "shock": 5, "heal": 7, "cleanse": 5, "petrify": 4, "teleport": 3}.get(
            preset, 2
        )
        record = {"id": index + 1, "preset": preset, "selectedSample": selected, "samples": samples}
        records.append(record)
        print(
            f"Captured {index + 1:02d}/26 {preset}: playback + twelve presented frames", flush=True
        )
        if index < 25:
            engine.key("right")
    engine.key("o")
    neutral = output / "captures/base.png"
    engine.capture(neutral)
    engine.key("o")
    manifest = {
        "schema": "eve.example.surface-capture",
        "version": 3,
        "viewport": [1280, 800],
        "crop": list(CURRENT_CROP),
        "playbackSecondsBeforeSampling": 1.0,
        "selectionPolicy": "Effect-specific representative time; 12 original frames retained per effect",
        "lastRecapturedPresets": args.only or preset_ids(),
        "imageOperations": ["fixed crop", "resize", "labels", "composition"],
        "effects": records,
    }
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    compose(output, records, neutral)
    print(f"Saved 312 effect frames, neutral frame and comparison sheets to {output}", flush=True)


if __name__ == "__main__":
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")
    main()
