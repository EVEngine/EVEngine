# The module manifest: single source of truth for what a build contains.
#
# Edit here, not in src/modules/CMakeLists.txt or the EVELIBS / ThirdParty lists
# in src/engine/CMakeLists.txt -- those are derived. See cmake/modules.cmake for
# the eve_declare_module() signature and docs/dev/模块编排与裁剪架构.md for the
# layering the LAYER field records.
#
# DEPS mirrors the real #include graph, which scripts/module_depgraph.py prints;
# keep the two in step when a module gains or drops a dependency.

include_guard(GLOBAL)
include(${CMAKE_CURRENT_LIST_DIR}/modules.cmake)

# Third-party groups in link order. GNU ld resolves left to right, so a
# dependent must appear before its provider (vorbisfile -> vorbis -> ogg,
# PocoNet -> PocoFoundation). Groups are filtered by this order, never sorted.
set(EVE_TP_ORDER
    squirrel
    sdl2
    medialoader_image
    medialoader_model
    medialoader_sound
    assimp
    zlib
    physfs
    lz4
    box2d
    box3d
    audio_codecs
    openal
    freetype
    poco_data
    poco
    xxhash
    CACHE INTERNAL "Third-party groups, in link order")

# ---------------------------------------------------------------------------
# L-1 -- engine core. Built by src/engine/CMakeLists.txt rather than src/modules.
# ---------------------------------------------------------------------------

eve_declare_module(NAME common CORE REQUIRED LIB EVCommon LAYER -1
                   THIRDPARTY squirrel)
eve_declare_module(NAME cmdline CORE REQUIRED LIB EVCmdLine LAYER -1
                   DEPS filesystem
                   THIRDPARTY squirrel poco)
# DevTools consumes business modules only through the capability interfaces in
# common/ (ISceneQuery / IRenderCapture / IPhysicsQuery / IProcgenQuery /
# IParticlesQuery / IAudioQuery / IEditorHost), so it no longer blocks
# trimming scene / physics / procgen / particles / audio / ui / graphics.
eve_declare_module(NAME devtools CORE LIB EVDevTools LAYER -1
                   DEPS platform_event
                   THIRDPARTY poco
                   GROUP 3d)

# ---------------------------------------------------------------------------
# L0 -- foundation
# ---------------------------------------------------------------------------

eve_declare_module(NAME math LAYER 0 SCRIPT Math SLOT math
                   GROUP minimal 2d 3d web)
# Unified grid: layout/topology + projection. Pure math, no Module class.
eve_declare_module(NAME grid LAYER 0
                   GROUP 2d 3d web)
# UI-independent property-access contracts shared by game UI, editor UI and
# automation hosts. This module deliberately has no renderer or script runtime.
eve_declare_module(NAME property_access LAYER 0
                   GROUP minimal 2d 3d web)
# Squirrel reflection adapter for renderer-independent property-access adapters.
# L1 -- gameplay/model adapters
eve_declare_module(NAME scriptmodel LAYER 1
                   DEPS property_access
                   GROUP minimal 2d 3d web)
# L0 -- foundation (continued)
eve_declare_module(NAME data LAYER 0 SCRIPT DataModule
                   THIRDPARTY lz4 poco xxhash
                   GROUP minimal 2d 3d web)
# Filesystem also exposes HotReload, the asset-reload dispatcher.
eve_declare_module(NAME filesystem REQUIRED LIB EVFileSystem LAYER 0 SCRIPT Filesystem HotReload SLOT fs hot
                   THIRDPARTY physfs lz4 zlib poco sdl2)
eve_declare_module(NAME platform_event REQUIRED LAYER 0 SCRIPT PlatformEvent SLOT platform_event
                   THIRDPARTY sdl2)
eve_declare_module(NAME timer REQUIRED LAYER 0 SCRIPT Timer SLOT timer
                   THIRDPARTY sdl2)
eve_declare_module(NAME system LAYER 0 SCRIPT System SLOT system
                   THIRDPARTY sdl2
                   GROUP minimal 2d 3d web)
