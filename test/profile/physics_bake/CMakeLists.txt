cmake_minimum_required(VERSION 3.22)
project(PhysicsBakePlaybackCheck LANGUAGES C CXX)
get_filename_component(EVE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(CMAKE_CXX_STANDARD 20)
enable_testing()
find_package(Threads REQUIRED)
add_subdirectory("${EVE_ROOT}/external/zeroerr" zeroerr EXCLUDE_FROM_ALL)
# Deliberately no solver, graphics, script binding, live-world capture or section GC:
# this link proves the production codec/sampler can execute without those providers.
add_library(physics_bake_runtime STATIC
  "${EVE_ROOT}/src/engine/common/Diagnostic.cpp"
  "${EVE_ROOT}/src/engine/common/Status.cpp"
  "${EVE_ROOT}/src/engine/common/Result.cpp"
  "${EVE_ROOT}/src/modules/physics/bake/BakedAssetFile.cpp"
  "${EVE_ROOT}/src/modules/physics/bake/BakedParticlePlayback.cpp"
  "${EVE_ROOT}/src/modules/physics/bake/ParticleTrack.cpp"
  "${EVE_ROOT}/src/modules/physics/bake/ParticleTrackFile.cpp"
  "${EVE_ROOT}/src/modules/physics/bake/DeformTopology.cpp"
  "${EVE_ROOT}/src/modules/physics/bake/DeformTrack.cpp"
  "${EVE_ROOT}/src/modules/physics/bake/DeformRecording.cpp"
  "${EVE_ROOT}/src/modules/physics/bake/DeformPlayback.cpp"
  "${EVE_ROOT}/src/modules/physics/bake/DeformTrackFile.cpp"
  "${EVE_ROOT}/src/modules/physics/bake/RigidPoseTrack.cpp"
  "${EVE_ROOT}/src/modules/physics/bake/RigidRecording.cpp"
  "${EVE_ROOT}/src/modules/physics/bake/RigidPoseTrackFile.cpp"
  "${EVE_ROOT}/src/modules/physics/bake/BakePlaybackClock.cpp"
  "${EVE_ROOT}/src/modules/physics/bake/BakeRigidTransform.cpp"
  "${EVE_ROOT}/src/modules/physics/bake/RigidPlayback.cpp"
  "${EVE_ROOT}/src/modules/physics/bake/collapse/CollapseAnimation.cpp"
  "${EVE_ROOT}/src/modules/physics/bake/collapse/CollapseAnimationCapture.cpp"
  "${EVE_ROOT}/src/modules/physics/bake/collapse/CollapsePlayback.cpp"
  "${EVE_ROOT}/src/modules/physics/bake/BakeRecipe.cpp"
  "${EVE_ROOT}/src/modules/physics/bake/collapse/FractureGeometry.cpp")
add_executable(physics_bake_playback "${EVE_ROOT}/test/integration/house-collapse/physics_bake_storage.cpp"
  "${EVE_ROOT}/test/integration/physics-bake/track_storage.cpp"
  "${EVE_ROOT}/test/unit/physics_bake/physics_bake_values.cpp"
  "${EVE_ROOT}/test/unit/physics_bake_collapse/physics_bake_collapse_values.cpp")
add_executable(physics_bake_inspect inspect.cpp)
add_executable(physics_bake_measure measure.cpp)
add_executable(physics_bake_allocation allocation.cpp)
target_link_libraries(physics_bake_allocation PRIVATE physics_bake_runtime)
add_test(NAME physics.bake.allocationFailure COMMAND physics_bake_allocation)
set_tests_properties(physics.bake.allocationFailure PROPERTIES TIMEOUT 30)
add_executable(physics_bake_collapse_error collapse_error.cpp)
target_link_libraries(physics_bake_collapse_error PRIVATE physics_bake_runtime)
target_include_directories(physics_bake_runtime PUBLIC "${EVE_ROOT}/src/engine" "${EVE_ROOT}/src/modules")
target_link_libraries(physics_bake_runtime PUBLIC zeroerr)
target_link_libraries(physics_bake_inspect PRIVATE physics_bake_runtime)
target_link_libraries(physics_bake_measure PRIVATE physics_bake_runtime)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # Link-time injection exercises the production IO path without a runtime test switch.
  add_executable(physics_bake_durability durability.cpp)
  target_link_libraries(physics_bake_durability PRIVATE physics_bake_runtime)
  target_link_options(physics_bake_durability PRIVATE -Wl,--wrap=fsync -Wl,--wrap=write -Wl,--wrap=openat)
  target_compile_options(physics_bake_durability PRIVATE -Wall -Wextra -Werror)
  add_test(NAME physics.bake.durabilityBarriers COMMAND physics_bake_durability)
  set_tests_properties(physics.bake.durabilityBarriers PROPERTIES
    LABELS "integration;no-render;reason:platform-file;requires:filesystem")
endif()
target_include_directories(physics_bake_playback PRIVATE "${EVE_ROOT}/src/engine" "${EVE_ROOT}/src/modules")
target_link_libraries(physics_bake_playback PRIVATE physics_bake_runtime Threads::Threads)
foreach(target physics_bake_runtime physics_bake_playback physics_bake_inspect physics_bake_measure physics_bake_collapse_error physics_bake_allocation)
  if(MSVC)
    # Match the engine warning policy for STL members in exported classes.
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /wd4251 /wd4275)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
  endif()
endforeach()
include("${EVE_ROOT}/cmake/ZeroErrDiscoverTests.cmake")
zeroerr_discover_tests(physics_bake_playback)
add_custom_command(TARGET physics_bake_playback POST_BUILD
  COMMAND ${CMAKE_COMMAND}
    -DTEST_FILE=${CMAKE_CURRENT_BINARY_DIR}/physics_bake_playback_zeroerr_tests.cmake
    -P ${CMAKE_CURRENT_SOURCE_DIR}/../physics_mechanics/StrictResults.cmake)
