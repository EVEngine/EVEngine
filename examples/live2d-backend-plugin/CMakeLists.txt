cmake_minimum_required(VERSION 3.21)
project(eve_live2d_backend_plugin LANGUAGES CXX)

# Point at a target-platform SDK, e.g.:
#   cmake -B build -DEVEngine_DIR=/path/to/dist/eve-sdk/linux-debug/cmake
find_package(EVEngine REQUIRED)

add_eve_plugin(live2d_backend_plugin SOURCES live2d_backend_plugin.cpp)
