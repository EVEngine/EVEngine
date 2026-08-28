enable_testing()

# Compile-fail fixtures for critical non-Result return values. This target is
# intentionally independent of unit_test and never triggers a full engine
# build by itself; CI also runs the same script in the fast structural job.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_target(nodiscard_compile_check
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/check_nodiscard.py
          --source-dir ${CMAKE_SOURCE_DIR}
  USES_TERMINAL)
add_test(NAME common.nodiscard_compile_diagnostics
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/check_nodiscard.py
          --source-dir ${CMAKE_SOURCE_DIR})
set_tests_properties(common.nodiscard_compile_diagnostics PROPERTIES LABELS "quality")
add_test(NAME cmake.third_party_patch_idempotence
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/tests/test_patch_third_party.py)
set_tests_properties(cmake.third_party_patch_idempotence PROPERTIES LABELS "quality;cmake")

file(GLOB all_test_cpp
  ECS.cpp
  capability.cpp
  common_identity_version.cpp
  identity_result_audit.cpp
  runtime_handle.cpp
  common_subscription.cpp
  time_subscription_consumers.cpp
  container_transfer.cpp
  container_adapters.cpp
  versioned_registry.cpp
  time_contract.cpp
  data.cpp
  decal.cpp
  decal_script.cpp
  crowd.cpp
  crowd_script.cpp
  profiler.cpp
  profiler_script.cpp
  crashlog.cpp
  resource.cpp
  resource_account.cpp
  resource_uri.cpp
  result.cpp
  value.cpp
  state_value.cpp
  json.cpp
  filesystem.cpp
  filesystem_cpp.cpp
  fluids.cpp
  fluids_surface.cpp
  graphic.cpp
  graphics_font.cpp
  graphics_headless.cpp
  graphics_backend_parity.cpp
  medialoader_model_link.cpp
  model.cpp
  mouse.cpp
  mouse_cpp.cpp
  image_script.cpp
  joystick_script.cpp
  keyboard_cpp.cpp
  joystick_cpp.cpp
  touch.cpp
  network.cpp
  plugins.cpp
  database.cpp
  RenderSystem.cpp
  RenderSystem3D.cpp
  render_graph.cpp
  Quad.cpp
  TextureCellBomb.cpp
  ParallaxMap.cpp
  Lighting2D.cpp
  ClusteredLighting.cpp
  IBL.cpp
  Shadow3D.cpp
  MaterialRenderControl.cpp
  ClassicScenes.cpp
  RenderImageAudit.cpp
  RenderSceneEffects.cpp
  Volumetric.cpp
  atmosphere_volume.cpp
  TextureSampler.cpp
  AmbientOcclusion.cpp
  Outline.cpp
  AntiAliasing.cpp
  GlobalIllumination.cpp
  ScreenSpaceReflection.cpp
  UISystem.cpp
  UIBuilder.cpp
  editor_property_model.cpp
  editor_transaction_consumer.cpp
  property_validation_parity.cpp
  script_property_model.cpp
  ui_control_semantics.cpp
  ui_nine_patch.cpp
  ui_presentation.cpp
  UIMoveRepro.cpp
  UIPhaseB.cpp
  UIPhaseAdv.cpp
  UIEditorKit.cpp
  UIOverlaySmoke.cpp
  ui_texture_bridge.cpp
  Inspector.cpp
  DatabasePanel.cpp
  EditorShell.cpp
  ScenePanel.cpp
  scene.cpp
  sceneloader.cpp
  service_mocks.cpp
  particles.cpp
  particles_playback.cpp
  particles_scalability.cpp
  particles_effect_asset.cpp
  particles_gpu_resident.cpp
  particles_reference_effects.cpp
  particles_renderer.cpp
  particles_attach_skin.cpp
  particles_dynamic_bones.cpp
  particles_attach_more.cpp
  particles_attach_extra.cpp
  map.cpp
  map_path.cpp
  map_fov.cpp
  rpg.cpp
  action_timeline.cpp
  action_notify_registry.cpp
  action_timeline_widget.cpp
  action_preview_controller.cpp
  animation_action_preview.cpp
  projectile_runtime.cpp
  ability_controller.cpp
  ability_system.cpp
  combat_attributes.cpp
  combat_damage.cpp
  combat_framework_composition.cpp
  kaykit_combat_assets.cpp
  standard_combat_abilities.cpp
  gameplay_action.cpp
  rpg_simulation.cpp
  rpg_reload.cpp
  hex_level_simulation.cpp
  hex_level_data.cpp
  inventory.cpp
  building.cpp
  buildingfx.cpp
  camera.cpp
  card.cpp
  card_reload.cpp
  composable_gameplay.cpp
  i18n.cpp
  i18n_script.cpp
  avatar.cpp
  dialogue.cpp
  dialogue_conversation.cpp
  dialogue_persistence.cpp
  dialogue_text.cpp
  dialogue_hot_reload.cpp
  dialogue_authoring.cpp
    dialogue_compiler.cpp
    dialogue_importers.cpp
    dialogue_localization.cpp
    dialogue_toolchain.cpp
  dialogue_ux.cpp
  dialogue_voice.cpp
  dialogue_proc.cpp
  dialogue_proc_script.cpp
  dialogue_state_integration.cpp
  dialogue_payment.cpp
  physics_core_boundary.cpp
  physics_artifact_provider.cpp
  physics_cloth.cpp
  procgen.cpp
  prototype_kit.cpp
  procgen_e2e.cpp
  procgen_artifact_publish.cpp
  procgen_generated_artifact.cpp
  procgen_core_boundary.cpp
  procgen_schema_script.cpp
  procgen_pointset.cpp
  procgen_lsystem.cpp
  procgen_poisson.cpp
  procgen_spatial_data.cpp
  procgen_runtime_generation.cpp
  procgen_scene_sink.cpp
  procgen_point_graph.cpp
  procgen_point_graph_stress.cpp
  procgen_biome.cpp
  procgen_shape_grammar.cpp
  urban_generator.cpp
  housegen.cpp
  housegen_render.cpp
  procgen_simulation.cpp
  roguelike_generator.cpp
  stylize.cpp
  stylize_effects.cpp
  snow.cpp
  Xray.cpp
  voxel.cpp
  voxel_render.cpp
  virtualgeometry_builder.cpp
  virtualgeometry_gpu.cpp
  gpgpu.cpp
  gpu_driven.cpp
  gpu_driven_parity.cpp
  indirect_builder.cpp
  box2d.cpp
  box3d.cpp
    climbing.cpp
    climbing_anchor_execution.cpp
    climbing_anchor_graph.cpp
    climbing_anchor_route.cpp
    climbing_anchor_authoring.cpp
    climbing_animation.cpp
    climbing_codec.cpp
    climbing_definition_policy.cpp
    climbing_services.cpp
    climbing_ecs.cpp
    climbing_events.cpp
    climbing_input.cpp
    climbing_parkour.cpp
    climbing_performance.cpp
    climbing_physics_policy.cpp
    climbing_pose.cpp
    climbing_prediction.cpp
    climbing_playground.cpp
    climbing_reload.cpp
    climbing_snapshot.cpp
    climbing_telemetry.cpp
    climbing_vault.cpp
  binding_selfcheck.cpp
  squirrel_binding.cpp
  script_ownership.cpp
  softbody.cpp
  softbody_gpu.cpp
  hotreload.cpp
  remotesync.cpp
  shader.cpp
  hair.cpp
  grass.cpp
  hd2d.cpp
  spritestack.cpp
  daynight.cpp
  weather.cpp
  weapon.cpp
  vehicle.cpp
  attribute_domain_adapters.cpp
  rts_composition.cpp
  vehicle_orders_adapter.cpp
  simplesquirrel.cpp
  ScriptECS.cpp
  sound.cpp
  model3d.cpp
  audio.cpp
  font.cpp
  thread.cpp
  async.cpp
  window.cpp
  window_cpp.cpp
  image.cpp
  event.cpp
  event_envelope.cpp
  economy.cpp
  attributes.cpp
  attributes_rpg_unification.cpp
  decision.cpp
  condition.cpp
  rpg_skill_condition.cpp
  card_play_condition.cpp
  resource_gameplay_adapters.cpp
  authority.cpp
  definitions.cpp
  definition_runtime.cpp
  definition_runtime_skill_vehicle.cpp
  definition_runtime_weapon.cpp
  snapshot_envelope.cpp
  snapshot_consumers.cpp
  effects.cpp
  effects_lifecycle.cpp
  status_effect_adapter.cpp
  effects_domain_adapters.cpp
  eventstream.cpp
  card_rts_production_integration.cpp
  schema_production_integration.cpp
  orders.cpp
  policyregistry.cpp
  production.cpp
  production_time.cpp
  sensing.cpp
  targeting.cpp
  schema.cpp
  schema_contract.cpp
  social.cpp
  statepatch.cpp
  steering.cpp
  tags.cpp
  gameplay_tags.cpp
  transaction.cpp
  transaction_participant.cpp
  rx.cpp
  runtime.cpp
  language_index.cpp
  script_compiler.cpp
  script_config.cpp
  script_module.cpp
  timer.cpp
  timer_cpp.cpp
  system.cpp
  math.cpp
  spatial.cpp
  grid.cpp
  editor.cpp
  editor_automation_v2.cpp
  editor_core_v2.cpp
  editor_disk_asset_catalog_v2.cpp
  editor_disk_document_v2.cpp
  editor_document_asset_v2.cpp
  editor_extension_v2.cpp
  editor_graph_material_v2.cpp
  editor_pcg_graph.cpp
  editor_persistence_v2.cpp
  editor_property_v2.cpp
  editor_runtime_manifest_v2.cpp
  editor_scene_target_v2.cpp
  editor_session_persistence.cpp
  editor_script_v2.cpp
  editor_action_timeline_script.cpp
  editor_field_tool_script.cpp
  editor_tilemap_target_script.cpp
  editor_voxel_target_script.cpp
  editor_selection_v2.cpp
  editor_workspace.cpp
  editor_task_diagnostic_v2.cpp
  editor_transaction_v2.cpp
  editor_value_json_v2.cpp
  ik.cpp
  animation.cpp
  animation_clip_editing.cpp
  animation_retarget_compression.cpp
  animation_batch_lod.cpp
  animation_constraints_sync.cpp
  animation_mixamo.cpp
  animation_skinned.cpp
  animation_lattice.cpp
  animation_sprite_spine.cpp
  animation_reload.cpp
  tensor.cpp
  tensor_llm.cpp
  tensor_quant.cpp
  callgraph.cpp
  renderflow.cpp
  resource_lifetime.cpp
  debugger.cpp
  debugger_audit.cpp
  ReflectScript.cpp
  reload_script.cpp
  dap.cpp
  mcp.cpp
  mcp_hot_reload.cpp
  console.cpp
  runtime_error_sink.cpp
  scenario_recorder.cpp
  cmdline.cpp
  main.cpp
)

