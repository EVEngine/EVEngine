# Bundles the Vulkan loader + MoltenVK from the LunarG SDK next to the
# installed eve binary so players do NOT need to install the Vulkan SDK.
# Runs at `cmake --install` time via install(SCRIPT) (see install_sdk.cmake).
#
# Resulting layout:
#   <prefix>/bin/eve
#   <prefix>/lib/libvulkan.1.dylib    loader (LC_ID_DYLIB -> @rpath/libvulkan.1.dylib)
#   <prefix>/lib/libMoltenVK.dylib    ICD   (LC_ID_DYLIB -> @rpath/libMoltenVK.dylib)
#   <prefix>/lib/MoltenVK_icd.json    ICD manifest (library_path -> libMoltenVK.dylib)
#
# eve is rebased to load "@rpath/libvulkan.1.dylib" and carries
# INSTALL_RPATH "@loader_path/../lib" (set in install_sdk.cmake), so the whole
# tree runs without VULKAN_SDK / DYLD_* / VK_ICD_FILENAMES. At startup eve also
# points SDL at the bundled loader and sets VK_ICD_FILENAMES to the bundled
# manifest (platform/macosx bootstrapBundledVulkan).

if(NOT CMAKE_INSTALL_PREFIX)
    message(FATAL_ERROR "macosx_bundle_vulkan.cmake must run through install(SCRIPT)")
endif()

# --- Locate the LunarG SDK lib dir ------------------------------------------
set(_eve_vk_lib "$ENV{VULKAN_SDK}/lib")
if(NOT EXISTS "${_eve_vk_lib}/libvulkan.1.dylib")
    set(_eve_vk_lib "$ENV{VULKAN_SDK}/macOS/lib")
endif()
if(NOT EXISTS "${_eve_vk_lib}/libvulkan.1.dylib")
    # Fallback: newest SDK under the user's home (setup-env.sh may not have
    # been sourced in the installing shell).
    set(_eve_vk_lib "")
    file(GLOB _eve_vk_sdks
        "$ENV{HOME}/VulkanSDK/*/macOS/lib"
        "$ENV{HOME}/VulkanSDK/*/lib")
    foreach(_cand IN LISTS _eve_vk_sdks)
        if(EXISTS "${_cand}/libvulkan.1.dylib")
            set(_eve_vk_lib "${_cand}")
        endif()
    endforeach()
endif()
if(NOT EXISTS "${_eve_vk_lib}/libvulkan.1.dylib")
    message(WARNING
        "Vulkan bundle: LunarG SDK lib not found (VULKAN_SDK unset?). "
        "Installed eve will need MoltenVK from a separately installed Vulkan SDK.")
    return()
endif()

set(_eve_lib "${CMAKE_INSTALL_PREFIX}/lib")
file(MAKE_DIRECTORY "${_eve_lib}")

# --- Copy loader + MoltenVK -------------------------------------------------
foreach(_eve_dylib IN ITEMS
        "${_eve_vk_lib}/libvulkan.1.dylib"
        "${_eve_vk_lib}/libMoltenVK.dylib")
    if(EXISTS "${_eve_dylib}")
        file(INSTALL "${_eve_dylib}" DESTINATION "${_eve_lib}")
    endif()
endforeach()

# --- ICD manifest (library_path relative to the manifest directory) ---------
set(_eve_icd "")
foreach(_cand IN ITEMS
        "${_eve_vk_lib}/../etc/vulkan/icd.d/MoltenVK_icd.json"
        "${_eve_vk_lib}/../share/vulkan/icd.d/MoltenVK_icd.json")
    if(EXISTS "${_cand}")
        set(_eve_icd "${_cand}")
        break()
    endif()
endforeach()
if(NOT _eve_icd)
    message(WARNING "Vulkan bundle: MoltenVK_icd.json not found; MoltenVK will not be discoverable.")
else()
    file(READ "${_eve_icd}" _eve_icd_json)
    string(REGEX REPLACE
        "\"library_path\"[ \t\r\n]*:[ \t\r\n]*\"[^\"]*\""
        "\"library_path\" : \"libMoltenVK.dylib\""
        _eve_icd_json "${_eve_icd_json}")
    file(WRITE "${_eve_lib}/MoltenVK_icd.json" "${_eve_icd_json}")
endif()

# --- Rebase eve's loader reference and dylib ids ----------------------------
find_program(_eve_otool otool)
find_program(_eve_itool install_name_tool)
if(_eve_otool AND _eve_itool)
    set(_eve_exe "${CMAKE_INSTALL_PREFIX}/bin/eve")
    if(EXISTS "${_eve_exe}")
        execute_process(COMMAND "${_eve_otool}" -L "${_eve_exe}"
            OUTPUT_VARIABLE _eve_otool_out RESULT_VARIABLE _eve_otool_rc)
        if(_eve_otool_rc EQUAL 0)
            string(REGEX MATCHALL "[^\r\n]*libvulkan[^\r\n]*" _eve_vk_refs "${_eve_otool_out}")
            foreach(_eve_ref IN LISTS _eve_vk_refs)
                string(REGEX REPLACE "^[ \t]*([^ \t(]+).*" "\\1" _eve_ref_path "${_eve_ref}")
                if(_eve_ref_path AND NOT _eve_ref_path STREQUAL "@rpath/libvulkan.1.dylib")
                    execute_process(COMMAND "${_eve_itool}" -change "${_eve_ref_path}"
                        "@rpath/libvulkan.1.dylib" "${_eve_exe}"
                        RESULT_VARIABLE _eve_change_rc)
                endif()
            endforeach()
        endif()
    endif()
    foreach(_eve_dylib IN ITEMS "${_eve_lib}/libvulkan.1.dylib" "${_eve_lib}/libMoltenVK.dylib")
        if(EXISTS "${_eve_dylib}")
            get_filename_component(_eve_dylib_name "${_eve_dylib}" NAME)
            execute_process(COMMAND "${_eve_itool}" -id "@rpath/${_eve_dylib_name}" "${_eve_dylib}"
                RESULT_VARIABLE _eve_id_rc)
        endif()
    endforeach()
endif()

# --- Re-sign (install_name_tool invalidates the existing ad-hoc signature) --
find_program(_eve_codesign codesign)
if(_eve_codesign)
    foreach(_eve_sig IN ITEMS
            "${CMAKE_INSTALL_PREFIX}/bin/eve"
            "${_eve_lib}/libvulkan.1.dylib"
            "${_eve_lib}/libMoltenVK.dylib")
        if(EXISTS "${_eve_sig}")
            execute_process(COMMAND "${_eve_codesign}" --force -s - "${_eve_sig}"
                RESULT_VARIABLE _eve_sig_rc)
        endif()
    endforeach()
endif()
