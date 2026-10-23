# Optional Asset dependencies; resolved only when the module is enabled.
include_guard(GLOBAL)

# Zstd is independently pinned for the .evpack wire contract, so the aggregate
# dependency checkout and read-only prebuilt installs do not need modification.
function(load_asset_zstd)
    eve_module_enabled(asset _eve_has_asset)
    if(NOT _eve_has_asset)
        set(EVENGINE_ASSET_ZSTD_TARGET "" CACHE INTERNAL "Asset Zstd target" FORCE)
        return()
    endif()

    include(FetchContent)
    set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(evengine_zstd
        URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
        URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
        SOURCE_SUBDIR build/cmake)
    FetchContent_MakeAvailable(evengine_zstd)
    if(TARGET deps)
        add_dependencies(deps libzstd_static)
    endif()
    set(EVENGINE_ASSET_ZSTD_TARGET libzstd_static CACHE INTERNAL "Asset Zstd target" FORCE)
endfunction()

# Unicode NFC is part of the .eva path wire contract. Pin the Unicode table and
# normalization algorithm so every host accepts exactly the same archive names.
function(load_asset_utf8proc)
    eve_module_enabled(asset _eve_has_asset)
    if(NOT _eve_has_asset)
        set(EVENGINE_ASSET_UTF8PROC_TARGET "" CACHE INTERNAL "Asset utf8proc target" FORCE)
        return()
    endif()

    include(FetchContent)
    set(BUILD_SHARED_LIBS OFF)
    set(UTF8PROC_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(evengine_utf8proc
        URL https://github.com/JuliaStrings/utf8proc/archive/refs/tags/v2.11.3.tar.gz
        URL_HASH SHA256=abfed50b6d4da51345713661370290f4f4747263ee73dc90356299dfc7990c78
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
    FetchContent_MakeAvailable(evengine_utf8proc)
    if(TARGET deps)
        add_dependencies(deps utf8proc)
    endif()
    set(EVENGINE_ASSET_UTF8PROC_TARGET utf8proc CACHE INTERNAL "Asset utf8proc target" FORCE)
endfunction()
