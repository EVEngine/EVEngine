#pragma once
#include <memory>
#include "common/Result.h"
#include "physics/OwnedQuery3D.h"
#include "physics/backend/BodyBackend3D.h"
#include "physics/backend/JointBackend3D.h"
#include "physics/backend/SimulationBackend.h"
#include "physics/rigid/World3DTypes.h"
namespace eve::physics {
/** @brief Solver identity fixed for the lifetime of one world; independent of CPU/GPU execution. */
enum class RigidBackend3D { Box3D, Jolt };
class World3D;
class Body3D;
class Shape3D;
/**
 * @brief Complete rigid-world execution boundary, bound once to an owning World3D facade.
 * @ownership Owns exactly one native world; child backends own native children of that world.
 * The facade remains sole owner of engine identity, portable topology, query projections and event queues.
 * @thread Simulation thread only between steps. Native callbacks are internal predicates;
 * public events are collected first and published by the facade after returning from the solver.
 * @lifetime Owning query results survive later queries. Legacy query projections live in the facade.
 * @remarks Unsupported operations must return Unsupported before mutation; no hidden alternate solver.
 */
class IWorldBackend3D {
public:
    /** @brief Creates an owning Weld constraint; bodies are synchronous non-owning inputs.
     * @cost One native constraint allocation per topology addition; no callbacks.
     */
    [[nodiscard]] virtual eve::Result<std::unique_ptr<IJointBackend3D>> newWeldJoint(Body3D& bodyA, Body3D& bodyB,
                                                                                     float anchorX, float anchorY,
                                                                                     float anchorZ,
                                                                                     bool  collideConnected) = 0;
    /** @brief Creates an owning Motor constraint; bodies are synchronous non-owning inputs.
     * @cost One native constraint allocation per topology addition; no callbacks.
     */
    [[nodiscard]] virtual eve::Result<std::unique_ptr<IJointBackend3D>> newMotorJoint(Body3D& bodyA, Body3D& bodyB,
                                                                                      bool collideConnected) = 0;
    /** @brief Creates an owning Parallel constraint; bodies are synchronous non-owning inputs.
     * @cost One native constraint allocation per topology addition; no callbacks.
     */
    [[nodiscard]] virtual eve::Result<std::unique_ptr<IJointBackend3D>> newParallelJoint(Body3D& bodyA, Body3D& bodyB,
                                                                                         float axisX, float axisY,
                                                                                         float axisZ,
                                                                                         bool  collideConnected) = 0;
    /** @brief Creates an owning Filter constraint; bodies are synchronous non-owning inputs.
     * @cost One native constraint allocation per topology addition; no callbacks.
     */
    [[nodiscard]] virtual eve::Result<std::unique_ptr<IJointBackend3D>> newFilterJoint(Body3D& bodyA,
                                                                                       Body3D& bodyB) = 0;
    /** @brief Creates an owning Distance constraint; bodies are synchronous non-owning inputs.
     * @cost One native constraint allocation per topology addition; no callbacks.
     */
    [[nodiscard]] virtual eve::Result<std::unique_ptr<IJointBackend3D>> newDistanceJoint(
        Body3D& bodyA, Body3D& bodyB, float anchorAX, float anchorAY, float anchorAZ, float anchorBX, float anchorBY,
        float anchorBZ, float length, bool collideConnected) = 0;
    /** @brief Creates an owning Revolute constraint; bodies are synchronous non-owning inputs.
     * @cost One native constraint allocation per topology addition; no callbacks.
     */
    [[nodiscard]] virtual eve::Result<std::unique_ptr<IJointBackend3D>> newRevoluteJoint(Body3D& bodyA, Body3D& bodyB,
                                                                                         float anchorX, float anchorY,
                                                                                         float anchorZ, float axisX,
                                                                                         float axisY, float axisZ,
                                                                                         bool collideConnected) = 0;
    /** @brief Creates an owning Prismatic constraint; bodies are synchronous non-owning inputs.
     * @cost One native constraint allocation per topology addition; no callbacks.
     */
    [[nodiscard]] virtual eve::Result<std::unique_ptr<IJointBackend3D>> newPrismaticJoint(Body3D& bodyA, Body3D& bodyB,
                                                                                          float anchorX, float anchorY,
                                                                                          float anchorZ, float axisX,
                                                                                          float axisY, float axisZ,
                                                                                          bool collideConnected) = 0;
    /** @brief Creates an owning Spherical constraint; bodies are synchronous non-owning inputs.
     * @cost One native constraint allocation per topology addition; no callbacks.
     */
    [[nodiscard]] virtual eve::Result<std::unique_ptr<IJointBackend3D>> newSphericalJoint(Body3D& bodyA, Body3D& bodyB,
                                                                                          float anchorX, float anchorY,
                                                                                          float anchorZ, float axisX,
                                                                                          float axisY, float axisZ,
                                                                                          bool collideConnected) = 0;
    /** @brief Creates an owning Wheel constraint; bodies are synchronous non-owning inputs.
     * @cost One native constraint allocation per topology addition; no callbacks.
     */
    [[nodiscard]] virtual eve::Result<std::unique_ptr<IJointBackend3D>> newWheelJoint(
        Body3D& bodyA, Body3D& bodyB, float anchorX, float anchorY, float anchorZ, float suspensionAxisX,
        float suspensionAxisY, float suspensionAxisZ, float wheelAxisX, float wheelAxisY, float wheelAxisZ,
        bool collideConnected) = 0;
    virtual ~IWorldBackend3D() = default;
    /** @brief Native world liveness. */
    virtual bool isValid() const noexcept = 0;
    /** @brief Idempotent native teardown after all facades have been invalidated. */
    virtual void destroy() noexcept = 0;
    /** @brief Builds the separate CPU/accelerator step driver; driver borrows this world's stable state.
     * @return Owning selection with explicit accelerator absence/failure evidence.
     */
    [[nodiscard]] virtual eve::Result<SimulationBackendSelection> selectSimulation() = 0;
    /** @brief Swaps prepared native topology while retaining stable step-driver contexts.
     * @remarks Both worlds must have the same implementation; failure leaves both unchanged.
     */
    [[nodiscard]] virtual eve::Result<void> swapTopology(IWorldBackend3D& prepared) = 0;
    /** @brief Opaque native lookup key scoped to this world, used only by internal event history. */
    virtual uint64_t shapeKey(const Shape3D& shape) const = 0;
    /** @brief Collects solver events into the owner's queues without publishing callbacks. */
    [[nodiscard]] virtual eve::Result<void> collectEvents() = 0;
    /** @brief Creates an owning body backend without publishing an engine wrapper.
     * @cost One native body allocation per topology addition.
     */
    [[nodiscard]] virtual eve::Result<std::unique_ptr<IBodyBackend3D>> createBody(std::string_view type, float x,
                                                                                  float y, float z) = 0;
    /** @brief setContinuousCollisionEnabled; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setContinuousCollisionEnabled(bool enabled) = 0;
    /** @brief isContinuousCollisionEnabled; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<bool> isContinuousCollisionEnabled() const = 0;
    /** @brief setRestitutionThreshold; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setRestitutionThreshold(float speed) = 0;
    /** @brief getRestitutionThreshold; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getRestitutionThreshold() const = 0;
    /** @brief setContactTuning; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setContactTuning(float hertz, float dampingRatio, float pushOutSpeed) = 0;
    /** @brief setContactRecycleDistance; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setContactRecycleDistance(float distance) = 0;
    /** @brief getContactRecycleDistance; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getContactRecycleDistance() const = 0;
    /** @brief setMaximumLinearSpeed; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setMaximumLinearSpeed(float speed) = 0;
    /** @brief getMaximumLinearSpeed; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getMaximumLinearSpeed() const = 0;
    /** @brief setWarmStartingEnabled; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setWarmStartingEnabled(bool enabled) = 0;
    /** @brief isWarmStartingEnabled; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<bool> isWarmStartingEnabled() const = 0;
    /** @brief explode; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<int> explode(float x, float y, float z, float radius, float falloff,
                                                   float impulsePerArea, int maskBits) = 0;
    /** @brief getBoundsMinX; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getBoundsMinX() const = 0;
    /** @brief getBoundsMinY; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getBoundsMinY() const = 0;
    /** @brief getBoundsMinZ; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getBoundsMinZ() const = 0;
    /** @brief getBoundsMaxX; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getBoundsMaxX() const = 0;
    /** @brief getBoundsMaxY; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getBoundsMaxY() const = 0;
    /** @brief getBoundsMaxZ; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getBoundsMaxZ() const = 0;
    /** @brief getBodyCount; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<int> getBodyCount() const = 0;
    /** @brief getShapeCount; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<int> getShapeCount() const = 0;
    /** @brief getContactCount; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<int> getContactCount() const = 0;
    /** @brief getJointCount; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<int> getJointCount() const = 0;
    /** @brief getIslandCount; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<int> getIslandCount() const = 0;
    /** @brief getAwakeContactCount; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<int> getAwakeContactCount() const = 0;
    /** @brief getRecycledContactCount; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<int> getRecycledContactCount() const = 0;
    /** @brief getStaticTreeHeight; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<int> getStaticTreeHeight() const = 0;
    /** @brief getDynamicTreeHeight; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<int> getDynamicTreeHeight() const = 0;
    /** @brief getMemoryByteCount; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<int> getMemoryByteCount() const = 0;
    /** @brief getAwakeBodyCount; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<int> getAwakeBodyCount() const = 0;
    /** @brief getProfileStepMs; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getProfileStepMs() const = 0;
    /** @brief getProfilePairsMs; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getProfilePairsMs() const = 0;
    /** @brief getProfileCollideMs; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getProfileCollideMs() const = 0;
    /** @brief getProfileSolveMs; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getProfileSolveMs() const = 0;
    /** @brief getProfileBulletsMs; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getProfileBulletsMs() const = 0;
    /** @brief getProfileSensorsMs; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getProfileSensorsMs() const = 0;
    /** @brief setGravity; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setGravity(float gx, float gy, float gz) = 0;
    /** @brief getGravityX; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getGravityX() const = 0;
    /** @brief getGravityY; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getGravityY() const = 0;
    /** @brief getGravityZ; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getGravityZ() const = 0;
    /** @brief setHitEventThreshold; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setHitEventThreshold(float speed) = 0;
    /** @brief getHitEventThreshold; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getHitEventThreshold() const = 0;
    /** @brief sphereCast; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<CameraSphereHit3D> sphereCast(float x1, float y1, float z1, float x2, float y2,
                                                                    float z2, float radius, uint64_t maskBits,
                                                                    int ignoredBodyId) const = 0;
    /** @brief rayCastFiltered; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<int> rayCastFiltered(float x1, float y1, float z1, float x2, float y2, float z2,
                                                           uint64_t maskBits) = 0;
    /** @brief rayCastOwned; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<RayHit3D> rayCastOwned(float x1, float y1, float z1, float x2, float y2, float z2,
                                                             QueryFilter3D filter) = 0;
    /** @brief rayCastAll; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<int> rayCastAll(float x1, float y1, float z1, float x2, float y2, float z2,
                                                      int maxHits) = 0;
    /** @brief queryAABB; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<int> queryAABB(float minX, float minY, float minZ, float maxX, float maxY,
                                                     float maxZ) = 0;
    /** @brief queryAabbBroadPhaseOwned; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<BroadPhaseAabb3D> queryAabbBroadPhaseOwned(float minX, float minY, float minZ,
                                                                                 float maxX, float maxY, float maxZ,
                                                                                 QueryFilter3D filter) = 0;
    /** @brief querySphere; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<int> querySphere(float x, float y, float z, float radius) = 0;
    /** @brief queryCapsule; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<int> queryCapsule(float ax, float ay, float az, float bx, float by, float bz,
                                                        float radius) = 0;
    /** @brief queryCapsuleOwned; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<CapsuleOverlap3D> queryCapsuleOwned(float ax, float ay, float az, float bx,
                                                                          float by, float bz, float radius,
                                                                          QueryFilter3D filter) = 0;
    /** @brief queryBox; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<int> queryBox(float x, float y, float z, float width, float height, float depth,
                                                    float qx, float qy, float qz, float qw) = 0;
    /** @brief castSphereAll; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<int> castSphereAll(float x, float y, float z, float radius, float dx, float dy,
                                                         float dz, int maxHits) = 0;
    /** @brief castCapsuleAll; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<int> castCapsuleAll(float ax, float ay, float az, float bx, float by, float bz,
                                                          float radius, float dx, float dy, float dz, int maxHits) = 0;
    /** @brief castBoxAll; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<int> castBoxAll(float x, float y, float z, float width, float height, float depth,
                                                      float qx, float qy, float qz, float qw, float dx, float dy,
                                                      float dz, int maxHits) = 0;
    /** @brief closestPoint; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<int> closestPoint(float x, float y, float z, float maxDistance) = 0;
    /** @brief setMoverUp; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setMoverUp(float x, float y, float z) = 0;
    /** @brief moveCapsule; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<bool> moveCapsule(float ax, float ay, float az, float bx, float by, float bz,
                                                        float radius, float dx, float dy, float dz) = 0;
    /** @brief moveCapsuleOwned; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<CapsuleMove3D> moveCapsuleOwned(float ax, float ay, float az, float bx, float by,
                                                                      float bz, float radius, float dx, float dy,
                                                                      float dz, QueryFilter3D filter,
                                                                      CapsuleMovePolicy3D policy) = 0;
    /** @brief pointProbeFiltered; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<ClothContact3D> pointProbeFiltered(float x, float y, float z, float radius,
                                                                         uint64_t categoryBits,
                                                                         uint64_t maskBits) const = 0;
    /** @brief queryBodyContactsOwned; uses the class ownership, thread and failure contract. */
    [[nodiscard]] virtual eve::Result<BodyContacts3D> queryBodyContactsOwned(PhysicsBodyHandle body,
                                                                             int               maxPoints) const = 0;
};
}  // namespace eve::physics
