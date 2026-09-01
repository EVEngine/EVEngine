# Module targets are created from cmake/module_manifest.cmake, not listed here.
# This file only carries the per-module build settings that cannot be expressed
# in the manifest: extra include directories and compile definitions.

# --- sources that follow an optional dependency ------------------------------
# graphics/Font.cpp is the only bridge from graphics into the font module, so it
# comes out with font. (The Emscripten build already excluded it by name; the
# rule is now the same everywhere.)

eve_module_enabled(font _has_font)
if(NOT _has_font)
  set(EVENGINE_EXCLUDED_MODULE_FILES "${EVENGINE_EXCLUDED_MODULE_FILES};Font.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

# editor/EditorParticleGraphRuntime.cpp applies compiled particle module graphs
# to the real emitter only when the particles module is enabled.
eve_module_enabled(particles _has_particles_editor_bridge)
if(NOT _has_particles_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};ParticleGraphRuntime.cpp;ParticleOffscreenRuntime.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

# Crowd authoring documents stay standalone; this bridge applies them to the
# live simulation only when the crowd runtime module is selected.
eve_module_enabled(crowd _has_crowd_editor_bridge)
if(NOT _has_crowd_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};CrowdRuntime.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

# The shared queue inspector copies both Orders and Production runtime state.
eve_module_enabled(orders _has_orders_editor_bridge)
eve_module_enabled(production _has_production_editor_bridge)
if(NOT _has_orders_editor_bridge OR NOT _has_production_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};QueueInspector.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

eve_module_enabled(social _has_social_editor_bridge)
if(NOT _has_social_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};SocialRuntime.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

# Lighting and environment documents are standalone; each live applier follows
# only the runtime module it includes.
eve_module_enabled(graphics _has_graphics_editor_bridge)
if(NOT _has_graphics_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};LightingRuntime.cpp;MaterialRuntime.cpp;ParticleOffscreenRuntime.cpp;EditorSurfaceFluidOffscreen.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()
eve_module_enabled(image _has_image_editor_bridge)
if(NOT _has_graphics_editor_bridge OR NOT _has_image_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};OffscreenPreview.cpp;EditorMaterialOffscreen.cpp;ParticleOffscreen.cpp;EditorUiOffscreen.cpp;EditorSurfaceFluidOffscreen.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()
eve_module_enabled(building _has_building_editor_bridge)
if(NOT _has_building_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};BuildingTarget.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()
eve_module_enabled(daynight _has_daynight_editor_bridge)
if(NOT _has_daynight_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};EnvironmentDayNightRuntime.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()
eve_module_enabled(weather _has_weather_editor_bridge)
if(NOT _has_weather_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};EnvironmentWeatherRuntime.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

# audio_editing runtime bridges apply backend-neutral source documents to a
# live OpenAL Source only when the audio module is enabled.
eve_module_enabled(audio _has_audio_editor_bridge)
if(NOT _has_audio_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};AudioRuntime.cpp;AudioTransportRuntime.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

# graphics/SceneLinks.cpp registers the link kinds graphics owns and
# graphics/ScenePicking.cpp implements the scene picking entry points that
# need a complete Camera3D; both include scene headers, so dropping scene
# drops both files. physics / audio each have their own SceneLinks.cpp with
# the same basename, so one rule covers all three.
eve_module_enabled(scene _has_scene)
if(NOT _has_scene)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};SceneLinks.cpp;ScenePicking.cpp;EditorSceneRuntime.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

# editor/EditorPcgGraph.cpp is the optional bridge to procgen PointGraph.
eve_module_enabled(procgen _has_procgen)
if(NOT _has_procgen)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};PcgGraph.cpp;BiomeTarget.cpp;BiomeRuntime.cpp;HeightmapTarget.cpp;HeightmapMesh.cpp;TextureRecipeTarget.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

# Map authoring documents remain available without the runtime map provider;
# these bridges are the only map_editing sources that require live map types.
eve_module_enabled(map _has_map_editor_bridge)
if(NOT _has_map_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};MapIntegration.cpp;TileLayerTarget.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

eve_module_enabled(network _has_network_editor_bridge)
if(NOT _has_network_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};NetworkTelemetryRuntime.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

eve_module_enabled(sceneloader _has_sceneloader_editor_bridge)
if(NOT _has_sceneloader_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};SceneImportRuntime.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

