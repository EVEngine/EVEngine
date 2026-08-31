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

include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/core_foundation.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/platform_resources.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/input_playback.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/rendering_hub.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/rendering_simulation.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/orchestration.cmake)
