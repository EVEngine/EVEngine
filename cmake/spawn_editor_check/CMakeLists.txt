# Editor integration uses the actual configured host composition, including optional editor providers.
enable_testing()
eve_add_test_executable(eve_spawn_editor_check
    "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
    "${CMAKE_SOURCE_DIR}/test/GraphicsWindowFixtures.cpp"
    "${CMAKE_SOURCE_DIR}/test/integration/spawn_editor/spawn_editor.cpp")
target_compile_features(eve_spawn_editor_check PRIVATE cxx_std_20)
get_target_property(_spawn_form_host_libraries eve LINK_LIBRARIES)
target_link_libraries(eve_spawn_editor_check PRIVATE ${_spawn_form_host_libraries}
    zeroerr eve_engine_includes)
eve_test_source_case_names(spawn_editor.cpp _document_cases eve_spawn_editor_check)
foreach(_case IN LISTS _document_cases)
    add_test(NAME "profile.spawn_editor.${_case}" COMMAND eve_spawn_editor_check "--testcase=^${_case}$" --no-color)
    set_tests_properties("profile.spawn_editor.${_case}" PROPERTIES TIMEOUT 30
        PASS_REGULAR_EXPRESSION "TEST CASE: +1 +0 +0 +0"
        FAIL_REGULAR_EXPRESSION "Assertion Failed" LABELS "spawn_editor")
    eve_label_test_case("profile.spawn_editor.${_case}" spawn_editor.cpp "${_case}")
endforeach()
if(TARGET EVMapRpgSpawnEditor)
    target_sources(eve_spawn_editor_check PRIVATE
        "${CMAKE_SOURCE_DIR}/test/integration/map_rpg_spawn_editor/map_rpg_spawn_editor.cpp")
    eve_test_source_case_names(map_rpg_spawn_editor.cpp _rpg_editor_cases eve_spawn_editor_check)
    foreach(_case IN LISTS _rpg_editor_cases)
        add_test(NAME "profile.spawn_editor.${_case}" COMMAND eve_spawn_editor_check "--testcase=^${_case}$" --no-color)
        set_tests_properties("profile.spawn_editor.${_case}" PROPERTIES TIMEOUT 30
            PASS_REGULAR_EXPRESSION "TEST CASE: +1 +0 +0 +0"
            FAIL_REGULAR_EXPRESSION "Assertion Failed" LABELS "map_rpg_spawn_editor")
        eve_label_test_case("profile.spawn_editor.${_case}" map_rpg_spawn_editor.cpp "${_case}")
    endforeach()
endif()
