file(GLOB_RECURSE source_files ${CMAKE_CURRENT_SOURCE_DIR}/${module_name}/*.cpp)
file(GLOB_RECURSE sub_source_files ${CMAKE_CURRENT_SOURCE_DIR}/${module_name}/**/*.cpp)

set(file_list "")
foreach(file ${source_files})
    set(file_list "${file_list}${file}\n")
endforeach()
foreach(file ${sub_source_files})
    set(file_list "${file_list}${file}\n")
endforeach()

file(READ ${output_dir}/${module_name}_src.txt old_file_list)

if(NOT "${file_list}" STREQUAL "${old_file_list}")
    file(WRITE ${output_dir}/${module_name}_src.txt ${file_list})

    # rerun cmake
    message(WARNING "Rerun CMake to update source files")
    # execute_process(COMMAND ${CMAKE_COMMAND} --build . --target rebuild_cache WORKING_DIRECTORY ${build_dir})
endif()
