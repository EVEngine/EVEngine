enable_testing()

file(all_test_cpp 
  ECS.cpp
  filesystem.cpp
  graphic.cpp
  model.cpp
  mouse.cpp
  simplesquirrel.cpp
  window.cpp
)

file(all_test_scripts 
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
    set(SystemLibraries ${SystemLibraries} winmm version imm32 Setupapi)
elseif(UNIX)
    set(SystemLibraries ${SystemLibraries} dl X11 pthread)
endif()

if(WIN32)
  if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set(ThirdParty ${ThirdParty} physfsmdd)
  else()
    set(ThirdParty ${ThirdParty} physfsmd)
  endif()
else()
  set(ThirdParty ${ThirdParty} physfs)
endif()

if(WIN32 AND (${CMAKE_BUILD_TYPE} STREQUAL "Debug"))
    set(ThirdParty ${ThirdParty} gtest_maind gtestd simplesquirrel_static sqstdlib_static squirrel_static SDL2d ${SystemLibraries})
else()
    set(ThirdParty ${ThirdParty} gtest_main gtest simplesquirrel_static sqstdlib_static squirrel_static SDL2 ${SystemLibraries})
endif()

set(EVELIBS EVCmdLine EVScripts EVWindow EVFileSystem EVMouse EVDevTools EVCommon)

target_link_libraries(unit_test ${EVELIBS} ${ThirdParty})

include(GoogleTest)
gtest_discover_tests(unit_test)