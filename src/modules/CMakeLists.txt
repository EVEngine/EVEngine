# Module targets are created from cmake/module_manifest.cmake, not listed here.
# This file only carries the per-module build settings that cannot be expressed
# in the manifest: extra include directories and compile definitions.

include(${CMAKE_SOURCE_DIR}/cmake/shader_asset_validation.cmake)

# --- sources that follow an optional dependency ------------------------------
# graphics/Font.cpp is the only bridge from graphics into the font module, so it
# comes out with font. (The Emscripten build already excluded it by name; the
# rule is now the same everywhere.)

eve_module_enabled(font _has_font)
if(NOT _has_font)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES Font.cpp)
endif()

# weather::Snow applies SnowField onto procgen::Heightmap. Without procgen the
# precipitation Weather surface stays, but Snow sources and the boot slot drop
# so web/WASM (and any trim that leaves weather without procgen) never pull
# procgen → map.
eve_module_enabled(weather _has_weather_snow_bridge)
eve_module_enabled(procgen _has_procgen_for_snow)
if(_has_weather_snow_bridge AND NOT _has_procgen_for_snow)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES Snow.cpp SnowField.cpp)
  set(EVE_MODULE_weather_SCRIPT Weather CACHE INTERNAL "" FORCE)
  set(EVE_MODULE_weather_SLOT weather CACHE INTERNAL "" FORCE)
endif()

# HexMapTerrain.cpp bakes hexmap cells. HexTerrain.cpp is the unrelated
# mesh.hexterrain continent recipe and must stay compiled either way.
# Without hexmap the absent stubs still expose generateHexTerrain /
# generateHexSphere so scripts get a structured NotFound instead of a
# missing method.
#
# All optional-bridge exclusions above/below use list(APPEND) on the *normal*
# variable: root already FORCE-set the CACHE entry, and CMP0126 NEW leaves that
# normal value as what create_module() reads. set(... CACHE) without FORCE is a
# no-op here and previously left Snow.cpp / SpriteStackModel.cpp linked on web.
eve_module_enabled(hexmap _has_hexmap)
if(_has_hexmap)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES HexTerrainAbsent.cpp)
else()
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES HexMapTerrain.cpp)
endif()

# editor/EditorParticleGraphRuntime.cpp applies compiled particle module graphs
# to the real emitter only when the particles module is enabled.
eve_module_enabled(particles _has_particles_editor_bridge)
if(NOT _has_particles_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES ParticleGraphRuntime.cpp ParticleOffscreenRuntime.cpp)
endif()

# Crowd authoring documents stay standalone; this bridge applies them to the
# live simulation only when the crowd runtime module is selected.
eve_module_enabled(crowd _has_crowd_editor_bridge)
if(NOT _has_crowd_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES CrowdRuntime.cpp)
endif()

# The shared queue inspector copies both Orders and Production runtime state.
eve_module_enabled(orders _has_orders_editor_bridge)
eve_module_enabled(production _has_production_editor_bridge)
if(NOT _has_orders_editor_bridge OR NOT _has_production_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES QueueInspector.cpp)
endif()

eve_module_enabled(social _has_social_editor_bridge)
if(NOT _has_social_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES SocialRuntime.cpp)
endif()

# Lighting and environment documents are standalone; each live applier follows
# only the runtime module it includes.
eve_module_enabled(graphics _has_graphics_editor_bridge)
if(NOT _has_graphics_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES LightingRuntime.cpp MaterialRuntime.cpp ParticleOffscreenRuntime.cpp EditorSurfaceFluidOffscreen.cpp)
endif()
eve_module_enabled(image _has_image_editor_bridge)
if(NOT _has_graphics_editor_bridge OR NOT _has_image_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES OffscreenPreview.cpp EditorMaterialOffscreen.cpp ParticleOffscreen.cpp EditorUiOffscreen.cpp EditorSurfaceFluidOffscreen.cpp)
endif()
eve_module_enabled(daynight _has_daynight_editor_bridge)
if(NOT _has_daynight_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES EnvironmentDayNightRuntime.cpp)
endif()
eve_module_enabled(weather _has_weather_editor_bridge)
if(NOT _has_weather_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES EnvironmentWeatherRuntime.cpp)
endif()

