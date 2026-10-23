# Keep the pinned checkout untouched. The generated runner closes the upstream
# context-free assertion catch and warning-success exit paths.
get_target_property(_eve_zeroerr_sources zeroerr SOURCES)
set(_eve_zeroerr_runner "${CMAKE_SOURCE_DIR}/external/zeroerr/src/unittest.cpp")
file(READ "${_eve_zeroerr_runner}" _eve_zeroerr_text)
string(REPLACE "\r\n" "\n" _eve_zeroerr_text "${_eve_zeroerr_text}")
set(_eve_zeroerr_catch "catch (const AssertionData&) {\n            }")
set(_eve_zeroerr_return "return (sum.failed > 0 || sum.failed_as > 0) ? 1 : 0;")
foreach(_expected IN ITEMS "${_eve_zeroerr_catch}" "${_eve_zeroerr_return}")
  string(FIND "${_eve_zeroerr_text}" "${_expected}" _offset)
  if(_offset EQUAL -1)
    message(FATAL_ERROR "Pinned zeroerr runner no longer matches the assertion contract adapter")
  endif()
endforeach()
string(REPLACE "${_eve_zeroerr_catch}"
  "catch (const AssertionData&) {\n                if (context.failed_as == 0) context.failed_as = 1;\n            }"
  _eve_zeroerr_text "${_eve_zeroerr_text}")
string(REPLACE "${_eve_zeroerr_return}"
  "return (sum.failed > 0 || sum.failed_as > 0 || sum.warning_as > 0) ? 1 : 0;"
  _eve_zeroerr_text "${_eve_zeroerr_text}")
set(_eve_zeroerr_generated "${CMAKE_BINARY_DIR}/generated/zeroerr/unittest.cpp")
set(_eve_zeroerr_input "${CMAKE_BINARY_DIR}/generated/zeroerr/unittest.cpp.in")
file(WRITE "${_eve_zeroerr_input}" "${_eve_zeroerr_text}")
configure_file("${_eve_zeroerr_input}" "${_eve_zeroerr_generated}" COPYONLY)
list(REMOVE_ITEM _eve_zeroerr_sources "${_eve_zeroerr_runner}")
list(APPEND _eve_zeroerr_sources "${_eve_zeroerr_generated}")
set_property(TARGET zeroerr PROPERTY SOURCES "${_eve_zeroerr_sources}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_eve_zeroerr_runner}")
