enable_testing()

file(GLOB all_test_cpp
  ECS.cpp
  data.cpp
  filesystem.cpp
  filesystem_cpp.cpp
  graphic.cpp
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
  UISystem.cpp
  UIBuilder.cpp
  UIPhaseB.cpp
  UIPhaseAdv.cpp
  UIOverlaySmoke.cpp
  particles.cpp
  simplesquirrel.cpp
  sound.cpp
  audio.cpp
  window.cpp
  window_cpp.cpp
  image.cpp
  event.cpp
  timer.cpp
  timer_cpp.cpp
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
    set(SystemLibraries ${SystemLibraries} dl X11 pthread)
endif()

if(WIN32)
  if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set(ThirdParty ${ThirdParty} physfsmdd medialoader_imagemdd medialoader_modelmdd medialoader_soundmdd assimpmdd zlibstaticd lz4mdd
        vorbis-staticmdd vorbisfile-staticmdd ogg-staticmdd modplug-staticmdd mpg123mdd OpenAL32mdd)
  else()
    set(ThirdParty ${ThirdParty} physfsmd medialoader_imagemd medialoader_modelmd medialoader_soundmd assimpmd zlibstatic lz4md
        vorbis-staticmd vorbisfile-staticmd ogg-staticmd modplug-staticmd mpg123md OpenAL32md)
  endif()
else()
  if(APPLE)
    set(ThirdParty ${ThirdParty} physfs medialoader_image medialoader_model medialoader_sound assimp z lz4
        vorbis-static vorbisfile-static ogg-static modplug-static mpg123 openal)
  else()
    set(ThirdParty ${ThirdParty} physfs medialoader_image medialoader_model medialoader_sound assimp z lz4
        vorbis vorbisfile modplug mpg123 OpenAL)
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
            PocoFoundation PocoNet PocoJSON PocoXML
            ${SystemLibraries})
    endif()
endif()

set(EVELIBS EVCmdLine EVScripts EVWindow EVFileSystem EVMouse EVKeyboard EVJoystick EVTouch EVGraphics EVImage EVDevTools EVCommon EVEvent EVTimer EVNetwork EVData EVSound EVAudio EVUI eve_imgui EVParticles)

if(EVENGINE_BUILD_DEMO)
  list(APPEND EVELIBS EVDemo)
endif()

if(EVENGINE_PLATFORM_LIB)
    list(APPEND EVELIBS ${EVENGINE_PLATFORM_LIB})
endif()

target_link_libraries(unit_test ${EVELIBS} zeroerr ${ThirdParty} ${EVENGINE_VULKAN_LIB})

include(${CMAKE_SOURCE_DIR}/cmake/ZeroErrDiscoverTests.cmake)
zeroerr_discover_tests(unit_test)
