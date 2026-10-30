enable_testing()

# Compile-fail fixtures for critical non-Result return values. This target is
# intentionally independent of unit_test and never triggers a full engine
# build by itself; CI also runs the same script in the fast structural job.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
if(NOT ANDROID AND NOT EVENGINE_ANDROID_TEST_APP AND NOT EVENGINE_IOS_TEST_APP)
  add_executable(assertion_contract_probe fixtures/assertion_contract.cpp)
  target_link_libraries(assertion_contract_probe PRIVATE zeroerr eve_engine_includes)
  target_compile_options(assertion_contract_probe PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/UZEROERR_NO_ASSERT>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UZEROERR_NO_ASSERT>)
  add_test(NAME test.assertionExitContract
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/check_test_assertions.py
      --runner $<TARGET_FILE:assertion_contract_probe>)
endif()
include("${CMAKE_SOURCE_DIR}/cmake/TestLevels.cmake")
eve_configure_test_levels(EVE_TEST_LEVEL_FILE)
include("${EVE_TEST_LEVEL_FILE}")
if(TARGET assertion_contract_probe)
  eve_label_test_group(test.assertionExitContract assertion_contract.cpp)
endif()
add_custom_target(nodiscard_compile_check
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/check_nodiscard.py
          --source-dir ${CMAKE_SOURCE_DIR}
  USES_TERMINAL)
add_test(NAME common.nodiscard_compile_diagnostics
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/check_nodiscard.py
          --source-dir ${CMAKE_SOURCE_DIR})
set_tests_properties(common.nodiscard_compile_diagnostics PROPERTIES LABELS "quality")