# Auto-discover newly added test files (CONFIGURE_DEPENDS re-globs at build
# time), so a new test/*.cpp is picked up without editing this list. demo.cpp
# stays gated on EVENGINE_BUILD_DEMO below.
file(GLOB _eve_extra_test_cpp CONFIGURE_DEPENDS *.cpp)
list(REMOVE_ITEM _eve_extra_test_cpp "${CMAKE_CURRENT_SOURCE_DIR}/demo.cpp")
list(APPEND all_test_cpp ${_eve_extra_test_cpp})

if(EVENGINE_BUILD_DEMO)
  list(APPEND all_test_cpp demo.cpp)
endif()

# Desktop-only DevTools (DAP / MCP / debugger / callgraph / renderflow) are not
# compiled into the Android/iOS runtimes, so their test files cannot link there.
# all_test_cpp holds absolute paths from file(GLOB), so filter by basename.
if(ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Android") OR EVENGINE_IOS_TEST_APP)
  foreach(_eve_devtool_test IN ITEMS
      callgraph.cpp renderflow.cpp debugger.cpp debugger_audit.cpp reload_script.cpp dap.cpp mcp.cpp console.cpp runtime_error_sink.cpp scenario_recorder.cpp)
    list(FILTER all_test_cpp EXCLUDE REGEX "/${_eve_devtool_test}$")
  endforeach()
  message(STATUS "Mobile test app: removed devtools test files, remaining=${all_test_cpp}")
endif()

# These tests intentionally exercise Vulkan implementation details rather than
# the backend-neutral graphics API, so they cannot be linked into a WebGPU test
# binary. Backend parity coverage lives in graphics_backend_parity.cpp.
if(BUILD_PLATFORM STREQUAL "webgpu")
  foreach(_eve_vulkan_test IN ITEMS gpu_driven.cpp render_graph.cpp)
    list(FILTER all_test_cpp EXCLUDE REGEX "/${_eve_vulkan_test}$")
  endforeach()
endif()

file(GLOB all_test_scripts
  filesystem.nut
  graphic.nut
  i18n.nut
  dialogue_proc_script.nut
  image.nut
  joystick.nut
  model.nut
  model2.nut
  mouse.nut
  simplesquirrel.nut
  physics_cloth.nut
  window.nut
  timer.nut
  decal.nut
  crowd.nut
  profiler.nut
)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/nut_scripts.cpp
  COMMAND ${CMAKE_COMMAND} -DOUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}" -P "${CMAKE_SOURCE_DIR}/cmake/integrate.cmake"
  DEPENDS ${all_test_scripts} ${CMAKE_CURRENT_SOURCE_DIR}/template.cpp.in
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# On Android the zeroerr runner is packaged as the app's native entry point
# (libmain.so loaded by SDLActivity), replacing the engine main.cpp.
if(EVENGINE_ANDROID_TEST_APP)
  add_library(main SHARED
    ${all_test_cpp}
    ${CMAKE_CURRENT_BINARY_DIR}/nut_scripts.cpp
  )
  target_link_libraries(main eve_engine_includes)
