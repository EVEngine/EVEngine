"""Regenerate the original radial sprites with Python's standard library."""

import math
from pathlib import Path
import struct
import zlib


def write_sprite(path: Path, size: int, sharp: bool) -> None:
    """Write a straight-alpha white RGBA sprite with a transparent border."""
    rows = bytearray()
    for y in range(size):
        rows.append(0)
        for x in range(size):
            dx = (x + 0.5) / size * 2.0 - 1.0
            dy = (y + 0.5) / size * 2.0 - 1.0
            radius_squared = dx * dx + dy * dy
            edge = max(0.0, 1.0 - radius_squared)
            if sharp:
                alpha = (0.8 * math.exp(-32.0 * radius_squared)
                         + 0.2 * math.exp(-5.0 * radius_squared)) * edge
            else:
                alpha = math.exp(-4.5 * radius_squared) * edge * edge
            rows.extend((255, 255, 255, round(alpha * 255)))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
                     + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


if __name__ == "__main__":
    destination = Path(__file__).resolve().parent / "assets"
    destination.mkdir(exist_ok=True)
    write_sprite(destination / "star.png", 64, True)
    write_sprite(destination / "glow.png", 128, False)