# audio_editing runtime bridges apply backend-neutral source documents to a
# live OpenAL Source only when the audio module is enabled.
eve_module_enabled(audio _has_audio_editor_bridge)
if(NOT _has_audio_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES AudioRuntime.cpp AudioTransportRuntime.cpp)
endif()

# graphics/SceneLinks.cpp registers the link kinds graphics owns and
# graphics/ScenePicking.cpp provides the ISceneCameraProjection capability that
# scene's picking entry points query; both include scene headers, so dropping
# scene drops both files. physics / audio each have their own SceneLinks.cpp
# with the same basename, so one rule covers all three.
eve_module_enabled(scene _has_scene)
if(NOT _has_scene)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES SceneLinks.cpp ScenePicking.cpp EditorSceneRuntime.cpp)
endif()

# editor/EditorPcgGraph.cpp is the optional bridge to procgen PointGraph.
eve_module_enabled(procgen _has_procgen)
if(NOT _has_procgen)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES PcgGraph.cpp BiomeTarget.cpp BiomeRuntime.cpp HeightmapTarget.cpp HeightmapMesh.cpp TextureRecipeTarget.cpp)
endif()

# Map authoring documents remain available without the runtime map provider;
# these bridges are the only map_editing sources that require live map types.
eve_module_enabled(map _has_map_editor_bridge)
if(NOT _has_map_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES MapIntegration.cpp TileLayerTarget.cpp)
endif()

eve_module_enabled(network _has_network_editor_bridge)
if(NOT _has_network_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES NetworkTelemetryRuntime.cpp)
endif()

eve_module_enabled(sceneloader _has_sceneloader_editor_bridge)
if(NOT _has_sceneloader_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES SceneImportRuntime.cpp)
endif()

# Animation editor documents remain standalone; these files are optional bridges
# to real runtime clips and state machines.
eve_module_enabled(animation _has_animation_editor_bridge)
if(NOT _has_animation_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES AnimationGraphRuntime.cpp AnimationClipRuntime.cpp SkeletonOverlayRuntime.cpp ControlRigDocument.cpp)
endif()

# UI document style/layout preview stays standalone. Runtime publication is
# included only when the retained UI module is enabled.
eve_module_enabled(ui _has_ui_editor_bridge)
if(NOT _has_ui_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES UiRuntime.cpp)
endif()

# Retained UI keeps its own presentation tweens (UiTween.cpp). Motion push
# binders include animation headers and are omitted when animation is trimmed.
eve_module_enabled(animation _has_animation_ui_bridge)
if(NOT _has_animation_ui_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES UiMotionSinks.cpp)
endif()

eve_module_enabled(decal _has_decal_editor_bridge)
if(NOT _has_decal_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES DecalRuntime.cpp)
endif()

eve_module_enabled(stylize _has_stylize_editor_bridge)
if(NOT _has_stylize_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES StylizeTarget.cpp StylizeRuntime.cpp)
endif()

# Camera assets are renderer-neutral; only the publication bridge needs the
# runtime CameraController module.
eve_module_enabled(camera _has_camera_editor_bridge)
if(NOT _has_camera_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES CameraTarget.cpp CameraRuntime.cpp)
endif()

eve_module_enabled(procgen _has_procgen_editor_bridge)
if(NOT _has_procgen_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES HouseGenTarget.cpp HouseGenRuntime.cpp)
endif()

eve_module_enabled(spritestack _has_spritestack_editor_bridge)
if(NOT _has_spritestack_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES SpriteStackTarget.cpp SpriteStackRuntime.cpp)
endif()

