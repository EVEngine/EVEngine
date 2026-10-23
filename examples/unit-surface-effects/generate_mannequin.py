#!/usr/bin/env python3
"""Reproduce the original neutral armored lookdev mesh; no downloaded assets."""

from __future__ import annotations
import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def normal(v):
    length = math.sqrt(dot(v, v))
    return tuple(x / length for x in v)


class Mesh:
    def __init__(self):
        self.positions = []
        self.normals = []
        self.uv = []
        self.indices = []

    def triangle(self, a, b, c, material=0, expected=None, normals=None, edges=None):
        n = cross(sub(b, a), sub(c, a))
        if dot(n, n) < 1e-14:
            return
        if expected and dot(n, expected) < 0:
            b, c = c, b
            if normals:
                normals = (normals[0], normals[2], normals[1])
            if edges:
                edges = (edges[0], edges[2], edges[1])
            n = tuple(-x for x in n)
        n = normal(n)
        offset = len(self.positions) // 3
        for i, p in enumerate((a, b, c)):
            self.positions.extend(p)
            self.normals.extend(normals[i] if normals else n)
            self.uv.extend((float(material), edges[i] if edges else 0.0))
            self.indices.append(offset + i)

    def ellipsoid(self, center, radii, material=0, segments=20, rings=12):
        def point(i, j):
            theta = math.pi * i / rings
            phi = 2 * math.pi * j / segments
            v = (math.sin(theta) * math.cos(phi), math.cos(theta), math.sin(theta) * math.sin(phi))
            p = tuple(center[k] + radii[k] * v[k] for k in range(3))
            return p, normal(tuple(v[k] / radii[k] for k in range(3)))

        for i in range(rings):
            for j in range(segments):
                q = [point(i, j), point(i + 1, j), point(i + 1, j + 1), point(i, j + 1)]
                for ids in ((0, 1, 2), (0, 2, 3)):
                    points = [q[k][0] for k in ids]
                    outward = tuple(sum(p[k] for p in points) / 3 - center[k] for k in range(3))
                    self.triangle(*points, material, outward, tuple(q[k][1] for k in ids))

    def plate(self, outline, front, back, bevel=0.035, material=0):
        cx = sum(p[0] for p in outline) / len(outline)
        cy = sum(p[1] for p in outline) / len(outline)
        radius = sum(math.hypot(p[0] - cx, p[1] - cy) for p in outline) / len(outline)
        scale = max(0.1, 1 - bevel / radius)
        inner = [(cx + (x - cx) * scale, cy + (y - cy) * scale, front) for x, y in outline]
        outer = [(x, y, front - bevel) for x, y in outline]
        rear = [(x, y, back) for x, y in outline]
        for i in range(len(outline)):
            j = (i + 1) % len(outline)
            self.triangle(
                (cx, cy, front),
                inner[i],
                inner[j],
                material,
                (0, 0, 1),
                edges=(radius, bevel, bevel),
            )
            self.triangle((cx, cy, back), rear[i], rear[j], material, (0, 0, -1))
            mid = ((outer[i][0] + outer[j][0]) / 2 - cx, (outer[i][1] + outer[j][1]) / 2 - cy, 0)
            for a, b, c in ((inner[i], outer[i], outer[j]), (inner[i], outer[j], inner[j])):
                self.triangle(a, b, c, material, (mid[0], mid[1], 0.5))
            for a, b, c in ((outer[i], rear[i], rear[j]), (outer[i], rear[j], outer[j])):
                self.triangle(a, b, c, material, mid)

    def cube(self, center, half):
        for axis in range(3):
            for side in (-1, 1):
                axes = [k for k in range(3) if k != axis]
                q = []
                for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                    p = list(center)
                    p[axis] += side * half
                    p[axes[0]] += a * half
                    p[axes[1]] += b * half
                    q.append(tuple(p))
                expected = [0, 0, 0]
                expected[axis] = side
                self.triangle(q[0], q[1], q[2], 0, expected)
                self.triangle(q[0], q[2], q[3], 0, expected)


