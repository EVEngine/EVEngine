enable_testing()

file(GLOB all_test_cpp
  ECS.cpp
  data.cpp
  filesystem.cpp
  filesystem_cpp.cpp
  graphic.cpp
  graphics_font.cpp
  medialoader_model_link.cpp
  model.cpp
  mouse.cpp
  mouse_cpp.cpp
  keyboard_cpp.cpp
  joystick_cpp.cpp
  touch.cpp
  network.cpp
  database.cpp
  RenderSystem.cpp
  RenderSystem3D.cpp
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
  TextureSampler.cpp
  AmbientOcclusion.cpp
  Outline.cpp
  AntiAliasing.cpp
  GlobalIllumination.cpp
  UISystem.cpp
  UIBuilder.cpp
  UIMoveRepro.cpp
  UIPhaseB.cpp
  UIPhaseAdv.cpp
  UIOverlaySmoke.cpp
  scene.cpp
  sceneloader.cpp
  particles.cpp
  particles_attach_skin.cpp
  particles_dynamic_bones.cpp
  particles_attach_more.cpp
  particles_attach_extra.cpp
  map.cpp
  map_path.cpp
  map_fov.cpp
  rpg.cpp
  rpg_simulation.cpp
  hex_level_simulation.cpp
  hex_level_data.cpp
  inventory.cpp
  building.cpp
  i18n.cpp
  i18n_script.cpp
  avatar.cpp
  dialogue.cpp
  procgen.cpp
  housegen.cpp
  housegen_render.cpp
  procgen_simulation.cpp
  roguelike_generator.cpp
  stylize.cpp
  Xray.cpp
  voxel.cpp
  voxel_render.cpp
  virtualgeometry_builder.cpp
  virtualgeometry_gpu.cpp
  gpgpu.cpp
  box2d.cpp
  box3d.cpp
  softbody.cpp
  hotreload.cpp
  shader.cpp
  grass.cpp
  spritestack.cpp
  daynight.cpp
  weather.cpp
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
  rx.cpp
  timer.cpp
  timer_cpp.cpp
  system.cpp
  math.cpp
  spatial.cpp
  editor.cpp
  ik.cpp
  animation.cpp
  animation_mixamo.cpp
  animation_skinned.cpp
  animation_sprite_spine.cpp
  tensor.cpp
  callgraph.cpp
  renderflow.cpp
  debugger.cpp
  debugger_audit.cpp
  dap.cpp
  mcp.cpp
  console.cpp
  main.cpp
)

if(EVENGINE_BUILD_DEMO)
  list(APPEND all_test_cpp demo.cpp)
endif()

