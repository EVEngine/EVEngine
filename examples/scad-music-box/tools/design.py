#!/usr/bin/env python3
"""Authoritative SCAD music box: joints, part geometry, tune pins, OBJ emitters.

Dimensions are millimetres, OpenSCAD Z-up. EveScript consumes metres/Y-up OBJ
batches, musicbox.rig.json, and the pin schedule in assets/tune.nut.

Mechanism (classic cylinder music box):
  shell → lid (hinged) / bed (fixed plate) / barrel (mainspring) /
         cylinder (pin drum) / comb (tuned tines) / governor (air brake) /
         dancer (figurine that spins while the tune plays).
"""
from __future__ import annotations

import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets"

# Parent-relative OpenSCAD millimetres (Z-up). Each part mesh is authored in that
# joint's local frame with its origin at the joint pivot.
JOINTS = [
    {"name": "shell", "parent": None, "pivot": [0, 0, 0], "axis": [0, 0, 1], "limits": [0, 0]},
    {
        "name": "lid",
        "parent": "shell",
        "pivot": [0, 55, 58],
        "axis": [1, 0, 0],
        "limits": [-95, 0],
    },
    {"name": "bed", "parent": "shell", "pivot": [0, 0, 18], "axis": [0, 0, 1], "limits": [0, 0]},
    {
        "name": "barrel",
        "parent": "bed",
        "pivot": [-58, 8, 22],
        "axis": [0, 0, 1],
        "limits": [-1e6, 1e6],
    },
    {
        "name": "cylinder",
        "parent": "bed",
        "pivot": [8, -6, 28],
        "axis": [1, 0, 0],
        "limits": [-1e6, 1e6],
    },
    {"name": "comb", "parent": "bed", "pivot": [8, 22, 22], "axis": [0, 0, 1], "limits": [0, 0]},
    {
        "name": "governor",
        "parent": "bed",
        "pivot": [62, 10, 34],
        "axis": [0, 0, 1],
        "limits": [-1e6, 1e6],
    },
    {
        "name": "dancer",
        "parent": "shell",
        "pivot": [0, -28, 62],
        "axis": [0, 0, 1],
        "limits": [-1e6, 1e6],
    },
]

PARTS = [
    {"name": "shell", "joint": "shell", "tint": [0.42, 0.24, 0.12], "metal": 0.04, "rough": 0.72},
    {"name": "lid", "joint": "lid", "tint": [0.48, 0.28, 0.14], "metal": 0.06, "rough": 0.55},
    {"name": "bed", "joint": "bed", "tint": [0.72, 0.58, 0.28], "metal": 0.78, "rough": 0.32},
    {"name": "barrel", "joint": "barrel", "tint": [0.78, 0.62, 0.30], "metal": 0.82, "rough": 0.28},
    {"name": "cylinder", "joint": "cylinder", "tint": [0.55, 0.56, 0.58], "metal": 0.70, "rough": 0.35},
    {"name": "comb", "joint": "comb", "tint": [0.82, 0.78, 0.62], "metal": 0.88, "rough": 0.22},
    {"name": "governor", "joint": "governor", "tint": [0.62, 0.50, 0.22], "metal": 0.75, "rough": 0.30},
    {"name": "dancer", "joint": "dancer", "tint": [0.86, 0.72, 0.78], "metal": 0.08, "rough": 0.48},
]

PART_NAMES = tuple(part["name"] for part in PARTS)

# Gear ratios from the mainspring barrel (positive = same sense as barrel).
BARREL_TO_CYLINDER = 1.0
BARREL_TO_GOVERNOR = 9.0
CYLINDER_TO_DANCER = 0.55

# Eight tuned comb teeth (low → high). Frequencies are equal temperament A4=440.
TOOTH_NAMES = ("C5", "D5", "E5", "F5", "G5", "A5", "B5", "C6")
TOOTH_HZ = (523.25, 587.33, 659.25, 698.46, 783.99, 880.00, 987.77, 1046.50)

