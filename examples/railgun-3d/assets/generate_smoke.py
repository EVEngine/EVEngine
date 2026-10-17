"""Rebuild the deterministic, feathered smoke opacity texture using stdlib only."""
import math
from pathlib import Path
import struct
import zlib


def generate():
    size = 128
    pixels = bytearray()
    for y in range(size):
        pixels.append(0)
        for x in range(size):
            u, v = (x + 0.5) / size * 2 - 1, (y + 0.5) / size * 2 - 1
            radius = math.sqrt(u * u + v * v)
            envelope = max(0.0, 1 - radius * radius) ** 3
            noise = 0.66 + 0.16 * math.sin(9*u + 3*math.sin(5*v)) * math.cos(11*v - 2*u)
            noise += 0.09 * math.sin(23*u + 17*v) * math.cos(19*v - 7*u)
            pixels.extend((255, 255, 255, int(255 * envelope * noise)))

    def chunk(kind, data):
        return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind + data))

    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('!2I5B', size, size, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(pixels))) + chunk(b'IEND', b'')
    Path(__file__).with_name('smoke.png').write_bytes(png)



if __name__ == '__main__':
    generate()
