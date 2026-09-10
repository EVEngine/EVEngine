# Independent profile contract targets.
#
# These targets deliberately do not link unit_test or the host executable. They
# compile public headers as standalone translation units and exercise the
# capability registry with both provider-present and provider-absent cases.
# Core profiles additionally depend on their curated OBJECT boundary target;
# this makes `cmake --build ... --target eve_profile_smoke` compile the boundary
# without pulling in a renderer.

if(NOT EVENGINE_BUILD_PROFILE_CHECKS)
    return()
endif()

if(TARGET eve AND TARGET EVGraphics)
    add_executable(eve_skin_palette_check
        "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
        "${CMAKE_SOURCE_DIR}/test/graphics_skin_palette.cpp"
        "${CMAKE_SOURCE_DIR}/test/graphics_skin_palette_render.cpp")
    target_compile_features(eve_skin_palette_check PRIVATE cxx_std_20)
    get_target_property(_eve_skin_host_libraries eve LINK_LIBRARIES)
    target_link_libraries(eve_skin_palette_check PRIVATE ${_eve_skin_host_libraries}
        zeroerr eve_engine_includes)
    add_test(NAME profile.skin_palette COMMAND eve_skin_palette_check)
    add_executable(eve_pbr_surface_check
        "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
        "${CMAKE_SOURCE_DIR}/test/graphics_pbr_surface.cpp"
        "${CMAKE_SOURCE_DIR}/test/model3d_gltf_uv.cpp")
    target_compile_features(eve_pbr_surface_check PRIVATE cxx_std_20)
    target_link_libraries(eve_pbr_surface_check PRIVATE ${_eve_skin_host_libraries}
        zeroerr eve_engine_includes)
    add_test(NAME profile.pbr_surface COMMAND eve_pbr_surface_check)
    if(TARGET EVAsset_import AND TARGET EVAsset_graphics)
        add_executable(eve_material_compat_check
            "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
            "${CMAKE_SOURCE_DIR}/test/asset_import_unity_native.cpp"
            "${CMAKE_SOURCE_DIR}/test/asset_import_gltf_material.cpp"
            "${CMAKE_SOURCE_DIR}/test/asset_graphics_loader.cpp"
            "${CMAKE_SOURCE_DIR}/test/asset_import.cpp"
            "${CMAKE_SOURCE_DIR}/test/asset_mesh_uv.cpp")
        target_compile_definitions(eve_material_compat_check PRIVATE EVE_TEST_MESH_UPLOAD=1)
        target_compile_features(eve_material_compat_check PRIVATE cxx_std_20)
        target_link_libraries(eve_material_compat_check PRIVATE ${_eve_skin_host_libraries}
            zeroerr eve_engine_includes)
        add_test(NAME profile.material_compat COMMAND eve_material_compat_check)
    endif()
endif()

set(_eve_profile_check_sources
    "${CMAKE_SOURCE_DIR}/test/profile/public_headers_common.cpp"
    "${CMAKE_SOURCE_DIR}/test/profile/public_headers_domains.cpp")
add_library(eve_public_header_checks OBJECT ${_eve_profile_check_sources})
target_compile_features(eve_public_header_checks PRIVATE cxx_std_20)
target_link_libraries(eve_public_header_checks PRIVATE eve_engine_includes)
set_target_properties(eve_public_header_checks PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    EXCLUDE_FROM_ALL FALSE)

set(_eve_capability_source "${CMAKE_SOURCE_DIR}/src/engine/common/Capability.cpp")
add_executable(eve_capability_present_check
    "${CMAKE_SOURCE_DIR}/test/profile/capability_present.cpp"
    ${_eve_capability_source})
add_executable(eve_capability_absent_check
    "${CMAKE_SOURCE_DIR}/test/profile/capability_absent.cpp"
    ${_eve_capability_source})
foreach(_eve_capability_target IN ITEMS
        eve_capability_present_check eve_capability_absent_check)
    target_compile_features(${_eve_capability_target} PRIVATE cxx_std_20)
    target_link_libraries(${_eve_capability_target} PRIVATE eve_engine_includes)
    set_target_properties(${_eve_capability_target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/profile")
endforeach()

set(_eve_profile_smoke_dependencies
    eve_public_header_checks
    eve_capability_present_check
    eve_capability_absent_check)
foreach(_eve_profile_module_lib IN LISTS EVE_MODULE_LIBS)
    if(TARGET ${_eve_profile_module_lib})
        list(APPEND _eve_profile_smoke_dependencies ${_eve_profile_module_lib})
    endif()
endforeach()
if(TARGET EVPhysicsCoreCheck)
    list(APPEND _eve_profile_smoke_dependencies EVPhysicsCoreCheck)
endif()
if(TARGET EVProcgenCoreCheck)
    list(APPEND _eve_profile_smoke_dependencies EVProcgenCoreCheck)
endif()
add_custom_target(eve_profile_smoke
    DEPENDS ${_eve_profile_smoke_dependencies})

if(BUILD_TESTING)
    add_test(NAME profile.capability.present COMMAND eve_capability_present_check)
    add_test(NAME profile.capability.absent COMMAND eve_capability_absent_check)
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_target(eve_nodiscard_compile_check
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/check_nodiscard.py
            --source-dir ${CMAKE_SOURCE_DIR}
    USES_TERMINAL)
if(BUILD_TESTING)
    add_test(NAME profile.nodiscard_compile_diagnostics
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/check_nodiscard.py
                --source-dir ${CMAKE_SOURCE_DIR})
    set_tests_properties(profile.nodiscard_compile_diagnostics PROPERTIES LABELS "quality")
endif()

message(STATUS "Independent profile checks enabled for '${EVENGINE_PROFILE}'")

# The glTF admission boundary is data-only even when the CLI/import module is
# excluded. Exercise the real importer and archive/Cook path in that profile.
if(TARGET EVAsset)
    add_executable(eve_gltf_animation_check
        "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
        "${CMAKE_SOURCE_DIR}/test/asset_import_gltf_animation.cpp"
        "${CMAKE_SOURCE_DIR}/test/asset_import_gltf_material.cpp"
        "${CMAKE_SOURCE_DIR}/test/asset_migration.cpp"
        "${CMAKE_SOURCE_DIR}/test/asset_mesh_uv.cpp"
        "${CMAKE_SOURCE_DIR}/src/modules/asset/import/GltfImporter.cpp"
        "${CMAKE_SOURCE_DIR}/src/modules/asset/import/GltfAnimation.cpp"
        "${CMAKE_SOURCE_DIR}/src/modules/asset/import/GltfDecode.cpp"
        "${CMAKE_SOURCE_DIR}/src/modules/asset/import/GltfMaterials.cpp"
        "${CMAKE_SOURCE_DIR}/src/modules/asset/import/ImageImporter.cpp"
        "${CMAKE_SOURCE_DIR}/src/modules/asset/import/ImportReport.cpp")
    target_compile_features(eve_gltf_animation_check PRIVATE cxx_std_20)
    eve_thirdparty_libs(_eve_gltf_libraries ${EVE_THIRDPARTY_GROUPS})
    eve_append_system_libraries(_eve_gltf_system_libraries)
    target_link_libraries(eve_gltf_animation_check PRIVATE EVAsset EVData EVCommon zeroerr eve_engine_includes
        ${_eve_gltf_libraries} ${_eve_gltf_system_libraries})
    add_test(NAME profile.gltf_animation COMMAND eve_gltf_animation_check)
endif()
