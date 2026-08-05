enable_language(OBJCXX)

add_library(EVPlatformMacOSX STATIC macosx.mm)
target_include_directories(EVPlatformMacOSX PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(EVPlatformMacOSX PUBLIC cxx_std_20)
target_link_libraries(EVPlatformMacOSX PUBLIC
    "-framework Foundation"
    "-framework AppKit"
)

set(EVENGINE_PLATFORM_LIB EVPlatformMacOSX CACHE INTERNAL "Platform library for the active BUILD_PLATFORM")