# Public-domain lullaby fragment (Twinkle Twinkle) as (angle_deg, tooth_index).
# One full cylinder turn = one chorus. Pins sit on the drum surface.
TUNE_PINS: list[tuple[float, int]] = [
    (0.0, 0),
    (15.0, 0),
    (30.0, 4),
    (45.0, 4),
    (60.0, 5),
    (75.0, 5),
    (90.0, 4),
    (120.0, 3),
    (135.0, 3),
    (150.0, 2),
    (165.0, 2),
    (180.0, 1),
    (195.0, 1),
    (210.0, 0),
    (240.0, 4),
    (255.0, 4),
    (270.0, 3),
    (285.0, 3),
    (300.0, 2),
    (315.0, 2),
    (330.0, 1),
    (345.0, 1),
]


def scad_escape(name: str) -> str:
    return name.replace("\\", "\\\\").replace('"', '\\"')


def emit_rig() -> dict:
    return {
        "schema": "eve.scad-rig",
        "schemaVersion": 1,
        "unknownFields": "reject",
        "coordinateSystem": "openscad-mm-z-up",
        "joints": [
            {
                "name": joint["name"],
                "parent": joint["parent"],
                "pivot": joint["pivot"],
                "axis": joint["axis"],
                "limits": joint["limits"],
            }
            for joint in JOINTS
        ],
        "parts": [{"name": part["name"], "joint": part["joint"]} for part in PARTS],
    }


