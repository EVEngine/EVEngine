file(GLOB_RECURSE scripts_src ${CMAKE_CURRENT_SOURCE_DIR} *.nut)
file(GLOB_RECURSE shaders_src ${CMAKE_CURRENT_SOURCE_DIR} *.glsl)

foreach(i IN LISTS scripts_src)
    get_filename_component(script ${i} NAME_WE)
    file(READ ${i} ${script}_content)
endforeach()

# Generated at configure time by eve_write_module_manifest().
if(GENERATED_DIR AND EXISTS "${GENERATED_DIR}/module_list.nut")
    file(READ "${GENERATED_DIR}/module_list.nut" module_list_content)
endif()

foreach(i IN LISTS shaders_src)
    get_filename_component(script ${i} NAME_WE)
    file(READ ${i} ${script}_content)
endforeach()

# Without EVDemo, do not embed the default mini-game script.
if(DEFINED EVENGINE_BUILD_DEMO AND NOT EVENGINE_BUILD_DEMO)
    set(demo_content "")
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/template.cpp.in ${OUTPUT_DIR}/scripts_content.cpp)
