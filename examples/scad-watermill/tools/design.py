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
    {
        "name": "building",
        "joint": "building",
        "tint": [0.30, 0.22, 0.14],
        "metal": 0.02,
        "rough": 0.92,
    },
    {"name": "race", "joint": "race", "tint": [0.28, 0.28, 0.26], "metal": 0.02, "rough": 0.95},
    {"name": "sluice", "joint": "sluice", "tint": [0.24, 0.14, 0.08], "metal": 0.05, "rough": 0.78},
    {"name": "drive", "joint": "drive", "tint": [0.18, 0.18, 0.19], "metal": 0.12, "rough": 0.72},
    {"name": "main", "joint": "main", "tint": [0.40, 0.30, 0.16], "metal": 0.10, "rough": 0.68},
    {"name": "runner", "joint": "runner", "tint": [0.40, 0.38, 0.36], "metal": 0.03, "rough": 0.93},
    {"name": "hoist", "joint": "hoist", "tint": [0.18, 0.14, 0.10], "metal": 0.12, "rough": 0.70},
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


def mechanism_triangles(name: str):
    if name == "hoist":
        tris = cyl_triangles(0, 0, 0, 220, 180, sides=20)
        tris += cyl_triangles(0, 0, 160, 60, 220, sides=16)
        tris += cyl_triangles(0, 0, -160, 60, 220, sides=16)
        for a in range(0, 360, 60):
            tris += map_tris(
                box_triangles(160, 0, 0, 40, 50, 280), lambda p, ang=a: rotate_z(p, ang)
            )
        tris += box_triangles(0, 280, 0, 40, 360, 40)
        tris += cyl_triangles(0, 460, -80, 120, 30, sides=10)
        return tris

    raise KeyError(name)


# Named materials are shared by OBJ and SCAD; all geometry remains joint-local.
PALETTE = {
    "timber": (0.25, 0.13, 0.065),
    "wood": (0.49, 0.29, 0.13),
    "plaster": (0.76, 0.69, 0.53),
    "stone": (0.43, 0.46, 0.43),
    "roof": (0.22, 0.29, 0.31),
    "iron": (0.12, 0.15, 0.16),
    "earth": (0.23, 0.30, 0.20),
    "glass": (0.18, 0.33, 0.36),
}


