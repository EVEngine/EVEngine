"""Regenerate maps/world.json for the EVEngine physics metroidvania demo."""

import json
from pathlib import Path

W, H, TILE = 120, 22, 32


def layer(fill=0):
    return [fill] * (W * H)


def put(data, x, y, gid):
    if 0 <= x < W and 0 <= y < H:
        data[y * W + x] = gid


def rect(data, x, y, w, h, gid):
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            put(data, xx, yy, gid)


def obj(name, kind, tx, ty, w=1, h=1):
    return {
        "name": name,
        "type": kind,
        "x": tx * TILE,
        "y": ty * TILE,
        "width": w * TILE,
        "height": h * TILE,
    }


background = layer()
terrain = layer()
foreground = layer()
hazards = layer()

# Region color language: grass forest, dirt ruins, stone mine/boss.
rect(background, 0, 18, 40, 1, 2)
rect(background, 40, 18, 40, 1, 4)
rect(background, 80, 18, 40, 1, 6)
rect(terrain, 0, 19, W, 3, 2)
for x in range(0, 40):
    put(terrain, x, 19, 1)
for x in range(40, 80):
    put(terrain, x, 19, 3)
for x in range(80, W):
    put(terrain, x, 19, 5)

# Forest: attack/kick playground and a wall-jump gate.
rect(terrain, 5, 16, 7, 1, 1)
rect(terrain, 15, 13, 7, 1, 1)
rect(terrain, 25, 16, 6, 1, 1)
# The left shaft wall floats above a three-tile entrance. Players collect wall
# jump before it, walk underneath, then alternate between the two faces.
rect(terrain, 34, 12, 1, 4, 2)
rect(terrain, 38, 10, 2, 9, 2)

# Ruins: vertical rooms for wall jumping, then a dash altar.
rect(terrain, 42, 16, 7, 1, 3)
rect(terrain, 51, 13, 5, 1, 3)
rect(terrain, 59, 12, 1, 7, 4)
rect(terrain, 64, 9, 1, 10, 4)
rect(terrain, 60, 15, 4, 1, 3)
rect(terrain, 65, 11, 7, 1, 3)
rect(terrain, 74, 15, 5, 1, 3)
for y in range(12, 19, 2):
    put(foreground, 57, y, 9)
    put(foreground, 66, y, 9)

# Mine: lava gaps, movable rocks and a long dash bridge.
for x in range(84, 88):
    put(terrain, x, 19, 0)
    put(hazards, x, 19, 11)
rect(terrain, 81, 15, 3, 1, 5)
rect(terrain, 88, 15, 4, 1, 5)
rect(terrain, 94, 12, 5, 1, 5)
rect(terrain, 101, 16, 3, 1, 5)
for x in range(84, 88):
    put(foreground, x, 17, 12)

# Boss arena boundary and decoration.
rect(terrain, 105, 10, 1, 9, 6)
rect(terrain, 119, 10, 1, 9, 6)
for x in (108, 112, 116):
    put(foreground, x, 18, 14)
put(foreground, 118, 18, 16)

objects = [
    obj("player_start", "player", 3, 18),
    obj("cp_forest", "checkpoint", 3, 18),
    obj("crate_1", "prop_crate", 9, 15),
    obj("crate_2", "prop_crate", 20, 18),
    obj("rock_1", "prop_rock", 28, 15),
    obj("scout_1", "enemy_melee", 18, 18),
    obj("scout_2", "enemy_leaper", 29, 15),
    # Full-height trigger before the shaft: running or double-jumping cannot
    # accidentally skip the ability required by the following room.
    obj("wall_jump", "ability_walljump", 32, 19, 2, 7),
    obj("cp_ruins", "checkpoint", 42, 18),
    obj("guard_1", "enemy_melee", 48, 18),
    obj("guard_2", "enemy_leaper", 68, 10),
    obj("dash", "ability_dash", 76, 14),
    obj("cp_mine", "checkpoint", 81, 18),
    obj("mine_platform", "moving_platform", 85, 17, 3, 1),
    obj("rock_2", "prop_rock", 89, 14),
    obj("crate_3", "prop_crate", 96, 11),
    obj("miner_1", "enemy_melee", 91, 18),
    obj("miner_2", "enemy_leaper", 100, 18),
    obj("cp_boss", "checkpoint", 108, 18),
    obj("dragon", "boss", 113, 12, 3, 3),
    obj("boss_exit", "exit", 117, 18, 2, 2),
]


def validate_level():
    """Catch ability-order and jump-route regressions before writing the map."""
    wall_jump = next(item for item in objects if item["type"] == "ability_walljump")
    trigger_left = wall_jump["x"] // TILE
    trigger_right = (wall_jump["x"] + wall_jump["width"]) // TILE
    assert trigger_left < 34 and trigger_right <= 34, "wall jump must be acquired before the shaft"
    assert wall_jump["y"] - wall_jump["height"] <= 12 * TILE, "wall-jump trigger is too short"
    assert all(terrain[y * W + 34] == 0 for y in range(16, 19)), "shaft entrance is blocked"
    assert all(terrain[y * W + x] == 0 for y in range(10, 19) for x in range(35, 38)), (
        "wall-jump shaft contains an impassable crossbar"
    )

    gap = longest_gap = 0
    for x in range(W):
        if terrain[19 * W + x] == 0:
            gap += 1
            longest_gap = max(longest_gap, gap)
        else:
            gap = 0
    assert longest_gap <= 4, "ground gap exceeds the configured running-jump route"


validate_level()

tileset = {
    "firstgid": 1,
    "image": "assets/tiles.png",
    "columns": 4,
    "tilewidth": TILE,
    "tileheight": TILE,
    "imagewidth": 128,
    "imageheight": 128,
}
doc = {
    "width": W,
    "height": H,
    "tilewidth": TILE,
    "tileheight": TILE,
    "orientation": "orthogonal",
    "autoReload": True,
    "tilesets": [tileset],
    "layers": [
        {"type": "tilelayer", "name": "background", "layer": -2, "width": W, "height": H, "data": background},
        {"type": "tilelayer", "name": "collision", "layer": 0, "width": W, "height": H, "data": terrain},
        {"type": "tilelayer", "name": "hazards", "layer": 1, "width": W, "height": H, "data": hazards},
        {"type": "tilelayer", "name": "foreground", "layer": 5, "width": W, "height": H, "data": foreground},
        {"type": "objectgroup", "name": "gameplay", "objects": objects},
    ],
}

out = Path(__file__).with_name("maps") / "world.json"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(doc, indent=2), encoding="utf-8")
print(out)
