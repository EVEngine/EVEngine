# Real RTS root allocation contract; no display is initialized by this executable.
enable_testing()
if(TARGET EVProduction)
    eve_add_test_executable(eve_production_checkpoint_check
        "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
        "${CMAKE_SOURCE_DIR}/test/unit/production/production_tests.cpp")
    target_compile_features(eve_production_checkpoint_check PRIVATE cxx_std_20)
    get_target_property(_production_checkpoint_libraries eve LINK_LIBRARIES)
    target_link_libraries(eve_production_checkpoint_check PRIVATE ${_production_checkpoint_libraries} zeroerr eve_engine_includes)
    eve_test_source_case_names(production_tests.cpp _production_checkpoint_cases eve_production_checkpoint_check)
    foreach(_case IN LISTS _production_checkpoint_cases)
        add_test(NAME "profile.production_checkpoint.${_case}" COMMAND eve_production_checkpoint_check "--testcase=^${_case}$" --no-color)
        set_tests_properties("profile.production_checkpoint.${_case}" PROPERTIES TIMEOUT 30
            PASS_REGULAR_EXPRESSION "TEST CASE: +1 +0 +0 +0" FAIL_REGULAR_EXPRESSION "Assertion Failed"
            LABELS "production;checkpoint-contract")
        eve_label_test_case("profile.production_checkpoint.${_case}" production_tests.cpp "${_case}")
    endforeach()
endif()
if(TARGET EVEffects)
    eve_add_test_executable(eve_effects_checkpoint_check
        "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
        "${CMAKE_SOURCE_DIR}/test/unit/effects/effects_unit.cpp")
    target_compile_features(eve_effects_checkpoint_check PRIVATE cxx_std_20)
    get_target_property(_effects_checkpoint_libraries eve LINK_LIBRARIES)
    target_link_libraries(eve_effects_checkpoint_check PRIVATE ${_effects_checkpoint_libraries} zeroerr eve_engine_includes)
    eve_test_source_case_names(effects_unit.cpp _effects_checkpoint_cases eve_effects_checkpoint_check)
    foreach(_case IN LISTS _effects_checkpoint_cases)
        add_test(NAME "profile.effects_checkpoint.${_case}" COMMAND eve_effects_checkpoint_check "--testcase=^${_case}$" --no-color)
        set_tests_properties("profile.effects_checkpoint.${_case}" PROPERTIES TIMEOUT 30
            PASS_REGULAR_EXPRESSION "TEST CASE: +1 +0 +0 +0" FAIL_REGULAR_EXPRESSION "Assertion Failed"
            LABELS "effects;checkpoint-contract")
        eve_label_test_case("profile.effects_checkpoint.${_case}" effects_unit.cpp "${_case}")
    endforeach()
endif()
if(TARGET EVSensing)
    eve_add_test_executable(eve_sensing_checkpoint_check
        "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
        "${CMAKE_SOURCE_DIR}/test/unit/sensing/sensing_unit.cpp")
    target_compile_features(eve_sensing_checkpoint_check PRIVATE cxx_std_20)
    get_target_property(_sensing_checkpoint_libraries eve LINK_LIBRARIES)
    target_link_libraries(eve_sensing_checkpoint_check PRIVATE ${_sensing_checkpoint_libraries} zeroerr eve_engine_includes)
    eve_test_source_case_names(sensing_unit.cpp _sensing_checkpoint_cases eve_sensing_checkpoint_check)
    foreach(_case IN LISTS _sensing_checkpoint_cases)
        add_test(NAME "profile.sensing_checkpoint.${_case}" COMMAND eve_sensing_checkpoint_check "--testcase=^${_case}$" --no-color)
        set_tests_properties("profile.sensing_checkpoint.${_case}" PROPERTIES TIMEOUT 30
            PASS_REGULAR_EXPRESSION "TEST CASE: +1 +0 +0 +0" FAIL_REGULAR_EXPRESSION "Assertion Failed"
            LABELS "sensing;checkpoint-contract")
        eve_label_test_case("profile.sensing_checkpoint.${_case}" sensing_unit.cpp "${_case}")
    endforeach()
