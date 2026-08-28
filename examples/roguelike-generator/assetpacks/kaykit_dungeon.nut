// Optional KayKit Dungeon Pack preset. The procgen algorithm knows only semantic
// roles; this file is the replaceable adapter that maps those roles to model ids.
// Values are comma-separated pools so adding renamed, EXTRA, or custom variants
// requires no C++ change.

function configureDungeonAssetPack(p) {
    p.setString("assetPack", "kaykit-dungeon-1.1");

    // Modular architecture (including the 1.1 ceiling/stair extension slots).
    p.setString("assets.wall", "wall,wall_arched,wall_cracked,wall_gated,wall_pillar,wall_sloped");
    p.setString("assets.wallCorner", "wall_corner,wall_corner_small,wall_corner_gated");
    p.setString("assets.wallJunction", "wall_Tsplit,wall_Tsplit_sloped,wall_crossing,wall_endcap");
    p.setString("assets.wallDoor", "wall_doorway,wall_doorway_door,wall_doorway_sides,wall_doorway_Tsplit");
    p.setString("assets.wallWindow", "wall_window_open,wall_window_closed,wall_archedwindow_open,wall_archedwindow_gated");
    p.setString("assets.wallHalf", "wall_half,wall_half_endcap,wall_half_endcap_sloped");
    p.setString("assets.wallBroken", "wall_broken");
    p.setString("assets.wallScaffold", "wall_scaffold,wall_open_scaffold,wall_corner_scaffold,wall_doorway_scaffold,wall_doorway_scaffold_door,wall_window_open_scaffold,wall_window_closed_scaffold,wall_archedwindow_gated_scaffold");
    p.setString("assets.floor", "floor_tile_small,floor_tile_large,floor_tile_small_decorated,floor_tile_large_rocks");
    p.setString("assets.floorBroken", "floor_tile_small_broken_A,floor_tile_small_broken_B,floor_tile_small_corner,floor_tile_small_weeds_A,floor_tile_small_weeds_B");
    p.setString("assets.floorDirt", "floor_dirt_large,floor_dirt_large_rocky,floor_dirt_small_A,floor_dirt_small_B,floor_dirt_small_C,floor_dirt_small_D,floor_dirt_small_corner,floor_dirt_small_weeds");
    p.setString("assets.floorWood", "floor_wood_small,floor_wood_small_dark,floor_wood_large,floor_wood_large_dark");
    p.setString("assets.floorGrate", "floor_tile_grate,floor_tile_grate_open,floor_tile_big_grate,floor_tile_big_grate_open,floor_tile_extralarge_grates,floor_tile_extralarge_grates_open,floor_tile_big_spikes");
    p.setString("assets.floorFoundation", "floor_foundation_allsides,floor_foundation_corner,floor_foundation_diagonal_corner,floor_foundation_front,floor_foundation_front_and_back,floor_foundation_front_and_sides");
    p.setString("assets.ceiling", "ceiling");
    p.setString("assets.stairs", "stairs,stairs_narrow,stairs_wide,stairs_walled,stairs_wood,stairs_wood_decorated");
    p.setString("assets.stairsRail", "stairs_wall_left,stairs_wall_right");
    p.setString("assets.door", "wall_doorway_door,wall_doorway_scaffold_door");
    p.setString("assets.barrier", "barrier,barrier_half,barrier_corner,barrier_column,barrier_colum_half");

    // Freestanding, wall-mounted and tabletop props.
    p.setString("assets.column", "column,pillar,pillar_decorated");
    p.setString("assets.container", "barrel_large_decorated,barrel_large_decorated,barrel_large_decorated,barrel_large,barrel_large,barrel_small_stack,barrel_small_stack,crates_stacked,crates_stacked,crates_stacked,crates_stacked,box_stacked,box_stacked,trunk_large_A,trunk_large_A,trunk_large_B,trunk_large_B,trunk_large_C,trunk_large_C,barrel_small,box_large,box_small,box_small_decorated,trunk_medium_A,trunk_medium_B,trunk_medium_C,trunk_small_A,trunk_small_B,trunk_small_C");
    p.setString("assets.treasure", "chest,chest_gold,chest_lid,chest_gold_lid,coin,coin_stack_small,coin_stack_medium,coin_stack_large,key,keyring,keyring_hanging");
    p.setString("assets.table", "table_medium_decorated_A,table_medium_decorated_A,table_long_decorated_A,table_long_decorated_C,table_long_tablecloth_decorated_A,table_small,table_small_decorated_A,table_small_decorated_B,table_medium,table_medium_broken,table_medium_tablecloth,table_medium_tablecloth_decorated_B,table_long,table_long_broken,table_long_tablecloth");
    p.setString("assets.seating", "chair,stool");
    p.setString("assets.bed", "bed_decorated,bed_decorated,bed_floor,bed_frame");
    p.setString("assets.shelf", "shelf_small,shelf_small_candles,shelf_large,shelves,wall_shelves");
    p.setString("assets.light", "candle,candle_lit,candle_melted,candle_thin,candle_thin_lit,candle_triple,torch,torch_lit,torch_mounted");
    p.setString("assets.wallShelf", "shelf_small,shelf_small_candles,wall_shelves");
    p.setString("assets.wallLight", "torch_mounted");
    p.setString("assets.banner", "banner_blue,banner_brown,banner_green,banner_red,banner_white,banner_yellow,banner_patternA_blue,banner_patternA_brown,banner_patternA_green,banner_patternA_red,banner_patternA_white,banner_patternA_yellow,banner_patternB_blue,banner_patternB_brown,banner_patternB_green,banner_patternB_red,banner_patternB_white,banner_patternB_yellow,banner_patternC_blue,banner_patternC_brown,banner_patternC_green,banner_patternC_red,banner_patternC_white,banner_patternC_yellow,banner_shield_blue,banner_shield_brown,banner_shield_green,banner_shield_red,banner_shield_white,banner_shield_yellow,banner_thin_blue,banner_thin_brown,banner_thin_green,banner_thin_red,banner_thin_white,banner_thin_yellow,banner_triple_blue,banner_triple_brown,banner_triple_green,banner_triple_red,banner_triple_white,banner_triple_yellow");
    p.setString("assets.weapon", "sword_shield,sword_shield_broken,sword_shield_gold");
    p.setString("assets.trap", "spikes,floor_tile_big_spikes");
    p.setString("assets.food", "plate,plate_small,plate_stack,plate_food_A,plate_food_B,bottle_A_brown,bottle_A_green,bottle_A_labeled_brown,bottle_A_labeled_green,bottle_B_brown,bottle_B_green,bottle_C_brown,bottle_C_green");
    p.setString("assets.tavern", "keg,keg_decorated,barrel_large_decorated,table_long_tablecloth_decorated_A");
    p.setString("assets.clutter", "rubble_half,rubble_large,coin,plate_small,bottle_A_brown");
}
