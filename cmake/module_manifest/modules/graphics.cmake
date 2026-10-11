# ---------------------------------------------------------------------------
# Host package: graphics
# ---------------------------------------------------------------------------

# L4 -- graphics

eve_declare_module(NAME material_graphics_editing LAYER 4
                   DEPS graphics material_editing
                   GROUP 3d web)

eve_declare_module(NAME graphics_editing LAYER 4
                   DEPS editing graphics image
                   GROUP 2d 3d web)

# Optional Vulkan KHR ray-tracing satellite (BLAS/TLAS + reflections). Soft-fails
# when the GPU lacks the extensions; excluded from web / 2d / minimal profiles.
eve_declare_module(NAME graphics_raytracing DIR graphics/raytracing LAYER 4
                   SCRIPT RayTracing SLOT rayTracing
                   DEPS graphics
                   GROUP 3d)

# Realtime fog satellite: MAC transport, SceneWind, analytic interactors, world-space
# ray march, froxel mapping, Beer cache, analytic volumetric lights and art layer.
eve_declare_module(NAME graphics_fog DIR graphics/fog LAYER 4
                   LIB EVGraphicsFog SCRIPT RealtimeFog SLOT realtimeFog
                   DEPS graphics
                   GROUP 3d web)

# L7 -- graphics
eve_declare_module(NAME graphics_editor LAYER 7 SCRIPT GraphicsEditorModule SLOT graphicsEditor
                   DEPS editor graphics graphics_editing GROUP 3d web)

eve_declare_module(NAME lighting_editor LAYER 7 DEPS editor lighting_editing GROUP 3d web)

eve_declare_module(NAME material_editor LAYER 7 DEPS editor graphics_editor material_editing
                   SCRIPT MaterialEditorModule SLOT materialEditor GROUP 3d web)

# L6 -- graphics
eve_declare_module(NAME lighting_editing LAYER 6
                   DEPS daynight editing graphics weather
                   GROUP 3d web)
