# Independent Crowd contract tests; CTest can run this directory alone.
enable_testing()

add_executable(eve_crowd_check
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
foreach(_eve_crowd_source IN ITEMS crowd.cpp crowd_contacts.cpp crowd_interaction.cpp crowd_spawn.cpp crowd_avoidance.cpp)
    file(STRINGS "${CMAKE_SOURCE_DIR}/test/${_eve_crowd_source}" _eve_crowd_cases
        REGEX "^TEST_CASE\\(\"")
    foreach(_eve_crowd_case IN LISTS _eve_crowd_cases)
        string(REGEX MATCH "TEST_CASE\\(\"([^\"]+)\"" _eve_crowd_match "${_eve_crowd_case}")
        set(_eve_crowd_name "${CMAKE_MATCH_1}")
        add_test(NAME "profile.${_eve_crowd_name}" COMMAND eve_crowd_check
            "--testcase=^${_eve_crowd_name}$")
        set_tests_properties("profile.${_eve_crowd_name}" PROPERTIES LABELS
            "source:${_eve_crowd_source};crowd"
            FAIL_REGULAR_EXPRESSION "Assertion Failed" TIMEOUT 60)
    endforeach()
endforeach()

if(TARGET EVRts AND TARGET eve)
    add_executable(eve_rts_crowd_check
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
    foreach(_eve_rts_source IN ITEMS rts_orders.cpp rts_crowd.cpp rts_production.cpp rts_crowd_production.cpp rts_formation.cpp rts_groups.cpp rts_restore.cpp)
        file(STRINGS "${CMAKE_SOURCE_DIR}/test/${_eve_rts_source}" _eve_rts_cases REGEX "^TEST_CASE\\(\"")
        foreach(_eve_rts_case IN LISTS _eve_rts_cases)
            string(REGEX MATCH "TEST_CASE\\(\"([^\"]+)\"" _eve_rts_match "${_eve_rts_case}")
            set(_eve_rts_name "${CMAKE_MATCH_1}")
            add_test(NAME "profile.${_eve_rts_name}" COMMAND eve_rts_crowd_check "--testcase=^${_eve_rts_name}$")
            set_tests_properties("profile.${_eve_rts_name}" PROPERTIES LABELS "source:${_eve_rts_source};rts-crowd"
                FAIL_REGULAR_EXPRESSION "Assertion Failed" TIMEOUT 60)
        endforeach()
    endforeach()
endif()