# Assimp-backed sliceModel + the editor bake path that calls it. Primitive /
# atlas SpriteStack2D remains available when model3d is trimmed (web/WASM).
eve_module_enabled(model3d _has_model3d_for_spritestack)
eve_module_enabled(spritestack _has_spritestack_model_bridge)
if(_has_spritestack_model_bridge AND NOT _has_model3d_for_spritestack)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES SpriteStackModel.cpp SpriteStackRuntime.cpp)
endif()

# Assimp/ModelData import for skeletons, clips, skins and lattices. Fixture text
# import and all pose/skin runtime stay compiled without model3d.
eve_module_enabled(model3d _has_model3d_for_animation)
eve_module_enabled(animation _has_animation_model_bridge)
if(_has_animation_model_bridge AND NOT _has_model3d_for_animation)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES AnimImporterAssimp.cpp AnimImporterModel.cpp AnimSkinFromModel.cpp AnimLatticeFromModel.cpp AnimationModelImport.cpp)
endif()

eve_module_enabled(virtualgeometry _has_virtualgeometry_editor_bridge)
if(NOT _has_virtualgeometry_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES VirtualGeometryTarget.cpp VirtualGeometryRuntime.cpp)
endif()

eve_module_enabled(avatar _has_avatar_editor_bridge)
if(NOT _has_avatar_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES AvatarTarget.cpp AvatarRuntime.cpp)
endif()

# Voxel brush targets are neutral; palette publication follows the voxel runtime.
eve_module_enabled(voxel _has_voxel_editor_bridge)
if(NOT _has_voxel_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES VoxelPaletteTarget.cpp VoxelPaletteRuntime.cpp VoxelWorldTarget.cpp)
endif()

eve_module_enabled(hd2d _has_hd2d_editor_bridge)
if(NOT _has_hd2d_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES Hd2dTarget.cpp Hd2dRuntime.cpp)
endif()

# Schema-driven definition forms consume reflected SchemaDefinition metadata;
# raw DefinitionDocument editing remains available without the schema module.
eve_module_enabled(schema _has_schema_editor_bridge)
if(NOT _has_schema_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES DefinitionFormRuntime.cpp)
endif()

# Fluid settings and budget previews are standalone; only the live parameter
# applier includes FluidSimulation.
eve_module_enabled(fluids _has_fluids_editor_bridge)
if(NOT _has_fluids_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES FluidRuntime.cpp SurfaceFluidRuntime.cpp EditorSurfaceFluidPreview.cpp EditorSurfaceFluidOffscreen.cpp)
endif()

# editor/EditorDialogueGraphRuntime.cpp converts the standalone graph value to
# ConversationDocument only when the dialogue module is enabled.
eve_module_enabled(dialogue _has_dialogue_editor_bridge)
if(NOT _has_dialogue_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES DialogueGraphRuntime.cpp LocalizationRuntime.cpp)
endif()

# definitions_editing/DefinitionRuntime.cpp publishes neutral definition documents to
# DefinitionRegistry only when the definitions module is enabled.
eve_module_enabled(definitions _has_definitions_editor_bridge)
if(NOT _has_definitions_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES DefinitionRuntime.cpp)
endif()

# Voice audition needs both the dialogue voice service and a live audio Source.
if(NOT _has_audio_editor_bridge)
  list(APPEND EVENGINE_EXCLUDED_MODULE_FILES LocalizationRuntime.cpp)
endif()

