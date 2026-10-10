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
    webp
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

include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/core_foundation.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/platform_resources.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/input_playback.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/rendering_hub.cmake)
# Host-owned declaration fragments (ordered by first appearance in the former
# rendering_simulation.cmake + orchestration.cmake). Do not GLOB.
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/physics.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/graphics.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/asset.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/pixelworld.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/camera.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/gpgpu.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/ui.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/map.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/hexmap.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/building.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/weapon.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/combat.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/scene.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/vehicle.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/animation.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/daynight.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/decal.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/stylize.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/voxel.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/spritestack.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/archspace.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/card.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/demo.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/particles.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/fluids.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/gpuagents.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/procgen.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/rts.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/tactics.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/avatar.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/climbing.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/tensor.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/virtualgeometry.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/hd2d.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/agent.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/action.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/audio.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/editor.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/crowd.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/definitions.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/dialogue.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/i18n.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/network.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/npc_ai.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/profiler.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/social.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/weather.cmake)

# --- link groups -----------------------------------------------------------
# The unit a SHARED build turns into one DLL. Modules stay OBJECT libraries; a
# group DLL aggregates the objects of its modules, which is what moves the
# engine's debug information out of every consumer executable and into one PDB
# per group (a per-domain test PDB measured 1.2 GB, of which only ~0.8 MB per
# test file was the test code -- the rest was this engine closure, paid again by
# every consumer).
#
# Grouping is by LAYER, which is what makes the DLL import graph acyclic. Only
# two edges in the whole manifest point at a HIGHER layer -- cmdline and devtools
# (L-1) into filesystem and platform_event (L0) -- so -1 and 0 form one group and
# absorb both. The 25 same-layer edges collapse inside their group. Layer 3 holds
# only 2 modules, so 2 and 3 share a group.
#
# LAYER is the graph-minimum depth derived from DEPS, not a semantic tier: a
# module with no dependents may legally sit higher (same-layer dependencies are
# allowed in this manifest). Once DEPS are reconciled with real usage, moving a
# module between groups is an edit to this table alone.
#
# Row format: "<dll name>|<layers>". Order is the DLL import order (lowest layer
# first). See cmake/link_groups.cmake for how a row becomes a target.
set(EVE_LINK_GROUP_TABLE
    "EVFoundation|-1 0"
    "EVPlatform|1"
    "EVBackends|2 3"
    "EVWorld|4"
    "EVDomains|5"
    "EVOrchestration|6"
    "EVEditors|7"
    CACHE INTERNAL "Engine link groups: <dll name>|<layers>")
