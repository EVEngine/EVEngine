#!/usr/bin/env python3
"""Authoritative SCAD watermill: joints, part geometry, OBJ/SCAD/rig emitters.

Dimensions are millimetres, OpenSCAD Z-up (front +Y for the visitor gallery).
EveScript consumes metres/Y-up OBJ batches and mill.rig.json.
Traditional overshot layout: waterwheel + watershaft + pitwheel (drive),
vertical mainshaft with wallower/spur/crown (main), runner stone (runner),
hinged sluice gate, and a walkable timber mill shell.
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
    {"name": "building", "parent": None, "pivot": [0, 0, 0], "axis": [0, 0, 1], "limits": [0, 0]},
    {"name": "race", "parent": "building", "pivot": [0, 0, 0], "axis": [0, 0, 1], "limits": [0, 0]},
    {
        "name": "sluice",
        "parent": "building",
        "pivot": [0, -2650, 3100],
        "axis": [1, 0, 0],
        "limits": [0, 75],
    },
    {
        "name": "drive",
        "parent": "building",
        "pivot": [0, -2500, 1800],
        "axis": [1, 0, 0],
        "limits": [-100000, 100000],
    },
    {
        "name": "main",
        "parent": "building",
        "pivot": [0, -900, 900],
        "axis": [0, 0, 1],
        "limits": [-100000, 100000],
    },
    {
        "name": "runner",
        "parent": "building",
        "pivot": [1450, -200, 2860],
        "axis": [0, 0, 1],
        "limits": [-100000, 100000],
    },
    {
        "name": "hoist",
        "parent": "main",
        "pivot": [0, 0, 4300],
        "axis": [0, 0, 1],
        "limits": [-100000, 100000],
    },
]

PARTS = [
    {"name": "building", "joint": "building", "tint": [0.55, 0.42, 0.28], "metal": 0.04, "rough": 0.82},
    {"name": "race", "joint": "race", "tint": [0.45, 0.43, 0.38], "metal": 0.02, "rough": 0.92},
    {"name": "sluice", "joint": "sluice", "tint": [0.32, 0.18, 0.10], "metal": 0.10, "rough": 0.68},
    {"name": "drive", "joint": "drive", "tint": [0.16, 0.17, 0.19], "metal": 0.55, "rough": 0.42},
    {"name": "main", "joint": "main", "tint": [0.72, 0.52, 0.22], "metal": 0.28, "rough": 0.48},
    {"name": "runner", "joint": "runner", "tint": [0.58, 0.56, 0.54], "metal": 0.04, "rough": 0.88},
    {"name": "hoist", "joint": "hoist", "tint": [0.20, 0.16, 0.12], "metal": 0.35, "rough": 0.50},
]

PART_NAMES = tuple(part["name"] for part in PARTS)

# Drive -> main speed-up (pitwheel / wallower). Main -> runner (spur / stone-nut).
DRIVE_TO_MAIN = 80.0 / 24.0
MAIN_TO_RUNNER = 96.0 / 24.0
MAIN_TO_HOIST = 2.2


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
        "// Articulated overshot watermill — rotatable SCAD mechanical diagram.",
        "// Compile one joint mesh with: openscad -D 'part=\"drive\"' -o drive.stl mill.scad",
        'part = "building";',
        "",
        "module box(sx, sy, sz) { cube([sx, sy, sz], center = true); }",
        "module cyl(h, r, r2 = undef, fn = 28) {",
        "    cylinder(h = h, r1 = r, r2 = (r2 == undef ? r : r2), center = true, $fn = fn);",
        "}",
        "",
        "module building_geom() {",
        "    // Ground floor slab with shaft / stair wells.",
        "    difference() {",
        "        translate([0, 200, 80]) box(6200, 5200, 160);",
        "        translate([0, -900, 80]) cyl(220, 220, fn = 24);",
        "        translate([1450, -200, 80]) cyl(220, 180, fn = 24);",
        "        translate([2200, 1400, 80]) box(900, 1400, 220);",
        "    }",
        "    // Stone floor (visitor / grinding level).",
        "    difference() {",
        "        translate([0, 200, 2760]) box(6200, 5200, 140);",
        "        translate([0, -900, 2760]) cyl(220, 160, fn = 24);",
        "        translate([1450, -200, 2760]) cyl(220, 420, fn = 28);",
        "        translate([2200, 1400, 2760]) box(900, 1400, 220);",
        "    }",
        "    // Bin loft floor.",
        "    difference() {",
        "        translate([0, 200, 5160]) box(6200, 5200, 120);",
        "        translate([0, -900, 5160]) cyl(200, 120, fn = 20);",
        "        translate([2200, 1400, 5160]) box(900, 1400, 200);",
        "        translate([1450, -200, 5160]) box(700, 700, 200);",
        "    }",
        "    // Timber posts and beams (cutaway +Y gallery keeps machinery visible).",
        "    for (x = [-2800, 2800]) for (y = [-2100, 2400])",
        "        translate([x, y, 2700]) box(220, 220, 5400);",
        "    for (z = [1400, 4000]) {",
        "        translate([0, -2100, z]) box(5600, 180, 220);",
        "        translate([0, 2400, z]) box(5600, 180, 220);",
        "        translate([-2800, 200, z]) box(180, 4300, 220);",
        "        translate([2800, 200, z]) box(180, 4300, 220);",
        "    }",
        "    // Solid walls with door / windows; open +Y gallery wall for tour view.",
        "    translate([-3010, 150, 2700]) box(140, 4700, 5200);",
        "    translate([3010, 150, 2700]) difference() {",
        "        box(140, 4700, 5200);",
        "        translate([0, -400, 900]) box(200, 900, 1400);",
        "        translate([0, 900, 3600]) box(200, 900, 1100);",
        "    }",
        "    translate([0, -2310, 2700]) difference() {",
        "        box(6200, 140, 5200);",
        "        translate([0, 0, 1800]) cyl(300, 180, fn = 20); // watershaft bore",
        "        translate([-1600, 0, 3600]) box(900, 200, 1100);",
        "        translate([1600, 0, 3600]) box(900, 200, 1100);",
        "    }",
        "    // Gallery rail on the open +Y side (still walkable through the gap).",
        "    translate([0, 2550, 1100]) box(6200, 80, 80);",
        "    translate([0, 2550, 2900]) box(6200, 80, 80);",
        "    for (x = [-2400, -1200, 0, 1200, 2400]) translate([x, 2550, 2000]) box(80, 80, 1800);",
        "    // Door leaf frame on +X wall.",
        "    translate([2940, -400, 900]) box(40, 820, 1600);",
        "    // Stairs (visitor path ground -> stone floor -> loft).",
        "    for (i = [0:11]) translate([2200, 900 + i * 100, 120 + i * 220]) box(800, 220, 80);",
        "    for (i = [0:10]) translate([2200, 900 + i * 110, 2860 + i * 210]) box(800, 220, 80);",
        "    translate([2500, 1400, 2700]) box(80, 1400, 5200);",
        "    // Roof plates + ridge (partial, keeps loft airy).",
        "    translate([0, 0, 6200]) rotate([0, 0, 0]) box(6600, 200, 180);",
        "    translate([-1600, 200, 6000]) rotate([0, 18, 0]) box(3600, 5600, 80);",
        "    translate([1600, 200, 6000]) rotate([0, -18, 0]) box(3600, 5600, 80);",
        "    // Fixed bedstone + tun shell + meal bin.",
        "    translate([1450, -200, 2780]) cyl(120, 620, fn = 36);",
        "    translate([1450, -200, 2920]) difference() {",
        "        cyl(280, 660, fn = 28);",
        "        cyl(320, 620, fn = 28);",
        "        translate([0, 650, 0]) box(420, 200, 320);",
        "    }",
        "    translate([1450, 700, 2500]) box(700, 500, 900);",
        "    // Hopper frame above the stones (grain chute).",
        "    translate([1450, -200, 3600]) box(700, 700, 60);",
        "    translate([1450, -200, 3900]) box(520, 520, 520);",
        "    translate([1450, -200, 3300]) cyl(420, 70, r2 = 40, fn = 16);",
        "    // Interior workbench and sack pile.",
        "    translate([-1800, 1200, 550]) box(1200, 500, 700);",
        "    translate([-1800, 1200, 950]) box(1000, 360, 80);",
        "    translate([-900, 1600, 350]) box(500, 400, 500);",
        "    translate([-900, 1600, 650]) box(420, 320, 280);",
        "    // Hurst frame around the pit gears.",
        "    translate([-700, -900, 1400]) box(120, 120, 2200);",
        "    translate([700, -900, 1400]) box(120, 120, 2200);",
        "    translate([0, -900, 2500]) box(1500, 120, 120);",
        "    translate([0, -900, 1100]) box(1500, 160, 160);",
        "}",
        "",
        "module race_geom() {",
        "    // Mill pond bank, leat walls, wheel pit and stream bed.",
        "    translate([0, -4200, -80]) box(9000, 3600, 200);",
        "    translate([0, 200, -120]) box(9000, 7000, 160);",
        "    translate([0, -3600, 400]) box(3200, 1400, 800); // pond volume shell",
        "    difference() {",
        "        translate([0, -3600, 900]) box(2800, 1200, 200);",
        "        translate([0, -3400, 900]) box(2200, 900, 240);",
        "    }",
        "    // Stone wheel pit under the overshot wheel.",
        "    translate([0, -2500, 200]) box(2400, 2200, 600);",
        "    translate([-1300, -2500, 900]) box(200, 2200, 1800);",
        "    translate([1300, -2500, 900]) box(200, 2200, 1800);",
        "    translate([0, -3600, 1200]) box(2400, 200, 2400);",
        "    // Tail race.",
        "    translate([0, -4800, 100]) box(1600, 1800, 300);",
        "    // Path stones to the gallery.",
        "    for (x = [-1800, -600, 600, 1800]) translate([x, 3200, 40]) box(700, 900, 80);",
        "    // Water surface placeholders (rendered opaque teal).",
        "    translate([0, -3600, 700]) box(2400, 1000, 40);",
        "    translate([0, -4800, 40]) box(1400, 1600, 40);",
        "}",
        "",
        "module sluice_geom() {",
        "    // Guillotine / flap gate at the launder lip (hinge at local origin).",
        "    translate([0, 0, -40]) box(900, 40, 80);",
        "    translate([0, 180, -20]) box(860, 320, 40);",
        "    translate([0, 340, 40]) box(820, 40, 160);",
        "    for (x = [-300, 0, 300]) translate([x, 180, 40]) box(40, 300, 40);",
        "}",
        "",
        "module drive_geom() {",
        "    // Local origin = watershaft / wheel axle.",
        "    // Watershaft through the wall into the pit.",
        "    rotate([0, 90, 0]) cyl(4200, 90, fn = 24);",
        "    translate([-1400, 0, 0]) rotate([0, 90, 0]) cyl(160, 140, fn = 20);",
        "    translate([1400, 0, 0]) rotate([0, 90, 0]) cyl(160, 140, fn = 20);",
        "    // Overshot waterwheel (rim + buckets + spokes).",
        "    translate([0, -700, 0]) {",
        "        rotate([0, 90, 0]) difference() {",
        "            cyl(280, 1600, fn = 48);",
        "            cyl(320, 1420, fn = 48);",
        "        }",
        "        rotate([0, 90, 0]) cyl(220, 220, fn = 24);",
        "        for (a = [0:30:330]) rotate([a, 0, 0]) {",
        "            translate([0, 0, 780]) box(40, 80, 1280);",
        "            translate([0, 40, 1480]) box(220, 160, 70); // bucket",
        "        }",
        "    }",
        "    // Pitwheel (large bevel / face gear).",
        "    translate([0, 1600, 0]) {",
        "        rotate([0, 90, 0]) cyl(160, 900, fn = 40);",
        "        for (a = [0:12:348]) rotate([a, 0, 0])",
        "            translate([90, 0, 820]) box(120, 70, 160);",
        "        for (a = [0:60:300]) rotate([a, 0, 0]) translate([0, 0, 450]) box(40, 70, 700);",
        "    }",
        "}",
        "",
        "module main_geom() {",
        "    // Local origin near wallower; shaft rises through floors.",
        "    cyl(5200, 110, fn = 24);",
        "    // Wallower (bevel pinion meshing pitwheel).",
        "    translate([0, 0, 900]) {",
        "        cyl(140, 320, fn = 28);",
        "        for (a = [0:15:345]) rotate([0, 0, a]) translate([300, 0, 0]) box(90, 55, 120);",
        "    }",
        "    // Great spur wheel.",
        "    translate([0, 0, 2100]) {",
        "        cyl(160, 780, fn = 40);",
        "        for (a = [0:10:350]) rotate([0, 0, a]) translate([740, 0, 0]) box(90, 55, 140);",
        "        for (a = [0:45:315]) rotate([0, 0, a]) translate([380, 0, 0]) box(40, 70, 560);",
        "    }",
        "    // Crown wheel for sack-hoist drive.",
        "    translate([0, 0, 4000]) {",
        "        cyl(120, 420, fn = 32);",
        "        for (a = [0:18:342]) rotate([0, 0, a]) translate([390, 0, 0]) box(70, 45, 100);",
        "    }",
        "}",
        "",
        "module runner_geom() {",
        "    // Spindle + stone nut + runner millstone.",
        "    translate([0, 0, -900]) cyl(2000, 55, fn = 20);",
        "    translate([0, 0, -700]) {",
        "        cyl(120, 220, fn = 24);",
        "        for (a = [0:20:340]) rotate([0, 0, a]) translate([200, 0, 0]) box(70, 45, 100);",
        "    }",
        "    // Runner stone with eye and harps (furrow hint).",
        "    difference() {",
        "        cyl(160, 600, fn = 40);",
        "        cyl(200, 70, fn = 16);",
        "    }",
        "    for (a = [0:45:315]) rotate([0, 0, a]) translate([280, 0, 90]) box(260, 30, 20);",
        "    // Rynd / mace bridge.",
        "    translate([0, 0, -40]) box(420, 60, 40);",
        "    translate([0, 0, -40]) box(60, 420, 40);",
        "}",
        "",
        "module hoist_geom() {",
        "    // Sack hoist drum and clutch pulley at loft level.",
        "    cyl(220, 180, fn = 24);",
        "    translate([0, 0, 160]) cyl(60, 220, fn = 20);",
        "    translate([0, 0, -160]) cyl(60, 220, fn = 20);",
        "    for (a = [0:60:300]) rotate([0, 0, a]) translate([160, 0, 0]) box(40, 50, 280);",
        "    translate([0, 280, 0]) box(40, 360, 40); // jib arm",
        "    translate([0, 460, -80]) cyl(120, 30, fn = 12);",
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
    tris = []
    hh = h * 0.5
    r_top = r if r2 is None else r2

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
        tris.append((point(i, -hh, r), point(n, -hh, r), point(n, hh, r_top)))
        tris.append((point(i, -hh, r), point(n, hh, r_top), point(i, hh, r_top)))
        if axis == "z":
            center0, center1 = (cx, cy, cz - hh), (cx, cy, cz + hh)
        elif axis == "x":
            center0, center1 = (cx - hh, cy, cz), (cx + hh, cy, cz)
        else:
            center0, center1 = (cx, cy - hh, cz), (cx, cy + hh, cz)
        tris.append((center0, point(n, -hh, r), point(i, -hh, r)))
        tris.append((center1, point(i, hh, r_top), point(n, hh, r_top)))
    return tris


def ring_triangles(cx, cy, cz, h, r_outer, r_inner, axis="z", sides=40):
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
        # outer wall
        tris.append((point(i, -hh, r_outer), point(n, -hh, r_outer), point(n, hh, r_outer)))
        tris.append((point(i, -hh, r_outer), point(n, hh, r_outer), point(i, hh, r_outer)))
        # inner wall (reversed winding)
        tris.append((point(i, -hh, r_inner), point(i, hh, r_inner), point(n, hh, r_inner)))
        tris.append((point(i, -hh, r_inner), point(n, hh, r_inner), point(n, -hh, r_inner)))
        # bottom annulus
        tris.append((point(i, -hh, r_outer), point(i, -hh, r_inner), point(n, -hh, r_inner)))
        tris.append((point(i, -hh, r_outer), point(n, -hh, r_inner), point(n, -hh, r_outer)))
        # top annulus
        tris.append((point(i, hh, r_outer), point(n, hh, r_outer), point(n, hh, r_inner)))
        tris.append((point(i, hh, r_outer), point(n, hh, r_inner), point(i, hh, r_inner)))
    return tris


def rotate_x(point, degrees):
    x, y, z = point
    a = math.radians(degrees)
    c, s = math.cos(a), math.sin(a)
    return (x, y * c - z * s, y * s + z * c)


def rotate_y(point, degrees):
    x, y, z = point
    a = math.radians(degrees)
    c, s = math.cos(a), math.sin(a)
    return (x * c + z * s, y, -x * s + z * c)


def rotate_z(point, degrees):
    x, y, z = point
    a = math.radians(degrees)
    c, s = math.cos(a), math.sin(a)
    return (x * c - y * s, x * s + y * c, z)


def map_tris(tris, fn):
    return [tuple(fn(v) for v in tri) for tri in tris]


def part_triangles(name: str):
    if name == "building":
        tris = []
        # Floors with walkable open plan (gallery on +Y is intentionally wall-free).
        tris += box_triangles(0, 200, 80, 6200, 5200, 160)
        tris += box_triangles(0, 200, 2760, 6200, 5200, 140)
        tris += box_triangles(0, 200, 5160, 6200, 5200, 120)
        # Corner posts + mid posts keep the shell readable without boxing the visitor in.
        for x in (-2800, 0, 2800):
            for y in (-2100, 400, 2400):
                tris += box_triangles(x, y, 2700, 180, 180, 5400)
        for z in (1400, 4000, 5400):
            tris += box_triangles(0, -2100, z, 5600, 140, 160)
            tris += box_triangles(0, 2400, z, 5600, 140, 160)
            tris += box_triangles(-2800, 200, z, 140, 4300, 160)
            tris += box_triangles(2800, 200, z, 140, 4300, 160)
        # -X wall panels with two window bays.
        tris += box_triangles(-3010, -1400, 900, 120, 1400, 1600)
        tris += box_triangles(-3010, 1400, 900, 120, 1400, 1600)
        tris += box_triangles(-3010, 0, 2700, 120, 900, 1200)
        tris += box_triangles(-3010, 0, 4500, 120, 4200, 1400)
        # +X wall with door bay (visitor entry) and upper window.
        tris += box_triangles(3010, -1400, 900, 120, 1400, 1600)
        tris += box_triangles(3010, 1400, 900, 120, 1400, 1600)
        tris += box_triangles(3010, -400, 2000, 120, 820, 400)  # lintel
        tris += box_triangles(3010, 0, 4500, 120, 4200, 1400)
        # -Y wheel-side wall with shaft bore left as an open bay around the axle.
        tris += box_triangles(-2000, -2310, 900, 2000, 120, 1600)
        tris += box_triangles(2000, -2310, 900, 2000, 120, 1600)
        tris += box_triangles(0, -2310, 3200, 6200, 120, 2800)
        # Gallery rail on the open +Y visit face.
        tris += box_triangles(0, 2550, 1100, 6200, 80, 80)
        tris += box_triangles(0, 2550, 2900, 6200, 80, 80)
        tris += box_triangles(0, 2550, 4700, 6200, 80, 80)
        for x in (-2400, -1200, 0, 1200, 2400):
            tris += box_triangles(x, 2550, 2900, 70, 70, 3600)
        # Stairs (visitor path ground -> stone floor -> loft).
        for i in range(12):
            tris += box_triangles(2200, 900 + i * 100, 120 + i * 220, 800, 220, 80)
        for i in range(11):
            tris += box_triangles(2200, 900 + i * 110, 2860 + i * 210, 800, 220, 80)
        tris += box_triangles(2500, 1400, 2700, 80, 1400, 5200)
        # Roof: ridge + two plates seated on the top plate beam (not floating).
        tris += box_triangles(0, 200, 5580, 6400, 5000, 120)
        tris += box_triangles(0, 200, 6100, 400, 5200, 220)
        tris += box_triangles(-1700, 200, 5850, 3400, 5000, 100)
        tris += box_triangles(1700, 200, 5850, 3400, 5000, 100)
        # Fixed bedstone + tun shell + meal bin + hopper.
        tris += cyl_triangles(1450, -200, 2780, 120, 620, sides=32)
        tris += ring_triangles(1450, -200, 2920, 280, 660, 620, sides=28)
        tris += box_triangles(1450, 700, 2500, 700, 500, 900)
        tris += box_triangles(1450, -200, 3600, 700, 700, 60)
        tris += box_triangles(1450, -200, 3900, 520, 520, 520)
        tris += cyl_triangles(1450, -200, 3300, 420, 70, sides=16, r2=40)
        tris += box_triangles(-1800, 1200, 550, 1200, 500, 700)
        tris += box_triangles(-1800, 1200, 950, 1000, 360, 80)
        tris += box_triangles(-900, 1600, 350, 500, 400, 500)
        tris += box_triangles(-900, 1600, 650, 420, 320, 280)
        tris += box_triangles(-700, -900, 1400, 120, 120, 2200)
        tris += box_triangles(700, -900, 1400, 120, 120, 2200)
        tris += box_triangles(0, -900, 2500, 1500, 120, 120)
        tris += box_triangles(0, -900, 1100, 1500, 160, 160)
        return tris

    if name == "race":
        tris = []
        # Stone banks / ground (muted), wheel pit, pond rim; water plates stay separate teal.
        tris += box_triangles(0, -4200, -120, 9000, 3600, 160)
        tris += box_triangles(0, 200, -160, 9000, 7000, 140)
        tris += box_triangles(0, -3600, 300, 3200, 1400, 600)
        tris += box_triangles(0, -3600, 900, 2800, 1200, 180)
        tris += box_triangles(0, -2500, 160, 2400, 2200, 500)
        tris += box_triangles(-1300, -2500, 900, 200, 2200, 1800)
        tris += box_triangles(1300, -2500, 900, 200, 2200, 1800)
        tris += box_triangles(0, -3600, 1200, 2400, 200, 2400)
        tris += box_triangles(0, -4800, 60, 1600, 1800, 260)
        for x in (-1800, -600, 600, 1800):
            tris += box_triangles(x, 3200, 40, 700, 900, 80)
        return tris

    if name == "sluice":
        tris = box_triangles(0, 0, -40, 900, 40, 80)
        tris += box_triangles(0, 180, -20, 860, 320, 40)
        tris += box_triangles(0, 340, 40, 820, 40, 160)
        for x in (-300, 0, 300):
            tris += box_triangles(x, 180, 40, 40, 300, 40)
        return tris

    if name == "drive":
        tris = cyl_triangles(0, 0, 0, 4200, 90, axis="x", sides=20)
        tris += cyl_triangles(-1400, 0, 0, 160, 140, axis="x", sides=16)
        tris += cyl_triangles(1400, 0, 0, 160, 140, axis="x", sides=16)
        # Waterwheel at y=-700
        wy = -700
        tris += ring_triangles(0, wy, 0, 280, 1600, 1420, axis="x", sides=40)
        tris += cyl_triangles(0, wy, 0, 220, 220, axis="x", sides=20)
        for a in range(0, 360, 30):

            def rot_wheel(p, ang=a, y0=wy):
                x, y, z = p
                y2, z2 = rotate_x((0, y - y0, z), ang)[1:]
                return (x, y0 + y2, z2)

            tris += map_tris(box_triangles(0, wy, 780, 40, 80, 1280), rot_wheel)
            tris += map_tris(box_triangles(0, wy + 40, 1480, 220, 160, 70), rot_wheel)
        # Pitwheel at y=+1600
        py = 1600
        tris += cyl_triangles(0, py, 0, 160, 900, axis="x", sides=36)

        def rot_pit(p, ang, y0=py):
            x, y, z = p
            y2, z2 = rotate_x((0, y - y0, z), ang)[1:]
            return (x, y0 + y2, z2)

        for a in range(0, 360, 12):
            tris += map_tris(box_triangles(90, py, 820, 120, 70, 160), lambda p, ang=a: rot_pit(p, ang))
        for a in range(0, 360, 60):
            tris += map_tris(box_triangles(0, py, 450, 40, 70, 700), lambda p, ang=a: rot_pit(p, ang))
        return tris

    if name == "main":
        tris = cyl_triangles(0, 0, 0, 5200, 110, sides=20)
        tris += cyl_triangles(0, 0, 900, 140, 320, sides=24)
        for a in range(0, 360, 15):
            tris += map_tris(box_triangles(300, 0, 900, 90, 55, 120), lambda p, ang=a: rotate_z(p, ang))
        tris += cyl_triangles(0, 0, 2100, 160, 780, sides=36)
        for a in range(0, 360, 10):
            tris += map_tris(box_triangles(740, 0, 2100, 90, 55, 140), lambda p, ang=a: rotate_z(p, ang))
        for a in range(0, 360, 45):
            tris += map_tris(box_triangles(380, 0, 2100, 40, 70, 560), lambda p, ang=a: rotate_z(p, ang))
        tris += cyl_triangles(0, 0, 4000, 120, 420, sides=28)
        for a in range(0, 360, 18):
            tris += map_tris(box_triangles(390, 0, 4000, 70, 45, 100), lambda p, ang=a: rotate_z(p, ang))
        return tris

    if name == "runner":
        tris = cyl_triangles(0, 0, -900, 2000, 55, sides=16)
        tris += cyl_triangles(0, 0, -700, 120, 220, sides=20)
        for a in range(0, 360, 20):
            tris += map_tris(box_triangles(200, 0, -700, 70, 45, 100), lambda p, ang=a: rotate_z(p, ang))
        tris += ring_triangles(0, 0, 0, 160, 600, 70, sides=36)
        for a in range(0, 360, 45):
            tris += map_tris(box_triangles(280, 0, 90, 260, 30, 20), lambda p, ang=a: rotate_z(p, ang))
        tris += box_triangles(0, 0, -40, 420, 60, 40)
        tris += box_triangles(0, 0, -40, 60, 420, 40)
        return tris

    if name == "hoist":
        tris = cyl_triangles(0, 0, 0, 220, 180, sides=20)
        tris += cyl_triangles(0, 0, 160, 60, 220, sides=16)
        tris += cyl_triangles(0, 0, -160, 60, 220, sides=16)
        for a in range(0, 360, 60):
            tris += map_tris(box_triangles(160, 0, 0, 40, 50, 280), lambda p, ang=a: rotate_z(p, ang))
        tris += box_triangles(0, 280, 0, 40, 360, 40)
        tris += cyl_triangles(0, 460, -80, 120, 30, sides=10)
        return tris

    raise KeyError(name)


def write_obj(path: Path, triangles):
    vertices = []
    for tri in triangles:
        for vertex in tri:
            vertices.append(mm_to_m_yup(*vertex))
    with path.open("w", encoding="utf-8") as out:
        out.write("# Generated from design.py / mill.scad; metres, Y-up\ns off\n")
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
    lines = ["// Auto-generated part materials for the SCAD watermill.", "millMaterials <- {"]
    for part in PARTS:
        t = part["tint"]
        lines.append(
            f'    {part["name"]}={{r={t[0]},g={t[1]},b={t[2]},metal={part["metal"]},rough={part["rough"]}}},'
        )
    lines.append("};")
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def write_pose_constants(path: Path):
    text = (
        "// Auto-generated gear ratios from design.py\n"
        f"millDriveToMain <- {DRIVE_TO_MAIN};\n"
        f"millMainToRunner <- {MAIN_TO_RUNNER};\n"
        f"millMainToHoist <- {MAIN_TO_HOIST};\n"
    )
    path.write_text(text, encoding="utf-8")


def main():
    ASSETS.mkdir(parents=True, exist_ok=True)
    (ASSETS / "mill.scad").write_text(emit_scad(), encoding="utf-8")
    (ASSETS / "mill.rig.json").write_text(
        json.dumps(emit_rig(), indent=2, ensure_ascii=True) + "\n", encoding="utf-8"
    )
    write_materials_nut(ASSETS / "materials.nut")
    write_pose_constants(ROOT / "ratios.nut")
    for name in PART_NAMES:
        tris = part_triangles(name)
        write_obj(ASSETS / f"{name}.obj", tris)
        print(f"{name}: wrote OBJ ({len(tris)} tris)", flush=True)


if __name__ == "__main__":
    main()
