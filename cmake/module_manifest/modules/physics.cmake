# ---------------------------------------------------------------------------
# Host package: physics
# ---------------------------------------------------------------------------

# L3 -- pure owning baked tracks and asset publication; no simulation or graphics provider.
eve_declare_module(NAME physics_bake DIR physics/bake LIB EVPhysicsBake LAYER 3
                   GROUP 2d 3d web)

# L3 -- persistent collapse geometry/poses; live source queries are injected through a narrow protocol.
eve_declare_module(NAME physics_bake_collapse DIR physics/bake/collapse LIB EVPhysicsBakeCollapse LAYER 3
                   DEPS physics_bake GROUP 3d web)

# L4 -- optional VM projection; the pure baked runtime keeps no script dependency.
eve_declare_module(NAME physics_bake_script DIR physics/bake/script LIB EVPhysicsBakeScript LAYER 4
                   SCRIPT PhysicsBake SLOT physicsBake DEPS physics_bake
                   GROUP 2d 3d web)

# L5 -- recording adapter may be trimmed while pure baked tracks remain available.
eve_declare_module(NAME physics_bake_capture DIR physics/bake/capture LIB EVPhysicsBakeCapture LAYER 5
                   DEPS physics physics_bake
                   GROUP 2d 3d web)

# L4 -- baked pose presentation without a simulation dependency.
eve_declare_module(NAME physics_bake_graphics DIR physics/bake/graphics LIB EVPhysicsBakeGraphics LAYER 4
                   DEPS graphics physics_bake
                   GROUP 2d 3d web)

# L4 -- physics

eve_declare_module(NAME physics_softbody_graphics DIR physics/softbody/graphics LAYER 4
                   DEPS graphics physics_softbody
                   GROUP 3d web)

eve_declare_module(NAME physics_softbody_cook DIR physics/softbody/cook LAYER 4
                   DEPS asset physics_softbody
                   GROUP 3d web)

# The public Physics facade retains its interactive presentation dependencies;
# src/modules/CMakeLists.txt separately compiles the domain core (World/Body/
# Shape/Joint/query/fixed-step) without those dependencies for core profiles.
eve_declare_module(NAME physics LAYER 4 SCRIPT Physics SLOT physics
                   DEPS physics_backend physics_softbody physics_softbody_graphics platform_event graphics gpgpu sensing
                   OPTIONAL_DEPS scene
                   THIRDPARTY box2d box3d jolt
                   GROUP 2d 3d web)

# L5 -- physics
# Cloth is a host-owned physics satellite: rigid-body physics stays usable in
# trimmed builds without cloth topology, rendering, or compute backends. Its
# accelerator provider registers independently and cannot replace the fluids provider.
eve_declare_module(NAME physics_cloth DIR physics/cloth LAYER 5 SCRIPT Cloth SLOT cloth
                   DEPS physics graphics gpgpu
                   GROUP 2d 3d web)

eve_declare_module(NAME physics_rope DIR physics/rope LIB EVPhysicsRope LAYER 5
                   SCRIPT Rope SLOT rope
                   DEPS physics schema
                   GROUP 3d web)

# Optional Chaos-style geometry-collection destruction (pre-fracture connection
# graph + runtime fields). Depends on physics World3D/Body3D; LAYER 5 matches
# rope / pixelworld_physics. Design originally sketched L3, but Body3D binding
# requires the physics facade.
eve_declare_module(NAME physics_destruction DIR physics/destruction LIB EVPhysicsDestruction
                   LAYER 5 SCRIPT Destruction SLOT destruction
                   DEPS physics schema physics_bake physics_bake_collapse
                   GROUP 3d web)

# Offline fracture cook for geometry collections. Same layer as the host is
# allowed; nested DIR physics/destruction/cook is excluded from the host scan.
eve_declare_module(NAME physics_destruction_cook DIR physics/destruction/cook
                   LIB EVPhysicsDestruction_cook LAYER 5
                   SCRIPT DestructionCook SLOT destructionCook
                   DEPS asset physics_destruction
                   GROUP 3d web)

