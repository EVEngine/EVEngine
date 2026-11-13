# Standalone spawn contracts: domain adapters join only when selected by the manifest.
enable_testing()
set(_spawn_libraries EVSpawn EVCommon)
if(EVENGINE_MODULE_LINKAGE STREQUAL "SHARED")
    set(_spawn_libraries ${EVE_MODULE_spawn_LINK_GROUP})
endif()
set(_spawn_sources "${CMAKE_SOURCE_DIR}/test/unit/spawn/spawn.cpp")
set(_spawn_test_names spawn.cpp)
if(TARGET EVSpawn_definitions)
    list(APPEND _spawn_sources "${CMAKE_SOURCE_DIR}/test/unit/spawn_definitions/spawn_definitions.cpp")
    list(APPEND _spawn_test_names spawn_definitions.cpp)
    if(NOT EVENGINE_MODULE_LINKAGE STREQUAL "SHARED")
        list(APPEND _spawn_libraries EVSpawn_definitions EVDefinitions EVSchema)
    endif()
endif()
if(TARGET EVRPGSpawn)
    list(APPEND _spawn_sources "${CMAKE_SOURCE_DIR}/test/unit/rpg_spawn/rpg_spawn.cpp")
    list(APPEND _spawn_test_names rpg_spawn.cpp)
    # Resolve the real dependency closure from the manifest, never hand-copy its link wiring.
    set(_spawn_pending rpg_spawn)
    set(_spawn_seen)
    while(_spawn_pending)
        list(POP_FRONT _spawn_pending _module)
        if(_module IN_LIST _spawn_seen)
            continue()
        endif()
        list(APPEND _spawn_seen ${_module})
        list(APPEND _spawn_pending ${EVE_MODULE_${_module}_DEPS})
        if(EVENGINE_MODULE_LINKAGE STREQUAL "SHARED")
            list(APPEND _spawn_libraries ${EVE_MODULE_${_module}_LINK_GROUP})
        else()
            list(APPEND _spawn_libraries ${EVE_MODULE_${_module}_LIB})
        endif()
    endwhile()
    list(REMOVE_DUPLICATES _spawn_libraries)
endif()
eve_add_test_executable(eve_spawn_check
    "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
    ${_spawn_sources})
target_compile_features(eve_spawn_check PRIVATE cxx_std_20)
eve_thirdparty_libs(_spawn_thirdparty squirrel)
find_package(Threads REQUIRED)
target_link_libraries(eve_spawn_check PRIVATE ${_spawn_libraries} zeroerr eve_engine_includes
    ${_spawn_thirdparty} Threads::Threads ${CMAKE_DL_LIBS})
add_executable(eve_spawn_capability_shutdown_check "${CMAKE_SOURCE_DIR}/test/profile/spawn_capability_shutdown.cpp")
target_compile_features(eve_spawn_capability_shutdown_check PRIVATE cxx_std_20)
target_link_libraries(eve_spawn_capability_shutdown_check PRIVATE ${_spawn_libraries} zeroerr eve_engine_includes
    ${_spawn_thirdparty} Threads::Threads ${CMAKE_DL_LIBS})
if(WIN32)
    target_link_libraries(eve_spawn_capability_shutdown_check PRIVATE EVBacktrace)
endif()
add_dependencies(eve_spawn_check eve_spawn_capability_shutdown_check)
add_test(NAME profile.spawn.capabilityShutdown
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE_COMMAND=$<TARGET_FILE:eve_spawn_capability_shutdown_check>"
        -P "${CMAKE_CURRENT_LIST_DIR}/VerifyCapabilityShutdown.cmake")
