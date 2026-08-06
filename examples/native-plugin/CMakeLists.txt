cmake_minimum_required(VERSION 3.21)
project(eve_hello_plugin LANGUAGES CXX)

# Point at a target-platform SDK, e.g.:
#   cmake -B build -DEVEngine_DIR=/path/to/dist/eve-sdk/macosx-debug/cmake
find_package(EVEngine REQUIRED)

add_eve_plugin(hello_plugin SOURCES hello_plugin.cpp)
