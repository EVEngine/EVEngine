# The exclude lists arrive ','-joined (semicolons and pipes are shell
# metacharacters, so they would be split/executed by the shell running this
# -P script on Linux/macOS Ninja builds).
include("${CMAKE_CURRENT_LIST_DIR}/collect_module_sources.cmake")

set(_eve_src_txt "${output_dir}/${module_name}_src.txt")
if(NOT EXISTS "${_eve_src_txt}")
    file(WRITE "${_eve_src_txt}" "${file_list}")
    message(WARNING "Rerun CMake to update source files")
    return()
endif()
file(READ "${_eve_src_txt}" old_file_list)
if(NOT file_list STREQUAL old_file_list)
    file(WRITE "${_eve_src_txt}" "${file_list}")
    message(WARNING "Rerun CMake to update source files")
endif()
