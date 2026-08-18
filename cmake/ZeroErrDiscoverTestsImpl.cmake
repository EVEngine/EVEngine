if(NOT DEFINED ZEROERR_EXE OR NOT DEFINED CTEST_FILE)
  message(FATAL_ERROR "ZEROERR_EXE and CTEST_FILE required")
endif()

# 1) Collect test cases.
#
# Prefer zeroerr's machine-readable listing (--list-format=plain, one
# "<name>\t<basename>:<line>" per line on stdout).  Fall back to parsing the
# console listing ("TEST CASE [<basename>:<line>] <name>") when the pinned
# zeroerr checkout predates that flag, so the script keeps working for both.
execute_process(
  COMMAND "${ZEROERR_EXE}" --list-test-cases --list-format=plain
  OUTPUT_VARIABLE _plain_out
  ERROR_VARIABLE _plain_err
  RESULT_VARIABLE _plain_rc
)
if(NOT _plain_rc EQUAL 0)
  message(FATAL_ERROR "list-test-cases failed (${_plain_rc}): ${_plain_err}\n${_plain_out}")
endif()

set(_combined "${_plain_out}\n${_plain_err}")
# COLORFUL_OUTPUT is forced OFF for EVEngine; no ANSI strip needed.
string(REPLACE "\r\n" "\n" _combined "${_combined}")
string(REPLACE "\r" "\n" _combined "${_combined}")
string(REPLACE "\n" ";" _lines "${_combined}")

# Entries are stored as "<name>|<basename>|<line>" triples.
set(_cases "")
foreach(_line IN LISTS _lines)
  if(_line MATCHES "^([^\t]+)\t([^:]+):([0-9]+)$")
    list(APPEND _cases "${CMAKE_MATCH_1}|${CMAKE_MATCH_2}|${CMAKE_MATCH_3}")
  endif()
endforeach()

if(NOT _cases)
  # Legacy fallback: console listing lines look like
  #   TEST CASE [<basename>:<line>] <name>  ✅
  execute_process(
    COMMAND "${ZEROERR_EXE}" --list-test-cases
    OUTPUT_VARIABLE _legacy_out
    ERROR_VARIABLE _legacy_err
    RESULT_VARIABLE _legacy_rc
  )
  if(NOT _legacy_rc EQUAL 0)
    message(FATAL_ERROR "list-test-cases failed (${_legacy_rc}): ${_legacy_err}\n${_legacy_out}")
  endif()
  set(_combined "${_legacy_out}\n${_legacy_err}")
  string(REPLACE "\r\n" "\n" _combined "${_combined}")
  string(REPLACE "\r" "\n" _combined "${_combined}")
  string(REPLACE "\n" ";" _lines "${_combined}")
  foreach(_line IN LISTS _lines)
    if(_line MATCHES "TEST CASE \\[([^:]+):([0-9]+)\\] ([A-Za-z0-9_.]+)")
      list(APPEND _cases "${CMAKE_MATCH_3}|${CMAKE_MATCH_1}|${CMAKE_MATCH_2}")
    endif()
  endforeach()
endif()

if(NOT _cases)
  message(FATAL_ERROR "no test cases discovered from ${ZEROERR_EXE}")
endif()

# Escape a string for use inside a C++ std::regex.
function(_zeroerr_escape_regex _out_var _in)
  string(REGEX REPLACE "([][+.*()^$|\\\\])" "\\\\\\1" _escaped "${_in}")
  set(${_out_var} "${_escaped}" PARENT_SCOPE)
endfunction()

# 2) Unique source files (by basename) that own at least one test case.
set(_bundle_files "")
foreach(_entry IN LISTS _cases)
  string(REPLACE "|" ";" _parts "${_entry}")
  list(GET _parts 1 _basename)
  list(FIND _bundle_files "${_basename}" _found)
  if(_found EQUAL -1)
    list(APPEND _bundle_files "${_basename}")
  endif()
endforeach()

# 3) Emit CTest entries:
#    - one per test case (exact --testcase filter, kept for `ctest -R`),
#    - one "bundle/<basename>" per file that runs all of the file's cases in
#      a single process (--file=<basename regex>, --quiet) so the default
#      `make test` run spawns ~100 processes instead of ~1400.
set(_content "")
foreach(_basename IN LISTS _bundle_files)
  _zeroerr_escape_regex(_escaped_file "${_basename}")
  string(APPEND _content
    "add_test(\"bundle/${_basename}\" \"${ZEROERR_EXE}\" \"--quiet\" \"--file=.*${_escaped_file}\")\n"
    "set_tests_properties(\"bundle/${_basename}\" PROPERTIES LABELS \"bundle\")\n")
  foreach(_entry IN LISTS _cases)
    string(REPLACE "|" ";" _parts "${_entry}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _file)
    if(_file STREQUAL _basename)
      _zeroerr_escape_regex(_escaped "${_name}")
      # CTest include files use classic add_test(name exe [args...]), not NAME/COMMAND keywords.
      string(APPEND _content
        "add_test(\"${_name}\" \"${ZEROERR_EXE}\" \"--testcase=^${_escaped}$\")\n")
    endif()
  endforeach()
endforeach()
file(WRITE "${CTEST_FILE}" "${_content}")
