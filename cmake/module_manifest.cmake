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
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/rendering_simulation.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/orchestration.cmake)

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
