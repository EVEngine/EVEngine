# Module manifest and resolution.
#
# One declaration per module, from which the build derives everything that used
# to be maintained by hand in three places: the create_module() calls in
# src/modules/CMakeLists.txt, the EVELIBS link list and the ThirdParty link list
# in src/engine/CMakeLists.txt, and the module wiring at the top of
# src/scripts/load.nut.
#
# Keeping those in sync manually is why trimming only ever worked along the one
# hard-coded axis (Emscripten). With a single source of truth, any module can be
# switched off and its dependencies, its third-party libraries and its script
# binding follow automatically.
#
#   -DEVENGINE_PROFILE=minimal|2d|3d|full|web|procgen-core-only|physics-core-only|headless|server
#                                                   pick a preset (default: full)
#   -DEVENGINE_MODULE_<NAME>=ON|OFF              override one module
#
# See docs/dev/模块编排与裁剪架构.md.

include_guard(GLOBAL)

set(EVE_ALL_MODULES "" CACHE INTERNAL "Every declared module, in declaration order")

# eve_declare_module(
#   NAME       <dir>            directory under src/modules and manifest key
#   LIB        <target>         OBJECT library name (default: EV<Name>)
#   LAYER      <n>              informational; matches scripts/module_depgraph.py
#   DEPS       <mod> ...        modules that must also be enabled
#   OPTIONAL_DEPS <mod> ...     modules this integrates with when present; the
#                               code is guarded by EVENGINE_HAS_<MOD>
#   THIRDPARTY <group> ...      third-party groups to link (see EVE_TP_ORDER)
#   SCRIPT     <Class> ...      Squirrel classes exposed on the eve table
#   SLOT       <name> ...       root-table slots, paired with SCRIPT by position
#   GROUP      <profile> ...    profiles that include this module
#   REQUIRED                    always on; not switchable
#   CORE                        lives under src/engine, not src/modules
# )
function(eve_declare_module)
    set(options REQUIRED CORE)
    set(oneValue NAME LIB LAYER)
    set(multiValue DEPS OPTIONAL_DEPS THIRDPARTY GROUP SCRIPT SLOT)
    cmake_parse_arguments(M "${options}" "${oneValue}" "${multiValue}" ${ARGN})

    if(NOT M_NAME)
        message(FATAL_ERROR "eve_declare_module: NAME is required")
    endif()
    if(NOT M_LIB)
        # EV + CamelCase of the directory name, matching the existing targets.
        string(SUBSTRING "${M_NAME}" 0 1 _first)
        string(SUBSTRING "${M_NAME}" 1 -1 _rest)
        string(TOUPPER "${_first}" _first)
        set(M_LIB "EV${_first}${_rest}")
    endif()

    set(EVE_MODULE_${M_NAME}_LIB "${M_LIB}" CACHE INTERNAL "")
    set(EVE_MODULE_${M_NAME}_DEPS "${M_DEPS}" CACHE INTERNAL "")
    set(EVE_MODULE_${M_NAME}_OPTIONAL_DEPS "${M_OPTIONAL_DEPS}" CACHE INTERNAL "")
    set(EVE_MODULE_${M_NAME}_THIRDPARTY "${M_THIRDPARTY}" CACHE INTERNAL "")
    set(EVE_MODULE_${M_NAME}_SCRIPT "${M_SCRIPT}" CACHE INTERNAL "")
    set(EVE_MODULE_${M_NAME}_SLOT "${M_SLOT}" CACHE INTERNAL "")
    set(EVE_MODULE_${M_NAME}_GROUP "${M_GROUP}" CACHE INTERNAL "")
    set(EVE_MODULE_${M_NAME}_LAYER "${M_LAYER}" CACHE INTERNAL "")
    set(EVE_MODULE_${M_NAME}_REQUIRED "${M_REQUIRED}" CACHE INTERNAL "")
    set(EVE_MODULE_${M_NAME}_CORE "${M_CORE}" CACHE INTERNAL "")

    list(LENGTH M_SCRIPT _nscript)
    list(LENGTH M_SLOT _nslot)
    if(M_SLOT AND NOT _nscript EQUAL _nslot)
        message(FATAL_ERROR
            "eve_declare_module(${M_NAME}): SCRIPT and SLOT must pair up "
            "(${_nscript} classes vs ${_nslot} slots)")
    endif()

    list(APPEND EVE_ALL_MODULES "${M_NAME}")
    set(EVE_ALL_MODULES "${EVE_ALL_MODULES}" CACHE INTERNAL "Every declared module")
