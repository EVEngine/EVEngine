# Copies the shared third-party runtime libraries next to the executables.
#
# Run at *build* time on purpose: the install tree is populated by the third-party
# target this command depends on, so a configure-time file(GLOB) would produce an
# empty list on a fresh build (and a stale one after a rebuild that adds a
# library). SRC_DIRS is the list of third-party install bin directories to search,
# DST the binary directory the executables and their CTest PATH entry live in.
if(NOT DEFINED SRC_DIRS OR NOT DEFINED DST)
    message(FATAL_ERROR "copy_third_party_runtime.cmake requires -DSRC_DIRS= and -DDST=")
endif()

foreach(_eve_runtime_src_dir IN LISTS SRC_DIRS)
    file(GLOB _eve_runtime_dlls
        "${_eve_runtime_src_dir}/SDL2*.dll"
        "${_eve_runtime_src_dir}/box2d*.dll"
        "${_eve_runtime_src_dir}/box3d*.dll"
        "${_eve_runtime_src_dir}/Box2D*.dll")
    foreach(_eve_runtime_dll IN LISTS _eve_runtime_dlls)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_eve_runtime_dll}" "${DST}")
    endforeach()
endforeach()
