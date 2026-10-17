# Protobuf is built in the engine CMake graph so consumers use its exported
# target and inherit Abseil's full transitive link closure. The external
# third-party install cannot provide that closure on the first configure.
include_guard(GLOBAL)

function(eve_load_protobuf_runtime)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third-party/protobuf/CMakeLists.txt" OR
       NOT EXISTS "${CMAKE_SOURCE_DIR}/third-party/abseil-cpp/CMakeLists.txt")
        message(FATAL_ERROR "The pinned third-party checkout must include protobuf and abseil-cpp submodules")
    endif()
    set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_CONFORMANCE OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_PROTOC_BINARIES OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_LIBPROTOC OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_LIBUPB OFF CACHE BOOL "" FORCE)
    if(EVENGINE_MODULE_LINKAGE STREQUAL "SHARED")
        # Generated messages cross link-group DLLs; they must share one descriptor registry.
        set(protobuf_BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
        if(MSVC)
            set(BUILD_SHARED_LIBS ON)
        endif()
    else()
        set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    endif()
    set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)
    set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)
    set(protobuf_LOCAL_DEPENDENCIES_ONLY ON CACHE BOOL "" FORCE)
    # GCC cannot constant-evaluate an Abseil function-pointer comparison under
    # UBSan (abseil-cpp#1634). Keep ASan on these third-party targets while the
    # engine's own targets retain both sanitizers.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_FLAGS MATCHES "-fsanitize=[^ ]*undefined")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-sanitize=undefined")
    endif()
    # These subdirectories are configured before the engine's Emscripten
    # directory options. Their objects must still support shared memory.
    if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -matomics -mbulk-memory")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -matomics -mbulk-memory")
    endif()
    add_subdirectory("${CMAKE_SOURCE_DIR}/third-party/abseil-cpp"
                     "${CMAKE_BINARY_DIR}/protobuf/abseil" EXCLUDE_FROM_ALL)
    if(MSVC AND TARGET abseil_dll AND protobuf_BUILD_SHARED_LIBS)
        set_target_properties(abseil_dll PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    endif()
    add_subdirectory("${CMAKE_SOURCE_DIR}/third-party/protobuf"
                     "${CMAKE_BINARY_DIR}/protobuf/runtime" EXCLUDE_FROM_ALL)
    if(EVENGINE_MODULE_LINKAGE STREQUAL "SHARED")
        set(_eve_protobuf_runtime_targets libprotobuf utf8_validity)
        if(TARGET abseil_dll)
            list(APPEND _eve_protobuf_runtime_targets abseil_dll)
        endif()
        install(TARGETS ${_eve_protobuf_runtime_targets}
            RUNTIME DESTINATION bin
            LIBRARY DESTINATION lib
            ARCHIVE DESTINATION lib)
    endif()
endfunction()
