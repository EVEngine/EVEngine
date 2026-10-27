# zeroerr can report warnings with exit status zero. Keep this fixture fail-closed.
file(STRINGS "${TEST_FILE}" registrations REGEX "^add_test\\(\"(physics\\.|carrierComposition\\.|carrierWorldSweep\\.)")
foreach(line IN LISTS registrations)
  string(REGEX MATCH "add_test\\(\"([^\"]+)\"" match "${line}")
  file(APPEND "${TEST_FILE}" "set_tests_properties(\"${CMAKE_MATCH_1}\" PROPERTIES FAIL_REGULAR_EXPRESSION \"Assertion Failed\" PASS_REGULAR_EXPRESSION \"TEST CASE:[ ]+1[ ]+0[ ]+0[ ]+0\")\n")
endforeach()