# Desktop-only DevTools (DAP / MCP / debugger / callgraph / renderflow) are not
# compiled into the Android runtime, so their test files cannot link there.
# all_test_cpp holds absolute paths from file(GLOB), so filter by basename.
if(ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Android"))
  foreach(_eve_devtool_test IN ITEMS callgraph.cpp renderflow.cpp debugger.cpp dap.cpp mcp.cpp console.cpp)
    list(FILTER all_test_cpp EXCLUDE REGEX "/${_eve_devtool_test}$")
  endforeach()
  message(STATUS "Android test: removed devtools test files, remaining=${all_test_cpp}")
endif()

file(GLOB all_test_scripts
  filesystem.nut
  graphic.nut
  i18n.nut
  model.nut
  model2.nut
  mouse.nut
  simplesquirrel.nut
  window.nut
  timer.nut
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
else()
  add_executable(
    unit_test
    ${all_test_cpp}
    ${CMAKE_CURRENT_BINARY_DIR}/nut_scripts.cpp
  )
endif()


if(WIN32)
  set(SystemLibraries ${SystemLibraries} winmm version imm32 Setupapi ws2_32 iphlpapi shlwapi)
elseif(ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Android"))
  find_library(ANDROID_LOG_LIB log)
  find_library(ANDROID_LIB android)
  find_library(ANDROID_OPENSLES_LIB OpenSLES)
  set(SystemLibraries ${SystemLibraries}
    ${ANDROID_LOG_LIB}
    ${ANDROID_LIB}
    ${ANDROID_OPENSLES_LIB}
    dl
    m
    c++_shared
  )
elseif(APPLE)
  set(SystemLibraries ${SystemLibraries}
    dl pthread iconv
    "-framework Cocoa"
    "-framework IOKit"
    "-framework ForceFeedback"
    "-framework Carbon"
    "-framework CoreVideo"
    "-framework Metal"
    "-framework QuartzCore"
    "-framework AudioToolbox"
    "-framework CoreAudio"
    "-framework GameController"
    "-framework CoreHaptics"
    "-framework AVFoundation"
    "-framework CoreMedia"
    "-framework CoreFoundation"
    "-framework Foundation"
    "-framework OpenGL"
  )
elseif(UNIX)
  set(SystemLibraries ${SystemLibraries}
    dl pthread m rt bz2 png
    X11 Xext Xcursor Xrandr Xxf86vm Xi Xss Xinerama
    wayland-client wayland-cursor wayland-egl xkbcommon
    asound
  )
  # OpenAL Soft may compile the sndio backend when libsndio-dev is present.
  find_library(EVENGINE_SNDIO_LIBRARY sndio)
  if(EVENGINE_SNDIO_LIBRARY)
    list(APPEND SystemLibraries sndio)
  endif()
endif()

# Windows lib names must match local + CI third-party install (md/mdd postfixes).
# FreeType uses its own Debug postfix "d" (freetyped) and Release "md" (freetypemd).
if(WIN32)
  if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set(ThirdParty ${ThirdParty} physfsmdd medialoader_imagemdd medialoader_modelmdd medialoader_soundmdd assimpmdd zlibstaticd lz4mdd Box2Dmdd box3dd
      vorbis-staticmdd vorbisfile-staticmdd ogg-staticmdd modplug-staticmdd mpg123mdd OpenAL32mdd freetyped)
  else()
    set(ThirdParty ${ThirdParty} physfsmd medialoader_imagemd medialoader_modelmd medialoader_soundmd assimpmd zlibstatic lz4md Box2Dmd box3dmd
      vorbis-staticmd vorbisfile-staticmd ogg-staticmd modplug-staticmd mpg123md OpenAL32md freetypemd)
  endif()
else()
  if(APPLE)
    set(ThirdParty ${ThirdParty} physfs medialoader_image medialoader_model medialoader_sound assimp z lz4 Box2D box3d
      vorbisfile-static vorbis-static ogg-static modplug-static mpg123 openal freetype)
  else()
    set(ThirdParty ${ThirdParty} physfs medialoader_image medialoader_model medialoader_sound assimp z lz4 Box2D box3d
      vorbisfile-static vorbis-static ogg-static modplug-static mpg123 openal freetype)
  endif()
endif()

if(WIN32 AND (${CMAKE_BUILD_TYPE} STREQUAL "Debug"))
  set(ThirdParty ${ThirdParty} simplesquirrel_staticmdd sqstdlib_staticmdd squirrel_staticmdd SDL2d
    PocoDataSQLitemdd PocoDatamdd PocoFoundationmdd PocoNetmdd PocoJSONmdd PocoXMLmdd
    ${SystemLibraries})
else()
  if(WIN32)
    set(ThirdParty ${ThirdParty} simplesquirrel_staticmd sqstdlib_staticmd squirrel_staticmd SDL2
      PocoDataSQLitemd PocoDatamd PocoFoundationmd PocoNetmd PocoJSONmd PocoXMLmd
      ${SystemLibraries})
  elseif(ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Android"))
    # Same unprefixed static names as the engine Android build; SDL2 stays shared.
    set(ThirdParty ${ThirdParty} simplesquirrel_static sqstdlib_static squirrel_static SDL2 SDL2main
      PocoDataSQLite PocoData PocoNet PocoXML PocoJSON PocoFoundation
      ${SystemLibraries})
  else()
    set(ThirdParty ${ThirdParty} simplesquirrel_static sqstdlib_static squirrel_static SDL2
      PocoDataSQLite PocoData PocoNet PocoXML PocoJSON PocoFoundation
      ${SystemLibraries})
  endif()
endif()

set(EVELIBS
  EVCmdLine
  EVScripts
  EVWindow
  EVFileSystem
  EVMouse
  EVKeyboard
  EVJoystick
  EVTouch
  EVGraphics
  EVImage
  EVDayNight
  EVWeather
  EVCommon
  EVEvent
  EVRx
  EVTimer
  EVSystem
  EVMath
  EVSpatial
  EVEditor
  EVIK
  EVAnimation
  EVTensor
  EVNetwork
  EVDatabase
  EVData
  EVI18n
  EVSound
  EVAudio
  EVUI
  eve_imgui
  EVScene
  EVSceneLoader
  EVParticles
  EVPhysics
  EVMap
  EVGpgpu
  EVVirtualGeometry
  EVModel3D
  EVFont
  EVThread
  EVRPG
  EVInventory
  EVBuilding
  EVAvatar
  EVDialogue
  EVProcgen
  EVHouseGen
  EVVoxel
  EVStylize
  EVSpriteStack
  EVEditor)

if(NOT (ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Android")))
  list(APPEND EVELIBS EVDevTools)
endif()

if(EVENGINE_BUILD_DEMO)
  list(APPEND
    EVELIBS EVDemo)
endif()

if(EVENGINE_PLATFORM_LIB)
  list(APPEND EVELIBS ${EVENGINE_PLATFORM_LIB})
endif()

if(EVENGINE_ANDROID_TEST_APP)
  target_link_libraries(main ${EVELIBS} zeroerr ${ThirdParty} ${EVENGINE_VULKAN_LIB})
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
else()
  target_link_libraries(unit_test ${EVELIBS} zeroerr ${ThirdParty} ${EVENGINE_VULKAN_LIB})
  target_compile_definitions(unit_test PRIVATE
    EVENGINE_TEST_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    EVENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endif()

# Large classic scenes (Sponza / DamagedHelmet). Cornell Box is committed.
# Enable with -DEVENGINE_DOWNLOAD_CLASSIC_SCENES=ON, or build target
# `download_classic_scenes` before running ClassicScenes.* tests.
# (Skipped on the Android test app: bash/python3 download steps do not run there.)
if(NOT EVENGINE_ANDROID_TEST_APP)
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
add_custom_target(download_spine_models
  COMMAND ${CMAKE_COMMAND} -E env
          "EVENGINE_SPINE_DIR=${CMAKE_SOURCE_DIR}/test/assets/spine"
          python3 "${_eve_spine_script}"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "Downloading Spine demo models (spineboy, hero, dragon)"
  VERBATIM)
if(EVENGINE_DOWNLOAD_SPINE_MODELS)
  add_custom_command(TARGET unit_test PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E env
            "EVENGINE_SPINE_DIR=${CMAKE_SOURCE_DIR}/test/assets/spine"
            python3 "${_eve_spine_script}"
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
endif()  # NOT EVENGINE_ANDROID_TEST_APP

if(NOT EVENGINE_ANDROID_TEST_APP)
include(${CMAKE_SOURCE_DIR}/cmake/ZeroErrDiscoverTests.cmake)
zeroerr_discover_tests(unit_test)
endif()
