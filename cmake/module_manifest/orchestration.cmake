# ---------------------------------------------------------------------------
# L5 / L6 -- aggregates and orchestration
# ---------------------------------------------------------------------------

# Renderables, bodies and audio sources attach through registered link kinds
# (scene/SceneLink.h), so scene no longer depends on those modules. The two
# picking entry points that take a Camera3D are implemented in the graphics
# module (graphics/ScenePicking.cpp, excluded when scene is off).
# L1 -- scene graph protocol
eve_declare_module(NAME scene LAYER 1 SCRIPT Scene SLOT scene
                   DEPS spatial
                   THIRDPARTY poco
                   GROUP 3d web)
# L5 -- aggregates (continued)
eve_declare_module(NAME animation_editing LAYER 5
                   DEPS editing
                   OPTIONAL_DEPS animation
                   GROUP 3d web)
eve_declare_module(NAME ui_editing LAYER 5
                   DEPS editing ui
                   GROUP 3d web)
eve_declare_module(NAME decal_editing LAYER 5
                   DEPS decal editing
                   GROUP 3d web)
eve_declare_module(NAME spritestack_editing LAYER 5
                   DEPS editing
                   OPTIONAL_DEPS spritestack
                   GROUP 2d 3d)
eve_declare_module(NAME stylize_editing LAYER 5
                   DEPS editing stylize
                   GROUP 3d web)
eve_declare_module(NAME housegen_editing LAYER 5
                   DEPS editing
                   OPTIONAL_DEPS housegen
                   GROUP 3d)
eve_declare_module(NAME camera_editing LAYER 5
                   DEPS camera editing
                   GROUP 3d web)
eve_declare_module(NAME lighting_editing LAYER 5
                   DEPS daynight editing graphics weather
                   GROUP 3d web)
eve_declare_module(NAME building_editing LAYER 5
                   DEPS editing
                   OPTIONAL_DEPS building
                   GROUP 2d 3d web)
eve_declare_module(NAME level_editing LAYER 5
                   DEPS editing
                   GROUP 2d 3d web)
eve_declare_module(NAME particles LAYER 5 SCRIPT Particles SLOT particles
                   DEPS animation data filesystem graphics ik
                   THIRDPARTY poco
                   GROUP 2d 3d)
# Surface fluid simulation: particles constrained to mesh SDFs (flow down
# surfaces, droplet coalescence) with screen-space surface reconstruction.
eve_declare_module(NAME fluids LAYER 5 SCRIPT Fluids SLOT fluids
                   DEPS gpgpu graphics physics
                   GROUP 3d web)
eve_declare_module(NAME procgen LAYER 5 SCRIPT Procgen SLOT procgen
                   DEPS gpgpu graphics image map transaction
                   GROUP 3d)
# L5 -- RTS domain composition profile. Provider modules remain behind typed
# links; these are the direct implementation dependencies of the profile.
eve_declare_module(NAME rts LAYER 5 SCRIPT RTS SLOT rts
                   DEPS action attributes combat crowd definitions economy effects map orders production sensing transaction weapon
                   GROUP 2d 3d)
# L5 -- deterministic turn/grid tactics composition profile. Shared gameplay
# providers are introduced by adapters as their implementation slices land;
# the phase-one board/turn core depends only on common engine contracts.
eve_declare_module(NAME tactics LAYER 5 SCRIPT Tactics SLOT tactics
                   DEPS action
                   GROUP 2d 3d web)
eve_declare_module(NAME avatar LAYER 5 SCRIPT Avatar SLOT avatar
                   DEPS animation graphics inventory model3d scene
                   GROUP 3d)
# Deterministic climbing/parkour planning and capsule-constrained execution.
eve_declare_module(NAME climbing LAYER 5 SCRIPT Climbing SLOT climbing
                   DEPS animation physics
                   GROUP 3d)
eve_declare_module(NAME tensor LAYER 5 LIB EVTensor SCRIPT TF SLOT tf
                   DEPS gpgpu
                   GROUP 3d web)
eve_declare_module(NAME virtualgeometry LIB EVVirtualGeometry LAYER 5 SCRIPT VirtualGeometry
                   DEPS data gpgpu graphics
                   GROUP 3d)
# HD-2D: extrudes a 2D map::TileLayer into a 3D terrain mesh (TileMap3D) and
# renders 2D sprite sheets / characters as camera-facing 3D billboards
# (Sprite3D) via the ECS Renderable3D forward path.
eve_declare_module(NAME hd2d LIB EVHd2D LAYER 5 SCRIPT Hd2D SLOT hd2d
                   DEPS graphics map
                   GROUP 3d)
# L6 -- orchestration
eve_declare_module(NAME sceneloader_editing LAYER 6
                   DEPS editing
                   OPTIONAL_DEPS sceneloader
                   GROUP 3d web)
