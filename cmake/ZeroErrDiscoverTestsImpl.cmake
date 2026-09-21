if(NOT DEFINED ZEROERR_EXE OR NOT DEFINED CTEST_FILE OR NOT DEFINED ZEROERR_WORKING_DIRECTORY)
  message(FATAL_ERROR "ZEROERR_EXE, CTEST_FILE, and ZEROERR_WORKING_DIRECTORY required")
endif()

# Every generated entry carries a label so a run can be restricted to one link
# unit (ctest -L '^unit_test_<domain>$'). Falls back to a generic label when the
# caller passes none, because LABELS must not be empty.
if(NOT DEFINED ZEROERR_LABEL OR ZEROERR_LABEL STREQUAL "")
  set(ZEROERR_LABEL "zeroerr")
endif()

# Runtime library discovery for SHARED module-linkage builds: the link-group
# libraries are written to CMAKE_BINARY_DIR (cmake/link_groups.cmake), the test
# executable lives in <binary dir>/test and CTest's WORKING_DIRECTORY is the
# repository root, so the loader finds nothing without help. ZEROERR_DLL_DIR is a
# *list* of directories passed by ZeroErrDiscoverTests.cmake (empty for
# OBJECT/static builds, where the engine is linked into the executable and no
# loader entry is emitted at all); on ELF it also carries the third-party install
# directory, whose shared libraries (SDL) live outside the binary dir.
#
# The variable that names the loader search path is platform-specific, and so is
# its separator: Win32 loads DLLs from PATH and separates entries with ";" (which
# is also CTest's list separator), ELF loads from LD_LIBRARY_PATH and Mach-O from
# DYLD_LIBRARY_PATH, both separated by ":".
#
# The value is expanded here, at generate time, and escaped for the *second*
# parse it goes through: the generated file is CMake code again, so
#   1. "\" -> "\\"  (a Windows PATH is full of backslashes; MSVC-style paths make
#      "\U" an invalid escape sequence and abort CTest's include of the file),
#   2. ";" -> "\;"  (ENVIRONMENT is a ;-separated list, so unescaped separators
#      would split one PATH value into many bogus entries).
# The inherited value is read at generate time for the same reason: a value CTest
# itself expanded later would already have been split on its semicolons.
set(_eve_test_env_property "")
if(DEFINED ZEROERR_DLL_DIR AND NOT ZEROERR_DLL_DIR STREQUAL "")
  if(WIN32)
    set(_eve_dll_var "PATH")
    set(_eve_dll_sep ";")
    set(_eve_dll_inherited "$ENV{PATH}")
  elseif(APPLE)
    set(_eve_dll_var "DYLD_LIBRARY_PATH")
    set(_eve_dll_sep ":")
    set(_eve_dll_inherited "$ENV{DYLD_LIBRARY_PATH}")
  else()
    set(_eve_dll_var "LD_LIBRARY_PATH")
    set(_eve_dll_sep ":")
    set(_eve_dll_inherited "$ENV{LD_LIBRARY_PATH}")
  endif()
  set(_eve_test_path "")
  foreach(_eve_dir IN LISTS ZEROERR_DLL_DIR)
    if(_eve_dir STREQUAL "")
      continue()
    endif()
    if(_eve_test_path STREQUAL "")
      set(_eve_test_path "${_eve_dir}")
    else()
      set(_eve_test_path "${_eve_test_path}${_eve_dll_sep}${_eve_dir}")
    endif()
  endforeach()
  if(NOT _eve_dll_inherited STREQUAL "")
    set(_eve_test_path "${_eve_test_path}${_eve_dll_sep}${_eve_dll_inherited}")
  endif()
  string(REPLACE "\\" "\\\\" _eve_test_path "${_eve_test_path}")
  string(REPLACE ";" "\\;" _eve_test_path "${_eve_test_path}")
  set(_eve_test_env_property " ENVIRONMENT \"${_eve_dll_var}=${_eve_test_path}\"")

  # The listing step below *runs* the freshly linked executable, so this script's
  # own environment needs the same entries. Setting it here also covers the
  # execute_process children and needs no CMake list escaping.
  set(_eve_test_path_raw "${_eve_test_path}")
  string(REPLACE "\\;" ";" _eve_test_path_raw "${_eve_test_path_raw}")
  string(REPLACE "\\\\" "\\" _eve_test_path_raw "${_eve_test_path_raw}")
  set(ENV{${_eve_dll_var}} "${_eve_test_path_raw}")
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

