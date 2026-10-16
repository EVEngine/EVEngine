#pragma once
#include <box3d/box3d.h>
#include "common/Exception.h"
#include "physics/World3D.h"
#include "physics/rigid/WorldBackend3D.h"
namespace eve::physics::box3d {
class WorldBackend final : public IWorldBackend3D {
    friend struct Access;
    template <class F>
    auto checked(const char* path, F&& operation) const -> decltype(operation()) {
        using R = decltype(operation());
        try {
            return operation();
        } catch (const eve::Exception& error) {
            return R::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, error.what(), path));
        }
    }

public:
    WorldBackend(World3D& owner, float gx, float gy, float gz, bool sleep);
    eve::Result<std::unique_ptr<IJointBackend3D>> newWeldJoint(Body3D& bodyA, Body3D& bodyB, float anchorX,
                                                               float anchorY, float anchorZ,
                                                               bool collideConnected) override;
    eve::Result<std::unique_ptr<IJointBackend3D>> newMotorJoint(Body3D& bodyA, Body3D& bodyB,
                                                                bool collideConnected) override;
    eve::Result<std::unique_ptr<IJointBackend3D>> newParallelJoint(Body3D& bodyA, Body3D& bodyB, float axisX,
                                                                   float axisY, float axisZ,
                                                                   bool collideConnected) override;
    eve::Result<std::unique_ptr<IJointBackend3D>> newFilterJoint(Body3D& bodyA, Body3D& bodyB) override;
    eve::Result<std::unique_ptr<IJointBackend3D>> newDistanceJoint(Body3D& bodyA, Body3D& bodyB, float anchorAX,
                                                                   float anchorAY, float anchorAZ, float anchorBX,
                                                                   float anchorBY, float anchorBZ, float length,
                                                                   bool collideConnected) override;
    eve::Result<std::unique_ptr<IJointBackend3D>> newRevoluteJoint(Body3D& bodyA, Body3D& bodyB, float anchorX,
                                                                   float anchorY, float anchorZ, float axisX,
                                                                   float axisY, float axisZ,
                                                                   bool collideConnected) override;
    eve::Result<std::unique_ptr<IJointBackend3D>> newPrismaticJoint(Body3D& bodyA, Body3D& bodyB, float anchorX,
                                                                    float anchorY, float anchorZ, float axisX,
                                                                    float axisY, float axisZ,
                                                                    bool collideConnected) override;
    eve::Result<std::unique_ptr<IJointBackend3D>> newSphericalJoint(Body3D& bodyA, Body3D& bodyB, float anchorX,
                                                                    float anchorY, float anchorZ, float axisX,
                                                                    float axisY, float axisZ,
                                                                    bool collideConnected) override;
    eve::Result<std::unique_ptr<IJointBackend3D>> newWheelJoint(Body3D& bodyA, Body3D& bodyB, float anchorX,
                                                                float anchorY, float anchorZ, float suspensionAxisX,
                                                                float suspensionAxisY, float suspensionAxisZ,
                                                                float wheelAxisX, float wheelAxisY, float wheelAxisZ,
                                                                bool collideConnected) override;
    ~WorldBackend() override;
    bool                                         isValid() const noexcept override;
    void                                         destroy() noexcept override;
    eve::Result<SimulationBackendSelection>      selectSimulation() override;
    eve::Result<void>                            swapTopology(IWorldBackend3D& prepared) override;
    uint64_t                                     shapeKey(const Shape3D& shape) const override;
    eve::Result<void>                            collectEvents() override;
    eve::Result<std::unique_ptr<IBodyBackend3D>> createBody(std::string_view type, float x, float y, float z) override;
    eve::Result<void>                            setContinuousCollisionEnabled(bool enabled) override;
    eve::Result<bool>                            isContinuousCollisionEnabled() const override;
    eve::Result<void>                            setRestitutionThreshold(float speed) override;
    eve::Result<float>                           getRestitutionThreshold() const override;
    eve::Result<void>              setContactTuning(float hertz, float dampingRatio, float pushOutSpeed) override;
    eve::Result<void>              setContactRecycleDistance(float distance) override;
    eve::Result<float>             getContactRecycleDistance() const override;
    eve::Result<void>              setMaximumLinearSpeed(float speed) override;
    eve::Result<float>             getMaximumLinearSpeed() const override;
    eve::Result<void>              setWarmStartingEnabled(bool enabled) override;
    eve::Result<bool>              isWarmStartingEnabled() const override;
    eve::Result<int>               explode(float x, float y, float z, float radius, float falloff, float impulsePerArea,
                                           int maskBits) override;
    eve::Result<float>             getBoundsMinX() const override;
    eve::Result<float>             getBoundsMinY() const override;
    eve::Result<float>             getBoundsMinZ() const override;
    eve::Result<float>             getBoundsMaxX() const override;
    eve::Result<float>             getBoundsMaxY() const override;
    eve::Result<float>             getBoundsMaxZ() const override;
    eve::Result<int>               getBodyCount() const override;
    eve::Result<int>               getShapeCount() const override;
    eve::Result<int>               getContactCount() const override;
    eve::Result<int>               getJointCount() const override;
    eve::Result<int>               getIslandCount() const override;
    eve::Result<int>               getAwakeContactCount() const override;
    eve::Result<int>               getRecycledContactCount() const override;
    eve::Result<int>               getStaticTreeHeight() const override;
    eve::Result<int>               getDynamicTreeHeight() const override;
    eve::Result<int>               getMemoryByteCount() const override;
    eve::Result<int>               getAwakeBodyCount() const override;
    eve::Result<float>             getProfileStepMs() const override;
    eve::Result<float>             getProfilePairsMs() const override;
    eve::Result<float>             getProfileCollideMs() const override;
    eve::Result<float>             getProfileSolveMs() const override;
    eve::Result<float>             getProfileBulletsMs() const override;
    eve::Result<float>             getProfileSensorsMs() const override;
    eve::Result<void>              setGravity(float gx, float gy, float gz) override;
    eve::Result<float>             getGravityX() const override;
    eve::Result<float>             getGravityY() const override;
    eve::Result<float>             getGravityZ() const override;
    eve::Result<void>              setHitEventThreshold(float speed) override;
    eve::Result<float>             getHitEventThreshold() const override;
    eve::Result<CameraSphereHit3D> sphereCast(float x1, float y1, float z1, float x2, float y2, float z2, float radius,
                                              uint64_t maskBits, int ignoredBodyId) const override;
    eve::Result<int>               rayCastFiltered(float x1, float y1, float z1, float x2, float y2, float z2,
                                                   uint64_t maskBits) override;
    eve::Result<RayHit3D>          rayCastOwned(float x1, float y1, float z1, float x2, float y2, float z2,
                                                QueryFilter3D filter) override;
    eve::Result<int> rayCastAll(float x1, float y1, float z1, float x2, float y2, float z2, int maxHits) override;
    eve::Result<int> queryAABB(float minX, float minY, float minZ, float maxX, float maxY, float maxZ) override;
    eve::Result<BroadPhaseAabb3D> queryAabbBroadPhaseOwned(float minX, float minY, float minZ, float maxX, float maxY,
                                                           float maxZ, QueryFilter3D filter) override;
    eve::Result<int>              querySphere(float x, float y, float z, float radius) override;
    eve::Result<int> queryCapsule(float ax, float ay, float az, float bx, float by, float bz, float radius) override;
    eve::Result<CapsuleOverlap3D> queryCapsuleOwned(float ax, float ay, float az, float bx, float by, float bz,
                                                    float radius, QueryFilter3D filter) override;
    eve::Result<int>  queryBox(float x, float y, float z, float width, float height, float depth, float qx, float qy,
                               float qz, float qw) override;
    eve::Result<int>  castSphereAll(float x, float y, float z, float radius, float dx, float dy, float dz,
                                    int maxHits) override;
    eve::Result<int>  castCapsuleAll(float ax, float ay, float az, float bx, float by, float bz, float radius, float dx,
                                     float dy, float dz, int maxHits) override;
    eve::Result<int>  castBoxAll(float x, float y, float z, float width, float height, float depth, float qx, float qy,
                                 float qz, float qw, float dx, float dy, float dz, int maxHits) override;
    eve::Result<int>  closestPoint(float x, float y, float z, float maxDistance) override;
    eve::Result<void> setMoverUp(float x, float y, float z) override;
    eve::Result<bool> moveCapsule(float ax, float ay, float az, float bx, float by, float bz, float radius, float dx,
                                  float dy, float dz) override;
    eve::Result<CapsuleMove3D>  moveCapsuleOwned(float ax, float ay, float az, float bx, float by, float bz,
                                                 float radius, float dx, float dy, float dz, QueryFilter3D filter,
                                                 CapsuleMovePolicy3D policy) override;
    eve::Result<ClothContact3D> pointProbeFiltered(float x, float y, float z, float radius, uint64_t categoryBits,
                                                   uint64_t maskBits) const override;
    eve::Result<BodyContacts3D> queryBodyContactsOwned(PhysicsBodyHandle body, int maxPoints) const override;

