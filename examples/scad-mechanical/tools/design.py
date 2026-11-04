#!/usr/bin/env python3
"""Authoritative SCAD mechanical boom: joints, part geometry, OBJ/SCAD/rig emitters.

Dimensions are millimetres, OpenSCAD Z-up (front -Y). EveScript consumes metres/Y-up
OBJ batches and mechanism.rig.json. No third-party Python modules.
"""
from __future__ import annotations

import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets"

# Joint pivots are in OpenSCAD mm / Z-up. Geometry for each part is authored in
# that joint's local frame (origin at the joint pivot).
# Pivots are parent-relative OpenSCAD millimetres (Z-up). Each part mesh is authored
# in that joint's local frame with its origin at the joint.
JOINTS = [
    {"name": "pedestal", "parent": None, "pivot": [0, 0, 0], "axis": [0, 0, 1], "limits": [0, 0]},
    {"name": "slew", "parent": "pedestal", "pivot": [0, 0, 180], "axis": [0, 0, 1], "limits": [-160, 160]},
    {"name": "shoulder", "parent": "slew", "pivot": [0, 0, 240], "axis": [1, 0, 0], "limits": [-15, 75]},
    {"name": "elbow", "parent": "shoulder", "pivot": [0, 0, 560], "axis": [1, 0, 0], "limits": [-120, 10]},
    {"name": "wrist", "parent": "elbow", "pivot": [0, 0, 580], "axis": [0, 0, 1], "limits": [-170, 170]},
    {"name": "tool", "parent": "wrist", "pivot": [0, 0, 160], "axis": [1, 0, 0], "limits": [-60, 40]},
]

PARTS = [
    {"name": "pedestal", "joint": "pedestal", "tint": [0.18, 0.20, 0.22], "metal": 0.55, "rough": 0.42},
    {"name": "slew", "joint": "slew", "tint": [0.72, 0.55, 0.12], "metal": 0.35, "rough": 0.48},
    {"name": "shoulder", "joint": "shoulder", "tint": [0.22, 0.45, 0.62], "metal": 0.40, "rough": 0.40},
    {"name": "elbow", "joint": "elbow", "tint": [0.18, 0.38, 0.55], "metal": 0.42, "rough": 0.38},
    {"name": "wrist", "joint": "wrist", "tint": [0.55, 0.58, 0.60], "metal": 0.70, "rough": 0.32},
    {"name": "tool", "joint": "tool", "tint": [0.78, 0.22, 0.14], "metal": 0.25, "rough": 0.50},
]