# Exact-name filters must select one definition. Otherwise each duplicate CTest
# entry runs every definition of that name, multiplying execution and hiding
# which source owns a failure. Validate before writing the generated registry.
set(_seen_names "")
set(_seen_locations "")
foreach(_entry IN LISTS _cases)
  string(REPLACE "|" ";" _parts "${_entry}")
  list(GET _parts 0 _name)
  list(GET _parts 1 _file)
  list(GET _parts 2 _line)
  list(FIND _seen_names "${_name}" _previous)
  if(NOT _previous EQUAL -1)
    list(GET _seen_locations ${_previous} _first_location)
    message(FATAL_ERROR
      "duplicate zeroerr test name '${_name}': ${_first_location} and ${_file}:${_line}; test names must be unique")
  endif()
  list(APPEND _seen_names "${_name}")
  list(APPEND _seen_locations "${_file}:${_line}")
endforeach()

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
#      a single process (--file=<basename regex>, --quiet), as an opt-in.
#      Default `make test` excludes bundles to preserve process isolation.
#
# Test names and source basenames only contain [A-Za-z0-9_.], so no regex
# escaping is applied: backslash-escaping (e.g. \.) produced "Invalid escape
# sequence" in CMake's quoted-string parser (CMP0010) on some CMake versions.
set(_content "")
foreach(_basename IN LISTS _bundle_files)
  string(APPEND _content
    "add_test(\"bundle/${_basename}\" \"${ZEROERR_EXE}\" \"--quiet\" \"--file=.*${_basename}\")\n"
    "set_tests_properties(\"bundle/${_basename}\" PROPERTIES LABELS \"bundle;${ZEROERR_LABEL}\" WORKING_DIRECTORY \"${ZEROERR_WORKING_DIRECTORY}\"${_eve_test_env_property})\n")
  foreach(_entry IN LISTS _cases)
    string(REPLACE "|" ";" _parts "${_entry}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _file)
    if(_file STREQUAL _basename)
      # Keep source ownership on every per-case entry.  CI can then select the
      # cases from changed test translation units without relying on naming
      # conventions inside TEST_CASE strings. The link unit's own label is added
      # too, so `make test/<platform> DOMAIN=<domain>` and `ctest -L` can select
      # one domain; CTest labels are a list, so both coexist.
      set(_labels "source:${_basename}")
      if(DEFINED ZEROERR_LABEL AND NOT ZEROERR_LABEL STREQUAL "")
        list(APPEND _labels "${ZEROERR_LABEL}")
      endif()
      if(_name STREQUAL "ClassicScenes.perf.maxFps")
        # Full FPS sweeps stay opt-in: the benchmark label lets `make test`
        # exclude them (CTEST_BENCHMARK_SEL) while -L benchmark still selects them.
        list(APPEND _labels "benchmark")
      endif()
      endif()
      # CTest include files use classic add_test(name exe [args...]), not NAME/COMMAND keywords.
      string(APPEND _content
        "add_test(\"${_name}\" \"${ZEROERR_EXE}\" \"--testcase=^${_name}$\")\n"
        "set_tests_properties(\"${_name}\" PROPERTIES LABELS \"${_labels}\" WORKING_DIRECTORY \"${ZEROERR_WORKING_DIRECTORY}\"${_eve_test_env_property})\n")
      if(_name MATCHES "^ClassicScenes[.]")
        # These asset-dependent cases already return early with these diagnostics.
        # Expose that outcome as skipped rather than a zero-assertion pass.
        string(APPEND _content
          "set_tests_properties(\"${_name}\" PROPERTIES SKIP_REGULAR_EXPRESSION \"ClassicScenes.*: missing\")\n")
      endif()
      if(_name MATCHES "^resourceFormats\\.")
        # Helpers must receive TestContext, but fail closed if a future helper
        # logs an assertion without propagating it to zeroerr's exit status.
        string(APPEND _content
          "set_tests_properties(\"${_name}\" PROPERTIES FAIL_REGULAR_EXPRESSION \"Assertion Failed\")\n")
      endif()
    endif()
  endforeach()
endforeach()
file(WRITE "${CTEST_FILE}" "${_content}")
