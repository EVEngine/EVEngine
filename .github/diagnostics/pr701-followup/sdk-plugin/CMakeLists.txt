cmake_minimum_required(VERSION 3.21)
project(pr701_followup LANGUAGES CXX)
find_package(EVEngine REQUIRED)
add_eve_plugin(pr701_probe SOURCES plugin.cpp)
target_include_directories(pr701_probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include" "${PROBE_TP_DIR}/glm" "${PROBE_TP_DIR}/SDL2/include" "${PROBE_TP_DIR}/medialoader/include" "${PROBE_TP_DIR}/medialoader/model-libs/assimp/include")

if(PROBE_ASSIMP_DIR)
  target_include_directories(pr701_probe PRIVATE "${PROBE_ASSIMP_DIR}/include")
endif()
