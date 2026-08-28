function(zeroerr_discover_tests TARGET)
  set(ctest_file "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_zeroerr_tests.cmake")
  add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      -DZEROERR_EXE=$<TARGET_FILE:${TARGET}>
      -DCTEST_FILE=${ctest_file}
      -DZEROERR_WORKING_DIRECTORY=${CMAKE_SOURCE_DIR}
      -P ${CMAKE_SOURCE_DIR}/cmake/ZeroErrDiscoverTestsImpl.cmake
    BYPRODUCTS ${ctest_file}
    COMMENT "Discovering zeroerr tests for ${TARGET}"
    VERBATIM
  )
  set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${ctest_file}")
endfunction()
