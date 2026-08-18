# Maps the logical third-party groups used in cmake/module_manifest.cmake to the
# actual link names for the current platform and build type.
#
# The link names differ per platform: MSVC gets the md / mdd postfixes the
# third-party ExternalProject was told to use, Emscripten takes SDL2 and zlib
# from ports instead of the vendored trees, and only Windows distinguishes
# debug from release archives.
#
# Previously each platform carried its own hand-written flat list, unrelated to
# which modules were actually built -- so dropping a module never dropped its
# libraries. Resolving group by group means the link line follows the module
# set.

include_guard(GLOBAL)

# eve_thirdparty_libs(<out_var> <group> ...)
# Appends the link names for the given groups, in the order supplied.
function(eve_thirdparty_libs out_var)
    set(_win_debug FALSE)
    if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(_win_debug TRUE)
    endif()
    set(_emscripten FALSE)
    if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        set(_emscripten TRUE)
    endif()

    # The Emscripten aggregate only builds the simple vendored libraries; the
    # heavy ones are skipped and the module code that would need them is
    # compiled out (see EVENGINE_EXCLUDED_MODULE_FILES and the module guards).
    # Asking for these groups on WASM must resolve to nothing, not to a missing
    # archive.
    set(_skip_on_emscripten
        assimp medialoader_model medialoader_sound audio_codecs openal freetype poco poco_data)

    set(_libs "")
    foreach(g IN LISTS ARGN)
        if(_emscripten AND g IN_LIST _skip_on_emscripten)
            continue()
        endif()
        if(g STREQUAL "squirrel")
            if(_win_debug)
                list(APPEND _libs simplesquirrel_staticmdd sqstdlib_staticmdd squirrel_staticmdd)
            elseif(WIN32)
                list(APPEND _libs simplesquirrel_staticmd sqstdlib_staticmd squirrel_staticmd)
            else()
                list(APPEND _libs simplesquirrel_static sqstdlib_static squirrel_static)
            endif()

        elseif(g STREQUAL "sdl2")
            # Emscripten links SDL2 through -sUSE_SDL=2, set in the root CMakeLists.
            if(_emscripten)
            elseif(_win_debug)
                list(APPEND _libs SDL2d SDL2maind)
            else()
                list(APPEND _libs SDL2 SDL2main)
            endif()

        elseif(g STREQUAL "medialoader_image")
            if(_win_debug)
                list(APPEND _libs medialoader_imagemdd)
            elseif(WIN32)
                list(APPEND _libs medialoader_imagemd)
            else()
                list(APPEND _libs medialoader_image)
            endif()

        elseif(g STREQUAL "medialoader_model")
            if(_win_debug)
                list(APPEND _libs medialoader_modelmdd)
            elseif(WIN32)
                list(APPEND _libs medialoader_modelmd)
            else()
                list(APPEND _libs medialoader_model)
            endif()

        elseif(g STREQUAL "medialoader_sound")
            if(_win_debug)
                list(APPEND _libs medialoader_soundmdd)
            elseif(WIN32)
                list(APPEND _libs medialoader_soundmd)
            else()
                list(APPEND _libs medialoader_sound)
            endif()

        elseif(g STREQUAL "assimp")
            if(_win_debug)
                list(APPEND _libs assimpmdd)
            elseif(WIN32)
                list(APPEND _libs assimpmd)
            else()
                list(APPEND _libs assimp)
            endif()

        elseif(g STREQUAL "zlib")
            # Emscripten links zlib through -sUSE_ZLIB.
            if(_emscripten)
            elseif(_win_debug)
                list(APPEND _libs zlibstaticd)
            elseif(WIN32)
                list(APPEND _libs zlibstatic)
            else()
                list(APPEND _libs z)
            endif()

        elseif(g STREQUAL "physfs")
            if(_win_debug)
                list(APPEND _libs physfsmdd)
            elseif(WIN32)
                list(APPEND _libs physfsmd)
            else()
                list(APPEND _libs physfs)
            endif()

        elseif(g STREQUAL "lz4")
            if(_win_debug)
                list(APPEND _libs lz4mdd)
            elseif(WIN32)
                list(APPEND _libs lz4md)
            else()
                list(APPEND _libs lz4)
            endif()

        elseif(g STREQUAL "box2d")
            if(_win_debug)
                list(APPEND _libs Box2Dmdd)
            elseif(WIN32)
                list(APPEND _libs Box2Dmd)
            else()
                list(APPEND _libs Box2D)
            endif()

        elseif(g STREQUAL "box3d")
            if(_win_debug)
                list(APPEND _libs box3dd)
            elseif(WIN32)
                list(APPEND _libs box3dmd)
            else()
                list(APPEND _libs box3d)
            endif()

        elseif(g STREQUAL "audio_codecs")
            if(_win_debug)
                list(APPEND _libs vorbis-staticmdd vorbisfile-staticmdd ogg-staticmdd
                                   modplug-staticmdd mpg123mdd)
            elseif(WIN32)
                list(APPEND _libs vorbis-staticmd vorbisfile-staticmd ogg-staticmd
                                   modplug-staticmd mpg123md)
            else()
                # GNU ld needs dependents first: vorbisfile -> vorbis -> ogg.
                list(APPEND _libs vorbisfile-static vorbis-static ogg-static
                                   modplug-static mpg123)
            endif()

        elseif(g STREQUAL "openal")
            if(_win_debug)
                list(APPEND _libs OpenAL32mdd)
            elseif(WIN32)
                list(APPEND _libs OpenAL32md)
            else()
                list(APPEND _libs openal)
            endif()

        elseif(g STREQUAL "freetype")
            # FreeType forces CMAKE_DEBUG_POSTFIX=d and ignores our mdd.
            if(_win_debug)
                list(APPEND _libs freetyped)
            elseif(WIN32)
                list(APPEND _libs freetypemd)
            else()
                list(APPEND _libs freetype)
            endif()

        elseif(g STREQUAL "poco_data")
            if(_win_debug)
                list(APPEND _libs PocoDataSQLitemdd PocoDatamdd)
            elseif(WIN32)
                list(APPEND _libs PocoDataSQLitemd PocoDatamd)
            else()
                list(APPEND _libs PocoDataSQLite PocoData)
            endif()

        elseif(g STREQUAL "poco")
            if(_win_debug)
                list(APPEND _libs PocoFoundationmdd PocoNetmdd PocoJSONmdd PocoXMLmdd)
            elseif(WIN32)
                list(APPEND _libs PocoFoundationmd PocoNetmd PocoJSONmd PocoXMLmd)
            else()
                # GNU ld needs PocoNet/XML/JSON before PocoFoundation.
                list(APPEND _libs PocoNet PocoXML PocoJSON PocoFoundation)
            endif()

        elseif(g STREQUAL "xxhash")
            # Only the WASM aggregate installs xxHash as a separate archive;
            # elsewhere it is folded into the libraries that use it.
            if(_emscripten)
                list(APPEND _libs xxHash)
            endif()

        else()
            message(FATAL_ERROR "Unknown third-party group '${g}'")
        endif()
    endforeach()

    set(${out_var} "${_libs}" PARENT_SCOPE)
endfunction()
