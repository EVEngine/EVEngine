# In-process GLSL -> SPIR-V through the Vulkan SDK's shaderc archive (glslang +
# SPIRV-Tools). Every runtime GLSL compile in the engine goes through
# graphics/vulkan/GlslCompiler.cpp, so enabling this on EVGraphics also covers the
# modules above it: gpgpu's compute codegen inherits the INTERFACE link through its
# dependency on graphics. Linking the archive removes the external glslc.exe
# dependency from Windows builds — players do not install the Vulkan SDK.
function(eve_enable_inprocess_shaderc target)
  if(NOT WIN32 OR NOT DEFINED ENV{VULKAN_SDK} OR "$ENV{VULKAN_SDK}" STREQUAL "")
    return()
  endif()
  set(_eve_shaderc_libnames shaderc_combined)
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_eve_shaderc_libnames shaderc_combinedd)
  endif()
  find_library(EVE_SHADERC_LIB NAMES ${_eve_shaderc_libnames}
    HINTS "$ENV{VULKAN_SDK}/Lib" "$ENV{VULKAN_SDK}/lib")
  unset(_eve_shaderc_libnames)
  if(NOT EVE_SHADERC_LIB)
    message(STATUS "${target}: shaderc lib not found; runtime GLSL compilation will spawn glslc")
    return()
  endif()
  # INTERFACE: module libraries are OBJECT libraries, so their private link deps
  # would not reach the final executable.
  target_link_libraries(${target} INTERFACE ${EVE_SHADERC_LIB})
  # PRIVATE: GlslCompiler.cpp selects shaderc at compile time; INTERFACE would only
  # define the macro for consumers and silently leave the target spawning glslc.
  target_compile_definitions(${target} PRIVATE EVE_HAS_SHADERC=1)
  message(STATUS "${target}: in-process shaderc compiler: ${EVE_SHADERC_LIB}")
endfunction()

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

  # Runtime GLSL compilation (newShader / newMeshShader / gpgpu compute codegen).
  eve_enable_inprocess_shaderc(EVGraphics)
endif()

