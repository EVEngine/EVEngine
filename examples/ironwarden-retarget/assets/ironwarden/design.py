"""RX-78-2 reference study. All dimensions in mm, front -Y, Z up.

Authoritative geometry, material palette and humanoid joint landmarks.
Official visual reference URLs are recorded in ../../REFERENCE.md.
"""

import math
import random

PARTS = []
PALETTE = {
    "white": [0.86, 0.88, 0.85],
    "grey": [0.49, 0.54, 0.56],
    "blue": [0.045, 0.12, 0.42],
    "red": [0.62, 0.028, 0.045],
    "yellow": [0.94, 0.63, 0.075],
    "dark": [0.045, 0.052, 0.065],
    "steel": [0.23, 0.27, 0.30],
    "chrome": [0.56, 0.63, 0.67],
    "rubber": [0.018, 0.023, 0.03],
    "cyan": [0.65, 0.94, 0.12],
    "rust": [0.24, 0.09, 0.035],
}
JOINTS = {
    "pelvis": [0, 1, 106],
    "spine1": [0, 1, 119],
    "spine2": [0, 1, 135],
    "chest": [0, 1, 148],
    "neck": [0, 0, 173],
    "head": [0, 0, 183],
    "shoulder": [31, 0, 158],
    "elbow": [39, 0, 129],
    "wrist": [41, -2, 101],
    "fingers": [41, -2, 91],
    "hip": [15, 1, 104],
    "knee": [17, 0, 63],
    "ankle": [18, 0, 16],
    "toe": [18, -18, 8],
}


def box(name, pos, size, mat, bone, bevel=0.45, rot=(0, 0, 0)):
    PARTS.append(
        dict(
            name=name,
            kind="box",
            pos=list(pos),
            size=list(size),
            mat=mat,
            bone=bone,
            bevel=min(bevel, min(size) * 0.42),
            rot=list(rot),
        )
    )


def cylinder(name, pos, radius, length, mat, bone, rot=(0, 0, 0), r2=None):
    PARTS.append(
        dict(
            name=name,
            kind="cylinder",
            pos=list(pos),
            size=[radius, length],
            topRadius=radius if r2 is None else r2,
            mat=mat,
            bone=bone,
            bevel=0.16,
            rot=list(rot),
        )
    )


def mesh(name, pos, vertices, faces, mat, bone, bevel=0.12, rot=(0, 0, 0)):
    PARTS.append(
        dict(
            name=name,
            kind="mesh",
            pos=list(pos),
            vertices=vertices,
            faces=faces,
            mat=mat,
            bone=bone,
            bevel=bevel,
            rot=list(rot),
        )
    )


def loft(name, pos, rings, mat, bone, bevel=0.12, rot=(0, 0, 0)):
    """Eight-sided cross sections (z,width,depth,corner,[x offset],[y offset])."""
    vertices = []
    for ring in rings:
        z, w, d, c = ring[:4]
        ox = ring[4] if len(ring) > 4 else 0
        oy = ring[5] if len(ring) > 5 else 0
        c = min(c, w * 0.45, d * 0.45)
        x = w / 2
        y = d / 2
        vertices.extend(
            [
                [a + ox, b + oy, z]
                for a, b in [
                    (-x + c, -y),
                    (x - c, -y),
                    (x, -y + c),
                    (x, y - c),
                    (x - c, y),
                    (-x + c, y),
                    (-x, y - c),
                    (-x, -y + c),
                ]
            ]
        )
    faces = [list(reversed(range(8))), list(range((len(rings) - 1) * 8, len(rings) * 8))]
    for j in range(len(rings) - 1):
        for k in range(8):
            faces.append(
                [j * 8 + k, j * 8 + (k + 1) % 8, (j + 1) * 8 + (k + 1) % 8, (j + 1) * 8 + k]
            )
    mesh(name, pos, vertices, faces, mat, bone, bevel, rot)