eve_declare_module(NAME voxel_editing LAYER 6
                   DEPS editing
                   OPTIONAL_DEPS voxel
                   GROUP 3d)
# L7 -- domain editor adapters
eve_declare_module(NAME tilelayer_target LAYER 7 SCRIPT TileLayerTargetModule SLOT tileLayerTarget
                   DEPS editor map map_editing
                   GROUP 3d)
eve_declare_module(NAME heightmap_target LAYER 7 SCRIPT HeightmapTargetModule SLOT heightmapTarget
                   DEPS editor procgen procgen_editing procgen_graphics_editing
                   GROUP 3d)
eve_declare_module(NAME voxelworld_target LAYER 7 SCRIPT VoxelWorldTargetModule SLOT voxelWorldTarget
                   DEPS editor voxel voxel_editing
                   GROUP 3d)
# Domain-facing editor compatibility adapters. These modules preserve the
# eve::editor type spellings without forcing the editor core to link every
# domain editing implementation.
eve_declare_module(NAME action_editor LAYER 7 SCRIPT ActionEditorModule SLOT actionEditor
                   DEPS action animation editor GROUP 3d)
eve_declare_module(NAME animation_editor LAYER 7 DEPS animation_editing editor
                   SCRIPT AnimationEditorModule SLOT animationEditor GROUP 3d web)
eve_declare_module(NAME audio_editor LAYER 7 SCRIPT AudioEditorModule SLOT audioEditor
                   DEPS audio_editing editor OPTIONAL_DEPS audio sound GROUP 3d web)
eve_declare_module(NAME avatar_editor LAYER 7 SCRIPT AvatarEditorModule SLOT avatarEditor
                   DEPS avatar_editing editor GROUP 3d)
eve_declare_module(NAME biome_editor LAYER 7 DEPS biome_editing editor procgen
                   SCRIPT BiomeEditorModule SLOT biomeEditor GROUP 3d)
eve_declare_module(NAME building_editor LAYER 7 DEPS building_editing editor GROUP 2d 3d web)
eve_declare_module(NAME domain_gizmo_editor LAYER 7 DEPS domain_gizmo_editing editor GROUP 3d web)
eve_declare_module(NAME camera_editor LAYER 7 DEPS camera_editing domain_gizmo_editor editor GROUP 3d web)
eve_declare_module(NAME crowd_editor LAYER 7 DEPS crowd_editing editor GROUP 2d 3d web)
eve_declare_module(NAME decal_editor LAYER 7 DEPS decal_editing editor GROUP 3d web)
eve_declare_module(NAME definitions_editor LAYER 7 DEPS definitions_editing editor GROUP 2d 3d web)
eve_declare_module(NAME dialogue_editor LAYER 7 DEPS audio_editor dialogue_editing editor GROUP 3d web)
eve_declare_module(NAME graphics_editor LAYER 7 SCRIPT GraphicsEditorModule SLOT graphicsEditor
                   DEPS editor graphics graphics_editing GROUP 3d web)
eve_declare_module(NAME fluids_editor LAYER 7 DEPS editor fluids fluids_editing graphics_editor GROUP 3d web)
eve_declare_module(NAME hd2d_editor LAYER 7 DEPS editor hd2d_editing GROUP 3d)
eve_declare_module(NAME housegen_editor LAYER 7 DEPS domain_gizmo_editor editor housegen_editing GROUP 3d)
eve_declare_module(NAME input_editor LAYER 7 DEPS editor input_editing GROUP 2d 3d web)
eve_declare_module(NAME lighting_editor LAYER 7 DEPS editor lighting_editing GROUP 3d web)
eve_declare_module(NAME localization_editor LAYER 7 DEPS editor localization_editing GROUP 2d 3d web)
eve_declare_module(NAME level_editor LAYER 7 DEPS editor level_editing
                   SCRIPT LevelEditorModule SLOT levelEditor GROUP 2d 3d web)
eve_declare_module(NAME map_editor LAYER 7 DEPS editor map_editing GROUP 2d 3d web)
eve_declare_module(NAME material_editor LAYER 7 DEPS editor graphics_editor material_editing
                   SCRIPT MaterialEditorModule SLOT materialEditor GROUP 3d web)
