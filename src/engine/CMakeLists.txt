
create_module(EVCommon common)
create_module(EVDevTools devtools)
create_module(EVCmdLine cmdline)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/common/config.h.in common/config.h)
add_executable(eve main.cpp)

if(WIN32)
    set(SystemLibraries ${SystemLibraries} winmm version imm32 Setupapi ws2_32 iphlpapi shlwapi)
elseif(UNIX)
    set(SystemLibraries ${SystemLibraries} dl X11 pthread)
endif()

if(WIN32 AND (${CMAKE_BUILD_TYPE} STREQUAL "Debug"))
    set(ThirdParty ${ThirdParty} simplesquirrel_staticmdd sqstdlib_staticmdd squirrel_staticmdd SDL2d SDL2maind
        medialoader_imagemdd medialoader_modelmdd medialoader_soundmdd assimpmdd zlibstaticd physfsmdd lz4mdd
        vorbis-staticmdd vorbisfile-staticmdd ogg-staticmdd modplug-staticmdd mpg123mdd OpenAL32mdd
        PocoFoundationmdd PocoNetmdd PocoJSONmdd PocoXMLmdd
        ${SystemLibraries})
else()
    if(WIN32)
        set(ThirdParty ${ThirdParty} simplesquirrel_staticmd sqstdlib_staticmd squirrel_staticmd SDL2 SDL2main
            medialoader_imagemd medialoader_modelmd medialoader_soundmd assimpmd zlibstatic physfsmd lz4md
            vorbis-staticmd vorbisfile-staticmd ogg-staticmd modplug-staticmd mpg123md OpenAL32md
            PocoFoundationmd PocoNetmd PocoJSONmd PocoXMLmd
            ${SystemLibraries})
    else()
        set(ThirdParty ${ThirdParty} simplesquirrel_static sqstdlib_static squirrel_static SDL2 SDL2main
            medialoader_image medialoader_model medialoader_sound assimp z physfs lz4
            vorbis vorbisfile modplug mpg123 OpenAL
            PocoFoundation PocoNet PocoJSON PocoXML
            ${SystemLibraries})
    endif()
endif()

set(EVELIBS EVWindow EVMouse EVEvent EVCmdLine EVDevTools EVScripts EVCommon EVGraphics EVImage EVFileSystem EVNetwork EVData EVSound EVAudio)

target_link_libraries(eve ${EVELIBS} zeroerr ${ThirdParty} ${EVENGINE_VULKAN_LIB})
