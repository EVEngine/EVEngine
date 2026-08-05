enable_language(OBJCXX)

add_library(EVPlatformIOS STATIC ios.mm)
target_include_directories(EVPlatformIOS PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(EVPlatformIOS PUBLIC cxx_std_20)
target_link_libraries(EVPlatformIOS PUBLIC
    "-framework Foundation"
    "-framework UIKit"
    "-framework AVFoundation"
    "-framework AudioToolbox"
    "-framework QuartzCore"
    "-framework CoreGraphics"
)

set(EVENGINE_PLATFORM_LIB EVPlatformIOS CACHE INTERNAL "Platform library for the active BUILD_PLATFORM")
