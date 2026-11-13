# Use the real client link contract, including canonical map/graphics/filesystem dependencies.
enable_testing()
get_target_property(_map_spawn_host_libraries eve LINK_LIBRARIES)
# Reuse the common-owned suite with its source-origin compilation boundary.
eve_add_test_executable(eve_listener_staging_check
    "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
    "${CMAKE_SOURCE_DIR}/test/unit/common/common_tests.cpp")
target_compile_features(eve_listener_staging_check PRIVATE cxx_std_20)
target_compile_definitions(eve_listener_staging_check PRIVATE EVE_TEST_FILTER_ORIGINS=1 EVE_TEST_ORIGIN_capability=1)
target_link_libraries(eve_listener_staging_check PRIVATE ${_map_spawn_host_libraries} zeroerr eve_engine_includes)
eve_test_source_case_names(common_tests.cpp _listener_cases eve_listener_staging_check)
foreach(_case IN LISTS _listener_cases)
    if(_case MATCHES "^common[.]capability[.]")
        add_test(NAME "profile.listener_staging.${_case}" COMMAND eve_listener_staging_check "--testcase=^${_case}$" --no-color)
        set_tests_properties("profile.listener_staging.${_case}" PROPERTIES TIMEOUT 30
            PASS_REGULAR_EXPRESSION "TEST CASE: +1 +0 +0 +0" FAIL_REGULAR_EXPRESSION "Assertion Failed"
            LABELS "common;capability-contract")
        eve_label_test_case("profile.listener_staging.${_case}" common_tests.cpp "${_case}")
    endif()
endforeach()
# Combat owns native policy capture; retain the existing canonical damage contracts as consumers.
eve_add_test_executable(eve_damage_capture_check
    "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
    "${CMAKE_SOURCE_DIR}/test/unit/combat/combat_tests.cpp"
    "${CMAKE_SOURCE_DIR}/test/integration/combat-presentation/combat_settlement.cpp"
    "${CMAKE_SOURCE_DIR}/test/unit/settlement/settlement_simulation_rules.cpp")
target_compile_features(eve_damage_capture_check PRIVATE cxx_std_20)
target_compile_definitions(eve_damage_capture_check PRIVATE EVE_TEST_FILTER_ORIGINS=1
    EVE_TEST_ORIGIN_combat_damage_capture=1 EVE_TEST_ORIGIN_settlement_rules=1 EVE_TEST_ORIGIN_settlement_replay=1)
target_link_libraries(eve_damage_capture_check PRIVATE ${_map_spawn_host_libraries} zeroerr eve_engine_includes)
foreach(_origin combat_tests.cpp combat_settlement.cpp settlement_simulation_rules.cpp)
    eve_test_source_case_names(${_origin} _damage_cases eve_damage_capture_check)
    foreach(_case IN LISTS _damage_cases)
        if(_case MATCHES "^combat[.]damageCapture[.]" OR _case MATCHES "^combatSettlement[.]" OR _case MATCHES "^settlement")
            add_test(NAME "profile.damage_capture.${_case}" COMMAND eve_damage_capture_check "--testcase=^${_case}$" --no-color)
            set_tests_properties("profile.damage_capture.${_case}" PROPERTIES TIMEOUT 30
                PASS_REGULAR_EXPRESSION "TEST CASE: +1 +0 +0 +0" FAIL_REGULAR_EXPRESSION "Assertion Failed"
                LABELS "combat;checkpoint-contract")
            eve_label_test_case("profile.damage_capture.${_case}" ${_origin} "${_case}")
        endif()
    endforeach()
endforeach()
# Exercise the definitions-owned canonical suite, including detached capture semantics.
eve_add_test_executable(eve_definitions_capture_check
    "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
    "${CMAKE_SOURCE_DIR}/test/unit/definitions/definitions_unit.cpp")
target_compile_features(eve_definitions_capture_check PRIVATE cxx_std_20)
target_link_libraries(eve_definitions_capture_check PRIVATE ${_map_spawn_host_libraries} zeroerr eve_engine_includes)
eve_test_source_case_names(definitions_unit.cpp _definitions_cases eve_definitions_capture_check)
foreach(_case IN LISTS _definitions_cases)
    add_test(NAME "profile.definitions_capture.${_case}" COMMAND eve_definitions_capture_check "--testcase=^${_case}$" --no-color)
    set_tests_properties("profile.definitions_capture.${_case}" PROPERTIES TIMEOUT 30
        PASS_REGULAR_EXPRESSION "TEST CASE: +1 +0 +0 +0" FAIL_REGULAR_EXPRESSION "Assertion Failed"
        LABELS "definitions;checkpoint-contract")
    eve_label_test_case("profile.definitions_capture.${_case}" definitions_unit.cpp "${_case}")
endforeach()
set(_map_spawn_sources "${CMAKE_SOURCE_DIR}/test/integration/map_spawn/map_spawn_load.cpp")
set(_map_spawn_origins map_spawn_load.cpp)
if(TARGET EVRPGSpawn)
    list(APPEND _map_spawn_sources "${CMAKE_SOURCE_DIR}/test/integration/map_spawn/map_spawn_space.cpp")
    list(APPEND _map_spawn_origins map_spawn_space.cpp)
endif()

