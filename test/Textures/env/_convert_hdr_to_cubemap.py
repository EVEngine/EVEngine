#!/usr/bin/env python3
"""Convert Poly Haven 1k HDR equirectangulars to 128^2 RGBA cubemap faces.

Face order matches Graphics::newCubemap: +X,-X,+Y,-Y,+Z,-Z.
Source HDR files are not committed; only the LDR PNG faces are.
"""

from __future__ import annotations

import math
from pathlib import Path

import cv2
import numpy as np
from PIL import Image

FACE = 128
ROOT = Path(__file__).resolve().parent
SRC = ROOT / "_src"

# Reinhard-ish exposure tuned so indoor studio and outdoor sky both stay usable as IBL LDR.
EXPOSURE = {
    "studio_small_09": 0.55,
    "kloppenheim_06_puresky": 0.35,
}


def tonemap_reinhard(hdr: np.ndarray, exposure: float) -> np.ndarray:
    x = np.maximum(hdr.astype(np.float32) * exposure, 0.0)
    ldr = x / (1.0 + x)
    # Mild gamma for display-like albedo used as env color.
    ldr = np.power(np.clip(ldr, 0.0, 1.0), 1.0 / 2.2)
    return (ldr * 255.0 + 0.5).astype(np.uint8)


def sample_equirect(img: np.ndarray, dirs: np.ndarray) -> np.ndarray:
    """dirs: (N,3) unit vectors → RGB samples from equirectangular HDR."""
    x, y, z = dirs[:, 0], dirs[:, 1], dirs[:, 2]
    u = 0.5 + np.arctan2(x, -z) / (2.0 * math.pi)
    v = 0.5 - np.arcsin(np.clip(y, -1.0, 1.0)) / math.pi
    h, w = img.shape[:2]
    map_x = (u * (w - 1)).astype(np.float32).reshape(FACE, FACE)
    map_y = (v * (h - 1)).astype(np.float32).reshape(FACE, FACE)
    return cv2.remap(img, map_x, map_y, interpolation=cv2.INTER_LINEAR, borderMode=cv2.BORDER_WRAP)


def face_dirs(face: str) -> np.ndarray:
    # OpenGL-style cubemap face local coords, Y-up.
    a = np.linspace(-1.0, 1.0, FACE, dtype=np.float32)
    u, v = np.meshgrid(a, a)
    if face == "+X":
        xyz = np.stack([np.ones_like(u), -v, -u], axis=-1)
    elif face == "-X":
        xyz = np.stack([-np.ones_like(u), -v, u], axis=-1)
    elif face == "+Y":
        xyz = np.stack([u, np.ones_like(u), v], axis=-1)
    elif face == "-Y":
        xyz = np.stack([u, -np.ones_like(u), -v], axis=-1)
    elif face == "+Z":
        xyz = np.stack([u, -v, np.ones_like(u)], axis=-1)
    elif face == "-Z":
        xyz = np.stack([-u, -v, -np.ones_like(u)], axis=-1)
    else:
        raise ValueError(face)
    flat = xyz.reshape(-1, 3)
    n = np.linalg.norm(flat, axis=1, keepdims=True)
    return flat / np.maximum(n, 1e-8)


def convert(name: str) -> None:
    hdr_path = SRC / f"{name}_1k.hdr"
    out_dir = ROOT / name
    out_dir.mkdir(parents=True, exist_ok=True)

    # OpenCV reads Radiance HDR as BGR float.
    bgr = cv2.imread(str(hdr_path), cv2.IMREAD_UNCHANGED)
    if bgr is None:
        raise SystemExit(f"failed to read {hdr_path}")
    rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)

    face_names = ["+X", "-X", "+Y", "-Y", "+Z", "-Z"]
    file_names = ["px", "nx", "py", "ny", "pz", "nz"]
    for face, fname in zip(face_names, file_names):
        sampled = sample_equirect(rgb, face_dirs(face)).reshape(FACE, FACE, 3)
        ldr = tonemap_reinhard(sampled, EXPOSURE[name])
        rgba = np.dstack([ldr, np.full((FACE, FACE), 255, dtype=np.uint8)])
        Image.fromarray(rgba, mode="RGBA").save(out_dir / f"{fname}.png", optimize=True)
        print(f"wrote {out_dir / fname}.png")


def main() -> None:
    for name in EXPOSURE:
        convert(name)


if __name__ == "__main__":
    main()
