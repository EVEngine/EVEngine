# Full module composition; no selected production-source shortcut.
foreach(_bake_forbidden physics physics_cloth physics_rope physics_destruction particles)
    if(_bake_forbidden IN_LIST EVE_ENABLED_MODULES)
        message(FATAL_ERROR "physics-bake-render acquired live producer ${_bake_forbidden}")
    endif()
endforeach()
foreach(_bake_solver box2d box3d jolt)
    if(_bake_solver IN_LIST EVE_THIRDPARTY_GROUPS)
        message(FATAL_ERROR "physics-bake-render acquired solver ${_bake_solver}")
    endif()
endforeach()
eve_thirdparty_libs(_bake_render_libraries ${EVE_THIRDPARTY_GROUPS})
eve_append_system_libraries(_bake_render_system_libraries)
add_executable(eve_bake_render_check
    "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
    "${CMAKE_SOURCE_DIR}/test/integration/physics-bake/deform_presentation.cpp"
    "${CMAKE_SOURCE_DIR}/test/integration/physics-bake/graphics_owner_retirement.cpp"
    "${CMAKE_SOURCE_DIR}/test/integration/physics-bake/script_deform_presentation.cpp"
    "${CMAKE_SOURCE_DIR}/test/integration/physics-bake/script_particle_presentation.cpp"
    "${CMAKE_SOURCE_DIR}/test/integration/physics-bake/script_presentation.cpp")
target_compile_features(eve_bake_render_check PRIVATE cxx_std_20)
target_link_libraries(eve_bake_render_check PRIVATE ${EVE_LINK_TARGETS} EVScripts zeroerr eve_engine_includes
    ${_bake_render_libraries} ${_bake_render_system_libraries} ${EVENGINE_VULKAN_LIB} ${EVENGINE_PLATFORM_LIB})
set_target_properties(eve_bake_render_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/profile")
foreach(_bake_case deformPresentationSurvivesGraphicsOwnerReplacement
        deformMeshesRenderAndUpdateWithoutSimulation
        scriptDeformPresentationRendersAfterSourceAndPresenterRelease
        scriptParticlePresentationBatchesMaterialsAndRejectsPartialDraw
        scriptRigidPresentationBorrowsTargetsAndPreservesFailures)
    add_test(NAME "bake.render.${_bake_case}"
        COMMAND eve_bake_render_check "--testcase=^physics.bake.${_bake_case}$" --no-color)
    set_tests_properties("bake.render.${_bake_case}" PROPERTIES
        PASS_REGULAR_EXPRESSION "TEST CASE: +1 +0 +0 +0"
        FAIL_REGULAR_EXPRESSION "Assertion Failed"
        TIMEOUT 120)
endforeach()
