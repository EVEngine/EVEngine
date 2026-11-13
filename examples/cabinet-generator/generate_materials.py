"""Rebuild the cabinet's deterministic, periodic PBR tiles (requires Pillow).

No runtime dependency on Python. No downloaded textures or random global state.
Tile footprint is one world metre; height amplitudes below are in metres.
"""
from __future__ import annotations

import math
from pathlib import Path

from PIL import Image

SIZE = 256
TAU = math.tau


def periodic_noise(u: float, v: float, nx: int, ny: int) -> float:
    """Smooth deterministic tileable value noise; no global RNG state."""
    x, y = u * nx, v * ny
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = x - ix, y - iy
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)

    def sample(a: int, b: int) -> float:
        value = math.sin((a % nx) * 127.1 + (b % ny) * 311.7 + 19.3) * 43758.5453
        return value - math.floor(value)

    a = sample(ix, iy) * (1 - fx) + sample(ix + 1, iy) * fx
    b = sample(ix, iy + 1) * (1 - fx) + sample(ix + 1, iy + 1) * fx
    return a * (1 - fy) + b * fy


def surface(kind: str, u: float, v: float) -> tuple[float, float, float]:
    """Return periodic relief, albedo modulation and perceptual roughness."""
    micro = math.sin(TAU * (71 * u + 3 * v)) * math.sin(TAU * (5 * u - 89 * v))
    if kind == "wood":
        warp = 0.85 * periodic_noise(u, v, 4, 3) + 0.18 * periodic_noise(u, v, 13, 7)
        rings = (0.5 + 0.5 * math.sin(TAU * (19 * u + warp))) ** 5
        fibres = (0.5 + 0.5 * math.sin(TAU * (73 * u + 0.8 * warp))) ** 8
        broad = periodic_noise(u, v, 6, 2)
        value = 0.55 + 0.28 * broad - 0.16 * rings - 0.045 * fibres + 0.008 * micro
        return value * 0.00014, 0.70 + 0.25 * value, 0.34 + 0.13 * (1 - value)
    if kind == "lacquer":
        orange_peel = math.sin(TAU * (13 * u + 7 * v)) * math.sin(TAU * (9 * v - 11 * u))
        return orange_peel * 0.000008, 0.96 + 0.005 * micro, 0.28 + 0.018 * orange_peel
    if kind == "brushed":
        lines = math.sin(TAU * (91 * v + 0.06 * math.sin(TAU * u)))
        return lines * 0.000018, 0.83 + 0.035 * lines, 0.23 + 0.06 * (0.5 + 0.5 * lines)
    raise ValueError(kind)


def byte(value: float) -> int:
    return round(max(0.0, min(1.0, value)) * 255)


def generate(kind: str, output: Path) -> None:
    """Build aligned maps, including the engine's RG normal/B AO/A smoothness map."""
    data = [surface(kind, x / SIZE, y / SIZE) for y in range(SIZE) for x in range(SIZE)]
    names = ("albedo", "normal", "roughness", "metallic", "ao", "orm", "packed")
    maps = {name: Image.new("RGBA", (SIZE, SIZE)) for name in names}
    pixels = {name: [] for name in names}
    metal = 1.0 if kind == "brushed" else 0.0
    for y in range(SIZE):
        for x in range(SIZE):
            h, color, roughness = data[y * SIZE + x]
            dx = (data[y * SIZE + (x + 1) % SIZE][0] - data[y * SIZE + (x - 1) % SIZE][0]) * SIZE / 2
            dy = (data[((y + 1) % SIZE) * SIZE + x][0] - data[((y - 1) % SIZE) * SIZE + x][0]) * SIZE / 2
            length = math.sqrt(dx * dx + dy * dy + 1)
            nx, ny, nz = -dx / length, -dy / length, 1 / length
            # Texture-scale cavity only; furniture/contact AO belongs to scene lighting.
            neighbours = (data[y * SIZE + (x + 1) % SIZE][0] + data[y * SIZE + (x - 1) % SIZE][0]) / 2
            ao = max(0.96, 1 - max(0.0, neighbours - h) * 160)
            r, m, a = byte(roughness), byte(metal), byte(ao)
            rgb = (color, color * 0.92, color * 0.79) if kind == "wood" else (color,) * 3
            pixels["albedo"].append(tuple(byte(c) for c in rgb) + (255,))
            pixels["normal"].append((byte(nx * 0.5 + 0.5), byte(ny * 0.5 + 0.5), byte(nz * 0.5 + 0.5), 255))
            pixels["roughness"].append((r, r, r, 255))
            pixels["metallic"].append((m, m, m, 255))
            pixels["ao"].append((a, a, a, 255))
            pixels["orm"].append((a, r, m, 255))
            pixels["packed"].append((byte(nx * 0.5 + 0.5), byte(ny * 0.5 + 0.5), a, 255 - r))
    output.mkdir(parents=True, exist_ok=True)
    for name, image in maps.items():
        image.putdata(pixels[name])
        image.save(output / f"{kind}_{name}.png", optimize=True)


if __name__ == "__main__":
    for kind in ("wood", "lacquer", "brushed"):
        generate(kind, Path(__file__).parent / "materials")