def plate(name, pos, outline, depth, mat, bone, bevel=0.10):
    """Extrude a CCW X/Z silhouette along Y; supports shield, antenna and face wedges."""
    if (
        sum(
            x * outline[(i + 1) % len(outline)][1] - outline[(i + 1) % len(outline)][0] * z
            for i, (x, z) in enumerate(outline)
        )
        < 0
    ):
        outline = list(reversed(outline))
    n = len(outline)
    vertices = [[x, y, z] for y in [-depth / 2, depth / 2] for x, z in outline]
    faces = [list(range(n)), list(reversed(range(n, n * 2)))]
    faces.extend([[k, n + k, n + (k + 1) % n, (k + 1) % n] for k in range(n)])
    mesh(name, pos, vertices, faces, mat, bone, bevel)


def slot(name, pos, width, height, bone, mat="dark"):
    box(name, pos, (width, 0.35, height), mat, bone, 0.08)


P = "IW_pelvis"
C = "IW_chest"
H = "IW_head"
# Inverted-triangle chest, recessed yellow intake frames and narrow red core.
loft("chest_core", (0, 1, 150), [(-15, 31, 23, 4), (3, 44, 27, 4), (14, 41, 25, 4)], "blue", C)
loft("upper_chest_mantle", (0, -2, 160), [(-2, 43, 23, 3), (3, 39, 22, 3)], "blue", C)
loft("cockpit_nose", (0, -15, 146), [(-14, 7, 5, 1), (-4, 10, 8, 1), (10, 13, 6, 1)], "blue", C)
box("collar_socket", (0, 0, 170), (21, 17, 5), "dark", C, 0.65)
plate("collar_gold", (0, -7, 169), [(-11, -2), (11, -2), (9, 3), (-9, 3)], 6, "yellow", C)
for s in [-1, 1]:
    box("chest_intake_frame", (s * 13, -15.4, 153), (13, 3.5, 13), "yellow", C, 0.65)
    box("chest_intake_recess", (s * 13, -17.3, 153), (10, 1, 10), "dark", C, 0.1)
    for z in [-3.6, 0, 3.6]:
        box(
            "chest_intake_louvre",
            (s * 13, -18.0, 153 + z),
            (10.5, 1.3, 1.1),
            "yellow",
            C,
            0.12,
            (-12, 0, 0),
        )
    slot("chest_seam", (s * 12, -16, 162), 11, 0.38, C)
loft(
    "red_abdomen",
    (0, 1, 130),
    [(-11, 21, 17, 3), (0, 28, 21, 3), (10, 29, 21, 3)],
    "red",
    "IW_spine_01",
)
box("waist_mechanism", (0, 0, 117), (22, 17, 6), "dark", P, 0.7)
for z in [115.8, 118.1]:
    box("waist_rail", (0, -10, z), (21, 2, 1), "steel", P, 0.1)
