# Optional Asset dependencies; resolved only when the module is enabled.
include_guard(GLOBAL)

# FetchContent's ExternalProject download tries each URL once. Transient GitHub
# release 5xx (seen as HTTP 504 on zstd/utf8proc) aborts configure with no
# second chance. Prefetch into CMAKE_BINARY_DIR with retries, then point
# FetchContent at the local archive.
function(eve_download_archive_with_retries url outfile expected_sha256)
    # Optional trailing URLs must identify byte-identical canonical archives.
    # Keep one total attempt budget, independent of the number of routes.
    set(_eve_urls "${url}" ${ARGN})
    list(LENGTH _eve_urls _eve_url_count)
    get_filename_component(_eve_dl_dir "${outfile}" DIRECTORY)
    file(MAKE_DIRECTORY "${_eve_dl_dir}")

    if(EXISTS "${outfile}")
        file(SHA256 "${outfile}" _eve_existing_hash)
        if(_eve_existing_hash STREQUAL "${expected_sha256}")
            return()
        endif()
        file(REMOVE "${outfile}")
    endif()

    set(_eve_max_attempts 5)
    foreach(_eve_attempt RANGE 1 ${_eve_max_attempts})
        math(EXPR _eve_url_index "(${_eve_attempt} - 1) % ${_eve_url_count}")
        list(GET _eve_urls ${_eve_url_index} _eve_url)
        message(STATUS
            "Downloading ${_eve_url} (attempt ${_eve_attempt}/${_eve_max_attempts})")
        file(DOWNLOAD "${_eve_url}" "${outfile}"
            HTTPHEADER "Accept: application/octet-stream"
            STATUS _eve_dl_status
            LOG _eve_dl_log
            TIMEOUT 120
            TLS_VERIFY ON
            SHOW_PROGRESS)
        list(GET _eve_dl_status 0 _eve_dl_code)
        if(_eve_dl_code EQUAL 0 AND EXISTS "${outfile}")
            file(SHA256 "${outfile}" _eve_got_hash)
            if(_eve_got_hash STREQUAL "${expected_sha256}")
                return()
            endif()
            message(WARNING
                "Hash mismatch for ${outfile}: got ${_eve_got_hash}, "
                "expected ${expected_sha256}")
        else()
            # Report status lines only, never redirect URLs or authorization data.
            string(REGEX MATCHALL "HTTP/[0-9.]+ [0-9]+" _eve_http_status "${_eve_dl_log}")
            message(WARNING
                "Download failed for ${_eve_url}: ${_eve_dl_status}; HTTP: ${_eve_http_status}")
        endif()
        if(EXISTS "${outfile}")
            file(REMOVE "${outfile}")
        endif()
        if(_eve_attempt LESS _eve_max_attempts)
            # Back off 2, 4, 6, 8 seconds between attempts.
            math(EXPR _eve_sleep "2 * ${_eve_attempt}")
            execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep "${_eve_sleep}")
        endif()
    endforeach()

    message(FATAL_ERROR
        "Failed to download ${url} after ${_eve_max_attempts} attempts "
        "(expected SHA256 ${expected_sha256})")
endfunction()

# Zstd is independently pinned for the .evpack wire contract, so the aggregate
# dependency checkout and read-only prebuilt installs do not need modification.
function(load_asset_zstd)
    eve_module_enabled(asset _eve_has_asset)
    if(NOT _eve_has_asset)
        set(EVENGINE_ASSET_ZSTD_TARGET "" CACHE INTERNAL "Asset Zstd target" FORCE)
        return()
    endif()

    set(_eve_zstd_sha256
        eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3)
    set(_eve_zstd_url
        https://api.github.com/repos/facebook/zstd/releases/assets/230758919)
    # Release Asset API for the same v1.5.7 tarball; avoids release-frontend 504s.
    # The archive SHA256 above remains the authority for downloaded content.
    set(_eve_zstd_archive
        "${CMAKE_BINARY_DIR}/_eve_downloads/zstd-1.5.7.tar.gz")
    eve_download_archive_with_retries(
        "${_eve_zstd_url}" "${_eve_zstd_archive}" "${_eve_zstd_sha256}"
        "https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz")

    include(FetchContent)
    set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(evengine_zstd
        URL "${_eve_zstd_archive}"
        URL_HASH SHA256=${_eve_zstd_sha256}
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

    set(_eve_utf8proc_sha256
        abfed50b6d4da51345713661370290f4f4747263ee73dc90356299dfc7990c78)
    set(_eve_utf8proc_url
        https://github.com/JuliaStrings/utf8proc/archive/refs/tags/v2.11.3.tar.gz)
    set(_eve_utf8proc_archive
        "${CMAKE_BINARY_DIR}/_eve_downloads/utf8proc-2.11.3.tar.gz")
    eve_download_archive_with_retries(
        "${_eve_utf8proc_url}" "${_eve_utf8proc_archive}" "${_eve_utf8proc_sha256}")

    include(FetchContent)
    set(BUILD_SHARED_LIBS OFF)
    set(UTF8PROC_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(evengine_utf8proc
        URL "${_eve_utf8proc_archive}"
        URL_HASH SHA256=${_eve_utf8proc_sha256}
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
    FetchContent_MakeAvailable(evengine_utf8proc)
    if(TARGET deps)
        add_dependencies(deps utf8proc)
    endif()
    set(EVENGINE_ASSET_UTF8PROC_TARGET utf8proc CACHE INTERNAL "Asset utf8proc target" FORCE)
endfunction()
