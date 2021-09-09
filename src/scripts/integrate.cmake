file(GLOB_RECURSE scripts_src ${CMAKE_CURRENT_SOURCE_DIR} *.nut)
file(GLOB_RECURSE shaders_src ${CMAKE_CURRENT_SOURCE_DIR} *.glsl)

foreach(i IN LISTS scripts_src)
    get_filename_component(script ${i} NAME_WE)
    file(READ ${i} ${script}_content)
endforeach()

foreach(i IN LISTS shaders_src)
    get_filename_component(script ${i} NAME_WE)
    file(READ ${i} ${script}_content)
endforeach()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/template.cpp.in ${OUTPUT_DIR}/scripts_content.cpp)