cmake_minimum_required(VERSION 3.21)
project(EveShaderBindingContracts LANGUAGES C CXX)
get_filename_component(EVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_TEST OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(USE_MOLD OFF CACHE BOOL "" FORCE)
add_subdirectory("${EVE_ROOT}/external/zeroerr" zeroerr)
find_package(Vulkan REQUIRED)
include("${EVE_ROOT}/cmake/shader_asset_validation.cmake")
set(_binding_sources BindingValidationTests.cpp
    "${EVE_ROOT}/src/engine/common/Diagnostic.cpp"
    "${EVE_ROOT}/src/engine/common/Status.cpp"
    "${EVE_ROOT}/src/engine/common/Result.cpp"
    "${EVE_ROOT}/src/modules/graphics/ShaderResourceValidation.cpp"
    "${EVE_ROOT}/src/modules/graphics/vulkan/ShaderBindingValidation.cpp")
enable_testing()
foreach(_provider present absent)
    add_executable(shader_bindings_${_provider} ${_binding_sources})
    target_include_directories(shader_bindings_${_provider} PRIVATE
        "${EVE_ROOT}/src/engine" "${EVE_ROOT}/src/modules" "${EVE_ROOT}/test"
        "${EVE_ROOT}/external/VKBuilder/include" ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(shader_bindings_${_provider} PRIVATE zeroerr)
    if(MSVC)
        target_compile_options(shader_bindings_${_provider} PRIVATE /utf-8 /EHsc /W4)
    endif()
    if(_provider STREQUAL "present")
        eve_configure_shader_asset_validation(shader_bindings_present)
        get_target_property(_definitions shader_bindings_present COMPILE_DEFINITIONS)
        if(NOT "EVENGINE_SPIRV_ASSET_VALIDATION=1" IN_LIST _definitions)
            message(FATAL_ERROR "Shader binding contracts require SPIRV-Tools and SPIRV-Cross core")
        endif()
        target_compile_definitions(shader_bindings_present PRIVATE EVE_TEST_RESOURCE_VALIDATION=1)
    endif()
    add_test(NAME graphics.shaderBindings.provider_${_provider} COMMAND shader_bindings_${_provider})
    set_tests_properties(graphics.shaderBindings.provider_${_provider} PROPERTIES
        FAIL_REGULAR_EXPRESSION "Assertion Failed")
endforeach()
