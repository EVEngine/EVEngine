# Shared by scan_source.cmake / rescan_source.cmake (-P). Sets file_list to a
# stable newline-joined list of .cpp paths for ${module_name}.
#
# One GLOB_RECURSE is enough: `*.cpp` already walks subdirectories. A second
# `**/*.cpp` pass duplicated every file and made the list order filesystem-
# dependent, which then looked like a source-list change on every rescan.
string(REPLACE "," ";" exclude_dirs "${exclude_dirs}")
string(REPLACE "," ";" exclude_files "${exclude_files}")

file(GLOB_RECURSE source_files LIST_DIRECTORIES false
     "${CMAKE_CURRENT_SOURCE_DIR}/${module_name}/*.cpp")
list(REMOVE_DUPLICATES source_files)
list(SORT source_files)

set(file_list "")
foreach(file ${source_files})
    # Configure-time cmake and the Ninja rescan -P script can disagree on
    # `C:/` vs `C:\`. That looks like a source-list change, dirties
    # CMAKE_CONFIGURE_DEPENDS, and the next build re-runs CMake + rebuilds
    # every TU.
    file(TO_CMAKE_PATH "${file}" file)
    set(_eve_skip FALSE)
    foreach(exc ${exclude_dirs})
        if(exc AND (file MATCHES "/${exc}/" OR file MATCHES "${exc}[\\/]+"))
            set(_eve_skip TRUE)
        endif()
    endforeach()
    if(NOT _eve_skip)
        foreach(exc ${exclude_files})
            get_filename_component(_eve_fname "${file}" NAME)
            if(exc AND _eve_fname STREQUAL "${exc}")
                set(_eve_skip TRUE)
            endif()
        endforeach()
    endif()
    if(NOT _eve_skip)
        string(APPEND file_list "${file}\n")
    endif()
endforeach()
