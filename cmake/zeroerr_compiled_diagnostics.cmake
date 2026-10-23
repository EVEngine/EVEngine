# Keep the pinned headers usable without compiled diagnostics. The engine's
# library and all consumers select the same implementation mode during configure.
option(EVENGINE_ZEROERR_COMPILED_DIAGNOSTICS
    "Centralize zeroerr diagnostics and test registration in its compiled library" ON)

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/zeroerr-compiled-diagnostics.patch"
        "-DPATCH_DIR=${CMAKE_SOURCE_DIR}/external/zeroerr"
        -DPATCH_NORMALIZE_CRLF=ON
        -P "${CMAKE_CURRENT_LIST_DIR}/patch_third_party.cmake"
    RESULT_VARIABLE _eve_zeroerr_patch_result)
if(NOT _eve_zeroerr_patch_result EQUAL 0)
    message(FATAL_ERROR "Preparing zeroerr diagnostics headers failed; see the patch diagnostics above.")
endif()

if(EVENGINE_ZEROERR_COMPILED_DIAGNOSTICS)
    # Module OBJECT targets do not all link zeroerr directly. Match the library's
    # PUBLIC definition in every engine translation unit to avoid mixed bodies.
    add_compile_definitions(ZEROERR_COMPILED_DIAGNOSTICS)
endif()

function(eve_enable_zeroerr_compiled_diagnostics)
    if(EVENGINE_ZEROERR_COMPILED_DIAGNOSTICS)
        target_sources(zeroerr PRIVATE "${CMAKE_SOURCE_DIR}/cmake/zeroerr/CompiledDiagnostics.cpp")
        target_compile_definitions(zeroerr PUBLIC ZEROERR_COMPILED_DIAGNOSTICS)
    endif()
endfunction()