# Animation editor documents remain standalone; these files are optional bridges
# to real runtime clips and state machines.
eve_module_enabled(animation _has_animation_editor_bridge)
if(NOT _has_animation_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};AnimationGraphRuntime.cpp;AnimationClipRuntime.cpp;SkeletonOverlayRuntime.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

# UI document style/layout preview stays standalone. Runtime publication is
# included only when the retained UI module is enabled.
eve_module_enabled(ui _has_ui_editor_bridge)
if(NOT _has_ui_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};UiRuntime.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

eve_module_enabled(decal _has_decal_editor_bridge)
if(NOT _has_decal_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};DecalRuntime.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

eve_module_enabled(stylize _has_stylize_editor_bridge)
if(NOT _has_stylize_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};StylizeTarget.cpp;StylizeRuntime.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

# Camera assets are renderer-neutral; only the publication bridge needs the
# runtime CameraController module.
eve_module_enabled(camera _has_camera_editor_bridge)
if(NOT _has_camera_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};CameraTarget.cpp;CameraRuntime.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

eve_module_enabled(housegen _has_housegen_editor_bridge)
if(NOT _has_housegen_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};HouseGenTarget.cpp;HouseGenRuntime.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

eve_module_enabled(spritestack _has_spritestack_editor_bridge)
if(NOT _has_spritestack_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};SpriteStackTarget.cpp;SpriteStackRuntime.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

eve_module_enabled(virtualgeometry _has_virtualgeometry_editor_bridge)
if(NOT _has_virtualgeometry_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};VirtualGeometryTarget.cpp;VirtualGeometryRuntime.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

eve_module_enabled(avatar _has_avatar_editor_bridge)
if(NOT _has_avatar_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};AvatarTarget.cpp;AvatarRuntime.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

# Voxel brush targets are neutral; palette publication follows the voxel runtime.
eve_module_enabled(voxel _has_voxel_editor_bridge)
if(NOT _has_voxel_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};VoxelPaletteTarget.cpp;VoxelPaletteRuntime.cpp;VoxelWorldTarget.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

eve_module_enabled(hd2d _has_hd2d_editor_bridge)
if(NOT _has_hd2d_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};Hd2dTarget.cpp;Hd2dRuntime.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

eve_module_enabled(snow _has_snow_editor_bridge)
if(NOT _has_snow_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};SnowFieldTarget.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

# Schema-driven definition forms consume reflected SchemaDefinition metadata;
# raw DefinitionDocument editing remains available without the schema module.
eve_module_enabled(schema _has_schema_editor_bridge)
if(NOT _has_schema_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};DefinitionFormRuntime.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

# Fluid settings and budget previews are standalone; only the live parameter
# applier includes FluidSimulation.
eve_module_enabled(fluids _has_fluids_editor_bridge)
if(NOT _has_fluids_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};FluidRuntime.cpp;SurfaceFluidRuntime.cpp;EditorSurfaceFluidPreview.cpp;EditorSurfaceFluidOffscreen.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

# editor/EditorDialogueGraphRuntime.cpp converts the standalone graph value to
# ConversationDocument only when the dialogue module is enabled.
eve_module_enabled(dialogue _has_dialogue_editor_bridge)
if(NOT _has_dialogue_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};DialogueGraphRuntime.cpp;LocalizationRuntime.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

# definitions_editing/DefinitionRuntime.cpp publishes neutral definition documents to
# DefinitionRegistry only when the definitions module is enabled.
eve_module_enabled(definitions _has_definitions_editor_bridge)
if(NOT _has_definitions_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};DefinitionRuntime.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

# Voice audition needs both the dialogue voice service and a live audio Source.
if(NOT _has_audio_editor_bridge)
  set(EVENGINE_EXCLUDED_MODULE_FILES
      "${EVENGINE_EXCLUDED_MODULE_FILES};LocalizationRuntime.cpp"
      CACHE INTERNAL "Exact basenames excluded from module source scan")
endif()

# --- create every enabled module -------------------------------------------

foreach(_mod IN LISTS EVE_ENABLED_MODULES)
  if(EVE_MODULE_${_mod}_CORE)
    continue()  # built by src/engine/CMakeLists.txt
  endif()
  create_module(${EVE_MODULE_${_mod}_LIB} ${_mod})
endforeach()

