# ---------------------------------------------------------------------------
# Host package: particles
# ---------------------------------------------------------------------------

# L5 -- particles
# Bone/skin attach uses animation runtime (Assimp-free). Animation's Assimp
# import is OPTIONAL_DEPS model3d. Web membership still waits on animation's
# Spine JSON (Poco) path. Config/effect JSON already uses common/Json.
eve_declare_module(NAME particles LAYER 5 SCRIPT Particles SLOT particles
                   DEPS action animation data filesystem graphics ik stylize physics_backend
                   GROUP 2d 3d)

# L6 -- particles
eve_declare_module(NAME particles_editing LAYER 6
                   DEPS editing
                   OPTIONAL_DEPS particles
                   GROUP 3d)

eve_declare_module(NAME particles_graphics_editing LAYER 6
                   DEPS graphics_editing particles_editing
                   OPTIONAL_DEPS particles
                   GROUP 2d 3d)