endif()
if(TARGET EVCrowd)
    eve_add_test_executable(eve_crowd_checkpoint_check
        "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
        "${CMAKE_SOURCE_DIR}/test/unit/crowd/crowd_tests.cpp"
        "${CMAKE_SOURCE_DIR}/test/integration/crowd-runtime/CrowdScript.cpp")
    target_compile_features(eve_crowd_checkpoint_check PRIVATE cxx_std_20)
    target_compile_definitions(eve_crowd_checkpoint_check PRIVATE EVE_TEST_FILTER_ORIGINS=1 EVE_TEST_ORIGIN_crowd=1)
    get_target_property(_crowd_checkpoint_libraries eve LINK_LIBRARIES)
    target_link_libraries(eve_crowd_checkpoint_check PRIVATE ${_crowd_checkpoint_libraries} zeroerr eve_engine_includes)
    foreach(_origin crowd_tests.cpp CrowdScript.cpp)
        eve_test_source_case_names(${_origin} _crowd_checkpoint_cases eve_crowd_checkpoint_check)
        foreach(_case IN LISTS _crowd_checkpoint_cases)
            if(_case MATCHES "^crowd[.](snapshot|cache|ties|script|fieldArchive)[.]")
                add_test(NAME "profile.crowd_checkpoint.${_case}" COMMAND eve_crowd_checkpoint_check "--testcase=^${_case}$" --no-color)
                set_tests_properties("profile.crowd_checkpoint.${_case}" PROPERTIES TIMEOUT 30
                    PASS_REGULAR_EXPRESSION "TEST CASE: +1 +0 +0 +0" FAIL_REGULAR_EXPRESSION "Assertion Failed"
                    LABELS "crowd;checkpoint-contract")
                eve_label_test_case("profile.crowd_checkpoint.${_case}" ${_origin} "${_case}")
            endif()
        endforeach()
    endforeach()
endif()
if(TARGET EVAction)
    eve_add_test_executable(eve_action_checkpoint_check
        "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
        "${CMAKE_SOURCE_DIR}/test/unit/action/action_tests.cpp")
    target_compile_features(eve_action_checkpoint_check PRIVATE cxx_std_20)
    target_compile_definitions(eve_action_checkpoint_check PRIVATE EVE_TEST_FILTER_ORIGINS=1 EVE_TEST_ORIGIN_action_checkpoint=1 EVE_TEST_ORIGIN_ability_asset=1)
    get_target_property(_action_checkpoint_host_libraries eve LINK_LIBRARIES)
    target_link_libraries(eve_action_checkpoint_check PRIVATE ${_action_checkpoint_host_libraries} zeroerr eve_engine_includes)
    eve_test_source_case_names(action_tests.cpp _action_checkpoint_cases eve_action_checkpoint_check)
    foreach(_case IN LISTS _action_checkpoint_cases)
        if(_case MATCHES "^action\\.(checkpoint|definitionArchive|abilityAsset)\\.")
            add_test(NAME "profile.action_checkpoint.${_case}" COMMAND eve_action_checkpoint_check "--testcase=^${_case}$" --no-color)
            set_tests_properties("profile.action_checkpoint.${_case}" PROPERTIES TIMEOUT 30
                PASS_REGULAR_EXPRESSION "TEST CASE: +1 +0 +0 +0" FAIL_REGULAR_EXPRESSION "Assertion Failed"
                LABELS "action;checkpoint-contract")
            eve_label_test_case("profile.action_checkpoint.${_case}" action_tests.cpp "${_case}")
        endif()
    endforeach()