eve_declare_module(NAME network_editor LAYER 7 DEPS editor network_editing GROUP 2d 3d web)
eve_declare_module(NAME npc_ai_editor LAYER 7 DEPS editor npc_ai_editing GROUP 2d 3d web)
eve_declare_module(NAME particles_editor LAYER 7 DEPS editor particles_editing particles_graphics_editing GROUP 2d 3d)
eve_declare_module(NAME physics_editor LAYER 7 DEPS asset editor physics_editing GROUP 3d web)
eve_declare_module(NAME procgen_editor LAYER 7 DEPS editor procgen_editing GROUP 3d)
eve_declare_module(NAME profiler_editor LAYER 7 DEPS editor profiler_editing GROUP 3d web)
eve_declare_module(NAME queue_editor LAYER 7 DEPS editor queue_editing GROUP 2d 3d web)
eve_declare_module(NAME scene_editor LAYER 7 DEPS editor scene_editing OPTIONAL_DEPS scene
                   SCRIPT SceneEditorModule SLOT sceneEditor GROUP 3d web)
eve_declare_module(NAME sceneloader_editor LAYER 7 DEPS editor sceneloader_editing GROUP 3d web)
eve_declare_module(NAME social_editor LAYER 7 DEPS editor social_editing GROUP 2d 3d web)
eve_declare_module(NAME snow_editor LAYER 7 DEPS editor snow_editing GROUP 3d)
eve_declare_module(NAME spritestack_editor LAYER 7 DEPS editor spritestack_editing GROUP 2d 3d)
eve_declare_module(NAME stylize_editor LAYER 7 DEPS editor graphics_editor stylize_editing GROUP 3d web)
eve_declare_module(NAME ui_editor LAYER 7 DEPS editor ui_editing GROUP 3d web)
eve_declare_module(NAME virtualgeometry_editor LAYER 7 DEPS editor virtualgeometry_editing GROUP 3d)
eve_declare_module(NAME voxel_editor LAYER 7 DEPS editor voxel_editing GROUP 3d)
# L6 -- orchestration (continued)
eve_declare_module(NAME virtualgeometry_editing LAYER 6
                   DEPS editing virtualgeometry
                   GROUP 3d)
eve_declare_module(NAME hd2d_editing LAYER 6
                   DEPS editing
                   OPTIONAL_DEPS hd2d
                   GROUP 3d)
eve_declare_module(NAME avatar_editing LAYER 6
                   DEPS editing
                   OPTIONAL_DEPS avatar
                   GROUP 3d)
eve_declare_module(NAME biome_editing LAYER 6
                   DEPS editing
                   OPTIONAL_DEPS procgen
                   GROUP 3d)
eve_declare_module(NAME fluids_editing LAYER 6
                   DEPS editing fluids
                   GROUP 3d web)
eve_declare_module(NAME dialogue_editing LAYER 6
                   DEPS editing
                   OPTIONAL_DEPS dialogue
                   GROUP web)
eve_declare_module(NAME localization_editing LAYER 6
                   DEPS editing
                   OPTIONAL_DEPS audio dialogue
                   GROUP 2d 3d web)
eve_declare_module(NAME particles_editing LAYER 6
                   DEPS editing
                   OPTIONAL_DEPS particles
                   GROUP 3d)
eve_declare_module(NAME procgen_editing LAYER 6
                   DEPS editing image
                   OPTIONAL_DEPS procgen
                   GROUP 3d)
eve_declare_module(NAME snow_editing LAYER 6
                   DEPS editing
                   OPTIONAL_DEPS snow
                   GROUP 3d)
eve_declare_module(NAME particles_graphics_editing LAYER 6
                   DEPS graphics_editing particles_editing
                   OPTIONAL_DEPS particles
                   GROUP 2d 3d)
eve_declare_module(NAME procgen_graphics_editing LAYER 6
                   DEPS editing graphics
                   OPTIONAL_DEPS procgen
                   GROUP 3d)
eve_declare_module(NAME domain_gizmo_editing LAYER 6
                   DEPS audio_editing editing lighting_editing physics_editing
                   GROUP 3d web)
eve_declare_module(NAME pixelworld_editor LAYER 6 SCRIPT PixelWorldEditorModule SLOT pixelworldEditor
                   DEPS pixelworld ui
                   GROUP 2d 3d web)
# Runtime bridge from capability-selected packages into executable PointGraphs.
eve_declare_module(NAME asset_procgen LAYER 6
                   DEPS asset procgen
                   GROUP 3d)
eve_declare_module(NAME snow LAYER 6 SCRIPT Snow SLOT snow
                   DEPS graphics procgen
                   GROUP 3d)
eve_declare_module(NAME sceneloader LIB EVSceneLoader LAYER 6 SCRIPT SceneLoader
                   DEPS animation data filesystem graphics image model3d scene thread
                   THIRDPARTY assimp
                   GROUP 3d)
eve_declare_module(NAME dialogue LAYER 6
                   SCRIPT Dialogue DialogueUX DialogueVoice DialogueFlow
                   SLOT dialogue dialogueUX dialogueVoice dialogueFlow
                   DEPS avatar audio decision filesystem transaction)
