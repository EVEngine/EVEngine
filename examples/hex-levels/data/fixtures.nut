// Squirrel mirrors of fixture JSON used by examples/hex-levels/main.nut.
// Canonical sources remain the *.json files (consumed by C++ tests).

LEVEL_CATALOG <- {
    [0] = { key = "pipeline_full", seed = 20260812, algo = "dungeon.bsp", loot = "raid", w = 36, h = 28 },
    [1] = { key = "procgen_path", seed = 42, algo = "dungeon.bsp", loot = "starter", w = 32, h = 24 },
    [2] = { key = "dynamic_fov", seed = 7, algo = "cave.cellular", loot = "cave", w = 28, h = 20 },
    [3] = { key = "dynamic_light", seed = 99, algo = "dungeon.bsp", loot = "starter", w = 24, h = 18 },
    [4] = { key = "pickup_collision", seed = 123, algo = "dungeon.bsp", loot = "rich", w = 24, h = 18 },
    [5] = { key = "particles", seed = 55, algo = "dungeon.bsp", loot = "starter", w = 20, h = 16 },
    [6] = { key = "flow_field_swarm", seed = 606, algo = "dungeon.bsp", loot = "starter", w = 28, h = 22 },
    [7] = { key = "cell_cost_detour", seed = 707, algo = "dungeon.bsp", loot = "starter", w = 26, h = 18 },
    [8] = { key = "multi_revealer", seed = 808, algo = "cave.cellular", loot = "cave", w = 24, h = 18 },
    [9] = { key = "fow_mask", seed = 909, algo = "dungeon.bsp", loot = "starter", w = 20, h = 16 }
};

LOOT_TABLES <- {
    starter = [
        { itemId = "hex.potion", ox = 1, oy = 0, qty = 1 },
        { itemId = "hex.key", ox = 0, oy = 1, qty = 1 },
        { itemId = "hex.coin", ox = -1, oy = 0, qty = 5 }
    ],
    cave = [
        { itemId = "hex.ore", ox = 1, oy = 0, qty = 3 },
        { itemId = "hex.crystal", ox = 0, oy = 1, qty = 1 },
        { itemId = "hex.ration", ox = -1, oy = 1, qty = 2 },
        { itemId = "hex.torch", ox = 1, oy = -1, qty = 1 }
    ],
    rich = [
        { itemId = "hex.potion", ox = 1, oy = 0, qty = 2 },
        { itemId = "hex.key", ox = 0, oy = 1, qty = 1 },
        { itemId = "hex.gem", ox = -1, oy = 0, qty = 1 },
        { itemId = "hex.coin", ox = 1, oy = 1, qty = 12 },
        { itemId = "hex.map_fragment", ox = -1, oy = -1, qty = 1 },
        { itemId = "hex.boots", ox = 2, oy = 0, qty = 1 }
    ],
    raid = [
        { itemId = "hex.relic", ox = 0, oy = 0, qty = 1, pathMid = true },
        { itemId = "hex.gem", ox = 2, oy = 0, qty = 1 },
        { itemId = "hex.potion", ox = 0, oy = 2, qty = 2 },
        { itemId = "hex.torch", ox = -2, oy = 0, qty = 1 },
        { itemId = "hex.coin", ox = 1, oy = -1, qty = 20 },
        { itemId = "hex.crystal", ox = -1, oy = 1, qty = 1 },
        { itemId = "hex.scroll", ox = 2, oy = 1, qty = 1 }
    ],
    equipment = [
        { itemId = "hex.boots", ox = 1, oy = 0, qty = 1 },
        { itemId = "hex.shield", ox = 0, oy = 1, qty = 1 },
        { itemId = "hex.scroll", ox = -1, oy = 0, qty = 1 },
        { itemId = "hex.bomb", ox = 1, oy = 1, qty = 1 },
        { itemId = "hex.coin", ox = -1, oy = 1, qty = 8 }
    ]
};