endif()
if(TARGET EVRts)
    eve_add_test_executable(eve_rts_snapshot_check
        "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
        "${CMAKE_SOURCE_DIR}/test/unit/rts/gameplay_control_rts.cpp"
        "${CMAKE_SOURCE_DIR}/test/integration/rts-unit-lifecycle/rts_unit_lifecycle_tactics_rts_tests.cpp")
    target_compile_features(eve_rts_snapshot_check PRIVATE cxx_std_20)
    get_target_property(_rts_snapshot_host_libraries eve LINK_LIBRARIES)
    target_link_libraries(eve_rts_snapshot_check PRIVATE ${_rts_snapshot_host_libraries} zeroerr eve_engine_includes)
    foreach(_source gameplay_control_rts.cpp rts_unit_lifecycle_tactics_rts_tests.cpp)
        eve_test_source_case_names(${_source} _rts_snapshot_cases eve_rts_snapshot_check)
        foreach(_case IN LISTS _rts_snapshot_cases)
            if(_source STREQUAL "gameplay_control_rts.cpp" OR
               _case MATCHES "^rts[.]action[.](checkpointProducer|correlated)" OR
               _case MATCHES "^rts\\.(rootSnapshot|rebuildSnapshot|canonicalState|facadeMigrates|orderAndProduction|ballisticProjectile|buildingAmmoProduction|groups\\.(restore|rebuild))")
                add_test(NAME "profile.rts_snapshot.${_case}" COMMAND eve_rts_snapshot_check "--testcase=^${_case}$" --no-color)
                set_tests_properties("profile.rts_snapshot.${_case}" PROPERTIES TIMEOUT 60
                    PASS_REGULAR_EXPRESSION "TEST CASE: +1 +0 +0 +0" FAIL_REGULAR_EXPRESSION "Assertion Failed"
                    LABELS "rts;snapshot-contract")
                eve_label_test_case("profile.rts_snapshot.${_case}" ${_source} "${_case}")
            endif()
        endforeach()
    endforeach()
endif()
if(TARGET EVWeapon)
    eve_add_test_executable(eve_weapon_pool_check
        "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
        "${CMAKE_SOURCE_DIR}/test/unit/weapon/weapon_tests.cpp")
    target_compile_features(eve_weapon_pool_check PRIVATE cxx_std_20)
    target_compile_definitions(eve_weapon_pool_check PRIVATE EVE_TEST_FILTER_ORIGINS=1 EVE_TEST_ORIGIN_weapon=1 EVE_TEST_ORIGIN_projectile_runtime=1)
    get_target_property(_weapon_pool_host_libraries eve LINK_LIBRARIES)
    target_link_libraries(eve_weapon_pool_check PRIVATE ${_weapon_pool_host_libraries} zeroerr eve_engine_includes)
    eve_test_source_case_names(weapon_tests.cpp _weapon_pool_cases eve_weapon_pool_check)
    foreach(_case IN LISTS _weapon_pool_cases)
        if(_case MATCHES "^weapon\\.(ammoPool|spec)\\." OR _case MATCHES "^weapon\\.projectile\\.spec\\.")
            add_test(NAME "profile.weapon_pool.${_case}" COMMAND eve_weapon_pool_check "--testcase=^${_case}$" --no-color)
            set_tests_properties("profile.weapon_pool.${_case}" PROPERTIES TIMEOUT 30
                PASS_REGULAR_EXPRESSION "TEST CASE: +1 +0 +0 +0" FAIL_REGULAR_EXPRESSION "Assertion Failed"
                LABELS "weapon")
            eve_label_test_case("profile.weapon_pool.${_case}" weapon_tests.cpp "${_case}")
        endif()
    endforeach()