eve_declare_module(NAME physics_destruction_graphics DIR physics/destruction/graphics
                   LIB EVPhysicsDestruction_graphics LAYER 5
                   SCRIPT DestructionFx SLOT destructionFx
                   DEPS graphics physics_destruction
                   GROUP 3d web)

eve_declare_module(NAME physics_destruction_editing DIR physics/destruction/editing
                   LIB EVPhysicsDestruction_editing LAYER 5
                   DEPS editing physics_destruction
                   GROUP 3d web)

# Optional Action adapter for generation-safe 3D body-pair collision windows.
eve_declare_module(NAME physics_action DIR physics/action LAYER 5
                   DEPS action physics
                   GROUP 3d web)

# Bone-bound continuous trajectory sweeps (UE5 Physics Asset / anim-notify style).
# Samples IAttachmentPointSource and casts through World3D; no animation include.
eve_declare_module(NAME physics_trajectory DIR physics/trajectory LAYER 5
                   DEPS physics
                   GROUP 3d web)

# Optional editing satellite. Runtime-only profiles can enable physics without
# pulling editing/editor contracts or AssetDB adapters.
eve_declare_module(NAME physics_editing LAYER 5
                   DEPS editing physics
                   GROUP 3d web)

eve_declare_module(NAME physics_softbody_editing DIR physics/softbody/editing LAYER 5
                   DEPS editing physics_softbody
                   GROUP 3d web)

# Optional offline gas cache and depth-aware destruction smoke.
eve_declare_module(NAME physics_destruction_dust DIR physics/destruction/fx
                   LAYER 5 SCRIPT DestructionDust SLOT destructionDust
                   DEPS fluids graphics physics_destruction physics_bake
                   GROUP 3d web)

# L7 -- physics
eve_declare_module(NAME physics_editor LAYER 7 DEPS asset editor physics_editing
                   SCRIPT PhysicsEditorModule SLOT physicsEditor GROUP 3d web)

# L6 -- optional deformation capture; playback can exclude cloth and rope solvers.
eve_declare_module(NAME physics_bake_deform_capture DIR physics/bake/deform_capture LIB EVPhysicsBakeDeformCapture LAYER 6
                   DEPS physics_bake physics_cloth physics_rope
                   GROUP 3d web)

# L6 -- CPU particle journal conversion; pure playback never depends on live particles.
eve_declare_module(NAME physics_bake_particle_capture DIR physics/bake/particle_capture LIB EVPhysicsBakeParticleCapture LAYER 6
                   DEPS physics_bake particles
                   GROUP 2d 3d)

# L6 -- VM authoring/capture adapter; pure runtime and playback VM remain independently selectable.
eve_declare_module(NAME physics_bake_recording_script DIR physics/bake/recording_script
                   LIB EVPhysicsBakeRecordingScript LAYER 6 SCRIPT PhysicsBakeRecording SLOT physicsBakeRecording
                   DEPS physics_bake physics_bake_script physics_bake_capture physics_bake_deform_capture physics physics_cloth physics_rope
                   GROUP 3d web)

# L6 -- CPU particle recording VM; independent of cloth/rope and rigid capture.
eve_declare_module(NAME physics_bake_particle_script DIR physics/bake/particle_script
                   LIB EVPhysicsBakeParticleScript LAYER 6 SCRIPT ParticleBakeRecording SLOT particleBakeRecording
                   DEPS physics_bake physics_bake_script physics_bake_particle_capture particles
                   GROUP 2d 3d)

# L4 -- optional VM presentation bridge; native playback and native graphics adapters remain independent.
eve_declare_module(NAME physics_bake_graphics_script DIR physics/bake/graphics_script
                   LIB EVPhysicsBakeGraphicsScript LAYER 4 SCRIPT PhysicsBakeGraphics SLOT physicsBakeGraphics
                   DEPS graphics physics_bake physics_bake_graphics physics_bake_script
                   GROUP 2d 3d web)
