# Dawn's pinned fetch_dawn_dependencies.py ignores git fetch/checkout errors.
# Use an immutable mirror archive for SPIRV-Headers instead of an unchecked
# shallow checkout, retaining the exact revision declared by Dawn's DEPS.
function(eve_prepare_dawn_spirv_headers dawn_source_dir)
    file(STRINGS "${dawn_source_dir}/DEPS" _eve_spirv_pin_lines
        REGEX "KhronosGroup/SPIRV-Headers@")
    list(LENGTH _eve_spirv_pin_lines _eve_spirv_pin_count)
    if(NOT _eve_spirv_pin_count EQUAL 1)
        message(FATAL_ERROR "Dawn DEPS must declare exactly one pinned KhronosGroup/SPIRV-Headers revision")
    endif()
    string(REGEX MATCH "SPIRV-Headers@([0-9a-fA-F]+)" _eve_spirv_match "${_eve_spirv_pin_lines}")
    set(_eve_spirv_revision "${CMAKE_MATCH_1}")
    string(LENGTH "${_eve_spirv_revision}" _eve_spirv_revision_length)
    if(NOT _eve_spirv_revision_length EQUAL 40)
        message(FATAL_ERROR "Dawn DEPS SPIRV-Headers revision must be a full 40-character commit SHA")
    endif()

    include(FetchContent)
    FetchContent_Declare(eve_spirv_headers
        URL "https://github.com/KhronosGroup/SPIRV-Headers/archive/${_eve_spirv_revision}.tar.gz"
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
        SOURCE_SUBDIR eve-populate-only
    )
    FetchContent_MakeAvailable(eve_spirv_headers)
    foreach(_eve_spirv_required IN ITEMS CMakeLists.txt include/spirv/unified1/spirv.h)
        if(NOT EXISTS "${eve_spirv_headers_SOURCE_DIR}/${_eve_spirv_required}")
            message(FATAL_ERROR "Incomplete pinned SPIRV-Headers archive: missing ${_eve_spirv_required}")
        endif()
    endforeach()
    # Dawn consumes the former; SPIRV-Tools consumes the latter. Set both in
    # the caller's scope so neither falls back to Dawn's incomplete git tree.
    set(DAWN_SPIRV_HEADERS_DIR "${eve_spirv_headers_SOURCE_DIR}" PARENT_SCOPE)
    set(SPIRV-Headers_SOURCE_DIR "${eve_spirv_headers_SOURCE_DIR}" PARENT_SCOPE)
endfunction()
