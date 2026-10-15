cmake_minimum_required(VERSION 3.21)
project(EveOceanCoreCheck LANGUAGES C CXX)

# Independent link evidence: this target has no graphics, physics, Vulkan or
# fluids facade object files. The same tests also enter the main per-case suite.
get_filename_component(EVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_TEST OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(USE_MOLD OFF CACHE BOOL "" FORCE)
add_subdirectory("${EVE_ROOT}/external/zeroerr" zeroerr)
add_executable(ocean_core_check
    "${EVE_ROOT}/src/engine/common/Diagnostic.cpp"
    "${EVE_ROOT}/src/engine/common/Status.cpp"
    "${EVE_ROOT}/src/engine/common/Result.cpp"
    "${EVE_ROOT}/src/engine/common/Value.cpp"
    "${EVE_ROOT}/src/engine/common/Json.cpp"
    "${EVE_ROOT}/src/modules/fluids/OceanSpectrumCodec.cpp"
    "${EVE_ROOT}/src/modules/fluids/OceanInteractionCodec.cpp"
    "${EVE_ROOT}/test/ocean_codec.cpp"
    "${EVE_ROOT}/src/modules/fluids/OceanMesh.cpp"
    "${EVE_ROOT}/src/modules/fluids/OceanMeshFrame.cpp"
    "${EVE_ROOT}/test/ocean_mesh.cpp"
    "${EVE_ROOT}/src/modules/fluids/OceanHullMask.cpp"
    "${EVE_ROOT}/test/ocean_hull_mask.cpp"
    "${EVE_ROOT}/src/modules/fluids/OceanSpray.cpp"
    "${EVE_ROOT}/src/modules/fluids/OceanSprayMesh.cpp"
    "${EVE_ROOT}/test/ocean_spray.cpp"
    "${EVE_ROOT}/src/modules/fluids/OceanSurface.cpp"
    "${EVE_ROOT}/src/modules/fluids/OceanSeaTransition.cpp"
    "${EVE_ROOT}/test/ocean_transition.cpp"
    "${EVE_ROOT}/src/modules/fluids/OceanSpectrum.cpp"
    "${EVE_ROOT}/src/modules/fluids/OceanSpectrumFrame.cpp"
    "${EVE_ROOT}/src/modules/fluids/OceanWake.cpp"
    "${EVE_ROOT}/src/modules/fluids/OceanWaterView.cpp"
    "${EVE_ROOT}/src/modules/fluids/OceanBoatWake.cpp"
    "${EVE_ROOT}/test/ocean_surface.cpp"
    "${EVE_ROOT}/test/ocean_spectrum.cpp"
    "${EVE_ROOT}/test/ocean_wake.cpp")
target_include_directories(ocean_core_check PRIVATE
    "${EVE_ROOT}/src/engine" "${EVE_ROOT}/src/modules")
target_include_directories(ocean_core_check SYSTEM PRIVATE
    "${EVE_ROOT}/third-party/glm" "${EVE_ROOT}/external/zeroerr/include")
target_link_libraries(ocean_core_check PRIVATE zeroerr)
if(MSVC)
    target_compile_options(ocean_core_check PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(ocean_core_check PRIVATE -Wall -Wextra -Werror)
endif()
enable_testing()
foreach(ocean_case displacedWorldQueryMatchesGeometry rejectsFoldedAndNonfiniteInputs buoyancyVolumeAndDrag
        spectrumEnergyPeriodicityAndSeed spectrumFftMatchesIndependentDft spectrumVelocityAndValidation
        spectrumMultibandCurrentAndBuilderLifetime wakePropagatesAndRejectsAtomically wakeRecenterAndComposition
        wakeDispersionMatchesIndependentDft foamTransportMatchesCurrentAndDecay
        foamTransportFailurePreservesWholeStep wakePreparedStepIsOwningAndRevisionChecked
        meshHasNoInteriorBoundary meshQualityAndValidation
        meshFloatingOriginMatchesPhysics spectrumDefinitionPreservesSeedAndWaves
        spectrumDefinitionRejectsAmbiguousInput interactionArgumentsAreStrictAndOwning sprayDeterminismAndSourceOrder
        sprayBudgetsLifetimeAndReentry sprayRejectsWholeInvalidBatch sprayMeshBasisAndFloatingOrigin
        hullMaskPoseBevelAndOrigin hullMaskRejectsWholeInvalidBatch
        seaTransitionVelocityAndEndpoints seaTransitionValidationAndOwningBuilder
        seaTransitionCurrentPreservesEndpointVelocityContract)
    add_test(NAME "fluids.ocean.${ocean_case}"
        COMMAND ocean_core_check "--testcase=^fluids.ocean.${ocean_case}$" --no-color)
    set_tests_properties("fluids.ocean.${ocean_case}" PROPERTIES
        PASS_REGULAR_EXPRESSION "TEST CASE: +1 +0 +0 +0"
        FAIL_REGULAR_EXPRESSION "Assertion Failed")
endforeach()
