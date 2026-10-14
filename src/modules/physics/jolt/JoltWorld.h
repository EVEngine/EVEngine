#pragma once
#include "physics/rigid/WorldBackend3D.h"
namespace eve::physics::jolt {
struct State;
struct BodyState;
struct ShapeState;
struct QueryVolume;
// Internal provider implementation; all operations inherit the neutral interface contract.
class WorldBackend final : public IWorldBackend3D {
public:
    /** @brief Reads built-in pair overrides and prepared one-way planes synchronously; no user callbacks or locks. */
    static bool isContactAllowed(const Shape3D&, const Shape3D&, double, double, double, float, float, float);
    /** @brief Reads explicit body/shape pair exclusions for solid and sensor contacts without callbacks. */
    static bool            isPairAllowed(const Shape3D&, const Shape3D&);
    PhysicsVector3D        moverUp{0, 1, 0};
    World3D*               owner;
    std::shared_ptr<State> state;
    /** @brief Internal synchronous projection of an ephemeral cooked query volume. */
    [[nodiscard]] eve::Result<int> overlapQuery(const QueryVolume&, QueryFilter3D);
    /** @brief Internal synchronous sweep; owning native data is projected into facade caches. */
    [[nodiscard]] eve::Result<int> castQuery(const QueryVolume&, float, float, float, int);
    WorldBackend(World3D&, float, float, float, bool);
    ~WorldBackend() override;
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
    bool                                          isValid() const noexcept override;
    void                                          destroy() noexcept override;
    eve::Result<SimulationBackendSelection>       selectSimulation() override;
    eve::Result<void>                             swapTopology(IWorldBackend3D& prepared) override;
    uint64_t                                      shapeKey(const Shape3D& shape) const override;
    eve::Result<void>                             collectEvents() override;
    eve::Result<std::unique_ptr<IBodyBackend3D>>  createBody(std::string_view type, float x, float y, float z) override;
    eve::Result<void>                             setContinuousCollisionEnabled(bool enabled) override;
    eve::Result<bool>                             isContinuousCollisionEnabled() const override;
    eve::Result<void>                             setRestitutionThreshold(float speed) override;
    eve::Result<float>                            getRestitutionThreshold() const override;
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
};
}  // namespace eve::physics::jolt