def build():
    m = Mesh()
    # Articulated dark undersuit, neck and rounded helmet. +Z is the face.
    m.ellipsoid((0, 1.75, 0), (0.64, 0.85, 0.29), 1)
    m.ellipsoid((0, 2.43, 0), (0.19, 0.23, 0.18), 1)
    m.ellipsoid((0, 2.98, 0), (0.37, 0.48, 0.32), 0)
    # Symmetric chest/collar plates with a deliberate central seam.
    for side in (-1, 1):

        def mirror(points):
            return [(side * x, y) for x, y in points]

        m.plate(
            mirror([(0.055, 2.30), (0.49, 2.34), (0.67, 2.11), (0.52, 1.83), (0.08, 1.92)]),
            0.38,
            -0.12,
            0.045,
        )
        m.plate(
            mirror([(0.07, 2.40), (0.37, 2.50), (0.60, 2.36), (0.46, 2.30), (0.09, 2.32)]),
            0.29,
            -0.15,
            0.028,
        )
        m.ellipsoid((side * 0.79, 2.21, 0), (0.32, 0.29, 0.32), 1)
        m.plate(
            mirror([(0.62, 2.45), (0.99, 2.39), (1.16, 2.15), (0.98, 2.02), (0.67, 2.13)]),
            0.34,
            -0.27,
            0.05,
        )
        m.ellipsoid((side * 0.99, 1.78, 0), (0.20, 0.38, 0.22), 1)
        m.plate(
            mirror([(0.85, 2.04), (1.10, 1.99), (1.20, 1.52), (0.96, 1.40), (0.80, 1.63)]),
            0.26,
            -0.17,
            0.035,
        )
        m.ellipsoid((side * 1.04, 1.36, 0), (0.17, 0.16, 0.18), 1)
        m.plate(
            mirror([(0.91, 1.29), (1.19, 1.31), (1.24, 0.76), (1.03, 0.64), (0.86, 0.81)]),
            0.31,
            -0.18,
            0.04,
        )
        m.plate(
            mirror([(0.10, 3.13), (0.30, 3.10), (0.32, 2.91), (0.14, 2.71), (0.07, 2.85)]),
            0.30,
            0.16,
            0.018,
        )
    # Helmet visor, cheek planes, sternum and overlapping abdominal armor.
    m.plate([(-0.27, 3.02), (0.27, 3.02), (0.22, 2.93), (-0.22, 2.93)], 0.326, 0.29, 0.008, 2)
    m.plate(
        [(-0.12, 2.95), (0, 2.98), (0.12, 2.95), (0.10, 2.72), (0, 2.64), (-0.10, 2.72)],
        0.34,
        0.15,
        0.018,
    )
    m.plate(
        [(-0.07, 2.22), (0.07, 2.22), (0.13, 2.03), (0, 1.89), (-0.13, 2.03)], 0.435, 0.30, 0.018
    )
    for i in range(3):
        y = 1.69 - i * 0.26
        w = 0.49 - i * 0.035
        m.plate(
            [
                (-w, y + 0.11),
                (w, y + 0.11),
                (w * 0.84, y - 0.13),
                (0, y - 0.17),
                (-w * 0.84, y - 0.13),
            ],
            0.31 - i * 0.01,
            -0.19,
            0.03,
        )
    m.plate(
        [(-0.39, 0.96), (0.39, 0.96), (0.46, 0.69), (0.28, 0.49), (-0.28, 0.49), (-0.46, 0.69)],
        0.25,
        -0.20,
        0.04,
    )
    # Surface-associated fragment seeds, shared unit-space 0.125 lattice.
    # One small cube per occupied cell; no disconnected per-part origins.
    cells = set()
    for i in range(0, len(m.positions), 9):
        tri = [tuple(m.positions[i + j * 3 + k] for k in range(3)) for j in range(3)]
        for a in range(4):
            for b in range(4 - a):
                u = a / 3
                v = b / 3
                p = tuple(tri[0][k] * u + tri[1][k] * v + tri[2][k] * (1 - u - v) for k in range(3))
                cells.add(tuple(math.floor(x / 0.125) for x in p))
    fragments = Mesh()
    for cell in sorted(cells):
        if (cell[0] * 73856093 ^ cell[1] * 19349663 ^ cell[2] * 83492791) % 4:
            continue
        fragments.cube(tuple((v + 0.5) * 0.125 for v in cell), 0.035)
    ring = Mesh()
    for i in range(96):
        a = 2 * math.pi * i / 96
        b = 2 * math.pi * (i + 1) / 96
        points = [
            (math.cos(a) * 0.992, math.sin(a) * 0.992, 0),
            (math.cos(a) * 1.008, math.sin(a) * 1.008, 0),
            (math.cos(b) * 1.008, math.sin(b) * 1.008, 0),
            (math.cos(b) * 0.992, math.sin(b) * 0.992, 0),
        ]
        ring.triangle(points[0], points[1], points[2], 0, (0, 0, 1))
        ring.triangle(points[0], points[2], points[3], 0, (0, 0, 1))
    orbits = Mesh()
    for band in range(3):

        def torus(i, j):
            angle, tube = i * 2 * math.pi / 96, j * 2 * math.pi / 6
            n = (math.cos(angle) * math.cos(tube), math.sin(angle) * math.cos(tube), math.sin(tube))
            p = (
                math.cos(angle) + n[0] * 0.009,
                math.sin(angle) + n[1] * 0.009,
                band + n[2] * 0.009,
            )
            return p, n

        for i in range(96):
            for j in range(6):
                quad = [torus(i, j), torus(i + 1, j), torus(i + 1, j + 1), torus(i, j + 1)]
                for ids in ((0, 1, 2), (0, 2, 3)):
                    pts = tuple(quad[k][0] for k in ids)
                    ns = tuple(quad[k][1] for k in ids)
                    expected = tuple(sum(n[k] for n in ns) / 3 for k in range(3))
                    orbits.triangle(*pts, expected=expected, normals=ns)
    wisps = Mesh()
    for strand in range(10):
        phase = strand * 2 * math.pi / 10

        def point(j, side):
            along = j / 32
            y = 0.55 + along * 2.95
            width = 0.02 + math.sin(math.pi * along) * 0.09
            x = (
                math.cos(phase + along * 2.8) * (1.13 - 0.45 * along)
                + math.sin(along * 9 + phase) * 0.18
            )
            z = math.sin(phase + along * 2.8) * 0.63 + math.cos(along * 7 + phase) * 0.08
            return (x + side * width, y, z)

        for j in range(32):
            a, b, c, d = point(j, -1), point(j, 1), point(j + 1, 1), point(j + 1, -1)
            for pts, uv in [
                ((a, b, c), [-1, j / 32, 1, j / 32, 1, (j + 1) / 32]),
                ((a, c, d), [-1, j / 32, 1, (j + 1) / 32, -1, (j + 1) / 32]),
            ]:
                wisps.triangle(*pts, expected=(0, 0, 1))
                wisps.uv[-6:] = uv
    return m, fragments, ring, orbits, wisps


