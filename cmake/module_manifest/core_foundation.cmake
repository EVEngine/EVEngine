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
# UI-independent editable-target, command and transaction contracts shared by
# developer editors, in-game builders and automation hosts.
eve_declare_module(NAME editing LAYER 1
                   DEPS property_access schema transaction)
# L0 -- foundation (continued)
eve_declare_module(NAME data LAYER 0 SCRIPT DataModule
                   THIRDPARTY lz4 poco xxhash
                   GROUP minimal 2d 3d web)
# Filesystem also exposes HotReload, the asset-reload dispatcher.
eve_declare_module(NAME filesystem REQUIRED LIB EVFileSystem LAYER 0 SCRIPT Filesystem HotReload SLOT fs hot
                   THIRDPARTY physfs lz4 zlib poco sdl2)
# Canonical source/runtime asset packages. Importer integrations remain in
# higher modules; this layer owns only package admission, manifests and Cook IR.
eve_declare_module(NAME asset LAYER 0
                   DEPS data
                   THIRDPARTY zlib
                   GROUP minimal 2d 3d web)
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
                   DEPS action asset editing material_editing property_access scene_editing tags transaction
                   GROUP 3d web
                   OPTIONAL_DEPS animation audio avatar building camera crowd daynight definitions dialogue fluids graphics hd2d housegen image network orders particles physics physics_editing production procgen profiler map scene sceneloader schema snow social spritestack ui virtualgeometry voxel weather)
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
# Data-oriented NPC AI orchestration. Domain adapters provide navigation,
# animation, combat and smart-object tasks without making the core depend on
# those higher-level modules.
eve_declare_module(NAME npc_ai LAYER 1
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
