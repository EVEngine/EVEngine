# Debug compiler profiles for CMAKE_BUILD_TYPE=Debug (and multi-config Debug).
#
#   normal (default): GCC/Clang -g0 -O2 ; MSVC /O2 (no /Zi)
#     No DWARF/PDB in compiler outputs. CI stores each .o in sccache → GitHub
#     Actions cache, and the host matrix (linux/win/mac/android ×
#     debug/release/asan/…) keeps a separate namespace per cell — DWARF at
#     ~75%+ of .o size was exhausting the account quota and forcing cold
#     compiles. Asserts still use __FILE__/__LINE__; function names remain
#     in the symbol table. ASan/UBSan keeps its own RelWithDebInfo `-g` lane.
#   detail:           GCC/Clang -g2 -O1 ; MSVC /Zi /O1
#     Rich DWARF/PDB and milder opts for interactive debugging / triage.
#
# Asserts stay enabled for CONFIG:Debug regardless of optimization level
# (see ZEROERR_NO_ASSERT wiring in the root CMakeLists.txt).

set(EVENGINE_DEBUG_LEVEL "normal" CACHE STRING
    "Debug compiler profile: normal (-g0 -O2 / MSVC /O2, no DWARF) or detail (-g2 -O1 / MSVC /Zi /O1)")
set_property(CACHE EVENGINE_DEBUG_LEVEL PROPERTY STRINGS normal detail)
if(NOT EVENGINE_DEBUG_LEVEL MATCHES "^(normal|detail)$")
    message(FATAL_ERROR
        "EVENGINE_DEBUG_LEVEL must be normal or detail (got: ${EVENGINE_DEBUG_LEVEL})")
endif()

# Coverage builds pass --coverage -O0 via CMAKE_CXX_FLAGS; do not replace
# CMAKE_*_FLAGS_DEBUG with -O2 or those rightmost -O2 flags would win.
if(CMAKE_C_FLAGS MATCHES "--coverage" OR CMAKE_CXX_FLAGS MATCHES "--coverage")
    message(STATUS
        "Debug profile: skipped (coverage flags present in CMAKE_C_FLAGS/CMAKE_CXX_FLAGS)")
    return()
endif()

if(MSVC)
    if(EVENGINE_DEBUG_LEVEL STREQUAL "detail")
        set(_eve_debug_c_flags "/Zi /O1")
        set(_eve_debug_cxx_flags "/Zi /O1")
    else()
        # /RTC1 (CMake's historic Debug default) is incompatible with /O2.
        # Omit /Zi so .obj (and sccache entries) stay free of debug records;
        # local triage uses DEBUG_LEVEL=detail.
        set(_eve_debug_c_flags "/O2")
        set(_eve_debug_cxx_flags "/O2")
    endif()
else()
    if(EVENGINE_DEBUG_LEVEL STREQUAL "detail")
        set(_eve_debug_c_flags "-g2 -O1")
        set(_eve_debug_cxx_flags "-g2 -O1")
    else()
        set(_eve_debug_c_flags "-g0 -O2")
        set(_eve_debug_cxx_flags "-g0 -O2")
    endif()
endif()

# Keep the cache flags in sync with EVENGINE_DEBUG_LEVEL so flipping the
# option (or Makefile DEBUG_LEVEL=) rewrites CMAKE_*_FLAGS_DEBUG instead of
# leaving a stale -g /Od from the first configure.
if(NOT DEFINED CACHE{EVENGINE_DEBUG_LEVEL_APPLIED}
   OR NOT EVENGINE_DEBUG_LEVEL_APPLIED STREQUAL "${EVENGINE_DEBUG_LEVEL}"
   OR NOT CMAKE_C_FLAGS_DEBUG STREQUAL "${_eve_debug_c_flags}"
   OR NOT CMAKE_CXX_FLAGS_DEBUG STREQUAL "${_eve_debug_cxx_flags}")
    set(CMAKE_C_FLAGS_DEBUG "${_eve_debug_c_flags}" CACHE STRING
        "Flags used by the C compiler during DEBUG builds." FORCE)
    set(CMAKE_CXX_FLAGS_DEBUG "${_eve_debug_cxx_flags}" CACHE STRING
        "Flags used by the CXX compiler during DEBUG builds." FORCE)
    set(CMAKE_ASM_FLAGS_DEBUG "${_eve_debug_c_flags}" CACHE STRING
        "Flags used by the ASM compiler during DEBUG builds." FORCE)
    set(EVENGINE_DEBUG_LEVEL_APPLIED "${EVENGINE_DEBUG_LEVEL}" CACHE INTERNAL
        "Last EVENGINE_DEBUG_LEVEL applied to CMAKE_*_FLAGS_DEBUG")
endif()

message(STATUS
    "Debug profile: ${EVENGINE_DEBUG_LEVEL} "
    "(C/C++ Debug flags: ${CMAKE_CXX_FLAGS_DEBUG})")

unset(_eve_debug_c_flags)
unset(_eve_debug_cxx_flags)
