#!/usr/bin/env python3
"""Perspective room atlas matching the shader's behind-facade convention.

Camera at window z=+1 looking toward -Z (into the building).
film (u,v) → ray origin (0,0,+1), direction (u, v, -1).
"""
from __future__ import annotations

import struct
import zlib
from pathlib import Path

COLS, ROWS, CELL = 4, 4, 128


def clamp01(v: float) -> float:
    return 0.0 if v < 0.0 else 1.0 if v > 1.0 else v


def palette(room_id: int):
    walls = [
        (0.78, 0.74, 0.68), (0.55, 0.68, 0.72), (0.72, 0.58, 0.52), (0.62, 0.66, 0.55),
        (0.70, 0.62, 0.78), (0.80, 0.72, 0.58), (0.58, 0.60, 0.68), (0.74, 0.70, 0.62),
        (0.66, 0.74, 0.70), (0.76, 0.66, 0.60), (0.60, 0.70, 0.78), (0.68, 0.60, 0.58),
        (0.72, 0.72, 0.66), (0.58, 0.64, 0.58), (0.70, 0.58, 0.66), (0.64, 0.68, 0.74),
    ]
    return walls[room_id % len(walls)]


def ray_box_far(origin, direction):
    ox, oy, oz = origin
    dx, dy, dz = direction
    eps = 1e-5

    def safe(v):
        if abs(v) < eps:
            return -eps if v < 0.0 else eps
        return v

    dx, dy, dz = safe(dx), safe(dy), safe(dz)
    inv = (1.0 / dx, 1.0 / dy, 1.0 / dz)
    t_far = (abs(inv[0]) - ox * inv[0], abs(inv[1]) - oy * inv[1], abs(inv[2]) - oz * inv[2])
    t = min(t_far)
    return (ox + t * dx, oy + t * dy, oz + t * dz)


def face_of(hit):
    ax, ay, az = abs(hit[0]), abs(hit[1]), abs(hit[2])
    if az >= ax and az >= ay:
        return "back" if hit[2] < 0.0 else "window"
    if ay >= ax:
        return "ceil" if hit[1] > 0.0 else "floor"
    return "right" if hit[0] > 0.0 else "left"


def shade_room(room_id, hit, face):
    wall = palette(room_id)
    floor_c = (wall[0] * 0.50, wall[1] * 0.42, wall[2] * 0.36)
    ceil_c = (min(1.0, wall[0] * 1.12), min(1.0, wall[1] * 1.10), min(1.0, wall[2] * 1.08))
    left_c = (wall[0] * 0.78, wall[1] * 0.78, wall[2] * 0.82)
    right_c = (wall[0] * 0.86, wall[1] * 0.84, wall[2] * 0.80)
    accent = (
        0.35 + 0.4 * ((room_id * 17) % 5) / 4.0,
        0.25 + 0.35 * ((room_id * 9) % 7) / 6.0,
        0.20 + 0.45 * ((room_id * 13) % 4) / 3.0,
    )
    hx, hy, hz = hit
    if face == "floor":
        board = 0.88 if int((hx + 1.0) * 4.0) % 2 == 0 else 1.0
        return (floor_c[0] * board, floor_c[1] * board, floor_c[2] * board)
    if face == "ceil":
        if abs(hx) < 0.12 and abs(hz + 0.2) < 0.12:
            return (0.95, 0.90, 0.70)
        return ceil_c
    if face == "left":
        if room_id % 4 != 1 and -0.2 < hy < 0.55 and -0.7 < hz < 0.0:
            if int((hy + 1.0) * 10.0) % 4 == 0:
                return (accent[0] * 0.5, accent[1] * 0.5, accent[2] * 0.5)
            return accent
        return left_c
    if face == "right":
        if room_id % 5 == 2 and -0.15 < hy < 0.45 and -0.75 < hz < -0.15:
            return (0.45, 0.62, 0.82)
        return right_c
    col = wall
    if room_id % 3 == 0 and -0.35 < hx < 0.35 and -1.0 < hy < 0.55:
        col = (0.22, 0.18, 0.14)
        if 0.18 < hx < 0.28 and -0.05 < hy < 0.08:
            col = (0.75, 0.65, 0.35)
    if room_id % 5 == 1 and 0.35 < hx < 0.75 and -0.2 < hy < 0.45:
        col = (0.40, 0.55, 0.75)
    if room_id % 4 == 2 and -0.55 < hx < 0.55 and -1.0 < hy < -0.35 and hz < -0.85:
        col = (0.42, 0.26, 0.14)
    return col


def paint_room(buf, ox, oy, stride, room_id):
    origin = (0.0, 0.0, 1.0)
    for y in range(CELL):
        for x in range(CELL):
            u = (x + 0.5) / CELL * 2.0 - 1.0
            v = 1.0 - (y + 0.5) / CELL * 2.0  # PNG top = +Y
            direction = (u, v, -1.0)  # into the building
            hit = ray_box_far(origin, direction)
            face = face_of(hit)
            if face == "window":
                face = "back"
            col = shade_room(room_id, hit, face)
            edge = clamp01(1.0 - max(abs(u), abs(v)) * 0.15)
            i = ((oy + y) * stride + (ox + x)) * 4
            buf[i] = int(clamp01(col[0] * (0.85 + 0.15 * edge)) * 255 + 0.5)
            buf[i + 1] = int(clamp01(col[1] * (0.85 + 0.15 * edge)) * 255 + 0.5)
            buf[i + 2] = int(clamp01(col[2] * (0.85 + 0.15 * edge)) * 255 + 0.5)
            buf[i + 3] = 255


def write_png(path, width, height, rgba):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = bytearray()
    row = width * 4
    for y in range(height):
        raw.append(0)
        raw.extend(rgba[y * row : (y + 1) * row])
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    path.write_bytes(png)


def main():
    w, h = COLS * CELL, ROWS * CELL
    buf = bytearray(w * h * 4)
    room_id = 0
    for row in range(ROWS):
        for col in range(COLS):
            paint_room(buf, col * CELL, row * CELL, w, room_id)
            room_id += 1
    out = Path(__file__).resolve().parent.parent / "assets" / "room_atlas.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    write_png(out, w, h, bytes(buf))
    print(f"wrote {out} ({w}x{h})")


if __name__ == "__main__":
    main()
