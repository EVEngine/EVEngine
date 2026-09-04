# The exclude lists arrive ','-joined (semicolons and pipes are shell
# metacharacters, so they would be split/executed by the shell running this
# script on Linux/macOS Ninja builds).
include("${CMAKE_CURRENT_LIST_DIR}/collect_module_sources.cmake")

set(_eve_src_txt "${output_dir}/${module_name}_src.txt")
set(_eve_write TRUE)
if(EXISTS "${_eve_src_txt}")
    file(READ "${_eve_src_txt}" _eve_old)
    if(_eve_old STREQUAL file_list)
        set(_eve_write FALSE)
    endif()
endif()
if(_eve_write)
    file(WRITE "${_eve_src_txt}" "${file_list}")
    message(STATUS "detected source files: ${_eve_src_txt}")
endif()