# The pinned package owns the native ABI/compiler policy. Only physics TUs reference
# Jolt symbols; its static archive is pulled into EVWorld once in SHARED builds.
eve_module_enabled(physics _eve_jolt_physics)
if(EVENGINE_BUILD_JOLT AND (_eve_jolt_physics OR EVENGINE_BUILD_PHYSICS_CORE_CHECK OR EVENGINE_PROFILE STREQUAL "physics-core-only"))
  if(EVENGINE_THIRD_PARTY_BINARY_DIR)
    find_package(Jolt CONFIG REQUIRED PATHS "${EVENGINE_THIRD_PARTY_BINARY_DIR}"
      NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    # Link groups and tests are assembled in parent/sibling directories.
    set_property(TARGET Jolt::Jolt PROPERTY IMPORTED_GLOBAL TRUE)
  else()
    include(${CMAKE_SOURCE_DIR}/third-party/cmake/Jolt.cmake)
    eve_add_jolt()
    set_target_properties(Jolt PROPERTIES POSITION_INDEPENDENT_CODE ON)
  endif()
  # This private native subclass must match Jolt's default no-RTTI ABI. Keep
  # RTTI enabled for engine-facing provider types in the other translation units.
  if(NOT MSVC AND NOT CPP_RTTI_ENABLED)
    set_property(SOURCE physics/jolt/JoltDistanceConstraint.cpp APPEND PROPERTY
      COMPILE_OPTIONS -fno-rtti)
  endif()
else()
  list(APPEND EVENGINE_EXCLUDED_BACKEND_SUBDIRS jolt)
endif()

# --- create every enabled module -------------------------------------------

foreach(_mod IN LISTS EVE_ENABLED_MODULES)
  if(EVE_MODULE_${_mod}_CORE)
    continue()  # built by src/engine/CMakeLists.txt
  endif()
  create_module(${EVE_MODULE_${_mod}_LIB} ${_mod})
endforeach()

if(TARGET EVPhysics)
  target_compile_definitions(EVPhysics PRIVATE EVENGINE_HAS_JOLT=$<BOOL:${EVENGINE_BUILD_JOLT}>)
  if(TARGET Jolt::Jolt)
    target_link_libraries(EVPhysics PRIVATE Jolt::Jolt)
  endif()
endif()

if(TARGET EVAnimation_editor AND _has_animation_editor_bridge)
  # Runtime clip/skeleton import is an optional bridge. Web authoring profiles
  # retain the standalone clip editor without pulling Assimp/model3d into WASM.
  target_compile_definitions(EVAnimation_editor PRIVATE EVE_ANIMATION_EDITOR_RUNTIME=1)
endif()

if(TARGET EVSpriteStack AND _has_spritestack_model_bridge AND _has_model3d_for_spritestack)
  target_compile_definitions(EVSpriteStack PRIVATE EVE_SPRITESTACK_MODEL3D=1)
endif()

if(TARGET EVAnimation AND _has_animation_model_bridge AND _has_model3d_for_animation)
  target_compile_definitions(EVAnimation PRIVATE EVE_ANIMATION_MODEL3D=1)
endif()

# create_module() globs both physics bridge TUs; keep exactly one.
eve_module_enabled(vehicle _has_vehicle)
eve_module_enabled(physics _has_physics)
if(_has_vehicle)
  get_target_property(_eve_vehicle_srcs EVVehicle SOURCES)
  if(_has_physics)
    list(FILTER _eve_vehicle_srcs EXCLUDE REGEX "VehiclePhysicsAbsent\\.cpp$")
  else()
    list(FILTER _eve_vehicle_srcs EXCLUDE REGEX "/VehiclePhysics\\.cpp$")
    list(FILTER _eve_vehicle_srcs EXCLUDE REGEX "/VehicleTires2D\\.cpp$")
    list(FILTER _eve_vehicle_srcs EXCLUDE REGEX "/VehicleTires3D\\.cpp$")
    list(FILTER _eve_vehicle_srcs EXCLUDE REGEX "/VehicleTireRuntime\\.cpp$")
  endif()
  set_target_properties(EVVehicle PROPERTIES SOURCES "${_eve_vehicle_srcs}")
endif()

# Same exclusive-pair rule as VehiclePhysics: the scan may still have both
# names until cmake rewrites procgen_src.txt.
if(TARGET EVProcgen)
  get_target_property(_eve_procgen_hex_srcs EVProcgen SOURCES)
  if(_has_hexmap)
    list(FILTER _eve_procgen_hex_srcs EXCLUDE REGEX "HexTerrainAbsent\\.cpp$")
  else()
    list(FILTER _eve_procgen_hex_srcs EXCLUDE REGEX "HexMapTerrain\\.cpp$")
  endif()
  set_target_properties(EVProcgen PROPERTIES SOURCES "${_eve_procgen_hex_srcs}")
endif()

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

eve_module_enabled(model3d _has_fluids_model3d)
if(TARGET EVFluids AND _has_fluids_model3d)
  target_compile_definitions(EVFluids PRIVATE EVE_FLUIDS_HAS_MODEL3D=1)
endif()

# --- per-module build settings ----------------------------------------------

if(TARGET EVAsset_graphics)
  eve_configure_shader_asset_validation(EVAsset_graphics)
  # GCC aarch64 -O2 miscompiles prefab AABB Z translation in this TU (Linux
  # ARM64 CI: localBounds zmax loses root tz while worldT.z stays correct).
  # Force -O1 for the file so the last -O on the command line wins.
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _eve_prefab_arch)
    if(_eve_prefab_arch MATCHES "^(aarch64|arm64)$")
      set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/asset/graphics/EvpackStaticPrefab.cpp"
        PROPERTIES COMPILE_OPTIONS "-O1")
    endif()
  endif()