set_tests_properties(profile.spawn.capabilityShutdown PROPERTIES TIMEOUT 30 LABELS "common;lifetime-contract")
if(TARGET EVRPGSpawn)
    # Reuse the owning module's existing baseline tests for the canonical initializer migration.
    set(_spawn_rpg_compat_origins rpg.cpp rpg_reload.cpp rpg_state_canonical.cpp rpg_encounter_catalogue.cpp rpg_actor_checkpoint.cpp rpg_progression.cpp status_effect_adapter.cpp attributes.cpp rpg_skill_condition.cpp)
    eve_add_test_executable(eve_spawn_rpg_compat
        "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp" ${_spawn_rpg_compat_origins})
    target_compile_features(eve_spawn_rpg_compat PRIVATE cxx_std_20)
    target_link_libraries(eve_spawn_rpg_compat PRIVATE ${_spawn_libraries} zeroerr eve_engine_includes
        ${_spawn_thirdparty} Threads::Threads ${CMAKE_DL_LIBS})
    add_dependencies(eve_spawn_check eve_spawn_rpg_compat)
    # The legacy effects.cpp alias also names script integration sources. Select
    # the module-owned physical suite directly to keep this profile hostless.
    eve_add_test_executable(eve_spawn_effects_compat
        "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
        "${CMAKE_SOURCE_DIR}/test/unit/effects/effects_unit.cpp")
    target_compile_features(eve_spawn_effects_compat PRIVATE cxx_std_20)
    target_link_libraries(eve_spawn_effects_compat PRIVATE ${_spawn_libraries} zeroerr eve_engine_includes
        ${_spawn_thirdparty} Threads::Threads ${CMAKE_DL_LIBS})
    add_dependencies(eve_spawn_check eve_spawn_effects_compat)
    eve_test_source_case_names(effects_unit.cpp _effects_cases eve_spawn_effects_compat)
    foreach(_case IN LISTS _effects_cases)
        add_test(NAME "profile.spawn-effects-compat.${_case}"
            COMMAND eve_spawn_effects_compat "--testcase=^${_case}$" --no-color)
        set_tests_properties("profile.spawn-effects-compat.${_case}" PROPERTIES TIMEOUT 30
            PASS_REGULAR_EXPRESSION "TEST CASE: +1 +0 +0 +0"
            FAIL_REGULAR_EXPRESSION "Assertion Failed" LABELS "effects;rpg_spawn;compatibility")
        eve_label_test_case("profile.spawn-effects-compat.${_case}" effects_unit.cpp "${_case}")
    endforeach()
    if(EVENGINE_MODULE_LINKAGE STREQUAL "OBJECT")
        add_executable(eve_rpg_spawn_allocation_check "${CMAKE_SOURCE_DIR}/test/profile/rpg_spawn_allocation.cpp")
        target_compile_features(eve_rpg_spawn_allocation_check PRIVATE cxx_std_20)
        target_link_libraries(eve_rpg_spawn_allocation_check PRIVATE ${_spawn_libraries} zeroerr eve_engine_includes
            ${_spawn_thirdparty} Threads::Threads ${CMAKE_DL_LIBS})
        add_dependencies(eve_spawn_check eve_rpg_spawn_allocation_check)
        add_executable(eve_rpg_spawn_factory_allocation_check "${CMAKE_SOURCE_DIR}/test/profile/rpg_spawn_factory_allocation.cpp")
        target_compile_features(eve_rpg_spawn_factory_allocation_check PRIVATE cxx_std_20)
        target_include_directories(eve_rpg_spawn_factory_allocation_check PRIVATE "${CMAKE_SOURCE_DIR}/test")
        target_link_libraries(eve_rpg_spawn_factory_allocation_check PRIVATE ${_spawn_libraries} zeroerr eve_engine_includes
            ${_spawn_thirdparty} Threads::Threads ${CMAKE_DL_LIBS})
        add_dependencies(eve_spawn_check eve_rpg_spawn_factory_allocation_check)
        foreach(_stage prepare commit)
            add_test(NAME profile.rpg_spawn.factoryAllocation.${_stage} COMMAND eve_rpg_spawn_factory_allocation_check ${_stage})
            set_tests_properties(profile.rpg_spawn.factoryAllocation.${_stage} PROPERTIES TIMEOUT 60 LABELS "rpg_spawn;allocation-contract")
        endforeach()
        add_executable(eve_rpg_spawn_restore_allocation_check "${CMAKE_SOURCE_DIR}/test/profile/rpg_spawn_restore_allocation.cpp")
        target_compile_features(eve_rpg_spawn_restore_allocation_check PRIVATE cxx_std_20)
        target_include_directories(eve_rpg_spawn_restore_allocation_check PRIVATE "${CMAKE_SOURCE_DIR}/test")
        target_link_libraries(eve_rpg_spawn_restore_allocation_check PRIVATE ${_spawn_libraries} zeroerr eve_engine_includes
            ${_spawn_thirdparty} Threads::Threads ${CMAKE_DL_LIBS})
        add_dependencies(eve_spawn_check eve_rpg_spawn_restore_allocation_check)
        add_test(NAME profile.rpg_spawn.restoreAllocation COMMAND eve_rpg_spawn_restore_allocation_check)
        set_tests_properties(profile.rpg_spawn.restoreAllocation PROPERTIES TIMEOUT 60 LABELS "rpg_spawn;allocation-contract")
        add_test(NAME profile.rpg_spawn.actorAllocation COMMAND eve_rpg_spawn_allocation_check)
        set_tests_properties(profile.rpg_spawn.actorAllocation PROPERTIES TIMEOUT 60 LABELS "rpg;attributes;allocation-contract")
    endif()
    set(_spawn_rpg_compat_cases)
    foreach(_origin IN LISTS _spawn_rpg_compat_origins)
        eve_test_source_case_names("${_origin}" _cases eve_spawn_rpg_compat)
        list(APPEND _spawn_rpg_compat_cases ${_cases})
    endforeach()
    list(REMOVE_DUPLICATES _spawn_rpg_compat_cases)
    foreach(_case IN LISTS _spawn_rpg_compat_cases)
        add_test(NAME "profile.spawn-rpg-compat.${_case}"
            COMMAND eve_spawn_rpg_compat "--testcase=^${_case}$" --no-color)
        set_tests_properties("profile.spawn-rpg-compat.${_case}" PROPERTIES TIMEOUT 30
            PASS_REGULAR_EXPRESSION "TEST CASE: +1 +0 +0 +0"
            FAIL_REGULAR_EXPRESSION "Assertion Failed" LABELS "rpg_spawn;compatibility")
    endforeach()
endif()
set(_spawn_cases)
foreach(_source IN LISTS _spawn_test_names)
    eve_test_source_case_names("${_source}" _cases eve_spawn_check)
    list(APPEND _spawn_cases ${_cases})
endforeach()
foreach(_case IN LISTS _spawn_cases)
    add_test(NAME "profile.spawn.${_case}" COMMAND eve_spawn_check "--testcase=^${_case}$" --no-color)
    set_tests_properties("profile.spawn.${_case}" PROPERTIES TIMEOUT 30
        PASS_REGULAR_EXPRESSION "TEST CASE: +1 +0 +0 +0"
        FAIL_REGULAR_EXPRESSION "Assertion Failed" LABELS "spawn")
endforeach()
