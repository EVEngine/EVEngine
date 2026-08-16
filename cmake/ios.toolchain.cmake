# Minimal iOS device toolchain for Ninja/external dependency builds.
# App bundles still use the Xcode generator; this file is for library deps only.

set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "" FORCE)
endif()
if(NOT CMAKE_OSX_SYSROOT)
    set(CMAKE_OSX_SYSROOT iphoneos CACHE STRING "" FORCE)
endif()

# Dependency builds produce static/shared libraries, not app bundles.
set(CMAKE_MACOSX_BUNDLE OFF CACHE BOOL "" FORCE)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
# Do not preset CMAKE_OBJC_COMPILER/CMAKE_OBJCXX_COMPILER: a bare compiler name
# (e.g. "clang") makes enable_language(OBJC) fail with "not a full path and was
# not found in the PATH". Leaving them unset lets CMake auto-detect the host
# toolchain (e.g. /usr/bin/clang) when SDL2's .m/.mm sources enable OBJC.

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(IOS TRUE CACHE BOOL "" FORCE)
