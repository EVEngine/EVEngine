# Opt-in native build contract, checked before dependency downloads.
option(EVENGINE_NATIVE_ARM64 "Require a native Windows or Linux ARM64 build" OFF)
if(EVENGINE_NATIVE_ARM64)
    if((EVENGINE_BUILD_PLATFORM STREQUAL "win32" AND NOT WIN32)
       OR (EVENGINE_BUILD_PLATFORM STREQUAL "linux" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux"))
        message(FATAL_ERROR "The requested ARM64 platform must match the native host")
    endif()
    string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" _eve_host_arch)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _eve_target_arch)
    if(CMAKE_CROSSCOMPILING OR NOT CMAKE_HOST_SYSTEM_NAME MATCHES "^(Windows|Linux)$"
       OR NOT _eve_host_arch MATCHES "^(arm64|aarch64)$"
       OR NOT _eve_target_arch MATCHES "^(arm64|aarch64)$")
        message(FATAL_ERROR "EVENGINE_NATIVE_ARM64 requires a native Windows/Linux ARM64 host and target")
    endif()
    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles("
        #if !defined(__aarch64__) && !defined(_M_ARM64)
        #error ARM64 compiler required
        #endif
        int main() { return 0; }
    " EVENGINE_COMPILER_IS_ARM64)
    if(NOT EVENGINE_COMPILER_IS_ARM64)
        message(FATAL_ERROR "EVENGINE_NATIVE_ARM64 requires an ARM64 compiler")
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_BUILD_TYPE STREQUAL "Release")
        # The OBJECT test executable exceeds CALL26's 128 MiB branch range.
        # LLD inserts range-extension thunks for this large native AArch64 link.
        find_program(EVENGINE_NATIVE_ARM64_LLD NAMES ld.lld REQUIRED)
        add_link_options(-fuse-ld=lld)
    endif()
endif()
