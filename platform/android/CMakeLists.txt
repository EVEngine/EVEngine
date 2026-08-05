add_library(EVPlatformAndroid STATIC android.cpp)
target_include_directories(EVPlatformAndroid PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(EVPlatformAndroid PUBLIC cxx_std_20)
# SDL headers come from third-party install include path (global include_directories).
find_library(ANDROID_LOG_LIB log)
find_library(ANDROID_LIB android)
target_link_libraries(EVPlatformAndroid PUBLIC ${ANDROID_LOG_LIB} ${ANDROID_LIB})

set(EVENGINE_PLATFORM_LIB EVPlatformAndroid CACHE INTERNAL "Platform library for the active BUILD_PLATFORM")
