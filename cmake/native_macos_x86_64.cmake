# Match the native ARM64 lanes: reject the wrong host before dependency downloads.
option(EVENGINE_NATIVE_MACOS_X86_64 "Require a native Intel macOS build" OFF)
if(EVENGINE_NATIVE_MACOS_X86_64)
    string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" _eve_mac_host_arch)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _eve_mac_target_arch)
    if(CMAKE_CROSSCOMPILING OR NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin"
       OR NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin"
       OR NOT EVENGINE_BUILD_PLATFORM STREQUAL "macosx"
       OR NOT _eve_mac_host_arch MATCHES "^(x86_64|amd64)$"
       OR NOT _eve_mac_target_arch MATCHES "^(x86_64|amd64)$"
       OR NOT CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
        message(FATAL_ERROR "EVENGINE_NATIVE_MACOS_X86_64 requires a native Intel macOS host and target")
    endif()
    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles("
        #if !defined(__APPLE__) || !defined(__x86_64__)
        #error Intel macOS compiler required
        #endif
        int main() { return 0; }
    " EVENGINE_COMPILER_IS_MACOS_X86_64)
    if(NOT EVENGINE_COMPILER_IS_MACOS_X86_64)
        message(FATAL_ERROR "EVENGINE_NATIVE_MACOS_X86_64 requires an Intel macOS compiler")
    endif()
endif()