loft("pelvis_core", (0, 1, 109), [(-8, 25, 20, 3), (7, 31, 23, 3)], "white", P)
plate("pelvis_red_badge", (0, -13, 113), [(-5, -4), (5, -4), (6, 5), (-6, 5)], 2, "red", P)
plate(
    "pelvis_gold_chevron",
    (0, -14.4, 114),
    [(-3, 1), (0, -2), (3, 1), (2, 2), (0, 0), (-2, 2)],
    0.5,
    "yellow",
    P,
)
for s in [-1, 1]:
    side = ".L" if s > 0 else ".R"
    thigh = "IW_thigh" + side
    calf = "IW_calf" + side
    foot = "IW_foot" + side
    upper = "IW_upperarm" + side
    fore = "IW_forearm" + side
    hand = "IW_hand" + side
    # Split skirt plates, thigh shells and elongated shin silhouettes.
    loft(
        "front_skirt", (s * 10, -11.5, 109), [(-13, 17, 5, 2, 0, -2), (10, 13, 6, 1.5)], "white", P
    )
    box("skirt_yellow_latch", (s * 10, -16, 114), (8, 2, 7), "yellow", P, 0.5)
    slot("skirt_lower_seam", (s * 10, -16.25, 99), 10, 0.5, P, "grey")
    loft(
        "side_skirt",
        (s * 22, 1, 111),
        [(-13, 6, 22, 2), (8, 5, 18, 2)],
        "white",
        P,
        0.1,
        (0, -s * 12, 0),
    )
    loft("rear_skirt", (s * 10, 13, 108), [(-11, 17, 5, 1), (10, 14, 5, 1)], "white", P)
    cylinder("hip_motor", (s * 15, 1, 104), 5.5, 13, "steel", thigh, (0, 90, 0))
    loft("thigh_inner_frame", (s * 16, 1, 86), [(-18, 10, 12, 2), (18, 11, 12, 2)], "dark", thigh)
    loft(
        "thigh_white_shell",
        (s * 16, -1, 86),
        [(-17, 14, 15, 3), (5, 19, 18, 3), (18, 16, 17, 3)],
        "white",
        thigh,
        0.2,
    )
    loft("thigh_outer_overlay", (s * 23, 0, 87), [(-12, 3, 12, 1), (13, 3, 14, 1)], "grey", thigh)
    slot("thigh_service_seam", (s * 16, -10.3, 85), 10, 0.4, thigh, "grey")
    cylinder("knee_hinge", (s * 17, 0, 63), 5.5, 21, "steel", calf, (0, 90, 0))
    cylinder("knee_piston", (s * 23, 6, 64), 1.1, 14, "chrome", calf)
    for dx in [-11, 11]:
        cylinder("knee_white_motor_cap", (s * 17 + dx, 0, 63), 4.3, 1.4, "white", calf, (0, 90, 0))
        cylinder(
            "knee_motor_inset", (s * 17 + dx * 1.04, 0, 63), 2.6, 1.5, "grey", calf, (0, 90, 0)
        )
    loft(
        "knee_armour",
        (s * 17, -9, 64),
        [(-7, 11, 6, 1), (1, 16, 9, 2), (12, 12, 6, 1, 0, 1)],
        "white",
        calf,
    )
    loft("shin_frame", (s * 18, 2, 40), [(-21, 10, 13, 2), (20, 12, 14, 2)], "dark", calf)
    loft(
        "shin_outer_armour",
        (s * 18, 0, 39),
        [(-21, 21, 18, 3), (-12, 18, 17, 3), (7, 22, 23, 4), (19, 18, 19, 3)],
        "white",
        calf,
        0.2,
    )
    loft(
        "shin_ridge",
        (s * 18, -10, 39),
        [(-16, 9, 4, 1), (4, 10, 5, 1), (15, 7, 4, 1)],
        "white",
        calf,
    )
    for dz in [-6, -3, 0]:
        slot("shin_front_duct", (s * 18, -13, 42 + dz), 7.0, 1, calf)
    slot("shin_lower_seam", (s * 18, -9.7, 24), 12, 0.4, calf, "grey")
    box("calf_access_hatch", (s * 18, 12.2, 42), (10, 1.4, 15), "grey", calf, 0.3)
    for z in [38, 41, 44, 47]:
        box("calf_louvre", (s * 18, 13.1, z), (7, 0.5, 1), "dark", calf, 0.1)
    cylinder("ankle_joint", (s * 18, 0, 16), 4, 19, "steel", foot, (0, 90, 0))
    loft("ankle_cuff", (s * 18, -1, 18), [(-3, 20, 19, 2), (4, 18, 17, 2)], "white", calf)
    loft(
        "red_foot",
        (s * 18, -8, 6),
        [(-4, 19, 33, 3), (-1, 20, 34, 3), (6, 16, 25, 3, 0, 3)],
        "red",
        foot,
        0.15,
    )
    box("foot_sole", (s * 18, -8, 1.2), (18.5, 32, 2), "rubber", foot, 0.3)
    loft("white_instep", (s * 18, -3, 12), [(-2, 16, 21, 2), (6, 15, 15, 2, 0, 3)], "white", foot)
    slot("toe_insert", (s * 18, -25.1, 4.3), 9, 1, foot, "grey")
    # Shoulder cap facets, visible motor and restrained inset panel detail.
    cylinder("shoulder_joint", (s * 31, 0, 158), 5.8, 14, "steel", upper, (0, 90, 0))
    loft(
        "shoulder_armour",
        (s * 35, 0, 162),
        [(-12, 17, 18, 3), (-5, 25, 23, 3), (9, 24, 22, 3), (12, 20, 18, 2)],
        "white",
        upper,
        0.2,
    )
    loft("shoulder_top_cap", (s * 35, 0, 174), [(-1.5, 22, 20, 2), (1, 20, 18, 2)], "white", upper)
    slot("shoulder_panel_seam", (s * 35, -12, 166), 17, 0.4, upper, "grey")
    for x in [-7, 7]:
        box("shoulder_dark_recess", (s * 35 + x, -11.6, 170), (2, 0.4, 2), "dark", upper, 0.12)
    box("upper_arm_frame", (s * 35, 1, 144), (8, 12, 20), "dark", upper, 0.6)
    loft("upper_arm_shell", (s * 35, -1, 144), [(-9, 11, 14, 2), (8, 13, 15, 2)], "white", upper)
    slot("upper_arm_seam", (s * 35, -8.6, 145), 8, 0.4, upper, "grey")
    cylinder("elbow_motor", (s * 39, 0, 129), 4.8, 16, "steel", fore, (0, 90, 0))
    cylinder("elbow_cap", (s * 47.2, 0, 129), 3.8, 1.4, "white", fore, (0, 90, 0))
    loft(
        "forearm_armour",
        (s * 40, -1, 115),
        [(-13, 13, 15, 2), (-4, 17, 19, 3), (10, 14, 16, 2)],
        "white",
        fore,
    )
    box("forearm_panel", (s * 40, -11, 114), (9, 1.5, 12), "grey", fore, 0.3)
    box("forearm_inner_panel", (s * 40, -12, 114), (7, 1, 9), "white", fore, 0.25)
    slot("forearm_service_line", (s * 40, -12.8, 115), 5, 0.35, fore, "grey")
    box("wrist_cuff", (s * 41, -1, 101), (12, 14, 4), "dark", hand, 0.4)
    box("hand_palm", (s * 41, -2, 96), (10, 11, 9), "dark", hand, 0.8)
    for i in range(4):
        box("finger_knuckle", (s * 41 + (i - 1.5) * 2.3, -8, 96), (2, 3.5, 4), "steel", hand, 0.35)
        box("finger_digit", (s * 41 + (i - 1.5) * 2.3, -8, 92.9), (2, 3.2, 3), "dark", hand, 0.3)
    box("thumb", (s * 47, -4, 96), (3, 4, 6), "dark", hand, 0.5, (0, s * 20, 0))

