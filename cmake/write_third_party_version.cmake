# Writes third-party-version.txt into a third-party install prefix.
#
# Usage:
#   cmake -DPREFIX=<install prefix> -DTP_DIR=<third-party source root>
#         [-DPLATFORM=<platform>] [-DBUILD_TYPE=<build type>]
#         -P cmake/write_third_party_version.cmake
#
# The stamp records the aggregate commit, dirty flag, submodule commits and
# build time, so a prebuilt tree can be checked against the source checkout
# (see EVENGINE_THIRD_PARTY_BINARY_DIR handling in CMakeLists.txt) and so
# eve's build info can name the exact third-party version.

if(NOT DEFINED PREFIX OR NOT DEFINED TP_DIR)
    message(FATAL_ERROR "write_third_party_version.cmake requires -DPREFIX=... and -DTP_DIR=...")
endif()

find_package(Git QUIET)

set(_eve_tp_aggregate "unknown")
set(_eve_tp_dirty 0)
set(_eve_tp_submodules "submodules: unknown")

if(GIT_EXECUTABLE)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${TP_DIR}"
        RESULT_VARIABLE _eve_tp_head_result
        OUTPUT_VARIABLE _eve_tp_head
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_eve_tp_head_result EQUAL 0 AND _eve_tp_head)
        set(_eve_tp_aggregate "${_eve_tp_head}")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" status --porcelain
        WORKING_DIRECTORY "${TP_DIR}"
        RESULT_VARIABLE _eve_tp_dirty_result
        OUTPUT_VARIABLE _eve_tp_dirty_out
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_eve_tp_dirty_result EQUAL 0 AND _eve_tp_dirty_out)
        set(_eve_tp_dirty 1)
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" submodule status --recursive
        WORKING_DIRECTORY "${TP_DIR}"
        RESULT_VARIABLE _eve_tp_sub_result
        OUTPUT_VARIABLE _eve_tp_sub_out
        ERROR_QUIET)
    if(_eve_tp_sub_result EQUAL 0)
        set(_eve_tp_submodules "submodules:\n${_eve_tp_sub_out}")
    endif()
endif()

string(TIMESTAMP _eve_tp_built "%Y-%m-%d %H:%M:%S %z")

set(_eve_tp_content "EVEngine third-party build info\n")
string(APPEND _eve_tp_content "platform=${PLATFORM}\n")
string(APPEND _eve_tp_content "build_type=${BUILD_TYPE}\n")
string(APPEND _eve_tp_content "aggregate=${_eve_tp_aggregate}\n")
string(APPEND _eve_tp_content "dirty=${_eve_tp_dirty}\n")
string(APPEND _eve_tp_content "built=${_eve_tp_built}\n")
string(APPEND _eve_tp_content "${_eve_tp_submodules}")

file(WRITE "${PREFIX}/third-party-version.txt" "${_eve_tp_content}")
message(STATUS "Wrote ${PREFIX}/third-party-version.txt (aggregate=${_eve_tp_aggregate})")