elseif(EVENGINE_IOS_TEST_APP)
  # The zeroerr runner replaces main.cpp inside the eve .app target created by
  # src/engine/CMakeLists.txt (which owns the bundle id/plist/signing, MoltenVK
  # embedding and the whole-archive link list). Attach the suite sources here;
  # test/main.cpp provides SDL_main for the UIKit entry point.
  # nut_scripts.cpp is produced by a custom command declared in this directory;
  # the Xcode generator only wires generated sources into the target that
  # consumes them when they are declared in the same directory, so route the
  # generation through an explicit custom target the eve target depends on.
  add_custom_target(eve_nut_scripts ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/nut_scripts.cpp)
  add_dependencies(eve eve_nut_scripts)
  target_sources(eve PRIVATE
    ${all_test_cpp}
    ${CMAKE_CURRENT_BINARY_DIR}/nut_scripts.cpp
    ios_test.mm
  )
  set_source_files_properties(ios_test.mm PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(eve eve_engine_includes)
else()
  add_executable(
    unit_test
    ${all_test_cpp}
    ${CMAKE_CURRENT_BINARY_DIR}/nut_scripts.cpp
  )
  target_link_libraries(unit_test eve_engine_includes)
endif()

# zeroerr's REQUIRE/CHECK/ASSERT are compiled out under ZEROERR_NO_ASSERT
# (the root CMakeLists strips them from non-Debug builds). That silently
# deletes test setup calls written as REQUIRE(initSideEffect()), which made
# every graphics test fail in Release with "graphics not initialized". Keep
# test assertions live in the test binary so Release CI actually asserts.
set(_eve_test_assert_targets unit_test main)
if(EVENGINE_IOS_TEST_APP)
  list(APPEND _eve_test_assert_targets eve)
endif()
foreach(_eve_test_tgt IN LISTS _eve_test_assert_targets)
  if(TARGET ${_eve_test_tgt})
    target_compile_options(${_eve_test_tgt} PRIVATE
      $<$<CXX_COMPILER_ID:MSVC>:/UZEROERR_NO_ASSERT>
      $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UZEROERR_NO_ASSERT>)
  endif()
endforeach()

# Several tests include Poco headers (data, mcp, dap, remotesync). MSVC would
# otherwise #pragma-comment PocoFoundationd.lib, which the third-party prefix
# does not ship (it installs PocoFoundationmdd.lib).
if(WIN32)
  if(EVENGINE_ANDROID_TEST_APP)
    target_compile_definitions(main PRIVATE POCO_STATIC POCO_NO_AUTOMATIC_LIBS NOMINMAX)
  else()
    target_compile_definitions(unit_test PRIVATE POCO_STATIC POCO_NO_AUTOMATIC_LIBS NOMINMAX)
  endif()
endif()


# Platform system libraries, shared with the host binary
# (see cmake/system_libraries.cmake).
eve_append_system_libraries(SystemLibraries)
 # Keep the test runner's link closure identical to the host's manifest-derived
 # closure. The previous hand-written list linked codecs, OpenAL, Box2D and
 # PocoData even when a profile had removed their modules, making profile tests
 # falsely depend on the full third-party aggregate.
eve_thirdparty_libs(_eve_test_tp_libs ${EVE_THIRDPARTY_GROUPS})
set(ThirdParty ${_eve_test_tp_libs} ${SystemLibraries})

set(EVELIBS EVScripts ${EVE_MODULE_LIBS})

if(TARGET eve_imgui)
  list(APPEND EVELIBS eve_imgui)
endif()

if(EVENGINE_BUILD_DEMO)
  list(APPEND
    EVELIBS EVDemo)
endif()

if(EVENGINE_PLATFORM_LIB)
  list(APPEND EVELIBS ${EVENGINE_PLATFORM_LIB})
endif()

if(EVENGINE_ANDROID_TEST_APP)
  target_link_libraries(main ${EVELIBS} zeroerr ${ThirdParty} ${EVENGINE_VULKAN_LIB} ${EVENGINE_WEBGPU_LIB})
  # Source/binary roots are resolved at runtime: the Android test runner chdirs to
  # the unpacked asset root before running, so "." means "repo root on device".
  target_compile_definitions(main PRIVATE
    EVENGINE_TEST_BINARY_DIR="."
    EVENGINE_SOURCE_DIR="."
    EVENGINE_ANDROID=1)
  # Keep SDL_main / module registration symbols visible for the dlopened SDLActivity.
  set_target_properties(main PROPERTIES
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF)
elseif(EVENGINE_IOS_TEST_APP)
  # eve is already linked to ${EVELIBS} + zeroerr + third-party by
  # src/engine/CMakeLists.txt; only the on-device path roots differ. The runner
  # chdirs to the staged tree, so "." means "repo root on device".
  target_compile_definitions(eve PRIVATE
    EVENGINE_TEST_BINARY_DIR="."
    EVENGINE_SOURCE_DIR="."
    EVENGINE_IOS_TEST_APP=1)
else()
  target_link_libraries(unit_test ${EVELIBS} zeroerr ${ThirdParty} ${EVENGINE_VULKAN_LIB} ${EVENGINE_WEBGPU_LIB})
  if(WIN32)
    # Crash-backtrace support (backward-cpp / DbgHelp) so unit_test prints a
    # symbolized stack trace on unhandled exceptions (see test/main.cpp).
    target_link_libraries(unit_test EVBacktrace)
  endif()
  target_compile_definitions(unit_test PRIVATE
    EVENGINE_TEST_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    EVENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    EVENGINE_TEST_SHARED_LIBRARY_SUFFIX="${CMAKE_SHARED_LIBRARY_SUFFIX}")

  # A real shared library exercises native loading and C++ module registration
  # instead of testing only loader failures. It resolves engine symbols from
  # the unit-test host exactly as a shipped plugin resolves them from eve.
  add_library(native_test_plugin SHARED fixtures/native_test_plugin.cpp)
  target_compile_features(native_test_plugin PRIVATE cxx_std_20)
  target_compile_definitions(native_test_plugin PRIVATE EVENGINE_PLUGIN=1)
  target_link_libraries(native_test_plugin PRIVATE eve_engine_includes)
  set_target_properties(native_test_plugin PROPERTIES PREFIX "")
  if(WIN32)
    set_target_properties(unit_test PROPERTIES ENABLE_EXPORTS ON)
    target_link_libraries(native_test_plugin PRIVATE "$<TARGET_LINKER_FILE:unit_test>")
  elseif(APPLE)
    target_link_options(native_test_plugin PRIVATE "LINKER:-undefined,dynamic_lookup")
    target_link_options(unit_test PRIVATE "LINKER:-export_dynamic")
  elseif(UNIX)
    target_link_options(unit_test PRIVATE "LINKER:--export-dynamic")
  endif()
endif()

# Large classic scenes (Sponza / DamagedHelmet). Cornell Box is committed.
# Enable with -DEVENGINE_DOWNLOAD_CLASSIC_SCENES=ON, or build target
# `download_classic_scenes` before running ClassicScenes.* tests.
# (Skipped on the mobile test apps: the downloads target the host test tree,
# not the assets packaged into the APK/.app.)
if(NOT EVENGINE_ANDROID_TEST_APP AND NOT EVENGINE_IOS_TEST_APP)
option(EVENGINE_DOWNLOAD_CLASSIC_SCENES
  "Download large classic render test scenes (Sponza, DamagedHelmet)" OFF)
set(_eve_classic_scenes_script "${CMAKE_SOURCE_DIR}/scripts/download_classic_scenes.sh")
set(_eve_classic_scenes_marker "${CMAKE_SOURCE_DIR}/test/assets/classic/.downloaded")
add_custom_target(download_classic_scenes
  COMMAND ${CMAKE_COMMAND} -E env
          "EVENGINE_CLASSIC_SCENES_DIR=${CMAKE_SOURCE_DIR}/test/assets/classic"
          bash "${_eve_classic_scenes_script}"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "Downloading classic graphics test scenes (Sponza, DamagedHelmet)"
  VERBATIM)
if(EVENGINE_DOWNLOAD_CLASSIC_SCENES)
  add_custom_command(TARGET unit_test PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E env
            "EVENGINE_CLASSIC_SCENES_DIR=${CMAKE_SOURCE_DIR}/test/assets/classic"
            bash "${_eve_classic_scenes_script}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "PRE_BUILD: download classic scenes"
    VERBATIM)
endif()

# Free Spine demo models (spineboy / hero / dragon, region attachments only)
# for animation.spine.* window tests. Default ON; disable offline.
option(EVENGINE_DOWNLOAD_SPINE_MODELS
  "Download Spine demo models (spineboy, hero, dragon) for spine animation tests" ON)
set(_eve_spine_script "${CMAKE_SOURCE_DIR}/scripts/download_spine_models.py")
find_program(EVENGINE_PYTHON NAMES python3 python REQUIRED)
add_custom_target(download_spine_models
  COMMAND ${CMAKE_COMMAND} -E env
          "EVENGINE_SPINE_DIR=${CMAKE_SOURCE_DIR}/test/assets/spine"
          "${EVENGINE_PYTHON}" "${_eve_spine_script}"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "Downloading Spine demo models (spineboy, hero, dragon)"
  VERBATIM)
if(EVENGINE_DOWNLOAD_SPINE_MODELS)
  add_custom_command(TARGET unit_test PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E env
            "EVENGINE_SPINE_DIR=${CMAKE_SOURCE_DIR}/test/assets/spine"
            "${EVENGINE_PYTHON}" "${_eve_spine_script}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "PRE_BUILD: download Spine demo models"
    VERBATIM)
endif()

# Free skinned character (CesiumMan, ~0.5 MB) for animation.skinned.* tests.
# Default ON so test builds fetch the asset over the network; disable offline.
option(EVENGINE_DOWNLOAD_SKINNED_CHARACTER
  "Download CesiumMan skinned character for skeletal animation tests" ON)
set(_eve_skinned_script "${CMAKE_SOURCE_DIR}/scripts/download_skinned_character.sh")
add_custom_target(download_skinned_character
  COMMAND ${CMAKE_COMMAND} -E env
          "EVENGINE_SKINNED_DIR=${CMAKE_SOURCE_DIR}/test/assets/skinned"
          bash "${_eve_skinned_script}"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "Downloading skinned character (CesiumMan) for animation tests"
  VERBATIM)
if(EVENGINE_DOWNLOAD_SKINNED_CHARACTER)
  add_custom_command(TARGET unit_test PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E env
            "EVENGINE_SKINNED_DIR=${CMAKE_SOURCE_DIR}/test/assets/skinned"
            bash "${_eve_skinned_script}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "PRE_BUILD: download skinned character (CesiumMan)"
    VERBATIM)
endif()
endif()  # NOT mobile test app

if(NOT EVENGINE_ANDROID_TEST_APP AND NOT EVENGINE_IOS_TEST_APP)
include(${CMAKE_SOURCE_DIR}/cmake/ZeroErrDiscoverTests.cmake)
zeroerr_discover_tests(unit_test)
endif()
