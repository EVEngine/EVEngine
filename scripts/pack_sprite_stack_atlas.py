#!/usr/bin/env python3
"""Pack equal-sized sprite-stack layer PNGs into one horizontal atlas strip.

Runtime consumption: EVEngine's `stack.setLayersFromAtlas(gfx, atlasTex, count)`
splits the strip back into `count` equal columns, so all input layers must have
identical dimensions.

Usage:
    python3 scripts/pack_sprite_stack_atlas.py layer_00.png layer_01.png ... \\
        -o atlas.png [--json manifest.json]

Output: RGBA PNG, width = count * cellWidth, height = cellHeight.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import zlib
from pathlib import Path

PNG_SIG = b"\x89PNG\r\n\x1a\n"


def read_png(path: Path):
    """Decode an RGB/RGBA/gray PNG into (width, height, RGBA bytes)."""
    data = path.read_bytes()
    if data[:8] != PNG_SIG:
        raise SystemExit(f"{path}: not a PNG")
    pos = 8
    w = h = bitdepth = coltype = 0
    idat = b""
    while pos < len(data):
        ln = struct.unpack(">I", data[pos : pos + 4])[0]
        typ = data[pos + 4 : pos + 8]
        chunk = data[pos + 8 : pos + 8 + ln]
        if typ == b"IHDR":
            w, h, bitdepth, coltype = struct.unpack(">IIBB", chunk[:10])
        elif typ == b"IDAT":
            idat += chunk
        elif typ == b"IEND":
            break
        pos += 12 + ln
    if coltype not in (0, 2, 4, 6):
        raise SystemExit(f"{path}: unsupported color type {coltype} (palette not supported)")
    if bitdepth != 8:
        raise SystemExit(f"{path}: only 8-bit PNGs are supported")

    channels = {0: 1, 2: 3, 4: 2, 6: 4}[coltype]
    stride = w * channels
    raw = zlib.decompress(idat)
    out = bytearray(w * h * channels)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]
        p += 1
        line = bytearray(raw[p : p + stride])
        p += stride
        if f == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = prev[i]
                c = prev[i - channels] if i >= channels else 0
                pa = abs(b - c)
                pb = abs(a - c)
                pc = abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        out[y * stride : (y + 1) * stride] = line
        prev = line

    if channels == 4:
        return w, h, bytes(out)
    rgba = bytearray(w * h * 4)
    for i in range(w * h):
        if channels == 3:
            rgba[i * 4 : i * 4 + 3] = out[i * 3 : i * 3 + 3]
            rgba[i * 4 + 3] = 255
        elif channels == 2:
            rgba[i * 4 : i * 4 + 2] = out[i * 2 : i * 2 + 2]
            rgba[i * 4 + 3] = out[i * 2 + 1]
        else:
            rgba[i * 4 : i * 4 + 4] = bytes((out[i], out[i], out[i], 255))
    return w, h, bytes(rgba)


def write_png(path: Path, w: int, h: int, rgba: bytes) -> None:
    def chunk(typ: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload))
            + typ
            + payload
            + struct.pack(">I", zlib.crc32(typ + payload) & 0xFFFFFFFF)
        )

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    raw = bytearray()
    stride = w * 4
    for y in range(h):
        raw.append(0)  # filter: none
        raw += rgba[y * stride : (y + 1) * stride]
    png = (
        PNG_SIG
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )
    path.write_bytes(png)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Pack equal-sized sprite-stack layer PNGs into one atlas strip."
    )
    parser.add_argument("inputs", nargs="+", type=Path, help="layer PNGs (all same size)")
    parser.add_argument("-o", "--output", type=Path, required=True, help="output atlas PNG")
    parser.add_argument("--json", type=Path, default=None, help="optional manifest JSON")
    args = parser.parse_args()

    if len(args.inputs) < 2:
        raise SystemExit("need at least two layer images")
    cells = [read_png(p) for p in args.inputs]
    w0, h0 = cells[0][0], cells[0][1]
    for p, (w, h, _) in zip(args.inputs[1:], cells[1:]):
        if w != w0 or h != h0:
            raise SystemExit(f"layer size mismatch: {p} is {w}x{h}, expected {w0}x{h0}")

    out_w = w0 * len(cells)
    out = bytearray(out_w * h0 * 4)
    for i, (_, _, px) in enumerate(cells):
        for y in range(h0):
            src = y * w0 * 4
            dst = (y * out_w + i * w0) * 4
            out[dst : dst + w0 * 4] = px[src : src + w0 * 4]

    write_png(args.output, out_w, h0, bytes(out))
    print(f"wrote {args.output} ({len(cells)} layers, {w0}x{h0} cells -> {out_w}x{h0})")
    if args.json:
        manifest = {
            "width": out_w,
            "height": h0,
            "layers": len(cells),
            "cellWidth": w0,
            "cellHeight": h0,
        }
        args.json.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        print(f"wrote {args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
