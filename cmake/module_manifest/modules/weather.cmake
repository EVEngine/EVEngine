# ---------------------------------------------------------------------------
# Host package: weather
# ---------------------------------------------------------------------------

# L6 -- weather
# Precipitation (Weather) only needs graphics. Interactive Snow applies to
# procgen heightmaps and is an OPTIONAL_DEPS bridge: when procgen is off
# (web/WASM, or an explicit trim), Snow.cpp is excluded and the Snow script
# slot is dropped in src/modules/CMakeLists.txt so the profile does not pull
# procgen -> map (map JSON/TSX no longer pulls Poco).
eve_declare_module(NAME weather LAYER 6 SCRIPT Weather Snow SLOT weather snow
                   DEPS graphics
                   OPTIONAL_DEPS procgen
                   GROUP 3d web)
