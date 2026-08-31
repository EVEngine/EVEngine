# ---------------------------------------------------------------------------
# L3 -- the rendering hub
# ---------------------------------------------------------------------------

# graphics/Font.cpp is the bridge from graphics into the optional font module.
eve_declare_module(NAME graphics REQUIRED LAYER 3 SCRIPT Graphics SLOT gfx
                   DEPS data filesystem image thread
                   OPTIONAL_DEPS font
                   THIRDPARTY sdl2 assimp)
