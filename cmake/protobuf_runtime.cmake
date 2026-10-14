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
    set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)
    set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)
    set(protobuf_LOCAL_DEPENDENCIES_ONLY ON CACHE BOOL "" FORCE)
    add_subdirectory("${CMAKE_SOURCE_DIR}/third-party/abseil-cpp"
                     "${CMAKE_BINARY_DIR}/protobuf/abseil" EXCLUDE_FROM_ALL)
    add_subdirectory("${CMAKE_SOURCE_DIR}/third-party/protobuf"
                     "${CMAKE_BINARY_DIR}/protobuf/runtime" EXCLUDE_FROM_ALL)
endfunction()
