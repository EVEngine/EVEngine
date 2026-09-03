function(ensure_third_party_checkout)
    set(_eve_tp_dir "${CMAKE_SOURCE_DIR}/third-party")
    find_package(Git REQUIRED)
    if(EXISTS "${_eve_tp_dir}/CMakeLists.txt")
        # An existing checkout is authoritative for the build, so verify it is
        # actually the pinned commit instead of silently compiling whatever
        # main happened to be at clone time (the ExternalProject GIT_TAG has
        # no effect when the source dir already exists).
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
            WORKING_DIRECTORY "${_eve_tp_dir}"
            RESULT_VARIABLE _eve_tp_head_result
            OUTPUT_VARIABLE _eve_tp_head
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(NOT _eve_tp_head_result EQUAL 0)
            message(FATAL_ERROR
                "third-party exists at ${_eve_tp_dir} but is not a readable git checkout")
        endif()
        if(NOT _eve_tp_head STREQUAL EVENGINE_THIRD_PARTY_PIN)
            message(FATAL_ERROR
                "third-party checkout is at ${_eve_tp_head}, expected pinned ${EVENGINE_THIRD_PARTY_PIN}.\n"
                "Fix with:\n"
                "  git -C third-party fetch --depth 1 origin ${EVENGINE_THIRD_PARTY_PIN}\n"
                "  git -C third-party checkout --detach ${EVENGINE_THIRD_PARTY_PIN}\n"
                "then force a deps rebuild (remove build/third-party/<plat>/third-party-prefix),\n"
                "or override the pin with -DEVENGINE_THIRD_PARTY_PIN=<commit>.")
        endif()
        message(STATUS "third-party: pinned checkout ${_eve_tp_head} verified")
        return()
    endif()

    message(STATUS "third-party: cloning https://github.com/EVEngine/third-party at ${EVENGINE_THIRD_PARTY_PIN} ...")
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} clone --depth 1 --branch main --recurse-submodules
            https://github.com/EVEngine/third-party.git "${_eve_tp_dir}"
        RESULT_VARIABLE _eve_tp_clone_result
        OUTPUT_VARIABLE _eve_tp_clone_out
        ERROR_VARIABLE _eve_tp_clone_err
    )
    if(NOT _eve_tp_clone_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to clone third-party (exit ${_eve_tp_clone_result}).\n"
            "${_eve_tp_clone_out}${_eve_tp_clone_err}\n"
            "Manually run: git clone --recurse-submodules https://github.com/EVEngine/third-party ${_eve_tp_dir}")
    endif()
    # The shallow main clone is not guaranteed to be the pinned commit; move
    # it onto the pin before submodules are synced.
    execute_process(
        COMMAND ${GIT_EXECUTABLE} fetch --depth 1 origin ${EVENGINE_THIRD_PARTY_PIN}
        WORKING_DIRECTORY "${_eve_tp_dir}"
        RESULT_VARIABLE _eve_tp_fetch_result
        OUTPUT_VARIABLE _eve_tp_fetch_out
        ERROR_VARIABLE _eve_tp_fetch_err
    )
    if(NOT _eve_tp_fetch_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to fetch pinned third-party commit ${EVENGINE_THIRD_PARTY_PIN}.\n"
            "${_eve_tp_fetch_out}${_eve_tp_fetch_err}")
    endif()
    execute_process(
        COMMAND ${GIT_EXECUTABLE} checkout --detach ${EVENGINE_THIRD_PARTY_PIN}
        WORKING_DIRECTORY "${_eve_tp_dir}"
        RESULT_VARIABLE _eve_tp_checkout_result
        OUTPUT_VARIABLE _eve_tp_checkout_out
        ERROR_VARIABLE _eve_tp_checkout_err
    )
    if(NOT _eve_tp_checkout_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to check out pinned third-party commit ${EVENGINE_THIRD_PARTY_PIN}.\n"
            "${_eve_tp_checkout_out}${_eve_tp_checkout_err}")
    endif()
    # Shallow + recurse can leave nested submodules empty on some runners; force-init.
    execute_process(
        COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
        WORKING_DIRECTORY "${_eve_tp_dir}"
        RESULT_VARIABLE _eve_tp_sub_result
        OUTPUT_VARIABLE _eve_tp_sub_out
        ERROR_VARIABLE _eve_tp_sub_err
    )
    if(NOT _eve_tp_sub_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to init third-party submodules (exit ${_eve_tp_sub_result}).\n"
            "${_eve_tp_sub_out}${_eve_tp_sub_err}")
    endif()
    if(NOT EXISTS "${_eve_tp_dir}/imgui/imgui.cpp")
        message(FATAL_ERROR
            "third-party clone is incomplete (missing imgui/imgui.cpp).\n"
            "Check ${_eve_tp_dir}")
    endif()
    if(NOT EXISTS "${_eve_tp_dir}/medialoader/CMakeLists.txt")
        message(FATAL_ERROR
            "third-party clone is incomplete (missing medialoader submodule).\n"
            "Check ${_eve_tp_dir}/medialoader")
    endif()
    if(NOT EXISTS "${_eve_tp_dir}/medialoader/model-libs/assimp/CMakeLists.txt")
        message(FATAL_ERROR
            "third-party clone is incomplete (missing medialoader/assimp submodule).\n"
            "Check ${_eve_tp_dir}/medialoader/model-libs/assimp")
    endif()