def emit_scad() -> str:
    pin_lines = []
    for angle, tooth in TUNE_PINS:
        # Cylinder local: X along drum axis, rotation around X. Pin rows along X.
        x = -42.0 + tooth * 12.0
        pin_lines.append(
            f"    rotate([{angle:.1f}, 0, 0]) translate([{x:.1f}, 0, 24]) "
            "box(3.2, 3.2, 10);"
        )
    pins_block = "\n".join(pin_lines)

    lines = [
        "// Articulated cylinder music box — rotatable SCAD mechanical diagram.",
        '// Compile one joint mesh with: openscad -D \'part="cylinder"\' -o cylinder.stl musicbox.scad',
        'part = "shell";',
        "",
        "module box(sx, sy, sz) { cube([sx, sy, sz], center = true); }",
        "module cyl(h, r, r2 = undef, fn = 28) {",
        "    cylinder(h = h, r1 = r, r2 = (r2 == undef ? r : r2), center = true, $fn = fn);",
        "}",
        "",
        "module shell_geom() {",
        "    // Ornate wooden shell with cutaway front so the mechanism stays readable.",
        "    difference() {",
        "        translate([0, 0, 28]) box(180, 120, 56);",
        "        translate([0, -8, 36]) box(156, 88, 48);",
        "        translate([0, -62, 40]) box(140, 20, 36); // front window",
        "    }",
        "    // Feet.",
        "    for (x = [-70, 70]) for (y = [-42, 42])",
        "        translate([x, y, 4]) cyl(8, 8, fn = 16);",
        "    // Inner shelf for the dancer platform.",
        "    translate([0, -28, 54]) box(50, 40, 4);",
        "    translate([0, -28, 48]) cyl(8, 6, fn = 16);",
        "    // Hinge knuckles on the back wall.",
        "    for (x = [-40, 0, 40]) translate([x, 55, 58]) rotate([0, 90, 0]) cyl(16, 4, fn = 12);",
        "}",
        "",
        "module lid_geom() {",
        "    // Local origin = hinge axis. Lid body opens toward +Z / -Y when rotated.",
        "    translate([0, -55, 6]) box(176, 112, 8);",
        "    // Beveled glass well (opaque panel stand-in).",
        "    translate([0, -55, 2]) box(140, 80, 4);",
        "    // Front lip.",
        "    translate([0, -108, 10]) box(160, 8, 12);",
        "    // Hinge barrels.",
        "    for (x = [-40, 0, 40]) rotate([0, 90, 0]) translate([0, 0, x]) cyl(14, 5, fn = 14);",
        "}",
        "",
        "module bed_geom() {",
        "    // Brass bedplate with axle supports.",
        "    translate([0, 0, 2]) box(150, 90, 4);",
        "    // Cylinder axle stands.",
        "    translate([-55, -6, 18]) box(10, 18, 32);",
        "    translate([70, -6, 18]) box(10, 18, 32);",
        "    translate([-55, -6, 28]) rotate([0, 90, 0]) cyl(8, 5, fn = 14);",
        "    translate([70, -6, 28]) rotate([0, 90, 0]) cyl(8, 5, fn = 14);",
        "    // Comb mount rail.",
        "    translate([8, 22, 8]) box(110, 14, 10);",
        "    // Barrel well.",
        "    translate([-58, 8, 8]) cyl(8, 22, fn = 28);",
        "    // Governor tower.",
        "    translate([62, 10, 14]) cyl(20, 6, fn = 16);",
        "}",
        "",
        "module barrel_geom() {",
        "    // Mainspring barrel + winding arbor.",
        "    cyl(16, 20, fn = 36);",
        "    translate([0, 0, 12]) cyl(6, 22, fn = 36);",
        "    translate([0, 0, -12]) cyl(6, 22, fn = 36);",
        "    for (a = [0:30:330]) rotate([0, 0, a]) translate([18, 0, 0]) box(6, 3, 14);",
        "    translate([0, 0, 18]) cyl(12, 4, fn = 12);",
        "    translate([0, 0, 26]) box(18, 4, 4); // winding key stub",
        "}",
        "",
        "module cylinder_geom() {",
        "    // Local origin = drum axle. Drum + end flanges + tune pins.",
        "    rotate([0, 90, 0]) cyl(100, 22, fn = 40);",
        "    translate([-52, 0, 0]) rotate([0, 90, 0]) cyl(6, 26, fn = 32);",
        "    translate([52, 0, 0]) rotate([0, 90, 0]) cyl(6, 26, fn = 32);",
        "    // Drive gear at +X end.",
        "    translate([58, 0, 0]) rotate([0, 90, 0]) {",
        "        cyl(8, 16, fn = 28);",
        "        for (a = [0:15:345]) rotate([a, 0, 0]) translate([0, 0, 14]) box(4, 3, 6);",
        "    }",
        "    // Pin schedule (one chorus).",
        pins_block,
        "}",
        "",
        "module comb_geom() {",
        "    // Tuned comb: longer (lower) teeth toward -X.",
        "    translate([0, 0, 2]) box(110, 12, 4);",
        "    for (i = [0:7]) {",
        "        length = 28 + (7 - i) * 4;",
        "        translate([-42 + i * 12, -length * 0.5, 0]) box(8, length, 2.4);",
        "        translate([-42 + i * 12, -length, 0]) box(6, 4, 3);",
        "    }",
        "}",
        "",
        "module governor_geom() {",
        "    // Air-brake fan / worm governor.",
        "    cyl(10, 5, fn = 16);",
        "    translate([0, 0, 8]) cyl(4, 7, fn = 16);",
        "    for (a = [0:90:270]) rotate([0, 0, a]) {",
        "        translate([0, 14, 10]) box(3, 22, 1.6);",
        "        translate([0, 24, 10]) box(10, 6, 1.2);",
        "    }",
        "}",
        "",
        "module dancer_geom() {",
        "    // Compact ballerina figurine (skirt + torso + arms).",
        "    translate([0, 0, 2]) cyl(4, 10, fn = 24);",
        "    translate([0, 0, 10]) cyl(14, 16, r2 = 4, fn = 28); // skirt",
        "    translate([0, 0, 22]) cyl(12, 4, fn = 16);",
        "    translate([0, 0, 30]) cyl(8, 5, fn = 16); // head",
        "    translate([-10, 0, 24]) rotate([0, 55, 0]) box(16, 3, 3);",
        "    translate([10, 0, 24]) rotate([0, -55, 0]) box(16, 3, 3);",
        "}",
        "",
    ]
    for name in PART_NAMES:
        lines.append(f'if (part == "{scad_escape(name)}") {name}_geom();')
    lines.append("")
    return "\n".join(lines)


