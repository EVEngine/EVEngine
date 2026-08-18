file(GLOB_RECURSE all_scripts ${CMAKE_CURRENT_SOURCE_DIR}/*.nut)
file(GLOB_RECURSE all_glsl ${CMAKE_CURRENT_SOURCE_DIR}/*.glsl)

# The slot -> class table load.nut iterates, generated from the same manifest
# that produced the link list, so the boot script never names a trimmed module.
eve_write_module_manifest(${CMAKE_CURRENT_BINARY_DIR}/module_list.nut)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/scripts_content.cpp
  COMMAND ${CMAKE_COMMAND}
    -DOUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    -DGENERATED_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    -DEVENGINE_BUILD_DEMO=${EVENGINE_BUILD_DEMO}
    -P "${CMAKE_CURRENT_SOURCE_DIR}/integrate.cmake"
  DEPENDS ${all_scripts} ${all_glsl} ${CMAKE_CURRENT_SOURCE_DIR}/template.cpp.in
          ${CMAKE_CURRENT_BINARY_DIR}/module_list.nut
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(EVScripts ${CMAKE_CURRENT_BINARY_DIR}/scripts_content.cpp)