# Profiler: built-in engine-wide profiler. Scoped zones (common/Profile.h) are
# sprinkled across the core hot systems (physics, animation, particles, audio,
# ui, scene, map, crowd, event, graphics passes) and aggregated per frame into a
# per-module/per-zone call tree with self/total time; GPU frame time comes from
# Vulkan timestamp queries. Zero overhead when disabled.
eve_declare_module(NAME profiler LAYER 0 SCRIPT Profiler SLOT profiler
                   GROUP minimal 2d 3d web)
eve_declare_module(NAME thread LAYER 0 SCRIPT Thread SLOT thread
                   GROUP minimal 2d 3d web)
eve_declare_module(NAME spatial LAYER 0 SCRIPT Spatial SLOT spatial
                   GROUP 2d 3d web)
# Crowd: 连续流场寻路 + 海量单位移动/转向/行动 + Boids 鸟群（纯 CPU 仿真，
# 与渲染解耦；examples/crowd 演示渲染由游戏脚本自行完成）。
eve_declare_module(NAME crowd LAYER 0 SCRIPT Crowd SLOT crowd
                   GROUP 2d 3d web)
eve_declare_module(NAME ik LIB EVIK LAYER 0 SCRIPT IK
                   GROUP 2d 3d web)
# L6 -- editor orchestration
eve_declare_module(NAME editor LAYER 6 SCRIPT Editor SLOT editor
                   DEPS action property_access tags transaction
                   GROUP 3d web
                   OPTIONAL_DEPS animation procgen map voxel)
# L0 -- foundation (continued)
eve_declare_module(NAME plugins LAYER 0 SCRIPT Plugins
                   GROUP 3d)
eve_declare_module(NAME database LAYER 0 SCRIPT Database
                   THIRDPARTY poco_data poco)
# L1 -- gameplay domain adapter
eve_declare_module(NAME rpg LIB EVRPG LAYER 1 SCRIPT RPG
                   DEPS action attributes decision definitions effects inventory settlement)
# L0 -- foundation (continued)
eve_declare_module(NAME inventory LAYER 0 SCRIPT Inventory)
eve_declare_module(NAME economy LAYER 0 SCRIPT Economy SLOT economy
                   GROUP minimal 2d 3d web)
eve_declare_module(NAME attributes LAYER 0 SCRIPT Attributes SLOT attributes
                   GROUP minimal 2d 3d web)
eve_declare_module(NAME authority LAYER 0 SCRIPT Authority SLOT authority
                   GROUP minimal 2d 3d web)
eve_declare_module(NAME decision LAYER 0 SCRIPT Decision SLOT decision
                   GROUP minimal 2d 3d web)
eve_declare_module(NAME definitions LAYER 0 SCRIPT Definitions SLOT definitions
                   DEPS schema
                   GROUP minimal 2d 3d web)
eve_declare_module(NAME effects LAYER 0 SCRIPT Effects SLOT effects
                   DEPS definitions
                   GROUP minimal 2d 3d web)
eve_declare_module(NAME game_event LAYER 0 SCRIPT GameEvent SLOT game_event
                   DEPS schema
                   GROUP minimal 2d 3d web)
eve_declare_module(NAME settlement LIB EVSettlement LAYER 0
                   DEPS game_event
                   GROUP minimal 2d 3d web)
eve_declare_module(NAME orders LAYER 0 SCRIPT Orders SLOT orders
                   GROUP minimal 2d 3d web)
eve_declare_module(NAME policyregistry LAYER 0 SCRIPT PolicyRegistryModule SLOT policies
                   DEPS schema
                   GROUP minimal 2d 3d web)
eve_declare_module(NAME production LAYER 0 SCRIPT Production SLOT production
                   GROUP minimal 2d 3d web)
# L1 -- sensing/action protocol
eve_declare_module(NAME sensing LAYER 1 SCRIPT Sensing SLOT sensing
                   DEPS spatial
                   GROUP minimal 2d 3d web)
# Renderer- and ruleset-neutral gameplay action lifecycle. Domain adapters
# (RPG Skill, Weapon Attack, Card Play, RTS Command) depend on this protocol;
# the core depends on sensing/decision values but never on a gameplay domain.
eve_declare_module(NAME action LIB EVAction LAYER 1
                   DEPS decision sensing tags transaction
                   GROUP minimal 2d 3d web)
