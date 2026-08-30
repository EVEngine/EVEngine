function(load_webgpu_backend)
    if(NOT EVENGINE_BUILD_HOST OR NOT BUILD_PLATFORM STREQUAL "webgpu")
        set(EVENGINE_WEBGPU_LIB "" CACHE INTERNAL "WebGPU link target (unused)")
        return()
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        message(STATUS "WebGPU: using emsdk headers (no Dawn build needed)")
        set(EVENGINE_WEBGPU_LIB "" CACHE INTERNAL "WebGPU link target (unused on Emscripten)")
        return()
    endif()

    message(STATUS "WebGPU (native): fetching Google Dawn")
    include(FetchContent)
    set(DAWN_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(DAWN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(DAWN_BUILD_WGPU_TESTS OFF CACHE BOOL "" FORCE)
    set(DAWN_FETCH_DEPENDENCIES ON CACHE BOOL "" FORCE)
    set(TINT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(TINT_BUILD_CMD_TOOLS OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_NULL OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_D3D11 OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_OPENGLES OFF CACHE BOOL "" FORCE)
    set(DAWN_USE_XCODE OFF CACHE BOOL "" FORCE)
    if(WIN32)
        # Dawn's generic loader combines LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR with
        # bare system DLL names, which LoadLibraryEx rejects with ERROR 87.
        # Select its dedicated System32 path for Vulkan and D3D compiler DLLs.
        set(DAWN_FORCE_SYSTEM_COMPONENT_LOAD ON CACHE BOOL "" FORCE)
    endif()

    # Pinned Dawn commit (2026-08-24 main HEAD) so native WebGPU builds stay
    # reproducible; override with -DEVENGINE_DAWN_TAG=<commit> if needed.
    set(EVENGINE_DAWN_TAG "521035d96861a33eadc06ebff7985add6ac7fdf6" CACHE STRING
        "Dawn commit/tag to fetch for the native WebGPU backend (pin for reproducible builds)")
    # A shallow git clone cannot reliably checkout an arbitrary fixed SHA once
    # main advances beyond it. GitHub's immutable commit archive preserves the
    # reproducible pin without downloading Dawn's complete history.
    FetchContent_Declare(dawn
        URL "https://github.com/google/dawn/archive/${EVENGINE_DAWN_TAG}.tar.gz"
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    )
    FetchContent_MakeAvailable(dawn)

    if(TARGET dawn::webgpu_dawn)
        set(_eve_dawn_target dawn::webgpu_dawn)
    elseif(TARGET dawn::dawn)
        set(_eve_dawn_target dawn::dawn)
    else()
        message(FATAL_ERROR "Dawn fetch did not produce a native WebGPU target")
    endif()
    # Dawn's webgpu.h / webgpu_cpp.h live under src/ in the checkout; expose
    # them to every module target (engine, eve_imgui, gpgpu/webgpu, ...).
    target_include_directories(eve_engine_includes INTERFACE
        "${dawn_SOURCE_DIR}/include"
        "${dawn_SOURCE_DIR}/src"
        "${dawn_BINARY_DIR}/gen/include")
    if(TARGET dawncpp_headers)
        add_dependencies(eve_engine_includes dawncpp_headers)
    endif()
    if(TARGET dawn_headers)
        add_dependencies(eve_engine_includes dawn_headers)
    endif()
    if(TARGET emdawnwebgpu_headers_gen)
        add_dependencies(eve_engine_includes emdawnwebgpu_headers_gen)
    endif()
    set(EVENGINE_WEBGPU_LIB ${_eve_dawn_target} CACHE INTERNAL "WebGPU link target")
    message(STATUS "WebGPU (native): Dawn ready, link target = ${EVENGINE_WEBGPU_LIB}")
endfunction()