# Poco's MSVC #pragma comment auto-link names libraries without the md/mdd
# postfix the third-party build was told to use (PocoFoundationd.lib vs
# PocoFoundationmdd.lib). OBJECT libraries do not inherit compile definitions
# from other modules, so any TU that includes a Poco header -- directly or via
# data/JsonDocument.h -- must have the flag on its own target. Apply it to
# every native module rather than hoping the THIRDPARTY list stays in sync.
foreach(_mod IN LISTS EVE_ENABLED_MODULES)
  if(EVE_MODULE_${_mod}_CORE)
    continue()
  endif()
  target_compile_definitions(${EVE_MODULE_${_mod}_LIB} PUBLIC POCO_STATIC POCO_NO_AUTOMATIC_LIBS)
endforeach()

# --- per-module build settings ----------------------------------------------

eve_module_enabled(data _has_data)
if(_has_data)
  # Compressor.cpp uses the vendored API directly. Profile builds may reuse a
  # prebuilt dependency install that contains liblz4 but not its development
  # headers, so the source checkout is the authoritative header location.
  target_include_directories(EVData BEFORE PRIVATE
    ${CMAKE_SOURCE_DIR}/third-party)
endif()

eve_module_enabled(asset _has_asset)
if(_has_asset)
  target_link_libraries(EVAsset PRIVATE ${EVENGINE_ASSET_ZSTD_TARGET} ${EVENGINE_ASSET_UTF8PROC_TARGET})
endif()

eve_module_enabled(graphics _has_graphics)
if(_has_graphics)
  # Header-only VKBuilder: prefer the source tree over a stale third-party install.
  target_include_directories(EVGraphics BEFORE PUBLIC
    ${EVENGINE_VKBUILDER_INCLUDE_DIR})
  # Graphics.h includes <assimp/matrix4x4.h> (header-only usage). On WASM the
  # vendored assimp library is skipped but the header tree is still present.
  target_include_directories(EVGraphics BEFORE PUBLIC
    ${CMAKE_SOURCE_DIR}/third-party/medialoader/model-libs/assimp/include)

  if(BUILD_PLATFORM STREQUAL "webgpu")
    # Regenerate WGSL .inc arrays from the GLSL shader sources when tooling
    # (glslc + tint) is available; committed .inc files keep the build going
    # otherwise.
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
      add_custom_target(webgpu_shaders
        COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_SOURCE_DIR}/scripts/compile_webgpu_shaders.py
          --shaders-dir ${CMAKE_SOURCE_DIR}/src/modules/graphics/shaders
        COMMENT "Generating WGSL shader .inc files for the WebGPU backend"
        VERBATIM)
      add_dependencies(EVGraphics webgpu_shaders)
    endif()
  endif()
endif()

eve_module_enabled(ik _has_ik)
if(_has_ik)
  target_include_directories(EVIK PUBLIC ${CMAKE_SOURCE_DIR}/external/ik.hpp)
endif()

eve_module_enabled(animation _has_animation)
if(_has_animation)
  # AnimImporter uses Assimp headers (same install tree as model3d / medialoader).
  # Spine JSON uses Poco via data/JsonDocument; atlas/skeleton loaders use filesystem.
  target_include_directories(EVAnimation PRIVATE
    ${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}-debug/include
    ${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}/include
    ${CMAKE_SOURCE_DIR}/third-party/medialoader/include
    ${CMAKE_SOURCE_DIR}/third-party/medialoader/assimp/include
  )
endif()

eve_module_enabled(image _has_image)
if(_has_image)
  # ImageData.h includes medialoader/image/pixelformat.h; the vendored header
  # tree is in the checkout even where the library itself is skipped.
  target_include_directories(EVImage BEFORE PUBLIC
    ${CMAKE_SOURCE_DIR}/third-party/medialoader/include)
endif()

eve_module_enabled(sound _has_sound)
if(_has_sound)
  # Only the mpg123 header needs a shim (install exposes a flat mpg123.h, while
  # medialoader's Mpg123Decoder.h includes <libmpg123/mpg123.h>). Everything else
  # resolves from the install tree: Wuff/, modplug.h, dr_flac/, vorbis/.
  target_include_directories(EVSound PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sound/include_shim)
endif()

eve_module_enabled(audio _has_audio)
if(_has_audio)
  target_compile_definitions(EVAudio PRIVATE AL_LIBTYPE_STATIC)
endif()

