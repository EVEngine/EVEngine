cmake_minimum_required(VERSION 3.21)
project(EVEngineRpgBalance LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(EVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(EVE_BALANCE_SCRIPT ON CACHE BOOL "" FORCE)
add_subdirectory("${EVE_ROOT}/test/balance" balance)
include("${EVE_ROOT}/cmake/TestLevels.cmake")
include("${CMAKE_CURRENT_BINARY_DIR}/balance/test_levels.cmake")
set(EVE_ECS_SOURCE "${EVE_ROOT}/external/ECS.hpp" CACHE PATH "Canonical ECS source checkout")
target_include_directories(balance_core PUBLIC "${EVE_ECS_SOURCE}/src")
set(rpg_sources RPGActor AttributeSystem Skill SkillDefinitionRuntime Effect StatusSystem VitalsSystem
    Class Trait Battle BattleControl Settlement SettlementAdapter EquipmentSystem SimulationEvents)
set(runtime_sources)
foreach(name IN LISTS rpg_sources)
    list(APPEND runtime_sources "${EVE_ROOT}/src/modules/rpg/${name}.cpp")
endforeach()
list(APPEND runtime_sources
    "${EVE_ROOT}/src/modules/rpg/balance/RPGBalance.cpp"
    "${EVE_ROOT}/src/modules/rpg/balance/RPGBalanceBindings.cpp"
    "${EVE_ROOT}/src/modules/rpg/balance/RPGBalanceModule.cpp"
    "${EVE_ROOT}/src/modules/attributes/AttributeSet.cpp"
    "${EVE_ROOT}/src/modules/attributes/StateAccessAdapter.cpp"
    "${EVE_ROOT}/src/modules/definitions/Definitions.cpp"
    "${EVE_ROOT}/src/modules/effects/EffectContainer.cpp"
    "${EVE_ROOT}/src/modules/effects/EffectDefinitionRuntime.cpp"
    "${EVE_ROOT}/src/modules/inventory/Equipment.cpp"
    "${EVE_ROOT}/src/modules/inventory/Bag.cpp"
    "${EVE_ROOT}/src/modules/inventory/Item.cpp"
    "${EVE_ROOT}/src/modules/inventory/InventorySystem.cpp"
    "${EVE_ROOT}/src/modules/action/Action.cpp"
    "${EVE_ROOT}/src/modules/action/AbilityRuntime.cpp"
    "${EVE_ROOT}/src/modules/action/ActionTimeline.cpp")
list(APPEND runtime_sources
    "${EVE_ROOT}/src/modules/rpg/GameState.cpp"
    "${EVE_ROOT}/src/modules/rpg/Quest.cpp"
    "${EVE_ROOT}/src/modules/rpg/Tracker.cpp"
    "${EVE_ROOT}/src/modules/rpg/WorldInteraction.cpp"
    "${EVE_ROOT}/src/modules/game_event/GameEvent.cpp"
    "${EVE_ROOT}/src/modules/profiler/Profiler.cpp"
    "${EVE_ROOT}/src/modules/sensing/Sensing.cpp"
    "${EVE_ROOT}/src/modules/sensing/Targeting.cpp"
    "${EVE_ROOT}/src/modules/sensing/TargetingPipeline.cpp"
    "${EVE_ROOT}/src/modules/sensing/SensingEnvironment.cpp"
    "${EVE_ROOT}/src/modules/sensing/EnvironmentQuery.cpp"
    "${EVE_ROOT}/src/modules/sensing/SensingQueryCodec.cpp"
    "${EVE_ROOT}/src/modules/sensing/SensingQueryBinding.cpp"
    "${EVE_ROOT}/src/modules/sensing/SensingScriptCandidateSeam.cpp"
    "${EVE_ROOT}/src/modules/spatial/SpatialHash2D.cpp"
    "${EVE_ROOT}/src/modules/spatial/Spatial.cpp"
    "${EVE_ROOT}/src/modules/schema/SchemaRegistry.cpp"
    "${EVE_ROOT}/src/modules/transaction/Transaction.cpp"
    "${EVE_ROOT}/src/modules/transaction/AtomicResourcePayment.cpp"
    "${EVE_ROOT}/src/modules/transaction/ResourceAccountParticipant.cpp"
    "${EVE_ROOT}/src/modules/transaction/AtomicBatch.cpp"
    "${EVE_ROOT}/src/engine/common/Profile.cpp"
    "${EVE_ROOT}/src/engine/common/Exception.cpp"
    "${EVE_ROOT}/src/engine/common/CrashLog.cpp"
    "${EVE_ROOT}/src/engine/common/RenderTrace.cpp"
    "${EVE_ROOT}/src/engine/common/RuntimeRegistry.cpp"
    "${EVE_ROOT}/src/engine/common/Module.cpp"
    "${EVE_ROOT}/src/engine/common/ModuleRegistry.cpp"
    "${EVE_ROOT}/src/engine/common/Subscription.cpp"
    "${EVE_ROOT}/src/engine/common/Time.cpp"
    "${EVE_ROOT}/src/engine/common/Uri.cpp"
    "${EVE_ROOT}/src/engine/common/VersionedRegistry.cpp"
    "${EVE_ROOT}/src/engine/common/Snapshot.cpp"
    "${EVE_ROOT}/src/engine/common/definitions/DefinitionRuntime.cpp")
file(GLOB settlement_sources "${EVE_ROOT}/src/modules/settlement/*.cpp")
list(REMOVE_ITEM settlement_sources "${EVE_ROOT}/src/modules/settlement/SettlementModule.cpp")
file(GLOB resource_sources "${EVE_ROOT}/src/engine/common/Resource*.cpp")
list(APPEND runtime_sources ${settlement_sources} ${resource_sources})
foreach(name IN ITEMS Condition ConditionCodec ConditionKeys Identity DefinitionRef SnapshotEnvelope ContentHash
    DefinitionRuntime EntityTable ScriptError)
    if(EXISTS "${EVE_ROOT}/src/engine/common/${name}.cpp")
        list(APPEND runtime_sources "${EVE_ROOT}/src/engine/common/${name}.cpp")
    endif()
endforeach()
add_library(rpg_balance_runtime STATIC ${runtime_sources})
target_link_libraries(rpg_balance_runtime PUBLIC balance_script)
target_include_directories(rpg_balance_runtime PUBLIC "${EVE_THIRD_PARTY_SOURCE}/glm")
add_executable(rpg_balance_demo "${EVE_ROOT}/examples/rpg-balance/main.cpp")
target_link_libraries(rpg_balance_demo PRIVATE rpg_balance_runtime)
target_compile_definitions(rpg_balance_demo PRIVATE EVE_RPG_BALANCE_EXAMPLE="${EVE_ROOT}/examples/rpg-balance")
add_executable(rpg_balance_tests "${EVE_ROOT}/test/integration/rpg-balance/rpg_balance.cpp")
target_link_libraries(rpg_balance_tests PRIVATE rpg_balance_runtime)
foreach(target IN ITEMS rpg_balance_runtime rpg_balance_demo rpg_balance_tests)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /EHsc /Gy /W4 /wd4251 /wd4275)
        target_link_options(${target} PRIVATE /OPT:REF)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -ffunction-sections -fdata-sections)
        if(NOT APPLE)
            target_link_options(${target} PRIVATE -Wl,--gc-sections)
        endif()
    endif()
endforeach()
enable_testing()
add_test(NAME rpg.balance.example COMMAND rpg_balance_demo)
set_tests_properties(rpg.balance.example PROPERTIES TIMEOUT 180 FAIL_REGULAR_EXPRESSION "Assertion Failed")
eve_test_source_case_names("${EVE_ROOT}/test/integration/rpg-balance/rpg_balance.cpp" rpg_cases rpg_balance_tests)
foreach(name IN LISTS rpg_cases)
    string(REPLACE "." "\\." pattern "${name}")
    add_test(NAME "${name}" COMMAND rpg_balance_tests "--testcase=^${pattern}$")
    set_tests_properties("${name}" PROPERTIES TIMEOUT 60 FAIL_REGULAR_EXPRESSION "Assertion Failed")
endforeach()
