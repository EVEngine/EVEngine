# Independent Crowd contract tests; CTest can run this directory alone.
enable_testing()
include("${CMAKE_SOURCE_DIR}/cmake/host_backtrace.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/TestLevels.cmake")
eve_configure_test_levels(EVE_TEST_LEVEL_FILE)
include("${EVE_TEST_LEVEL_FILE}")
set(_eve_crowd_link_targets EVCrowd EVCommon)
if(EVENGINE_MODULE_LINKAGE STREQUAL "SHARED")
    # Consume the manifest-derived owning group, never the group's OBJECT producers.
    set(_eve_crowd_link_targets ${EVE_MODULE_crowd_LINK_GROUP})
endif()

eve_add_test_executable(eve_crowd_check
    "${CMAKE_SOURCE_DIR}/test/profile/crowd_main.cpp"
    "${CMAKE_SOURCE_DIR}/test/result.cpp"
    "${CMAKE_SOURCE_DIR}/test/crowd.cpp"
    "${CMAKE_SOURCE_DIR}/test/crowd_contacts.cpp"
    "${CMAKE_SOURCE_DIR}/test/crowd_interaction.cpp"
    "${CMAKE_SOURCE_DIR}/test/crowd_spawn.cpp"
    "${CMAKE_SOURCE_DIR}/test/crowd_budget.cpp"
    "${CMAKE_SOURCE_DIR}/test/crowd_spatial.cpp"
    "${CMAKE_SOURCE_DIR}/test/crowd_contact_pairs.cpp"
    "${CMAKE_SOURCE_DIR}/test/crowd_scheduler.cpp"
    "${CMAKE_SOURCE_DIR}/test/crowd_registry.cpp"
    "${CMAKE_SOURCE_DIR}/test/crowd_policy.cpp"
    "${CMAKE_SOURCE_DIR}/test/crowd_advance.cpp"
    "${CMAKE_SOURCE_DIR}/test/crowd_snapshot.cpp"
    "${CMAKE_SOURCE_DIR}/test/crowd_ties.cpp"
    "${CMAKE_SOURCE_DIR}/test/crowd_revision.cpp"
    "${CMAKE_SOURCE_DIR}/test/crowd_navigation.cpp"
    "${CMAKE_SOURCE_DIR}/test/crowd_navigation_script.cpp"
    "${CMAKE_SOURCE_DIR}/test/crowd_surface.cpp"
    "${CMAKE_SOURCE_DIR}/test/crowd_flow_cache.cpp"
    "${CMAKE_SOURCE_DIR}/test/integration/crowd-runtime/crowd_avoidance.cpp")
target_compile_features(eve_crowd_check PRIVATE cxx_std_20)
eve_thirdparty_libs(_eve_crowd_libraries squirrel)
eve_append_system_libraries(_eve_crowd_system_libraries)
target_link_libraries(eve_crowd_check PRIVATE ${_eve_crowd_link_targets} zeroerr eve_engine_includes
    ${_eve_crowd_libraries} ${_eve_crowd_system_libraries})
if(WIN32)
    target_link_libraries(eve_crowd_check PRIVATE EVBacktrace)
