# One shared library per link group (see EVE_LINK_GROUP_TABLE in
# cmake/module_manifest.cmake).
#
# Modules stay OBJECT libraries: the module manifest, the profiles, per-module
# compile settings and the source scan are all untouched. A group DLL links the
# objects of its modules, and that aggregation is the entire point -- the engine's
# debug information then lives in one PDB per group instead of being merged into
# every consumer executable. A per-domain test PDB measured 1.2 GB for a 0.18 GB
# executable, and only ~0.8 MB per test file of that was the test code.
#
# A single whole-engine DLL is not an option, and neither is
# WINDOWS_EXPORT_ALL_SYMBOLS at any engine-wide granularity: measured on
# EVFoundation (39 modules) it exports 75,885 symbols, over MSVC's 65535 limit
# (LNK1189). The export surface is the EVENGINE_API-annotated API only -- see
# src/engine/common/Export.h and the rule already stated in
# src/engine/CMakeLists.txt.
#
# Included after add_subdirectory(src/modules) so every module target exists.
# Consumers only ever see EVE_LINK_TARGETS (set by eve_resolve_modules) and never
# need to know how modules are grouped.

if(NOT EVENGINE_MODULE_LINKAGE STREQUAL "SHARED")
    return()
endif()

# The group shared objects link the zeroerr test framework as well, and ELF
# refuses a non-PIC object there (`relocation R_X86_64_PC32 against symbol
# zeroerr::Reset can not be used when making a shared object`). zeroerr comes
# from a submodule that does not enable position independent code itself, so it
# is set from this side; it is a no-op on Windows. The third-party closure gets
# the same flag from cmake/third_party_build.cmake.
if(TARGET zeroerr)
    set_target_properties(zeroerr PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# The third-party libraries that are dynamic in this configuration (SDL, so that
# the video subsystem has a single owner instead of one copy per link unit) must
# be findable beside the executables: CTest's PATH and the Makefile's run targets
# both start at the binary directory. Their location comes from the link
# directories the engine already uses, so a prebuilt third-party tree works too,
# and the copy is a target rather than a POST_BUILD step because the group
# libraries may already be up to date when only third-party changed. The glob
# itself runs inside the script, at build time — the install tree is empty until
# the third-party dependency of this target has run.
if(WIN32)
    get_target_property(_eve_tp_link_dirs eve_engine_includes INTERFACE_LINK_DIRECTORIES)
    set(_eve_tp_bin_dirs "")
    foreach(_eve_tp_link_dir IN LISTS _eve_tp_link_dirs)
        get_filename_component(_eve_tp_bin_dir "${_eve_tp_link_dir}/../bin" ABSOLUTE)
        list(APPEND _eve_tp_bin_dirs "${_eve_tp_bin_dir}")
    endforeach()
    add_custom_target(eve_third_party_runtime ALL
        COMMAND ${CMAKE_COMMAND}
            "-DSRC_DIRS=${_eve_tp_bin_dirs}"
            "-DDST=${CMAKE_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_third_party_runtime.cmake"
        DEPENDS third-party
        COMMENT "Placing the shared third-party runtime libraries beside the executables")
endif()

# The host, the tests, the benchmarks and the plugins all consume the annotated
# engine surface from these DLLs, so their view of EVENGINE_API is dllimport.
# Modules get this too (eve_engine_includes is what they link), but Export.h
# checks EVENGINE_ENGINE_EXPORTS first, so the defining side still exports.
target_compile_definitions(eve_engine_includes INTERFACE EVENGINE_MODULE_DLL)

# The shared route links the dynamic twins of box3d and Box2D (one world / contact
# registry per process, see third_party_build.cmake), and their headers only mark
# the imports when these are defined. Functions would resolve through the import
# library anyway; the macros keep the declarations honest.
if(WIN32)
    target_compile_definitions(eve_engine_includes INTERFACE BOX3D_DLL)
    # Box2D's data symbols are the load-bearing part here: MSVC's all-symbols .def
    # generation covers functions only, so b2Vec2_zero and b2_version are
    # annotated explicitly (cmake/patches/box2d-shared-library.patch).
    target_compile_definitions(eve_engine_includes INTERFACE BOX2D_DLL)
endif()

# vulkan-hpp's default dispatcher is a *data* symbol that must be imported, not
# re-declared, across a module-DLL boundary (MSVC only provides __imp_<symbol>
# for imported data). VULKAN_HPP_STORAGE_SHARED makes every consumer see the
# declaration as dllimport; the group that defines the storage adds
# VULKAN_HPP_STORAGE_SHARED_EXPORT for dllexport (see create_module() in
# CMakeLists.txt, where EVENGINE_EXPORTS_<GROUP> is added). Without a shared
# storage the referencing groups link LNK2001 against the bare
# ?defaultDispatchLoaderDynamic@vk@@3VDispatchLoaderDynamic@1@A that only
# EVBackends defines.
target_compile_definitions(eve_engine_includes INTERFACE VULKAN_HPP_STORAGE_SHARED)

# The same external closure the host and the test runner link. ThirdParty is
# directory-scoped in src/engine and test/, so this file derives its own copy
# from the manifest rather than reading theirs.
eve_append_system_libraries(_eve_group_system_libs)
eve_thirdparty_libs(_eve_group_tp_libs ${EVE_THIRDPARTY_GROUPS})

set(_eve_lower_groups "")
foreach(_eve_group IN LISTS EVE_LINK_GROUP_NAMES)
    set(_eve_group_modules "")
    foreach(m IN LISTS EVE_ENABLED_MODULES)
        if(EVE_MODULE_${m}_LINK_GROUP STREQUAL _eve_group)
            list(APPEND _eve_group_modules "${EVE_MODULE_${m}_LIB}")
        endif()
    endforeach()

    # The module OBJECT libraries are link inputs, not sources, so each module's
    # usage requirements (include directories, transitive link libraries) stay
    # attached to the group. Objects and libraries alike only ever reach the link
    # line as inputs, so this choice does not decide whether the MSVC CRT is
    # resolved: a static third-party library defining the DLL entry symbol does.
    # SDL2's CRT-less _DllMainCRTStartup stub did exactly that and made every
    # group link fail on __acrt_initialize / __vcrt_initialize (see
    # cmake/patches/sdl2-static-library-crt-entry.patch). link_group.cpp exists
    # only to give the target a source of its own.
    add_library(${_eve_group} SHARED "${CMAKE_SOURCE_DIR}/cmake/link_group.cpp")
    target_link_libraries(${_eve_group} PRIVATE ${_eve_group_modules})
    target_link_libraries(${_eve_group} PRIVATE eve_engine_includes)
    if(TARGET eve_imgui)
        target_link_libraries(${_eve_group} PRIVATE eve_imgui)
    endif()
    # Group-to-group edges. EVE_LINK_GROUP_NAMES is in table order (lowest layer
    # first) and the manifest graph has only two edges pointing at a higher layer
    # (cmdline/devtools into filesystem/platform_event), which merging layers -1
    # and 0 absorbs. Linking every earlier group therefore yields the complete
    # downward closure with no import cycle.
    target_link_libraries(${_eve_group} PRIVATE ${_eve_lower_groups})
    target_link_libraries(${_eve_group} PRIVATE
        EVScripts zeroerr ${_eve_group_tp_libs} ${_eve_group_system_libs}
        ${EVENGINE_VULKAN_LIB} ${EVENGINE_WEBGPU_LIB})
    if(NOT EVENGINE_PROFILE_HOSTLESS)
        add_dependencies(${_eve_group} third-party)
    endif()
    list(APPEND _eve_lower_groups "${_eve_group}")

    if(WIN32)
        set_target_properties(${_eve_group} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    else()
        # ELF/Mach-O: the annotated symbols carry default visibility themselves
        # (Export.h); inline code stays hidden so it is emitted in the consumer
        # instead of being duplicated into every group.
        set_target_properties(${_eve_group} PROPERTIES
            CXX_VISIBILITY_PRESET default
            VISIBILITY_INLINES_HIDDEN OFF
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
        # The third-party closure is a set of static archives linked into every
        # group library. ELF interposes the first definition of a symbol across
        # shared objects, but each archive copy still registers its own static
        # destructors, so Poco's global SORTABLE_FORMAT string was destroyed by
        # both libEVDomains.so and the executable and the process died with
        # "double free or corruption (!prev)" before main -- which made the
        # zeroerr discovery step fail. Binding each group's references to its own
        # definitions keeps the per-link-unit copies independent, exactly as the
        # Windows build already behaves.
        #
        # ELF only: -Bsymbolic is a GNU ld option, and Mach-O already binds each
        # dylib's references through the two-level namespace, so ld64 rejects it
        # with "unknown options: -Bsymbolic".
        if(NOT APPLE)
            target_link_options(${_eve_group} PRIVATE "LINKER:-Bsymbolic")
        endif()
    endif()

    # A SHARED build produces a dynamic SDK: the host binary needs its group
    # libraries next to it, exactly like the DLLs cmake/win32_bundle_runtime.cmake
    # copies. Destinations mirror install(TARGETS eve ...) in
    # cmake/install_sdk.cmake -- RUNTIME bin (the DLL/.dll beside eve.exe), plus
    # lib for the import library plugins link against. Only reached when
    # EVENGINE_MODULE_LINKAGE=SHARED: the release SDK stays a static/one-exe
    # install because its configure passes OBJECT and this file returns above.
    if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android")
        # Android packages the host as lib/libmain.so, so its satellites follow.
        install(TARGETS ${_eve_group}
            RUNTIME DESTINATION lib
            LIBRARY DESTINATION lib
            ARCHIVE DESTINATION lib)
    elseif(WIN32)
        install(TARGETS ${_eve_group}
            RUNTIME DESTINATION bin
            ARCHIVE DESTINATION lib)
    else()
        install(TARGETS ${_eve_group}
            RUNTIME DESTINATION bin
            LIBRARY DESTINATION lib
            ARCHIVE DESTINATION lib)
    endif()

    list(LENGTH _eve_group_modules _eve_group_module_count)
    message(STATUS "Link group ${_eve_group}: ${_eve_group_module_count} modules -> one shared library")
endforeach()
