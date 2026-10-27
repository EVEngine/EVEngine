cmake_minimum_required(VERSION 3.21)
project(eve_modelconverter_plugin LANGUAGES CXX)

# Build against a target-platform EVEngine SDK, e.g.:
#   cmake -B build -DEVEngine_DIR=/path/to/dist/eve-sdk/win32-debug/cmake
find_package(EVEngine REQUIRED)

add_eve_plugin(modelconverter
    SOURCES ModelConverter.cpp
)