PART_NAMES = tuple(part["name"] for part in PARTS)


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
    lines = [
        "// Articulated inspection boom — rotatable SCAD mechanical diagram.",
        "// Compile one joint mesh with: openscad -D 'part=\"shoulder\"' -o shoulder.stl mechanism.scad",
        'part = "pedestal";',
        "",
        "module box(sx, sy, sz) { cube([sx, sy, sz], center = true); }",
        "module cyl(h, r, r2 = undef) {",
        "    cylinder(h = h, r1 = r, r2 = (r2 == undef ? r : r2), center = true, $fn = 28);",
        "}",
        "",
        "module pedestal_geom() {",
        "    translate([0, 0, 40]) box(520, 520, 80);",
        "    translate([0, 0, 110]) cyl(60, 170);",
        "    for (a = [0:90:270]) rotate([0, 0, a]) translate([180, 0, 20]) cyl(40, 28);",
        "}",
        "",
        "module slew_geom() {",
        "    // Local origin = slew pivot (top of pedestal column).",
        "    translate([0, 0, 40]) cyl(80, 150);",
        "    translate([0, 0, 110]) box(280, 280, 60);",
        "    translate([0, -40, 180]) box(160, 120, 160);",
        "    translate([0, 0, 220]) cyl(40, 70);",
        "}",
        "",
        "module shoulder_geom() {",
        "    translate([0, 0, 0]) rotate([0, 90, 0]) cyl(140, 55);",
        "    translate([0, 0, 280]) box(90, 110, 560);",
        "    translate([0, 0, 560]) rotate([0, 90, 0]) cyl(120, 48);",
        "    translate([55, 0, 180]) box(18, 70, 220);",
        "    translate([-55, 0, 180]) box(18, 70, 220);",
        "}",
        "",
        "module elbow_geom() {",
        "    translate([0, 0, 0]) rotate([0, 90, 0]) cyl(110, 44);",
        "    translate([0, 0, 290]) box(70, 90, 580);",
        "    translate([0, 0, 580]) rotate([0, 90, 0]) cyl(100, 38);",
        "    translate([40, 0, 200]) box(14, 54, 260);",
        "    translate([-40, 0, 200]) box(14, 54, 260);",
        "}",
        "",
        "module wrist_geom() {",
        "    translate([0, 0, 0]) cyl(50, 42);",
        "    translate([0, 0, 70]) box(90, 90, 90);",
        "    translate([0, 0, 130]) cyl(40, 34);",
        "}",
        "",
        "module tool_geom() {",
        "    translate([0, 0, 20]) box(70, 40, 40);",
        "    translate([0, 28, 55]) box(18, 70, 18);",
        "    translate([0, -28, 55]) box(18, 70, 18);",
        "    translate([0, 55, 90]) box(14, 18, 50);",
        "    translate([0, -55, 90]) box(14, 18, 50);",
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


def cyl_triangles(cx, cy, cz, h, r, axis="z", sides=24):
    tris = []
    hh = h * 0.5

    def point(i, along):
        a = 2 * math.pi * i / sides
        c, s = math.cos(a) * r, math.sin(a) * r
        if axis == "x":
            return (cx + along, cy + c, cz + s)
        if axis == "y":
            return (cx + c, cy + along, cz + s)
        return (cx + c, cy + s, cz + along)

    for i in range(sides):
        n = (i + 1) % sides
        tris.append((point(i, -hh), point(n, -hh), point(n, hh)))
        tris.append((point(i, -hh), point(n, hh), point(i, hh)))
        center0 = (cx, cy, cz - hh) if axis == "z" else ((cx - hh, cy, cz) if axis == "x" else (cx, cy - hh, cz))
        center1 = (cx, cy, cz + hh) if axis == "z" else ((cx + hh, cy, cz) if axis == "x" else (cx, cy + hh, cz))
        tris.append((center0, point(n, -hh), point(i, -hh)))
        tris.append((center1, point(i, hh), point(n, hh)))
    return tris


def part_triangles(name: str):
    if name == "pedestal":
        tris = []
        tris += box_triangles(0, 0, 40, 520, 520, 80)
        tris += cyl_triangles(0, 0, 110, 60, 170)
        for a in (0, 90, 180, 270):
            rad = math.radians(a)
            tris += cyl_triangles(180 * math.cos(rad), 180 * math.sin(rad), 20, 40, 28)
        return tris
    if name == "slew":
        tris = cyl_triangles(0, 0, 40, 80, 150)
        tris += box_triangles(0, 0, 110, 280, 280, 60)
        tris += box_triangles(0, -40, 180, 160, 120, 160)
        tris += cyl_triangles(0, 0, 220, 40, 70)
        return tris
    if name == "shoulder":
        tris = cyl_triangles(0, 0, 0, 140, 55, axis="x")
        tris += box_triangles(0, 0, 280, 90, 110, 560)
        tris += cyl_triangles(0, 0, 560, 120, 48, axis="x")
        tris += box_triangles(55, 0, 180, 18, 70, 220)
        tris += box_triangles(-55, 0, 180, 18, 70, 220)
        return tris
    if name == "elbow":
        tris = cyl_triangles(0, 0, 0, 110, 44, axis="x")
        tris += box_triangles(0, 0, 290, 70, 90, 580)
        tris += cyl_triangles(0, 0, 580, 100, 38, axis="x")
        tris += box_triangles(40, 0, 200, 14, 54, 260)
        tris += box_triangles(-40, 0, 200, 14, 54, 260)
        return tris
    if name == "wrist":
        tris = cyl_triangles(0, 0, 0, 50, 42)
        tris += box_triangles(0, 0, 70, 90, 90, 90)
        tris += cyl_triangles(0, 0, 130, 40, 34)
        return tris
    if name == "tool":
        tris = box_triangles(0, 0, 20, 70, 40, 40)
        tris += box_triangles(0, 28, 55, 18, 70, 18)
        tris += box_triangles(0, -28, 55, 18, 70, 18)
        tris += box_triangles(0, 55, 90, 14, 18, 50)
        tris += box_triangles(0, -55, 90, 14, 18, 50)
        return tris
    raise KeyError(name)


def write_obj(path: Path, triangles):
    vertices = []
    for tri in triangles:
        for vertex in tri:
            vertices.append(mm_to_m_yup(*vertex))
    with path.open("w", encoding="utf-8") as out:
        out.write(f"# Generated from design.py / mechanism.scad; metres, Y-up\ns off\n")
        for v in vertices:
            out.write("v %.7g %.7g %.7g\n" % v)
        for i in range(0, len(vertices), 3):
            a, b, c = vertices[i : i + 3]
            u = [b[j] - a[j] for j in range(3)]
            v = [c[j] - a[j] for j in range(3)]
            normal = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            length = math.sqrt(sum(n * n for n in normal))
            if length <= 1e-12:
                raise ValueError(f"{path}: degenerate triangle {i // 3}")
            out.write("vn %.9g %.9g %.9g\n" % tuple(n / length for n in normal))
        for i in range(0, len(vertices), 3):
            n = i // 3 + 1
            out.write(f"f {i + 1}//{n} {i + 2}//{n} {i + 3}//{n}\n")


def write_materials_nut(path: Path):
    lines = ["// Auto-generated part materials for the SCAD mechanical boom.", "mechMaterials <- {"]
    for part in PARTS:
        t = part["tint"]
        lines.append(
            f'    {part["name"]}={{r={t[0]},g={t[1]},b={t[2]},metal={part["metal"]},rough={part["rough"]}}},'
        )
    lines.append("};")
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def main():
    ASSETS.mkdir(parents=True, exist_ok=True)
    (ASSETS / "mechanism.scad").write_text(emit_scad(), encoding="utf-8")
    (ASSETS / "mechanism.rig.json").write_text(
        json.dumps(emit_rig(), indent=2, ensure_ascii=True) + "\n", encoding="utf-8"
    )
    write_materials_nut(ASSETS / "materials.nut")
    for name in PART_NAMES:
        write_obj(ASSETS / f"{name}.obj", part_triangles(name))
        print(f"{name}: wrote OBJ", flush=True)


if __name__ == "__main__":
    main()
