#!/usr/bin/env python3
"""Composable foundation for project-defined 2.5D tile asset workflows.

The engine owns only the stage protocol and TileSet manifest contract. Projects
choose the stages, their order and options, and may register replacement stages
from ordinary Python files.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import re
import struct
import sys
import zlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

PNG_SIG = b"\x89PNG\r\n\x1a\n"
Stage = Callable[["PipelineContext", dict[str, Any]], None]


def _chunk(kind: bytes, payload: bytes) -> bytes:
    return (struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))


def read_png(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if data[:8] != PNG_SIG:
        raise ValueError(f"{path}: not a PNG")
    pos, width, height, bit_depth, color_type = 8, 0, 0, 0, 0
    compressed = bytearray()
    palette = b""
    palette_alpha = b""
    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        kind = data[pos + 4:pos + 8]
        payload = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack(">IIBB", payload[:10])
        elif kind == b"IDAT":
            compressed += payload
        elif kind == b"PLTE":
            palette = payload
        elif kind == b"tRNS":
            palette_alpha = payload
        elif kind == b"IEND":
            break
        pos += length + 12
    if bit_depth != 8 or color_type not in (0, 2, 3, 4, 6):
        raise ValueError(f"{path}: requires 8-bit gray/RGB/palette/RGBA PNG")
    if color_type == 3 and not palette:
        raise ValueError(f"{path}: palette PNG has no PLTE chunk")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color_type]
    stride = width * channels
    encoded = zlib.decompress(compressed)
    decoded = bytearray(width * height * channels)
    previous = bytearray(stride)
    cursor = 0
    for row in range(height):
        filter_type = encoded[cursor]
        cursor += 1
        line = bytearray(encoded[cursor:cursor + stride])
        cursor += stride
        for i in range(stride):
            left = line[i - channels] if i >= channels else 0
            up = previous[i]
            upper_left = previous[i - channels] if i >= channels else 0
            if filter_type == 1:
                line[i] = (line[i] + left) & 255
            elif filter_type == 2:
                line[i] = (line[i] + up) & 255
            elif filter_type == 3:
                line[i] = (line[i] + ((left + up) // 2)) & 255
            elif filter_type == 4:
                estimate = left + up - upper_left
                distances = (abs(estimate - left), abs(estimate - up), abs(estimate - upper_left))
                predictor = (left, up, upper_left)[distances.index(min(distances))]
                line[i] = (line[i] + predictor) & 255
            elif filter_type != 0:
                raise ValueError(f"{path}: unsupported PNG filter {filter_type}")
        decoded[row * stride:(row + 1) * stride] = line
        previous = line
    rgba = bytearray(width * height * 4)
    for index in range(width * height):
        if channels == 4:
            rgba[index * 4:index * 4 + 4] = decoded[index * 4:index * 4 + 4]
        elif channels == 3:
            rgba[index * 4:index * 4 + 3] = decoded[index * 3:index * 3 + 3]
            rgba[index * 4 + 3] = 255
        elif channels == 2:
            gray, alpha = decoded[index * 2:index * 2 + 2]
            rgba[index * 4:index * 4 + 4] = bytes((gray, gray, gray, alpha))
        elif color_type == 3:
            palette_index = decoded[index]
            start = palette_index * 3
            alpha = palette_alpha[palette_index] if palette_index < len(palette_alpha) else 255
            rgba[index * 4:index * 4 + 4] = palette[start:start + 3] + bytes((alpha,))
        else:
            gray = decoded[index]
            rgba[index * 4:index * 4 + 4] = bytes((gray, gray, gray, 255))
    return width, height, bytes(rgba)


def write_png(path: Path, width: int, height: int, rgba: bytes) -> None:
    raw = bytearray()
    stride = width * 4
    for row in range(height):
        raw.append(0)
        raw += rgba[row * stride:(row + 1) * stride]
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(PNG_SIG + _chunk(b"IHDR", header) +
                     _chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + _chunk(b"IEND", b""))


@dataclass
class TileAsset:
    source: Path
    gid: int
    name: str
    width: int = 0
    height: int = 0
    pixels: bytes = b""
    trim: tuple[int, int, int, int] = (0, 0, 0, 0)
    region: tuple[int, int, int, int] = (0, 0, 0, 0)
    pivot: tuple[float, float] = (0.0, 0.0)
    metadata: dict[str, Any] = field(default_factory=dict)


@dataclass
class PipelineContext:
    config_path: Path
    config: dict[str, Any]
    output_dir: Path
    tiles: list[TileAsset] = field(default_factory=list)
    atlas: tuple[int, int, bytes] | None = None
    artifacts: dict[str, Path] = field(default_factory=dict)
    diagnostics: list[str] = field(default_factory=list)


class StageRegistry:
    def __init__(self) -> None:
        self._stages: dict[str, Stage] = {}

    def register(self, name: str, stage: Stage) -> None:
        if not name or not callable(stage):
            raise ValueError("stage needs a name and callable")
        self._stages[name] = stage

    def resolve(self, name: str) -> Stage:
        if name not in self._stages:
            raise KeyError(f"unknown stage '{name}'")
        return self._stages[name]


def stage_scan(context: PipelineContext, options: dict[str, Any]) -> None:
    source = Path(options.get("source", context.config.get("source", ".")))
    if not source.is_absolute():
        source = (context.config_path.parent / source).resolve()
    pattern = options.get("glob", "*.png")
    gid_pattern = re.compile(options.get("gidRegex", r"(\d+)"))
    first_gid = int(options.get("firstGid", 1))
    paths = sorted(source.glob(pattern), key=lambda path: path.name)
    used: set[int] = set()
    for index, path in enumerate(paths):
        match = gid_pattern.search(path.stem)
        gid = int(match.group(1)) + first_gid if match else first_gid + index
        if gid in used:
            raise ValueError(f"duplicate gid {gid}: {path}")
        used.add(gid)
        context.tiles.append(TileAsset(path, gid, path.stem))
    if not context.tiles:
        raise ValueError(f"no inputs matched {source / pattern}")


def _alpha_bounds(width: int, height: int, rgba: bytes, threshold: int) -> tuple[int, int, int, int]:
    xs: list[int] = []
    ys: list[int] = []
    for y in range(height):
        for x in range(width):
            if rgba[(y * width + x) * 4 + 3] > threshold:
                xs.append(x)
                ys.append(y)
    return (min(xs), min(ys), max(xs) + 1, max(ys) + 1) if xs else (0, 0, 1, 1)


def stage_analyze(context: PipelineContext, options: dict[str, Any]) -> None:
    threshold = int(options.get("alphaThreshold", 0))
    normalized_pivot = options.get("pivot", [0.5, 0.75])
    overrides = context.config.get("overrides", {})
    for tile in context.tiles:
        tile.width, tile.height, tile.pixels = read_png(tile.source)
        tile.trim = _alpha_bounds(tile.width, tile.height, tile.pixels, threshold)
        override = overrides.get(tile.name, overrides.get(str(tile.gid), {}))
        source_pivot = override.get("pivot", normalized_pivot)
        px = float(source_pivot[0]) * tile.width if abs(float(source_pivot[0])) <= 1 else float(source_pivot[0])
        py = float(source_pivot[1]) * tile.height if abs(float(source_pivot[1])) <= 1 else float(source_pivot[1])
        tile.pivot = (px - tile.trim[0], py - tile.trim[1])
        tile.metadata = {
            "footprint": override.get("footprint", options.get("footprint", [1, 1])),
            "walkable": override.get("walkable", options.get("walkable", True)),
            "cost": override.get("cost", options.get("cost", 1.0)),
            "sortBias": override.get("sortBias", options.get("sortBias", 0.0)),
            "tags": override.get("tags", []),
        }


def stage_pack(context: PipelineContext, options: dict[str, Any]) -> None:
    padding = max(0, int(options.get("padding", 2)))
    max_width = max(64, int(options.get("maxWidth", 2048)))
    x = y = row_height = 0
    placements: list[tuple[TileAsset, int, int, int, int]] = []
    atlas_width = 1
    for tile in context.tiles:
        left, top, right, bottom = tile.trim
        width, height = right - left, bottom - top
        if x and x + width > max_width:
            x = 0
            y += row_height + padding
            row_height = 0
        placements.append((tile, x, y, width, height))
        tile.region = (x, y, width, height)
        x += width + padding
        row_height = max(row_height, height)
        atlas_width = max(atlas_width, x - padding)
    atlas_height = max(1, y + row_height)
    atlas = bytearray(atlas_width * atlas_height * 4)
    for tile, dst_x, dst_y, width, height in placements:
        src_x, src_y, _, _ = tile.trim
        for row in range(height):
            src = ((src_y + row) * tile.width + src_x) * 4
            dst = ((dst_y + row) * atlas_width + dst_x) * 4
            atlas[dst:dst + width * 4] = tile.pixels[src:src + width * 4]
    context.atlas = (atlas_width, atlas_height, bytes(atlas))


def stage_emit(context: PipelineContext, options: dict[str, Any]) -> None:
    if context.atlas is None:
        raise ValueError("emit requires an atlas; run a pack stage first")
    atlas_name = options.get("atlas", "tiles.atlas.png")
    manifest_name = options.get("manifest", "tiles.tileset.json")
    atlas_path = context.output_dir / atlas_name
    manifest_path = context.output_dir / manifest_name
    write_png(atlas_path, *context.atlas)
    manifest = {
        "schema": "eve.tileset/1",
        "tileset": {
            "image": options.get("runtimeImage", atlas_name),
            "firstGid": min(tile.gid for tile in context.tiles),
            "columns": 1,
            "tiles": [
                {
                    "gid": tile.gid,
                    "name": tile.name,
                    "region": list(tile.region),
                    "pivot": [round(tile.pivot[0], 4), round(tile.pivot[1], 4)],
                    **tile.metadata,
                }
                for tile in context.tiles
            ],
        },
        "pipeline": {
            "config": context.config_path.name,
            "stages": [stage if isinstance(stage, str) else stage.get("use")
                       for stage in context.config.get("pipeline", [])],
        },
    }
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    context.artifacts.update(atlas=atlas_path, manifest=manifest_path)


def default_registry() -> StageRegistry:
    registry = StageRegistry()
    registry.register("scan", stage_scan)
    registry.register("analyze", stage_analyze)
    registry.register("pack", stage_pack)
    registry.register("emit", stage_emit)
    return registry


def load_plugin(path: Path, registry: StageRegistry) -> None:
    spec = importlib.util.spec_from_file_location(f"eve_tile_pipeline_{path.stem}", path)
    if spec is None or spec.loader is None:
        raise ValueError(f"cannot load plugin {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if hasattr(module, "register"):
        module.register(registry)
    elif hasattr(module, "STAGES"):
        for name, stage in module.STAGES.items():
            registry.register(name, stage)
    else:
        raise ValueError(f"plugin {path} must export register(registry) or STAGES")


def run(config_path: Path, output_override: Path | None = None) -> PipelineContext:
    config = json.loads(config_path.read_text(encoding="utf-8"))
    output_dir = output_override or Path(config.get("output", "generated"))
    if not output_dir.is_absolute():
        output_dir = (config_path.parent / output_dir).resolve()
    context = PipelineContext(config_path.resolve(), config, output_dir)
    registry = default_registry()
    for plugin in config.get("plugins", []):
        plugin_path = Path(plugin)
        if not plugin_path.is_absolute():
            plugin_path = (config_path.parent / plugin_path).resolve()
        load_plugin(plugin_path, registry)
    for entry in config.get("pipeline", ["scan", "analyze", "pack", "emit"]):
        name = entry if isinstance(entry, str) else entry["use"]
        options = {} if isinstance(entry, str) else entry.get("options", {})
        registry.resolve(name)(context, options)
    return context


def main() -> int:
    parser = argparse.ArgumentParser(description="Run a composable EVEngine 2.5D TileSet pipeline")
    parser.add_argument("config", type=Path, help="project-owned pipeline JSON")
    parser.add_argument("-o", "--output", type=Path, default=None, help="override output directory")
    args = parser.parse_args()
    try:
        context = run(args.config, args.output)
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"tile-pipeline: {error}", file=sys.stderr)
        return 1
    for name, path in context.artifacts.items():
        print(f"{name}: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
