# ---------------------------------------------------------------------------
# L2 -- input state and playback
# ---------------------------------------------------------------------------

eve_declare_module(NAME scene_editing LAYER 2
                   DEPS editing scene
                   GROUP 3d web)
eve_declare_module(NAME material_editing LAYER 2
                   DEPS editing
                   GROUP 3d web)
eve_declare_module(NAME audio_editing LAYER 2
                   DEPS editing
                   GROUP 3d web)
eve_declare_module(NAME map_editing LAYER 2
                   DEPS editing map
                   GROUP 3d web)
eve_declare_module(NAME input_editing LAYER 2
                   DEPS editing
                   GROUP 2d 3d web)
eve_declare_module(NAME definitions_editing LAYER 2
                   DEPS definitions editing schema
                   GROUP 2d 3d web)
eve_declare_module(NAME crowd_editing LAYER 2
                   DEPS crowd editing
                   GROUP 2d 3d web)
eve_declare_module(NAME social_editing LAYER 2
                   DEPS editing social
                   GROUP 2d 3d web)
eve_declare_module(NAME network_editing LAYER 2
                   DEPS editing network
                   GROUP 2d 3d web)
eve_declare_module(NAME profiler_editing LAYER 2
                   DEPS editing profiler
                   GROUP 2d 3d web)
eve_declare_module(NAME queue_editing LAYER 2
                   DEPS editing orders production
                   GROUP 2d 3d web)
eve_declare_module(NAME npc_ai_editing LAYER 2
                   DEPS editing npc_ai
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
