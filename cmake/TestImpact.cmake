# Build-derived test impact data. The module manifest remains the module truth;
# the File API reports the actual selected profile and link route.
include_guard(GLOBAL)

function(_eve_test_build_dependencies DIRECTORY OUTPUT)
  get_property(_targets DIRECTORY "${DIRECTORY}" PROPERTY BUILDSYSTEM_TARGETS)
  set(_result)
  foreach(_target IN LISTS _targets)
    get_target_property(_type "${_target}" TYPE)
    get_target_property(_excluded "${_target}" EXCLUDE_FROM_ALL)
    if(NOT _type STREQUAL "EXECUTABLE" OR _excluded)
      continue()
    endif()
    get_target_property(_sources "${_target}" SOURCES)
    get_target_property(_source_dir "${_target}" SOURCE_DIR)
    foreach(_source IN LISTS _sources)
      if(_source MATCHES "^\\$<")
        continue()
      endif()
      get_filename_component(_absolute "${_source}" ABSOLUTE BASE_DIR "${_source_dir}")
      if(_absolute MATCHES "/test/")
        list(APPEND _result "${_target}")
        break()
      endif()
    endforeach()
  endforeach()
  get_property(_children DIRECTORY "${DIRECTORY}" PROPERTY SUBDIRECTORIES)
  foreach(_child IN LISTS _children)
    _eve_test_build_dependencies("${_child}" _nested)
    list(APPEND _result ${_nested})
  endforeach()
  set(${OUTPUT} "${_result}" PARENT_SCOPE)
endfunction()

function(eve_enable_test_impact)
  cmake_parse_arguments(IMPACT "" "" "TARGETS" ${ARGN})
  if(CMAKE_CROSSCOMPILING)
    return()
  endif()
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  get_filename_component(_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/.cmake/api/v1/query/client-eve-test-impact")
  file(WRITE "${CMAKE_BINARY_DIR}/.cmake/api/v1/query/client-eve-test-impact/codemodel-v2" "")
  set(_all)
  if(COMMAND cmake_file_api)
    cmake_file_api(QUERY API_VERSION 1 CODEMODEL 2)
    set(_all ALL)
  else()
    file(GLOB _replies "${CMAKE_BINARY_DIR}/.cmake/api/v1/reply/index-*.json")
    if(_replies)
      set(_all ALL)
    else()
      message(STATUS "Test impact: CMake <3.27 needs one further configure to answer its File API query")
    endif()
  endif()
  set(_args)
  foreach(_target IN LISTS IMPACT_TARGETS)
    list(APPEND _args --target "${_target}")
  endforeach()
  string(JOIN "\n" _targets ${IMPACT_TARGETS})
  file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/test-impact/targets-$<CONFIG>.txt"
    CONTENT "${_targets}\n")
  # Run after discovery, not during configure: the compiled runner supplies the
  # active case identities (including platform branches and generator macros).
  # Static profile/codec test executables must also finish before CTest reads the
  # complete registration tree. Optional EXCLUDE_FROM_ALL proofs stay opt-in.
  _eve_test_build_dependencies("${CMAKE_SOURCE_DIR}" _dependencies)
  add_custom_target(eve_test_impact_index ${_all}
    COMMAND "${Python3_EXECUTABLE}" -X utf8 "${_root}/scripts/test_impact.py"
      index --build-dir "${CMAKE_BINARY_DIR}" --root "${_root}"
      --config "$<CONFIG>" ${_args}
    DEPENDS ${IMPACT_TARGETS} ${_dependencies}
    COMMENT "Indexing source dependencies and CTest cases"
    VERBATIM)
endfunction()
