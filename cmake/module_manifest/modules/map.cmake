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

eve_declare_module(NAME map_editor LAYER 7 DEPS editor map_editing GROUP 2d 3d web)