endif()
if(EVENGINE_MODULE_LINKAGE STREQUAL "OBJECT")
    add_executable(eve_rts_spawn_allocation_check "${CMAKE_SOURCE_DIR}/test/profile/rts_spawn_allocation.cpp")
    target_compile_features(eve_rts_spawn_allocation_check PRIVATE cxx_std_20)
    get_target_property(_rts_spawn_host_libraries eve LINK_LIBRARIES)
    target_link_libraries(eve_rts_spawn_allocation_check PRIVATE ${_rts_spawn_host_libraries} eve_engine_includes)
    add_test(NAME profile.rts_spawn.rootAllocation COMMAND eve_rts_spawn_allocation_check)
    add_test(NAME profile.rts_spawn.facadeAllocation COMMAND eve_rts_spawn_allocation_check facade)
    add_test(NAME profile.rts_spawn.factionAllocation COMMAND eve_rts_spawn_allocation_check faction)
    add_test(NAME profile.rts_spawn.weaponAllocation COMMAND eve_rts_spawn_allocation_check weapon)
    set_tests_properties(profile.rts_spawn.weaponAllocation PROPERTIES TIMEOUT 60 LABELS "rts;allocation-contract")
    set_tests_properties(profile.rts_spawn.factionAllocation PROPERTIES TIMEOUT 60 LABELS "rts;allocation-contract")
    add_test(NAME profile.rts_spawn.attributeMove COMMAND eve_rts_spawn_allocation_check attributes)
    add_test(NAME profile.rts_spawn.capturedRecipe COMMAND eve_rts_spawn_allocation_check recipe)
    add_test(NAME profile.rts_spawn.ownedWorld COMMAND eve_rts_spawn_allocation_check ownership)
    add_test(NAME profile.rts_spawn.unitBatch COMMAND eve_rts_spawn_allocation_check batch)
    add_test(NAME profile.rts_spawn.batchRemoval COMMAND eve_rts_spawn_allocation_check removal)
    add_test(NAME profile.rts_spawn.administrativeRemoval COMMAND eve_rts_spawn_allocation_check administrative)
    set_tests_properties(profile.rts_spawn.administrativeRemoval PROPERTIES TIMEOUT 60 LABELS "rts;sensing;crowd;allocation-contract")
    set_tests_properties(profile.rts_spawn.batchRemoval PROPERTIES TIMEOUT 60 LABELS "rts;sensing;crowd;allocation-contract")
    set_tests_properties(profile.rts_spawn.unitBatch PROPERTIES TIMEOUT 60 LABELS "rts;crowd;transaction-contract")
    set_tests_properties(profile.rts_spawn.ownedWorld PROPERTIES TIMEOUT 60 LABELS "rts;lifetime-contract")
    set_tests_properties(profile.rts_spawn.capturedRecipe PROPERTIES TIMEOUT 60 LABELS "rts;recipe-contract")
    set_tests_properties(profile.rts_spawn.attributeMove PROPERTIES TIMEOUT 60 LABELS "rts;attributes;allocation-contract")
    set_tests_properties(profile.rts_spawn.facadeAllocation PROPERTIES TIMEOUT 60 LABELS "rts;allocation-contract")
    set_tests_properties(profile.rts_spawn.rootAllocation PROPERTIES TIMEOUT 60 LABELS "rts;allocation-contract")
endif()
if(TARGET EVRTSSpawn)
    eve_add_test_executable(eve_rts_spawn_factory_check
        "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
        "${CMAKE_SOURCE_DIR}/test/integration/rts_spawn/rts_spawn.cpp"
        "${CMAKE_SOURCE_DIR}/test/integration/rts_spawn/rts_weapon_restore.cpp"
        "${CMAKE_SOURCE_DIR}/test/integration/rts_spawn/rts_crowd_restore.cpp")
    target_compile_features(eve_rts_spawn_factory_check PRIVATE cxx_std_20)
    get_target_property(_rts_factory_host_libraries eve LINK_LIBRARIES)
    target_link_libraries(eve_rts_spawn_factory_check PRIVATE ${_rts_factory_host_libraries} zeroerr eve_engine_includes)
    foreach(_origin rts_spawn.cpp rts_weapon_restore.cpp rts_crowd_restore.cpp)
        eve_test_source_case_names(${_origin} _rts_factory_cases eve_rts_spawn_factory_check)
        foreach(_case IN LISTS _rts_factory_cases)
            add_test(NAME "profile.rts_spawn_factory.${_case}" COMMAND eve_rts_spawn_factory_check "--testcase=^${_case}$" --no-color)
            set_tests_properties("profile.rts_spawn_factory.${_case}" PROPERTIES TIMEOUT 30
                PASS_REGULAR_EXPRESSION "TEST CASE: +1 +0 +0 +0"
                FAIL_REGULAR_EXPRESSION "Assertion Failed" LABELS "rts_spawn")
            eve_label_test_case("profile.rts_spawn_factory.${_case}" ${_origin} "${_case}")
        endforeach()
    endforeach()
