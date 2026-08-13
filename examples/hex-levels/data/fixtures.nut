// Auto-synced mirror of catalog.json for examples/hex-levels/main.nut.
// Canonical source: catalog.json (C++ tests). Playable ids follow catalog.

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
    },
    [10] = {
        key = "camera_pick",
        name = "相机拾取",
        seed = 1010,
        algo = "dungeon.bsp",
        loot = "starter",
        w = 22,
        h = 16,
        fov = {
            algorithm = "raycast",
            radius = 5
        },
        enable = {
            path = true,
            fov = true,
            light = true,
            pickup = true,
            camera = true
        },
        camera = {
            zoom = 1.2
        }
    },
    [11] = {
        key = "dual_grid",
        name = "双网格六角",
        seed = 1111,
        algo = "dungeon.bsp",
        loot = "starter",
        w = 20,
        h = 16,
        fov = {
            algorithm = "shadowcast",
            radius = 4
        },
        enable = {
            path = true,
            fov = true,
            dualgrid = true
        }
    },
    [12] = {
        key = "procgen_variants",
        name = "程序化变体",
        seed = 1212,
        algo = "maze.backtrack",
        loot = "cave",
        w = 24,
        h = 18,
        params = {
        },
        fov = {
            algorithm = "permissive",
            radius = 5
        },
        enable = {
            path = true,
            fov = true,
            pickup = true
        }
    },
    [13] = {
        key = "quadtree_cull",
        name = "四叉树裁剪",
        seed = 1313,
        algo = "dungeon.bsp",
        loot = "starter",
        w = 32,
        h = 24,
        fov = {
            algorithm = "rectangle",
            radius = 7
        },
        enable = {
            path = true,
            fov = true,
            cull = true,
            pickup = true
        }
    },
    [14] = {
        key = "multi_light",
        name = "多光源",
        seed = 1414,
        algo = "dungeon.bsp",
        loot = "starter",
        w = 24,
        h = 18,
        fov = {
            algorithm = "shadowcast",
            radius = 5
        },
        light = {
            type = "multi",
            count = 3,
            radius = 120,
            color = [0.85, 0.9, 1.0, 1.8]
        },
        enable = {
            path = true,
            fov = true,
            light = true
        }
    },
    [15] = {
        key = "particle_stash",
        name = "粒子与仓库",
        seed = 1515,
        algo = "dungeon.bsp",
        loot = "equipment",
        w = 22,
        h = 16,
        fov = {
            algorithm = "shadowcast",
            radius = 5
        },
        particles = ["torch_fire", "pickup_burst", "ember_trail", "mist_fog"],
        enable = {
            path = true,
            fov = true,
            light = true,
            pickup = true,
            particles = true,
            stash = true
        }
    },
    [16] = {
        key = "facing_cone",
        name = "朝向锥视野",
        seed = 1616,
        algo = "dungeon.bsp",
        loot = "starter",
        w = 24,
        h = 18,
        fov = {
            algorithm = "shadowcast",
            radius = 5,
            facingDeg = 0,
            halfAngle = 40
        },
        enable = {
            path = true,
            fov = true,
            light = true,
            pickup = true,
            particles = false,
            facing = true
        }
    },
    [17] = {
        key = "flow_cellcost",
        name = "Flow+代价绕路",
        seed = 1717,
        algo = "dungeon.bsp",
        loot = "starter",
        w = 28,
        h = 20,
        fov = {
            algorithm = "shadowcast",
            radius = 5
        },
        cellCost = {
            cost = 10.0,
            stripWidth = 5
        },
        swarmStarts = 3,
        enable = {
            path = true,
            fov = true,
            light = true,
            pickup = true,
            flow = true,
            cellcost = true
        }
    },
    [18] = {
        key = "seed_lock",
        name = "种子复现",
        seed = 4242,
        algo = "dungeon.bsp",
        loot = "starter",
        w = 24,
        h = 18,
        fov = {
            algorithm = "shadowcast",
            radius = 5
        },
        enable = {
            path = true,
            fov = true,
            light = false,
            pickup = false,
            particles = false
        }
    },
    [19] = {
        key = "corner_peek",
        name = "拐角窥视",
        seed = 1919,
        algo = "dungeon.bsp",
        loot = "cave",
        w = 22,
        h = 16,
        fov = {
            algorithm = "shadowcast",
            radius = 6,
            cornerPeek = true
        },
        enable = {
            path = true,
            fov = true,
            light = true,
            pickup = true,
            cornerpeek = true
        }
    },
    [20] = {
        key = "equipment_loot",
        name = "装备掉落",
        seed = 2020,
        algo = "dungeon.bsp",
        loot = "equipment",
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
    [21] = {
        key = "world_tile_pick",
        name = "世界拾格",
        seed = 2121,
        algo = "dungeon.bsp",
        loot = "starter",
        w = 22,
        h = 16,
        fov = {
            algorithm = "raycast",
            radius = 5
        },
        enable = {
            path = true,
            fov = true,
            light = true,
            pickup = true,
            camera = true
        },
        camera = {
            zoom = 1.35
        }
    },
    [22] = {
        key = "fov_memory",
        name = "探索记忆清除",
        seed = 2222,
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
            radius = 5
        },
        enable = {
            path = true,
            fov = true,
            light = true,
            pickup = true,
            mask = true
        }
    },
    [23] = {
        key = "group_path",
        name = "群体寻路",
        seed = 2323,
        algo = "dungeon.bsp",
        loot = "starter",
        w = 28,
        h = 20,
        fov = {
            algorithm = "shadowcast",
            radius = 5
        },
        swarmStarts = 5,
        enable = {
            path = true,
            fov = true,
            light = true,
            pickup = true,
            flow = true
        }
    },
    [24] = {
        key = "fov_algo_gallery",
        name = "FOV 算法画廊",
        seed = 2424,
        algo = "dungeon.bsp",
        loot = "starter",
        w = 22,
        h = 16,
        fov = {
            algorithm = "permissive",
            radius = 6
        },
        fovAlgorithms = ["shadowcast", "raycast", "permissive", "rectangle"],
        enable = {
            path = true,
            fov = true,
            light = true,
            pickup = false,
            mask = true
        }
    },
    [25] = {
        key = "dynamic_block",
        name = "动态阻挡",
        seed = 2525,
        algo = "dungeon.bsp",
        loot = "rich",
        w = 26,
        h = 18,
        fov = {
            algorithm = "shadowcast",
            radius = 5
        },
        cellCost = {
            cost = 9.0,
            stripWidth = 4
        },
        enable = {
            path = true,
            fov = true,
            light = true,
            pickup = true,
            cellcost = true
        }
    },
    [26] = {
        key = "drunkard_cave",
        name = "醉汉洞穴",
        seed = 2626,
        algo = "cave.drunkard",
        loot = "cave",
        w = 30,
        h = 22,
        params = {
            floorPct = 0.42
        },
        fov = {
            algorithm = "shadowcast",
            radius = 6
        },
        light = {
            type = "point",
            radius = 150,
            color = [0.7, 0.85, 1.0, 1.6]
        },
        enable = {
            path = true,
            fov = true,
            light = true,
            pickup = true,
            particles = true
        }
    },
    [27] = {
        key = "maze_hex",
        name = "六角迷宫",
        seed = 2727,
        algo = "maze.backtrack",
        loot = "starter",
        w = 28,
        h = 22,
        fov = {
            algorithm = "raycast",
            radius = 4
        },
        enable = {
            path = true,
            fov = true,
            light = true,
            pickup = true
        }
    },
    [28] = {
        key = "wfc_dungeon",
        name = "WFC 地牢",
        seed = 2828,
        algo = "wfc.simple",
        loot = "rich",
        w = 24,
        h = 18,
        params = {
            preset = "dungeon",
            maxAttempts = 64
        },
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
    [29] = {
        key = "mist_particles",
        name = "迷雾粒子",
        seed = 2929,
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
            radius = 110,
            color = [0.6, 0.7, 0.95, 1.4]
        },
        particles = ["mist_fog", "torch_fire", "ember_trail"],
        enable = {
            path = true,
            fov = true,
            light = true,
            pickup = true,
            particles = true
        }
    },
    [30] = {
        key = "raid_combo",
        name = "突袭综合",
        seed = 3030,
        algo = "dungeon.bsp",
        loot = "raid",
        w = 32,
        h = 24,
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
            stripWidth = 3
        },
        swarmStarts = 4,
        particles = ["torch_fire", "pickup_burst", "ember_trail", "mist_fog"],
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
    }
};

LEVEL_IDS <- [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30];

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

