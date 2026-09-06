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

# Test sources are discovered like engine/module sources. Do not use
# CONFIGURE_DEPENDS: Ninja glob verification plus files appearing during a
# long configure restacks "Re-checking globbed directories" forever.
# Write test_src.txt only when the sorted glob changes. `make test/win32-debug`
# reconfigures when an individual test/*.cpp is newer than that list (not when
# anything under test/ is written).
file(GLOB all_test_cpp "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(SORT all_test_cpp)
set(_eve_test_src_txt "${CMAKE_CURRENT_BINARY_DIR}/test_src.txt")
set(_eve_test_src_text "")
foreach(_eve_test_cpp IN LISTS all_test_cpp)
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

if(EVENGINE_BUILD_DEMO)
  list(APPEND all_test_cpp demo.cpp)
endif()

# Desktop-only DevTools (DAP / MCP / debugger / callgraph / renderflow) are not
# compiled into the Android/iOS runtimes, so their test files cannot link there.
# all_test_cpp holds absolute paths from file(GLOB), so filter by basename.
if(ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Android") OR EVENGINE_IOS_TEST_APP)
  foreach(_eve_devtool_test IN ITEMS
      callgraph.cpp renderflow.cpp debugger.cpp debugger_audit.cpp reload_script.cpp dap.cpp agent_development_session.cpp play_host.cpp play_trace.cpp mcp.cpp console.cpp runtime_error_sink.cpp scenario_recorder.cpp language_index.cpp language_server.cpp language_text.cpp)
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

# Tests that include a sibling module's headers are compiled only when that
# module is in the profile. Presence is a CMake source list, not EVENGINE_HAS_*.
eve_module_enabled(map _eve_test_has_map)
if(NOT _eve_test_has_map)
  list(FILTER all_test_cpp EXCLUDE REGEX "/editor_map.cpp$")
endif()
eve_module_enabled(physics _eve_test_has_physics)
if(NOT _eve_test_has_physics)
  list(FILTER all_test_cpp EXCLUDE REGEX "/vehicle_physics.cpp$")
endif()
eve_module_enabled(devtools _eve_test_has_devtools)
if(NOT _eve_test_has_devtools)
  foreach(_eve_lsp_test IN ITEMS language_index.cpp language_server.cpp language_text.cpp play_host.cpp play_trace.cpp)
    list(FILTER all_test_cpp EXCLUDE REGEX "/${_eve_lsp_test}$")
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
  add_custom_command(TARGET unit_test PRE_BUILD
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
zeroerr_discover_tests(unit_test)
endif()
