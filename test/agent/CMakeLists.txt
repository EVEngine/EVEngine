cmake_minimum_required(VERSION 3.21)
project(EVEngineAgentTests LANGUAGES CXX)
get_filename_component(EVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(EVE_ZEROERR_SOURCE "${EVE_ROOT}/external/zeroerr" CACHE PATH "Pinned zeroerr checkout")
set(EVE_ECS_SOURCE "${EVE_ROOT}/external/ECS.hpp" CACHE PATH "Pinned ECS checkout")
set(EVE_THIRD_PARTY_SOURCE "${EVE_ROOT}/third-party" CACHE PATH "Third-party source checkout")
option(EVE_AGENT_COMPOSITION "Compile real grid and UI layout consumers" ON)
option(EVE_AGENT_SCRIPT "Compile real Squirrel API tests" ON)
option(EVE_AGENT_TENSOR "Compile real tensor provider tests" ON)
option(EVE_AGENT_GPU_PROBE "Build tensor GPU shader probe generator" OFF)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TEST OFF CACHE BOOL "" FORCE)
set(USE_MOLD OFF CACHE BOOL "" FORCE)
add_subdirectory("${EVE_ZEROERR_SOURCE}" zeroerr)
add_library(agent_core STATIC "${EVE_ROOT}/src/modules/agent/Agent.cpp"
    "${EVE_ROOT}/src/modules/agent/Codec.cpp"
    "${EVE_ROOT}/src/engine/common/Capability.cpp"
    "${EVE_ROOT}/src/engine/common/Value.cpp"
    "${EVE_ROOT}/src/engine/common/Json.cpp")
target_include_directories(agent_core PUBLIC "${EVE_ROOT}/src/engine" "${EVE_ROOT}/src/modules")
target_link_libraries(agent_core PUBLIC zeroerr)
set(case_sources "${EVE_ROOT}/test/agent.cpp")
set(extra_agent_targets)
if(EVE_AGENT_TENSOR)
    add_library(agent_gpu_compile OBJECT "${EVE_ROOT}/src/modules/agent/tensor/GpuBackend.cpp"
        "${EVE_ROOT}/src/modules/agent/tensor/GpuGraph.cpp"
        "${EVE_ROOT}/src/modules/tensor/GpuBackend.cpp")
    target_link_libraries(agent_gpu_compile PRIVATE agent_core)
    list(APPEND extra_agent_targets agent_gpu_compile)
    add_library(agent_tensor_backend STATIC
        "${EVE_ROOT}/src/modules/agent/tensor/TensorBackend.cpp"
        "${EVE_ROOT}/src/modules/tensor/Tensor.cpp"
        "${EVE_ROOT}/src/modules/tensor/CpuKernels.cpp"
        "${EVE_ROOT}/src/engine/common/Exception.cpp"
        "${EVE_ROOT}/src/engine/common/RenderTrace.cpp")
    target_link_libraries(agent_tensor_backend PUBLIC agent_core)
    list(APPEND case_sources "${EVE_ROOT}/test/agent_tensor.cpp")
    list(APPEND extra_agent_targets agent_tensor_backend)
endif()
if(EVE_AGENT_GPU_PROBE)
    add_executable(agent_gpu_probe "${CMAKE_CURRENT_LIST_DIR}/GpuProbe.cpp"
        "${EVE_ROOT}/src/modules/agent/tensor/GpuGraph.cpp"
        "${EVE_ROOT}/src/modules/tensor/GraphStorage.cpp"
        "${EVE_ROOT}/src/modules/tensor/Optimizer.cpp"
        "${EVE_ROOT}/src/modules/tensor/KernelGen.cpp")
    target_link_libraries(agent_gpu_probe PRIVATE agent_tensor_backend)
    target_include_directories(agent_gpu_probe PRIVATE "${EVE_THIRD_PARTY_SOURCE}/squirrel/include"
        "${EVE_THIRD_PARTY_SOURCE}/simplesquirrel/include")
    list(APPEND extra_agent_targets agent_gpu_probe)
