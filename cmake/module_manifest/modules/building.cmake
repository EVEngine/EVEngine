# ---------------------------------------------------------------------------
# Host package: building
# ---------------------------------------------------------------------------

# L4 -- building
eve_declare_module(NAME buildingfx LIB EVBuildingFx LAYER 4 SCRIPT BuildingFx SLOT buildingfx
                   DEPS building graphics)

# L5 -- building
# BuildingTarget.cpp is the only TU; without building the module would be empty.
# building itself is off on web (Poco), so this satellite is not in GROUP web.
eve_declare_module(NAME building_editing LAYER 5
                   DEPS building editing
                   GROUP 2d 3d)

# L7 -- building
# building_editing requires building (Poco); keep off web/WASM with building.
eve_declare_module(NAME building_editor LAYER 7 DEPS building_editing editor GROUP 2d 3d)