# L2 -- combat resolution consuming the action protocol
eve_declare_module(NAME combat LIB EVCombat LAYER 2
                   DEPS action attributes tags
                   GROUP minimal 2d 3d web)
# L0 -- foundation (continued)
eve_declare_module(NAME schema LAYER 0 SCRIPT Schema SLOT schema
                   GROUP minimal 2d 3d web)
eve_declare_module(NAME social LAYER 0 SCRIPT Social SLOT social
                   GROUP minimal 2d 3d web)
eve_declare_module(NAME statepatch LAYER 0 SCRIPT StatePatch SLOT statepatch
                   DEPS transaction
                   GROUP minimal 2d 3d web)
eve_declare_module(NAME steering LAYER 0 SCRIPT Steering SLOT steering
                   GROUP minimal 2d 3d web)
eve_declare_module(NAME tags LAYER 0 SCRIPT Tags SLOT tags
                   GROUP minimal 2d 3d web)
eve_declare_module(NAME transaction LAYER 0 SCRIPT Transaction SLOT transaction
                   GROUP minimal 2d 3d web)
# L4 -- world construction
# PlacementWorld.cpp includes data/JsonDocument.h and Poco JSON (save/load).
# THIRDPARTY poco is required so MSVC compiles those TUs with
# POCO_NO_AUTOMATIC_LIBS; otherwise the obj records a link of
# PocoFoundationd.lib instead of the *mdd archive the third-party build emits.
eve_declare_module(NAME building LAYER 4 SCRIPT Building
                   DEPS grid data game_event
                   THIRDPARTY poco)

# ---------------------------------------------------------------------------
# L1 -- platform services and resource decoding
# ---------------------------------------------------------------------------

eve_declare_module(NAME window REQUIRED LAYER 1 SCRIPT Window SLOT win
                   DEPS platform_event
                   THIRDPARTY sdl2)
eve_declare_module(NAME image LAYER 1 SCRIPT Image
                   DEPS filesystem
                   THIRDPARTY medialoader_image
                   GROUP minimal 2d 3d web)
eve_declare_module(NAME i18n LAYER 1 SCRIPT I18n SLOT i18n
                   DEPS filesystem
                   GROUP 2d 3d web)
eve_declare_module(NAME rx LAYER 1 SCRIPT Rx
                   DEPS platform_event
                   GROUP 2d 3d web)
eve_declare_module(NAME joystick LAYER 1 SCRIPT Joystick
                   DEPS platform_event
                   THIRDPARTY sdl2
                   GROUP minimal 2d 3d web)
# L4 -- model/resource orchestration
eve_declare_module(NAME model3d LIB EVModel3D LAYER 4 SCRIPT Model3D SLOT model3d
                   DEPS filesystem
                   THIRDPARTY medialoader_model assimp
                   GROUP 3d)
# L1 -- platform services (continued)
eve_declare_module(NAME sound LAYER 1 SCRIPT Sound SLOT sound
                   DEPS filesystem
                   THIRDPARTY medialoader_sound audio_codecs
                   GROUP 2d 3d)
eve_declare_module(NAME network LAYER 1 SCRIPT Network
                   DEPS data platform_event
                   THIRDPARTY poco)

# ---------------------------------------------------------------------------
# L2 -- input state and playback
# ---------------------------------------------------------------------------

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

# ---------------------------------------------------------------------------
# L3 -- the rendering hub
# ---------------------------------------------------------------------------

# graphics/Font.cpp is the bridge from graphics into the optional font module.
eve_declare_module(NAME graphics REQUIRED LAYER 3 SCRIPT Graphics SLOT gfx
                   DEPS data filesystem image thread
                   OPTIONAL_DEPS font
                   THIRDPARTY sdl2 assimp)

# ---------------------------------------------------------------------------
# L4 -- rendering extensions and simulation
# ---------------------------------------------------------------------------