# Helmet is a layered sculpted shell, not a rectangular robot head.
cylinder("neck_column", (0, 0, 176), 3.7, 8, "steel", "IW_neck")
loft(
    "helmet_shell",
    (0, 0, 191),
    [(-10, 14, 13, 3), (-4, 21, 17, 4), (7, 20, 17, 4), (12, 14, 13, 3), (14, 8, 10, 2)],
    "white",
    H,
    0.16,
)
box("face_shadow", (0, -8.8, 189), (14, 3, 10), "dark", H, 0.25)
plate(
    "face_mask",
    (0, -11.2, 184),
    [(-3, -6), (3, -6), (6, -2), (5, 3), (0, 5), (-5, 3), (-6, -2)],
    3.3,
    "white",
    H,
)
plate("brow_left", (0, -11.3, 191), [(-8, 2), (-1, 0.5), (-1, -0.7), (-7, 0.4)], 1.5, "white", H)
plate("brow_right", (0, -11.3, 191), [(1, 0.5), (8, 2), (7, 0.4), (1, -0.7)], 1.5, "white", H)
plate(
    "left_eye",
    (0, -12.15, 190),
    [(-6, 0.5), (-1, -0.5), (-1, -1.7), (-5, -0.9)],
    0.4,
    "cyan",
    H,
    0.03,
)
plate(
    "right_eye", (0, -12.15, 190), [(1, -0.5), (6, 0.5), (5, -0.9), (1, -1.7)], 0.4, "cyan", H, 0.03
)
loft("chin_red", (0, -12.1, 178), [(-2, 2.7, 3, 0.5), (3, 4.6, 4, 0.6)], "red", H)
for z in [181.6, 183.3]:
    slot("mask_mouth_vent", (0, -13.0, z), 3.5, 0.5, H)