def part_groups(name, separate_solids=False):
    groups = {key: [] for key in PALETTE}
    solids = []

    def add(material, triangles):
        groups[material].extend(triangles)
        solids.append((material, triangles))

    def box(material, x, y, z, sx, sy, sz):
        add(material, box_triangles(x, y, z, sx, sy, sz))

    def beam(material, start, end, width=150):
        # A rectangular beam between endpoints in the X/Z plane.
        x, y, z = start
        dx, dz = end[0] - x, end[2] - z
        length = math.hypot(dx, dz)
        angle = -math.degrees(math.atan2(dz, dx))
        add(
            material,
            map_tris(
                box_triangles(0, 0, 0, length, width, width),
                lambda p: tuple(
                    a + b
                    for a, b in zip(rotate_y(p, angle), ((x + end[0]) / 2, y, (z + end[2]) / 2))
                ),
            ),
        )

    if name == "building":
        # Raised stone plinth, open visitor gallery, two timber working floors.
        for x in range(-2900, 3000, 580):
            for y in (-2200, 2400):
                box("stone", x, y, 220, 550, 420, 400)
        for z in (480, 2760, 5160):
            # Opening around the main shaft and the stairwell, rather than solid slabs.
            for x in range(-2750, 2900, 250):
                if -600 < x < 600:
                    for y in (-1800, 1500):
                        box("wood", x, y, z, 238, 1200, 100)
                elif x > 1700:
                    box("wood", x, -700, z, 238, 3000, 100)
                else:
                    box("wood", x, 100, z, 238, 4500, 100)
        for x in (-2800, 0, 2800):
            for y in (-2200, 2400):
                box("timber", x, y, 3100, 190, 190, 5500)
        for z in (600, 2700, 5100, 5480):
            for y in (-2200, 2400):
                box("timber", 0, y, z, 5800, 190, 180)
            for x in (-2800, 2800):
                box("timber", x, 100, z, 180, 4700, 180)

        # Continuous infill between sill and eaves. Only framed window openings
        # interrupt the three enclosed walls; +Y is the deliberate visitor gallery.
        def wall_bay(center, half_width, side=None):
            def panel(material, u, z, width, height, depth=130, offset=0):
                if side is None:
                    box(material, center + u, -2200 - offset, z, width, depth, height)
                else:
                    box(material, side * (2800 + offset), center + u, z, depth, width, height)

            width = half_width * 2
            for lo, hi in ((690, 1350), (2250, 3450), (4350, 5390)):
                panel("plaster", 0, (lo + hi) / 2, width, hi - lo)
            for z in (1800, 3900):
                jamb_width = (width - 760) / 2
                for sign in (-1, 1):
                    panel("plaster", sign * (380 + jamb_width / 2), z, jamb_width, 900)
                    panel("timber", sign * 405, z, 80, 980, 160, 35)
                for h in (-490, 490):
                    panel("timber", 0, z + h, 890, 80, 160, 35)
                panel("glass", 0, z, 740, 880, 40)
                panel("timber", 0, z, 45, 900, 150, 35)
                panel("timber", 0, z, 760, 45, 150, 35)
                panel("stone", 0, z - 550, 980, 90, 240, 65)
            panel("timber", -half_width, 3000, 130, 4800, 180, 30)

        for x in (-2100, -700, 700, 2100):
            wall_bay(x, 700)
        for side in (-1, 1):
            for bay in range(3):
                wall_bay(-2200 + (bay + 0.5) * 4600 / 3, 2300 / 3, side)
            box("plaster", side * 2800, 100, 5597.5, 130, 4600, 415)
        # Close the gables up to the underside of the roof, including the short
        # vertical section above the upper beam. No daylight slot along the eaves.
        for y in (-2200, 2400):
            outline = [(-2800, 5390), (2800, 5390), (2800, 5805), (0, 7840), (-2800, 5805)]
            points = [(x, y + dy, z) for dy in (-65, 65) for x, z in outline]
            triangles = []
            for i in range(1, 4):
                triangles.append((points[0], points[i], points[i + 1]))
                triangles.append((points[5], points[i + 6], points[i + 5]))
            for i in range(5):
                j = (i + 1) % 5
                triangles.extend(
                    (
                        (points[i], points[i + 5], points[j + 5]),
                        (points[i], points[j + 5], points[j]),
                    )
                )
            add("plaster", triangles)
        for y in (-2300,):
            for x in (-2100, -700, 700, 2100):
                # Braces stay below the windows instead of crossing their panes.
                beam("timber", (x - 550, y, 2750), (x + 550, y, 3370), 110)
        # True 36-degree gable, individual overlapping slate courses.
        pitch = math.radians(36)
        for side in (-1, 1):
            for row in range(10):
                x = side * (170 + row * 335)
                z = 7900 - abs(x) * math.tan(pitch)
                for col in range(11):
                    y = -2450 + col * 500 + (row % 2) * 70
                    add(
                        "roof",
                        map_tris(
                            box_triangles(0, 0, 0, 440, 485, 75),
                            lambda p, x=x, y=y, z=z, side=side: tuple(
                                a + b for a, b in zip(rotate_y(p, side * 36), (x, y, z))
                            ),
                        ),
                    )
            for y in (-2300, 2450):
                beam("timber", (0, y, 7880), (side * 3500, y, 5340), 170)
        box("roof", 0, 100, 7940, 180, 5600, 160)
        for y in (-2200, 2400):
            box("timber", 0, y, 6600, 160, 160, 2300)
            box("timber", 0, y, 5500, 5800, 170, 170)
        # Gallery rails, stairs and fixed milling furniture.
        for z in (1500, 3750):
            box("timber", 0, 2500, z, 5700, 90, 100)
            for x in range(-2700, 2800, 450):
                box("wood", x, 2500, z - 450, 55, 55, 850)
        for base in (500, 2800):
            for i in range(11):
                box("wood", 2250, 700 + i * 150, base + i * 210, 850, 240, 90)
            for x in (1850, 2650):
                add(
                    "timber",
                    map_tris(
                        box_triangles(0, 0, 0, 110, 2680, 140),
                        lambda p, x=x, base=base: tuple(
                            a + b
                            for a, b in zip(
                                rotate_x(p, math.degrees(math.atan2(2100, 1500))),
                                (x, 1450, base + 950),
                            )
                        ),
                    ),
                )
        add("stone", cyl_triangles(1450, -200, 2740, 120, 650, sides=40))
        add("wood", ring_triangles(1450, -200, 2940, 200, 690, 640, sides=40))
        box("wood", 1450, -200, 3800, 650, 650, 500)
        box("wood", 1450, -200, 3520, 780, 780, 80)
        add("wood", cyl_triangles(1450, -200, 3290, 520, 65, r2=260))
        for x in (-650, 650):
            box("timber", x, -900, 1550, 180, 180, 2200)
        box("timber", 0, -900, 2450, 1600, 220, 200)
        box("wood", -1600, 1200, 1000, 1300, 650, 110)
        for x in (-2100, -1100):
            box("timber", x, 1200, 700, 100, 500, 650)
    elif name == "race":
        box("earth", -800, 0, -130, 11600, 11000, 180)
        # Stone wheelpit below the west-side wheel; no obstruction in its sweep.
        box("stone", -3900, -2500, -60, 1600, 4400, 160)
        for x in (-4650, -3150):
            for y in range(-4300, -400, 500):
                box("stone", x, y, 120, 250, 470, 400)
        # Aqueduct from the sluice to the top/back quadrant of the overshot wheel.
        box("wood", -1950, -3450, 3240, 4200, 850, 120)
        for y in (-3900, -3000):
            box("wood", -1950, y, 3470, 4200, 100, 480)
        for x in (-300, -2000):
            for y in (-3900, -3000):
                box("stone", x, y, 1600, 400, 400, 3200)
        for x in (-900, -3000):
            box("wood", x, -3450, 3120, 180, 1200, 180)
        # Gate throat connects its original pivot to the aqueduct.
        box("wood", 0, -2900, 3060, 1100, 900, 100)
        for x in (-520, 520):
            box("timber", x, -2670, 3340, 130, 150, 800)
        for y in range(-4200, -1000, 650):
            box("stone", -3200, y, 700, 260, 500, 1000)
        for x in range(-2000, 2500, 800):
            box("stone", x, 3300, 20, 650, 800, 100)
    elif name == "drive":
        add("timber", cyl_triangles(-1800, 0, 0, 5600, 145, axis="x"))
        # Two substantial cheeks, iron hoops, twelve paired oak spokes, 30 buckets.
        for x in (-4260, -3540):
            add("wood", ring_triangles(x, 0, 0, 160, 1600, 1330, axis="x", sides=64))
            add("iron", ring_triangles(x, 0, 0, 175, 1580, 1530, axis="x", sides=64))
            add("timber", cyl_triangles(x, 0, 0, 240, 280, axis="x"))
            for a in range(0, 360, 30):
                add(
                    "wood",
                    map_tris(
                        box_triangles(x, 0, 790, 140, 145, 1300), lambda p, a=a: rotate_x(p, a)
                    ),
                )
        for a in range(0, 360, 12):
            for mat, shape in (
                ("wood", box_triangles(-3900, 0, 1430, 700, 265, 75)),
                ("timber", box_triangles(-3900, 125, 1510, 700, 65, 200)),
            ):
                add(mat, map_tris(shape, lambda p, a=a: rotate_x(p, a)))
        # Pitwheel remains coaxial with the watershaft, inside the mill wall.
        add("wood", ring_triangles(0, 0, 0, 200, 1230, 880, axis="x", sides=80))
        for a in range(0, 360, 45):
            add(
                "timber",
                map_tris(box_triangles(0, 0, 600, 180, 160, 1100), lambda p, a=a: rotate_x(p, a)),
            )
        for i in range(80):
            add(
                "wood",
                map_tris(
                    box_triangles(100, 0, 1220, 140, 65, 120), lambda p, i=i: rotate_x(p, i * 4.5)
                ),
            )
        for x in (-3350, -2800, 450):
            add("iron", cyl_triangles(x, 0, 0, 160, 210, axis="x"))
    elif name == "sluice":
        for x in range(-400, 401, 100):
            box("wood", x, 160, -20, 94, 380, 70)
        for x in (-300, 300):
            box("iron", x, 160, 25, 40, 380, 25)
        add("iron", cyl_triangles(0, 0, 0, 1100, 45, axis="x"))
    elif name in ("main", "runner"):
        if name == "main":
            add("iron", cyl_triangles(0, 0, 2100, 4800, 110, sides=24))
            gears = ((900, 360, 24), (1260, 1288, 96), (4000, 420, 24))
        else:
            add("iron", cyl_triangles(0, 0, -380, 1100, 55, sides=20))
            gears = ((-700, 322, 24),)
            add("stone", ring_triangles(0, 0, 0, 160, 600, 70, sides=48))
            for angle in range(0, 360, 45):
                add(
                    "iron",
                    map_tris(
                        box_triangles(280, 0, 85, 260, 25, 15),
                        lambda p, angle=angle: rotate_z(p, angle),
                    ),
                )
        for z, radius, teeth in gears:
            add("wood", ring_triangles(0, 0, z, 130, radius - 30, radius * 0.68, sides=teeth))
            add("wood", cyl_triangles(0, 0, z, 200, 165, sides=24))
            for angle in range(0, 360, 60):
                add(
                    "timber",
                    map_tris(
                        box_triangles(radius / 2, 0, z, radius, 100, 110),
                        lambda p, angle=angle: rotate_z(p, angle),
                    ),
                )
            for i in range(teeth):
                add(
                    "wood",
                    map_tris(
                        box_triangles(radius, 0, z, 100, 40, 120),
                        lambda p, i=i: rotate_z(p, i * 360 / teeth),
                    ),
                )
    else:
        add("wood", mechanism_triangles(name))
    return solids if separate_solids else [(mat, tris) for mat, tris in groups.items() if tris]


