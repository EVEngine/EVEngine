
create_module(EVCommon common)
create_module(EVDevTools devtools)
create_module(EVCmdLine cmdline)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/common/config.h.in common/config.h)
add_executable(eve main.cpp)

if(WIN32)
    set(SystemLibraries ${SystemLibraries} winmm version imm32 Setupapi)
elseif(UNIX)
    set(SystemLibraries ${SystemLibraries} dl X11 pthread)
endif()

if(WIN32 AND (${CMAKE_BUILD_TYPE} STREQUAL "Debug"))
    set(ThirdParty ${ThirdParty} simplesquirrel_static sqstdlib_static squirrel_static SDL2d SDL2maind
        medialoader_imagemdd zlibstaticd ${SystemLibraries})
else()
    if(WIN32)
        set(ThirdParty ${ThirdParty} simplesquirrel_static sqstdlib_static squirrel_static SDL2 SDL2main
            medialoader_imagemd zlibstatic ${SystemLibraries})
    else()
        set(ThirdParty ${ThirdParty} simplesquirrel_static sqstdlib_static squirrel_static SDL2 SDL2main
            medialoader_image z ${SystemLibraries})
    endif()
endif()

set(EVELIBS EVWindow EVMouse EVEvent EVCmdLine EVDevTools EVScripts EVCommon EVGraphics EVImage EVFileSystem)

target_link_libraries(eve ${EVELIBS} zeroerr ${ThirdParty} ${EVENGINE_VULKAN_LIB})