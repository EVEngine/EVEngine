if(NOT DEFINED ZEROERR_EXE OR NOT DEFINED CTEST_FILE)
  message(FATAL_ERROR "ZEROERR_EXE and CTEST_FILE required")
endif()

execute_process(
  COMMAND "${ZEROERR_EXE}" --list-test-cases
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  RESULT_VARIABLE _rc
)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "list-test-cases failed (${_rc}): ${_err}\n${_out}")
endif()

set(_combined "${_out}\n${_err}")
# COLORFUL_OUTPUT is forced OFF for EVEngine; no ANSI strip needed.
string(REPLACE "\r\n" "\n" _combined "${_combined}")
string(REPLACE "\r" "\n" _combined "${_combined}")
string(REPLACE "\n" ";" _lines "${_combined}")

set(_content "")
foreach(_line IN LISTS _lines)
  # Names are Suite.Case; list mode may append status markers after the name.
  if(_line MATCHES "TEST CASE .+\\] ([A-Za-z0-9_.]+)")
    set(_name "${CMAKE_MATCH_1}")
    if(NOT _name STREQUAL "")
      string(REGEX REPLACE "([][+.*()^$|\\\\])" "\\\\\\1" _escaped "${_name}")
      # CTest include files use classic add_test(name exe [args...]), not NAME/COMMAND keywords.
      string(APPEND _content "add_test(\"${_name}\" \"${ZEROERR_EXE}\" \"--testcase=^${_escaped}$\")\n")
    endif()
  endif()
endforeach()
string(APPEND _content "add_test(unit_test_all \"${ZEROERR_EXE}\")\n")
file(WRITE "${CTEST_FILE}" "${_content}")