endif()
set_target_properties(eve_crowd_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Global new interception is process-wide only for the OBJECT consumer; a DLL uses its own CRT symbols.
if(EVENGINE_MODULE_LINKAGE STREQUAL "OBJECT")
    add_executable(eve_crowd_update_allocation_check "${CMAKE_SOURCE_DIR}/test/profile/crowd_update_allocation.cpp")
    target_compile_features(eve_crowd_update_allocation_check PRIVATE cxx_std_20)
    target_link_libraries(eve_crowd_update_allocation_check PRIVATE ${_eve_crowd_link_targets}
        zeroerr eve_engine_includes ${_eve_crowd_libraries} ${_eve_crowd_system_libraries})
    set_target_properties(eve_crowd_update_allocation_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(NAME profile.crowd.updatePlan.allocationContract COMMAND eve_crowd_update_allocation_check)
    set_tests_properties(profile.crowd.updatePlan.allocationContract PROPERTIES TIMEOUT 60 LABELS "crowd;allocation-contract")
endif()
# Explicit benchmark target; it is never registered as a timing-sensitive CTest.
add_executable(eve_crowd_benchmark "${CMAKE_SOURCE_DIR}/test/profile/crowd_benchmark.cpp")
target_compile_features(eve_crowd_benchmark PRIVATE cxx_std_20)
target_link_libraries(eve_crowd_benchmark PRIVATE ${_eve_crowd_link_targets} zeroerr eve_engine_includes
    ${_eve_crowd_libraries} ${_eve_crowd_system_libraries})
if(TARGET EVCrowdThread)
    target_compile_definitions(eve_crowd_benchmark PRIVATE EV_CROWD_BENCH_THREAD=1)
    if(EVENGINE_MODULE_LINKAGE STREQUAL "SHARED")
        target_link_libraries(eve_crowd_benchmark PRIVATE ${EVE_MODULE_crowd_thread_LINK_GROUP})
    else()
        target_link_libraries(eve_crowd_benchmark PRIVATE EVCrowdThread EVThread)
    endif()
endif()
if(WIN32)
    target_link_libraries(eve_crowd_benchmark PRIVATE EVBacktrace)
endif()
set_target_properties(eve_crowd_benchmark PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Keep the normal process-isolated test contract, even for this small target.
set(_eve_crowd_sources result.cpp crowd.cpp crowd_contacts.cpp crowd_interaction.cpp crowd_spawn.cpp integration/crowd-runtime/crowd_avoidance.cpp crowd_budget.cpp crowd_spatial.cpp crowd_contact_pairs.cpp crowd_registry.cpp crowd_policy.cpp crowd_advance.cpp crowd_snapshot.cpp crowd_ties.cpp crowd_revision.cpp crowd_navigation.cpp crowd_navigation_script.cpp crowd_surface.cpp crowd_flow_cache.cpp crowd_scheduler.cpp)
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
        "${CMAKE_SOURCE_DIR}/test/rts_orders.cpp"
        "${CMAKE_SOURCE_DIR}/test/integration/rts-crowd/rts_crowd.cpp"
        "${CMAKE_SOURCE_DIR}/test/rts_crowd_zero_delta.cpp"
        "${CMAKE_SOURCE_DIR}/test/rts_combat_suppression_aura.cpp"
        "${CMAKE_SOURCE_DIR}/test/rts_combat_3.cpp"
        "${CMAKE_SOURCE_DIR}/test/sensing_spatial_cells.cpp"
        "${CMAKE_SOURCE_DIR}/test/rts_crowd_registry.cpp"
        "${CMAKE_SOURCE_DIR}/test/rts_production.cpp"
        "${CMAKE_SOURCE_DIR}/test/integration/rts-crowd/rts_crowd_production.cpp"
        "${CMAKE_SOURCE_DIR}/test/integration/rts-crowd/rts_formation.cpp"
        "${CMAKE_SOURCE_DIR}/test/integration/rts-crowd/rts_groups.cpp"
        "${CMAKE_SOURCE_DIR}/test/rts_restore.cpp"
        "${CMAKE_SOURCE_DIR}/test/map_navigation_snapshot.cpp"
        "${CMAKE_SOURCE_DIR}/test/rts_crowd_surface_adapter.cpp"
        "${CMAKE_SOURCE_DIR}/test/rts_crowd_flow_navigation.cpp")
    target_sources(eve_rts_crowd_check PRIVATE "${CMAKE_SOURCE_DIR}/test/rts_crowd_flow_groups.cpp" "${CMAKE_SOURCE_DIR}/test/rts_group_attack_move.cpp" "${CMAKE_SOURCE_DIR}/test/rts_attack_slots.cpp")
    if(NOT EVENGINE_MODULE_LINKAGE STREQUAL "SHARED")
        # OBJECT selects only the registry required by asset-import registration.
        # SHARED imports that same registry from its owning Foundation DLL.
        target_sources(eve_rts_crowd_check PRIVATE "${CMAKE_SOURCE_DIR}/src/engine/cmdline/cmdline.cpp")
    endif()
    target_sources(eve_rts_crowd_check PRIVATE "${CMAKE_SOURCE_DIR}/test/rts_performance_semantics.cpp")
    target_compile_features(eve_rts_crowd_check PRIVATE cxx_std_20)
    set_target_properties(eve_rts_crowd_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    get_target_property(_eve_rts_host_libraries eve LINK_LIBRARIES)
    # The composition test has its own main and does not execute CLI/devtool commands.
    list(REMOVE_ITEM _eve_rts_host_libraries EVCmdLine)
    target_link_libraries(eve_rts_crowd_check PRIVATE ${_eve_rts_host_libraries} zeroerr eve_engine_includes)
    if(WIN32)
        target_link_libraries(eve_rts_crowd_check PRIVATE EVBacktrace)
    endif()
    # Full composed battle runner, separate from timing-sensitive test discovery.
    add_executable(eve_rts_battle_benchmark "${CMAKE_SOURCE_DIR}/test/profile/rts_battle_benchmark.cpp")
    target_compile_features(eve_rts_battle_benchmark PRIVATE cxx_std_20)
    target_link_libraries(eve_rts_battle_benchmark PRIVATE ${_eve_rts_host_libraries} zeroerr eve_engine_includes)
    if(TARGET EVCrowdThread)
        target_compile_definitions(eve_rts_battle_benchmark PRIVATE EV_RTS_BENCH_THREAD=1)
        if(EVENGINE_MODULE_LINKAGE STREQUAL "SHARED")
            target_link_libraries(eve_rts_battle_benchmark PRIVATE ${EVE_MODULE_crowd_thread_LINK_GROUP})
        else()
            target_link_libraries(eve_rts_battle_benchmark PRIVATE EVCrowdThread EVThread)
        endif()
    endif()
    if(NOT EVENGINE_MODULE_LINKAGE STREQUAL "SHARED")
        target_sources(eve_rts_battle_benchmark PRIVATE "${CMAKE_SOURCE_DIR}/src/engine/cmdline/cmdline.cpp")
    endif()
    if(WIN32)
        target_link_libraries(eve_rts_battle_benchmark PRIVATE EVBacktrace)
    endif()
    set_target_properties(eve_rts_battle_benchmark PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    set(_eve_rts_sources rts_performance_semantics.cpp rts_attack_slots.cpp rts_group_attack_move.cpp rts_orders.cpp integration/rts-crowd/rts_crowd.cpp rts_crowd_zero_delta.cpp rts_combat_suppression_aura.cpp rts_combat_3.cpp sensing_spatial_cells.cpp rts_crowd_registry.cpp rts_production.cpp integration/rts-crowd/rts_crowd_production.cpp integration/rts-crowd/rts_formation.cpp integration/rts-crowd/rts_groups.cpp rts_restore.cpp map_navigation_snapshot.cpp rts_crowd_surface_adapter.cpp rts_crowd_flow_navigation.cpp rts_crowd_flow_groups.cpp)
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

add_executable(eve_result_policy_check
    "${CMAKE_SOURCE_DIR}/test/profile/crowd_main.cpp"
    "${CMAKE_SOURCE_DIR}/test/profile/result_policy_producer.cpp"
    "${CMAKE_SOURCE_DIR}/test/profile/result_policy_consumer.cpp")
target_compile_features(eve_result_policy_check PRIVATE cxx_std_20)
target_link_libraries(eve_result_policy_check PRIVATE ${_eve_crowd_link_targets}
    zeroerr eve_engine_includes ${_eve_crowd_libraries} ${_eve_crowd_system_libraries})
if(WIN32)
    target_link_libraries(eve_result_policy_check PRIVATE EVBacktrace)
endif()
set_target_properties(eve_result_policy_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_test(NAME profile.result.assertionPolicyInterop COMMAND eve_result_policy_check)
set_tests_properties(profile.result.assertionPolicyInterop PROPERTIES TIMEOUT 60
    FAIL_REGULAR_EXPRESSION "Assertion Failed" LABELS "crowd;result-policy")

# Match test/CMakeLists.txt: Release must retain zeroerr setup calls and assertions.
# Benchmark/engine targets keep the production assertion policy.
foreach(_eve_crowd_assert_target eve_result_policy_check eve_crowd_check eve_rts_crowd_check eve_crowd_update_allocation_check)
    if(TARGET ${_eve_crowd_assert_target})
        target_compile_options(${_eve_crowd_assert_target} PRIVATE
            $<$<CXX_COMPILER_ID:MSVC>:/UZEROERR_NO_ASSERT>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UZEROERR_NO_ASSERT>)
    endif()
endforeach()