endif()
if(TARGET EVRTSSpawn AND EVENGINE_MODULE_LINKAGE STREQUAL "OBJECT")
    add_executable(eve_rts_spawn_factory_allocation_check "${CMAKE_SOURCE_DIR}/test/profile/rts_spawn_factory_allocation.cpp")
    target_compile_features(eve_rts_spawn_factory_allocation_check PRIVATE cxx_std_20)
    target_include_directories(eve_rts_spawn_factory_allocation_check PRIVATE "${CMAKE_SOURCE_DIR}/test")
    get_target_property(_rts_factory_allocation_host_libraries eve LINK_LIBRARIES)
    target_link_libraries(eve_rts_spawn_factory_allocation_check PRIVATE ${_rts_factory_allocation_host_libraries} eve_engine_includes)
    foreach(_stage root-value-encode root-value-parse root-value-materialize effect-encode effect-parse effect-restore order-encode order-parse order-restore projectile-encode projectile-parse projectile-restore body-crowd-seal body-crowd-parse external-seal external-parse backend-encode backend-parse field-encode field-parse correlation-seal correlation-parse action-seal action-parse action-def-encode action-def-parse source-seal source-parse archetype-encode archetype-parse body-weapons-seal body-weapons-parse weapon-value-encode weapon-value-parse pool-value-encode pool-value-parse shared-flow-capture shared-flow-prepare shared-flow-restore request-capture request-prepare request-restore body-crowd-capture body-crowd-prepare body-crowd-restore prepare commit remove lifecycle capture root-capture action-capture action-prepare action-restore correlation-capture correlation-prepare correlation-restore crowd-capture crowd-prepare crowd-restore sensing-capture sensing-prepare sensing-restore combat-prepare combat-restore frame-prepare frame-restore attribute-capture attribute-prepare attribute-restore pool-graph weapon-prepare weapon-commit weapon-create-attributes pool-restore-prepare pool-restore-commit graph-restore-prepare graph-restore-commit graph-restore-create-attributes unit-attribute-capture unit-attribute-prepare unit-attribute-restore source-restore-factory source-restore-bind body-weapon-prepare body-weapon-materialize pool)
        add_test(NAME "profile.rts_spawn_factory.allocation.${_stage}" COMMAND eve_rts_spawn_factory_allocation_check ${_stage})
        set_tests_properties("profile.rts_spawn_factory.allocation.${_stage}" PROPERTIES TIMEOUT 60 LABELS "rts_spawn;allocation-contract")
    endforeach()
    # Full libstdc++ enumeration covers roughly 9k/11k allocation points. Every
    # point rebuilds real worlds and verifies complete file/native retries;
    # measured CPU cost exceeds eight minutes even in an optimized build.
    set_tests_properties(profile.rts_spawn_factory.allocation.body-crowd-seal
        profile.rts_spawn_factory.allocation.body-crowd-parse PROPERTIES TIMEOUT 1200)
endif()

# Derive the owner-local contract build closure; do not duplicate its target registration.
get_directory_property(_spawn_owner_contract_targets BUILDSYSTEM_TARGETS)
add_custom_target(eve_rts_spawn_contracts DEPENDS ${_spawn_owner_contract_targets})