# Test sources are discovered like engine/module sources. Do not use
# CONFIGURE_DEPENDS: Ninja glob verification plus files appearing during a
# long configure restacks "Re-checking globbed directories" forever.
# Write test_src.txt only when the sorted glob changes. `make test/win32-debug`
# reconfigures when an individual test/*.cpp is newer than that list (not when
# anything under test/ is written).
file(GLOB all_test_cpp "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE organized_test_cpp "${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/integration/*.cpp")
set(all_test_cpp ${EVE_TEST_DESKTOP_ORIGINS})
option(EVENGINE_BUILD_TEST_BENCHMARKS "Build explicit timing workloads with the host test runner" OFF)
if(EVENGINE_BUILD_TEST_BENCHMARKS)
  file(GLOB_RECURSE _eve_benchmark_sources "${CMAKE_CURRENT_SOURCE_DIR}/benchmark/*.cpp")
  list(APPEND all_test_cpp ${_eve_benchmark_sources})
endif()
list(REMOVE_ITEM all_test_cpp
  "${CMAKE_CURRENT_SOURCE_DIR}/integration/crowd-runtime/CrowdThread.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/integration/rts-crowd/CrowdMotion.cpp")
# Canonical sources are deduplicated by eve_resolve_test_sources below. Former
# basename exclusions would silently omit the newly separated workflows.
eve_test_source_properties(physics_jolt.cpp physics_jolt_cooking.cpp physics_jolt_contacts.cpp PROPERTIES COMPILE_DEFINITIONS "EVENGINE_TEST_HAS_JOLT=$<BOOL:${EVENGINE_BUILD_JOLT}>")
# Resource sharing must test the same optional validation provider as EVGraphics.
get_target_property(_eve_graphics_definitions EVGraphics COMPILE_DEFINITIONS)
if("EVENGINE_SPIRV_ASSET_VALIDATION=1" IN_LIST _eve_graphics_definitions)
  eve_resolve_test_sources(_eve_validation_sources ShaderResourceSharing.cpp SkyTextureGpu.cpp
    SkyAtmospherePass.cpp SkyRuntime.cpp SkyWisps.cpp SkyScriptBindings.cpp BloomPyramid.cpp)
  set_property(SOURCE ${_eve_validation_sources} APPEND PROPERTY COMPILE_DEFINITIONS EVE_TEST_RESOURCE_VALIDATION=1)
endif()
# The legacy RNG oracle uses the same pinned DTL compatibility headers as procgen.
eve_test_source_properties(procgen_rng_contract.cpp PROPERTIES INCLUDE_DIRECTORIES
  "${CMAKE_SOURCE_DIR}/src/modules/procgen/dtl_compat")
eve_module_enabled(crowd_thread _eve_test_has_crowd_thread)
if(_eve_test_has_crowd_thread)
  list(APPEND all_test_cpp "${CMAKE_CURRENT_SOURCE_DIR}/integration/crowd-runtime/CrowdThread.cpp")
  eve_module_enabled(rts _eve_test_parallel_rts)
  if(_eve_test_parallel_rts)
    list(APPEND all_test_cpp "${CMAKE_CURRENT_SOURCE_DIR}/integration/rts-crowd/CrowdMotion.cpp")
  endif()
endif()
list(SORT all_test_cpp)
set(_eve_test_src_txt "${CMAKE_CURRENT_BINARY_DIR}/test_src.txt")
set(_eve_test_src_text "")
eve_resolve_test_sources(_eve_snapshot_sources ${all_test_cpp})
list(SORT _eve_snapshot_sources)
foreach(_eve_test_cpp IN LISTS _eve_snapshot_sources)
    file(TO_CMAKE_PATH "${_eve_test_cpp}" _eve_test_cpp)
    string(APPEND _eve_test_src_text "${_eve_test_cpp}\n")
endforeach()
if(EXISTS "${_eve_test_src_txt}")
    file(READ "${_eve_test_src_txt}" _eve_test_src_old)
else()
    set(_eve_test_src_old "")
endif()
if(NOT _eve_test_src_text STREQUAL _eve_test_src_old)
    file(WRITE "${_eve_test_src_txt}" "${_eve_test_src_text}")
endif()
list(REMOVE_ITEM all_test_cpp "${CMAKE_CURRENT_SOURCE_DIR}/demo.cpp")

# The FBX success fixtures construct files with Assimp's native exporter.
# Emscripten uses UnityFbx.cpp's explicit provider-absent implementation, covered
# separately by test/asset_import/UnityFbxUnavailable.cpp.
if(EMSCRIPTEN)
  list(FILTER all_test_cpp EXCLUDE REGEX "/asset_import_unity_fbx.cpp$")
endif()

if(EVENGINE_BUILD_DEMO)
  list(APPEND all_test_cpp demo.cpp)
endif()

# Desktop-only DevTools (DAP / MCP / debugger / callgraph / renderflow) are not
# compiled into the Android/iOS runtimes, so their test files cannot link there.
# all_test_cpp holds absolute paths from file(GLOB), so filter by basename.
if(ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Android") OR EVENGINE_IOS_TEST_APP)
  foreach(_eve_devtool_test IN ITEMS
      callgraph.cpp renderflow.cpp debugger.cpp debugger_audit.cpp snapshot_devtools_references_tests.cpp reload_script.cpp dap.cpp agent_development_session.cpp play_host.cpp play_trace.cpp mcp.cpp mcp_console.cpp mcp_agent_export.cpp mcp_editor_targets.cpp mcp_domain_tools.cpp mcp_input_tools.cpp mcp_tool_filter.cpp mcp_soft_restart.cpp console.cpp frame_stats.cpp runtime_error_sink.cpp scenario_recorder.cpp language_index.cpp language_server.cpp language_text.cpp)
    list(FILTER all_test_cpp EXCLUDE REGEX "/${_eve_devtool_test}$")
  endforeach()
  message(STATUS "Mobile test app: removed devtools test files, remaining=${all_test_cpp}")
endif()

# These tests intentionally exercise Vulkan implementation details rather than
# the backend-neutral graphics API, so they cannot be linked into a WebGPU test
# binary. Merged Vulkan-only contract sections select the backend in source.
# Backend parity coverage lives in graphics_backend_parity.cpp.
if(BUILD_PLATFORM STREQUAL "webgpu")
  foreach(_eve_vulkan_test IN ITEMS shader_reload_vulkan.cpp physics_layered_cloth_gpu.cpp)
    list(FILTER all_test_cpp EXCLUDE REGEX "/${_eve_vulkan_test}$")
  endforeach()
endif()

# Tests that include a sibling module's headers are compiled only when that
# module is in the profile. Presence is a CMake source list, not EVENGINE_HAS_*.
eve_module_enabled(agent _eve_test_has_agent)
eve_module_enabled(grid _eve_agent_test_has_grid)
eve_module_enabled(ui _eve_agent_test_has_ui)
if(NOT _eve_test_has_agent)
  list(FILTER all_test_cpp EXCLUDE REGEX "/agent.cpp$")
  list(FILTER all_test_cpp EXCLUDE REGEX "/agent_script.cpp$")
endif()
eve_module_enabled(agent_tensor _eve_test_has_agent_tensor)
if(NOT _eve_test_has_agent_tensor)
  list(FILTER all_test_cpp EXCLUDE REGEX "/agent_tensor.cpp$")
  list(FILTER all_test_cpp EXCLUDE REGEX "/agent_tensor_script.cpp$")
endif()
if(NOT _eve_test_has_agent OR NOT _eve_agent_test_has_grid OR NOT _eve_agent_test_has_ui)
  list(FILTER all_test_cpp EXCLUDE REGEX "/agent_composition.cpp$")
endif()
eve_module_enabled(map _eve_test_has_map)
if(NOT _eve_test_has_map)
  list(FILTER all_test_cpp EXCLUDE REGEX "/editor_map.cpp$")
endif()
eve_module_enabled(combat_navigation _eve_test_has_combat_navigation)
if(NOT _eve_test_has_combat_navigation)
  list(FILTER all_test_cpp EXCLUDE REGEX "/combat_navigation.cpp$")
endif()
eve_module_enabled(sceneloader _eve_test_has_sceneloader)
if(NOT _eve_test_has_sceneloader)
  list(FILTER all_test_cpp EXCLUDE REGEX "/action_prefab_runtime.cpp$")
endif()
eve_module_enabled(map_editing _eve_test_has_map_editing)
if(NOT _eve_test_has_map OR NOT _eve_test_has_map_editing)
  list(FILTER all_test_cpp EXCLUDE REGEX "/map_tile_editing.cpp$")
endif()
eve_module_enabled(level_editing _eve_test_has_level_editing)
if(NOT _eve_test_has_level_editing)
  list(FILTER all_test_cpp EXCLUDE REGEX "/editor_tilemap_formats.cpp$")
endif()
eve_module_enabled(physics _eve_test_has_physics)
if(NOT _eve_test_has_physics)
  list(FILTER all_test_cpp EXCLUDE REGEX "/vehicle_physics.cpp$")
endif()
# UI Motion binders require both retained UI and the animation Motion runtime.
eve_module_enabled(animation _eve_test_has_animation)
eve_module_enabled(ui _eve_test_has_ui)
if(NOT _eve_test_has_animation OR NOT _eve_test_has_ui)
  list(FILTER all_test_cpp EXCLUDE REGEX "/ui_motion_bind.cpp$")
endif()
eve_module_enabled(devtools _eve_test_has_devtools)
if(NOT _eve_test_has_devtools)
  list(FILTER all_test_cpp EXCLUDE REGEX "/snapshot_devtools_references_tests\\.cpp$")
  foreach(_eve_lsp_test IN ITEMS language_index.cpp language_server.cpp language_text.cpp play_host.cpp play_trace.cpp mcp_console.cpp mcp_agent_export.cpp mcp_editor_targets.cpp mcp_domain_tools.cpp mcp_input_tools.cpp mcp_tool_filter.cpp)
    list(FILTER all_test_cpp EXCLUDE REGEX "/${_eve_lsp_test}$")
  endforeach()
endif()

eve_test_origin_definitions(_eve_test_origin_definitions ${all_test_cpp})
eve_resolve_test_sources(all_test_cpp ${all_test_cpp})

# Partition the profile-filtered sources into one link unit per domain. The rule
# lives in scripts/test_domains.py, which scripts/check_test_manifest.py also
# uses; CMake consumes the generated partition instead of re-implementing it so
# the two cannot drift. A source matching no rule, or two rules, fails here with
# the offending file named. The rule file is a configure dependency: editing the
# table (or SHARED_SOURCES) has to regenerate the partition, and nothing else in
# this directory would notice.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/test_domains.cmake"
  "${CMAKE_SOURCE_DIR}/scripts/test_domains.py")
include(${CMAKE_CURRENT_SOURCE_DIR}/test_domains.cmake)
set(_eve_test_domain_input "${CMAKE_CURRENT_BINARY_DIR}/test_domain_input.txt")
file(WRITE "${_eve_test_domain_input}" "")
foreach(_eve_test_cpp IN LISTS all_test_cpp)
  # demo.cpp is appended by bare name (see the EVENGINE_BUILD_DEMO branch above),
  # so normalize every entry: the partition rule resolves paths from the list
  # verbatim and rejects anything that is not absolute.
  get_filename_component(_eve_test_cpp_abs "${_eve_test_cpp}" ABSOLUTE
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  file(APPEND "${_eve_test_domain_input}" "${_eve_test_cpp_abs}\n")
endforeach()
set(_eve_test_domain_partition "${CMAKE_CURRENT_BINARY_DIR}/test_domain_sources.cmake")
execute_process(
  COMMAND "${Python3_EXECUTABLE}" -X utf8
          "${CMAKE_SOURCE_DIR}/scripts/test_domains.py"
          --input "${_eve_test_domain_input}"
          --output "${_eve_test_domain_partition}"
  RESULT_VARIABLE _eve_test_domain_status
  ERROR_VARIABLE _eve_test_domain_error)
if(NOT _eve_test_domain_status EQUAL 0)
  message(FATAL_ERROR
    "test domain partition failed; every test/*.cpp must match exactly one "
    "domain in test/test_domains.cmake:\n${_eve_test_domain_error}")
endif()
include("${_eve_test_domain_partition}")

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
  set(EVE_TEST_TARGETS main)
  add_library(main SHARED
    ${all_test_cpp}
    ${CMAKE_CURRENT_BINARY_DIR}/nut_scripts.cpp
  )
  target_link_libraries(main eve_engine_includes)
  target_include_directories(main PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
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
  eve_target_test_sources(eve PRIVATE
    ${all_test_cpp}
    ${CMAKE_CURRENT_BINARY_DIR}/nut_scripts.cpp
    ios_test.mm
  )
  eve_apply_test_origin_definitions(eve ${_eve_test_origin_definitions})
  eve_test_source_properties(ios_test.mm PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(eve eve_engine_includes)
else()
  # Desktop. The suite has two shapes, chosen by EVENGINE_TEST_DOMAIN_SPLIT:
  #
  # * split (default on the dynamic route): one executable per domain, so an
  #   agent links only the domain it is testing and no single link unit carries
  #   all 700+ test translation units. Each executable is a thin consumer of the
  #   link-group libraries, so the split costs almost nothing in disk.
  # * monolithic (default on the archive route): the whole suite in one
  #   executable, as it was before the partition. In the archive route every
  #   executable statically contains the whole engine, and 30 of them plus their
  #   debug info measured 93 GB on Windows, needed serial linking to avoid
  #   exhausting the disk, and still took longer than one binary holding the same
  #   code once.
  #
  # Either way the runner (main.cpp) and the shared helper translation units a
  # domain needs (EVE_TEST_DOMAIN_<domain>_SHARED_SOURCES, see SHARED_SOURCES in
  # scripts/test_domains.py) are compiled into the executable that links them:
  # a test helper another domain defines cannot cross a link unit any other way.
  set(EVE_TEST_TARGETS "")
  set(_eve_test_runner "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
  # Compile the generated script fixtures once, in their own target, and link that
  # archive into every test executable. Listing nut_scripts.cpp as a source of
  # each target instead would make the Unix Makefiles generator emit one rule for
  # that output per target; those rules are not deduplicated, so under `make -j`
  # one target regenerates (and briefly deletes) nut_scripts.cpp while another is
  # compiling it -- CI's Linux job failed with
  # "integrate.cmake:8 (configure_file): No such file or directory" and
  # "*** Deleting file 'test/nut_scripts.cpp'". Ninja deduplicates, which is why
  # local Ninja builds never showed it.
  add_library(eve_nut_fixtures STATIC ${CMAKE_CURRENT_BINARY_DIR}/nut_scripts.cpp)

  set(_eve_test_split_default OFF)
  if(EVENGINE_MODULE_LINKAGE STREQUAL "SHARED")
    set(_eve_test_split_default ON)
  endif()
  set(EVENGINE_TEST_DOMAIN_SPLIT "${_eve_test_split_default}" CACHE BOOL
      "Build one test executable per domain instead of a single unit_test (default: on for SHARED, off for OBJECT)")

  eve_ensure_graphics_window_fixtures()
  list(REMOVE_ITEM all_test_cpp "${CMAKE_CURRENT_SOURCE_DIR}/GraphicsWindowFixtures.cpp")

  if(EVENGINE_TEST_DOMAIN_SPLIT)
    foreach(_eve_domain IN LISTS EVE_TEST_DOMAINS)
      if(NOT DEFINED EVE_TEST_DOMAIN_${_eve_domain}_SOURCES)
        # Every test of this domain was filtered out by the profile.
        continue()
      endif()
      set(_eve_test_target "unit_test_${_eve_domain}")
      set(_eve_domain_helpers ${EVE_TEST_DOMAIN_${_eve_domain}_SHARED_SOURCES})
      set(_eve_window_helper "${CMAKE_CURRENT_SOURCE_DIR}/GraphicsWindowFixtures.cpp")
      list(REMOVE_ITEM _eve_domain_helpers "${_eve_window_helper}")
      eve_add_test_executable(${_eve_test_target}
        ${EVE_TEST_DOMAIN_${_eve_domain}_SOURCES}
        ${_eve_domain_helpers}
        ${_eve_test_runner}
      )
      target_link_libraries(${_eve_test_target} eve_engine_includes eve_nut_fixtures)
      if("${_eve_window_helper}" IN_LIST EVE_TEST_DOMAIN_${_eve_domain}_SHARED_SOURCES)
        target_link_libraries(${_eve_test_target} eve_graphics_window_fixtures)
      endif()
      # CTest includes <target>_zeroerr_tests.cmake, which that target's POST_BUILD
      # discovery step writes. Seed an empty stub at configure time when it is
      # absent: otherwise ctest refuses to run at all ("include" error) until every
      # domain has been built once, which defeats building and running a single
      # domain. The real file replaces the stub as soon as the target is linked.
      set(_eve_test_discovery
        "${CMAKE_CURRENT_BINARY_DIR}/${_eve_test_target}_zeroerr_tests.cmake")
      if(NOT EXISTS "${_eve_test_discovery}")
        file(WRITE "${_eve_test_discovery}"
          "# populated when ${_eve_test_target} is built\n")
      endif()
      list(APPEND EVE_TEST_TARGETS ${_eve_test_target})
    endforeach()
    list(LENGTH EVE_TEST_TARGETS _eve_test_target_count)
    if(_eve_test_target_count EQUAL 0)
      message(FATAL_ERROR
        "no test domain produced a target; check test/test_domains.cmake")
    endif()
    message(STATUS "Test units: ${_eve_test_target_count} domain executables")
  else()
    eve_add_test_executable(unit_test ${all_test_cpp})
    eve_configure_monolithic_test_linker(unit_test)
    target_link_libraries(unit_test eve_engine_includes eve_nut_fixtures eve_graphics_window_fixtures)
    set(_eve_test_discovery
      "${CMAKE_CURRENT_BINARY_DIR}/unit_test_zeroerr_tests.cmake")
    if(NOT EXISTS "${_eve_test_discovery}")
      file(WRITE "${_eve_test_discovery}"
        "# populated when unit_test is built\n")
    endif()
    list(APPEND EVE_TEST_TARGETS unit_test)
    message(STATUS "Test units: 1 monolithic executable (EVENGINE_TEST_DOMAIN_SPLIT=OFF)")
  endif()
endif()

# zeroerr's REQUIRE/CHECK/ASSERT are compiled out under ZEROERR_NO_ASSERT
# (the root CMakeLists strips them from non-Debug builds). That silently
# deletes test setup calls written as REQUIRE(initSideEffect()), which made
# every graphics test fail in Release with "graphics not initialized". Keep
# test assertions live in the test binary so Release CI actually asserts.
set(_eve_test_assert_targets ${EVE_TEST_TARGETS} main)
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
    foreach(_eve_test_target IN LISTS EVE_TEST_TARGETS)
      target_compile_definitions(${_eve_test_target} PRIVATE
        POCO_STATIC POCO_NO_AUTOMATIC_LIBS NOMINMAX)
    endforeach()
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

set(EVELIBS EVScripts ${EVE_LINK_TARGETS})

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
  foreach(_eve_test_target IN LISTS EVE_TEST_TARGETS)
    # Third-party first, deliberately. The static Squirrel/SimpleSquirrel archives are
    # compiled with `__declspec(dllexport)` (cmake/patches/third-party-squirrel-ssq-export.patch)
    # so that the host binary can export that API to plugins. The group DLLs link the same
    # archives, so they export the API too. A test executable links both, and the static
    # definition must be the one the linker sees first: with `${EVELIBS}` first, MSVC selects
    # the import-library thunk from a group DLL and then collides with the archive definition
    # (LNK2005, 40 symbols). Ordering the third-party closure first keeps the static definition
    # authoritative, which is also the order the OBJECT route effectively has.
    target_link_libraries(${_eve_test_target} ${ThirdParty} ${EVELIBS} zeroerr ${EVENGINE_VULKAN_LIB} ${EVENGINE_WEBGPU_LIB})
  endforeach()
  if(TARGET EVTensor AND NOT EVENGINE_WEBGPU)
    eve_add_test_executable(onnx_engine_probe EXCLUDE_FROM_ALL "${CMAKE_SOURCE_DIR}/examples/tensor/onnx/main.cpp")
    target_compile_definitions(onnx_engine_probe PRIVATE EVE_ONNX_ENGINE_PROVIDER POCO_STATIC POCO_NO_AUTOMATIC_LIBS NOMINMAX)
    target_link_libraries(onnx_engine_probe eve_engine_includes ${EVELIBS} zeroerr ${ThirdParty} ${EVENGINE_VULKAN_LIB})
    eve_add_test_executable(onnx_gpu_contracts EXCLUDE_FROM_ALL main.cpp tensor_onnx_gpu.cpp)
    target_compile_definitions(onnx_gpu_contracts PRIVATE POCO_STATIC POCO_NO_AUTOMATIC_LIBS NOMINMAX
      EVENGINE_TEST_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}"
      EVENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_compile_options(onnx_gpu_contracts PRIVATE
      $<$<CXX_COMPILER_ID:MSVC>:/UZEROERR_NO_ASSERT>
      $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UZEROERR_NO_ASSERT>)
    target_link_libraries(onnx_gpu_contracts eve_engine_includes ${EVELIBS} zeroerr ${ThirdParty} ${EVENGINE_VULKAN_LIB})


    if(WIN32)
      target_link_libraries(onnx_engine_probe EVBacktrace)
      target_link_libraries(onnx_gpu_contracts EVBacktrace)
    endif()
  endif()

  # The same house contract runs with and without authoring/renderer satellites.
  if(TARGET EVBuilding)
    eve_add_test_executable(building_workflow_test EXCLUDE_FROM_ALL
      profile/gltf_animation_main.cpp integration/building-workflow/building_assembly.cpp integration/building-workflow/building_roof_support.cpp)
    if(TARGET EVBuilding_editing)
      eve_target_test_sources(building_workflow_test PRIVATE integration/building-workflow/editor_building_assembly.cpp editor_building_target.cpp)
    endif()
    if(TARGET EVMap)
      eve_target_test_sources(building_workflow_test PRIVATE building.cpp)
    endif()
    target_link_libraries(building_workflow_test eve_engine_includes ${EVELIBS} zeroerr ${ThirdParty}
      ${EVENGINE_VULKAN_LIB} ${EVENGINE_WEBGPU_LIB})
    target_compile_options(building_workflow_test PRIVATE
      $<$<CXX_COMPILER_ID:MSVC>:/UZEROERR_NO_ASSERT>
      $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UZEROERR_NO_ASSERT>)
    if(WIN32)
      target_link_libraries(building_workflow_test EVBacktrace)
      target_compile_definitions(building_workflow_test PRIVATE POCO_STATIC POCO_NO_AUTOMATIC_LIBS NOMINMAX)
    endif()
  endif()

  # Scene composition must also run in a trimmed host, where unrelated unit-test
  # translation units still reference disabled optional domains.
  if(TARGET EVScene_editor)
    eve_add_test_executable(scene_editor_component_test EXCLUDE_FROM_ALL
      main.cpp scene_editing_commands.cpp scene_editor_session.cpp
      scene_editor_live_session.cpp scene_editor_script.cpp scene_editor_gizmo.cpp
      editor_scene_target_v2.cpp editor_scene_runtime.cpp)
    target_link_libraries(scene_editor_component_test eve_engine_includes ${EVELIBS} zeroerr ${ThirdParty}
      ${EVENGINE_VULKAN_LIB} ${EVENGINE_WEBGPU_LIB})
    target_compile_options(scene_editor_component_test PRIVATE
      $<$<CXX_COMPILER_ID:MSVC>:/UZEROERR_NO_ASSERT>
      $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UZEROERR_NO_ASSERT>)
    if(WIN32)
      target_link_libraries(scene_editor_component_test EVBacktrace)
      target_compile_definitions(scene_editor_component_test PRIVATE POCO_STATIC POCO_NO_AUTOMATIC_LIBS NOMINMAX)
    endif()
  endif()
  # Manual real-backend asset admission/visual probe; purchased inputs are never test fixtures.
  if(TARGET EVAsset_graphics AND TARGET EVAsset_scene)
    if(TARGET EVAsset_import)
      if(NOT ANDROID AND NOT EVENGINE_ANDROID AND NOT EVENGINE_IOS AND NOT IOS AND NOT EMSCRIPTEN)
        eve_add_test_executable(scad_compiler_timeout_fixture asset_import/ScadCompilerTimeoutFixture.cpp)
        target_compile_features(scad_compiler_timeout_fixture PRIVATE cxx_std_20)
        eve_resolve_test_sources(_eve_scad_sources asset_import_structured_models.cpp)
        set_property(SOURCE ${_eve_scad_sources} APPEND PROPERTY COMPILE_DEFINITIONS
          EVE_SCAD_TEST_COMPILER="$<TARGET_FILE:scad_compiler_timeout_fixture>")
        foreach(_eve_test_target IN LISTS EVE_TEST_TARGETS)
          get_target_property(_eve_target_sources ${_eve_test_target} SOURCES)
          foreach(_eve_scad_source IN LISTS _eve_scad_sources)
            if(_eve_scad_source IN_LIST _eve_target_sources)
              add_dependencies(${_eve_test_target} scad_compiler_timeout_fixture)
            endif()
          endforeach()
        endforeach()
      endif()
      eve_add_test_executable(structured_model_graphics_probe EXCLUDE_FROM_ALL asset_import/StructuredModelGraphicsProbe.cpp)
      target_link_libraries(structured_model_graphics_probe eve_engine_includes ${EVELIBS} zeroerr ${ThirdParty}
        ${EVENGINE_VULKAN_LIB} ${EVENGINE_WEBGPU_LIB})
      if(WIN32)
        target_link_libraries(structured_model_graphics_probe EVBacktrace)
      endif()
    endif()
    eve_add_test_executable(unity_graphics_probe EXCLUDE_FROM_ALL asset_import/UnityGraphicsProbe.cpp)
    if(TARGET EVAsset_import AND TARGET EVProcgen AND TARGET EVScene
        AND NOT ANDROID AND NOT EVENGINE_ANDROID AND NOT EVENGINE_IOS AND NOT IOS AND NOT EMSCRIPTEN)
      eve_add_test_executable(road_side_graphics_probe EXCLUDE_FROM_ALL asset_import/RoadSideGraphicsProbe.cpp)
      target_link_libraries(road_side_graphics_probe eve_engine_includes ${EVELIBS} zeroerr ${ThirdParty}
        ${EVENGINE_VULKAN_LIB} ${EVENGINE_WEBGPU_LIB})
      if(WIN32)
        target_link_libraries(road_side_graphics_probe EVBacktrace)
      endif()
    endif()
    eve_add_test_executable(unity_sprite_graphics_probe EXCLUDE_FROM_ALL asset_import/UnitySpriteGraphicsProbe.cpp)
    target_link_libraries(unity_sprite_graphics_probe eve_engine_includes ${EVELIBS} zeroerr ${ThirdParty}
      ${EVENGINE_VULKAN_LIB} ${EVENGINE_WEBGPU_LIB})
    target_link_libraries(unity_graphics_probe eve_engine_includes ${EVELIBS} zeroerr ${ThirdParty}
                          ${EVENGINE_VULKAN_LIB} ${EVENGINE_WEBGPU_LIB})
    if(WIN32)
      target_link_libraries(unity_graphics_probe EVBacktrace)
    endif()
  endif()
  if(WIN32)
    # Crash-backtrace support (backward-cpp / DbgHelp) so the test binaries print
    # a symbolized stack trace on unhandled exceptions (see test/main.cpp).
    foreach(_eve_test_target IN LISTS EVE_TEST_TARGETS)
      target_link_libraries(${_eve_test_target} EVBacktrace)
    endforeach()
  endif()
  foreach(_eve_test_target IN LISTS EVE_TEST_TARGETS)
    target_compile_definitions(${_eve_test_target} PRIVATE
      EVENGINE_TEST_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}"
      EVENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
      EVENGINE_TEST_SHARED_LIBRARY_SUFFIX="${CMAKE_SHARED_LIBRARY_SUFFIX}")
    if(TARGET EVAsset_graphics)
      # The full resource suite inventories the graphics UV upload contract too.
      target_compile_definitions(${_eve_test_target} PRIVATE EVE_TEST_MESH_UPLOAD=1)
    endif()
  endforeach()

  # A real shared library exercises native loading and C++ module registration
  # instead of testing only loader failures. It resolves engine symbols from
  # the unit-test host exactly as a shipped plugin resolves them from eve.
  add_library(native_test_plugin SHARED fixtures/native_test_plugin.cpp)
  target_compile_features(native_test_plugin PRIVATE cxx_std_20)
  target_compile_definitions(native_test_plugin PRIVATE EVENGINE_PLUGIN=1)
  target_link_libraries(native_test_plugin PRIVATE eve_engine_includes)
  set_target_properties(native_test_plugin PROPERTIES PREFIX "")
  # test/plugins.cpp links this shared library against the running test host, so
  # that host must export engine symbols. With one executable per domain the host
  # is whichever domain owns plugins.cpp (`core`); when the suite is monolithic
  # the host is unit_test itself. Fail loudly if the partition ever moves
  # plugins.cpp rather than silently dropping plugin-export coverage.
  if(EVENGINE_TEST_DOMAIN_SPLIT)
    set(_eve_plugin_host "unit_test_core")
    if(NOT TARGET ${_eve_plugin_host})
      message(FATAL_ERROR
        "test/plugins.cpp must be classified into ${_eve_plugin_host} so "
        "native_test_plugin can resolve engine symbols from the test host")
    endif()
  else()
    set(_eve_plugin_host "unit_test")
  endif()
  if(EVENGINE_MODULE_LINKAGE STREQUAL "SHARED")
    # SHARED module linkage: the host executable has no export table at all (it
    # is a thin consumer of the link-group libraries and there is no
    # unit_test_core.lib), so a plugin cannot resolve engine symbols from it --
    # the link fails with LNK1104 and the default build breaks. The shipped
    # model is the same one a real plugin follows: link the engine's import
    # libraries, here the per-link-group DLLs, and let the loader resolve them
    # from the group libraries installed beside the host.
    target_link_libraries(native_test_plugin PRIVATE ${EVE_LINK_TARGETS})
  elseif(WIN32)
    set_target_properties(${_eve_plugin_host} PROPERTIES ENABLE_EXPORTS ON)
    target_link_libraries(native_test_plugin PRIVATE "$<TARGET_LINKER_FILE:${_eve_plugin_host}>")
  elseif(APPLE)
    target_link_options(native_test_plugin PRIVATE "LINKER:-undefined,dynamic_lookup")
    target_link_options(${_eve_plugin_host} PRIVATE "LINKER:-export_dynamic")
  elseif(UNIX)
    target_link_options(${_eve_plugin_host} PRIVATE "LINKER:--export-dynamic")
  endif()
  foreach(_variant IN ITEMS v1 v2 fail_init throw_init fail_bind conflict no_entry)
    set(_plugin "reload_test_plugin_${_variant}")
    add_library(${_plugin} SHARED fixtures/reload_test_plugin.cpp)
    target_compile_features(${_plugin} PRIVATE cxx_std_20)
    target_compile_definitions(${_plugin} PRIVATE EVENGINE_PLUGIN=1)
    target_link_libraries(${_plugin} PRIVATE eve_engine_includes)
    set_target_properties(${_plugin} PROPERTIES PREFIX ""
      CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
    if(NOT _variant STREQUAL "v1")
      target_compile_definitions(${_plugin} PRIVATE PLUGIN_VERSION=2)
    endif()
    if(_variant STREQUAL "fail_init")
      target_compile_definitions(${_plugin} PRIVATE PLUGIN_FAILURE=1)
    elseif(_variant STREQUAL "throw_init")
      target_compile_definitions(${_plugin} PRIVATE PLUGIN_FAILURE=2)
    elseif(_variant STREQUAL "fail_bind")
      target_compile_definitions(${_plugin} PRIVATE PLUGIN_FAILURE=3)
    elseif(_variant STREQUAL "conflict")
      target_compile_definitions(${_plugin} PRIVATE PLUGIN_FAILURE=4)
    elseif(_variant STREQUAL "no_entry")
      target_compile_definitions(${_plugin} PRIVATE PLUGIN_FAILURE=5)
    endif()
    if(EVENGINE_MODULE_LINKAGE STREQUAL "SHARED")
      target_link_libraries(${_plugin} PRIVATE ${EVE_LINK_TARGETS})
    elseif(WIN32)
      target_link_libraries(${_plugin} PRIVATE "$<TARGET_LINKER_FILE:${_eve_plugin_host}>")
    elseif(APPLE)
      target_link_options(${_plugin} PRIVATE "LINKER:-undefined,dynamic_lookup")
    endif()
  endforeach()
endif()

# Large classic scenes (Sponza / DamagedHelmet). Cornell Box is committed.
# Enable with -DEVENGINE_DOWNLOAD_CLASSIC_SCENES=ON, or build target
# `download_classic_scenes` before running ClassicScenes.* tests.
# (Skipped on the mobile test apps: the downloads target the host test tree,
# not the assets packaged into the APK/.app.)
# Asset prefetch hooks attach to the domain executable that consumes the asset,
# not to every test unit: with one executable per domain a hook on each would
# re-run the same download for all of them. The consuming tests (ClassicScenes,
# animation_sprite_spine, animation_skinned) are all classified into the
# `graphics` domain, so that is where the hooks go.
function(eve_test_prefetch_asset target)
  if(TARGET ${target})
    add_custom_command(TARGET ${target} PRE_BUILD ${ARGN})
  else()
    message(STATUS "asset prefetch skipped: ${target} is not built in this profile")
  endif()
endfunction()

# Prefetch hooks hang off the executable that consumes the asset: the domain
# executable that owns the tests with the split, the single unit_test without it.
if(EVENGINE_TEST_DOMAIN_SPLIT)
  set(_eve_asset_host "unit_test_graphics")
else()
  set(_eve_asset_host "unit_test")
endif()

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
  eve_test_prefetch_asset(${_eve_asset_host}
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
if(WIN32)
  unset(EVENGINE_PYTHON CACHE)
  find_program(EVENGINE_PYTHON NAMES python REQUIRED)
else()
  find_program(EVENGINE_PYTHON NAMES python3 python REQUIRED)
endif()
add_custom_target(download_spine_models
  COMMAND ${CMAKE_COMMAND} -E env
          "EVENGINE_SPINE_DIR=${CMAKE_SOURCE_DIR}/test/assets/spine"
          "${EVENGINE_PYTHON}" "${_eve_spine_script}"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "Downloading Spine demo models (spineboy, hero, dragon)"
  VERBATIM)
if(EVENGINE_DOWNLOAD_SPINE_MODELS)
  eve_test_prefetch_asset(${_eve_asset_host}
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
if(WIN32)
  find_program(EVENGINE_GIT_BASH_EXECUTABLE
    NAMES bash.exe
    PATHS
      "$ENV{ProgramFiles}/Git/bin"
      "$ENV{LOCALAPPDATA}/Programs/Git/bin"
    NO_DEFAULT_PATH)
  if(NOT EVENGINE_GIT_BASH_EXECUTABLE)
    message(FATAL_ERROR "Git Bash is required to download the skinned-character test assets")
  endif()
  set(_eve_skinned_shell "${EVENGINE_GIT_BASH_EXECUTABLE}")
else()
  set(_eve_skinned_shell bash)
endif()
add_custom_target(download_skinned_character
  COMMAND ${CMAKE_COMMAND} -E env
          "EVENGINE_SKINNED_DIR=${CMAKE_SOURCE_DIR}/test/assets/skinned"
          "${_eve_skinned_shell}" -lc "${_eve_skinned_script}"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "Downloading skinned character (CesiumMan) for animation tests"
  VERBATIM)
if(EVENGINE_DOWNLOAD_SKINNED_CHARACTER)
  eve_test_prefetch_asset(${_eve_asset_host}
    COMMAND ${CMAKE_COMMAND} -E env
            "EVENGINE_SKINNED_DIR=${CMAKE_SOURCE_DIR}/test/assets/skinned"
            "${_eve_skinned_shell}" -lc "${_eve_skinned_script}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "PRE_BUILD: download skinned character (CesiumMan)"
    VERBATIM)
endif()
endif()  # NOT mobile test app

if(NOT EVENGINE_ANDROID_TEST_APP AND NOT EVENGINE_IOS_TEST_APP)
include(${CMAKE_SOURCE_DIR}/cmake/ZeroErrDiscoverTests.cmake)
# Each domain executable discovers its own cases; ZeroErrDiscoverTests writes one
# <target>_zeroerr_tests.cmake and appends it to TEST_INCLUDE_FILES, so ctest sees
# the union with per-case isolation unchanged. The label is the target name, which
# is what `make test/<platform> DOMAIN=<domain>` selects on.
foreach(_eve_test_target IN LISTS EVE_TEST_TARGETS)
  eve_apply_test_origin_definitions(${_eve_test_target} ${_eve_test_origin_definitions})
  zeroerr_discover_tests(${_eve_test_target} LABEL "${_eve_test_target}")
endforeach()
endif()
include(asset_shader/GraphicsProbe.cmake)
if(TARGET EVTensor AND NOT EVENGINE_WEBGPU AND NOT EVENGINE_ANDROID_TEST_APP AND NOT EVENGINE_IOS_TEST_APP)
  # Isolate the controlled compiler double from the real Gpgpu provider.
  eve_add_test_executable(onnx_compiler_queue_contract
    fixtures/onnx_compiler_queue_contract.cpp
    "${CMAKE_SOURCE_DIR}/src/engine/common/Result.cpp"
    "${CMAKE_SOURCE_DIR}/src/engine/common/Status.cpp"
    "${CMAKE_SOURCE_DIR}/src/engine/common/Diagnostic.cpp")
  target_link_libraries(onnx_compiler_queue_contract PRIVATE eve_engine_includes zeroerr)
  target_compile_definitions(onnx_compiler_queue_contract PRIVATE
    EVENGINE_EXPORTS_FOUNDATION EVENGINE_EXPORTS_WORLD)
  target_compile_options(onnx_compiler_queue_contract PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/UZEROERR_NO_ASSERT>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UZEROERR_NO_ASSERT>)
  zeroerr_discover_tests(onnx_compiler_queue_contract LABEL onnx_compiler_queue_contract)
endif()
if(NOT EVENGINE_ANDROID_TEST_APP AND NOT EVENGINE_IOS_TEST_APP)
  include("${CMAKE_SOURCE_DIR}/cmake/TestImpact.cmake")
  eve_enable_test_impact(TARGETS ${EVE_TEST_TARGETS})
endif()

# Focused composition proof for trimmed authoring hosts. The normal per-domain
# suite still discovers these module-owned sources; this target avoids linking
# unrelated optional-domain consumers into a minimal host.
if(TARGET EVScene_editor AND TARGET EVGraphics AND NOT EVENGINE_ANDROID_TEST_APP AND NOT EVENGINE_IOS_TEST_APP)
  eve_add_test_executable(scene_authoring_contract_test EXCLUDE_FROM_ALL
    main.cpp scene_authoring.cpp scene_authoring_script.cpp graphics_authoring_projection.cpp
    scene_relations.cpp
    scene_regions.cpp scene_projected_overlap.cpp scene_visibility.cpp
    scene_editing_commands.cpp scene_editor_session.cpp scene_editor_live_session.cpp)
  if(TARGET EVDevTools)
    eve_target_test_sources(scene_authoring_contract_test PRIVATE mcp_scene_literal.cpp)
  endif()
  target_link_libraries(scene_authoring_contract_test eve_engine_includes ${EVELIBS} zeroerr ${ThirdParty}
    ${EVENGINE_VULKAN_LIB} ${EVENGINE_WEBGPU_LIB})
  target_compile_options(scene_authoring_contract_test PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/UZEROERR_NO_ASSERT>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UZEROERR_NO_ASSERT>)
  if(WIN32)
    target_link_libraries(scene_authoring_contract_test EVBacktrace)
    target_compile_definitions(scene_authoring_contract_test PRIVATE POCO_STATIC POCO_NO_AUTOMATIC_LIBS NOMINMAX)
  endif()
  if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/scene_authoring_contract_test_zeroerr_tests.cmake")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/scene_authoring_contract_test_zeroerr_tests.cmake" "# populated on focused build\n")
  endif()
  zeroerr_discover_tests(scene_authoring_contract_test LABEL "scene_authoring_contract_test"
    NAME_PREFIX "authoring-contract/")
endif()