endfunction()

# Whether a module survived resolution. Use to guard per-module tweaks.
function(eve_module_enabled name out)
    if("${name}" IN_LIST EVE_ENABLED_MODULES)
        set(${out} TRUE PARENT_SCOPE)
    else()
        set(${out} FALSE PARENT_SCOPE)
    endif()
endfunction()

# Resolve the manifest against the profile and the per-module overrides.
# Sets, in the caller's scope and the cache:
#   EVE_ENABLED_MODULES   directory names, declaration order
#   EVE_MODULE_LIBS       OBJECT library targets to link
#   EVE_THIRDPARTY_GROUPS third-party groups needed, canonical order
function(eve_resolve_modules)
    if(NOT EVENGINE_PROFILE)
        set(EVENGINE_PROFILE "full")
    endif()

    set(_valid full minimal 2d 3d web procgen-core-only physics-core-only headless server)
    if(NOT EVENGINE_PROFILE IN_LIST _valid)
        message(FATAL_ERROR "EVENGINE_PROFILE must be one of: ${_valid} (got: ${EVENGINE_PROFILE})")
    endif()
    message(STATUS "Module profile: ${EVENGINE_PROFILE}")

    # Host profiles retain the historical required runtime (cmdline, window,
    # graphics, ...). Core/server profiles are deliberately different: they
    # compile and smoke-test domain code without constructing a window or
    # linking a renderer. Keep the seed list explicit so adding a new module
    # cannot silently turn a server build into a client build.
    set(_hostless_profile FALSE)
    set(_profile_seed "")
    if(EVENGINE_PROFILE STREQUAL "procgen-core-only")
        set(_hostless_profile TRUE)
        set(_profile_seed common)
        set(EVENGINE_PROFILE_CORE_KIND procgen CACHE INTERNAL
            "Core boundary selected by the active profile" FORCE)
    elseif(EVENGINE_PROFILE STREQUAL "physics-core-only")
        set(_hostless_profile TRUE)
        set(_profile_seed common event)
        set(EVENGINE_PROFILE_CORE_KIND physics CACHE INTERNAL
            "Core boundary selected by the active profile" FORCE)
    elseif(EVENGINE_PROFILE STREQUAL "headless")
        set(_hostless_profile TRUE)
        set(_profile_seed common data event timer)
        set(EVENGINE_PROFILE_CORE_KIND headless CACHE INTERNAL
            "Core boundary selected by the active profile" FORCE)
    elseif(EVENGINE_PROFILE STREQUAL "server")
        set(_hostless_profile TRUE)
        set(_profile_seed
            common data event timer network authority decision definitions effects
            game_event orders schema social statepatch steering tags transaction
            economy attributes sensing spatial action settlement)
        set(EVENGINE_PROFILE_CORE_KIND server CACHE INTERNAL
            "Core boundary selected by the active profile" FORCE)
    else()
        unset(EVENGINE_PROFILE_CORE_KIND CACHE)
    endif()
    if(_hostless_profile)
        set(EVENGINE_BUILD_HOST OFF CACHE BOOL
            "Build the interactive/native host executable" FORCE)
        set(EVENGINE_PROFILE_HOSTLESS ON CACHE INTERNAL
            "The active profile does not build a host executable" FORCE)
        message(STATUS "Hostless profile: renderer/window host disabled")
    else()
        set(EVENGINE_PROFILE_HOSTLESS OFF CACHE INTERNAL
            "The active profile does not build a host executable" FORCE)
    endif()

    # Accept either casing for the overrides: -DEVENGINE_MODULE_map=OFF and
    # -DEVENGINE_MODULE_MAP=OFF mean the same thing.
    foreach(m IN LISTS EVE_ALL_MODULES)
        string(TOUPPER "${m}" _upper)
        if(DEFINED EVENGINE_MODULE_${_upper} AND NOT DEFINED EVENGINE_MODULE_${m})
            set(EVENGINE_MODULE_${m} ${EVENGINE_MODULE_${_upper}})
        endif()
    endforeach()

    # --- 1. profile selection -------------------------------------------------
    set(_wanted "")
    foreach(m IN LISTS EVE_ALL_MODULES)
        set(_on FALSE)
        if(_hostless_profile)
            if("${m}" IN_LIST _profile_seed)
                set(_on TRUE)
            endif()
        elseif(EVE_MODULE_${m}_REQUIRED)
            set(_on TRUE)
        elseif(EVENGINE_PROFILE STREQUAL "full")
            set(_on TRUE)
        elseif("${EVENGINE_PROFILE}" IN_LIST EVE_MODULE_${m}_GROUP)
            set(_on TRUE)
        endif()

        # --- 2. explicit override -------------------------------------------
        if(DEFINED EVENGINE_MODULE_${m})
            if(EVE_MODULE_${m}_REQUIRED AND NOT EVENGINE_MODULE_${m} AND NOT _hostless_profile)
                message(FATAL_ERROR
                    "Module '${m}' is required by the engine core and cannot be disabled")
            endif()
            if(_hostless_profile AND EVENGINE_MODULE_${m} AND NOT "${m}" IN_LIST _profile_seed)
                message(FATAL_ERROR
                    "Hostless profile '${EVENGINE_PROFILE}' cannot enable '${m}'. "
                    "Use a normal client profile for the interactive/renderer module, "
                    "or add it to the explicit server/core profile seed.")
            endif()
            set(_on ${EVENGINE_MODULE_${m}})
        endif()

        if(_on)
            list(APPEND _wanted "${m}")
        endif()
    endforeach()

    # --- 3. dependency closure ------------------------------------------------
    # A module pulls in what it needs, so -DEVENGINE_MODULE_PARTICLES=ON does not
    # leave the build to fail at link time on missing gpgpu / animation symbols.
    set(_pending ${_wanted})
    while(_pending)
        list(POP_FRONT _pending m)
        foreach(dep IN LISTS EVE_MODULE_${m}_DEPS)
            if(NOT "${dep}" IN_LIST _wanted)
                if(NOT DEFINED EVE_MODULE_${dep}_LIB)
                    message(FATAL_ERROR "Module '${m}' depends on undeclared module '${dep}'")
                endif()
                if(DEFINED EVENGINE_MODULE_${dep} AND NOT EVENGINE_MODULE_${dep})
                    message(FATAL_ERROR
                        "Module '${m}' needs '${dep}', which was disabled with "
                        "-DEVENGINE_MODULE_${dep}=OFF. Disable '${m}' too, or re-enable '${dep}'.")
                endif()
                list(APPEND _wanted "${dep}")
                list(APPEND _pending "${dep}")
            endif()
        endforeach()
    endwhile()

    # --- 4. emit in declaration order ----------------------------------------
    set(_enabled "")
    set(_libs "")
    set(_tp "")
    foreach(m IN LISTS EVE_ALL_MODULES)
        if("${m}" IN_LIST _wanted)
            list(APPEND _enabled "${m}")
            list(APPEND _libs "${EVE_MODULE_${m}_LIB}")
            list(APPEND _tp ${EVE_MODULE_${m}_THIRDPARTY})
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _tp)

    # Canonical order, because GNU ld needs dependents before providers.
    set(_tp_sorted "")
    foreach(g IN LISTS EVE_TP_ORDER)
        if("${g}" IN_LIST _tp)
            list(APPEND _tp_sorted "${g}")
        endif()
    endforeach()
    foreach(g IN LISTS _tp)
        if(NOT "${g}" IN_LIST EVE_TP_ORDER)
            message(FATAL_ERROR "Third-party group '${g}' is not listed in EVE_TP_ORDER")
        endif()
    endforeach()

    # EVENGINE_HAS_<MODULE> lets a module light up an integration only when the
    # other side is in the build, without making it a hard dependency.
    foreach(m IN LISTS _enabled)
        string(TOUPPER "${m}" _upper)
        add_compile_definitions(EVENGINE_HAS_${_upper}=1)
    endforeach()

    set(EVE_ENABLED_MODULES "${_enabled}" CACHE INTERNAL "Modules in this build")
    set(EVE_MODULE_LIBS "${_libs}" CACHE INTERNAL "Module OBJECT libraries to link")
    set(EVE_THIRDPARTY_GROUPS "${_tp_sorted}" CACHE INTERNAL "Third-party groups to link")

    list(LENGTH EVE_ALL_MODULES _total)
    list(LENGTH _enabled _count)
    message(STATUS "Modules enabled: ${_count}/${_total}")
    if(_count LESS _total)
        set(_off "")
        foreach(m IN LISTS EVE_ALL_MODULES)
            if(NOT "${m}" IN_LIST _enabled)
                list(APPEND _off "${m}")
            endif()
        endforeach()
        message(STATUS "Modules disabled: ${_off}")
    endif()
