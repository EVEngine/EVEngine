# Prepare the dependencies used unconditionally by native Dawn configuration.
# Its pinned fetch script ignores failed Git commands; checked immutable archives
# keep an empty Git checkout from reaching add_subdirectory or Python imports.
function(_eve_dawn_support_archive output dawn_source_dir name dependency archive_prefix)
    file(STRINGS "${dawn_source_dir}/DEPS" _eve_pin_lines REGEX "${dependency}@")
    list(LENGTH _eve_pin_lines _eve_pin_count)
    if(NOT _eve_pin_count EQUAL 1)
        message(FATAL_ERROR "Dawn DEPS must declare exactly one pinned ${dependency} revision")
    endif()
    string(REGEX MATCH "@([0-9a-fA-F]+)" _eve_pin_match "${_eve_pin_lines}")
    set(_eve_revision "${CMAKE_MATCH_1}")
    string(LENGTH "${_eve_revision}" _eve_revision_length)
    if(NOT _eve_revision_length EQUAL 40)
        message(FATAL_ERROR "Dawn DEPS ${dependency} revision must be a full 40-character commit SHA")
    endif()

    include(FetchContent)
    FetchContent_Declare(eve_dawn_${name}
        URL "${archive_prefix}${_eve_revision}.tar.gz"
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
        # The generator imports Python packages by their directory basename.
        SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/eve_dawn_support/${name}"
        SOURCE_SUBDIR eve-populate-only
    )
    FetchContent_MakeAvailable(eve_dawn_${name})
    set(_eve_source "${eve_dawn_${name}_SOURCE_DIR}")
    foreach(_eve_required IN LISTS ARGN)
        if(NOT EXISTS "${_eve_source}/${_eve_required}")
            message(FATAL_ERROR "Incomplete pinned Dawn ${name} archive: missing ${_eve_required}")
        endif()
    endforeach()
    set(${output} "${_eve_source}" PARENT_SCOPE)
endfunction()

function(eve_prepare_dawn_support_dependencies dawn_source_dir)
    _eve_dawn_support_archive(_eve_abseil "${dawn_source_dir}" abseil
        "chromium/src/third_party/abseil-cpp"
        "https://chromium.googlesource.com/chromium/src/third_party/abseil-cpp/+archive/"
        CMakeLists.txt absl/strings/string_view.h)
    _eve_dawn_support_archive(_eve_glfw "${dawn_source_dir}" glfw
        "github.com/glfw/glfw"
        "https://github.com/glfw/glfw/archive/"
        CMakeLists.txt include/GLFW/glfw3.h)
    _eve_dawn_support_archive(_eve_jinja2 "${dawn_source_dir}" jinja2
        "chromium/src/third_party/jinja2"
        "https://chromium.googlesource.com/chromium/src/third_party/jinja2/+archive/"
        __init__.py environment.py loaders.py)
    _eve_dawn_support_archive(_eve_markupsafe "${dawn_source_dir}" markupsafe
        "chromium/src/third_party/markupsafe"
        "https://chromium.googlesource.com/chromium/src/third_party/markupsafe/+archive/"
        __init__.py _native.py _compat.py)

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" -I -c
            "import sys; sys.path[:0] = sys.argv[1:]; from jinja2 import BaseLoader, Environment; from markupsafe import escape"
            "${_eve_jinja2}/.." "${_eve_markupsafe}/.."
        RESULT_VARIABLE _eve_generator_status
        ERROR_VARIABLE _eve_generator_error
    )
    if(NOT _eve_generator_status EQUAL 0)
        message(FATAL_ERROR "Pinned Dawn generator dependencies cannot be imported: ${_eve_generator_error}")
    endif()
    set(DAWN_ABSEIL_DIR "${_eve_abseil}" PARENT_SCOPE)
    set(DAWN_GLFW_DIR "${_eve_glfw}" PARENT_SCOPE)
    set(DAWN_JINJA2_DIR "${_eve_jinja2}" PARENT_SCOPE)
    set(DAWN_MARKUPSAFE_DIR "${_eve_markupsafe}" PARENT_SCOPE)
endfunction()
