# ---------------------------------------------------------------------------
# L3 -- the rendering hub
# ---------------------------------------------------------------------------

# graphics/Font.cpp is the bridge from graphics into the optional font module.
eve_declare_module(NAME graphics REQUIRED LAYER 3 SCRIPT Graphics SLOT gfx
                   DEPS data filesystem image thread
                   OPTIONAL_DEPS font
                   THIRDPARTY sdl2 assimp)

# Optional Vulkan KHR ray-tracing satellite (BLAS/TLAS + reflections). Soft-fails
# when the GPU lacks the extensions; excluded from web / 2d / minimal profiles.
eve_declare_module(NAME graphics_raytracing DIR graphics/raytracing LAYER 4
                   SCRIPT RayTracing SLOT rayTracing
                   DEPS graphics
                   GROUP 3d)
