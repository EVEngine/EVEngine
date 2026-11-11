# ---------------------------------------------------------------------------
# Host package: map
# ---------------------------------------------------------------------------

# L4 -- map
eve_declare_module(NAME map LAYER 4 SCRIPT Map SLOT map
                   DEPS data filesystem graphics grid
                   GROUP 2d 3d)

eve_declare_module(NAME map_editing LAYER 4
                   DEPS editing
                   OPTIONAL_DEPS map
                   GROUP 3d web)

# L5 -- map
eve_declare_module(NAME level_editing LAYER 5
                   DEPS editing
                   GROUP 2d 3d web)

# L7 -- map
eve_declare_module(NAME tilelayer_target DIR map/tilelayer_target LAYER 7
                   SCRIPT TileLayerTargetModule SLOT tileLayerTarget
                   DEPS editor map map_editing
                   GROUP 3d)

eve_declare_module(NAME level_editor LAYER 7 DEPS editor level_editing
                   SCRIPT LevelEditorModule SLOT levelEditor GROUP 2d 3d web)


# L4 -- map-owned transactional spawn asset composition; runtime only.
eve_declare_module(NAME map_spawn DIR map/spawn LAYER 4
                   DEPS map spawn GROUP 2d 3d)

# L4 -- explicit RPG/map presentation composition; neither domain depends on it.
eve_declare_module(NAME map_rpg_spawn LIB EVMapRpgSpawn DIR map/rpg_spawn LAYER 4
                   SCRIPT MapRPGSpawn SLOT mapRpgSpawn
                   DEPS map_spawn rpg rpg_spawn spawn_definitions graphics GROUP 2d 3d)

# L6 -- game-owned RTS/map composition; neither lower runtime depends on it.
eve_declare_module(NAME map_rts_spawn LIB EVMapRTSSpawn DIR map/rts_spawn LAYER 6
                   SCRIPT MapRTSSpawn SLOT mapRtsSpawn
                   DEPS map map_spawn rts_spawn rts spawn spawn_definitions definitions crowd action combat sensing GROUP 2d 3d)

# L4 -- UI-neutral spawn sidecar authoring; does not depend on map/graphics runtime.
eve_declare_module(NAME spawn_editing DIR map/spawn/editing LAYER 4
                   DEPS editing spawn GROUP 2d 3d web)

# L7 -- owning property forms and explicit map-aware viewport composition.
eve_declare_module(NAME spawn_editor DIR map/spawn/editor LAYER 7
                   DEPS editing editor property_access map map_spawn spawn spawn_editing ui filesystem graphics GROUP 2d 3d web
                   THIRDPARTY sdl2)

# L7 -- RPG content adapter and launchable native spawn authoring composition.
eve_declare_module(NAME map_rpg_spawn_editor LIB EVMapRpgSpawnEditor DIR map/rpg_spawn/editor LAYER 7
                   SCRIPT RPGSpawnEditor SLOT rpgSpawnEditor
                   DEPS spawn_editor spawn spawn_definitions definitions rpg rpg_spawn map map_spawn map_rpg_spawn editor property_access ui graphics filesystem window platform_event
                   GROUP 2d 3d THIRDPARTY sdl2)