def mm_to_m_yup(x: float, y: float, z: float) -> tuple[float, float, float]:
    return (x / 1000.0, z / 1000.0, -y / 1000.0)


def box_triangles(cx, cy, cz, sx, sy, sz):
    hx, hy, hz = sx * 0.5, sy * 0.5, sz * 0.5
    corners = [
        (cx - hx, cy - hy, cz - hz),
        (cx + hx, cy - hy, cz - hz),
        (cx + hx, cy + hy, cz - hz),
        (cx - hx, cy + hy, cz - hz),
        (cx - hx, cy - hy, cz + hz),
        (cx + hx, cy - hy, cz + hz),
        (cx + hx, cy + hy, cz + hz),
        (cx - hx, cy + hy, cz + hz),
    ]
    faces = [
        (0, 3, 2, 1),
        (4, 5, 6, 7),
        (0, 1, 5, 4),
        (2, 3, 7, 6),
        (0, 4, 7, 3),
        (1, 2, 6, 5),
    ]
    tris = []
    for a, b, c, d in faces:
        tris.append((corners[a], corners[b], corners[c]))
        tris.append((corners[a], corners[c], corners[d]))
    return tris


def cyl_triangles(cx, cy, cz, h, r, axis="z", sides=24, r2=None):
    if r2 is None:
        r2 = r
    tris = []
    hh = h * 0.5

    def point(i, along, radius):
        a = 2 * math.pi * i / sides
        c, s = math.cos(a) * radius, math.sin(a) * radius
        if axis == "x":
            return (cx + along, cy + c, cz + s)
        if axis == "y":
            return (cx + c, cy + along, cz + s)
        return (cx + c, cy + s, cz + along)

    for i in range(sides):
        n = (i + 1) % sides
        tris.append((point(i, -hh, r), point(n, -hh, r), point(n, hh, r2)))
        tris.append((point(i, -hh, r), point(n, hh, r2), point(i, hh, r2)))
        if axis == "z":
            center0, center1 = (cx, cy, cz - hh), (cx, cy, cz + hh)
        elif axis == "x":
            center0, center1 = (cx - hh, cy, cz), (cx + hh, cy, cz)
        else:
            center0, center1 = (cx, cy - hh, cz), (cx, cy + hh, cz)
        tris.append((center0, point(n, -hh, r), point(i, -hh, r)))
        tris.append((center1, point(i, hh, r2), point(n, hh, r2)))
    return tris


def rotate_x_point(x, y, z, degrees):
    rad = math.radians(degrees)
    c, s = math.cos(rad), math.sin(rad)
    return (x, y * c - z * s, y * s + z * c)


def rotate_y_point(x, y, z, degrees):
    rad = math.radians(degrees)
    c, s = math.cos(rad), math.sin(rad)
    return (x * c + z * s, y, -x * s + z * c)


def rotate_z_point(x, y, z, degrees):
    rad = math.radians(degrees)
    c, s = math.cos(rad), math.sin(rad)
    return (x * c - y * s, x * s + y * c, z)


def map_tris(tris, fn):
    return [tuple(fn(*v) for v in tri) for tri in tris]


