# Bundles the runtime DLLs eve.exe needs (Vulkan loader + VC++ redistributable)
# into the installed SDK's bin/ so packaged games are self-contained and can be
# produced from ANY host (Linux/macOS cross-packaging included).
# Runs at `cmake --install` time via install(SCRIPT) (see install_sdk.cmake).
#
# Resulting layout:
#   <prefix>/bin/eve.exe
#   <prefix>/bin/vulkan-1.dll      Vulkan loader (players do NOT need the SDK)
#   <prefix>/bin/vcruntime140.dll  VC++ redistributable (msvcp140 / vcruntime140*)
#
# `eve package --sdk <win32-sdk>` copies these next to the packaged eve.exe, and
# Windows loads DLLs from the executable's directory first, so the game runs on
# a machine without any toolchain installed.

if(NOT CMAKE_INSTALL_PREFIX)
    message(FATAL_ERROR "win32_bundle_runtime.cmake must run through install(SCRIPT)")
endif()

set(_eve_bin "${CMAKE_INSTALL_PREFIX}/bin")
file(MAKE_DIRECTORY "${_eve_bin}")

# --- Vulkan loader ----------------------------------------------------------
set(_eve_vulkan_dll "")
foreach(_cand IN ITEMS
        "$ENV{VULKAN_SDK}/Bin/vulkan-1.dll"
        "$ENV{VULKAN_SDK}/bin/vulkan-1.dll")
    if(EXISTS "${_cand}")
        set(_eve_vulkan_dll "${_cand}")
    endif()
endforeach()
if(NOT _eve_vulkan_dll)
    # Fallback: newest SDK under common install roots (setup may not have been
    # sourced in the installing shell).
    file(GLOB _eve_vk_sdks
        "C:/VulkanSDK/*/Bin/vulkan-1.dll"
        "C:/Program Files/VulkanSDK/*/Bin/vulkan-1.dll")
    list(SORT _eve_vk_sdks)
    if(_eve_vk_sdks)
        list(GET _eve_vk_sdks -1 _eve_vulkan_dll)
    endif()
endif()
if(NOT EXISTS "${_eve_vulkan_dll}")
    message(WARNING
        "win32 bundle: vulkan-1.dll not found (VULKAN_SDK unset?). "
        "Packaged games will need a Vulkan loader from the user's GPU driver.")
else()
    file(INSTALL "${_eve_vulkan_dll}" DESTINATION "${_eve_bin}")
    message(STATUS "win32 bundle: vulkan-1.dll -> ${_eve_bin}")
endif()

# --- VC++ redistributable ---------------------------------------------------
# VS installs the redist under
#   <VS root>/<version>/<edition>/VC/Redist/MSVC/<ver>/x64/Microsoft.VC*.CRT/
# e.g. C:/Program Files/Microsoft Visual Studio/18/Community/...
set(_eve_crt_subs "")
foreach(_eve_ms_root IN ITEMS
        "C:/Program Files/Microsoft Visual Studio"
        "C:/Program Files (x86)/Microsoft Visual Studio")
    if(NOT EXISTS "${_eve_ms_root}")
        continue()
    endif()
    file(GLOB _eve_vs_roots "${_eve_ms_root}/*/")
    foreach(_eve_vs_root IN LISTS _eve_vs_roots)
        file(GLOB _eve_redist_vers "${_eve_vs_root}VC/Redist/MSVC/*")
        foreach(_eve_ver IN LISTS _eve_redist_vers)
            file(GLOB _eve_crt_subs_now
                "${_eve_ver}/x64/Microsoft.VC*.CRT"
                "${_eve_ver}/x64/Microsoft.VC*.DebugCRT")
            list(APPEND _eve_crt_subs ${_eve_crt_subs_now})
        endforeach()
    endforeach()
endforeach()
list(REMOVE_DUPLICATES _eve_crt_subs)

set(_eve_crt_dlls
    msvcp140.dll msvcp140_1.dll msvcp140_2.dll msvcp140_codecvt_ids.dll
    vcruntime140.dll vcruntime140_1.dll concrt140.dll
    msvcp140d.dll vcruntime140d.dll vcruntime140_1d.dll ucrtbased.dll
    concrt140d.dll)
foreach(_eve_dll IN LISTS _eve_crt_dlls)
    set(_eve_src "")
    foreach(_eve_dir IN LISTS _eve_crt_subs)
        if(EXISTS "${_eve_dir}/${_eve_dll}")
            set(_eve_src "${_eve_dir}/${_eve_dll}")
            break()
        endif()
    endforeach()
    if(_eve_src)
        file(INSTALL "${_eve_src}" DESTINATION "${_eve_bin}")
    else()
        message(STATUS "win32 bundle: ${_eve_dll} not found (may be system-installed)")
    endif()
endforeach()