eve_module_enabled(physics _has_physics)
option(EVENGINE_BUILD_PHYSICS_CORE_CHECK
  "Compile the physics domain core without graphics/gpgpu presentation paths" OFF)
set(_eve_physics_core_profile FALSE)
if(EVENGINE_PROFILE STREQUAL "physics-core-only")
  set(_eve_physics_core_profile TRUE)
endif()
if(_has_physics OR _eve_physics_core_profile OR EVENGINE_BUILD_PHYSICS_CORE_CHECK)
  # Box2D headers use <Box2D/Common/...>; prefer source tree over flattened install.
  # Box3D (Erin Catto) is a compiled library linked by name from the manifest.
  if(_has_physics)
    target_include_directories(EVPhysics PUBLIC ${CMAKE_SOURCE_DIR}/third-party)
  endif()

  # World/Body/Shape/Joint/query/fixed-step are the physics domain boundary.
  # Keep its source list explicit so this check cannot accidentally pull in the
  # graphics debug bridge, ClothGPU, or another presentation/accelerator TU.
  set(_eve_physics_core_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/Body.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/Body3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/Fixture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/Joint3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/PhysicsHandles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/PhysicsLink.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/Shape3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/SimulationBackend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/World.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/World3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/WorldSnapshot.cpp)
  set(_eve_physics_core_boundary_files ${_eve_physics_core_sources}
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/Body.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/Body3D.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/Fixture.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/Joint3D.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/PhysicsHandles.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/PhysicsLink.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/Shape3D.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/SimulationBackend.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/World.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/World3D.h)
  foreach(_eve_physics_core_file IN LISTS _eve_physics_core_boundary_files)
    if(NOT EXISTS "${_eve_physics_core_file}")
      message(FATAL_ERROR "Physics core source is missing: ${_eve_physics_core_file}")
    endif()
    file(READ "${_eve_physics_core_file}" _eve_physics_core_content)
    if(_eve_physics_core_content MATCHES "include[ \t]*[\"<](graphics|gpgpu)/")
      message(FATAL_ERROR
        "Physics core source must not include graphics/gpgpu: ${_eve_physics_core_file}")
    endif()
  endforeach()
  if(EVENGINE_BUILD_PHYSICS_CORE_CHECK OR _eve_physics_core_profile)
    add_library(EVPhysicsCoreCheck OBJECT ${_eve_physics_core_sources})
    target_link_libraries(EVPhysicsCoreCheck PRIVATE eve_engine_includes)
    target_include_directories(EVPhysicsCoreCheck PRIVATE
      ${CMAKE_SOURCE_DIR}/src/modules
      ${CMAKE_SOURCE_DIR}/third-party
      ${CMAKE_SOURCE_DIR}/third-party/box3d/include)
    set_target_properties(EVPhysicsCoreCheck PROPERTIES POSITION_INDEPENDENT_CODE ON)
  endif()
endif()

# vehicle 在 physics 开启时会包含 physics 头文件（Body3D.h -> <box3d/id.h>），
# 需要与 EVPhysics 相同的第三方头文件路径。
eve_module_enabled(vehicle _has_vehicle)
eve_module_enabled(physics _has_physics)
if(_has_vehicle AND _has_physics)
  target_include_directories(EVVehicle PRIVATE
    ${CMAKE_SOURCE_DIR}/third-party
    ${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}-debug/include
    ${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}/include)
endif()

eve_module_enabled(gpgpu _has_gpgpu)
if(_has_gpgpu)
  target_include_directories(EVGpgpu BEFORE PRIVATE
    ${EVENGINE_VKBUILDER_INCLUDE_DIR})
  if(WIN32 AND DEFINED ENV{VULKAN_SDK} AND NOT "$ENV{VULKAN_SDK}" STREQUAL "")
    # In-process GLSL -> SPIR-V via the Vulkan SDK's shaderc (glslang + SPIRV-Tools
    # static archive). The tensor module's runtime kernel codegen compiles generated
    # GLSL when a CompiledFunction is built; linking shaderc removes the external
    # glslc.exe dependency on Windows (players don't install the Vulkan SDK).
    set(_eve_shaderc_libnames shaderc_combined)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
      set(_eve_shaderc_libnames shaderc_combinedd)
    endif()
    find_library(EVE_SHADERC_LIB NAMES ${_eve_shaderc_libnames}
      HINTS "$ENV{VULKAN_SDK}/Lib" "$ENV{VULKAN_SDK}/lib")
    unset(_eve_shaderc_libnames)
    if(EVE_SHADERC_LIB)
      # INTERFACE: EVGpgpu is an OBJECT library; its private link deps would not
      # reach the final executable, so propagate the SDK archive to consumers.
      target_link_libraries(EVGpgpu INTERFACE ${EVE_SHADERC_LIB})
      target_compile_definitions(EVGpgpu INTERFACE EVE_HAS_SHADERC=1)
      message(STATUS "Gpgpu: in-process shaderc compiler: ${EVE_SHADERC_LIB}")
    else()
      message(STATUS "Gpgpu: shaderc lib not found; will spawn glslc at runtime")
    endif()
  endif()
