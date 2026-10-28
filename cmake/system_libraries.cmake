# Platform system libraries, appended to the list variable named by <out_var>.
#
# Shared by src/engine/CMakeLists.txt (host binary) and test/CMakeLists.txt
# (unit-test runner) so the per-platform link lists cannot drift apart.
function(eve_append_system_libraries out_var)
    if(WIN32)
        list(APPEND ${out_var} winmm version imm32 Setupapi ws2_32 iphlpapi shlwapi)
    elseif(ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Android"))
        find_library(ANDROID_LOG_LIB log)
        find_library(ANDROID_LIB android)
        find_library(ANDROID_OPENSLES_LIB OpenSLES)
        list(APPEND ${out_var}
            ${ANDROID_LOG_LIB}
            ${ANDROID_LIB}
            ${ANDROID_OPENSLES_LIB}
            dl
            m
            c++_shared
        )
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        # Emscripten exposes pthread/dl/GL as virtual libraries; nothing to link.
        # Game-side threading is enabled through -sPROXY_TO_PTHREAD (platform/webgpu).
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR EVENGINE_IOS)
        list(APPEND ${out_var}
            "-framework UIKit"
            "-framework Foundation"
            "-framework CoreFoundation"
            "-framework Metal"
            "-framework QuartzCore"
            "-framework CoreGraphics"
            "-framework AudioToolbox"
            "-framework CoreAudio"
            "-framework AVFoundation"
            "-framework CoreMedia"
            "-framework GameController"
            "-framework CoreHaptics"
            "-framework CoreMotion"
            "-framework OpenGLES"
            "-framework CoreBluetooth"
            # Static MoltenVK references IOSurface APIs.
            "-framework IOSurface"
            iconv
        )
    elseif(APPLE)
        list(APPEND ${out_var}
            dl pthread iconv
            "-framework Cocoa"
            "-framework IOKit"
            "-framework ForceFeedback"
            "-framework Carbon"
            "-framework CoreVideo"
            "-framework Metal"
            "-framework QuartzCore"
            "-framework AudioToolbox"
            "-framework CoreAudio"
            "-framework GameController"
            "-framework CoreHaptics"
            "-framework AVFoundation"
            "-framework CoreMedia"
            "-framework CoreFoundation"
            "-framework Foundation"
            "-framework OpenGL"
        )
    elseif(UNIX)
        # Static SDL2 pulls Wayland/X11 extras; FreeType needs bz2/png.
        list(APPEND ${out_var}
            dl pthread m rt bz2 png
            X11 Xext Xcursor Xrandr Xxf86vm Xi Xss Xinerama
            wayland-client wayland-cursor wayland-egl xkbcommon
            asound
        )
        # OpenAL Soft may compile the sndio backend when libsndio-dev is present.
        # GitHub Actions runners typically lack libsndio, so only link when found.
        find_library(EVENGINE_SNDIO_LIBRARY sndio)
        if(EVENGINE_SNDIO_LIBRARY)
            list(APPEND ${out_var} sndio)
        endif()
    endif()
    set(${out_var} "${${out_var}}" PARENT_SCOPE)
endfunction()
