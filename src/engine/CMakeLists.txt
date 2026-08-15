create_module(EVCommon common)
create_module(EVDevTools devtools)
# Snapshot / DAP pull Poco JSON+Net; disable Windows #pragma lib auto-link
# (would request PocoFoundationd.lib instead of our *mdd postfix).
target_compile_definitions(EVDevTools PUBLIC POCO_STATIC POCO_NO_AUTOMATIC_LIBS)
create_module(EVCmdLine cmdline)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/common/config.h.in common/config.h)

set(EVENGINE_IOS_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum iOS/iPadOS deployment target")

if(ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Android"))
    # SDLActivity loads libmain.so and calls SDL_main (via SDL_main.h macro on main).
    add_library(main SHARED main.cpp)
    set(EVENGINE_NATIVE_TARGET main CACHE INTERNAL "Host target for SDK install")
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR EVENGINE_IOS)
    # UIKit SDL entry: MACOSX_BUNDLE .app with SDL_main.
    add_executable(eve MACOSX_BUNDLE main.cpp)
    set(EVENGINE_NATIVE_TARGET eve CACHE INTERNAL "Host target for SDK install")
else()
    add_executable(eve main.cpp)
    set(EVENGINE_NATIVE_TARGET eve CACHE INTERNAL "Host target for SDK install")
endif()

if(WIN32)
    set(SystemLibraries ${SystemLibraries} winmm version imm32 Setupapi ws2_32 iphlpapi shlwapi)
elseif(ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Android"))
    find_library(ANDROID_LOG_LIB log)
    find_library(ANDROID_LIB android)
    find_library(ANDROID_OPENSLES_LIB OpenSLES)
    set(SystemLibraries ${SystemLibraries}
        ${ANDROID_LOG_LIB}
        ${ANDROID_LIB}
        ${ANDROID_OPENSLES_LIB}
        dl
        m
        c++_shared
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR EVENGINE_IOS)
    set(SystemLibraries ${SystemLibraries}
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
    set(SystemLibraries ${SystemLibraries}
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
    set(SystemLibraries ${SystemLibraries}
        dl pthread m rt bz2 png
        X11 Xext Xcursor Xrandr Xxf86vm Xi Xss Xinerama
        wayland-client wayland-cursor wayland-egl xkbcommon
        asound
    )
    # OpenAL Soft may compile the sndio backend when libsndio-dev is present.
    # GitHub Actions runners typically lack libsndio, so only link when found.
    find_library(EVENGINE_SNDIO_LIBRARY sndio)
    if(EVENGINE_SNDIO_LIBRARY)
        list(APPEND SystemLibraries sndio)
    endif()
endif()

# Windows MSVC static-lib names match third-party install postfixes (md/mdd).
# Postfixes are forced early via ExternalProject CMAKE_ARGS so squirrel/OpenAL
# match Poco-style names even on a fresh CI configure.
# FreeType forces CMAKE_DEBUG_POSTFIX=d (freetyped.lib) and ignores our mdd;
# Release still picks up CMAKE_RELEASE_POSTFIX=md → freetypemd.lib.
if(WIN32 AND (${CMAKE_BUILD_TYPE} STREQUAL "Debug"))
    set(ThirdParty ${ThirdParty} simplesquirrel_staticmdd sqstdlib_staticmdd squirrel_staticmdd SDL2d SDL2maind
        medialoader_imagemdd medialoader_modelmdd medialoader_soundmdd assimpmdd zlibstaticd physfsmdd lz4mdd Box2Dmdd
        vorbis-staticmdd vorbisfile-staticmdd ogg-staticmdd modplug-staticmdd mpg123mdd OpenAL32mdd
        freetyped
        PocoDataSQLitemdd PocoDatamdd PocoFoundationmdd PocoNetmdd PocoJSONmdd PocoXMLmdd
        ${SystemLibraries})
elseif(WIN32)
    set(ThirdParty ${ThirdParty} simplesquirrel_staticmd sqstdlib_staticmd squirrel_staticmd SDL2 SDL2main
        medialoader_imagemd medialoader_modelmd medialoader_soundmd assimpmd zlibstatic physfsmd lz4md Box2Dmd
        vorbis-staticmd vorbisfile-staticmd ogg-staticmd modplug-staticmd mpg123md OpenAL32md
        freetypemd
        PocoDataSQLitemd PocoDatamd PocoFoundationmd PocoNetmd PocoJSONmd PocoXMLmd
        ${SystemLibraries})
elseif(ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Android"))
    # SDL2 is shared (loaded by Java); other deps are static where possible.
    # GNU ld needs dependents before providers: vorbisfile -> vorbis -> ogg;
    # PocoNet/XML/JSON -> PocoFoundation.
    set(ThirdParty ${ThirdParty} simplesquirrel_static sqstdlib_static squirrel_static SDL2 SDL2main
        medialoader_image medialoader_model medialoader_sound assimp z physfs lz4 Box2D
        vorbisfile-static vorbis-static ogg-static modplug-static mpg123 openal
        freetype
        PocoDataSQLite PocoData PocoNet PocoXML PocoJSON PocoFoundation
        ${SystemLibraries})
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR EVENGINE_IOS OR APPLE)
    set(ThirdParty ${ThirdParty} simplesquirrel_static sqstdlib_static squirrel_static SDL2 SDL2main
        medialoader_image medialoader_model medialoader_sound assimp z physfs lz4 Box2D
        vorbisfile-static vorbis-static ogg-static modplug-static mpg123 openal
        freetype
        PocoDataSQLite PocoData PocoNet PocoXML PocoJSON PocoFoundation
        ${SystemLibraries})
else()
    set(ThirdParty ${ThirdParty} simplesquirrel_static sqstdlib_static squirrel_static SDL2 SDL2main
        medialoader_image medialoader_model medialoader_sound assimp z physfs lz4 Box2D
        vorbisfile-static vorbis-static ogg-static modplug-static mpg123 openal
        freetype
        PocoDataSQLite PocoData PocoNet PocoXML PocoJSON PocoFoundation
        ${SystemLibraries})
endif()

set(EVELIBS
    EVWindow
    EVMouse
    EVKeyboard
    EVJoystick
    EVEvent
    EVTimer
    EVSystem
    EVAnimation
    EVIK
    EVMath
    EVSpatial
    EVTensor
    EVTouch
    EVCmdLine)

# Desktop only: DAP / MCP / DevTools (trimmed out of Android / iOS runtimes).
if(NOT (ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Android") OR CMAKE_SYSTEM_NAME STREQUAL "iOS" OR EVENGINE_IOS))
    list(APPEND EVELIBS EVDevTools)
endif()

list(APPEND EVELIBS
    EVScripts
    EVCommon
    EVGraphics
    EVImage
    EVDatabase
    EVFileSystem
    EVNetwork
    EVData
    EVSound
    EVAudio
    EVUI
    eve_imgui
    EVScene
    EVParticles
    EVMap
    EVGpgpu
    EVModel3D
    EVFont
    EVThread
    EVPlugins
    EVPhysics
    EVRPG
    EVInventory
    EVBuilding
    EVAvatar
    EVDialogue
    EVProcgen
    EVHouseGen
    EVVoxel
    EVStylize
    EVEditor)

if(EVENGINE_BUILD_DEMO)
    list(APPEND EVELIBS EVDemo)
endif()

if(EVENGINE_PLATFORM_LIB)
    list(APPEND EVELIBS ${EVENGINE_PLATFORM_LIB})
endif()

if(CMAKE_GENERATOR MATCHES "Xcode")
    # create_module() emits STATIC libraries here, so the linker would drop the
    # translation units whose only job is to self-register a module or a
    # subcommand at static-init time. Pull the archives in whole.
    set(_eve_whole_archive_libs "")
    foreach(_eve_lib IN LISTS EVELIBS)
        list(APPEND _eve_whole_archive_libs "$<LINK_LIBRARY:WHOLE_ARCHIVE,${_eve_lib}>")
    endforeach()
    target_link_libraries(${EVENGINE_NATIVE_TARGET} ${_eve_whole_archive_libs} zeroerr ${ThirdParty} ${EVENGINE_VULKAN_LIB})
else()
    target_link_libraries(${EVENGINE_NATIVE_TARGET} ${EVELIBS} zeroerr ${ThirdParty} ${EVENGINE_VULKAN_LIB})
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR EVENGINE_IOS)
    if(EVENGINE_MOLTENVK_LINK_MODE STREQUAL "framework")
        if(NOT EVENGINE_MOLTENVK_FRAMEWORK_DIR)
            message(FATAL_ERROR "EVENGINE_MOLTENVK_FRAMEWORK_DIR is not set (MoltenVK xcframework)")
        endif()
        target_link_libraries(${EVENGINE_NATIVE_TARGET}
            "-F${EVENGINE_MOLTENVK_FRAMEWORK_DIR}"
            "-framework MoltenVK"
        )
        target_link_options(${EVENGINE_NATIVE_TARGET} PRIVATE
            "LINKER:-rpath,@executable_path/Frameworks"
        )
    elseif(EVENGINE_MOLTENVK_LINK_MODE STREQUAL "static")
        if(NOT EVENGINE_MOLTENVK_STATIC_LIB OR NOT EXISTS "${EVENGINE_MOLTENVK_STATIC_LIB}")
            message(FATAL_ERROR "EVENGINE_MOLTENVK_STATIC_LIB missing: ${EVENGINE_MOLTENVK_STATIC_LIB}")
        endif()
        # macOS/lib MoltenVK.xcframework ships ios-arm64 as libMoltenVK.a (CI layout).
        target_link_libraries(${EVENGINE_NATIVE_TARGET} "${EVENGINE_MOLTENVK_STATIC_LIB}")
    else()
        message(FATAL_ERROR "EVENGINE_MOLTENVK_LINK_MODE unset/unknown (got: '${EVENGINE_MOLTENVK_LINK_MODE}')")
    endif()

    set(EVENGINE_IOS_BUNDLE_ID "com.evengine.example" CACHE STRING "iOS/iPadOS bundle identifier")
    set(_eve_ios_team "$ENV{IOS_DEVELOPMENT_TEAM}")
    if(NOT _eve_ios_team)
        set(_eve_ios_team "")
    endif()
    set(IOS_DEVELOPMENT_TEAM "${_eve_ios_team}" CACHE STRING "Apple Development Team ID for code signing")

    set(_eve_ios_rpath "")
    if(EVENGINE_MOLTENVK_LINK_MODE STREQUAL "framework")
        set(_eve_ios_rpath "@executable_path/Frameworks")
    endif()
    set_target_properties(${EVENGINE_NATIVE_TARGET} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "${EVENGINE_IOS_BUNDLE_ID}"
        MACOSX_BUNDLE_BUNDLE_NAME "EVEngine"
        MACOSX_BUNDLE_BUNDLE_VERSION "${EVENGINE_MAJOR_VERSION}.${EVENGINE_MINOR_VERSION}.${EVENGINE_PATCH_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${EVENGINE_MAJOR_VERSION}.${EVENGINE_MINOR_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/platform/ios/Info.plist.in"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${EVENGINE_IOS_BUNDLE_ID}"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "${EVENGINE_IOS_DEPLOYMENT_TARGET}"
        XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH NO
        XCODE_ATTRIBUTE_ENABLE_BITCODE NO
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${IOS_DEVELOPMENT_TEAM}"
        XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "${_eve_ios_rpath}"
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
    )

    # Bundle game assets under Resources/game (iOS resourcePath == .app root).
    # Default "demo" = shell without main.nut → load.nut runs embedded eve.demoScript
    # (meteor defense + particles). "example" copies example/ (box + fire.json).
    set(EVENGINE_IOS_GAME "demo" CACHE STRING "iOS bundled game: demo | example | absolute path")
    if(EVENGINE_IOS_GAME STREQUAL "example")
        set(_eve_game_src "${CMAKE_SOURCE_DIR}/example")
    elseif(EVENGINE_IOS_GAME STREQUAL "demo")
        set(_eve_game_src "${CMAKE_SOURCE_DIR}/platform/ios/game-shell")
    elseif(IS_DIRECTORY "${EVENGINE_IOS_GAME}")
        set(_eve_game_src "${EVENGINE_IOS_GAME}")
    else()
        message(FATAL_ERROR "EVENGINE_IOS_GAME must be 'demo', 'example', or an existing directory (got: ${EVENGINE_IOS_GAME})")
    endif()
    add_custom_command(TARGET ${EVENGINE_NATIVE_TARGET} POST_BUILD
        # Wipe first so a previous example/main.nut cannot shadow demoScript.
        COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:${EVENGINE_NATIVE_TARGET}>/game"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${EVENGINE_NATIVE_TARGET}>/game"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_eve_game_src}" "$<TARGET_FILE_DIR:${EVENGINE_NATIVE_TARGET}>/game"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/platform/ios/LaunchScreen.storyboard"
        "$<TARGET_FILE_DIR:${EVENGINE_NATIVE_TARGET}>/LaunchScreen.storyboard"
        COMMENT "Bundle iOS game (${EVENGINE_IOS_GAME}) + LaunchScreen into EVEngine.app"
    )

    if(EVENGINE_MOLTENVK_LINK_MODE STREQUAL "framework")
        # Embed MoltenVK.framework so dlsym(RTLD_DEFAULT) / rpath resolve at runtime.
        # The helper also signs the framework; nested frameworks need their own signature.
        set(_eve_mvk_fw "${EVENGINE_MOLTENVK_FRAMEWORK_DIR}/MoltenVK.framework")
        add_custom_command(TARGET ${EVENGINE_NATIVE_TARGET} POST_BUILD
            COMMAND /bin/sh "${CMAKE_SOURCE_DIR}/cmake/ios_embed_moltenvk.sh"
            "${_eve_mvk_fw}"
            "$<TARGET_FILE_DIR:${EVENGINE_NATIVE_TARGET}>"
            COMMENT "Embed and sign MoltenVK.framework into EVEngine.app"
        )
    endif()
endif()

# Plugins resolve ModuleManager / ModuleRegister symbols from the host process.
# Export surface is EVENGINE_API (__declspec(dllexport)) only — do not enable
# WINDOWS_EXPORT_ALL_SYMBOLS: it pulls Poco/ImGui template instantiations into
# the .def and hits MSVC LNK1189 (65535 export limit) as the engine grows.
target_compile_definitions(${EVENGINE_NATIVE_TARGET} PRIVATE EVENGINE_BUILDING_HOST=1)
if(WIN32)
    # ENABLE_EXPORTS so plugins can link/import host symbols.
    # Do NOT enable WINDOWS_EXPORT_ALL_SYMBOLS: MSVC hits LNK1189 (65535
    # objects/exports) once the engine grows. Plugins only need EVENGINE_API
    # (__declspec(dllexport)) symbols such as ModuleManager / ModuleRegister.
    set_target_properties(${EVENGINE_NATIVE_TARGET} PROPERTIES
        ENABLE_EXPORTS ON
        WINDOWS_EXPORT_ALL_SYMBOLS OFF
    )
elseif(APPLE)
    # macOS + iOS: Apple ld rejects GNU --export-dynamic.
    target_link_options(${EVENGINE_NATIVE_TARGET} PRIVATE "LINKER:-export_dynamic")
elseif(UNIX AND NOT ANDROID AND NOT (CMAKE_SYSTEM_NAME STREQUAL "Android"))
    target_link_options(${EVENGINE_NATIVE_TARGET} PRIVATE "LINKER:--export-dynamic")
elseif(ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Android"))
    # libmain.so: keep default-visible EVENGINE_API symbols for dlopened plugins.
    set_target_properties(${EVENGINE_NATIVE_TARGET} PROPERTIES
        CXX_VISIBILITY_PRESET default
        VISIBILITY_INLINES_HIDDEN OFF
    )
endif()