endfunction()

# Emit the module list the boot script iterates (see src/scripts/load.nut).
# Only modules that expose a Squirrel class and name a slot appear.
function(eve_write_module_manifest out_file)
    set(_entries "")
    set(_contracts "")
    foreach(m IN LISTS EVE_ENABLED_MODULES)
        set(_classes ${EVE_MODULE_${m}_SCRIPT})
        set(_slots ${EVE_MODULE_${m}_SLOT})
        if(NOT _classes OR NOT _slots)
            continue()
        endif()
        list(LENGTH _slots _n)
        math(EXPR _last "${_n} - 1")
        foreach(i RANGE ${_last})
            list(GET _classes ${i} _cls)
            list(GET _slots ${i} _slot)
            list(APPEND _entries "{ slot = \"${_slot}\", cls = \"${_cls}\" }")
        endforeach()
    endforeach()
    foreach(m IN LISTS EVE_ALL_MODULES)
        if("${m}" IN_LIST EVE_ENABLED_MODULES)
            set(_enabled true)
        else()
            set(_enabled false)
        endif()
        foreach(_field IN ITEMS DEPS OPTIONAL_DEPS GROUP SCRIPT SLOT)
            set(_values ${EVE_MODULE_${m}_${_field}})
            if(_values)
                string(JOIN "\", \"" _values_joined ${_values})
                set(_${_field}_array "[\"${_values_joined}\"]")
            else()
                set(_${_field}_array "[]")
            endif()
        endforeach()
        if(EVE_MODULE_${m}_REQUIRED)
            set(_required true)
        else()
            set(_required false)
        endif()
        list(APPEND _contracts
            "{ name = \"${m}\", enabled = ${_enabled}, required = ${_required}, layer = ${EVE_MODULE_${m}_LAYER}, deps = ${_DEPS_array}, optionalDeps = ${_OPTIONAL_DEPS_array}, profiles = ${_GROUP_array}, classes = ${_SCRIPT_array}, slots = ${_SLOT_array} }")
    endforeach()
    string(JOIN "\n    " _joined ${_entries})
    string(JOIN "\n    " _contracts_joined ${_contracts})
    file(WRITE "${out_file}"
        "// Generated from cmake/module_manifest.cmake -- do not edit.\n"
        "// Consumed by src/scripts/load.nut to bind whatever modules this build contains.\n"
        "eve_modules <- [\n    ${_joined}\n];\n"
        "// Complete build/tooling contract, including modules disabled by the selected profile.\n"
        "eve_module_contract <- [\n    ${_contracts_joined}\n];\n")
endfunction()
