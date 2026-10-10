# ---------------------------------------------------------------------------
# Host package: pixelworld
# ---------------------------------------------------------------------------

# L4 -- pixelworld

eve_declare_module(NAME pixelworld_graphics LAYER 4 SCRIPT PixelWorldGraphics SLOT pixelworldGraphics
                   DEPS graphics pixelworld
                   GROUP 2d 3d web)

# L5 -- pixelworld
eve_declare_module(NAME pixelworld_physics LAYER 5 SCRIPT PixelWorldPhysics SLOT pixelworldPhysics
                   DEPS pixelworld physics
                   GROUP 2d web)

# L6 -- pixelworld
eve_declare_module(NAME pixelworld_editor LAYER 6 SCRIPT PixelWorldEditorModule SLOT pixelworldEditor
                   DEPS pixelworld ui
                   GROUP 2d 3d web)