for s in [-1, 1]:
    cylinder("ear_motor", (s * 10.4, 0, 191), 3.8, 3, "grey", H, (0, 90, 0))
    loft("cheek_guard", (s * 8, -5, 184), [(-5, 4, 8, 1), (7, 4.8, 11, 1)], "white", H)
    for z in [181, 184, 187, 190]:
        box("cheek_exhaust", (s * 8, -10.7, z), (2, 0.4, 1), "dark", H, 0.06)
    cylinder("head_vulcan_socket", (s * 6.4, -8, 197), 1.5, 2, "grey", H, (90, 0, 0))
    cylinder("head_vulcan_bore", (s * 6.4, -9.1, 197), 0.8, 0.4, "dark", H, (90, 0, 0))
    # Tapered, broad-at-root V fins with sharp outer tips.
    points = [(s * 1.5, 0), (s * 6, 1), (s * 24, 19), (s * 5, 7)]
    if s < 0:
        points.reverse()
    plate("v_fin", (0, -10.0, 195), points, 1.25, "white", H, 0.04)
loft("forehead_red_camera_mount", (0, -9, 199), [(-5, 3.8, 4, 1), (4, 3, 3, 0.5)], "red", H)
box("forehead_camera", (0, -11.2, 203), (2, 0.5, 2), "cyan", H, 0.15)
loft("helmet_crest", (0, 1, 202), [(-3, 4, 18, 1), (5, 3, 11, 1)], "white", H)
box("rear_head_camera", (0, 8.6, 197), (3, 0.6, 2.5), "red", H, 0.2)

# Compact backpack, twin bell nozzles and vertical beam-saber grips.
loft("backpack", (0, 21, 156), [(-16, 25, 14, 3), (15, 29, 17, 3)], "dark", C)
for s in [-1, 1]:
    cylinder("backpack_upper_fan", (s * 8, 30, 164), 4.5, 3, "steel", C, (-90, 0, 0))
    cylinder("backpack_fan_centre", (s * 8, 31.8, 164), 2, 1, "dark", C, (-90, 0, 0))
    cylinder("thruster_bell", (s * 9, 29, 143), 3.1, 10, "steel", C, (-90, 0, 0), 5.8)
    cylinder("thruster_nozzle", (s * 9, 34.3, 143), 4.6, 0.7, "dark", C, (-90, 0, 0))
    cylinder("thruster_core", (s * 9, 34.8, 143), 1.9, 0.8, "grey", C, (-90, 0, 0))
    for i in range(6):
        a = i * math.tau / 6
        box(
            "thruster_vane",
            (s * 9 + 3.4 * math.cos(a), 34.9, 143 + 3.4 * math.sin(a)),
            (0.8, 0.6, 2),
            "steel",
            C,
            0.08,
            (0, math.degrees(-a), 0),
        )
    box("saber_mount", (s * 12, 22, 174), (7, 9, 8), "grey", C, 0.5)
    cylinder("beam_saber_hilt", (s * 12, 22, 186), 2.3, 22, "white", C)
    for z in [178, 182, 195, 198]:
        cylinder("saber_hilt_ring", (s * 12, 22, z), 2.6, 1.2, "grey" if z == 178 else "white", C)
    box("backpack_service_rail", (s * 16, 23, 153), (2, 5, 18), "steel", C, 0.2)