eve_declare_module(NAME camera LAYER 4 SCRIPT Camera SLOT camera
                   DEPS platform_event graphics scene
                   GROUP minimal 2d 3d web)
eve_declare_module(NAME gpgpu LAYER 4 SCRIPT Gpgpu SLOT gpgpu
                   DEPS data filesystem graphics
                   GROUP 2d 3d web)
eve_declare_module(NAME ui LIB EVUI LAYER 4 SCRIPT UI SLOT ui
                   DEPS platform_event filesystem graphics image property_access scriptmodel timer window
                   THIRDPARTY sdl2 poco
                   GROUP minimal 2d 3d web)
# The public Physics facade retains its interactive presentation dependencies;
# src/modules/CMakeLists.txt separately compiles the domain core (World/Body/
# Shape/Joint/query/fixed-step) without those dependencies for core profiles.
eve_declare_module(NAME physics LAYER 4 SCRIPT Physics SLOT physics
                   DEPS platform_event graphics gpgpu sensing
                   OPTIONAL_DEPS scene
                   THIRDPARTY box2d box3d
                   GROUP 2d 3d web)
eve_declare_module(NAME map LAYER 4 SCRIPT Map SLOT map
                   DEPS data filesystem graphics grid
                   THIRDPARTY poco
                   GROUP 2d 3d)
eve_declare_module(NAME buildingfx LIB EVBuildingFx LAYER 4 SCRIPT BuildingFx SLOT buildingfx
                   DEPS building graphics)
# Weapon definitions, entities, mounts and fire logic. Standalone so buildings /
# vehicles / turrets all attach the same WeaponMount system.
eve_declare_module(NAME weapon LAYER 4 SCRIPT Weapon SLOT weapon
                   DEPS action attributes effects transaction definitions
                   GROUP 2d 3d)
# L5 -- vehicle adapter
# Vehicle entities, kinematic/tracked/wheeled mobility and the Vehicle adapter
# over the generic orders queue. Depends on weapon so definitions can declare
# weapon mounts.
eve_declare_module(NAME vehicle LAYER 5 SCRIPT Vehicle SLOT vehicle
                   DEPS attributes definitions effects orders weapon settlement game_event
                   OPTIONAL_DEPS physics
                   GROUP 2d 3d)
# L4 -- rendering extensions and simulation (continued)
eve_declare_module(NAME animation LAYER 4 SCRIPT Animation SLOT anim
                   DEPS data filesystem graphics image model3d
                   THIRDPARTY poco assimp
                   GROUP 2d 3d)
eve_declare_module(NAME daynight LIB EVDayNight LAYER 4 SCRIPT DayNight SLOT daynight
                   DEPS graphics
                   GROUP 3d web)
eve_declare_module(NAME weather LAYER 4 SCRIPT Weather SLOT weather
                   DEPS graphics
                   GROUP 3d web)
eve_declare_module(NAME decal LAYER 4 SCRIPT Decal SLOT decal
                   DEPS graphics
                   GROUP 3d)
eve_declare_module(NAME stylize LAYER 4 SCRIPT Stylize SLOT stylize
                   DEPS graphics image
                   GROUP 3d)
# L5 -- voxel aggregate
eve_declare_module(NAME voxel LAYER 5 SCRIPT Voxel
                   DEPS graphics procgen thread
                   GROUP 3d)
# L4 -- rendering extensions (continued)
eve_declare_module(NAME spritestack LIB EVSpriteStack LAYER 4 SCRIPT SpriteStack SLOT spritestack
                   DEPS graphics image model3d
                   GROUP 2d)
eve_declare_module(NAME housegen LIB EVHouseGen LAYER 4 SCRIPT HouseGen
                   DEPS data graphics image model3d)
eve_declare_module(NAME card LAYER 4 SCRIPT Card
                   DEPS attributes decision definitions effects graphics transaction)
eve_declare_module(NAME demo LAYER 4 SCRIPT Demo
                   DEPS graphics sound)

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
                   DEPS action attributes effects orders production transaction
                   GROUP 2d 3d)
eve_declare_module(NAME avatar LAYER 5 SCRIPT Avatar SLOT avatar
                   DEPS animation graphics model3d scene)
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
