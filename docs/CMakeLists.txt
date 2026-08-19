# Doxygen documentation: `cmake --build <build-dir> --target docs`
# Standalone (no engine configure): `cmake -S docs -B build/docs && cmake --build build/docs --target docs`
# Output: docs/api/html/index.html

cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(EVEngineDocs NONE)
    set(DOC_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
else()
    set(DOC_SOURCE_ROOT "${CMAKE_SOURCE_DIR}")
endif()

if(NOT DEFINED EVENGINE_MAJOR_VERSION)
    set(EVENGINE_MAJOR_VERSION "0")
    set(EVENGINE_MINOR_VERSION "1")
    set(EVENGINE_PATCH_VERSION "0")
endif()

get_filename_component(DOC_SOURCE_ROOT "${DOC_SOURCE_ROOT}" ABSOLUTE)
file(TO_CMAKE_PATH "${DOC_SOURCE_ROOT}" DOC_SOURCE_ROOT)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" DOC_TOP_DIR)

set(CMAKE_PROJECT_VERSION
    "${EVENGINE_MAJOR_VERSION}.${EVENGINE_MINOR_VERSION}.${EVENGINE_PATCH_VERSION}")

set(EVENGINE_DOC_LANGUAGE "Chinese" CACHE STRING
    "Doxygen OUTPUT_LANGUAGE and ENABLED_SECTIONS label (Chinese or English)")
set_property(CACHE EVENGINE_DOC_LANGUAGE PROPERTY STRINGS Chinese English)

set(DOC_LANGUAGE "${EVENGINE_DOC_LANGUAGE}")
set(DOC_VISIBILITY "PUBLIC")
set(DOC_OUTPUT_DIR "${DOC_TOP_DIR}/api")
set(DOC_MAINPAGE "${DOC_SOURCE_ROOT}/Readme.md")
set(DOC_EXCLUDE_SYMBOLS "")

set(_doc_inputs
    "${DOC_SOURCE_ROOT}/src"
    "${DOC_SOURCE_ROOT}/Readme.md"
    "${DOC_TOP_DIR}/usr")
string(REPLACE ";" " " DOC_INPUT "${_doc_inputs}")

set(DOC_PROJECT_LOGO "")
if(EXISTS "${DOC_TOP_DIR}/images/evengine.png")
    set(DOC_PROJECT_LOGO "${DOC_TOP_DIR}/images/evengine.png")
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
    @ONLY)

find_package(Doxygen QUIET)
if(NOT DOXYGEN_FOUND)
    add_custom_target(docs
        COMMAND ${CMAKE_COMMAND} -E echo
            "doxygen not found; install it (apt install doxygen / brew install doxygen / choco install doxygen) to generate API docs"
        VERBATIM)
    return()
endif()

add_custom_target(docs
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DOC_OUTPUT_DIR}"
    COMMAND ${DOXYGEN_EXECUTABLE} "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
    WORKING_DIRECTORY "${DOC_SOURCE_ROOT}"
    COMMENT "Generating Doxygen documentation (docs/api/html)"
    VERBATIM)
