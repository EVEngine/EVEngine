enable_testing()

file(GLOB all_test_cpp
  ECS.cpp
  data.cpp
  filesystem.cpp
  graphic.cpp
  medialoader_model_link.cpp
  model.cpp
  mouse.cpp
  network.cpp
  RenderSystem.cpp
  simplesquirrel.cpp
  window.cpp
  main.cpp
)

file(GLOB all_test_scripts
  filesystem.nut
  graphic.nut
  model.nut
  model2.nut
  mouse.nut
  simplesquirrel.nut
  window.nut
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
    set(SystemLibraries ${SystemLibraries} winmm version imm32 Setupapi ws2_32 iphlpapi)
elseif(UNIX)
    set(SystemLibraries ${SystemLibraries} dl X11 pthread)
endif()

if(WIN32)
  if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set(ThirdParty ${ThirdParty} physfsmdd medialoader_imagemdd medialoader_modelmdd assimpmdd zlibstaticd lz4mdd)
  else()
    set(ThirdParty ${ThirdParty} physfsmd medialoader_imagemd medialoader_modelmd assimpmd zlibstatic lz4md)
  endif()
else()
  set(ThirdParty ${ThirdParty} physfs medialoader_image medialoader_model assimp z lz4)
endif()

if(WIN32 AND (${CMAKE_BUILD_TYPE} STREQUAL "Debug"))
    set(ThirdParty ${ThirdParty} simplesquirrel_static sqstdlib_static squirrel_static SDL2d
        PocoFoundationmdd PocoNetmdd PocoJSONmdd PocoXMLmdd
        ${SystemLibraries})
else()
    if(WIN32)
        set(ThirdParty ${ThirdParty} simplesquirrel_static sqstdlib_static squirrel_static SDL2
            PocoFoundationmd PocoNetmd PocoJSONmd PocoXMLmd
            ${SystemLibraries})
    else()
        set(ThirdParty ${ThirdParty} simplesquirrel_static sqstdlib_static squirrel_static SDL2
            PocoFoundation PocoNet PocoJSON PocoXML
            ${SystemLibraries})
    endif()
endif()

set(EVELIBS EVCmdLine EVScripts EVWindow EVFileSystem EVMouse EVGraphics EVImage EVDevTools EVCommon EVEvent EVNetwork EVData)

target_link_libraries(unit_test ${EVELIBS} zeroerr ${ThirdParty} ${EVENGINE_VULKAN_LIB})

include(${CMAKE_SOURCE_DIR}/cmake/ZeroErrDiscoverTests.cmake)
zeroerr_discover_tests(unit_test)
