// Auto-synced mirror of catalog.json for examples/hex-levels/main.nut.
// Canonical source: catalog.json (C++ tests). Keep playable ids 0–9.

LEVEL_CATALOG <- {

    [0] = {
        key = "pipeline_full",
        name = "综合通关",
        seed = 20260812,
        algo = "dungeon.bsp",
        loot = "raid",
        w = 36,
        h = 28,
        fov = {
            algorithm = "shadowcast",
            heroRadius = 5,
            heroPerception = 2.0,
            torchRadius = 3,
            perceptionScale = 1.0
        },
        light = {
            type = "point",
            radius = 160,
            color = [1.0, 0.78, 0.45, 2.0]
        },
        cellCost = {
            cost = 8.0,
            stripWidth = 4
        },
        swarmStarts = 4,
        particles = ["torch_fire", "pickup_burst", "ember_trail"],
        enable = {
            path = true,
            fov = true,
            light = true,
            pickup = true,
            particles = true,
            flow = true,
            cellcost = true,
            perception = true
        }
    },

    [1] = {
        key = "procgen_path",
        name = "程序化寻路",
        seed = 42,
        algo = "dungeon.bsp",
        loot = "starter",
        w = 32,
        h = 24,
        fov = {
            algorithm = "shadowcast",
            radius = 0,
            enabled = false
        },
        enable = {
            path = true,
            fov = false,
            light = false,
            pickup = false,
            particles = false
        }
    },

    [2] = {
        key = "dynamic_fov",
        name = "动态视野",
        seed = 7,
        algo = "cave.cellular",
        loot = "cave",
        w = 28,
        h = 20,
        params = {
            loops = 4,
            fill = 0.45
        },
        fov = {
            algorithm = "shadowcast",
            radius = 6
        },
        enable = {
            path = true,
            fov = true,
            light = false,
            pickup = false,
            particles = false
        }
    },

    [3] = {
        key = "dynamic_light",
        name = "动态光照",
        seed = 99,
        algo = "dungeon.bsp",
        loot = "starter",
        w = 24,
        h = 18,
        fov = {
            algorithm = "shadowcast",
            radius = 5
        },
        light = {
            type = "point",
            radius = 140,
            color = [1.0, 0.75, 0.45, 2.2]
        },
        enable = {
            path = true,
            fov = true,
            light = true,
            pickup = false,
            particles = false
        }
    },

    [4] = {
        key = "pickup_collision",
        name = "拾取碰撞",
        seed = 123,
        algo = "dungeon.bsp",
        loot = "rich",
        w = 24,
        h = 18,
        fov = {
            algorithm = "shadowcast",
            radius = 5
        },
        enable = {
            path = true,
            fov = true,
            light = true,
            pickup = true,
            particles = false
        }
    },

    [5] = {
        key = "particles",
        name = "粒子系统",
        seed = 55,
        algo = "dungeon.bsp",
        loot = "starter",
        w = 20,
        h = 16,
        fov = {
            algorithm = "shadowcast",
            radius = 5
        },
        particles = ["torch_fire", "pickup_burst"],
        enable = {
            path = true,
            fov = true,
            light = true,
            pickup = true,
            particles = true
        }
    },

    [6] = {
        key = "flow_field_swarm",
        name = "Flow Field 群体",
        seed = 606,
        algo = "dungeon.bsp",
        loot = "starter",
        w = 28,
        h = 22,
        fov = {
            algorithm = "shadowcast",
            radius = 5
        },
        swarmStarts = 4,
        enable = {
            path = true,
            fov = true,
            light = true,
            pickup = true,
            particles = true,
            flow = true
        }
    },

    [7] = {
        key = "cell_cost_detour",
        name = "格子代价绕路",
        seed = 707,
        algo = "dungeon.bsp",
        loot = "starter",
        w = 26,
        h = 18,
        fov = {
            algorithm = "shadowcast",
            radius = 5
        },
        cellCost = {
            cost = 8.0,
            stripWidth = 4
        },
        enable = {
            path = true,
            fov = true,
            light = true,
            pickup = true,
            particles = false,
            cellcost = true
        }
    },

    [8] = {
        key = "multi_revealer",
        name = "多观察者感知",
        seed = 808,
        algo = "cave.cellular",
        loot = "cave",
        w = 24,
        h = 18,
        params = {
            loops = 4,
            fill = 0.45
        },
        fov = {
            algorithm = "shadowcast",
            heroRadius = 4,
            heroPerception = 2.0,
            torchRadius = 3,
            perceptionScale = 1.0
        },
        enable = {
            path = true,
            fov = true,
            light = true,
            pickup = true,
            particles = false,
            perception = true
        }
    },

    [9] = {
        key = "fow_mask",
        name = "FoW 遮罩算法",
        seed = 909,
        algo = "dungeon.bsp",
        loot = "starter",
        w = 20,
        h = 16,
        fov = {
            algorithm = "shadowcast",
            radius = 6
        },
        fovAlgorithms = ["shadowcast", "raycast", "permissive", "rectangle"],
        enable = {
            path = true,
            fov = true,
            light = true,
            pickup = true,
            particles = false,
            mask = true
        }
    }
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

