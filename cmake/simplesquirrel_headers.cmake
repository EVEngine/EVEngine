# Consume the canonical headers from the same pinned aggregate as its libraries.
function(eve_prepare_simplesquirrel_headers)
    set(_source "${CMAKE_SOURCE_DIR}/third-party/simplesquirrel/include")
    if(NOT EXISTS "${_source}/simplesquirrel/direct_member.hpp")
        message(FATAL_ERROR "Pinned third-party is missing native SimpleSquirrel direct bindings")
    endif()
    target_include_directories(eve_engine_includes BEFORE INTERFACE "${_source}")
endfunction()
