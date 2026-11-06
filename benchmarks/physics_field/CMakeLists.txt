cmake_minimum_required(VERSION 3.21)
project(EVEFieldNumericalBenchmark LANGUAGES CXX)

# Standalone numerical harness, not another engine module declaration channel.
get_filename_component(EVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(EVE_ZEROERR_SOURCE_DIR "${EVE_ROOT}/external/zeroerr" CACHE PATH "Initialized zeroerr checkout")
add_subdirectory("${EVE_ZEROERR_SOURCE_DIR}" zeroerr-build)
option(EVE_FIELD_PROBE_SHARED "Verify FOUNDATION DLL exports with a separate consumer" OFF)
option(EVE_FIELD_CONTRACT_TESTS "Build focused numerical and transaction contract tests" OFF)
if(EVE_FIELD_PROBE_SHARED)
    set(_probe_kind SHARED)
else()
    set(_probe_kind STATIC)
endif()
add_library(field_numeric_probe ${_probe_kind}
    "${EVE_ROOT}/src/modules/physics/field/GravityField.cpp"
    "${EVE_ROOT}/src/modules/physics/field/GravitySimulation.cpp"
    "${EVE_ROOT}/src/modules/spatial/multiscale/CellPosition.cpp"
    "${EVE_ROOT}/src/modules/spatial/multiscale/FrameGraph.cpp"
    "${EVE_ROOT}/src/modules/procgen/recursive/RecursiveModel.cpp"
    "${EVE_ROOT}/src/modules/procgen/recursive/RecursiveInstance.cpp"
    "${EVE_ROOT}/src/modules/procgen/recursive/RecursivePersistence.cpp"
    "${EVE_ROOT}/src/engine/common/Identity.cpp"
    "${EVE_ROOT}/src/engine/common/Value.cpp"
    "${EVE_ROOT}/src/engine/common/Json.cpp"
    "${EVE_ROOT}/src/engine/common/Snapshot.cpp"
    "${EVE_ROOT}/src/engine/common/RuntimeRegistry.cpp"
    "${EVE_ROOT}/src/engine/common/Status.cpp"
    "${EVE_ROOT}/src/engine/common/Diagnostic.cpp"
    "${EVE_ROOT}/src/engine/common/Result.cpp")
target_compile_features(field_numeric_probe PUBLIC cxx_std_20)
target_include_directories(field_numeric_probe PUBLIC "${EVE_ROOT}/src/engine" "${EVE_ROOT}/src/modules")
target_link_libraries(field_numeric_probe PRIVATE zeroerr)
if(EVE_FIELD_PROBE_SHARED)
    target_compile_definitions(field_numeric_probe PUBLIC EVENGINE_MODULE_DLL PRIVATE EVENGINE_EXPORTS_FOUNDATION)
    # The existing common registry uses the host export annotation rather than a group annotation.
    set_source_files_properties("${EVE_ROOT}/src/engine/common/RuntimeRegistry.cpp"
        "${EVE_ROOT}/src/engine/common/Identity.cpp"
        PROPERTIES COMPILE_DEFINITIONS EVENGINE_ENGINE_EXPORTS)
endif()
add_executable(field_benchmark main.cpp)
target_link_libraries(field_benchmark PRIVATE field_numeric_probe)
set(_probe_targets field_numeric_probe field_benchmark)
if(EVE_FIELD_CONTRACT_TESTS)
    enable_testing()
    add_executable(field_contract_tests
        "${EVE_ROOT}/test/unit/physics_field/gravity_field_unit.cpp"
        "${EVE_ROOT}/test/unit/procgen_recursive/recursive_model_unit.cpp"
        "${EVE_ROOT}/test/unit/spatial_multiscale/cell_position_unit.cpp")
    target_link_libraries(field_contract_tests PRIVATE field_numeric_probe zeroerr)
    add_test(NAME field_numerical_contract COMMAND field_contract_tests)
    list(APPEND _probe_targets field_contract_tests)
endif()
foreach(_target IN LISTS _probe_targets)
    if(MSVC)
        # Match the engine's STL/export warning policy (cmake/compiler_options.cmake).
        target_compile_options(${_target} PRIVATE /W4 /WX /wd4251 /wd4275 /utf-8 /EHsc /fp:strict)
    else()
        target_compile_options(${_target} PRIVATE -Wall -Wextra -Werror -fno-fast-math)
    endif()
endforeach()