private:
    using EventShape         = World3D::EventShape;
    using RayResult          = World3D::RayResult;
    using ShapeCastResult    = World3D::ShapeCastResult;
    using ExplosionResult    = World3D::ExplosionResult;
    using ContactPointResult = BodyContactPoint3D;
    World3D*    owner_;
    b3WorldId   worldId_{};
    b3Vec3      moverUp_{0.f, 1.f, 0.f};
    void        do_setContinuousCollisionEnabled(bool enabled);
    bool        do_isContinuousCollisionEnabled() const;
    void        do_setRestitutionThreshold(float speed);
    float       do_getRestitutionThreshold() const;
    void        do_setContactTuning(float hertz, float dampingRatio, float pushOutSpeed);
    void        do_setContactRecycleDistance(float distance);
    float       do_getContactRecycleDistance() const;
    void        do_setMaximumLinearSpeed(float speed);
    float       do_getMaximumLinearSpeed() const;
    void        do_setWarmStartingEnabled(bool enabled);
    bool        do_isWarmStartingEnabled() const;
    int         do_explode(float x, float y, float z, float radius, float falloff, float impulsePerArea, int maskBits);
    float       do_getBoundsMinX() const;
    float       do_getBoundsMinY() const;
    float       do_getBoundsMinZ() const;
    float       do_getBoundsMaxX() const;
    float       do_getBoundsMaxY() const;
    float       do_getBoundsMaxZ() const;
    int         do_getBodyCount() const;
    int         do_getShapeCount() const;
    int         do_getContactCount() const;
    int         do_getJointCount() const;
    int         do_getIslandCount() const;
    int         do_getAwakeContactCount() const;
    int         do_getRecycledContactCount() const;
    int         do_getStaticTreeHeight() const;
    int         do_getDynamicTreeHeight() const;
    int         do_getMemoryByteCount() const;
    int         do_getAwakeBodyCount() const;
    float       do_getProfileStepMs() const;
    float       do_getProfilePairsMs() const;
    float       do_getProfileCollideMs() const;
    float       do_getProfileSolveMs() const;
    float       do_getProfileBulletsMs() const;
    float       do_getProfileSensorsMs() const;
    void        do_setGravity(float gx, float gy, float gz);
    float       do_getGravityX() const;
    float       do_getGravityY() const;
    float       do_getGravityZ() const;
    void        do_setHitEventThreshold(float speed);
    float       do_getHitEventThreshold() const;
    static bool do_customFilterCallback(b3ShapeId shapeIdA, b3ShapeId shapeIdB, void* context);
    b3QueryFilter do_makeQueryFilter() const;
    bool          do_shouldIgnoreQueryShape(b3ShapeId shapeId) const;
    static bool do_preSolveCallback(b3ShapeId shapeIdA, b3ShapeId shapeIdB, b3Pos point, b3Vec3 normal, void* context);
    bool do_sphereCast(float x1, float y1, float z1, float x2, float y2, float z2, float radius, uint64_t maskBits,
                       int ignoredBodyId, CameraSphereHit3D* out) const;
    int  do_rayCastFiltered(float x1, float y1, float z1, float x2, float y2, float z2, uint64_t maskBits);
    eve::Result<RayHit3D> do_rayCastOwned(float x1, float y1, float z1, float x2, float y2, float z2,
                                          QueryFilter3D filter);
    int                   do_rayCastAll(float x1, float y1, float z1, float x2, float y2, float z2, int maxHits);
    int                   do_rayCastAllInternal(float x1, float y1, float z1, float x2, float y2, float z2, int maxHits,
                                                b3QueryFilter filter);
    int                   do_queryAABB(float minX, float minY, float minZ, float maxX, float maxY, float maxZ);
    eve::Result<BroadPhaseAabb3D> do_queryAabbBroadPhaseOwned(float minX, float minY, float minZ, float maxX,
                                                              float maxY, float maxZ, QueryFilter3D filter);
    int                           do_overlapProxy(b3Pos origin, const b3ShapeProxy& proxy);
    int                           do_querySphere(float x, float y, float z, float radius);
    int do_queryCapsule(float ax, float ay, float az, float bx, float by, float bz, float radius);
    eve::Result<CapsuleOverlap3D> do_queryCapsuleOwned(float ax, float ay, float az, float bx, float by, float bz,
                                                       float radius, QueryFilter3D filter);
    int  do_queryBox(float x, float y, float z, float width, float height, float depth, float qx, float qy, float qz,
                     float qw);
    int  do_castProxyAll(b3Pos origin, const b3ShapeProxy& proxy, b3Vec3 translation, int maxHits);
    int  do_castSphereAll(float x, float y, float z, float radius, float dx, float dy, float dz, int maxHits);
    int  do_castCapsuleAll(float ax, float ay, float az, float bx, float by, float bz, float radius, float dx, float dy,
                           float dz, int maxHits);
    int  do_castBoxAll(float x, float y, float z, float width, float height, float depth, float qx, float qy, float qz,
                       float qw, float dx, float dy, float dz, int maxHits);
    int  do_closestPoint(float x, float y, float z, float maxDistance);
    void do_setMoverUp(float x, float y, float z);
    bool do_moveCapsule(float ax, float ay, float az, float bx, float by, float bz, float radius, float dx, float dy,
                        float dz);
    eve::Result<CapsuleMove3D>  do_moveCapsuleOwned(float ax, float ay, float az, float bx, float by, float bz,
                                                    float radius, float dx, float dy, float dz, QueryFilter3D filter,
                                                    CapsuleMovePolicy3D policy);
    ClothProbeStatus            do_pointProbeFiltered(float x, float y, float z, float radius, ClothContact3D* out,
                                                      uint64_t categoryBits, uint64_t maskBits) const;
    EventShape                  do_eventShapeFrom(b3ShapeId shapeId) const;
    eve::Result<BodyContacts3D> do_queryBodyContactsOwned(PhysicsBodyHandle body, int maxPoints) const;
};
}  // namespace eve::physics::box3d
