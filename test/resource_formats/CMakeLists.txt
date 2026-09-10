cmake_minimum_required(VERSION 3.21)
project(EVEngineResourceCodecTests LANGUAGES C CXX)

# Focused production decoder lifetime regression, independent of GPU/window/VM.
# CMAKE_PREFIX_PATH points to the same installed dependencies as the engine.
get_filename_component(EVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(CMAKE_CXX_STANDARD 20)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TEST OFF CACHE BOOL "" FORCE)
set(USE_MOLD OFF CACHE BOOL "" FORCE)
add_subdirectory("${EVE_ROOT}/external/zeroerr" zeroerr)
find_path(MEDIALOADER_INCLUDE_DIR medialoader/sound/WaveDecoder.h REQUIRED)
find_library(MEDIALOADER_SOUND_LIBRARY NAMES medialoader_sound REQUIRED)
find_library(MEDIALOADER_IMAGE_LIBRARY NAMES medialoader_image REQUIRED)
add_executable(resource_decoder_tests
    "${EVE_ROOT}/test/resource_decoder_lifetime.cpp"
    "${EVE_ROOT}/src/modules/sound/Decoder.cpp"
    "${EVE_ROOT}/src/engine/common/Exception.cpp"
    "${EVE_ROOT}/src/engine/common/RenderTrace.cpp")
target_include_directories(resource_decoder_tests PRIVATE
    "${EVE_ROOT}/src/engine" "${EVE_ROOT}/src/modules" "${MEDIALOADER_INCLUDE_DIR}")
target_link_libraries(resource_decoder_tests PRIVATE
    zeroerr "${MEDIALOADER_SOUND_LIBRARY}" "${MEDIALOADER_IMAGE_LIBRARY}")
option(EVENGINE_RESOURCE_SANITIZERS "Instrument codec lifetime tests with AddressSanitizer" OFF)
if(EVENGINE_RESOURCE_SANITIZERS)
    if(MSVC)
        target_compile_options(resource_decoder_tests PRIVATE /fsanitize=address)
    else()
        target_compile_options(resource_decoder_tests PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(resource_decoder_tests PRIVATE -fsanitize=address)
    endif()
endif()
if(MSVC)
    target_compile_options(resource_decoder_tests PRIVATE /utf-8 /EHsc)
endif()
enable_testing()
include("${EVE_ROOT}/cmake/ZeroErrDiscoverTests.cmake")
zeroerr_discover_tests(resource_decoder_tests)
