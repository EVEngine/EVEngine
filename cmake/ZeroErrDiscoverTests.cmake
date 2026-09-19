function(zeroerr_discover_tests TARGET)
  # LABEL tags every generated CTest entry, so a run can be restricted to one
  # link unit: the suite builds as one executable per domain and each passes its
  # own target name (ctest -L '^unit_test_<domain>$').
  set(_options)
  set(_one_value LABEL)
  cmake_parse_arguments(_eve_zeroerr "${_options}" "${_one_value}" "" ${ARGN})
  set(ctest_file "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_zeroerr_tests.cmake")
  add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      -DZEROERR_EXE=$<TARGET_FILE:${TARGET}>
      -DCTEST_FILE=${ctest_file}
      -DZEROERR_WORKING_DIRECTORY=${CMAKE_SOURCE_DIR}
      -DZEROERR_LABEL=${_eve_zeroerr_LABEL}
      -P ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ZeroErrDiscoverTestsImpl.cmake
    BYPRODUCTS ${ctest_file}
    COMMENT "Discovering zeroerr tests for ${TARGET}"
    VERBATIM
  )
  set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${ctest_file}")
endfunction()
