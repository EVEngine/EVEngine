# ---------------------------------------------------------------------------
# Host package: procgen
# ---------------------------------------------------------------------------

# L5 -- procgen
eve_declare_module(NAME procgen LAYER 5 SCRIPT Procgen SLOT procgen
                   DEPS data gpgpu graphics image map model3d transaction
                   OPTIONAL_DEPS hexmap
                   GROUP 3d)

# L7 -- procgen
eve_declare_module(NAME heightmap_target DIR procgen/heightmap_target LAYER 7
                   SCRIPT HeightmapTargetModule SLOT heightmapTarget
                   DEPS editor procgen procgen_editing procgen_graphics_editing
                   GROUP 3d)

eve_declare_module(NAME biome_editor LAYER 7 DEPS biome_editing editor procgen
                   SCRIPT BiomeEditorModule SLOT biomeEditor GROUP 3d)

eve_declare_module(NAME procgen_editor LAYER 7 DEPS domain_gizmo_editor editor procgen procgen_editing
                   SCRIPT ProcgenEditorModule SLOT procgenEditor GROUP 3d)

# L6 -- procgen
eve_declare_module(NAME biome_editing LAYER 6
                   DEPS editing
                   OPTIONAL_DEPS procgen
                   GROUP 3d)

eve_declare_module(NAME procgen_editing LAYER 6
                   DEPS editing image
                   OPTIONAL_DEPS procgen
                   GROUP 3d)

eve_declare_module(NAME procgen_graphics_editing LAYER 6
                   DEPS editing graphics
                   OPTIONAL_DEPS procgen
                   GROUP 3d)

eve_declare_module(NAME procgen_physics LAYER 6 SCRIPT ProcgenPhysics SLOT procgenPhysics
                   DEPS physics procgen
                   GROUP 3d)

eve_declare_module(NAME procgen_animation DIR procgen/animation LAYER 6
                   SCRIPT ProcgenAnimation SLOT procgenAnimation
                   DEPS animation procgen
                   GROUP 3d)
