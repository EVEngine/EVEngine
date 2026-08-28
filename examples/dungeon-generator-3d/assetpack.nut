// Renderer-side adapter. Replace this table to use another modular pack.
dungeonAssets <- {
    root = "assets/kaykit-dungeon/obj/",
    extension = ".obj",
    floor = "floor_tile_large",
    // Some packs author floor albedo in very dark atlas regions. Keep the
    // neutral slab fallback by default; set true when a replacement pack's
    // floor material is suitable for direct rendering.
    usePackFloors = false,
    wall = "wall",
    walls = ["wall", "wall", "wall", "wall_cracked", "wall_arched", "wall_pillar"],
    windows = ["wall_window_closed", "wall_window_open", "wall_archedwindow_open",
               "wall_archedwindow_gated"],
    brokenWalls = ["wall_broken", "wall_cracked"],
    scaffoldWalls = ["wall_scaffold", "wall_open_scaffold",
                     "wall_window_closed_scaffold", "wall_window_open_scaffold"],
    gatedWalls = ["wall_gated", "wall_arched"],
    wallVariantRates = { window=14, broken=8, scaffold=5, gated=4 },
    corners = ["wall_corner_small", "wall_corner_small", "wall_corner_small", "wall_corner_gated"],
    doorways = ["wall_doorway", "wall_doorway", "wall_doorway_sides", "wall_doorway_door"],
    stairs = "stairs_walled",
    stairsVerticalOffset = -3.0,
    details = ["coin_stack_small", "plate_small", "bottle_A_brown"],
    roles = {
        column="pillar_decorated", container="barrel_large_decorated",
        treasure="chest_gold", table="table_medium_decorated_A", seating="chair",
        bed="bed_decorated", shelf="shelf_large", light="torch_mounted",
        banner="banner_patternA_blue", weapon="sword_shield", trap="spikes",
        food="plate_food_A", tavern="keg_decorated", clutter="rubble_half"
    }
};

// Parameter-side KayKit preset. The generator itself remains pack-neutral.
dofile("../roguelike-generator/assetpacks/kaykit_dungeon.nut");