endif()
if(TARGET EVGraphics)
  eve_configure_shader_asset_validation(EVGraphics)
endif()

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

include("${CMAKE_CURRENT_SOURCE_DIR}/graphics/BuildSettings.cmake")

eve_module_enabled(ik _has_ik)
if(_has_ik)
  target_include_directories(EVIK PUBLIC ${CMAKE_SOURCE_DIR}/external/ik.hpp)
endif()

eve_module_enabled(animation _has_animation)
eve_module_enabled(model3d _has_model3d_for_animation_includes)
if(_has_animation AND _has_model3d_for_animation_includes)
  # Assimp import bridge TUs only; runtime animation sources do not include Assimp.
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
eve_module_enabled(audio_editor _has_audio_editor)
if(_has_audio_editor AND _has_audio AND _has_sound)
  # Live OpenAL audition is optional. Do not hard-depend on sound: WASM/web
  # profiles do not ship Wuff/OpenAL, and pulling EVSound into that profile
  # fails to compile WaveDecoder.h.
  target_compile_definitions(EVAudio_editor PRIVATE EVE_AUDIO_EDITOR_LIVE=1)
endif()

# stylize_action's Audio AttackVfx executor is the same optional bridge: compile
# the OpenAL path only when audio+sound are in the profile (not on WASM/web).
eve_module_enabled(stylize_action _has_stylize_action)
if(_has_stylize_action AND _has_audio AND _has_sound)
  target_compile_definitions(EVStylize_action PRIVATE EVE_STYLIZE_ACTION_AUDIO=1)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/physics/BuildSettings.cmake")

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
  # Compute codegen uses graphics::compileGlslToSpirv, so EVGpgpu needs no shaderc
  # wiring of its own: the INTERFACE link on EVGraphics reaches this module through
  # its dependency on graphics (see eve_enable_inprocess_shaderc above).
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

include("${CMAKE_CURRENT_SOURCE_DIR}/procgen/BuildSettings.cmake")

if(_has_scene AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  # Scene serialization needs Poco JSON; the browser build has no Poco headers,
  # so EVENGINE_SCENE_JSON compiles that path out of Scene.cpp there.
  target_compile_definitions(EVScene PUBLIC EVENGINE_SCENE_JSON=1)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/ui/BuildSettings.cmake")

# WebP headers remain private to the image codec implementation.
if(TARGET EVImage)
  target_link_libraries(EVImage PRIVATE webpdemux)
endif()
