# Independent Crowd contract tests; CTest can run this directory alone.
enable_testing()
include("${CMAKE_SOURCE_DIR}/cmake/TestLevels.cmake")
eve_configure_test_levels(EVE_TEST_LEVEL_FILE)
include("${EVE_TEST_LEVEL_FILE}")

eve_add_test_executable(eve_crowd_check
    "${CMAKE_SOURCE_DIR}/test/profile/crowd_main.cpp"
    "${CMAKE_SOURCE_DIR}/test/crowd.cpp"
    "${CMAKE_SOURCE_DIR}/test/crowd_contacts.cpp"
    "${CMAKE_SOURCE_DIR}/test/crowd_interaction.cpp"
    "${CMAKE_SOURCE_DIR}/test/crowd_spawn.cpp"
    "${CMAKE_SOURCE_DIR}/test/crowd_avoidance.cpp")
target_compile_features(eve_crowd_check PRIVATE cxx_std_20)
eve_thirdparty_libs(_eve_crowd_libraries squirrel)
eve_append_system_libraries(_eve_crowd_system_libraries)
target_link_libraries(eve_crowd_check PRIVATE EVCrowd EVCommon zeroerr eve_engine_includes
    ${_eve_crowd_libraries} ${_eve_crowd_system_libraries})
if(WIN32)
    target_link_libraries(eve_crowd_check PRIVATE EVBacktrace)
endif()
# Keep the normal process-isolated test contract, even for this small target.
set(_eve_crowd_sources crowd.cpp crowd_contacts.cpp crowd_interaction.cpp crowd_spawn.cpp crowd_avoidance.cpp)
eve_resolve_test_sources(_eve_crowd_sources ${_eve_crowd_sources})
foreach(_eve_crowd_source IN LISTS _eve_crowd_sources)
    eve_test_source_case_names("${_eve_crowd_source}" _eve_crowd_cases eve_crowd_check)
    foreach(_eve_crowd_name IN LISTS _eve_crowd_cases)
        add_test(NAME "profile.${_eve_crowd_name}" COMMAND eve_crowd_check
            "--testcase=^${_eve_crowd_name}$")
        set_tests_properties("profile.${_eve_crowd_name}" PROPERTIES LABELS
            "source:${_eve_crowd_source};crowd"
            FAIL_REGULAR_EXPRESSION "Assertion Failed" TIMEOUT 60)
        eve_label_test_case("profile.${_eve_crowd_name}" "${_eve_crowd_source}" "${_eve_crowd_name}")
    endforeach()
endforeach()

if(TARGET EVRts AND TARGET eve)
    eve_add_test_executable(eve_rts_crowd_check
        "${CMAKE_SOURCE_DIR}/test/profile/crowd_main.cpp"
        # Asset import registers a command with the real registry; do not bring
        # the unrelated CLI command implementations and their devtools providers.
        "${CMAKE_SOURCE_DIR}/src/engine/cmdline/cmdline.cpp"
        "${CMAKE_SOURCE_DIR}/test/rts_orders.cpp"
        "${CMAKE_SOURCE_DIR}/test/rts_crowd.cpp"
        "${CMAKE_SOURCE_DIR}/test/rts_production.cpp"
        "${CMAKE_SOURCE_DIR}/test/rts_crowd_production.cpp"
        "${CMAKE_SOURCE_DIR}/test/rts_formation.cpp"
        "${CMAKE_SOURCE_DIR}/test/rts_groups.cpp"
        "${CMAKE_SOURCE_DIR}/test/rts_restore.cpp")
    target_compile_features(eve_rts_crowd_check PRIVATE cxx_std_20)
    get_target_property(_eve_rts_host_libraries eve LINK_LIBRARIES)
    # The composition test has its own main and does not execute CLI/devtool commands.
    list(REMOVE_ITEM _eve_rts_host_libraries EVCmdLine)
    target_link_libraries(eve_rts_crowd_check PRIVATE ${_eve_rts_host_libraries} zeroerr eve_engine_includes)
    if(WIN32)
        target_link_libraries(eve_rts_crowd_check PRIVATE EVBacktrace)
    endif()
    set(_eve_rts_sources rts_orders.cpp rts_crowd.cpp rts_production.cpp rts_crowd_production.cpp rts_formation.cpp rts_groups.cpp rts_restore.cpp)
eve_resolve_test_sources(_eve_rts_sources ${_eve_rts_sources})
foreach(_eve_rts_source IN LISTS _eve_rts_sources)
        eve_test_source_case_names("${_eve_rts_source}" _eve_rts_cases eve_rts_crowd_check)
        foreach(_eve_rts_name IN LISTS _eve_rts_cases)
            add_test(NAME "profile.${_eve_rts_name}" COMMAND eve_rts_crowd_check "--testcase=^${_eve_rts_name}$")
            set_tests_properties("profile.${_eve_rts_name}" PROPERTIES LABELS "source:${_eve_rts_source};rts-crowd"
                FAIL_REGULAR_EXPRESSION "Assertion Failed" TIMEOUT 60)
            eve_label_test_case("profile.${_eve_rts_name}" "${_eve_rts_source}" "${_eve_rts_name}")
        endforeach()
    endforeach()
endif()
