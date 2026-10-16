"""Compile the editable SCAD into a coloured, Y-up, metre-scale OBJ.

Run: python3 assets/models/type055/build.py
Requires OpenSCAD; preview additionally requires Pillow/numpy.
"""

import math
from pathlib import Path
import struct
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent
PARTS = {
    "hull": (0.46, 0.53, 0.57),
    "fittings": (0.67, 0.72, 0.74),
    "dark": (0.13, 0.20, 0.24),
    "markings": (0.91, 0.91, 0.81),
}


def build():
    faces = []
    with tempfile.TemporaryDirectory(prefix="type055-") as temporary:
        for part, colour in PARTS.items():
            target = Path(temporary) / (part + ".stl")
            subprocess.run([
                "openscad", "--hardwarnings", "--export-format", "binstl",
                "-D", f'part="{part}"', "-o", str(target),
                str(ROOT / "type055.scad"),
            ], check=True, timeout=120)
            data = target.read_bytes()
            count = struct.unpack_from("<I", data, 80)[0]
            if len(data) != 84 + count * 50 or count == 0:
                raise ValueError(f"Invalid STL: {part}")
            for index in range(count):
                record = struct.unpack_from("<12fH", data, 84 + index * 50)
                triangle = [record[3 + i * 3:6 + i * 3] for i in range(3)]
                if not all(math.isfinite(v) for p in triangle for v in p):
                    raise ValueError("Non-finite vertex")
                faces.append((part, colour, triangle))
    # The 1:1000 source's millimetre values equal the real vessel's metres.
    # Proper rotation (x,y,z) -> (x,z,-y), preserving face winding.
    lines = ["# Type 055 display model; metres; Y up; +X bow", "mtllib type055.mtl"]
    current = None
    for index, (part, _, triangle) in enumerate(faces):
        if part != current:
            lines.extend(["g " + part, "usemtl " + part])
            current = part
        for x, y, z in triangle:
            lines.append(f"v {x:.6f} {z:.6f} {-y:.6f}")
        first = index * 3 + 1
        lines.append(f"f {first} {first + 1} {first + 2}")
    (ROOT / "type055.obj").write_text("\n".join(lines) + "\n", encoding="utf-8")
    materials = []
    for name, colour in PARTS.items():
        materials.extend([f"newmtl {name}", "Kd " + " ".join(map(str, colour)),
                          "Ka 0.08 0.08 0.08", "Ks 0.15 0.15 0.15", "Ns 28", ""])
    (ROOT / "type055.mtl").write_text("\n".join(materials), encoding="utf-8")
    print(f"Exported {len(faces)} triangles to {ROOT / 'type055.obj'}")
    return faces


def preview(faces, view=(-49, 24), filename="preview.png"):
    """Rasterize triangles with a per-pixel depth buffer, including thin details."""
    import numpy as np
    from PIL import Image, ImageDraw, ImageFont

    width, height = 2240, 1260
    pixels = np.empty((height, width, 3), dtype=np.uint8)
    pixels[:] = (16, 30, 41)
    depth = np.full((height, width), -np.inf)
    azimuth, elevation = np.radians(view)
    eye = np.array([np.cos(elevation) * np.cos(azimuth),
                    np.cos(elevation) * np.sin(azimuth), np.sin(elevation)])
    right = np.array([-np.sin(azimuth), np.cos(azimuth), 0])
    up = np.cross(eye, right)
    light = np.array([0.4, -0.5, 0.9])
    light /= np.linalg.norm(light)
    scale = 11.5
    for _, colour, triangle in faces:
        vertices = np.array(triangle)
        screen = np.column_stack((width * 0.5 + vertices @ right * scale,
                                  height * 0.59 - (vertices - [0, 0, 10]) @ up * scale))
        z = vertices @ eye
        lower = np.maximum(np.floor(screen.min(axis=0)).astype(int), [0, 0])
        upper = np.minimum(np.ceil(screen.max(axis=0)).astype(int), [width - 1, height - 1])
        if np.any(lower > upper):
            continue
        x, y = np.meshgrid(np.arange(lower[0], upper[0] + 1) + 0.5,
                           np.arange(lower[1], upper[1] + 1) + 0.5)
        a, b, c = screen
        denominator = (b[1]-c[1])*(a[0]-c[0]) + (c[0]-b[0])*(a[1]-c[1])
        if abs(denominator) < 1e-10:
            continue
        w0 = ((b[1]-c[1])*(x-c[0]) + (c[0]-b[0])*(y-c[1])) / denominator
        w1 = ((c[1]-a[1])*(x-c[0]) + (a[0]-c[0])*(y-c[1])) / denominator
        w2 = 1 - w0 - w1
        fragment_depth = w0*z[0] + w1*z[1] + w2*z[2]
        region = np.s_[lower[1]:upper[1]+1, lower[0]:upper[0]+1]
        visible = (w0 >= -1e-8) & (w1 >= -1e-8) & (w2 >= -1e-8) & (fragment_depth > depth[region])
        normal = np.cross(vertices[1]-vertices[0], vertices[2]-vertices[0])
        length = np.linalg.norm(normal)
        shade = 0.55 + 0.45 * max(0, np.dot(normal / length, light)) if length else 0.7
        pixels[region][visible] = np.clip(np.array(colour) * shade * 255, 0, 255).astype(np.uint8)
        depth[region][visible] = fragment_depth[visible]
    picture = Image.fromarray(pixels)
    draw = ImageDraw.Draw(picture)
    font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
    title = ImageFont.truetype(font_path, 56)
    caption = ImageFont.truetype(font_path, 20)
    draw.text((145, 125), "TYPE 055", font=title, fill="#e2eaed")
    draw.text((148, 215), "101 / NANCHANG  ·  OPENSCAD STUDY", font=caption, fill="#90aebd")
    draw.text((148, 1120), "180 m  /  112 VLS CELLS  /  INTEGRATED MAST", font=caption, fill="#90aebd")
    picture.save(ROOT / filename)


if __name__ == "__main__":
    geometry = build()
    preview(geometry)
    preview(geometry, view=(-90, 90), filename="top.png")