endif()
if(EVE_AGENT_SCRIPT)
    file(GLOB squirrel_sources "${EVE_THIRD_PARTY_SOURCE}/squirrel/squirrel/*.cpp"
        "${EVE_THIRD_PARTY_SOURCE}/squirrel/sqstdlib/*.cpp"
        "${EVE_THIRD_PARTY_SOURCE}/simplesquirrel/source/*.cpp")
    add_library(agent_squirrel STATIC ${squirrel_sources}
        "${EVE_ROOT}/src/engine/common/SquirrelBinding.cpp"
        "${EVE_ROOT}/src/modules/agent/AgentBindings.cpp")
    target_include_directories(agent_squirrel PUBLIC "${EVE_THIRD_PARTY_SOURCE}/squirrel/include"
        "${EVE_THIRD_PARTY_SOURCE}/simplesquirrel/include")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        target_compile_definitions(agent_squirrel PUBLIC _SQ64)
    endif()
    target_link_libraries(agent_squirrel PUBLIC agent_core)
    list(APPEND case_sources "${EVE_ROOT}/test/agent_script.cpp")
    list(APPEND extra_agent_targets agent_squirrel)
    add_library(agent_registration OBJECT "${EVE_ROOT}/src/modules/agent/AgentModule.cpp")
    target_link_libraries(agent_registration PRIVATE agent_squirrel)
    list(APPEND extra_agent_targets agent_registration)
    if(EVE_AGENT_TENSOR)
        target_sources(agent_registration PRIVATE "${EVE_ROOT}/src/modules/agent/tensor/AgentTensor.cpp")
        list(APPEND case_sources "${EVE_ROOT}/test/agent_tensor_script.cpp")
    endif()
endif()
if(EVE_AGENT_COMPOSITION)
    list(APPEND case_sources "${EVE_ROOT}/test/agent_composition.cpp")
endif()
add_executable(agent_tests ${case_sources})
target_link_libraries(agent_tests PRIVATE agent_core)
if(EVE_AGENT_TENSOR)
    target_link_libraries(agent_tests PRIVATE agent_tensor_backend)
endif()
if(EVE_AGENT_SCRIPT)
    target_link_libraries(agent_tests PRIVATE agent_squirrel)
endif()
if(EVE_AGENT_COMPOSITION)
    add_library(agent_domains STATIC
        "${EVE_ROOT}/src/modules/grid/GridProjection.cpp"
        "${EVE_ROOT}/src/modules/ui/Layout.cpp"
        "${EVE_ROOT}/src/modules/ui/Theme.cpp"
        "${EVE_THIRD_PARTY_SOURCE}/imgui/imgui.cpp"
        "${EVE_THIRD_PARTY_SOURCE}/imgui/imgui_draw.cpp"
        "${EVE_THIRD_PARTY_SOURCE}/imgui/imgui_tables.cpp"
        "${EVE_THIRD_PARTY_SOURCE}/imgui/imgui_demo.cpp"
        "${EVE_THIRD_PARTY_SOURCE}/imgui/imgui_widgets.cpp")
    target_link_libraries(agent_domains PUBLIC agent_core)
    target_include_directories(agent_domains PUBLIC "${EVE_ECS_SOURCE}/src"
        "${EVE_THIRD_PARTY_SOURCE}/imgui" "${EVE_THIRD_PARTY_SOURCE}/glm")
    target_link_libraries(agent_tests PRIVATE agent_domains)
    add_executable(agent_demo "${EVE_ROOT}/examples/agent/main.cpp")
    target_link_libraries(agent_demo PRIVATE agent_domains)
endif()
set(agent_targets agent_core agent_tests ${extra_agent_targets})
if(EVE_AGENT_COMPOSITION)
    list(APPEND agent_targets agent_domains agent_demo)
endif()
foreach(target IN LISTS agent_targets)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /EHsc /Gy /W4)
        target_link_options(${target} PRIVATE /OPT:REF)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -ffunction-sections -fdata-sections)
        if(APPLE)
            target_link_options(${target} PRIVATE -Wl,-dead_strip)
        else()
            target_link_options(${target} PRIVATE -Wl,--gc-sections)
        endif()
    endif()
endforeach()
enable_testing()
foreach(source IN LISTS case_sources)
    file(STRINGS "${source}" cases REGEX "^TEST_CASE\\(\"")
    foreach(line IN LISTS cases)
        string(REGEX REPLACE "TEST_CASE\\(\"([^\"]+)\".*" "\\1" name "${line}")
        string(REPLACE "." "\\." pattern "${name}")
        add_test(NAME "${name}" COMMAND agent_tests "--testcase=^${pattern}$")
        set_tests_properties("${name}" PROPERTIES FAIL_REGULAR_EXPRESSION "Assertion Failed" LABELS "agent;cpu")
    endforeach()
endforeach()
