# ImageData supports the first composited frame of static/animated WebP.
# Build this only with the image module, including when using prebuilt legacy
# dependencies: those installs did not contain a WebP decoder.
function(load_image_webp)
    eve_module_enabled(image _eve_has_image)
    if(NOT _eve_has_image)
        return()
    endif()
    include(FetchContent)
    set(BUILD_SHARED_LIBS OFF)
    foreach(_option IN ITEMS ANIM_UTILS CWEBP DWEBP GIF2WEBP IMG2WEBP VWEBP WEBPINFO
                             LIBWEBPMUX WEBPMUX EXTRAS WEBP_JS FUZZTEST)
        set(WEBP_BUILD_${_option} OFF CACHE BOOL "" FORCE)
    endforeach()
    set(WEBP_LINK_STATIC ON CACHE BOOL "" FORCE)
    FetchContent_Declare(evengine_webp
        URL https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.6.0.tar.gz
        URL_HASH SHA256=e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
    FetchContent_MakeAvailable(evengine_webp)
    if(TARGET deps)
        add_dependencies(deps webpdemux)
    endif()
endfunction()