endif()

eve_module_enabled(virtualgeometry _has_virtualgeometry)
if(_has_virtualgeometry)
  target_include_directories(EVVirtualGeometry BEFORE PRIVATE
    ${EVENGINE_VKBUILDER_INCLUDE_DIR})
endif()

if(_has_font)
  # ft2build.h lives under include/freetype2/ (installed by third-party FreeType).
  if(EVENGINE_THIRD_PARTY_BINARY_ONLY)
    target_include_directories(EVFont PRIVATE
      ${EVENGINE_THIRD_PARTY_BINARY_DIR}/include/freetype2)
  else()
    target_include_directories(EVFont PRIVATE
      ${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}-debug/include/freetype2
      ${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}/include/freetype2)
  endif()
endif()

eve_module_enabled(procgen _has_procgen)
option(EVENGINE_BUILD_PROCGEN_CORE_CHECK
  "Compile procgen CPU core sources without graphics/map/scene adapters" OFF)
set(_eve_procgen_core_profile FALSE)
if(EVENGINE_PROFILE STREQUAL "procgen-core-only")
  set(_eve_procgen_core_profile TRUE)
endif()
if(_has_procgen OR _eve_procgen_core_profile OR EVENGINE_BUILD_PROCGEN_CORE_CHECK)
  if(_has_procgen)
    # Compat shims first (Apple Clang: missing drawOperatorArray in upstream DTL),
    # then DTL. BEFORE must precede the visibility keyword so -I dtl_compat wins
    # over the submodule.
    target_include_directories(EVProcgen BEFORE PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/procgen/dtl_compat)
    target_include_directories(EVProcgen PUBLIC
      ${CMAKE_SOURCE_DIR}/third-party/DungeonTemplateLibrary/include)
  endif()

  # Procgen's CPU data and recipe path is compiled separately as a dependency
  # boundary check. This is intentionally an OBJECT target: it proves that the
  # selected core translation units compile without linking graphics, map or
  # scene adapters, while the normal EVProcgen target keeps the public facade
  # and all compatibility implementations unchanged.
  set(_eve_procgen_core_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/ArtifactPublish.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/AttributeTable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/core/ProcgenCore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/GeneratorRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/GeneratedArtifact.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/Grid2D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/JsonExport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/MeshBuild.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/Palette.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/ParamSchema.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/Params.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/PointSet.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/PointDelta.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/ProcgenSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/Semantic.cpp)
  set(_eve_procgen_core_boundary_files ${_eve_procgen_core_sources}
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/ArtifactPublish.h
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/GeneratedArtifact.h
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/core/ProcgenCore.h)
  foreach(_eve_procgen_core_file IN LISTS _eve_procgen_core_boundary_files)
    if(NOT EXISTS "${_eve_procgen_core_file}")
      message(FATAL_ERROR "Procgen core source is missing: ${_eve_procgen_core_file}")
    endif()
    file(READ "${_eve_procgen_core_file}" _eve_procgen_core_content)
    if(_eve_procgen_core_content MATCHES "include[ \t]*[\"<](graphics|map|scene)/")
      message(FATAL_ERROR
        "Procgen core source must not include a presentation adapter: ${_eve_procgen_core_file}")
    endif()
  endforeach()
  if(EVENGINE_BUILD_PROCGEN_CORE_CHECK OR _eve_procgen_core_profile)
    add_library(EVProcgenCoreCheck OBJECT ${_eve_procgen_core_sources})
    target_link_libraries(EVProcgenCoreCheck PRIVATE eve_engine_includes)
    target_include_directories(EVProcgenCoreCheck PRIVATE
      ${CMAKE_SOURCE_DIR}/src/modules)
    set_target_properties(EVProcgenCoreCheck PROPERTIES POSITION_INDEPENDENT_CODE ON)
  endif()
