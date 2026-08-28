// Renderer-side adapter. Replace this table to use another modular pack.
dungeonAssets <- {
    root = "assets/kaykit-dungeon/obj/",
    extension = ".obj",
    floor = "floor_tile_large",
    wall = "wall",
    // Repetition weights the common straight wall more heavily.
    walls = ["wall", "wall", "wall", "wall", "wall", "wall",
             "wall_cracked", "wall", "wall_window_closed", "wall", "wall_broken", "wall"],
    stairs = "stairs",
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
