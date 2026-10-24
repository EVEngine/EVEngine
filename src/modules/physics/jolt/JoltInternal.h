#pragma once
// Jolt requires its platform header before any other native header.
#include <Jolt/Jolt.h>

// Provider implementation dependencies.
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/EmptyShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsStepListener.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <cmath>
#include <unordered_map>
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/jolt/JoltBody.h"
#include "physics/jolt/JoltShape.h"
namespace eve::physics::jolt {
template <class T>
eve::Result<T> rejected(const char* operation, eve::DiagnosticCode code = eve::DiagnosticCode::Unsupported) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::string("Jolt: ") + operation, "physics.jolt"));
}
inline eve::Result<void> success() { return eve::Result<void>::success(); }
inline bool   isFinite(float x, float y, float z) { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
struct Layers final : JPH::BroadPhaseLayerInterface, JPH::ObjectVsBroadPhaseLayerFilter, JPH::ObjectLayerPairFilter {
    JPH::uint            GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return JPH::BroadPhaseLayer(static_cast<JPH::uint8>(layer));
    }
    bool ShouldCollide(JPH::ObjectLayer a, JPH::BroadPhaseLayer b) const override {
        return a != 0 || b.GetValue() != 0;
    }
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override { return a != 0 || b != 0; }
};
struct ContactPair {
    int  bodyA, shapeA, tagA, bodyB, shapeB, tagB;
    bool sensorA, sensorB;
};
struct Listener;
struct SolvedPoint {
    PhysicsBodyHandle  bodyA, bodyB;
    PhysicsShapeHandle shapeA, shapeB;
    BodyContactPoint3D pointA, pointB;
    uint32_t           substep = 0;
};
struct SolvedHit {
    PhysicsShapeHandle shapeA, shapeB;
    ContactPair        pair;
    float              x, y, z, nx, ny, nz, speed, impulse;
};
struct JointState;
/** @brief Initializes the process-owned allocator and type factory before constructing any native members. */
void initializeRuntime();
// Jolt SIMD members require alignment padding on MSVC ARM64.
JPH_SUPPRESS_WARNING_PUSH
JPH_MSVC_SUPPRESS_WARNING(4324)
struct State {
    Layers                                                      layers;
    JPH::TempAllocatorMalloc                                    allocator;
    JPH::JobSystemSingleThreaded                                jobs{JPH::cMaxPhysicsJobs};
    JPH::PhysicsSystem                                          system;
    std::unique_ptr<Listener>                                   listener;
    std::unordered_map<JPH::uint32, std::shared_ptr<BodyState>> bodies;
    std::vector<std::weak_ptr<JointState>>                      joints;
    std::unordered_map<JPH::uint64, int>                        suppressedPairs;
    float                                                       inverseStep        = 0;
    float                                                       maximumLinearSpeed = 500.f, stepMilliseconds = 0.f;
    bool                                                        continuousCollision = true;
    float                                                       hitThreshold        = 1.f;
    bool                                                        alive = true, sleep = true, faulted = false;
    State(float, float, float, bool);
    ~State();
};
struct BodyState {
    std::weak_ptr<State>                     world;
    JPH::MassProperties                      mass;
    JPH::BodyID                              id;
    Body3D*                                  owner = nullptr;
    std::vector<std::shared_ptr<ShapeState>> shapes;
    bool                                     alive = true, active = true, attached = true;
    bool                                     bullet               = false;
    bool                                     customSleepThreshold = false, sleepEnabled = true;
    float                                    sleepThreshold = .03f, quietSeconds = 0.f;
    JPH::EAllowedDOFs                        dofs = JPH::EAllowedDOFs::All;
    bool                                     valid() const {
        auto w = world.lock();
        return alive && w && w->alive;
    }
    JPH::BodyInterface& api() const { return world.lock()->system.GetBodyInterface(); }
};
JPH_SUPPRESS_WARNING_POP
struct ShapeState {
    std::weak_ptr<BodyState>       body;
    Shape3D*                       owner = nullptr;
    JPH::RefConst<JPH::Shape>      native;
    ShapeGeometry3D                geometry;
    SurfaceMaterial3D              material;
    std::vector<SurfaceMaterial3D> meshMaterials;
    ShapeFilter3D                  filter;
    float                          density = 1.f;
    bool                           alive = true, sensor = false;
    bool                           valid() const {
        auto b = body.lock();
        return alive && b && b->valid();
    }
};
struct Listener final : JPH::ContactListener, JPH::PhysicsStepListener {
    void                     OnStep(const JPH::PhysicsStepListenerContext&) override;
    State*                   state = nullptr;  // Borrowed from owner; listener is destroyed before State.
    std::vector<ContactPair> begins, ends;
    std::unordered_map<JPH::SubShapeIDPair, ContactPair>              contacts;
    std::unordered_map<JPH::SubShapeIDPair, std::vector<SolvedPoint>> solved;
    std::unordered_map<uint64_t, SolvedHit>                           hits;
    std::unordered_map<uint64_t, float>                               normalTotals;
    uint32_t                                                          substep = 0;
    void OnContactSolved(const JPH::Body&, const JPH::SubShapeID&, const JPH::Body&, const JPH::SubShapeID&,
                         const JPH::ContactSolveResult&) override;
    JPH::ValidateResult OnContactValidate(const JPH::Body&, const JPH::Body&, JPH::RVec3Arg,
                                          const JPH::CollideShapeResult&) override;
    void                OnContactAdded(const JPH::Body&, const JPH::Body&, const JPH::ContactManifold&,
                                       JPH::ContactSettings&) override;
    void                OnContactPersisted(const JPH::Body&, const JPH::Body&, const JPH::ContactManifold&,
                                           JPH::ContactSettings&) override;
    void                OnContactRemoved(const JPH::SubShapeIDPair&) override;
};
[[nodiscard]] eve::Result<JPH::RefConst<JPH::Shape>> cook(const ShapeGeometry3D&, float density, JPH::uint64 userData);
[[nodiscard]] eve::Result<JPH::RefConst<JPH::Shape>> cookMesh(const ShapeGeometry3D&, JPH::uint64 userData);
[[nodiscard]] eve::Result<void>                      validateMaterial(SurfaceMaterial3D);
int                                                  triangleIndex(const JPH::Body&, const JPH::SubShapeID&);
SurfaceMaterial3D               contactMaterial(const JPH::Body&, const JPH::SubShapeID&, const ShapeState&);
[[nodiscard]] eve::Result<void> replaceShapes(BodyState&, const std::vector<std::shared_ptr<ShapeState>>&);
/** @brief Resolves a native child record for a synchronous callback or query.
 * @ownership Borrowed from the world's body record; never transferred or retained by the caller.
 * @lifetime Valid only while the native body is locked or on the idle simulation thread,
 * until the next shape or body mutation. Returns nullptr when no child record exists.
 */
inline ShapeState* child(const JPH::Body& body, const JPH::SubShapeID& id) {
    return reinterpret_cast<ShapeState*>(body.GetShape()->GetSubShapeUserData(id));
}
}  // namespace eve::physics::jolt