endif()

if(_has_scene AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  # Scene serialization needs Poco JSON; the browser build has no Poco headers,
  # so EVENGINE_SCENE_JSON compiles that path out of Scene.cpp there.
  target_compile_definitions(EVScene PUBLIC EVENGINE_SCENE_JSON=1)
endif()

# --- Dear ImGui --------------------------------------------------------------

eve_module_enabled(ui _has_ui)
if(_has_ui)
  # Stage the editor icon font beside other runtime shared data. The backend
  # resolves this from SDL_GetBasePath, so games may run from any working dir.
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/share/eve/fonts")
  configure_file(
    "${CMAKE_SOURCE_DIR}/src/modules/ui/fonts/FontAwesome.ttf"
    "${CMAKE_BINARY_DIR}/share/eve/fonts/FontAwesome.ttf" COPYONLY)
  if(EVENGINE_THIRD_PARTY_BINARY_ONLY AND NOT BUILD_PLATFORM STREQUAL "webgpu")
    # The shared prefix already contains ImGui and its SDL/Vulkan backends.
    add_library(eve_imgui INTERFACE)
    if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Debug")
      target_link_libraries(eve_imgui INTERFACE imguimdd)
    elseif(WIN32)
      target_link_libraries(eve_imgui INTERFACE imguimd)
    else()
      target_link_libraries(eve_imgui INTERFACE imgui)
    endif()
    # INTERFACE library: only propagate the shared include set to consumers.
    target_link_libraries(eve_imgui INTERFACE eve_engine_includes)
  else()
    # Compiled from third-party sources (also installed by the ExternalProject).
    set(_eve_imgui_sources
      ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui.cpp
      ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui_draw.cpp
      ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui_tables.cpp
      ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui_widgets.cpp
      ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui_impl_sdl.cpp
      ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui_impl_vulkan.cpp
    )
    if(BUILD_PLATFORM STREQUAL "webgpu")
      # Vulkan renderer is compiled out; the WebGPU backend is compiled by EVUI.
      list(REMOVE_ITEM _eve_imgui_sources
        ${CMAKE_SOURCE_DIR}/third-party/imgui/imgui_impl_vulkan.cpp)
    endif()
    add_library(eve_imgui STATIC ${_eve_imgui_sources})
    # Vendored third-party imgui mixes public/private enums (input-text/tree
    # flags); GCC 15's -Wdeprecated-enum-enum-conversion is noise for this
    # bundled library. Engine call sites that mix enums are fixed at the
    # source instead (only relevant when strict warnings are enabled).
    if(NOT MSVC AND EVENGINE_ENABLE_STRICT_WARNINGS)
      target_compile_options(eve_imgui PRIVATE -Wno-deprecated-enum-enum-conversion)
    endif()
    target_include_directories(eve_imgui PUBLIC ${CMAKE_SOURCE_DIR}/third-party/imgui)
    # On Emscripten SDL2 headers come from the port sysroot.
    if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
      target_include_directories(eve_imgui PRIVATE ${EMSCRIPTEN_SYSROOT}/include)
    else()
      target_include_directories(eve_imgui PRIVATE
        ${EVENGINE_THIRD_PARTY_BINARY_DIR}/include/SDL2
        ${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}-debug/include/SDL2
        ${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}/include/SDL2
      )
    endif()
    target_compile_definitions(eve_imgui PUBLIC IMGUI_DISABLE_DEMO_WINDOWS)
    # STATIC library: PUBLIC so eve_imgui's own TUs (imgui_impl_vulkan.cpp /
    # imgui_impl_sdl.cpp) compile against the shared include set (Vulkan SDK,
    # SDL2, engine headers) and consumers inherit it transitively.
    target_link_libraries(eve_imgui PUBLIC eve_engine_includes)
  endif()

  target_link_libraries(EVUI PUBLIC eve_imgui)
  target_include_directories(EVUI PUBLIC ${CMAKE_SOURCE_DIR}/third-party/imgui)
  target_include_directories(EVUI BEFORE PUBLIC
    ${EVENGINE_VKBUILDER_INCLUDE_DIR})
endif()