endfunction()

# This function will check the third party project
function(check_third_party_project name repo)
    include(ExternalProject)

    if(EVENGINE_THIRD_PARTY_BINARY_DIR)
        get_filename_component(_eve_tp_binary "${EVENGINE_THIRD_PARTY_BINARY_DIR}" ABSOLUTE)
        if(NOT IS_DIRECTORY "${_eve_tp_binary}/include" OR
           NOT IS_DIRECTORY "${_eve_tp_binary}/lib")
            message(FATAL_ERROR
                "EVENGINE_THIRD_PARTY_BINARY_DIR must contain include/ and lib/: "
                "${_eve_tp_binary}")
        endif()
        add_custom_target(${name})
        target_include_directories(eve_engine_includes INTERFACE "${_eve_tp_binary}/include")
        eve_external_include("${_eve_tp_binary}/include")
        # SDL2 and FreeType install their public headers one level below include/.
        # Their sources use <SDL.h> and <ft2build.h>, so expose those roots too.
        if(IS_DIRECTORY "${_eve_tp_binary}/include/SDL2")
            target_include_directories(eve_engine_includes INTERFACE
                "${_eve_tp_binary}/include/SDL2")
            eve_external_include("${_eve_tp_binary}/include/SDL2")
        endif()
        if(IS_DIRECTORY "${_eve_tp_binary}/include/freetype2")
            target_include_directories(eve_engine_includes INTERFACE
                "${_eve_tp_binary}/include/freetype2")
            eve_external_include("${_eve_tp_binary}/include/freetype2")
        endif()
        target_link_directories(eve_engine_includes INTERFACE "${_eve_tp_binary}/lib")
        message(STATUS "third-party: using read-only prebuilt tree ${_eve_tp_binary}")
        return()
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${name}/CMakeLists.txt)
        # This is a git clone version
        message("load ${name} folder from git")
        set(GIT_HTTPS_LINK "")
    else()
        message("download ${name} source code")
        set(GIT_HTTPS_LINK ${repo})
    endif()

    # macOS/Linux/iOS: keep third-party as Release (stable). A Debug deps build
    # would inherit Poco's CMAKE_DEBUG_POSTFIX=d and rename physfs → physfsd,
    # while the engine still links -lphysfs.
    # Windows: Debug engine must get Debug deps (*mdd.lib), otherwise CI links
    # *mdd against Release *md installs (LNK1104).
    set(_eve_tp_build_type "Release")
    if(WIN32)
        set(_eve_tp_build_type "${CMAKE_BUILD_TYPE}")
    endif()
    set(_eve_tp_cmake_args
        -DCMAKE_BUILD_TYPE=${_eve_tp_build_type}
        -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_SOURCE_DIR}/build/${name}-binary/${TP_BUILD_PATH}
        # Assimp enables ccache by default and installs it as a global rule
        # launcher for the whole aggregate. On Windows hosted runners that
        # resolves to Strawberry Perl's ccache, which cannot launch our
        # msvc-cl.cmd compiler wrapper. EVEngine owns compiler caching at the
        # parent build, so nested dependencies must not install another layer.
        -DASSIMP_BUILD_USE_CCACHE=OFF
    )
    # Pass the resolved logical groups into the isolated dependency project.
    # Comma encoding keeps one CMake argument intact across ExternalProject's
    # list expansion on POSIX, Ninja, MSBuild and Emscripten.
    string(JOIN "," _eve_tp_groups_arg ${EVE_THIRDPARTY_GROUPS})
    list(APPEND _eve_tp_cmake_args
        -DEVENGINE_THIRD_PARTY_GROUPS=${_eve_tp_groups_arg}
        -DEVENGINE_BUILD_HOST=${EVENGINE_BUILD_HOST})
    # ExternalProject configures the dependency aggregate in a separate CMake
    # process, so non-MSVC builds must receive the parent's compiler launcher
    # explicitly. LIST_SEPARATOR preserves compound launchers such as
    # `cmake -E env ... sccache` as one child cache value.
    # The Windows dependency install is cached as one Actions artifact. Do not
    # also put its objects through sccache: several vendored projects force
    # /Zi and /Fd, so sccache treats their shared PDB as an output and races
    # parallel cl.exe processes. The engine build keeps its parent launcher.
    if(CMAKE_C_COMPILER_LAUNCHER AND NOT MSVC)
        string(REPLACE ";" "|" _eve_tp_c_launcher
            "${CMAKE_C_COMPILER_LAUNCHER}")
        list(APPEND _eve_tp_cmake_args
            "-DCMAKE_C_COMPILER_LAUNCHER:STRING=${_eve_tp_c_launcher}")
    endif()
    if(CMAKE_CXX_COMPILER_LAUNCHER AND NOT MSVC)
        string(REPLACE ";" "|" _eve_tp_cxx_launcher
            "${CMAKE_CXX_COMPILER_LAUNCHER}")
        list(APPEND _eve_tp_cmake_args
            "-DCMAKE_CXX_COMPILER_LAUNCHER:STRING=${_eve_tp_cxx_launcher}")
    endif()
    if(CMAKE_C_COMPILER_LAUNCHER OR CMAKE_CXX_COMPILER_LAUNCHER)
        # Assimp otherwise finds Strawberry Perl's ccache.exe and installs it
        # as a global RULE_LAUNCH_COMPILE. That either double-wraps the
        # supplied launcher or, on MSVC, cannot execute our .cmd wrapper.
        list(APPEND _eve_tp_cmake_args -DASSIMP_BUILD_USE_CCACHE=OFF)
    endif()
    # Windows only: force md/mdd before any add_subdirectory so squirrel/OpenAL
    # match the names the engine already links. Do not set these on Apple/Linux.
    if(WIN32)
        list(APPEND _eve_tp_cmake_args
            -DCMAKE_DEBUG_POSTFIX=mdd
            -DCMAKE_RELEASE_POSTFIX=md
            -DCMAKE_MINSIZEREL_POSTFIX=md
            -DCMAKE_RELWITHDEBINFO_POSTFIX=md
        )
    endif()
    if(ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Android"))
        list(APPEND _eve_tp_cmake_args
            -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
            -DANDROID_ABI=${ANDROID_ABI}
            -DANDROID_PLATFORM=${ANDROID_PLATFORM}
            -DANDROID_STL=${ANDROID_STL}
            -DANDROID_NDK=${ANDROID_NDK}
            -DCMAKE_SYSTEM_NAME=Android
        )
        if(CMAKE_MAKE_PROGRAM)
            list(APPEND _eve_tp_cmake_args -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM})
        endif()
        message(STATUS "third-party Android toolchain: ABI=${ANDROID_ABI} PLATFORM=${ANDROID_PLATFORM}")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        find_program(_eve_ninja ninja REQUIRED)
        set(_eve_ios_tp_toolchain "${CMAKE_SOURCE_DIR}/cmake/ios.toolchain.cmake")
        list(APPEND _eve_tp_cmake_args
            -DCMAKE_TOOLCHAIN_FILE=${_eve_ios_tp_toolchain}
            -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}
            -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
            -DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}
            -DIOS=TRUE
            -DVIDEO_COCOA=OFF
            -DVIDEO_OPENGL=OFF
            -DCMAKE_MAKE_PROGRAM=${_eve_ninja}
        )
        # Force Ninja for deps: parent Xcode generator makes SDL try_compile unbearably slow.
        set(_eve_tp_use_ninja TRUE)
        message(STATUS "third-party iOS toolchain: ARCH=${CMAKE_OSX_ARCHITECTURES} DEPLOY=${CMAKE_OSX_DEPLOYMENT_TARGET} GEN=Ninja FILE=${_eve_ios_tp_toolchain}")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        # Third-party aggregates for the browser build use emcmake so they pick
        # up the emcc toolchain; Ninja keeps the many try_compile probes fast.
        find_program(_eve_ninja ninja REQUIRED)
        list(APPEND _eve_tp_cmake_args
            -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
            -DCMAKE_SYSTEM_NAME=Emscripten
            -DCMAKE_MAKE_PROGRAM=${_eve_ninja}
        )
        set(_eve_tp_use_ninja TRUE)
        message(STATUS "third-party Emscripten toolchain: ${CMAKE_TOOLCHAIN_FILE}")
    else()
        # Native dependency builds must use the compiler selected by the parent.
        # Otherwise ExternalProject starts a fresh configure and can silently pick
        # a different system default (for example Clang while Linux CI uses GCC).
        if(WIN32)
            # The dependency project applies /utf-8 to its MSVC targets, so it
            # does not need the parent's .cmd charset wrapper. Hosted runners
            # inject ccache into this isolated configure, and ccache cannot
            # CreateProcess() a batch file; keep its compiler executable-native.
            list(APPEND _eve_tp_cmake_args
                -DCMAKE_C_COMPILER=cl.exe
                -DCMAKE_CXX_COMPILER=cl.exe)
        else()
            if(CMAKE_C_COMPILER)
                list(APPEND _eve_tp_cmake_args -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER})
            endif()
            if(CMAKE_CXX_COMPILER)
                list(APPEND _eve_tp_cmake_args -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
            endif()
        endif()
    endif()

    # --config is required for multi-config generators (VS); ignored by Ninja.
    set(_eve_tp_build_cmd ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_SOURCE_DIR}/build/${name}/${TP_BUILD_PATH}/ --target install --config ${_eve_tp_build_type} --parallel 8)

    # Tracked patch applied at build time (never by mutating the checkout at
    # configure time). Paths in the patch are relative to the medialoader
    # submodule root. See cmake/patch_third_party.cmake.
    set(_eve_tp_patch_cmd ${CMAKE_COMMAND}
        -DPATCH=${CMAKE_SOURCE_DIR}/cmake/patches/medialoader-smooth-normals.patch
        -DPATCH_DIR=${CMAKE_CURRENT_SOURCE_DIR}/${name}/medialoader
        -P ${CMAKE_SOURCE_DIR}/cmake/patch_third_party.cmake)
    # Second patch: export squirrel/ssq symbols from the win32 host binary
    # (paths relative to the third-party aggregate root).
    set(_eve_tp_patch_cmd ${_eve_tp_patch_cmd}
        COMMAND ${CMAKE_COMMAND}
            -DPATCH=${CMAKE_SOURCE_DIR}/cmake/patches/third-party-squirrel-ssq-export.patch
            -DPATCH_DIR=${CMAKE_CURRENT_SOURCE_DIR}/${name}
            -P ${CMAKE_SOURCE_DIR}/cmake/patch_third_party.cmake)
    # Third patch: ssq class allocators must translate C++ exceptions from
    # module constructors into Squirrel errors. Without this, a throwing ctor
    # (e.g. Audio with no audio device) escapes the native call, corrupts the
    # VM's exception state and hangs during sq_close (SQInstance::Release).
    set(_eve_tp_patch_cmd ${_eve_tp_patch_cmd}
        COMMAND ${CMAKE_COMMAND}
            -DPATCH=${CMAKE_SOURCE_DIR}/cmake/patches/simplesquirrel-constructor-exceptions.patch
            -DPATCH_DIR=${CMAKE_CURRENT_SOURCE_DIR}/${name}
            -P ${CMAKE_SOURCE_DIR}/cmake/patch_third_party.cmake)
    # Fourth patch: SimpleSquirrel's Object move constructor swaps an
    # uninitialized `weak` flag. Optimized arm64 builds can observe that UB
    # when script bindings pass Array/Object wrappers by value.
    set(_eve_tp_patch_cmd ${_eve_tp_patch_cmd}
        COMMAND ${CMAKE_COMMAND}
            -DPATCH=${CMAKE_SOURCE_DIR}/cmake/patches/simplesquirrel-object-move-init.patch
            -DPATCH_DIR=${CMAKE_CURRENT_SOURCE_DIR}/${name}
            -P ${CMAKE_SOURCE_DIR}/cmake/patch_third_party.cmake)
    # Fifth patch: mpg123's K&R-style signal handler type is rejected by GCC
    # 15; use the POSIX void(int) signature accepted by sigaction.
    set(_eve_tp_patch_cmd ${_eve_tp_patch_cmd}
        COMMAND ${CMAKE_COMMAND}
            -DPATCH=${CMAKE_SOURCE_DIR}/cmake/patches/mpg123-signal-handler.patch
            -DPATCH_DIR=${CMAKE_CURRENT_SOURCE_DIR}/${name}/medialoader
            -P ${CMAKE_SOURCE_DIR}/cmake/patch_third_party.cmake)
    # Sixth patch: SDL2's Android sensor loop uses ALooper_pollAll, which the
    # NDK 27+ headers mark unavailable (obsoleted in Android 1). Use the
    # equivalent ALooper_pollOnce (same non-blocking semantics at timeout 0).
    # This is upstream SDL's own fix for libsdl-org/SDL#9792 (the exact NDK-27
    # build failure): commit 558630d "Use ALooper_pollOnce() instead of
    # ALooper_pollAll()" notes the existing draining loop already handles all
    # events, so no events are lost. SDL2 2.0.x never adopted it, hence the patch.
    # The sensor looper is dedicated (not the Android UI message loop), so this
    # cannot affect the main UI looper.
    set(_eve_tp_patch_cmd ${_eve_tp_patch_cmd}
        COMMAND ${CMAKE_COMMAND}
            -DPATCH=${CMAKE_SOURCE_DIR}/cmake/patches/sdl2-android-alooper-pollonce.patch
            -DPATCH_DIR=${CMAKE_CURRENT_SOURCE_DIR}/${name}
            -P ${CMAKE_SOURCE_DIR}/cmake/patch_third_party.cmake)
    # Seventh patch: Binding Contract constraints describe the argument as a
    # whole. Consume them at the expression's first factor so a string result
    # does not reject numeric literals nested in concatenations or ternaries.
    set(_eve_tp_patch_cmd ${_eve_tp_patch_cmd}
        COMMAND ${CMAKE_COMMAND}
            -DPATCH=${CMAKE_SOURCE_DIR}/cmake/patches/squirrel-binding-contract-expression-scope.patch
            -DPATCH_DIR=${CMAKE_CURRENT_SOURCE_DIR}/${name}
            -P ${CMAKE_SOURCE_DIR}/cmake/patch_third_party.cmake)
    # Eighth patch: RTTI hash_code is not stable across macOS dynamic-library
    # boundaries. Use a type-name hash so returned native objects retain their
    # registered Squirrel class when producer and consumer live in different modules.
    set(_eve_tp_patch_cmd ${_eve_tp_patch_cmd}
        COMMAND ${CMAKE_COMMAND}
            -DPATCH=${CMAKE_SOURCE_DIR}/cmake/patches/simplesquirrel-stable-type-hash.patch
            -DPATCH_DIR=${CMAKE_CURRENT_SOURCE_DIR}/${name}
            -P ${CMAKE_SOURCE_DIR}/cmake/patch_third_party.cmake)
    # Ninth patch: preserve an outer signature while nested binding discovery grows its storage.
    set(_eve_tp_patch_cmd ${_eve_tp_patch_cmd}
        COMMAND ${CMAKE_COMMAND}
            -DPATCH=${CMAKE_SOURCE_DIR}/cmake/patches/squirrel-nested-call-signature-lifetime.patch
            -DPATCH_DIR=${CMAKE_CURRENT_SOURCE_DIR}/${name}
            -P ${CMAKE_SOURCE_DIR}/cmake/patch_third_party.cmake)
    # Tenth patch: avoid an invalid fixed-point plus NEON64 mpg123 configuration on Apple Silicon.
    set(_eve_tp_patch_cmd ${_eve_tp_patch_cmd}
        COMMAND ${CMAKE_COMMAND}
            -DPATCH=${CMAKE_SOURCE_DIR}/cmake/patches/mpg123-apple-fpu-detection.patch
            -DPATCH_DIR=${CMAKE_CURRENT_SOURCE_DIR}/${name}/medialoader
            -P ${CMAKE_SOURCE_DIR}/cmake/patch_third_party.cmake)

    # Stamp the git versions into the install tree after every install so
    # prebuilt-mode consumers (and eve's build info) can report exactly which
    # third-party commit the libraries were built from.
    set(_eve_tp_version_cmd ${CMAKE_COMMAND}
        -DPREFIX=${CMAKE_CURRENT_SOURCE_DIR}/build/${name}-binary/${TP_BUILD_PATH}
        -DTP_DIR=${CMAKE_CURRENT_SOURCE_DIR}/${name}
        -DPLATFORM=${BUILD_PLATFORM}
        -DBUILD_TYPE=${_eve_tp_build_type}
        -P ${CMAKE_SOURCE_DIR}/cmake/write_third_party_version.cmake)

    if(_eve_tp_use_ninja)
        ExternalProject_Add(${name}
            GIT_REPOSITORY ${GIT_HTTPS_LINK}
            GIT_TAG ${EVENGINE_THIRD_PARTY_PIN}
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${name}
            BINARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build/${name}/${TP_BUILD_PATH}
            CMAKE_GENERATOR "Ninja"
            LIST_SEPARATOR "|"
            CMAKE_ARGS ${_eve_tp_cmake_args}
            PATCH_COMMAND ${_eve_tp_patch_cmd}
            BUILD_COMMAND ${_eve_tp_build_cmd} COMMAND ${_eve_tp_version_cmd}
            BUILD_ALWAYS 0
            EXCLUDE_FROM_ALL 1
        )
    else()
        ExternalProject_Add(${name}
            GIT_REPOSITORY ${GIT_HTTPS_LINK}
            GIT_TAG ${EVENGINE_THIRD_PARTY_PIN}
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${name}
            BINARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build/${name}/${TP_BUILD_PATH}
            LIST_SEPARATOR "|"
            CMAKE_ARGS ${_eve_tp_cmake_args}
            PATCH_COMMAND ${_eve_tp_patch_cmd}
            BUILD_COMMAND ${_eve_tp_build_cmd} COMMAND ${_eve_tp_version_cmd}
            BUILD_ALWAYS 0
            EXCLUDE_FROM_ALL 1
        )
    endif()
    target_include_directories(eve_engine_includes INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/build/${name}-binary/${TP_BUILD_PATH}/include)
    eve_external_include(
        ${CMAKE_CURRENT_SOURCE_DIR}/build/${name}-binary/${TP_BUILD_PATH}/include)
    target_link_directories(eve_engine_includes INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/build/${name}-binary/${TP_BUILD_PATH}/lib)