def part_triangles(name):
    return [tri for _, tris in part_groups(name) for tri in tris]


def emit_scad():
    lines = [
        "// Generated from design.py: same triangle groups as the native OBJ.",
        'part = "all";',
        "drive_angle = $t * 360;",
        "gate_angle = 18;",
        f"main_angle = -drive_angle * {DRIVE_TO_MAIN};",
        f"runner_angle = main_angle * {MAIN_TO_RUNNER};",
        f"hoist_angle = main_angle * {MAIN_TO_HOIST};",
    ]
    for name in PART_NAMES:
        lines.append(f"module {name}() {{")
        for mat, tris in part_groups(name, separate_solids=True):
            points, faces, lookup = [], [], {}
            for tri in tris:
                face = []
                for vertex in reversed(tri):
                    key = tuple(round(v, 7) for v in vertex)
                    if key not in lookup:
                        lookup[key] = len(points)
                        points.append(key)
                    face.append(lookup[key])
                faces.append(face)
            lines.append(
                f"color({list(PALETTE[mat])}) polyhedron(points={json.dumps(points)}, faces={json.dumps(faces)}, convexity=12);"
            )
        lines.append("}")

    # Hierarchical pivots are the same contract used by native FK.
    def node(joint):
        name = joint["name"]
        angle = (
            "gate_angle"
            if name == "sluice"
            else f"{name}_angle"
            if name in ("drive", "main", "runner", "hoist")
            else "0"
        )
        result = f"translate({joint['pivot']}) rotate(a={angle}, v={joint['axis']}) {{ {name}();"
        for child in JOINTS:
            if child["parent"] == joint["name"]:
                result += node(child)
        return result + "}"

    lines.append('if (part == "all") ' + node(JOINTS[0]))
    for name in PART_NAMES:
        lines.append(f'if (part == "{name}") {name}();')
    return "\n".join(lines) + "\n"


