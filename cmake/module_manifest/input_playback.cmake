# ---------------------------------------------------------------------------
# L2 -- input state and playback
# ---------------------------------------------------------------------------

eve_declare_module(NAME scene_editing LAYER 2
                   DEPS editing
                   GROUP 3d web)
eve_declare_module(NAME material_editing LAYER 2
                   DEPS editing
                   GROUP 3d web)

# Canonical runtime scene-template decoder; mounting remains an explicit Scene operation.
eve_declare_module(NAME asset_scene LAYER 2
                   DEPS asset scene
                   GROUP 3d web)
eve_declare_module(NAME keyboard LAYER 2 SCRIPT Keyboard SLOT keyboard
                   DEPS platform_event window
                   THIRDPARTY sdl2
                   GROUP minimal 2d 3d web)
eve_declare_module(NAME mouse LAYER 2 SCRIPT Mouse SLOT mouse
                   DEPS window
                   THIRDPARTY sdl2
                   GROUP minimal 2d 3d web)
eve_declare_module(NAME touch LAYER 2 SCRIPT Touch SLOT touch
                   DEPS platform_event window
                   THIRDPARTY sdl2
                   GROUP minimal 2d 3d web)
eve_declare_module(NAME audio LAYER 2 SCRIPT Audio SLOT audio
                   DEPS platform_event sound
                   OPTIONAL_DEPS scene
                   THIRDPARTY openal
                   GROUP 2d 3d)
eve_declare_module(NAME font LAYER 2 SCRIPT Font SLOT font
                   DEPS filesystem image
                   THIRDPARTY freetype
                   GROUP 2d 3d web)
