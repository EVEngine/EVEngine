cmake_minimum_required(VERSION 3.21)
project(EVEngineBalanceTests LANGUAGES CXX)
get_filename_component(EVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${EVE_ROOT}/cmake/TestLevels.cmake")
eve_configure_test_levels(EVE_TEST_LEVEL_FILE)
include("${EVE_TEST_LEVEL_FILE}")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(EVE_ZEROERR_SOURCE "${EVE_ROOT}/external/zeroerr" CACHE PATH "Canonical zeroerr source checkout")
set(EVE_THIRD_PARTY_SOURCE "${EVE_ROOT}/third-party" CACHE PATH "Canonical third-party source checkout")
option(EVE_BALANCE_SCRIPT "Build Squirrel binding and real example tests" ON)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TEST OFF CACHE BOOL "" FORCE)
set(USE_MOLD OFF CACHE BOOL "" FORCE)
add_subdirectory("${EVE_ZEROERR_SOURCE}" zeroerr)
add_library(balance_core STATIC
    "${EVE_ROOT}/src/modules/agent/Agent.cpp"
    "${EVE_ROOT}/src/modules/agent/Codec.cpp"
    "${EVE_ROOT}/src/modules/agent/Balance.cpp"
    "${EVE_ROOT}/src/modules/agent/BalanceCodec.cpp"
    "${EVE_ROOT}/src/engine/common/Value.cpp"
    "${EVE_ROOT}/src/engine/common/Json.cpp"
    "${EVE_ROOT}/src/engine/common/Diagnostic.cpp"
    "${EVE_ROOT}/src/engine/common/Status.cpp"
    "${EVE_ROOT}/src/engine/common/Result.cpp")
target_sources(balance_core PRIVATE "${EVE_ROOT}/src/engine/common/Capability.cpp"
    "${EVE_ROOT}/src/engine/common/CapabilityOwned.cpp")
target_include_directories(balance_core PUBLIC "${EVE_ROOT}/src/engine" "${EVE_ROOT}/src/modules")
target_link_libraries(balance_core PUBLIC zeroerr)
add_executable(balance_tests "${EVE_ROOT}/test/unit/agent/agent.cpp")
target_link_libraries(balance_tests PRIVATE balance_core)
set(balance_targets balance_core balance_tests)
enable_testing()
eve_test_source_case_names("${EVE_ROOT}/test/unit/agent/agent.cpp" native_cases balance_tests)
foreach(name IN LISTS native_cases)
    string(REPLACE "." "\\." pattern "${name}")
    add_test(NAME "${name}" COMMAND balance_tests "--testcase=^${pattern}$")
    set_tests_properties("${name}" PROPERTIES FAIL_REGULAR_EXPRESSION "Assertion Failed" TIMEOUT 60)
endforeach()
if(EVE_BALANCE_SCRIPT)
    file(GLOB squirrel_sources "${EVE_THIRD_PARTY_SOURCE}/squirrel/squirrel/*.cpp"
        "${EVE_THIRD_PARTY_SOURCE}/squirrel/sqstdlib/*.cpp"
        "${EVE_THIRD_PARTY_SOURCE}/simplesquirrel/source/*.cpp")
    add_library(balance_script STATIC ${squirrel_sources}
        "${EVE_ROOT}/src/engine/common/SquirrelBinding.cpp"
        "${EVE_ROOT}/src/engine/common/ScriptError.cpp"
        "${EVE_ROOT}/src/engine/common/ScriptProvider.cpp"
        "${EVE_ROOT}/src/engine/common/SquirrelOwnership.cpp"
        "${EVE_ROOT}/src/modules/agent/BalanceBindings.cpp")
    target_include_directories(balance_script PUBLIC "${EVE_THIRD_PARTY_SOURCE}/squirrel/include"
        "${EVE_THIRD_PARTY_SOURCE}/simplesquirrel/include")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        target_compile_definitions(balance_script PUBLIC _SQ64)
    endif()
    target_link_libraries(balance_script PUBLIC balance_core)
    add_executable(balance_script_tests "${EVE_ROOT}/test/integration/balance-search/balance_script.cpp")
    target_link_libraries(balance_script_tests PRIVATE balance_script)
    list(APPEND balance_targets balance_script balance_script_tests)
    eve_test_source_case_names("${EVE_ROOT}/test/integration/balance-search/balance_script.cpp" script_cases balance_script_tests)
    foreach(name IN LISTS script_cases)
        string(REPLACE "." "\\." pattern "${name}")
        add_test(NAME "${name}" COMMAND balance_script_tests "--testcase=^${pattern}$")
        set_tests_properties("${name}" PROPERTIES WORKING_DIRECTORY "${EVE_ROOT}/examples/rpg-balance"
            FAIL_REGULAR_EXPRESSION "Assertion Failed" TIMEOUT 60)
    endforeach()
endif()
foreach(target IN LISTS balance_targets)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /EHsc /Gy /W4 /wd4251 /wd4275)
        target_link_options(${target} PRIVATE /OPT:REF)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endforeach()
