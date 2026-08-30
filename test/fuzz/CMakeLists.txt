cmake_minimum_required(VERSION 3.21)
project(EVEngineFuzz LANGUAGES C CXX)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "EVEngine fuzz harnesses require Clang and libFuzzer")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(BUILD_TEST OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(USE_MOLD OFF CACHE BOOL "" FORCE)
set(COLORFUL_OUTPUT OFF CACHE STRING "" FORCE)
set(ENABLE_FUZZING ON CACHE BOOL "" FORCE)

add_compile_options(
    -fsanitize=fuzzer-no-link,address,undefined
    -fno-omit-frame-pointer)
add_link_options(-fsanitize=address,undefined)

add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/../../external/zeroerr"
    "${CMAKE_CURRENT_BINARY_DIR}/zeroerr")

execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" -print-runtime-dir
    OUTPUT_VARIABLE _clang_runtime_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
find_library(_fuzzer_no_main
    NAMES clang_rt.fuzzer_no_main-x86_64
    PATHS "${_clang_runtime_dir}"
    NO_DEFAULT_PATH
    REQUIRED)

add_executable(evengine_json_fuzz
    json_fuzz.cpp
    "${CMAKE_CURRENT_LIST_DIR}/../../src/engine/common/Json.cpp")
target_include_directories(evengine_json_fuzz PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../src/engine")
target_link_libraries(evengine_json_fuzz PRIVATE zeroerr "${_fuzzer_no_main}")
