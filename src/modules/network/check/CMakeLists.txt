enable_testing()
include("${CMAKE_SOURCE_DIR}/cmake/host_backtrace.cmake")
set(_eve_network_links)
foreach(_eve_network_module network data platform_event common)
    if(EVENGINE_MODULE_LINKAGE STREQUAL "SHARED")
        list(APPEND _eve_network_links ${EVE_MODULE_${_eve_network_module}_LINK_GROUP})
    else()
        list(APPEND _eve_network_links ${EVE_MODULE_${_eve_network_module}_LIB})
    endif()
endforeach()
list(REMOVE_DUPLICATES _eve_network_links)
set(_eve_network_test "${CMAKE_SOURCE_DIR}/test/integration/network/network_http_tls_tests.cpp")
eve_add_test_executable(eve_network_check
    "${CMAKE_SOURCE_DIR}/test/profile/network_main.cpp" "${_eve_network_test}")
target_compile_features(eve_network_check PRIVATE cxx_std_20)
eve_thirdparty_libs(_eve_network_libraries ${EVE_THIRDPARTY_GROUPS})
eve_append_system_libraries(_eve_network_system_libraries)
target_link_libraries(eve_network_check PRIVATE ${_eve_network_links} zeroerr eve_engine_includes
    ${_eve_network_libraries} ${_eve_network_system_libraries})
if(WIN32)
    target_link_libraries(eve_network_check PRIVATE EVBacktrace)
    target_compile_definitions(eve_network_check PRIVATE POCO_STATIC POCO_NO_AUTOMATIC_LIBS NOMINMAX)
endif()
if(TARGET eve_third_party_runtime)
    # This hostless check still uses the real SDL event queue through Foundation.
    add_dependencies(eve_network_check eve_third_party_runtime)
endif()
set_target_properties(eve_network_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_compile_options(eve_network_check PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/UZEROERR_NO_ASSERT>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UZEROERR_NO_ASSERT>)
eve_test_source_case_names("${_eve_network_test}" _eve_network_cases eve_network_check)
foreach(_eve_network_case IN LISTS _eve_network_cases)
    add_test(NAME "profile.${_eve_network_case}" COMMAND eve_network_check
        "--testcase=^${_eve_network_case}$")
    set_tests_properties("profile.${_eve_network_case}" PROPERTIES
        LABELS network TIMEOUT 30 FAIL_REGULAR_EXPRESSION "Assertion Failed")
    eve_label_test_case("profile.${_eve_network_case}" "${_eve_network_test}" "${_eve_network_case}")
endforeach()
