#!/usr/bin/env python3
"""Generate the pre-projected room atlas PNG for examples/interior-mapping."""
from __future__ import annotations

import struct
import zlib
from pathlib import Path

COLS, ROWS, CELL = 4, 4, 128


def clamp01(v: float) -> float:
    return 0.0 if v < 0.0 else 1.0 if v > 1.0 else v


def palette(room_id: int) -> tuple[float, float, float]:
    walls = [
        (0.78, 0.74, 0.68),
        (0.55, 0.68, 0.72),
        (0.72, 0.58, 0.52),
        (0.62, 0.66, 0.55),
        (0.70, 0.62, 0.78),
        (0.80, 0.72, 0.58),
        (0.58, 0.60, 0.68),
        (0.74, 0.70, 0.62),
        (0.66, 0.74, 0.70),
        (0.76, 0.66, 0.60),
        (0.60, 0.70, 0.78),
        (0.68, 0.60, 0.58),
        (0.72, 0.72, 0.66),
        (0.58, 0.64, 0.58),
        (0.70, 0.58, 0.66),
        (0.64, 0.68, 0.74),
    ]
    return walls[room_id % len(walls)]


def paint_room(buf: bytearray, ox: int, oy: int, stride: int, room_id: int) -> None:
    wall = palette(room_id)
    floor_c = (wall[0] * 0.55, wall[1] * 0.48, wall[2] * 0.42)
    ceil_c = (wall[0] * 1.08, wall[1] * 1.06, wall[2] * 1.05)
    accent = (
        0.35 + 0.4 * ((room_id * 17) % 5) / 4.0,
        0.25 + 0.35 * ((room_id * 9) % 7) / 6.0,
        0.20 + 0.45 * ((room_id * 13) % 4) / 3.0,
    )
    back_door = room_id % 3 == 0
    window_side = room_id % 5 == 2
    shelf = room_id % 4 != 1
    for y in range(CELL):
        for x in range(CELL):
            u = (x + 0.5) / CELL
            v = (y + 0.5) / CELL
            if v < 0.18:
                col = ceil_c
            elif v > 0.78:
                board = 0.92 if int(u * 8.0) % 2 == 0 else 1.0
                col = (floor_c[0] * board, floor_c[1] * board, floor_c[2] * board)
            elif u < 0.12:
                col = (wall[0] * 0.82, wall[1] * 0.82, wall[2] * 0.85)
            elif u > 0.88:
                col = (wall[0] * 0.88, wall[1] * 0.86, wall[2] * 0.84)
            else:
                col = wall
                if back_door and 0.38 < u < 0.62 and 0.28 < v < 0.78:
                    col = (0.22, 0.18, 0.14)
                    if 0.56 < u < 0.60 and 0.48 < v < 0.54:
                        col = (0.75, 0.65, 0.35)
                if window_side and 0.70 < u < 0.86 and 0.32 < v < 0.62:
                    col = (0.45, 0.62, 0.82)
                if shelf and 0.16 < u < 0.34 and 0.40 < v < 0.70:
                    if int(v * 20.0) % 4 == 0:
                        col = (accent[0] * 0.5, accent[1] * 0.5, accent[2] * 0.5)
                    else:
                        col = accent
                if 0.46 < u < 0.54 and 0.20 < v < 0.28:
                    col = (0.95, 0.90, 0.70)
            edge = (
                clamp01(u * 4.0)
                * clamp01((1.0 - u) * 4.0)
                * clamp01(v * 3.0)
                * clamp01((1.0 - v) * 3.0)
            )
            edge = 0.75 + 0.25 * edge
            i = ((oy + y) * stride + (ox + x)) * 4
            buf[i] = int(clamp01(col[0] * edge) * 255 + 0.5)
            buf[i + 1] = int(clamp01(col[1] * edge) * 255 + 0.5)
            buf[i + 2] = int(clamp01(col[2] * edge) * 255 + 0.5)
            buf[i + 3] = 255


def write_png(path: Path, width: int, height: int, rgba: bytes) -> None:
    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = bytearray()
    row = width * 4
    for y in range(height):
        raw.append(0)
        raw.extend(rgba[y * row : (y + 1) * row])
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    path.write_bytes(png)


def main() -> None:
    w, h = COLS * CELL, ROWS * CELL
    buf = bytearray(w * h * 4)
    room_id = 0
    for row in range(ROWS):
        for col in range(COLS):
            # Image top = GL V=1 after typical upload; paint row0 at top.
            oy = row * CELL
            paint_room(buf, col * CELL, oy, w, room_id)
            room_id += 1
    out = Path(__file__).resolve().parent.parent / "assets" / "room_atlas.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    write_png(out, w, h, bytes(buf))
    print(f"wrote {out} ({w}x{h})")


if __name__ == "__main__":
    main()
