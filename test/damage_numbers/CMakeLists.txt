cmake_minimum_required(VERSION 3.21)
project(DamageNumberCoreContracts LANGUAGES CXX)
get_filename_component(EVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TEST OFF CACHE BOOL "" FORCE)
set(USE_MOLD OFF CACHE BOOL "" FORCE)
add_subdirectory("${EVE_ROOT}/external/zeroerr" zeroerr)
add_executable(damage_number_core
    "${EVE_ROOT}/src/engine/common/Diagnostic.cpp"
    "${EVE_ROOT}/src/engine/common/Status.cpp"
    "${EVE_ROOT}/src/engine/common/Result.cpp"
    "${EVE_ROOT}/test/unit/graphics/graphics_damage_numbers.cpp"
    "${EVE_ROOT}/src/modules/graphics/DamageNumbers.cpp")
target_compile_features(damage_number_core PRIVATE cxx_std_20)
target_include_directories(damage_number_core PRIVATE "${EVE_ROOT}/src/engine" "${EVE_ROOT}/src/modules")
target_link_libraries(damage_number_core PRIVATE zeroerr)
enable_testing()
add_test(NAME damage_numbers.no_renderer_or_combat COMMAND damage_number_core)
