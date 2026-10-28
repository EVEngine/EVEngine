"""Regenerate original geometry masks with Python's standard library."""

from pathlib import Path
import math
import struct
import zlib


def png(path, size, coverage):
    def chunk(kind, data):
        return (
            struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
        )

    rows = bytearray()
    for y in range(size):
        rows.append(0)
        for x in range(size):
            hits = sum(
                coverage((x + (sx + 0.5) / 4) / size, (y + (sy + 0.5) / 4) / size)
                for sy in range(4)
                for sx in range(4)
            )
            rows.extend((255, 255, 255, round(hits * 255 / 16)))
    data = b"\x89PNG\r\n\x1a\n"
    data += chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b"")
    path.write_bytes(data)


def main():
    assets = Path(__file__).resolve().parents[1] / "assets"
    png(assets / "disc.png", 64, lambda x, y: math.hypot(x - 0.5, y - 0.5) <= 0.48)
    png(assets / "peak.png", 128, lambda x, y: abs(x - 0.5) <= y * 0.5)
    png(assets / "white.png", 4, lambda x, y: True)


if __name__ == "__main__":
    main()
