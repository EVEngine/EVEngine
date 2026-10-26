# Warning policy applies to engine consumers, not dependency targets.
include_guard(GLOBAL)

function(eve_configure_strict_warnings target)
    if(EVENGINE_ENABLE_STRICT_WARNINGS)
        target_compile_options(${target} INTERFACE
            $<$<CXX_COMPILER_ID:MSVC>:/W4>
            $<$<CXX_COMPILER_ID:MSVC>:/WX>
            # C4251/C4275: STL members/base classes in __declspec(dllexport)
            # classes are safe with MSVC's own STL; the warnings are noise for
            # an engine DLL. C4100 mirrors the GCC side's -Wno-unused-parameter:
            # callback APIs keep unused parameters for stability until the
            # legacy interface is cleaned up. C4244 loss-of-data conversions
            # stay enabled and are fixed at the call sites.
            # /external:W0 silences warnings from third-party headers marked
            # with eve_external_include() (engine_usage.cmake), so /W4 /WX applies
            # only to EVEngine's own code.
            # _CRT_SECURE_NO_WARNINGS: the engine deliberately uses standard C
            # APIs (getenv, etc.) for portability; MSVC's secure-CRT
            # deprecations are reviewed in code review instead of forcing
            # _s variants at every call site.
            $<$<CXX_COMPILER_ID:MSVC>:/wd4251>
            $<$<CXX_COMPILER_ID:MSVC>:/wd4275>
            $<$<CXX_COMPILER_ID:MSVC>:/wd4100>
            # C4702: unreachable code emitted by third-party header-only code
            # (vkbuilder.hpp) at flow-analysis level; engine-side dead code is
            # removed at the call sites instead.
            $<$<CXX_COMPILER_ID:MSVC>:/wd4702>
            # C4456/4457/4458: "declaration hides class member" family. The
            # legacy codebase pervasively names parameters after the member they
            # set (setWidth(int width) etc.); renaming ~60+ call sites is a
            # dedicated cleanup PR, not a gate blocker. C4244/C4267/C4099/
            # C4389/C4189 stay enabled and are fixed at the call sites.
            $<$<CXX_COMPILER_ID:MSVC>:/wd4456>
            $<$<CXX_COMPILER_ID:MSVC>:/wd4457>
            $<$<CXX_COMPILER_ID:MSVC>:/wd4458>
            $<$<CXX_COMPILER_ID:MSVC>:/external:W0>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Werror>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-unused-parameter>
            # -Wno-unused-function: shared per-TU helper headers (GraphicsInternal.h,
            # SceneInternal.h, ...) deliberately give every including TU its own
            # internal copy, so a helper unused in one TU is structural, not a
            # defect. Unused variables and unused-but-set stay enabled.
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-unused-function>
            # Third-party template headers (simplesquirrel) emit these during
            # instantiation in engine TUs. The third-party install root stays a
            # plain -I (marking it -isystem would demote it below the Vulkan SDK
            # -I and flip which <vulkan/vulkan.hpp> wins, breaking VKBuilder),
            # so the leaks cannot be silenced per-directory on GCC and are
            # downgraded from errors to warnings here. Engine-side instances of
            # these warnings are still reported.
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-error=unused-variable>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-error=unused-but-set-parameter>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-error=sign-compare>
            # Optimized Debug (EVENGINE_DEBUG_LEVEL normal/detail uses -O2/-O1)
            # turns on GCC flow analyses that were silent under the old -O0
            # Debug. Demote known noisy families so -Werror stays usable; the
            # diagnostics remain visible as warnings.
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-error=maybe-uninitialized>
            # Box2D's b2Math.h type-puns floats under -O2 -fstrict-aliasing;
            # the install tree is a plain -I (see note above), so demote here.
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-error=strict-aliasing>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-error=array-bounds>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-error=stringop-overflow>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-error=stringop-overread>
            # Poco::Data Date/Time implicit-copy deprecations and
            # -Wdelete-non-virtual-dtor (the ECS.hpp base Entity has virtuals
            # but no virtual dtor) fire from third-party headers during template
            # instantiation in engine TUs; same treatment as above.
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-error=deprecated-copy>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-error=delete-non-virtual-dtor>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-missing-field-initializers>)
        target_compile_definitions(${target} INTERFACE
            $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
            # POSIX-name deprecations (fileno etc.) are the same category as
            # the secure-CRT warnings above; the ISO C++ name is used where
            # practical.
            $<$<CXX_COMPILER_ID:MSVC>:_CRT_NONSTDC_NO_WARNINGS>)
        # Vendored third-party source roots (third-party/Box2D, imgui, ...) are
        # external for warning purposes. Safe on GCC: the third-party install
        # tree stays a plain -I and is searched first, so the vendored
        # vulkan.hpp still beats the SDK's.
        eve_external_include(${CMAKE_SOURCE_DIR}/third-party)
    endif()
endfunction()

# IPO is opt-in: debug iteration and dependency builds must not silently gain LTO.
option(EVENGINE_ENABLE_IPO "Enable IPO/LTO when supported by the selected toolchain" OFF)
function(eve_configure_ipo)
    if(NOT EVENGINE_ENABLE_IPO)
        return()
    endif()
    include(CheckIPOSupported)
    check_ipo_supported(RESULT supported OUTPUT diagnostic)
    if(NOT supported)
        message(FATAL_ERROR "EVENGINE_ENABLE_IPO requires IPO/LTO support: ${diagnostic}")
    endif()
    # main() owns target creation. A function-local setting has no effect there.
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE PARENT_SCOPE)
    message(STATUS "IPO / LTO enabled")
endfunction()

# The OBJECT test runner contains the entire engine and test catalogue. On Linux
# AArch64 its cold exception paths can exceed CALL26's +/-128 MiB range. Keep GCC
# compilation and use LLD's range-extension thunks only for this test executable.
function(eve_configure_monolithic_test_linker target)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _eve_test_arch)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux"
       AND _eve_test_arch MATCHES "^(aarch64|arm64)$"
       AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(${target} PRIVATE "-fuse-ld=lld")
    endif()
endfunction()
