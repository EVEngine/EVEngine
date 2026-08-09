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
  RenderSystem.cpp
  RenderSystem3D.cpp
  Quad.cpp
  Lighting2D.cpp
  ClusteredLighting.cpp
  IBL.cpp
  Shadow3D.cpp
  UISystem.cpp
  UIBuilder.cpp
  UIPhaseB.cpp
  UIPhaseAdv.cpp
  UIOverlaySmoke.cpp
  scene.cpp
  particles.cpp
  map.cpp
  rpg.cpp
  procgen.cpp
  stylize.cpp
  gpgpu.cpp
  box2d.cpp
  hotreload.cpp
  shader.cpp
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
  timer.cpp
  timer_cpp.cpp
  system.cpp
  math.cpp
  ik.cpp
  animation.cpp
  tensor.cpp
  callgraph.cpp
  renderflow.cpp
  debugger.cpp
  main.cpp
)

if(EVENGINE_BUILD_DEMO)
  list(APPEND all_test_cpp demo.cpp)
endif()

file(GLOB all_test_scripts
  filesystem.nut
  graphic.nut
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

add_executable(
  unit_test
  ${all_test_cpp}
  ${CMAKE_CURRENT_BINARY_DIR}/nut_scripts.cpp
)


if(WIN32)
    set(SystemLibraries ${SystemLibraries} winmm version imm32 Setupapi ws2_32 iphlpapi shlwapi)
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
endif()

# Windows lib names must match local + CI third-party install (md/mdd postfixes).
# FreeType uses its own Debug postfix "d" (freetyped) and Release "md" (freetypemd).
if(WIN32)
  if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set(ThirdParty ${ThirdParty} physfsmdd medialoader_imagemdd medialoader_modelmdd medialoader_soundmdd assimpmdd zlibstaticd lz4mdd Box2Dmdd
        vorbis-staticmdd vorbisfile-staticmdd ogg-staticmdd modplug-staticmdd mpg123mdd OpenAL32mdd freetyped)
  else()
    set(ThirdParty ${ThirdParty} physfsmd medialoader_imagemd medialoader_modelmd medialoader_soundmd assimpmd zlibstatic lz4md Box2Dmd
        vorbis-staticmd vorbisfile-staticmd ogg-staticmd modplug-staticmd mpg123md OpenAL32md freetypemd)
  endif()
else()
  if(APPLE)
    set(ThirdParty ${ThirdParty} physfs medialoader_image medialoader_model medialoader_sound assimp z lz4 Box2D
        vorbisfile-static vorbis-static ogg-static modplug-static mpg123 openal freetype)
  else()
    set(ThirdParty ${ThirdParty} physfs medialoader_image medialoader_model medialoader_sound assimp z lz4 Box2D
        vorbisfile-static vorbis-static ogg-static modplug-static mpg123 openal freetype)
  endif()
endif()

if(WIN32 AND (${CMAKE_BUILD_TYPE} STREQUAL "Debug"))
    set(ThirdParty ${ThirdParty} simplesquirrel_staticmdd sqstdlib_staticmdd squirrel_staticmdd SDL2d
        PocoFoundationmdd PocoNetmdd PocoJSONmdd PocoXMLmdd
        ${SystemLibraries})
else()
    if(WIN32)
        set(ThirdParty ${ThirdParty} simplesquirrel_staticmd sqstdlib_staticmd squirrel_staticmd SDL2
            PocoFoundationmd PocoNetmd PocoJSONmd PocoXMLmd
            ${SystemLibraries})
    else()
        set(ThirdParty ${ThirdParty} simplesquirrel_static sqstdlib_static squirrel_static SDL2
            PocoNet PocoXML PocoJSON PocoFoundation
            ${SystemLibraries})
    endif()
endif()

set(EVELIBS EVCmdLine EVScripts EVWindow EVFileSystem EVMouse EVKeyboard EVJoystick EVTouch EVGraphics EVImage EVDevTools EVCommon EVEvent EVTimer EVSystem EVMath EVIK EVAnimation EVTensor EVNetwork EVData EVSound EVAudio EVUI eve_imgui EVScene EVParticles EVPhysics EVMap EVGpgpu EVModel3D EVFont EVThread EVRPG EVProcgen EVStylize)

if(EVENGINE_BUILD_DEMO)
  list(APPEND EVELIBS EVDemo)
endif()

if(EVENGINE_PLATFORM_LIB)
    list(APPEND EVELIBS ${EVENGINE_PLATFORM_LIB})
endif()

target_link_libraries(unit_test ${EVELIBS} zeroerr ${ThirdParty} ${EVENGINE_VULKAN_LIB})
target_compile_definitions(unit_test PRIVATE
  EVENGINE_TEST_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}")

include(${CMAKE_SOURCE_DIR}/cmake/ZeroErrDiscoverTests.cmake)
zeroerr_discover_tests(unit_test)