def part_triangles(name: str):
    if name == "shell":
        tris = []
        # Hollow shell: floor, three walls, rim; open front for mechanism visibility.
        tris += box_triangles(0, 0, 6, 180, 120, 12)  # floor
        tris += box_triangles(0, 0, 54, 180, 120, 6)  # top rim
        tris += box_triangles(0, 55, 30, 180, 10, 48)  # back
        tris += box_triangles(-86, 0, 30, 8, 120, 48)  # left
        tris += box_triangles(86, 0, 30, 8, 120, 48)  # right
        tris += box_triangles(0, -55, 18, 180, 8, 24)  # front apron
        tris += box_triangles(0, -28, 54, 50, 40, 4)
        tris += cyl_triangles(0, -28, 48, 8, 6, sides=16)
        for x in (-70, 70):
            for y in (-42, 42):
                tris += cyl_triangles(x, y, 4, 8, 8, sides=12)
        for x in (-40, 0, 40):
            tris += map_tris(
                cyl_triangles(0, 0, 0, 16, 4, axis="x", sides=12),
                lambda px, py, pz, xx=x: (px + xx, py + 55, pz + 58),
            )
        return tris
    if name == "lid":
        tris = box_triangles(0, -55, 6, 176, 112, 8)
        tris += box_triangles(0, -55, 2, 140, 80, 4)
        tris += box_triangles(0, -108, 10, 160, 8, 12)
        for x in (-40, 0, 40):
            tris += map_tris(
                cyl_triangles(0, 0, 0, 14, 5, axis="x", sides=12),
                lambda px, py, pz, xx=x: (px + xx, py, pz),
            )
        return tris
    if name == "bed":
        tris = box_triangles(0, 0, 2, 150, 90, 4)
        tris += box_triangles(-55, -6, 18, 10, 18, 32)
        tris += box_triangles(70, -6, 18, 10, 18, 32)
        tris += map_tris(
            cyl_triangles(0, 0, 0, 8, 5, axis="x", sides=12),
            lambda px, py, pz: (px - 55, py - 6, pz + 28),
        )
        tris += map_tris(
            cyl_triangles(0, 0, 0, 8, 5, axis="x", sides=12),
            lambda px, py, pz: (px + 70, py - 6, pz + 28),
        )
        tris += box_triangles(8, 22, 8, 110, 14, 10)
        tris += cyl_triangles(-58, 8, 8, 8, 22, sides=28)
        tris += cyl_triangles(62, 10, 14, 20, 6, sides=16)
        return tris
    if name == "barrel":
        tris = cyl_triangles(0, 0, 0, 16, 20, sides=32)
        tris += cyl_triangles(0, 0, 12, 6, 22, sides=32)
        tris += cyl_triangles(0, 0, -12, 6, 22, sides=32)
        for a in range(0, 360, 30):
            p = rotate_z_point(18, 0, 0, a)
            tris += box_triangles(p[0], p[1], 0, 6, 3, 14)
        tris += cyl_triangles(0, 0, 18, 12, 4, sides=12)
        tris += box_triangles(0, 0, 26, 18, 4, 4)
        return tris
    if name == "cylinder":
        tris = cyl_triangles(0, 0, 0, 100, 22, axis="x", sides=36)
        for x in (-52, 52):
            tris += cyl_triangles(x, 0, 0, 6, 26, axis="x", sides=28)
        tris += cyl_triangles(58, 0, 0, 8, 16, axis="x", sides=24)
        for a in range(0, 360, 15):
            p = rotate_x_point(58, 0, 14, a)
            tris += box_triangles(p[0], p[1], p[2], 4, 3, 6)
        for angle, tooth in TUNE_PINS:
            x = -42.0 + tooth * 12.0
            p = rotate_x_point(x, 0, 24, angle)
            tris += box_triangles(p[0], p[1], p[2], 3.2, 3.2, 10)
        return tris
    if name == "comb":
        tris = box_triangles(0, 0, 2, 110, 12, 4)
        for i in range(8):
            length = 28 + (7 - i) * 4
            x = -42 + i * 12
            tris += box_triangles(x, -length * 0.5, 0, 8, length, 2.4)
            tris += box_triangles(x, -length, 0, 6, 4, 3)
        return tris
    if name == "governor":
        tris = cyl_triangles(0, 0, 0, 10, 5, sides=14)
        tris += cyl_triangles(0, 0, 8, 4, 7, sides=14)
        for a in range(0, 360, 90):
            blade = map_tris(
                box_triangles(0, 14, 10, 3, 22, 1.6) + box_triangles(0, 24, 10, 10, 6, 1.2),
                lambda px, py, pz, aa=a: rotate_z_point(px, py, pz, aa),
            )
            tris += blade
        return tris
    if name == "dancer":
        tris = cyl_triangles(0, 0, 2, 4, 10, sides=20)
        tris += cyl_triangles(0, 0, 10, 14, 16, sides=24, r2=4)
        tris += cyl_triangles(0, 0, 22, 12, 4, sides=14)
        tris += cyl_triangles(0, 0, 30, 8, 5, sides=14)
        left = map_tris(
            box_triangles(0, 0, 0, 16, 3, 3),
            lambda px, py, pz: rotate_y_point(px, py, pz, 55),
        )
        left = map_tris(left, lambda px, py, pz: (px - 10, py, pz + 24))
        right = map_tris(
            box_triangles(0, 0, 0, 16, 3, 3),
            lambda px, py, pz: rotate_y_point(px, py, pz, -55),
        )
        right = map_tris(right, lambda px, py, pz: (px + 10, py, pz + 24))
        tris += left + right
        return tris
    raise KeyError(name)


