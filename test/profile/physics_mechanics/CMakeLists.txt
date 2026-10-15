cmake_minimum_required(VERSION 3.22)
project(PhysicsMechanicsCheck LANGUAGES C CXX)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "This hostless fixture uses Linux section garbage collection; use the normal unit_test target on other hosts")
endif()
get_filename_component(EVE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
enable_testing()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_subdirectory("${EVE_ROOT}/external/zeroerr" zeroerr EXCLUDE_FROM_ALL)
set(CMAKE_CXX_STANDARD 20)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_INSTALL OFF CACHE BOOL "" FORCE)
set(DISABLE_DYNAMIC ON CACHE BOOL "" FORCE)
set(BOX3D_SAMPLES OFF CACHE BOOL "" FORCE)
set(BOX3D_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX3D_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(SQUIRREL_INCLUDE_DIR "${EVE_ROOT}/third-party/squirrel/include")
set(SQUIRREL_LIBRARIES squirrel_static)
set(SQSTDLIB_LIBRARIESRARIES sqstdlib_static)
add_subdirectory("${EVE_ROOT}/third-party/squirrel" squirrel EXCLUDE_FROM_ALL)
add_subdirectory("${EVE_ROOT}/third-party/simplesquirrel" simplesquirrel EXCLUDE_FROM_ALL)
add_subdirectory("${EVE_ROOT}/third-party/box3d" box3d EXCLUDE_FROM_ALL)
file(GLOB_RECURSE BOX2D_SOURCES "${EVE_ROOT}/third-party/Box2D/*.cpp")
add_library(force_box2d STATIC ${BOX2D_SOURCES})
target_include_directories(force_box2d PRIVATE "${EVE_ROOT}/third-party")
option(EVE_MECHANICS_WITH_JOLT "Exercise both rigid backends in the hostless integration fixture" ON)
if(EVE_MECHANICS_WITH_JOLT)
  include("${EVE_ROOT}/third-party/cmake/Jolt.cmake")
  eve_add_jolt()
  add_custom_target(physics_mechanics_jolt_patch
    COMMAND ${CMAKE_COMMAND}
      -DPATCH=${EVE_ROOT}/cmake/patches/jolt-solved-contacts-rolling.patch
      -DPATCH_DIR=${EVE_ROOT}/third-party/jolt
      -P ${EVE_ROOT}/cmake/patch_third_party.cmake)
  add_dependencies(Jolt physics_mechanics_jolt_patch)
  set_source_files_properties("${EVE_ROOT}/src/modules/physics/jolt/JoltDistanceConstraint.cpp" PROPERTIES COMPILE_OPTIONS -fno-rtti)
endif()
set(src
 test/physics_merge_contract.cpp
 src/engine/common/ModuleRegistry.cpp
 src/engine/common/Result.cpp src/engine/common/Status.cpp src/engine/common/Diagnostic.cpp
 src/modules/physics/WorldJoints2D.cpp
 src/modules/physics/Joint2D.cpp
 src/modules/physics/Mechanism2D.cpp
 src/modules/physics/World3DJoints.cpp
 src/modules/physics/World3DQueries.cpp
 src/modules/physics/World3DMover.cpp
 src/modules/physics/World3DContacts.cpp
 src/modules/physics/WorldSnapshotParsing.cpp
 src/modules/physics/snapshot/PortableWorldSnapshot.cpp
 src/modules/physics/box3d/Box3DShapeCook.cpp
 src/modules/physics/box3d/Box3DWorldJoints.cpp
 src/modules/physics/box3d/Box3DShape.cpp
 src/modules/physics/box3d/Box3DWorld.cpp
 src/modules/physics/box3d/Box3DWorldQueries.cpp
 src/modules/physics/box3d/Box3DWorldContacts.cpp
 src/modules/physics/box3d/Box3DBody.cpp
 src/modules/physics/box3d/Box3DWorldMover.cpp
 src/modules/physics/box3d/Box3DJoint.cpp
 test/physics_vehicle_tires3d.cpp
 src/modules/vehicle/VehicleTires3D.cpp src/modules/vehicle/VehicleTireRuntime.cpp
 src/modules/vehicle/VehicleSystem.cpp
 src/modules/vehicle/VehicleDefinitionRuntime.cpp
 test/physics_vehicle_tires.cpp
 src/modules/vehicle/VehiclePhysics.cpp src/modules/vehicle/VehicleTires2D.cpp
 test/physics_force_receipt.cpp
 test/physics_tire_stability.cpp
 test/physics_wing_assembly.cpp
 test/physics_buoyancy_geometry.cpp
 test/physics_field_set.cpp
 test/physics_snapshot3d_ownership.cpp
 test/physics_coupling3d_script.cpp src/modules/physics/mechanics/Coupling3DBindings.cpp
 test/physics_coupling3d.cpp
 test/physics_world_snapshot_topology.cpp
 src/modules/physics/snapshot/JointSnapshotCodec.cpp
 src/modules/physics/snapshot/Joint3DParameters.cpp
 src/modules/physics/snapshot/Coupling3DSnapshot.cpp
 src/modules/physics/snapshot/Shape3DContactSnapshot.cpp
 src/modules/physics/snapshot/PlanarMaterialSnapshot.cpp src/modules/physics/snapshot/WorldSnapshotCodec.cpp src/modules/physics/WorldSnapshot.cpp src/modules/physics/PhysicsLink.cpp
 src/engine/common/Snapshot.cpp src/engine/common/Identity.cpp
 test/physics_joint2d_script.cpp src/modules/physics/mechanics/Joint2DBindings.cpp src/engine/common/SquirrelOwnership.cpp
 test/physics_joint2d_snapshot.cpp test/physics_joint2d.cpp src/modules/physics/mechanics/Joint2D.cpp
 test/physics_force_models.cpp test/physics_force_application.cpp test/physics_force_script.cpp
 src/modules/physics/forces/ForceFields.cpp src/modules/physics/forces/Aerodynamics.cpp
 src/modules/physics/forces/ForceBatch.cpp
    src/modules/physics/forces/ForceReceipt.cpp
    src/modules/physics/World3DTopology.cpp
 src/modules/physics/forces/FieldSet.cpp
 src/modules/physics/forces/BuoyancyGeometry.cpp
 src/modules/physics/forces/WingAssembly.cpp
 src/modules/physics/forces/TireDynamics.cpp src/modules/physics/forces/ForceApplication.cpp src/modules/physics/forces/ForceBindings.cpp
 src/modules/physics/World.cpp src/modules/physics/World3D.cpp
    src/modules/physics/World3DSimulation.cpp
    src/modules/physics/mechanics/Coupling3D.cpp
    src/modules/physics/mechanics/Coupling3DStep.cpp src/modules/physics/Body.cpp src/modules/physics/Body3D.cpp
 src/modules/physics/Fixture.cpp src/modules/physics/Shape3D.cpp src/modules/physics/Joint3D.cpp
 src/modules/physics/PhysicsWorldFactory.cpp src/modules/sensing/Targeting.cpp
 src/modules/physics/PhysicsCapabilities.cpp src/modules/physics/TargetingLineOfSightAdapter.cpp src/modules/sensing/LineOfSightRouter.cpp
 src/modules/physics/PhysicsHandles.cpp src/modules/physics/backend/SimulationBackend.cpp
 src/engine/common/Module.cpp src/engine/common/Profile.cpp src/engine/common/Exception.cpp
 src/engine/common/RenderTrace.cpp src/engine/common/Capability.cpp src/engine/common/Time.cpp
 src/engine/common/SquirrelBinding.cpp src/engine/common/Value.cpp src/engine/common/Json.cpp)
if(EVE_MECHANICS_WITH_JOLT)
  file(GLOB jolt_sources RELATIVE "${EVE_ROOT}" "${EVE_ROOT}/src/modules/physics/jolt/*.cpp")
  list(APPEND src ${jolt_sources} test/physics_jolt.cpp test/physics_jolt_contacts.cpp test/physics_jolt_cooking.cpp)
endif()
list(TRANSFORM src PREPEND "${EVE_ROOT}/")
add_executable(force_integration ${src} NoPlatformEvent.cpp)
if(EVE_MECHANICS_WITH_JOLT)
  add_dependencies(force_integration physics_mechanics_jolt_patch)
  target_link_libraries(force_integration PRIVATE Jolt)
  target_compile_definitions(force_integration PRIVATE EVENGINE_HAS_JOLT=1 EVENGINE_TEST_HAS_JOLT=1)
endif()
add_dependencies(force_integration sq_static evescript_modules_test)
target_include_directories(force_integration PRIVATE "${EVE_ROOT}/src/modules" "${EVE_ROOT}/src/engine"
 "${EVE_ROOT}/third-party" "${EVE_ROOT}/third-party/glm" "${EVE_ROOT}/third-party/box3d/include" "${EVE_ROOT}/third-party/squirrel/include"
 "${EVE_ROOT}/third-party/simplesquirrel/include" "${EVE_ROOT}/external/ECS.hpp/src"
 "${EVE_ROOT}/external/zeroerr/include")
target_compile_options(force_integration PRIVATE -ffunction-sections -fdata-sections)
target_link_options(force_integration PRIVATE -Wl,--gc-sections)
target_link_libraries(force_integration PRIVATE simplesquirrel_static squirrel_static sqstdlib_static force_box2d box3d zeroerr pthread)

# Discover the compiled cases, including conditional provider-present/absent cases.
include("${EVE_ROOT}/cmake/ZeroErrDiscoverTests.cmake")
zeroerr_discover_tests(force_integration)
add_custom_command(TARGET force_integration POST_BUILD
  COMMAND ${CMAKE_COMMAND}
    -DTEST_FILE=${CMAKE_CURRENT_BINARY_DIR}/force_integration_zeroerr_tests.cmake
    -P ${CMAKE_CURRENT_SOURCE_DIR}/StrictResults.cmake)

# Link and run the real absent implementation without any physics object/library.
add_executable(vehicle_tires_absent "${EVE_ROOT}/src/engine/common/Diagnostic.cpp" "${EVE_ROOT}/src/engine/common/Result.cpp" "${EVE_ROOT}/src/engine/common/Status.cpp" VehicleAbsent.cpp "${EVE_ROOT}/src/modules/vehicle/VehiclePhysicsAbsent.cpp")
target_include_directories(vehicle_tires_absent PRIVATE "${EVE_ROOT}/src/modules" "${EVE_ROOT}/src/engine"
 "${EVE_ROOT}/external/ECS.hpp/src" "${EVE_ROOT}/external/zeroerr/include")
target_link_libraries(vehicle_tires_absent PRIVATE zeroerr pthread)
add_dependencies(force_integration vehicle_tires_absent)
add_test(NAME physics.vehicleTires.absentBackend COMMAND vehicle_tires_absent)
