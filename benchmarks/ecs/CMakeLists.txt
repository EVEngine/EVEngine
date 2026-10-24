cmake_minimum_required(VERSION 3.21)

# Standalone on purpose: exercise the vendored native ECS without renderer/VM dependencies.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(EVEcsBenchmark LANGUAGES CXX)
    include(CTest)
endif()

get_filename_component(ECS_ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(NOT EXISTS "${ECS_ENGINE_ROOT}/external/ECS.hpp/src/ECS.hpp")
    message(FATAL_ERROR "Initialize external/ECS.hpp before building the benchmark")
endif()

foreach(_ecs_patch ecs-shared-default-table ecs-view-preparation ecs-idle-commit ecs-view-scope-and-iteration ecs-view-live-slots ecs-query-and-record-lifetime)
    execute_process(COMMAND "${CMAKE_COMMAND}"
        "-DPATCH=${ECS_ENGINE_ROOT}/cmake/patches/${_ecs_patch}.patch"
        "-DPATCH_DIR=${ECS_ENGINE_ROOT}/external/ECS.hpp"
        -P "${ECS_ENGINE_ROOT}/cmake/patch_third_party.cmake"
        RESULT_VARIABLE _ecs_patch_result)
    if(NOT _ecs_patch_result EQUAL 0)
        message(FATAL_ERROR "Applying the engine ECS ${_ecs_patch} patch failed")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${ECS_ENGINE_ROOT}/cmake/patches/${_ecs_patch}.patch")
endforeach()

add_executable(ecs_benchmark main.cpp)
target_compile_features(ecs_benchmark PRIVATE cxx_std_20)
target_include_directories(ecs_benchmark PRIVATE "${ECS_ENGINE_ROOT}/external/ECS.hpp/src")
target_compile_definitions(ecs_benchmark PRIVATE ECS_BENCH_CONFIG="$<CONFIG>")
file(SHA256 "${ECS_ENGINE_ROOT}/external/ECS.hpp/src/ECS.hpp" ECS_BENCH_HEADER_SHA256)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${ECS_ENGINE_ROOT}/external/ECS.hpp/src/ECS.hpp")
target_compile_definitions(ecs_benchmark PRIVATE ECS_BENCH_HEADER_SHA256="${ECS_BENCH_HEADER_SHA256}")
if(MSVC)
    target_compile_options(ecs_benchmark PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(ecs_benchmark PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
    add_executable(ecs_entity_only_compile entity_only_compile.cpp)
    target_include_directories(ecs_entity_only_compile PRIVATE "${ECS_ENGINE_ROOT}/external/ECS.hpp/src")
    set_target_properties(ecs_entity_only_compile PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
    if(NOT MSVC)
        target_compile_options(ecs_entity_only_compile PRIVATE -pedantic-errors)
    endif()
    add_test(NAME ecs_benchmark.entity_only_cpp17 COMMAND ecs_entity_only_compile)
    foreach(_scenario dense dense4 cold holes50 holes90 refill inheritance deferred vector prepare)
        add_test(NAME ecs_benchmark.${_scenario}
            COMMAND ecs_benchmark --entities 1001 --scenario ${_scenario}
                --samples 3 --warmup 1 --iterations 2)
    endforeach()
    if(NOT TARGET zeroerr)
        add_subdirectory("${ECS_ENGINE_ROOT}/external/zeroerr" "${CMAKE_CURRENT_BINARY_DIR}/zeroerr" EXCLUDE_FROM_ALL)
        target_compile_definitions(zeroerr PRIVATE ZEROERR_NO_MAIN)
    endif()
    add_executable(ecs_contract_test contracts_main.cpp
        "${ECS_ENGINE_ROOT}/test/unit/common/common_tests.cpp")
    target_compile_definitions(ecs_contract_test PRIVATE
        EVE_TEST_FILTER_ORIGINS EVE_TEST_ORIGIN_ecs_view_preparation EVE_TEST_ORIGIN_ecs_container_instances)
    target_compile_features(ecs_contract_test PRIVATE cxx_std_20)
    target_include_directories(ecs_contract_test PRIVATE "${ECS_ENGINE_ROOT}/external/ECS.hpp/src")
    target_link_libraries(ecs_contract_test PRIVATE zeroerr)
    add_test(NAME ecs_benchmark.contracts COMMAND ecs_contract_test --no-color)
    add_executable(ecs_query_lifetime "${ECS_ENGINE_ROOT}/external/ECS.hpp/test/query_lifetime.cpp")
    target_include_directories(ecs_query_lifetime PRIVATE "${ECS_ENGINE_ROOT}/external/ECS.hpp/src")
    set_target_properties(ecs_query_lifetime PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
    foreach(_case scope alignment reuse)
        add_test(NAME ecs_benchmark.query_lifetime_${_case} COMMAND ecs_query_lifetime ${_case})
    endforeach()

    # Exercise the engine's actual default-Table/container owners with the same
    # generated extern-template overlay used by its SDK, in both linkage modes.
    file(READ "${ECS_ENGINE_ROOT}/external/ECS.hpp/src/ECS.hpp" _ecs_contract_header)
    string(REPLACE "\r\n" "\n" _ecs_contract_header "${_ecs_contract_header}")
    file(READ "${ECS_ENGINE_ROOT}/cmake/overrides/ecs/container_instances.hpp" _ecs_contract_instances)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/contracts-ecs")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/contracts-ecs/ECS.hpp.in"
        "${_ecs_contract_header}\n${_ecs_contract_instances}")
    configure_file("${CMAKE_CURRENT_BINARY_DIR}/contracts-ecs/ECS.hpp.in"
        "${CMAKE_CURRENT_BINARY_DIR}/contracts-ecs/ECS.hpp" COPYONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${ECS_ENGINE_ROOT}/cmake/overrides/ecs/container_instances.hpp")
    foreach(_linkage OBJECT SHARED)
        add_library(ecs_contract_foundation_${_linkage} ${_linkage}
            "${ECS_ENGINE_ROOT}/src/engine/common/EcsDefaultTable.cpp"
            "${ECS_ENGINE_ROOT}/src/engine/common/EcsContainerInstances.cpp")
        target_compile_features(ecs_contract_foundation_${_linkage} PUBLIC cxx_std_20)
        target_include_directories(ecs_contract_foundation_${_linkage} PUBLIC
            "${CMAKE_CURRENT_BINARY_DIR}/contracts-ecs")
        if(_linkage STREQUAL "SHARED")
            target_compile_definitions(ecs_contract_foundation_${_linkage}
                PUBLIC EVENGINE_MODULE_DLL PRIVATE EVENGINE_EXPORTS_FOUNDATION)
        endif()
        add_executable(ecs_contract_${_linkage} contracts_main.cpp
            "${ECS_ENGINE_ROOT}/test/unit/common/common_tests.cpp")
        target_compile_definitions(ecs_contract_${_linkage} PRIVATE
            EVE_TEST_FILTER_ORIGINS EVE_TEST_ORIGIN_ecs_view_preparation EVE_TEST_ORIGIN_ecs_container_instances)
        target_link_libraries(ecs_contract_${_linkage} PRIVATE ecs_contract_foundation_${_linkage} zeroerr)
        add_test(NAME ecs_benchmark.contracts_${_linkage} COMMAND ecs_contract_${_linkage} --no-color)
        add_executable(ecs_query_lifetime_${_linkage}
            "${ECS_ENGINE_ROOT}/external/ECS.hpp/test/query_lifetime.cpp")
        target_link_libraries(ecs_query_lifetime_${_linkage} PRIVATE ecs_contract_foundation_${_linkage})
        foreach(_case scope alignment reuse)
            add_test(NAME ecs_benchmark.query_lifetime_${_linkage}_${_case}
                COMMAND ecs_query_lifetime_${_linkage} ${_case})
        endforeach()
    endforeach()
endif()

find_package(Git QUIET)
if(Git_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${ECS_ENGINE_ROOT}" OUTPUT_VARIABLE ECS_BENCH_ENGINE_REV
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${ECS_ENGINE_ROOT}/external/ECS.hpp" OUTPUT_VARIABLE ECS_BENCH_CORE_REV
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    target_compile_definitions(ecs_benchmark PRIVATE
        ECS_BENCH_ENGINE_REV="${ECS_BENCH_ENGINE_REV}" ECS_BENCH_CORE_REV="${ECS_BENCH_CORE_REV}")
endif()
