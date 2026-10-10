# ---------------------------------------------------------------------------
# Host package: physics
# ---------------------------------------------------------------------------

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
                   THIRDPARTY box2d box3d
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
                   DEPS physics schema
                   GROUP 3d web)

# Offline fracture cook for geometry collections. Same layer as the host is
# allowed; nested DIR physics/destruction/cook is excluded from the host scan.
eve_declare_module(NAME physics_destruction_cook DIR physics/destruction/cook
                   LIB EVPhysicsDestruction_cook LAYER 5
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

# L7 -- physics
eve_declare_module(NAME physics_editor LAYER 7 DEPS asset editor physics_editing
                   SCRIPT PhysicsEditorModule SLOT physicsEditor GROUP 3d web)
