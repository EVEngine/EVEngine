eve_module_enabled(physics _has_physics)
option(EVENGINE_BUILD_PHYSICS_CORE_CHECK
  "Compile the physics domain core without graphics/gpgpu presentation paths" OFF)
set(_eve_physics_core_profile FALSE)
if(EVENGINE_PROFILE STREQUAL "physics-core-only")
  set(_eve_physics_core_profile TRUE)
endif()
if(_has_physics OR _eve_physics_core_profile OR EVENGINE_BUILD_PHYSICS_CORE_CHECK)
  # Box2D headers use <Box2D/Common/...>; prefer source tree over flattened install.
  # Box3D (Erin Catto) is a compiled library linked by name from the manifest.
  if(_has_physics)
    target_include_directories(EVPhysics PUBLIC ${CMAKE_SOURCE_DIR}/third-party)
  endif()

  # World/Body/Shape/Joint/query/fixed-step are the physics domain boundary.
  # Keep its source list explicit so this check cannot accidentally pull in the
  # graphics debug bridge, ClothGPU, or another presentation/accelerator TU.
  set(_eve_physics_core_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/Joint2D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/WorldJoints2D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/Mechanism2D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/mechanics/Joint2D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/forces/ForceFields.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/forces/Aerodynamics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/forces/TireDynamics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/forces/ForceApplication.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/forces/ForceBatch.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/forces/ForceReceipt.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/World3DTopology.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/forces/FieldSet.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/forces/BuoyancyGeometry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/forces/WingAssembly.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/Body.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/Body3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/Fixture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/Joint3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/box3d/Box3DJoint.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/box3d/Box3DBody.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/box3d/Box3DShape.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/box3d/Box3DShapeCook.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/box3d/Box3DWorld.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/box3d/Box3DWorldJoints.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/box3d/Box3DWorldQueries.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/box3d/Box3DWorldMover.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/box3d/Box3DWorldContacts.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/PhysicsHandles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/PhysicsLink.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/PhysicsWorldQueries.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/TargetingLineOfSightAdapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/Shape3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/backend/SimulationBackend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/World.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/World3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/World3DJoints.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/WorldSphereSweep.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/World3DQueries.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/World3DMover.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/World3DContacts.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/World3DSimulation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/mechanics/Coupling3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/mechanics/Coupling3DStep.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/snapshot/JointSnapshotCodec.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/snapshot/Joint3DParameters.cpp
 ${CMAKE_CURRENT_SOURCE_DIR}/physics/snapshot/Coupling3DSnapshot.cpp
 ${CMAKE_CURRENT_SOURCE_DIR}/physics/snapshot/Shape3DContactSnapshot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/snapshot/PlanarMaterialSnapshot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/snapshot/PortableWorldSnapshot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/snapshot/WorldSnapshotCodec.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/WorldSnapshot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/WorldSnapshotParsing.cpp)
  if(TARGET Jolt)
    file(GLOB _eve_jolt_core_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/physics/jolt/*.cpp")
    list(APPEND _eve_physics_core_sources ${_eve_jolt_core_sources})
  endif()
  set(_eve_physics_core_boundary_files ${_eve_physics_core_sources}
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/Body.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/Body3D.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/Fixture.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/Joint3D.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/PhysicsHandles.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/PhysicsLink.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/Shape3D.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/backend/SimulationBackend.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/World.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/World3D.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/WorldSnapshotParsing.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/World3DInternal.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/backend/RigidTypes3D.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/backend/BodyBackend3D.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/backend/ShapeBackend3D.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/backend/JointBackend3D.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/rigid/WorldBackend3D.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/rigid/World3DTypes.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/box3d/Box3DBody.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/box3d/Box3DJoint.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/box3d/Box3DShape.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/box3d/Box3DShapeCook.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/box3d/Box3DWorld.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/box3d/Box3DWorldFactory.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/box3d/Box3DWorldInternal.h)
  foreach(_eve_physics_core_file IN LISTS _eve_physics_core_boundary_files)
    if(NOT EXISTS "${_eve_physics_core_file}")
      message(FATAL_ERROR "Physics core source is missing: ${_eve_physics_core_file}")
    endif()
    file(READ "${_eve_physics_core_file}" _eve_physics_core_content)
    if(_eve_physics_core_content MATCHES "include[ \t]*[\"<](graphics|gpgpu)/")
      message(FATAL_ERROR
        "Physics core source must not include graphics/gpgpu: ${_eve_physics_core_file}")
    endif()
  endforeach()
  if(EVENGINE_BUILD_PHYSICS_CORE_CHECK OR _eve_physics_core_profile)
    add_library(EVPhysicsCoreCheck OBJECT ${_eve_physics_core_sources})
    target_link_libraries(EVPhysicsCoreCheck PRIVATE eve_engine_includes)
    target_compile_definitions(EVPhysicsCoreCheck PRIVATE EVENGINE_HAS_JOLT=$<BOOL:${EVENGINE_BUILD_JOLT}>)
    if(TARGET Jolt)
      target_include_directories(EVPhysicsCoreCheck BEFORE PRIVATE ${CMAKE_SOURCE_DIR}/third-party/jolt)
      target_link_libraries(EVPhysicsCoreCheck PRIVATE Jolt::Jolt)
    endif()
    # This compile-only target defines World and Backends implementations; it
    # is not a DLL consumer. Match their defining-side annotations in SHARED
    # builds so MSVC does not diagnose definitions of dllimport declarations.
    if(EVENGINE_MODULE_LINKAGE STREQUAL "SHARED")
      target_compile_definitions(EVPhysicsCoreCheck PRIVATE
        EVENGINE_ENGINE_EXPORTS EVENGINE_EXPORTS_WORLD EVENGINE_EXPORTS_BACKENDS)
    endif()
    target_include_directories(EVPhysicsCoreCheck PRIVATE
      ${CMAKE_SOURCE_DIR}/third-party/glm
      ${CMAKE_SOURCE_DIR}/src/modules
      ${CMAKE_SOURCE_DIR}/third-party
      ${CMAKE_SOURCE_DIR}/third-party/box3d/include)
    set_target_properties(EVPhysicsCoreCheck PROPERTIES POSITION_INDEPENDENT_CODE ON)
  endif()
endif()

