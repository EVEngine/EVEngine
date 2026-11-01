cmake_minimum_required(VERSION 3.21)
project(pr701_sdk_probe LANGUAGES CXX)
find_package(EVEngine REQUIRED)
add_eve_plugin(pr701_probe SOURCES plugin.cpp)
target_include_directories(pr701_probe PRIVATE "${PROBE_TP_DIR}/glm" "${PROBE_TP_DIR}/medialoader/include")

target_include_directories(pr701_probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/modules" "${PROBE_TP_DIR}/VKBuilder/include" "${PROBE_TP_DIR}" "${PROBE_TP_DIR}/vma")
target_compile_definitions(pr701_probe PRIVATE VKB_ENABLE_VMA=1 VMA_VULKAN_VERSION=1000000 VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=1)

target_sources(pr701_probe PRIVATE ../probe.cpp)
set_source_files_properties(../probe.cpp PROPERTIES COMPILE_DEFINITIONS PROBE_EMBEDDED=1)
target_link_libraries(pr701_probe PRIVATE "$ENV{VULKAN_SDK}/lib/libvulkan.dylib")
