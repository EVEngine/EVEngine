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

# Each of graphics / physics / audio has a SceneLinks.cpp registering the link
# kinds it owns (scene/SceneLink.h). They all share the basename, so dropping
# scene drops all three in one rule.
eve_module_enabled(scene _has_scene)
if(NOT _has_scene)
  set(EVENGINE_EXCLUDED_MODULE_FILES "${EVENGINE_EXCLUDED_MODULE_FILES};SceneLinks.cpp"
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
# postfix the third-party build was told to use, so every module that touches
# Poco has to opt out of it and into the static build.
foreach(_mod IN LISTS EVE_ENABLED_MODULES)
  if(EVE_MODULE_${_mod}_CORE)
    continue()
  endif()
  if("poco" IN_LIST EVE_MODULE_${_mod}_THIRDPARTY OR
     "poco_data" IN_LIST EVE_MODULE_${_mod}_THIRDPARTY)
    target_compile_definitions(${EVE_MODULE_${_mod}_LIB} PUBLIC POCO_STATIC POCO_NO_AUTOMATIC_LIBS)
  endif()
endforeach()

# --- per-module build settings ----------------------------------------------

eve_module_enabled(graphics _has_graphics)
if(_has_graphics)
  # Header-only VKBuilder: prefer the source tree over a stale third-party install.
  target_include_directories(EVGraphics BEFORE PUBLIC
    ${CMAKE_SOURCE_DIR}/external/VKBuilder/include)
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
if(_has_physics)
  # Box2D headers use <Box2D/Common/...>; prefer source tree over flattened install.
  # Box3D (Erin Catto) is a compiled library linked by name from the manifest.
  target_include_directories(EVPhysics PUBLIC ${CMAKE_SOURCE_DIR}/third-party)
endif()

eve_module_enabled(gpgpu _has_gpgpu)
if(_has_gpgpu)
  target_include_directories(EVGpgpu BEFORE PRIVATE
    ${CMAKE_SOURCE_DIR}/external/VKBuilder/include)
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
    ${CMAKE_SOURCE_DIR}/external/VKBuilder/include)
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
if(_has_procgen)
  # Compat shims first (Apple Clang: missing drawOperatorArray in upstream DTL),
  # then DTL. BEFORE must precede the visibility keyword so -I dtl_compat wins
  # over the submodule.
  target_include_directories(EVProcgen BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/dtl_compat)
  target_include_directories(EVProcgen PUBLIC
    ${CMAKE_SOURCE_DIR}/third-party/DungeonTemplateLibrary/include)
endif()

if(_has_scene AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  # Scene serialization needs Poco JSON; the browser build has no Poco headers,
  # so EVENGINE_SCENE_JSON compiles that path out of Scene.cpp there.
  target_compile_definitions(EVScene PUBLIC EVENGINE_SCENE_JSON=1)
endif()

# --- Dear ImGui --------------------------------------------------------------

eve_module_enabled(ui _has_ui)
if(_has_ui)
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
    target_include_directories(eve_imgui PUBLIC ${CMAKE_SOURCE_DIR}/third-party/imgui)
    # On Emscripten SDL2 headers come from the port sysroot.
    if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
      target_include_directories(eve_imgui PRIVATE ${EMSCRIPTEN_SYSROOT}/include)
    else()
      target_include_directories(eve_imgui PRIVATE
        ${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}-debug/include/SDL2
        ${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}/include/SDL2
      )
    endif()
    target_compile_definitions(eve_imgui PUBLIC IMGUI_DISABLE_DEMO_WINDOWS)
  endif()

  target_link_libraries(EVUI PUBLIC eve_imgui)
  target_include_directories(EVUI PUBLIC ${CMAKE_SOURCE_DIR}/third-party/imgui)
  target_include_directories(EVUI BEFORE PUBLIC
    ${CMAKE_SOURCE_DIR}/external/VKBuilder/include)
endif()