def write_obj(path: Path, triangles):
    vertices = []
    for tri in triangles:
        for vertex in tri:
            vertices.append(mm_to_m_yup(*vertex))
    with path.open("w", encoding="utf-8") as out:
        out.write("# Generated from design.py / musicbox.scad; metres, Y-up\ns off\n")
        for v in vertices:
            out.write("v %.7g %.7g %.7g\n" % v)
        for i in range(0, len(vertices), 3):
            a, b, c = vertices[i : i + 3]
            u = [b[j] - a[j] for j in range(3)]
            v = [c[j] - a[j] for j in range(3)]
            normal = (
                u[1] * v[2] - u[2] * v[1],
                u[2] * v[0] - u[0] * v[2],
                u[0] * v[1] - u[1] * v[0],
            )
            length = math.sqrt(sum(n * n for n in normal))
            if length <= 1e-12:
                raise ValueError(f"{path}: degenerate triangle {i // 3}")
            out.write("vn %.9g %.9g %.9g\n" % tuple(n / length for n in normal))
        for i in range(0, len(vertices), 3):
            n = i // 3 + 1
            out.write(f"f {i + 1}//{n} {i + 2}//{n} {i + 3}//{n}\n")


def write_materials_nut(path: Path):
    lines = ["// Auto-generated part materials for the SCAD music box.", "boxMaterials <- {"]
    for part in PARTS:
        t = part["tint"]
        lines.append(
            f'    {part["name"]}={{r={t[0]},g={t[1]},b={t[2]},metal={part["metal"]},rough={part["rough"]}}},'
        )
    lines.append("};")
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def write_ratios_nut(path: Path):
    path.write_text(
        "\n".join(
            [
                "// Auto-generated gear ratios for the SCAD music box.",
                f"boxBarrelToCylinder <- {BARREL_TO_CYLINDER};",
                f"boxBarrelToGovernor <- {BARREL_TO_GOVERNOR};",
                f"boxCylinderToDancer <- {CYLINDER_TO_DANCER};",
                "",
            ]
        ),
        encoding="utf-8",
    )


def write_tune_nut(path: Path):
    lines = [
        "// Auto-generated pin schedule + comb frequencies for the SCAD music box.",
        "// angleDeg is the cylinder rotation at which the pin plucks the tooth.",
        "boxToothHz <- [" + ", ".join(f"{hz:.2f}" for hz in TOOTH_HZ) + "];",
        "boxToothNames <- [" + ", ".join(f'"{n}"' for n in TOOTH_NAMES) + "];",
        "boxTunePins <- [",
    ]
    for angle, tooth in TUNE_PINS:
        lines.append(f"    {{angle={angle:.1f}, tooth={tooth}}},")
    lines.append("];")
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def main():
    ASSETS.mkdir(parents=True, exist_ok=True)
    (ASSETS / "musicbox.scad").write_text(emit_scad(), encoding="utf-8")
    (ASSETS / "musicbox.rig.json").write_text(
        json.dumps(emit_rig(), indent=2, ensure_ascii=True) + "\n", encoding="utf-8"
    )
    write_materials_nut(ASSETS / "materials.nut")
    write_ratios_nut(ROOT / "ratios.nut")
    write_tune_nut(ASSETS / "tune.nut")
    for name in PART_NAMES:
        tris = part_triangles(name)
        write_obj(ASSETS / f"{name}.obj", tris)
        print(f"{name}: {len(tris)} triangles", flush=True)


if __name__ == "__main__":
    main()
