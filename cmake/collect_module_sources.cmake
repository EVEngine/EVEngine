# Shared by scan_source.cmake / rescan_source.cmake (-P). Sets file_list to a
# stable newline-joined list of .cpp paths for ${scan_dir}.
#
# One GLOB_RECURSE is enough: `*.cpp` already walks subdirectories. A second
# `**/*.cpp` pass duplicated every file and made the list order filesystem-
# dependent, which then looked like a source-list change on every rescan.
#
# exclude_dirs are matched against the path *relative to the module root*, so
# a nested authoring facet (animation/editing) is skipped when scanning
# animation, but compiled when scanning that facet's own DIR.
if(NOT DEFINED scan_dir)
    set(scan_dir "${module_name}")
endif()
if(NOT DEFINED list_name)
    set(list_name "${module_name}")
endif()

string(REPLACE "," ";" exclude_dirs "${exclude_dirs}")
string(REPLACE "," ";" exclude_files "${exclude_files}")

set(_eve_mod_root "${CMAKE_CURRENT_SOURCE_DIR}/${scan_dir}")
file(TO_CMAKE_PATH "${_eve_mod_root}" _eve_mod_root)
file(GLOB_RECURSE source_files LIST_DIRECTORIES false
     "${_eve_mod_root}/*.cpp")
list(REMOVE_DUPLICATES source_files)
list(SORT source_files)

set(file_list "")
foreach(file ${source_files})
    # Configure-time cmake and the Ninja rescan -P script can disagree on
    # `C:/` vs `C:\`. That looks like a source-list change, dirties
    # CMAKE_CONFIGURE_DEPENDS, and the next build re-runs CMake + rebuilds
    # every TU.
    file(TO_CMAKE_PATH "${file}" file)
    file(RELATIVE_PATH _eve_rel "${_eve_mod_root}" "${file}")
    set(_eve_skip FALSE)
    foreach(exc ${exclude_dirs})
        if(exc AND (_eve_rel STREQUAL "${exc}" OR _eve_rel MATCHES "^${exc}/"))
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