if(TARGET EVMapRTSSpawn AND EVENGINE_MODULE_LINKAGE STREQUAL "OBJECT")
    add_executable(eve_map_rts_runtime_allocation_check
        "${CMAKE_SOURCE_DIR}/test/profile/map_rts_runtime_allocation.cpp")
    target_compile_features(eve_map_rts_runtime_allocation_check PRIVATE cxx_std_20)
    target_compile_definitions(eve_map_rts_runtime_allocation_check PRIVATE
        EVE_RTSSPAWN_EXAMPLE_DIR="${CMAKE_SOURCE_DIR}/examples/spawn-rts-reinforcements")
    target_link_libraries(eve_map_rts_runtime_allocation_check PRIVATE
        ${_map_spawn_host_libraries} zeroerr eve_engine_includes)
    foreach(_stage runtime-seal runtime-parse handoff-capture handoff-seal handoff-parse tracker-seal tracker-parse navigation-encode navigation-decode unit-attributes-seal unit-attributes-parse projection-encode projection-prepare attributes-encode attributes-prepare combat-capture combat-seal combat-parse frame-capture frame-seal frame-parse request-seal request-parse replay-result replay-request replay-verify sensing-seal sensing-parse owner-admit owner-restore listener-publish rts-construct owner-prepare capture capture-raw capture-pending owned-capture navigation-capture navigation-prepare tracker-capture tracker-restore session-admit session-prepare session-commit definition-capture definition-prepare definition-seal definition-parse damage-configure damage-capture damage-prepare damage-seal damage-parse)
        add_test(NAME profile.map_rts_runtime.allocation.${_stage} COMMAND eve_map_rts_runtime_allocation_check ${_stage})
        set_tests_properties(profile.map_rts_runtime.allocation.${_stage} PROPERTIES TIMEOUT 60
            LABELS "map_rts_spawn;allocation-contract")
    endforeach()
    # Full artifacts enumerate 4,857/6,115 allocation points with live-owner replacement and continuation.
    # Their measured 63s/80s workloads need a separate budget from individual participant codecs.
    # Each ordinary allocation failure verifies a full file retry and owning
    # continuation. Native GCC ARM64 measured 163s/337s for seal/parse; libstdc++
    # enumerates more allocation points than libc++. Keep the complete contracts.
    set_tests_properties(profile.map_rts_runtime.allocation.runtime-seal PROPERTIES TIMEOUT 360)
    set_tests_properties(profile.map_rts_runtime.allocation.runtime-parse PROPERTIES TIMEOUT 600)
    add_test(NAME profile.map_rts_runtime.allocation.frame COMMAND eve_map_rts_runtime_allocation_check)
    set_tests_properties(profile.map_rts_runtime.allocation.frame PROPERTIES TIMEOUT 60
        LABELS "map_rts_spawn;allocation-contract")
endif()
if(TARGET EVMapRpgSpawn)
    list(APPEND _map_spawn_sources "${CMAKE_SOURCE_DIR}/test/integration/map_spawn/map_rpg_spawn_presentation.cpp")
    list(APPEND _map_spawn_origins map_rpg_spawn_presentation.cpp)
endif()
if(TARGET EVRTSSpawn)
    list(APPEND _map_spawn_sources "${CMAKE_SOURCE_DIR}/test/integration/rts_spawn/map_rts_spawn.cpp")
    list(APPEND _map_spawn_origins map_rts_spawn.cpp)
endif()
if(TARGET EVMapRTSSpawn)
    list(APPEND _map_spawn_sources "${CMAKE_SOURCE_DIR}/test/integration/rts_spawn/map_rts_session.cpp")
    list(APPEND _map_spawn_origins map_rts_session.cpp)
endif()
eve_add_test_executable(eve_map_spawn_check
    "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp" ${_map_spawn_sources})
target_compile_features(eve_map_spawn_check PRIVATE cxx_std_20)

target_link_libraries(eve_map_spawn_check PRIVATE ${_map_spawn_host_libraries} zeroerr eve_engine_includes)
foreach(_origin IN LISTS _map_spawn_origins)
    eve_test_source_case_names(${_origin} _map_spawn_cases eve_map_spawn_check)
    foreach(_case IN LISTS _map_spawn_cases)
        add_test(NAME "profile.map_spawn.${_case}" COMMAND eve_map_spawn_check "--testcase=^${_case}$" --no-color)
        set_tests_properties("profile.map_spawn.${_case}" PROPERTIES TIMEOUT 30
            PASS_REGULAR_EXPRESSION "TEST CASE: +1 +0 +0 +0"
            FAIL_REGULAR_EXPRESSION "Assertion Failed" LABELS "map_spawn")
        eve_label_test_case("profile.map_spawn.${_case}" ${_origin} "${_case}")
    endforeach()
endforeach()

if(TARGET EVMapRpgSpawn AND EVENGINE_MODULE_LINKAGE STREQUAL "OBJECT")
    add_executable(eve_map_rpg_restore_allocation_check
        "${CMAKE_SOURCE_DIR}/test/profile/map_rpg_restore_allocation.cpp")
    target_compile_features(eve_map_rpg_restore_allocation_check PRIVATE cxx_std_20)
    target_include_directories(eve_map_rpg_restore_allocation_check PRIVATE "${CMAKE_SOURCE_DIR}/test")
    target_link_libraries(eve_map_rpg_restore_allocation_check PRIVATE
        ${_map_spawn_host_libraries} zeroerr eve_engine_includes)
    foreach(_stage map_cleanup prepare commit pending_prepare pending_commit runtime_prepare runtime_commit runtime_capture runtime_seal runtime_advance runtime_damage)
        add_test(NAME "profile.map_rpg_restore.allocation.${_stage}"
            COMMAND eve_map_rpg_restore_allocation_check ${_stage})
        set_tests_properties("profile.map_rpg_restore.allocation.${_stage}" PROPERTIES TIMEOUT 60
            LABELS "map_rpg_spawn;allocation-contract")
    endforeach()
endif()

# Derive the owner-local contract build closure; do not duplicate its target registration.
get_directory_property(_spawn_owner_contract_targets BUILDSYSTEM_TARGETS)
add_custom_target(eve_map_spawn_contracts DEPENDS ${_spawn_owner_contract_targets})
