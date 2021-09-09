set(GTEST_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/third-party/binary)
find_package(GTest REQUIRED)

enable_testing()

add_executable(
  hello_test
  hello_test.cpp
)
target_link_libraries(hello_test
  gtest_main gtest pthread
)

include(GoogleTest)
gtest_discover_tests(hello_test)