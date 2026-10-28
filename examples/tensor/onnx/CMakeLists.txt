cmake_minimum_required(VERSION 3.21)
project(EveTensorOnnx LANGUAGES CXX)
get_filename_component(EVE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(ZEROERR_SOURCE "${EVE_ROOT}/external/zeroerr" CACHE PATH "Initialized zeroerr source directory")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(eve_tensor_onnx STATIC
    "${EVE_ROOT}/src/modules/tensor/AffineQuant.cpp"
    "${EVE_ROOT}/src/modules/tensor/OnnxReader.cpp"
    "${EVE_ROOT}/src/modules/tensor/OnnxModel.cpp"
    "${EVE_ROOT}/src/modules/tensor/OnnxRuntime.cpp"
    "${EVE_ROOT}/src/modules/tensor/OnnxExecute.cpp"
    "${EVE_ROOT}/src/modules/tensor/OnnxQuant.cpp"
    "${EVE_ROOT}/src/modules/tensor/OnnxGpuKernels.cpp"
    "${EVE_ROOT}/src/modules/tensor/OnnxShapeOps.cpp"
    "${EVE_ROOT}/src/modules/tensor/OnnxIndexOps.cpp"
    "${EVE_ROOT}/src/modules/tensor/OnnxNumeric.cpp"
    "${EVE_ROOT}/src/modules/tensor/OnnxNeural.cpp"
    "${EVE_ROOT}/src/modules/tensor/OnnxMisc.cpp"
    "${EVE_ROOT}/src/modules/tensor/OnnxLstm.cpp"
    "${EVE_ROOT}/src/modules/tensor/Tensor.cpp"
    "${EVE_ROOT}/src/modules/tensor/CpuKernels.cpp"
    "${EVE_ROOT}/src/engine/common/Exception.cpp"
    "${EVE_ROOT}/src/engine/common/RenderTrace.cpp")
target_include_directories(eve_tensor_onnx PUBLIC "${EVE_ROOT}/src/modules" "${EVE_ROOT}/src/engine" "${ZEROERR_SOURCE}/include")
if(MSVC)
    target_compile_options(eve_tensor_onnx PUBLIC /utf-8 /EHsc)
endif()
add_executable(onnx_probe main.cpp)
target_link_libraries(onnx_probe PRIVATE eve_tensor_onnx)
option(EVE_ONNX_TESTS "Build focused tensor ONNX contract tests" ON)
if(EVE_ONNX_TESTS)
    set(BUILD_TEST OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory("${ZEROERR_SOURCE}" zeroerr)
    target_link_libraries(eve_tensor_onnx PUBLIC zeroerr)
    add_executable(onnx_contracts "${EVE_ROOT}/test/tensor_onnx.cpp")
    target_link_libraries(onnx_contracts PRIVATE eve_tensor_onnx zeroerr)
    enable_testing()
    foreach(case roundingAndRange exactAccumulators convolutionValidation)
        add_test(NAME "tensor.onnx.affine.${case}" COMMAND onnx_contracts "--testcase=tensor.onnx.affine.${case}")
    endforeach()
    add_test(NAME tensor.onnx.import.transactionAndDiagnostics COMMAND onnx_contracts "--testcase=tensor.onnx.import.transactionAndDiagnostics")
    add_test(NAME tensor.onnx.storage.aliasAndCpuMutation COMMAND onnx_contracts "--testcase=tensor.onnx.storage.aliasAndCpuMutation")
    add_test(NAME tensor.onnx.gpu.failureAndScope COMMAND onnx_contracts "--testcase=tensor.onnx.gpu.failureAndScope")
    add_test(NAME tensor.onnx.import.owningOutputs COMMAND onnx_contracts "--testcase=tensor.onnx.import.owningOutputs")
endif()

if(NOT EVE_ONNX_TESTS)
    target_compile_definitions(eve_tensor_onnx PRIVATE ZEROERR_NO_ASSERT)
    target_compile_definitions(onnx_probe PRIVATE ZEROERR_NO_ASSERT)
endif()

option(EVE_ONNX_VULKAN_PROBE "Enable standalone hardware parity provider (Vulkan SDK + shaderc)" OFF)
if(EVE_ONNX_VULKAN_PROBE)
    find_package(Vulkan REQUIRED)
    find_library(ONNX_SHADERC NAMES shaderc_shared HINTS "$ENV{VULKAN_SDK}/Lib" REQUIRED)
    target_sources(onnx_probe PRIVATE VulkanProbeCompute.cpp)
    target_compile_definitions(onnx_probe PRIVATE EVE_ONNX_VULKAN_PROBE)
    target_link_libraries(onnx_probe PRIVATE Vulkan::Vulkan "${ONNX_SHADERC}")
    if(WIN32)
        add_custom_command(TARGET onnx_probe POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "$ENV{VULKAN_SDK}/Bin/shaderc_shared.dll" "$<TARGET_FILE_DIR:onnx_probe>")
    endif()
endif()