endfunction()

# Load third party project from github
function(load_third_party)
    # Debug gets its own third-party install tree (<plat>-debug); the other
    # supported single-config build types (Release, RelWithDebInfo, MinSizeRel)
    # share the release-style tree and the md library postfixes on Windows.
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(DEBUG_APPENDIX "-debug")
    else()
        set(DEBUG_APPENDIX "")
    endif()
    message("Build Type: ${CMAKE_BUILD_TYPE} (third-party tree suffix '${DEBUG_APPENDIX}')")

    # Simulator builds cannot reuse the device third-party tree (the linker
    # rejects iphoneos Mach-O slices for an iphonesimulator target), so give
    # them their own install prefix instead of clobbering build/third-party-binary/ios-debug.
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND CMAKE_OSX_SYSROOT MATCHES "simulator")
        set(_eve_tp_platform "ios-simulator")
    else()
        set(_eve_tp_platform ${BUILD_PLATFORM})
    endif()
    set(TP_BUILD_PATH ${_eve_tp_platform}${DEBUG_APPENDIX})
    message("Third Party Build Path: ${TP_BUILD_PATH}")

    # The engine still compiles a small set of vendored sources (notably ImGui)
    # even when the compiled libraries come from a prebuilt install tree. Keep
    # the pinned checkout available on fresh CI runners as well as local builds.
    ensure_third_party_checkout()

    if(EVENGINE_THIRD_PARTY_BINARY_DIR)
        cmake_path(ABSOLUTE_PATH EVENGINE_THIRD_PARTY_BINARY_DIR NORMALIZE
                   OUTPUT_VARIABLE _eve_prebuilt_tp)
        if(NOT IS_DIRECTORY "${_eve_prebuilt_tp}/include" OR
           NOT IS_DIRECTORY "${_eve_prebuilt_tp}/lib")
            message(FATAL_ERROR
                "EVENGINE_THIRD_PARTY_BINARY_DIR must contain include/ and lib/: ${_eve_prebuilt_tp}")
        endif()
        message(STATUS "third-party: using read-only prebuilt install ${_eve_prebuilt_tp}")
        target_include_directories(eve_engine_includes INTERFACE "${_eve_prebuilt_tp}/include")
        eve_external_include("${_eve_prebuilt_tp}/include")
        target_link_directories(eve_engine_includes INTERFACE "${_eve_prebuilt_tp}/lib")
        set(EVENGINE_THIRD_PARTY_BINARY_ONLY ON CACHE INTERNAL
            "Use installed third-party prefix" FORCE)
        # Read the install tree's version stamp (written by
        # cmake/write_third_party_version.cmake at install time). A missing
        # stamp is tolerated with a warning (older prebuilt trees); a stamp
        # that disagrees with a present source checkout is a hard error,
        # because the engine would otherwise compile against new source
        # headers while linking stale prebuilt libraries.
        find_package(Git QUIET)
        set(EVENGINE_TP_DESC "unknown" CACHE INTERNAL "Third-party version for build info")
        set(_eve_tp_version_file "${_eve_prebuilt_tp}/third-party-version.txt")
        if(EXISTS "${_eve_tp_version_file}")
            file(STRINGS "${_eve_tp_version_file}" _eve_tp_version_lines)
            set(_eve_tp_aggregate "")
            set(_eve_tp_built "")
            foreach(_eve_tp_line IN LISTS _eve_tp_version_lines)
                if(_eve_tp_line MATCHES "^aggregate=(.*)$")
                    set(_eve_tp_aggregate "${CMAKE_MATCH_1}")
                elseif(_eve_tp_line MATCHES "^built=(.*)$")
                    set(_eve_tp_built "${CMAKE_MATCH_1}")
                endif()
            endforeach()
            if(_eve_tp_aggregate)
                string(SUBSTRING "${_eve_tp_aggregate}" 0 12 _eve_tp_agg_short)
                set(EVENGINE_TP_DESC "prebuilt@${_eve_tp_agg_short}" CACHE INTERNAL
                    "Third-party version for build info" FORCE)
                message(STATUS "third-party prebuilt ${_eve_prebuilt_tp}: aggregate ${_eve_tp_aggregate} (built ${_eve_tp_built})")
            else()
                message(WARNING "third-party-version.txt has no aggregate= line: ${_eve_tp_version_file}")
            endif()
            if(EXISTS "${CMAKE_SOURCE_DIR}/third-party/.git" AND GIT_EXECUTABLE)
                execute_process(
                    COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
                    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/third-party"
                    RESULT_VARIABLE _eve_tp_src_result
                    OUTPUT_VARIABLE _eve_tp_src_head
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
                if(_eve_tp_src_result EQUAL 0 AND _eve_tp_aggregate
                   AND NOT _eve_tp_src_head STREQUAL _eve_tp_aggregate)
                    message(FATAL_ERROR
                        "third-party mismatch: prebuilt tree ${_eve_prebuilt_tp} was built from aggregate\n"
                        "  ${_eve_tp_aggregate}\n"
                        "but the source checkout third-party/ is at\n"
                        "  ${_eve_tp_src_head}\n"
                        "The engine would compile against new headers but link old prebuilt libraries.\n"
                        "Sync the checkout to the pinned commit and reinstall, or point\n"
                        "EVENGINE_THIRD_PARTY_BINARY_DIR at a prebuilt tree matching the source.")
                endif()
            endif()
        else()
            message(WARNING
                "prebuilt third-party tree has no ${_eve_tp_version_file}.\n"
                "Run 'make reinstall/third-party/<plat>' once to stamp the git version into the tree.")
        endif()
        # Preserve the dependency target expected by create_module(), but give it no
        # configure/build/install commands so the prebuilt tree can remain read-only.
        add_custom_target(third-party)
        add_custom_target(deps DEPENDS third-party)
        return()
    endif()

    # EVEngine's database module uses POCO Data + SQLite. The third-party
    # aggregate already defaults ENABLE_DATA / ENABLE_DATA_SQLITE to ON (pinned
    # by GIT_TAG below), so no configure-time edits are needed for it.
    #
    # Normal generation for assets without authored normals: medialoader maps
    # generateNormalsIfMissing to aiProcess_GenNormals (per-face, flat shading).
    # A tracked patch (cmake/patches/medialoader-smooth-normals.patch) switches
    # it to aiProcess_GenSmoothNormals, so sphere-like models shade smoothly
    # while hard edges (default smoothing angle 80 degrees) keep seams. The
    # patch is applied by the third-party ExternalProject's PATCH_COMMAND at
    # build time — never by mutating the checkout during configure.
    check_third_party_project(third-party "https://github.com/EVEngine/third-party")

    ADD_CUSTOM_TARGET(deps DEPENDS third-party)
    # The source checkout was verified against the pin by
    # ensure_third_party_checkout(); report it in the generated BuildInfo.h.
    string(SUBSTRING "${EVENGINE_THIRD_PARTY_PIN}" 0 12 _eve_tp_pin_short)
    set(EVENGINE_TP_DESC "source@${_eve_tp_pin_short}" CACHE INTERNAL
        "Third-party version for build info" FORCE)
endfunction()


# Resolve the WebGPU headers / library for BUILD_PLATFORM=webgpu.
#   * Emscripten (browser): webgpu.h / webgpu_cpp.h ship with the emsdk and are
#     already on emcc's include path; no link target is needed.
#   * Native desktop: Google Dawn is fetched via FetchContent and linked through
#     Dawn's native WebGPU target (set into EVENGINE_WEBGPU_LIB).
