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
include("${CMAKE_SOURCE_DIR}/cmake/TestLevels.cmake")
eve_configure_test_levels(EVE_TEST_LEVEL_FILE)
include("${EVE_TEST_LEVEL_FILE}")

if(EVENGINE_PROFILE MATCHES "^physics-bake(-script)?-only$")
    add_subdirectory("${CMAKE_SOURCE_DIR}/cmake/physics_bake_check" "${CMAKE_BINARY_DIR}/profile/physics_bake")
endif()

if(EVENGINE_PROFILE STREQUAL "physics-bake-render")
    enable_testing()
    add_subdirectory("${CMAKE_SOURCE_DIR}/cmake/physics_bake_render_check" "${CMAKE_BINARY_DIR}/profile/bake_render")
endif()

# The real crowd solver and bindings link without graphics, window or RTS.
if(TARGET EVCrowd)
    add_subdirectory("${CMAKE_SOURCE_DIR}/cmake/crowd_check" "${CMAKE_BINARY_DIR}/profile/crowd")
endif()

if(TARGET eve AND TARGET EVGraphics)
    eve_ensure_graphics_window_fixtures()
    eve_add_test_executable(eve_skin_palette_check
        "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
        "${CMAKE_SOURCE_DIR}/test/graphics_skin_palette.cpp"
        "${CMAKE_SOURCE_DIR}/test/graphics_skin_palette_render.cpp")
    target_compile_features(eve_skin_palette_check PRIVATE cxx_std_20)
    target_link_libraries(eve_skin_palette_check PRIVATE eve_graphics_window_fixtures)
    get_target_property(_eve_skin_host_libraries eve LINK_LIBRARIES)
    target_link_libraries(eve_skin_palette_check PRIVATE ${_eve_skin_host_libraries}
        zeroerr eve_engine_includes)
    # Execute this profile contract, not every case sharing its physical CPP.
    set(_eve_skin_palette_filter "^graphics[.]skinPalette[.].*$")
    add_test(NAME profile.skin_palette COMMAND eve_skin_palette_check "--testcase=${_eve_skin_palette_filter}")
    eve_label_test_group(profile.skin_palette graphics_skin_palette.cpp graphics_skin_palette_render.cpp)
    if(TARGET EVCombat)
        eve_add_test_executable(eve_combat_presentation_check
            "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
            "${CMAKE_SOURCE_DIR}/test/integration/combat-presentation/combat_settlement.cpp"
            "${CMAKE_SOURCE_DIR}/test/integration/combat-presentation/combat_presentation_script.cpp"
            "${CMAKE_SOURCE_DIR}/test/unit/graphics/graphics_damage_numbers.cpp"
            "${CMAKE_SOURCE_DIR}/test/integration/combat-presentation/combat_damage_numbers.cpp"
            "${CMAKE_SOURCE_DIR}/test/integration/combat-presentation/graphics_damage_numbers_script.cpp")
        target_compile_features(eve_combat_presentation_check PRIVATE cxx_std_20)
        target_link_libraries(eve_combat_presentation_check PRIVATE eve_graphics_window_fixtures
            ${_eve_skin_host_libraries} zeroerr eve_engine_includes)
        add_test(NAME profile.combat_presentation COMMAND eve_combat_presentation_check)
    endif()
    if(TARGET EVModel3D)
        eve_add_test_executable(eve_pbr_surface_check
            "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
            "${CMAKE_SOURCE_DIR}/test/graphics_pbr_surface.cpp"
            "${CMAKE_SOURCE_DIR}/test/model3d_gltf_uv.cpp")
        target_compile_features(eve_pbr_surface_check PRIVATE cxx_std_20)
        target_link_libraries(eve_pbr_surface_check PRIVATE eve_graphics_window_fixtures)
        target_link_libraries(eve_pbr_surface_check PRIVATE ${_eve_skin_host_libraries}
            zeroerr eve_engine_includes)
        # Execute this profile contract, not every case sharing its physical CPP.
        set(_eve_pbr_surface_filter "^(graphics[.]pbrSurface[.](atomicValidationAndAbsentBackend|extensionFactorsAndUvChangePixels)|graphics[.]shaderCustomMeshTexturesValidateSlotsAndPreserveBorrows|model3d[.](gltfUvOriginAndTransformSurviveLoader|missingPreviewTextureRetainsMaterialFactors))$")
        add_test(NAME profile.pbr_surface COMMAND eve_pbr_surface_check "--testcase=${_eve_pbr_surface_filter}")
        eve_label_test_group(profile.pbr_surface graphics_pbr_surface.cpp model3d_gltf_uv.cpp)
    endif()
    if(TARGET EVAsset_graphics)
        eve_add_test_executable(eve_texture_mip_check
            "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
            "${CMAKE_SOURCE_DIR}/test/graphics_texture_mips.cpp")
        target_compile_features(eve_texture_mip_check PRIVATE cxx_std_20)
        target_link_libraries(eve_texture_mip_check PRIVATE eve_graphics_window_fixtures)
        target_link_libraries(eve_texture_mip_check PRIVATE ${_eve_skin_host_libraries}
            zeroerr eve_engine_includes)
        # Execute this profile contract, not every case sharing its physical CPP.
        set(_eve_texture_mips_filter "^(graphics[.]texture[.](explicit_mips_preserve_every_uploaded_level|rgba16f_array_upload_is_atomic_and_backend_explicit|rgba8_volume_upload_is_atomic_and_backend_explicit)|graphics[.]pbrSurface[.]texture_dimensions_are_validated_before_publication)$")
        add_test(NAME profile.texture_mips COMMAND eve_texture_mip_check "--testcase=${_eve_texture_mips_filter}")
        eve_label_test_group(profile.texture_mips graphics_texture_mips.cpp)
        eve_add_test_executable(eve_unity_normal_mip_graphics_probe
            "${CMAKE_SOURCE_DIR}/test/asset_import/UnityNormalMipGraphicsProbe.cpp")
        target_compile_features(eve_unity_normal_mip_graphics_probe PRIVATE cxx_std_20)
        target_link_libraries(eve_unity_normal_mip_graphics_probe PRIVATE ${_eve_skin_host_libraries}
            eve_engine_includes)
        eve_add_test_executable(eve_unity_material_graphics_probe
            "${CMAKE_SOURCE_DIR}/test/asset_import/UnityMaterialGraphicsProbe.cpp")
        target_compile_features(eve_unity_material_graphics_probe PRIVATE cxx_std_20)
        target_link_libraries(eve_unity_material_graphics_probe PRIVATE ${_eve_skin_host_libraries}
            eve_engine_includes)
        eve_add_test_executable(eve_unity_volume_graphics_probe
            "${CMAKE_SOURCE_DIR}/test/asset_import/UnityVolumeGraphicsProbe.cpp")
        target_compile_features(eve_unity_volume_graphics_probe PRIVATE cxx_std_20)
        target_link_libraries(eve_unity_volume_graphics_probe PRIVATE ${_eve_skin_host_libraries}
            eve_engine_includes)
    endif()
    if(TARGET EVAsset_import AND TARGET EVAsset_graphics)
        eve_add_test_executable(eve_material_compat_check
            "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
            "${CMAKE_SOURCE_DIR}/test/integration/asset-pipeline/asset_pipeline_graphics_file_admission_tests.cpp"
            "${CMAKE_SOURCE_DIR}/test/integration/asset-pipeline/asset_pipeline_graphics_import_workflows_tests.cpp"
            "${CMAKE_SOURCE_DIR}/test/asset_graphics_material_color.cpp")
        target_compile_definitions(eve_material_compat_check PRIVATE EVE_TEST_MESH_UPLOAD=1)
        target_compile_features(eve_material_compat_check PRIVATE cxx_std_20)
        target_link_libraries(eve_material_compat_check PRIVATE ${_eve_skin_host_libraries}
            zeroerr eve_engine_includes)
        # Execute this profile contract, not every case sharing its physical CPP.
        set(_eve_material_compat_filter "^(asset[.]import[.](unityNative(Mesh|Submesh)|unityNested|unityVegetationPrefabDependencies|gltfMaterial|gltfMissingTexture|materialV14Color|gltfExtensions|gltfUnlit|gltfTextureTransform|gltfPermissive|pngTraverses|realGlb|gltfRejectsNetwork|tamperedStructuredReport)|asset[.]graphics[.](material|validatesCanonicalMesh|uploadsCanonicalVertexColors|invalidMesh|backendRejection|imageValidation|imageBackendRejection)|asset[.]mesh[.](multipleUvSets|rejectsMalformedUv|graphicsSelectsUv|legacyBinary|rejectsInvalidSourceUv)).*$")
        add_test(NAME profile.material_compat COMMAND eve_material_compat_check "--testcase=${_eve_material_compat_filter}")
        set_tests_properties(profile.material_compat PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
        eve_label_test_group(profile.material_compat asset_import_unity_vegetation_material.cpp asset_mesh_attributes.cpp
            asset_import.cpp asset_mesh_uv.cpp)
    endif()
endif()

foreach(_eve_graphics_profile_target IN ITEMS
        eve_skin_palette_check eve_pbr_surface_check eve_texture_mip_check eve_material_compat_check)
    if(TARGET ${_eve_graphics_profile_target})
        target_compile_definitions(${_eve_graphics_profile_target} PRIVATE
            EVENGINE_TEST_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}"
            EVENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    endif()
endforeach()

set(_eve_profile_check_sources
    "${CMAKE_SOURCE_DIR}/test/profile/public_headers_common.cpp"
    "${CMAKE_SOURCE_DIR}/test/profile/public_headers_domains.cpp")
add_library(eve_public_header_checks OBJECT ${_eve_profile_check_sources})
target_compile_features(eve_public_header_checks PRIVATE cxx_std_20)
target_link_libraries(eve_public_header_checks PRIVATE eve_engine_includes)
set_target_properties(eve_public_header_checks PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    EXCLUDE_FROM_ALL FALSE)

set(_eve_capability_sources
    "${CMAKE_SOURCE_DIR}/src/engine/common/Capability.cpp"
    "${CMAKE_SOURCE_DIR}/src/engine/common/CapabilityOwned.cpp")
eve_add_test_executable(eve_capability_present_check
    "${CMAKE_SOURCE_DIR}/test/profile/capability_present.cpp"
    ${_eve_capability_sources})
eve_add_test_executable(eve_capability_absent_check
    "${CMAKE_SOURCE_DIR}/test/profile/capability_absent.cpp"
    ${_eve_capability_sources})
foreach(_eve_capability_target IN ITEMS
        eve_capability_present_check eve_capability_absent_check)
    target_compile_features(${_eve_capability_target} PRIVATE cxx_std_20)
    target_link_libraries(${_eve_capability_target} PRIVATE eve_engine_includes)
    if(EVENGINE_MODULE_LINKAGE STREQUAL "SHARED")
        # These isolated executables own their registry sources rather than importing an engine group.
        target_compile_definitions(${_eve_capability_target} PRIVATE
            EVENGINE_ENGINE_EXPORTS EVENGINE_EXPORTS_FOUNDATION)
    endif()
    set_target_properties(${_eve_capability_target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/profile")
endforeach()

set(_eve_profile_smoke_dependencies
    eve_public_header_checks
    eve_capability_present_check
    eve_capability_absent_check)
if(TARGET eve_bake_playback_check)
    list(APPEND _eve_profile_smoke_dependencies eve_bake_playback_check eve_bake_measure eve_bake_collapse_inspect)
endif()
if(TARGET eve_bake_script_check)
    list(APPEND _eve_profile_smoke_dependencies eve_bake_script_check)
endif()
if(TARGET eve_bake_render_check)
    list(APPEND _eve_profile_smoke_dependencies eve_bake_render_check)
endif()
if(TARGET eve_crowd_check)
    list(APPEND _eve_profile_smoke_dependencies eve_crowd_check)
endif()
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
    eve_label_test_group(profile.capability.present capability_present.cpp)
    eve_label_test_group(profile.capability.absent capability_absent.cpp)
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

# Prove the ocean/body composition without the graphics, gpgpu or fluids facade.
# Physics retains its own core source authority; the ocean adapter only submits forces.
if(EVENGINE_PROFILE STREQUAL "physics-core-only" AND TARGET EVPhysicsCoreCheck)
    add_executable(eve_ocean_boat_check
        "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
        "${CMAKE_SOURCE_DIR}/test/integration/ocean-voyage/ocean_boat.cpp"
        "${CMAKE_SOURCE_DIR}/src/modules/fluids/OceanSurface.cpp"
        "${CMAKE_SOURCE_DIR}/src/modules/fluids/OceanSpectrum.cpp"
        "${CMAKE_SOURCE_DIR}/src/modules/fluids/OceanSpectrumFrame.cpp"
        "${CMAKE_SOURCE_DIR}/src/modules/fluids/OceanWake.cpp"
        "${CMAKE_SOURCE_DIR}/src/modules/fluids/OceanWaterView.cpp"
        "${CMAKE_SOURCE_DIR}/src/modules/fluids/OceanBoatWake.cpp"
        "${CMAKE_SOURCE_DIR}/src/modules/fluids/OceanBoat.cpp")
    target_compile_features(eve_ocean_boat_check PRIVATE cxx_std_20)
    eve_thirdparty_libs(_eve_ocean_libraries squirrel sdl2 box2d box3d)
    eve_append_system_libraries(_eve_ocean_system_libraries)
    target_link_libraries(eve_ocean_boat_check PRIVATE EVPhysicsCoreCheck EVPlatform_event EVCommon
        zeroerr eve_engine_includes ${_eve_ocean_libraries} ${_eve_ocean_system_libraries})
    foreach(_eve_ocean_case boatFloatsAndPropelsRealRigidBody boatRejectsRepeatedStepAndStaleOwners
            boatRespondsToSpectralWaves dryPropellerAndAdapterFirstDestruction coreWorldQueriesSurviveSplit
            boatWakeChangesSecondRigidBody)
        add_test(NAME "fluids.ocean.${_eve_ocean_case}"
            COMMAND eve_ocean_boat_check "--testcase=^fluids.ocean.${_eve_ocean_case}$" --no-color)
        set_tests_properties("fluids.ocean.${_eve_ocean_case}" PROPERTIES
            PASS_REGULAR_EXPRESSION "TEST CASE: +1 +0 +0 +0"
            FAIL_REGULAR_EXPRESSION "Assertion Failed")
    endforeach()
endif()

# The glTF admission boundary is data-only even when the CLI/import module is
# excluded. Exercise the real importer and archive/Cook path in that profile.
if(TARGET EVAsset)
    eve_add_test_executable(eve_gltf_animation_check
        "${CMAKE_SOURCE_DIR}/test/profile/gltf_animation_main.cpp"
        "${CMAKE_SOURCE_DIR}/test/integration/asset-pipeline/asset_pipeline_graphics_file_admission_tests.cpp"
        "${CMAKE_SOURCE_DIR}/src/modules/asset/import/GltfImporter.cpp"
        "${CMAKE_SOURCE_DIR}/src/modules/asset/import/GltfAnimation.cpp"
        "${CMAKE_SOURCE_DIR}/src/modules/asset/import/GltfDecode.cpp"
        "${CMAKE_SOURCE_DIR}/src/modules/asset/import/GltfMaterials.cpp"
        "${CMAKE_SOURCE_DIR}/src/modules/asset/import/ImageImporter.cpp"
        "${CMAKE_SOURCE_DIR}/src/modules/asset/import/ImportReport.cpp")
    target_compile_features(eve_gltf_animation_check PRIVATE cxx_std_20)
    if(EVENGINE_MODULE_LINKAGE STREQUAL "SHARED")
        # These importer implementations belong to this data-only executable;
        # dependencies such as EVAsset remain ordinary DLL consumers.
        get_target_property(_eve_gltf_sources eve_gltf_animation_check SOURCES)
        foreach(_eve_gltf_source IN LISTS _eve_gltf_sources)
            if(_eve_gltf_source MATCHES "/src/modules/asset/import/")
                set_property(SOURCE "${_eve_gltf_source}" APPEND PROPERTY
                    COMPILE_DEFINITIONS EVENGINE_EXPORTS_PLATFORM)
            endif()
        endforeach()
    endif()
    eve_thirdparty_libs(_eve_gltf_libraries ${EVE_THIRDPARTY_GROUPS})
    eve_append_system_libraries(_eve_gltf_system_libraries)
    target_link_libraries(eve_gltf_animation_check PRIVATE EVAsset EVData EVCommon zeroerr eve_engine_includes
        ${_eve_gltf_libraries} ${_eve_gltf_system_libraries})
    add_test(NAME profile.gltf_animation COMMAND eve_gltf_animation_check)
    eve_label_test_group(profile.gltf_animation asset_import_gltf_animation.cpp asset_import_gltf_material.cpp
        asset_migration.cpp asset_mesh_uv.cpp asset_mesh_attributes.cpp asset_import_tiff.cpp asset_import_tga.cpp)
endif()