def write_obj(path, triangles, groups=None):
    groups = groups or [("wood", triangles)]
    with path.open("w", encoding="utf-8") as out:
        out.write("# Generated by design.py; metres Y-up\nmtllib mill.mtl\ns off\n")
        index = 1
        for mat, tris in groups:
            out.write(f"g {path.stem}_{mat}\nusemtl {mat}\n")
            for tri in tris:
                vertices = [mm_to_m_yup(*v) for v in tri]
                a, b, c = vertices
                u = [b[j] - a[j] for j in range(3)]
                v = [c[j] - a[j] for j in range(3)]
                normal = (
                    u[1] * v[2] - u[2] * v[1],
                    u[2] * v[0] - u[0] * v[2],
                    u[0] * v[1] - u[1] * v[0],
                )
                length = math.sqrt(sum(n * n for n in normal))
                if length <= 1e-12:
                    raise ValueError(f"Degenerate triangle in {path}")
                for point in vertices:
                    out.write("v %.7g %.7g %.7g\n" % point)
                # Each triangle has one flat normal, shared by its three vertices.
                out.write("vn %.9g %.9g %.9g\n" % tuple(n / length for n in normal))
                n = (index + 2) // 3
                out.write(f"f {index}//{n} {index + 1}//{n} {index + 2}//{n}\n")
                index += 3


def write_materials_nut(path: Path):
    lines = ["// Auto-generated part materials for the SCAD watermill.", "millMaterials <- {"]
    for part in PARTS:
        t = part["tint"]
        lines.append(
            f"    {part['name']}={{r={t[0]},g={t[1]},b={t[2]},metal={part['metal']},rough={part['rough']}}},"
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
    (ASSETS / "mill.mtl").write_text(
        "".join(
            f"newmtl {name}\nKd {r} {g} {b}\nKs 0.04 0.04 0.04\nNs 12\n\n"
            for name, (r, g, b) in PALETTE.items()
        ).rstrip()
        + "\n",
        encoding="utf-8",
    )
    (ASSETS / "mill.scad").write_text(emit_scad(), encoding="utf-8")
    (ASSETS / "mill.rig.json").write_text(
        json.dumps(emit_rig(), indent=2, ensure_ascii=True) + "\n", encoding="utf-8"
    )
    write_materials_nut(ASSETS / "materials.nut")
    write_pose_constants(ROOT / "ratios.nut")
    for name in PART_NAMES:
        tris = part_triangles(name)
        write_obj(ASSETS / f"{name}.obj", tris, part_groups(name))
        print(f"{name}: wrote OBJ ({len(tris)} tris)", flush=True)


if __name__ == "__main__":
    main()
