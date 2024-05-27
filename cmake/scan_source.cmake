file(GLOB_RECURSE source_files ${CMAKE_CURRENT_SOURCE_DIR}/${module_name}/*.cpp)
file(GLOB_RECURSE sub_source_files ${CMAKE_CURRENT_SOURCE_DIR}/${module_name}/**/*.cpp)

set(file_list "")
foreach(file ${source_files})
    set(file_list "${file_list}${file}\n")
endforeach()
foreach(file ${sub_source_files})
    set(file_list "${file_list}${file}\n")
endforeach()

file(WRITE ${output_dir}/${module_name}_src.txt "${file_list}")
message(STATUS "detected source files: ${output_dir}/${module_name}_src.txt")