def encode(mesh, name):
    def array(values, floating=True):
        if floating:
            return "[" + ",".join(f"{float(v):.6f}" for v in values) + "]"
        return "[" + ",".join(str(v) for v in values) + "]"

    return f"    local {name}=gfx.newMeshFromArrays(\n        {array(mesh.positions)},\n        {array(mesh.normals)},\n        {array(mesh.uv)},\n        {len(mesh.positions) // 3},{array(mesh.indices, False)},{len(mesh.indices)});\n"


def main():
    mesh, fragments, ring, orbits, wisps = build()
    for item in (mesh, fragments, ring, orbits, wisps):
        assert len(item.positions) == len(item.normals)
        assert len(item.positions) // 3 == len(item.uv) // 2 == len(item.indices)
        assert all(math.isfinite(v) for v in item.positions + item.normals)
        for i in range(0, len(item.indices), 3):
            a, b, c = [tuple(item.positions[(i + j) * 3 + k] for k in range(3)) for j in range(3)]
            assert (
                dot(cross(sub(b, a), sub(c, a)), tuple(item.normals[i * 3 + k] for k in range(3)))
                > 0
            )
    output = "// Generated by generate_mannequin.py. Original authored geometry, CC0.\n"
    output += "// Every vertex is in the same unit coordinate system.\n"
    output += (
        "armoredUnitMeshes <- function() {\n"
        + encode(mesh, "surface")
        + encode(fragments, "fragments")
        + encode(ring, "impact")
        + encode(orbits, "orbits")
        + encode(wisps, "wisps")
    )
    output += "    return {surface=surface,fragments=fragments,impact=impact,orbits=orbits,wisps=wisps};\n};\n"
    (HERE / "mannequin.nut").write_text(output, encoding="utf-8")
    print(
        f"Armor: {len(mesh.indices) // 3} triangles; fragments: {len(fragments.indices) // 36} cells; normals/winding PASS"
    )


if __name__ == "__main__":
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")
    main()
