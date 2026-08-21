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

# Already applied? `git apply --reverse --check` succeeds only then.
execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${PATCH_ABS}"
    WORKING_DIRECTORY "${PATCH_DIR_ABS}"
    RESULT_VARIABLE _eve_patch_reverse
    OUTPUT_QUIET ERROR_QUIET)
if(_eve_patch_reverse EQUAL 0)
    message(STATUS "Third-party patch already applied: ${PATCH_ABS}")
    return()
endif()

# Apply cleanly, or fail loudly so a stale patch cannot silently bit-rot.
execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --check "${PATCH_ABS}"
    WORKING_DIRECTORY "${PATCH_DIR_ABS}"
    RESULT_VARIABLE _eve_patch_check
    OUTPUT_QUIET ERROR_QUIET)
if(NOT _eve_patch_check EQUAL 0)
    message(FATAL_ERROR
        "Third-party patch no longer applies cleanly: ${PATCH_ABS}\n"
        "The vendored source changed upstream; update or drop the patch.")
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply "${PATCH_ABS}"
    WORKING_DIRECTORY "${PATCH_DIR_ABS}"
    RESULT_VARIABLE _eve_patch_result)
if(NOT _eve_patch_result EQUAL 0)
    message(FATAL_ERROR "git apply failed for ${PATCH_ABS} (exit ${_eve_patch_result})")
endif()
message(STATUS "Applied third-party patch: ${PATCH_ABS}")
