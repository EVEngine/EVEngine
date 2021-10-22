enable_testing()

file(GLOB_RECURSE all_test_cpp ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

file(GLOB_RECURSE all_test_scripts ${CMAKE_CURRENT_SOURCE_DIR}/*.nut)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/nut_scripts.cpp
  COMMAND ${CMAKE_COMMAND} -DOUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/integrate.cmake"
  DEPENDS ${all_test_scripts} ${CMAKE_CURRENT_SOURCE_DIR}/template.cpp.in
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(
  unit_test
  ${all_test_cpp}
  ${CMAKE_CURRENT_BINARY_DIR}/nut_scripts.cpp
)


set(ThirdParty ${ThirdParty} simplesquirrel_static sqstdlib_static squirrel_static physfs libSDL2.a dl X11 pthread)
set(EVELIBS EVCmdLine EVScripts EVWindow EVFileSystem EVMouse EVDevTools EVCommon)

target_link_libraries(unit_test
  gtest_main gtest 
  -Wl,--whole-archive ${EVELIBS} -Wl,--no-whole-archive ${ThirdParty}
)

include(GoogleTest)
gtest_discover_tests(unit_test)