# White edged red shield, yellow cross, lower spear-point silhouette.
shield = "IW_forearm.R"
outline = [(-12, 35), (12, 35), (15, 27), (13, -24), (0, -39), (-13, -24), (-15, 27)]
outline.reverse()
plate("shield_white_frame", (-48, -18, 121), outline, 5, "white", shield, 0.22)
inner = [(-9, 31), (9, 31), (11.5, 24), (10, -22), (0, -34), (-10, -22), (-11.5, 24)]
inner.reverse()
plate("shield_red_inset", (-48, -21, 121), inner, 1.6, "red", shield, 0.15)
box("shield_view_frame", (-48, -22.4, 145), (12, 1.3, 4), "grey", shield, 0.3)
slot("shield_view_slot", (-48, -23.2, 145), 9, 1.8, shield)
box("shield_cross_vertical", (-48, -22.5, 115), (3.2, 0.8, 24), "yellow", shield, 0.12)
box("shield_cross_horizontal", (-48, -22.6, 119), (17, 0.8, 3.2), "yellow", shield, 0.12)
for z in [134, 94]:
    slot("shield_panel_line", (-48, -22.2, z), 16, 0.45, shield, "dark")
box("shield_handle", (-44, -8, 119), (5, 13, 8), "steel", shield, 0.4)

# Human-sized beam rifle held by the weapon hand, not fused into a forearm.
weapon = "IW_hand.L"
box("rifle_receiver", (41, -24, 97), (9, 27, 12), "dark", weapon, 0.6)
box("rifle_upper_shroud", (41, -24, 104), (10, 24, 3), "grey", weapon, 0.35)
box("rifle_grip", (41, -10, 93), (6, 7, 12), "dark", weapon, 0.6, (20, 0, 0))
box("rifle_magazine", (41, -25, 87), (6, 9, 9), "steel", weapon, 0.3, (-12, 0, 0))
cylinder("rifle_barrel", (41, -51, 98), 2.1, 30, "dark", weapon, (90, 0, 0))
cylinder("rifle_muzzle_collar", (41, -66, 98), 3.1, 4, "grey", weapon, (90, 0, 0))
cylinder("rifle_muzzle_bore", (41, -68.2, 98), 1.5, 0.5, "dark", weapon, (90, 0, 0))
box("rifle_sight_mount", (41, -23, 109), (5, 8, 5), "dark", weapon, 0.25)
cylinder("rifle_optic", (41, -27.5, 109), 2.4, 2, "yellow", weapon, (90, 0, 0))
cylinder("rifle_optic_glass", (41, -28.7, 109), 1.7, 0.3, "cyan", weapon, (90, 0, 0))
for y in [-34, -30, -26]:
    box("rifle_cooling_slot", (46, -0 + y, 97), (0.35, 1, 6), "steel", weapon, 0.05)

# Discrete service fasteners and limited oxidation; keep the white surfaces legible.
for s in [-1, 1]:
    side = ".L" if s > 0 else ".R"
    for z in [31, 50]:
        cylinder(
            "leg_service_fastener",
            (s * 24, -8, z),
            0.55,
            0.7,
            "steel",
            "IW_calf" + side,
            (90, 0, 0),
        )
    cylinder("ankle_oxidation", (s * 27.6, 0, 16), 1, 0.5, "rust", "IW_foot" + side, (0, 90, 0))
rng = random.Random(417)
for i in range(24):
    s = 1 if i % 2 else -1
    z = rng.uniform(25, 54)
    # Follow the actual sloped ridge surface, rather than scattering detached flakes.
    front_y = -12 - 0.5 * (z - 23) / 20 if z <= 43 else -12.5 + 0.5 * (z - 43) / 11
    box(
        "edge_paint_chip",
        (s * 18 + rng.choice([-1, 1]) * rng.uniform(1.8, 2.4), front_y - 0.04, z),
        (rng.uniform(0.25, 0.8), 0.14, rng.uniform(0.15, 0.5)),
        "steel",
        "IW_calf" + (".L" if s > 0 else ".R"),
        0.03,
    )

# The jaw nestles into the collar; the exposed neck must not lengthen the silhouette.
JOINTS["head"][2] -= 4
JOINTS["neck"][2] -= 1
for part in PARTS:
    if part["bone"] == H:
        part["pos"][2] -= 4
    elif part["name"] == "neck_column":
        part["pos"][2] -= 2
        part["size"][1] -= 2
