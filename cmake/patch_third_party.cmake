# Idempotent git-apply wrapper used by ExternalProject PATCH_COMMAND.
#
# Usage:
#   cmake -DPATCH=<abs path to .patch>
#         -DPATCH_DIR=<git checkout root the patch paths are relative to>
#         -P cmake/patch_third_party.cmake
#
# The patch is applied with `git apply` inside PATCH_DIR. If it is already
# applied (reverse-apply succeeds) the step is a no-op so rebuilds do not fail.
# If the working tree drifted so the patch no longer applies, configure/build
# fails loudly instead of silently compiling different third-party code.
# In particular, do not relax whitespace matching here: that can turn a real
# source drift into an apparently successful but different third-party build.
#
# Line endings: a patch file is stored with LF, but a vendored checkout is not
# line-ending uniform (external/ECS.hpp commits CRLF). git apply bridges that
# difference only when core.autocrlf matches the host that generated the patch,
# so the same patch applies on Windows and fails on Linux. Normalize the files
# this patch touches to LF first (below), which keeps context matching exact --
# unlike --ignore-whitespace, drift still fails loudly.

if(NOT DEFINED PATCH OR NOT DEFINED PATCH_DIR)
    message(FATAL_ERROR "patch_third_party.cmake requires -DPATCH=... and -DPATCH_DIR=...")
endif()

get_filename_component(PATCH_ABS "${PATCH}" ABSOLUTE)
get_filename_component(PATCH_DIR_ABS "${PATCH_DIR}" ABSOLUTE)
if(NOT EXISTS "${PATCH_ABS}")
    message(FATAL_ERROR "Third-party patch file not found: ${PATCH_ABS}")
endif()
if(NOT IS_DIRECTORY "${PATCH_DIR_ABS}")
    message(FATAL_ERROR "Third-party patch target dir not found: ${PATCH_DIR_ABS}")
endif()

find_package(Git QUIET)
if(NOT GIT_EXECUTABLE)
    message(FATAL_ERROR "git is required to apply third-party patches")
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${PATCH_DIR_ABS}" rev-parse --show-toplevel
    RESULT_VARIABLE _eve_patch_repo_result
    OUTPUT_VARIABLE _eve_patch_repo_root
    ERROR_VARIABLE _eve_patch_repo_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _eve_patch_repo_result EQUAL 0)
    message(FATAL_ERROR
        "Third-party patch target is not a git checkout: ${PATCH_DIR_ABS}\n"
        "${_eve_patch_repo_error}")
endif()

# Normalize the line endings of every file this patch touches before looking at
# whether it is already applied, so the LF patch matches a CRLF vendor file.
file(READ "${PATCH_ABS}" _eve_patch_text)
string(REPLACE "\n" ";" _eve_patch_lines "${_eve_patch_text}")
foreach(_eve_patch_line IN LISTS _eve_patch_lines)
    if(_eve_patch_line MATCHES "^--- a/([^\t]+)")
        set(_eve_patch_target "${PATCH_DIR_ABS}/${CMAKE_MATCH_1}")
        if(EXISTS "${_eve_patch_target}" AND NOT IS_DIRECTORY "${_eve_patch_target}")
            file(READ "${_eve_patch_target}" _eve_target_text)
            string(REPLACE "\r\n" "\n" _eve_target_lf "${_eve_target_text}")
            if(NOT _eve_target_lf STREQUAL _eve_target_text)
                file(WRITE "${_eve_patch_target}" "${_eve_target_lf}")
                message(STATUS "Normalized line endings to LF: ${_eve_patch_target}")
            endif()
        endif()
    endif()
endforeach()

# Already applied? `git apply --reverse --check` succeeds only then.
execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --reverse --check --unidiff-zero "${PATCH_ABS}"
    WORKING_DIRECTORY "${PATCH_DIR_ABS}"
    RESULT_VARIABLE _eve_patch_reverse
    OUTPUT_VARIABLE _eve_patch_reverse_out
    ERROR_VARIABLE _eve_patch_reverse_err)
if(_eve_patch_reverse EQUAL 0)
    message(STATUS "Third-party patch already applied: ${PATCH_ABS}")
    return()
endif()

# Apply cleanly, or fail loudly so a stale patch cannot silently bit-rot.
execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --check --unidiff-zero "${PATCH_ABS}"
    WORKING_DIRECTORY "${PATCH_DIR_ABS}"
    RESULT_VARIABLE _eve_patch_check
    OUTPUT_VARIABLE _eve_patch_check_out
    ERROR_VARIABLE _eve_patch_check_err)
if(NOT _eve_patch_check EQUAL 0)
    message(FATAL_ERROR
        "Third-party patch no longer applies cleanly: ${PATCH_ABS}\n"
        "The vendored source changed upstream or the checkout is polluted; update or drop the patch.\n"
        "git apply --check output:\n${_eve_patch_check_out}${_eve_patch_check_err}")
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --unidiff-zero "${PATCH_ABS}"
    WORKING_DIRECTORY "${PATCH_DIR_ABS}"
    RESULT_VARIABLE _eve_patch_result
    OUTPUT_VARIABLE _eve_patch_out
    ERROR_VARIABLE _eve_patch_err)
if(NOT _eve_patch_result EQUAL 0)
    message(FATAL_ERROR
        "git apply failed for ${PATCH_ABS} (exit ${_eve_patch_result})\n"
        "${_eve_patch_out}${_eve_patch_err}")
endif()
message(STATUS "Applied third-party patch: ${PATCH_ABS}")
