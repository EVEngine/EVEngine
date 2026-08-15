# WebGPU platform: shared by the browser (Emscripten/WASM) and native Dawn
# desktop builds. The platform library is backend-neutral; toolchain-specific
# link flags (Emscripten) and game-asset packaging are applied in
# src/engine/CMakeLists.txt (which owns the `eve` host target).

add_library(EVPlatformWebGPU STATIC webplatform.cpp)
target_include_directories(EVPlatformWebGPU PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(EVPlatformWebGPU PUBLIC cxx_std_20)

set(EVENGINE_PLATFORM_LIB EVPlatformWebGPU CACHE INTERNAL "Platform library for the active BUILD_PLATFORM")

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    message(STATUS "WebGPU (Emscripten): game dir = ${EVENGINE_WEBGPU_GAME_DIR}")
endif()
