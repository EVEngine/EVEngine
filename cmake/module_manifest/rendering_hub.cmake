# ---------------------------------------------------------------------------
# L3 -- the rendering hub
# ---------------------------------------------------------------------------

# Public graphics text/atlas APIs call FontData metrics directly; font is a real runtime dependency.
eve_declare_module(NAME graphics REQUIRED LAYER 3 SCRIPT Graphics SLOT gfx
                   DEPS data filesystem image thread font
                   THIRDPARTY sdl2 assimp)
