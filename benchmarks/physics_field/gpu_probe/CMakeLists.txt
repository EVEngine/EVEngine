cmake_minimum_required(VERSION 3.21)
project(EVEGpuFieldProbe LANGUAGES CXX)
find_package(Vulkan REQUIRED)
find_program(GLSLC_EXECUTABLE glslc REQUIRED)
foreach(stage IN ITEMS reduce final evaluate)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${stage}.spv"
        COMMAND "${GLSLC_EXECUTABLE}" -O "${CMAKE_CURRENT_SOURCE_DIR}/${stage}.comp"
                -o "${CMAKE_CURRENT_BINARY_DIR}/${stage}.spv"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${stage}.comp"
        VERBATIM)
    list(APPEND GPU_FIELD_SHADERS "${CMAKE_CURRENT_BINARY_DIR}/${stage}.spv")
endforeach()
add_custom_target(gpu_field_probe_shaders DEPENDS ${GPU_FIELD_SHADERS})
add_executable(gpu_field_probe main.cpp)
add_dependencies(gpu_field_probe gpu_field_probe_shaders)
target_compile_features(gpu_field_probe PRIVATE cxx_std_20)
target_link_libraries(gpu_field_probe PRIVATE Vulkan::Vulkan)
