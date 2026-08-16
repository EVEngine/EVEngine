
create_module(EVFileSystem filesystem)
if(NOT EVENGINE_WEBGPU_TRIM)
  target_compile_definitions(EVFileSystem PUBLIC POCO_STATIC POCO_NO_AUTOMATIC_LIBS)
endif()
create_module(EVGraphics graphics)
# Day/night cycle: solar orbit + procedural skybox + night lighting systems.
create_module(EVDayNight daynight)
# Weather system: rain / snow / lightning / wind built on the 3D mesh pipeline.
create_module(EVWeather weather)
# Header-only VKBuilder: prefer the source tree over a stale third-party install.
target_include_directories(EVGraphics BEFORE PUBLIC
  ${CMAKE_SOURCE_DIR}/external/VKBuilder/include)
if(EVENGINE_WEBGPU_TRIM)
  # Graphics.h includes <assimp/matrix4x4.h> (header-only usage). The vendored
  # assimp headers live in medialoader (skipped on WASM) but the interface is
  # still needed; expose the header tree without linking the library.
  target_include_directories(EVGraphics BEFORE PUBLIC
    ${CMAKE_SOURCE_DIR}/third-party/medialoader/model-libs/assimp/include)
endif()
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
create_module(EVWindow window)
create_module(EVEvent event)
create_module(EVTimer timer)
create_module(EVSystem system)
create_module(EVMath math)
create_module(EVSpatial spatial)
create_module(EVIK ik)
target_include_directories(EVIK PUBLIC ${CMAKE_SOURCE_DIR}/external/ik.hpp)
if(NOT EVENGINE_WEBGPU_TRIM)
  create_module(EVAnimation animation)
  target_compile_definitions(EVAnimation PUBLIC POCO_STATIC POCO_NO_AUTOMATIC_LIBS)
  # AnimImporter uses Assimp headers (same install tree as model3d / medialoader).
  # Spine JSON uses Poco via data/JsonDocument; atlas/skeleton loaders use filesystem.
  target_include_directories(EVAnimation PRIVATE
    ${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}-debug/include
    ${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}/include
    ${CMAKE_SOURCE_DIR}/third-party/medialoader/include
    ${CMAKE_SOURCE_DIR}/third-party/medialoader/assimp/include
  )
endif()
create_module(EVTensor tensor)
create_module(EVMouse mouse)
create_module(EVKeyboard keyboard)
create_module(EVJoystick joystick)
create_module(EVTouch touch)
create_module(EVData data)
if(NOT EVENGINE_WEBGPU_TRIM)
  target_compile_definitions(EVData PUBLIC POCO_STATIC POCO_NO_AUTOMATIC_LIBS)
endif()
create_module(EVImage image)
if(EVENGINE_WEBGPU_TRIM)
  # ImageData.h includes medialoader/image/pixelformat.h; the vendored
  # medialoader header tree is available in the source checkout even though the
  # library itself is skipped on Emscripten.
  target_include_directories(EVImage BEFORE PUBLIC
    ${CMAKE_SOURCE_DIR}/third-party/medialoader/include)
endif()
if(NOT EVENGINE_WEBGPU_TRIM)
  create_module(EVNetwork network)
  target_compile_definitions(EVNetwork PUBLIC POCO_STATIC POCO_NO_AUTOMATIC_LIBS)
  create_module(EVDatabase database)
  target_compile_definitions(EVDatabase PUBLIC POCO_STATIC POCO_NO_AUTOMATIC_LIBS)
  create_module(EVSound sound)
  # Only the mpg123 header needs a shim (install exposes a flat mpg123.h, while
  # medialoader's Mpg123Decoder.h includes <libmpg123/mpg123.h>). Everything else
  # resolves from the install tree: Wuff/, modplug.h, dr_flac/, vorbis/.
  target_include_directories(EVSound PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sound/include_shim
  )
  create_module(EVAudio audio)
  target_compile_definitions(EVAudio PRIVATE AL_LIBTYPE_STATIC)
endif()

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
else()
  # Dear ImGui (compiled from third-party sources; also installed by third-party ExternalProject)
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
  target_include_directories(eve_imgui PUBLIC ${CMAKE_SOURCE_DIR}/third-party/imgui)
  # On Emscripten SDL2 headers come from the port sysroot.
  if(EVENGINE_WEBGPU_TRIM)
    target_include_directories(eve_imgui PRIVATE ${EMSCRIPTEN_SYSROOT}/include)
  else()
    target_include_directories(eve_imgui PRIVATE
      ${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}-debug/include/SDL2
      ${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}/include/SDL2
    )
  endif()
  target_compile_definitions(eve_imgui PUBLIC IMGUI_DISABLE_DEMO_WINDOWS)
