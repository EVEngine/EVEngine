function(zeroerr_discover_tests TARGET)
  # LABEL tags every generated CTest entry, so a run can be restricted to one
  # link unit: the suite builds as one executable per domain and each passes its
  # own target name (ctest -L '^unit_test_<domain>$').
  set(_options)
  set(_one_value LABEL)
  cmake_parse_arguments(_eve_zeroerr "${_options}" "${_one_value}" "" ${ARGN})
  set(ctest_file "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_zeroerr_tests.cmake")
  # SHARED module-linkage builds write the link-group libraries to
  # CMAKE_BINARY_DIR (see cmake/link_groups.cmake), while every test executable
  # lives in <binary dir>/test and CTest runs it with WORKING_DIRECTORY = the
  # repository root. Neither directory holds them, so each generated test entry
  # must carry that directory on the platform's loader search path (PATH on
  # Win32, LD_LIBRARY_PATH on ELF, DYLD_LIBRARY_PATH on Mach-O). On ELF the
  # third-party libraries that are shared in this configuration (SDL) stay in
  # their own install tree, so that directory is added too; on Windows they are
  # copied next to the executables by cmake/link_groups.cmake. Static/one-exe
  # builds (EVENGINE_MODULE_LINKAGE=OBJECT, what release/SDK passes) have no
  # group libraries and emit no loader property. Passed as an extra -D so the
  # generated text stays on one code path.
  set(_eve_zeroerr_dll_dirs "")
  if(EVENGINE_MODULE_LINKAGE STREQUAL "SHARED")
    list(APPEND _eve_zeroerr_dll_dirs "${CMAKE_BINARY_DIR}")
    if(NOT WIN32 AND NOT APPLE)
      get_target_property(_eve_zeroerr_link_dirs eve_engine_includes INTERFACE_LINK_DIRECTORIES)
      foreach(_eve_zeroerr_link_dir IN LISTS _eve_zeroerr_link_dirs)
        # Do not test for existence here: the third-party install tree is
        # registered before the `deps` step populates it, so on a clean configure
        # the filter would silently drop the directory that holds the shared
        # third-party libraries (SDL on ELF) and every discovered test would fail
        # to load them. Only unset/NOTFOUND entries are skipped.
        if(_eve_zeroerr_link_dir AND NOT _eve_zeroerr_link_dir MATCHES "NOTFOUND$")
          list(APPEND _eve_zeroerr_dll_dirs "${_eve_zeroerr_link_dir}")
        endif()
      endforeach()
    endif()
  endif()
  # Quote the -D argument: the value is a semicolon-separated list and an
  # unquoted expansion would be split into several command arguments before the
  # script ever sees it.
  add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      -DZEROERR_EXE=$<TARGET_FILE:${TARGET}>
      -DCTEST_FILE=${ctest_file}
      -DZEROERR_WORKING_DIRECTORY=${CMAKE_SOURCE_DIR}
      -DZEROERR_LABEL=${_eve_zeroerr_LABEL}
      "-DZEROERR_DLL_DIR=${_eve_zeroerr_dll_dirs}"
      -P ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ZeroErrDiscoverTestsImpl.cmake
    BYPRODUCTS ${ctest_file}
    COMMENT "Discovering zeroerr tests for ${TARGET}"
    VERBATIM
  )
  set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${ctest_file}")
endfunction()
