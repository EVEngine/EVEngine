"""Rebuild the original demo skins using only Python's standard library.

One transparent marker frame surrounds 64x64 RGBA artwork. Top/left mark
stretch; bottom/right mark a 24px content inset. No external art dependency.
"""
from pathlib import Path
import struct
import zlib


def make_skin(name, fill, edge, radius):
    size = 66
    pixels = bytearray(size * size * 4)

    def put(x, y, color):
        offset = (y * size + x) * 4
        pixels[offset:offset + 4] = bytes(color)

    for y in range(64):
        for x in range(64):
            dx = max(radius - x, 0, x - (63 - radius))
            dy = max(radius - y, 0, y - (63 - radius))
            if dx * dx + dy * dy > radius * radius:
                continue
            border = min(x, y, 63 - x, 63 - y) < 2
            rounded_edge = dx * dx + dy * dy > (radius - 2) ** 2
            color = edge if border or rounded_edge else fill
            put(x + 1, y + 1, color)
    for i in range(25, 41):
        for x, y in ((i, 0), (0, i), (i, 65), (65, i)):
            put(x, y, (0, 0, 0, 255))

    def chunk(kind, data):
        return (struct.pack('>I', len(data)) + kind + data
                + struct.pack('>I', zlib.crc32(kind + data)))

    rows = b''.join(b'\0' + pixels[y * size * 4:(y + 1) * size * 4]
                    for y in range(size))
    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))
    Path(__file__).with_name(name + '.9.png').write_bytes(png)


if __name__ == '__main__':
    make_skin('moonlight', (18, 27, 47, 246), (153, 183, 208, 255), 16)
    make_skin('letter', (58, 38, 34, 250), (217, 166, 91, 255), 5)