endif()

create_module(EVUI ui)
target_link_libraries(EVUI PUBLIC eve_imgui)
target_include_directories(EVUI PUBLIC ${CMAKE_SOURCE_DIR}/third-party/imgui)
target_include_directories(EVUI BEFORE PUBLIC
  ${CMAKE_SOURCE_DIR}/external/VKBuilder/include)

create_module(EVScene scene)
create_module(EVCamera camera)
# 3D scene loader (SceneLoader): decode a model into an ECS GameObject tree and
# hot-reload it via a fast diff. Depends on scene / model3d / graphics symbols,
# which all resolve at the final link of the host target.
create_module(EVSceneLoader sceneloader)

if(NOT EVENGINE_WEBGPU_TRIM)
  create_module(EVParticles particles)
  target_compile_definitions(EVParticles PUBLIC POCO_STATIC POCO_NO_AUTOMATIC_LIBS)
endif()
create_module(EVPhysics physics)
# Box2D headers use <Box2D/Common/...>; prefer source tree over flattened install.
target_include_directories(EVPhysics PUBLIC ${CMAKE_SOURCE_DIR}/third-party)
# Box3D (Erin Catto) is a compiled library built by the third-party ExternalProject
# and linked by name (box3d / box3dmd / box3dd) in src/engine/CMakeLists.txt.
if(NOT EVENGINE_WEBGPU_TRIM)
  create_module(EVMap map)
  target_compile_definitions(EVMap PUBLIC POCO_STATIC POCO_NO_AUTOMATIC_LIBS)
endif()
create_module(EVGpgpu gpgpu)
target_include_directories(EVGpgpu BEFORE PRIVATE
  ${CMAKE_SOURCE_DIR}/external/VKBuilder/include)
if(NOT EVENGINE_WEBGPU_TRIM)
  create_module(EVModel3D model3d)
  create_module(EVFont font)
  # ft2build.h lives under include/freetype2/ (installed by third-party FreeType).
  if(EVENGINE_THIRD_PARTY_BINARY_ONLY)
    target_include_directories(EVFont PRIVATE
      ${EVENGINE_THIRD_PARTY_BINARY_DIR}/include/freetype2)
  else()
    target_include_directories(EVFont PRIVATE
      ${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}-debug/include/freetype2
      ${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}/include/freetype2
    )
  endif()
endif()
create_module(EVThread thread)
if(NOT EVENGINE_WEBGPU_TRIM)
  create_module(EVPlugins plugins)
  create_module(EVRPG rpg)
  target_compile_definitions(EVRPG PUBLIC POCO_STATIC POCO_NO_AUTOMATIC_LIBS)
  create_module(EVInventory inventory)
  target_compile_definitions(EVInventory PUBLIC POCO_STATIC POCO_NO_AUTOMATIC_LIBS)
  create_module(EVBuilding building)
  target_compile_definitions(EVBuilding PUBLIC POCO_STATIC POCO_NO_AUTOMATIC_LIBS)
  create_module(EVAvatar avatar)
endif()
if(NOT EVENGINE_WEBGPU_TRIM)
    create_module(EVDialogue dialogue)
    create_module(EVProcgen procgen)
endif()
# Compat shims first (Apple Clang: missing drawOperatorArray in upstream DTL), then DTL.
# BEFORE must precede the visibility keyword so -I dtl_compat wins over the submodule.
if(NOT EVENGINE_WEBGPU_TRIM)
  target_include_directories(EVProcgen BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/dtl_compat)
  target_include_directories(EVProcgen PUBLIC
    ${CMAKE_SOURCE_DIR}/third-party/DungeonTemplateLibrary/include)
  create_module(EVHouseGen housegen)
  target_compile_definitions(EVHouseGen PUBLIC POCO_STATIC POCO_NO_AUTOMATIC_LIBS)
  create_module(EVVoxel voxel)
  target_compile_definitions(EVVoxel PUBLIC POCO_STATIC POCO_NO_AUTOMATIC_LIBS)
  create_module(EVStylize stylize)
endif()
create_module(EVEditor editor)
if(EVENGINE_BUILD_DEMO AND NOT EVENGINE_WEBGPU_TRIM)
    create_module(EVDemo demo)
endif()
