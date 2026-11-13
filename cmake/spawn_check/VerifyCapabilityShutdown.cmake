# Verify only after the child and its DLL static owners have finished teardown.
# The child retains service/listener assertions and emits this versioned record
# only after one successful component destruction. CTest owns the 30s deadline.
if(NOT DEFINED PROBE_COMMAND OR PROBE_COMMAND STREQUAL "")
    message(FATAL_ERROR "Capability shutdown probe command is required")
endif()
execute_process(COMMAND ${PROBE_COMMAND}
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
set(log "${output}${error}")
message("${log}")
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Capability shutdown child failed: ${status}")
endif()
string(REGEX MATCHALL "eve[.]spawn-capability-shutdown/[^\r\n]+" records "${log}")
list(LENGTH records count)
if(NOT count EQUAL 1)
    message(FATAL_ERROR "Expected exactly one capability destruction record; found ${count}")
endif()
list(GET records 0 record)
if(NOT record STREQUAL "eve.spawn-capability-shutdown/v1 destroyed=1")
    message(FATAL_ERROR "Invalid capability destruction record: ${record}")
endif()
message("Capability shutdown verified after process exit: destroyed=1")
