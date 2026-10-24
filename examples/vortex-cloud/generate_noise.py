"""Bake a deterministic periodic volume into a padded 8x8 slice atlas.

Requires numpy and Pillow. Runtime uses the checked-in atlas, not Python.
R: broad FBM, G: round cellular lobes, B: fine FBM, A: opaque.
"""

from pathlib import Path

import numpy as np
from PIL import Image


def value_noise(size, cells, rng):
    grid = rng.random((cells, cells, cells), dtype=np.float32)
    q = np.arange(size, dtype=np.float32) * cells / size
    lo = q.astype(np.int32)
    f = q - lo
    f = f * f * (3 - 2 * f)
    out = np.zeros((size, size, size), dtype=np.float32)
    for z in range(2):
        for y in range(2):
            for x in range(2):
                weights = ((f if z else 1 - f)[:, None, None]
                           * (f if y else 1 - f)[None, :, None]
                           * (f if x else 1 - f)[None, None, :])
                out += grid[np.ix_((lo + z) % cells, (lo + y) % cells,
                                   (lo + x) % cells)] * weights
    return out


def cellular(size, cells, rng):
    z, y, x = np.mgrid[:size, :size, :size].astype(np.float32) * cells / size
    p = np.stack((z, y, x), axis=-1)
    cell = np.floor(p).astype(np.int32)
    offsets = rng.random((cells, cells, cells, 3), dtype=np.float32)
    best = np.full((size, size, size), 10.0, dtype=np.float32)
    for dz in range(-1, 2):
        for dy in range(-1, 2):
            for dx in range(-1, 2):
                candidate = cell + (dz, dy, dx)
                wrapped = candidate % cells
                point = candidate + offsets[wrapped[..., 0], wrapped[..., 1], wrapped[..., 2]]
                best = np.minimum(best, np.sum((p - point) ** 2, axis=-1))
    return np.clip(1 - np.sqrt(best), 0, 1)


def main():
    rng = np.random.default_rng(22026)
    size, stride = 64, 66
    broad = sum(weight * value_noise(size, cells, rng)
                for weight, cells in ((0.5, 4), (0.3, 8), (0.15, 16), (0.05, 32)))
    fine = 0.65 * value_noise(size, 16, rng) + 0.35 * value_noise(size, 32, rng)
    volume = np.stack((broad, cellular(size, 8, rng), fine, np.ones_like(broad)), axis=-1)
    atlas = np.zeros((stride * 8, stride * 8, 4), dtype=np.uint8)
    for z in range(size):
        padded = np.pad(volume[z], ((1, 1), (1, 1), (0, 0)), mode="wrap")
        y, x = (z // 8) * stride, (z % 8) * stride
        atlas[y:y + stride, x:x + stride] = np.rint(padded * 255).astype(np.uint8)
    target = Path(__file__).parent / "cloud-noise.tga"
    Image.fromarray(atlas).save(target)
    print(f"Wrote {target.name}: 64 cubed, 528x528 padded atlas, seed 22026")


if __name__ == "__main__":
    main()
