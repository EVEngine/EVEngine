# Reuse non-template ECS map operations from foundation without modifying the
# upstream header-only API or standalone submodule builds.
function(eve_prepare_ecs_container_headers)
    set(_source "${CMAKE_SOURCE_DIR}/external/ECS.hpp/src/ECS.hpp")
    if(NOT EXISTS "${_source}")
        return()
    endif()
    set(_root "${CMAKE_BINARY_DIR}/generated/ecs")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${_source}" "${CMAKE_SOURCE_DIR}/cmake/overrides/ecs/container_instances.hpp")
    file(READ "${_source}" _header)
    string(REPLACE "\r\n" "\n" _header "${_header}")
    file(READ "${CMAKE_SOURCE_DIR}/cmake/overrides/ecs/container_instances.hpp" _instances)
    file(MAKE_DIRECTORY "${_root}")
    file(WRITE "${_root}/ECS.hpp.in" "${_header}\n${_instances}")
    configure_file("${_root}/ECS.hpp.in" "${_root}/ECS.hpp" COPYONLY)
    target_include_directories(eve_engine_includes BEFORE INTERFACE "${_root}")
    # Keep a single ordinary /I entry, as with the SimpleSquirrel overlay.
endfunction()
