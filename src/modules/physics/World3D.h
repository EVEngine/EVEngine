#pragma once

#include "common/Snapshot.h"
#include "physics/PhysicsHandles.h"
#include "physics/SimulationBackend.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <box3d/id.h>
#include <box3d/types.h>

namespace eve::physics {

class Body3D;
class Shape3D;
class Joint3D;
struct CameraSphereHit3D {
    bool  hit = false;
    int   bodyId = -1;
    float fraction = 1.f;
    float x = 0.f, y = 0.f, z = 0.f;
    float nx = 0.f, ny = 0.f, nz = 0.f;
};

/**
 * @brief Result of a 3D point probe: deepest non-sensor shape within radius.
 * Normal points from the shape toward the probed point (meters).
 */
struct ClothContact3D {
    bool   hit = false;
    float  nx = 0.f;
    float  ny = 0.f;
    float  nz = 0.f;
    float  depth = 0.f;  // radius - distance (meters); > 0 when inside
    Body3D *body = nullptr;
};

/**
 * @brief Box3D rigid-body world. Script coordinates are meters (Box3D native),
 * unlike 2D World which uses pixels + Physics.setMeter.
 */
class World3D {
public:
    /** @brief Stable snapshot describing one side of a 3D collision event. */
    struct EventShape {
        int bodyId = 0;
        int shapeId = 0;
        int shapeTag = 0;
    };

    /** @brief Begin/end event for two non-sensor shapes. */
    struct ContactEvent {
        EventShape shapeA;
        EventShape shapeB;
    };

    /** @brief Begin/end overlap event with an explicit sensor and visitor side. */
    struct TriggerEvent {
        EventShape sensor;
        EventShape visitor;
    };

    /** @brief Significant collision containing world-space feedback data. */
    struct HitEvent {
        EventShape shapeA;
        EventShape shapeB;
        float pointX = 0.f, pointY = 0.f, pointZ = 0.f;
        float normalX = 0.f, normalY = 0.f, normalZ = 0.f;
        float approachSpeed = 0.f;
        float normalImpulse = 0.f;
    };
    /** @brief Joint whose solver force or torque exceeded a configured threshold. */
    struct JointStressEvent {
        int jointId = 0;
        int bodyAId = 0;
        int bodyBId = 0;
        int kind = 0;
        float forceX = 0.f, forceY = 0.f, forceZ = 0.f;
        float torqueX = 0.f, torqueY = 0.f, torqueZ = 0.f;
    };
    World3D(float gravityX, float gravityY, float gravityZ, bool sleep);
    ~World3D();

    World3D(const World3D &)            = delete;
    World3D &operator=(const World3D &) = delete;

    /** @brief Steps the simulation by dt seconds (default substep count). */
    void update(float dt);
    /** @brief Steps with an explicit substep count. */
    void updateFull(float dt, int subStepCount);

    /**
     * @brief Advances the 3D domain with an injected deterministic step.
     * @param step Tick and fixed duration supplied by SimulationClock or replay.
     * @param settings Solver policy; subStepCount controls Box3D substeps.
     * @return Applied when the step completed, or a structured rejection/failure.
     * @remarks A rejected step leaves solver state and the current tick unchanged.
     */
    [[nodiscard("check the physics step outcome or explicitly ignore it")]]
    eve::Result<void> step(const eve::SimulationStep &step, const SimulationSettings &settings = {});

    /** @brief Snapshot of completed backend steps and logical simulation time. */
    [[nodiscard]] SimulationObservation simulationObservation() const noexcept;
    /** @brief Selected CPU/GPU/mock backend family. */
    [[nodiscard]] SimulationBackendKind backendKind() const noexcept;
    /** @brief Replay/numeric guarantee declared by the selected backend. */
    [[nodiscard]] SimulationDeterminism backendDeterminism() const noexcept;
    /** @brief Current deterministic tick; save data should persist this value. */
    [[nodiscard]] eve::SimulationTick simulationTick() const noexcept { return simulationTick_; }
    /** @brief Process-local identity used by PhysicsLink; invalid after destruction. */
    [[nodiscard]] PhysicsWorldHandle runtimeHandle() const noexcept { return runtimeHandle_; }
    /** @brief Whether optional accelerator selection fell back to CPU. */
    [[nodiscard]] bool usedBackendFallback() const noexcept { return backendFallback_; }
    /** @brief Selection status, including a structured absent-capability warning. */
    [[nodiscard("inspect backend selection diagnostics")]]
    eve::Status backendSelectionStatus() const;

    /**
     * @brief Captures a versioned, integrity-checked 3D world snapshot.
     * @param hashProvider Injected digest provider used to seal the envelope.
     * @return A snapshot containing the exact SimulationTick and body state.
     * @remarks The provider is not retained; this call is owner-thread-only.
     */
    [[nodiscard("check or persist the physics snapshot")]]
    eve::Result<eve::SnapshotEnvelope> snapshot(const eve::SnapshotHashProvider &hashProvider) const;

    /**
     * @brief Restores a verified snapshot without exposing partial state.
     * @param snapshot Versioned envelope produced for this world schema.
     * @param hashProvider Provider used to verify its content hash.
     * @return Applied when all body identities and tick metadata match.
     * @remarks The snapshot is borrowed for this call and runtime handles are
     *          never persisted or reused from its payload.
     */
    [[nodiscard("check the physics snapshot restore outcome")]]
    eve::Result<void> restore(const eve::SnapshotEnvelope &snapshot, const eve::SnapshotHashProvider &hashProvider);

    /** @brief Gravity in m/s². */
    void  setGravity(float gx, float gy, float gz);
    float getGravityX() const;
    float getGravityY() const;
    float getGravityZ() const;
    /** @brief Enables world-level continuous collision against static geometry. */
    void setContinuousCollisionEnabled(bool enabled);
    /** @brief Whether world-level continuous collision is enabled. */
    bool isContinuousCollisionEnabled() const;
    /** @brief Minimum impact speed that permits material restitution, in m/s. */
    void setRestitutionThreshold(float speed);
    /** @brief Current restitution speed threshold in m/s. */
    float getRestitutionThreshold() const;
    /** @brief Minimum approach speed in m/s required for enabled Shape3D hit events. */
    void setHitEventThreshold(float speed);
    /** @brief Current significant-hit speed threshold in m/s. */
    float getHitEventThreshold() const;
    /** @brief Overrides collision for a live body pair without changing layer masks. */
    void setBodyPairCollisionEnabled(Body3D *bodyA, Body3D *bodyB, bool enabled);
    /** @brief Whether a body pair is permitted by the runtime contact override. */
    bool isBodyPairCollisionEnabled(Body3D *bodyA, Body3D *bodyB) const;
    /** @brief Overrides collision for one live shape pair. */
    void setShapePairCollisionEnabled(Shape3D *shapeA, Shape3D *shapeB, bool enabled);
    /** @brief Whether a shape pair is permitted by the runtime contact override. */
    bool isShapePairCollisionEnabled(Shape3D *shapeA, Shape3D *shapeB) const;
    /**
     * @brief Configures contact overlap recovery.
     * @param hertz Positive contact stiffness frequency in cycles per second.
     * @param dampingRatio Non-negative damping ratio; 1 is critical damping.
     * @param pushOutSpeed Non-negative maximum overlap recovery speed in m/s.
     */
    void setContactTuning(float hertz, float dampingRatio, float pushOutSpeed);
    /** @brief Current contact stiffness frequency. */
    float getContactHertz() const { return contactHertz_; }
    /** @brief Current contact damping ratio. */
    float getContactDampingRatio() const { return contactDampingRatio_; }
    /** @brief Current maximum contact push-out speed in m/s. */
    float getContactPushOutSpeed() const { return contactPushOutSpeed_; }
    /** @brief Sets contact-point persistence distance in meters; zero disables recycling. */
    void setContactRecycleDistance(float distance);
    /** @brief Current contact-point persistence distance in meters. */
    float getContactRecycleDistance() const;
    /** @brief Sets the finite positive world-wide linear velocity clamp in m/s. */
    void setMaximumLinearSpeed(float speed);
    /** @brief Current world-wide maximum linear speed in m/s. */
    float getMaximumLinearSpeed() const;
    /** @brief Enables constraint warm starting for stable stacks and joints. */
    void setWarmStartingEnabled(bool enabled);
    /** @brief Whether constraint warm starting is enabled. */
    bool isWarmStartingEnabled() const;
    /**
     * @brief Applies a geometry-aware radial impulse to dynamic convex shapes.
     * @param x Explosion center X in world meters.
     * @param y Explosion center Y.
     * @param z Explosion center Z.
     * @param radius Full-strength radius, finite and non-negative.
     * @param falloff Additional distance over which impulse fades to zero.
     * @param impulsePerArea Signed impulse per projected square meter; negative implodes.
     * @param maskBits Collision categories accepted by the explosion (low 32 bits).
     * @return Number of bodies whose velocity changed; inspect getExplosionResult*.
     */
    int explode(float x, float y, float z, float radius, float falloff,
                float impulsePerArea, int maskBits = -1);
    /** @brief Number of bodies affected by the latest explosion. */
    int getExplosionResultCount() const { return static_cast<int>(explosionResults_.size()); }
    /** @brief Stable Body ID of an affected body, sorted ascending. */
    int getExplosionResultBodyId(int index) const;
    /** @brief Explosion-induced linear velocity delta X. */
    float getExplosionResultDeltaVelocityX(int index) const;
    /** @brief Explosion-induced linear velocity delta Y. */
    float getExplosionResultDeltaVelocityY(int index) const;
    /** @brief Explosion-induced linear velocity delta Z. */
    float getExplosionResultDeltaVelocityZ(int index) const;
    /** @brief Explosion-induced angular velocity delta X. */
    float getExplosionResultDeltaAngularVelocityX(int index) const;
    /** @brief Explosion-induced angular velocity delta Y. */
    float getExplosionResultDeltaAngularVelocityY(int index) const;
    /** @brief Explosion-induced angular velocity delta Z. */
    float getExplosionResultDeltaAngularVelocityZ(int index) const;
    /** @brief Minimum world-space X covered by current broad-phase bounds. */
    float getBoundsMinX() const;
    /** @brief Minimum world-space Y covered by current broad-phase bounds. */
    float getBoundsMinY() const;
    /** @brief Minimum world-space Z covered by current broad-phase bounds. */
    float getBoundsMinZ() const;
    /** @brief Maximum world-space X covered by current broad-phase bounds. */
    float getBoundsMaxX() const;
    /** @brief Maximum world-space Y covered by current broad-phase bounds. */
    float getBoundsMaxY() const;
    /** @brief Maximum world-space Z covered by current broad-phase bounds. */
    float getBoundsMaxZ() const;
    /** @brief Current backend body count. */
    int getBodyCount() const;
    /** @brief Current backend shape count. */
    int getShapeCount() const;
    /** @brief Current backend contact count. */
    int getContactCount() const;
    /** @brief Current backend joint count. */
    int getJointCount() const;
    /** @brief Current solver-island count. */
    int getIslandCount() const;
    /** @brief Number of awake bodies. */
    int getAwakeBodyCount() const;
    /** @brief Contacts processed by the latest collide pass. */
    int getAwakeContactCount() const;
    /** @brief Contact points recycled during the latest step. */
    int getRecycledContactCount() const;
    /** @brief Static broad-phase tree height. */
    int getStaticTreeHeight() const;
    /** @brief Dynamic broad-phase tree height. */
    int getDynamicTreeHeight() const;
    /** @brief Approximate Box3D world allocation in bytes. */
    int getMemoryByteCount() const;
    /** @brief Latest complete physics step time in milliseconds. */
    float getProfileStepMs() const;
    /** @brief Latest broad-phase pair generation time in milliseconds. */
    float getProfilePairsMs() const;
    /** @brief Latest narrow-phase collision time in milliseconds. */
    float getProfileCollideMs() const;
    /** @brief Latest constraint solver time in milliseconds. */
    float getProfileSolveMs() const;
    /** @brief Latest continuous-collision bullet time in milliseconds. */
    float getProfileBulletsMs() const;
    /** @brief Latest sensor processing time in milliseconds. */
    float getProfileSensorsMs() const;

    /**
     * @brief Creates a body in meter-space units.
     * @return Borrowed nullable body owned by this world; null means creation failed.
     * @ownership World3D owns the body and its shapes; callers must destroy it through this API.
     * @lifetime Valid until Body3D::destroy(), World3D::destroy(), or world teardown; use PhysicsBodyHandle across
     * frames.
     * @thread Call on the owning physics thread.
     * @reentrancy Creation does not invoke callbacks; do not re-enter structural world mutation while using the result.
     */
    Body3D *newBody(const std::string &bodyType, float x, float y, float z);
    /** @brief Resolves a live body handle; returns null for a stale or foreign handle. */
    [[nodiscard]] Body3D *findBody(PhysicsBodyHandle handle) const;
    /** @brief Resolves a live shape handle; returns null when stale or foreign. */
    [[nodiscard]] Shape3D *findShape(PhysicsShapeHandle handle) const;
    /** @brief Resolves a live joint handle; returns null when stale or foreign. */
    [[nodiscard]] Joint3D *findJoint(PhysicsJointHandle handle) const;
    /**
     * @brief Connects two world-space anchor points with a rigid distance constraint.
     * @return Borrowed nullable joint owned by this world; null means creation failed.
     * @ownership World3D owns the joint; body pointers are borrowed inputs and are never retained after validation.
     * @lifetime Valid until Joint3D::destroy(), World3D::destroy(), or dependent body destruction; use
     * PhysicsJointHandle across frames.
     * @thread Call on the owning physics thread.
     * @reentrancy Creation does not invoke callbacks; do not mutate either body re-entrantly.
     * @throws eve::Exception for invalid bodies, anchors, length, or cross-world bodies.
     */
    Joint3D *newDistanceJoint(Body3D *bodyA, Body3D *bodyB, float anchorAX, float anchorAY,
                              float anchorAZ, float anchorBX, float anchorBY, float anchorBZ,
                              float length, bool collideConnected = false);
    /**
     * @brief Creates a hinge around a normalized world-space axis at a shared anchor.
     * @return Borrowed nullable joint owned by this world; null means creation failed.
     * @ownership World3D owns the joint; body pointers are borrowed inputs and are not retained as caller ownership.
     * @lifetime Valid until Joint3D::destroy(), World3D::destroy(), or dependent body destruction; use
     * PhysicsJointHandle across frames.
     * @thread Call on the owning physics thread.
     * @reentrancy Creation does not invoke callbacks; do not mutate either body re-entrantly.
     * @throws eve::Exception for invalid/cross-world bodies or a zero/non-finite axis.
     */
    Joint3D *newRevoluteJoint(Body3D *bodyA, Body3D *bodyB, float anchorX, float anchorY,
                              float anchorZ, float axisX, float axisY, float axisZ,
                              bool collideConnected = false);
    /**
     * @brief Creates a slider whose permitted world-space translation follows axis.
     * @return Borrowed nullable joint owned by this world; null means creation failed.
     * @ownership World3D owns the joint; body pointers are borrowed inputs and are not retained as caller ownership.
     * @lifetime Valid until Joint3D::destroy(), World3D::destroy(), or dependent body destruction; use
     * PhysicsJointHandle across frames.
     * @thread Call on the owning physics thread.
     * @reentrancy Creation does not invoke callbacks; do not mutate either body re-entrantly.
     */
    Joint3D *newPrismaticJoint(Body3D *bodyA, Body3D *bodyB, float anchorX, float anchorY,
                               float anchorZ, float axisX, float axisY, float axisZ,
                               bool collideConnected = false);
    /**
     * @brief Creates a ball-and-socket joint with a world-space twist/cone axis.
     * @return Borrowed nullable joint owned by this world; null means creation failed.
     * @ownership World3D owns the joint; body pointers are borrowed inputs and are not retained as caller ownership.
     * @lifetime Valid until Joint3D::destroy(), World3D::destroy(), or dependent body destruction; use
     * PhysicsJointHandle across frames.
     * @thread Call on the owning physics thread.
     * @reentrancy Creation does not invoke callbacks; do not mutate either body re-entrantly.
     */
    Joint3D *newSphericalJoint(Body3D *bodyA, Body3D *bodyB, float anchorX, float anchorY,
                               float anchorZ, float axisX, float axisY, float axisZ,
                               bool collideConnected = false);
    /**
     * @brief Creates a vehicle wheel joint with independent world suspension and spin axes.
     * @return Borrowed nullable joint owned by this world; null means creation failed.
     * @ownership World3D owns the joint; body pointers are borrowed inputs and are not retained as caller ownership.
     * @lifetime Valid until Joint3D::destroy(), World3D::destroy(), or dependent body destruction; use
     * PhysicsJointHandle across frames.
     * @thread Call on the owning physics thread.
     * @reentrancy Creation does not invoke callbacks; do not mutate either body re-entrantly.
     */
    Joint3D *newWheelJoint(Body3D *bodyA, Body3D *bodyB, float anchorX, float anchorY,
                           float anchorZ, float suspensionAxisX, float suspensionAxisY,
                           float suspensionAxisZ, float wheelAxisX, float wheelAxisY,
                           float wheelAxisZ, bool collideConnected = false);

    /** @brief Destroys a body (null is ignored). */
    void destroyBody(Body3D *body);
    /** @brief Destroys the world and invalidates all wrappers. */
    void destroy();

    /**
     * @brief Closest raycast from (x1,y1,z1) to (x2,y2,z2) in meters.
     * Returns hit body id, or -1. Read hit details via getRayHit*.
     */
    int rayCast(float x1, float y1, float z1, float x2, float y2, float z2);
    /** @brief 带形状类别掩码的最短射线查询（掩码为接受的 shape categoryBits）。 */
    int rayCastFiltered(float x1, float y1, float z1, float x2, float y2, float z2,
                        uint64_t maskBits);
    /** @brief Internal camera query: swept sphere against non-sensor shapes. */
    bool sphereCast(float x1, float y1, float z1, float x2, float y2, float z2,
                    float radius, uint64_t maskBits, int ignoredBodyId,
                    CameraSphereHit3D* out) const;
    /**
     * @brief Collects the nearest maxHits ray intersections, sorted by fraction then Shape ID.
     * @return Number of cached intersections; read them with getRayResult*.
     */
    int rayCastAll(float x1, float y1, float z1, float x2, float y2, float z2, int maxHits);
    bool  hasRayHit() const { return rayHitBodyId_ >= 0; }
    int   getRayHitBodyId() const { return rayHitBodyId_; }
    /** @brief Stable shape id from the last ray cast, or -1. */
    int getRayHitShapeId() const { return rayHitShapeId_; }
    /** @brief User tag of the shape from the last ray cast, or zero on no hit. */
    int getRayHitShapeTag() const { return rayHitShapeTag_; }
    /** @brief Surface material ID at the last ray intersection, or zero. */
    int getRayHitMaterialId() const { return rayHitMaterialId_; }
    /** @brief Mesh/height-field triangle index from the last ray cast, otherwise -1. */
    int getRayHitTriangleIndex() const { return rayHitTriangleIndex_; }
    float getRayHitX() const { return rayHitX_; }
    float getRayHitY() const { return rayHitY_; }
    float getRayHitZ() const { return rayHitZ_; }
    float getRayHitNormalX() const { return rayHitNormalX_; }
    float getRayHitNormalY() const { return rayHitNormalY_; }
    float getRayHitNormalZ() const { return rayHitNormalZ_; }
    float getRayHitFraction() const { return rayHitFraction_; }
    /** @brief Number of results cached by the latest rayCast or rayCastAll call. */
    int getRayResultCount() const { return static_cast<int>(rayResults_.size()); }
    /** @brief Body ID of a sorted ray result. */
    int getRayResultBodyId(int index) const;
    /** @brief Shape ID of a sorted ray result. */
    int getRayResultShapeId(int index) const;
    /** @brief Shape Tag of a sorted ray result. */
    int getRayResultShapeTag(int index) const;
    /** @brief Surface material ID at a sorted ray intersection. */
    int getRayResultMaterialId(int index) const;
    /** @brief Mesh/height-field triangle index of a sorted ray result, otherwise -1. */
    int getRayResultTriangleIndex(int index) const;
    /** @brief World-space hit point X of a sorted ray result. */
    float getRayResultX(int index) const;
    /** @brief World-space hit point Y of a sorted ray result. */
    float getRayResultY(int index) const;
    /** @brief World-space hit point Z of a sorted ray result. */
    float getRayResultZ(int index) const;
    /** @brief Surface normal X of a sorted ray result. */
    float getRayResultNormalX(int index) const;
    /** @brief Surface normal Y of a sorted ray result. */
    float getRayResultNormalY(int index) const;
    /** @brief Surface normal Z of a sorted ray result. */
    float getRayResultNormalZ(int index) const;
    /** @brief Segment fraction in [0,1] of a sorted ray result. */
    float getRayResultFraction(int index) const;

    /**
     * @brief Query shapes overlapping an AABB in meters (min/max corners).
     * Returns match count; read ids with getQueryBodyId(i).
     */
    int queryAABB(float minX, float minY, float minZ, float maxX, float maxY, float maxZ);

    /**
     * @brief Query shapes overlapping a sphere in world meters.
     * @param x sphere center X
     * @param y sphere center Y
     * @param z sphere center Z
     * @param radius sphere radius, finite and non-negative
     * @return number of unique bodies hit
     */
    int querySphere(float x, float y, float z, float radius);

    /**
     * @brief Query shapes overlapping an arbitrarily oriented capsule.
     * The endpoints are the centers of its hemispherical caps.
     * @return number of unique bodies hit
     */
    int queryCapsule(float ax, float ay, float az, float bx, float by, float bz, float radius);

    /**
     * @brief Query shapes overlapping an oriented box.
     * @param width Full positive box width along its local X axis.
     * @param height Full positive box height along its local Y axis.
     * @param depth Full positive box depth along its local Z axis.
     * @param qx Quaternion X component; the quaternion is normalized internally.
     * @param qy Quaternion Y component.
     * @param qz Quaternion Z component.
     * @param qw Quaternion scalar component.
     * @return Number of unique bodies hit.
     */
    int queryBox(float x, float y, float z, float width, float height, float depth, float qx,
                 float qy, float qz, float qw);

    /**
     * @brief Sweep a sphere through the world and return the earliest hit body id, or -1.
     * @param dx world-space translation X
     * @param dy world-space translation Y
     * @param dz world-space translation Z
     */
    int castSphere(float x, float y, float z, float radius, float dx, float dy, float dz);
    /** @brief Collects the nearest maxHits swept-sphere intersections. */
    int castSphereAll(float x, float y, float z, float radius, float dx, float dy, float dz,
                      int maxHits);

    /**
     * @brief Sweep an arbitrarily oriented capsule through the world.
     * The endpoints are cap centers and d{x,y,z} is the world-space translation.
     * @return earliest hit body id, or -1
     */
    int castCapsule(float ax, float ay, float az, float bx, float by, float bz, float radius,
                    float dx, float dy, float dz);
    /** @brief Collects the nearest maxHits swept-capsule intersections. */
    int castCapsuleAll(float ax, float ay, float az, float bx, float by, float bz, float radius,
                       float dx, float dy, float dz, int maxHits);

    /**
     * @brief Sweep an oriented box without changing its rotation.
     * @return Earliest hit body id, or -1.
     */
    int castBox(float x, float y, float z, float width, float height, float depth, float qx,
                float qy, float qz, float qw, float dx, float dy, float dz);
    /** @brief Collects the nearest maxHits swept-oriented-box intersections. */
    int castBoxAll(float x, float y, float z, float width, float height, float depth, float qx,
                   float qy, float qz, float qw, float dx, float dy, float dz, int maxHits);

    /**
     * @brief Find the closest collision surface within maxDistance of a world point.
     * Uses the current query category/mask filter. The normal points from the surface toward the
     * target and is zero when the target lies on or inside the shape.
     * @return Closest stable body id, or -1 when no shape is within range.
     */
    int closestPoint(float x, float y, float z, float maxDistance);
    /** @brief Whether the latest closestPoint call found a shape. */
    bool hasClosestPoint() const { return closestBodyId_ >= 0; }
    /** @brief Stable body id from the latest closestPoint call, or -1. */
    int getClosestBodyId() const { return closestBodyId_; }
    /** @brief Stable shape id from the latest closestPoint call, or -1. */
    int getClosestShapeId() const { return closestShapeId_; }
    /** @brief User tag of the shape from the latest closestPoint call, or zero. */
    int getClosestShapeTag() const { return closestShapeTag_; }
    /** @brief Closest surface point X. */
    float getClosestX() const { return closestX_; }
    /** @brief Closest surface point Y. */
    float getClosestY() const { return closestY_; }
    /** @brief Closest surface point Z. */
    float getClosestZ() const { return closestZ_; }
    /** @brief Surface-to-target normal X, or zero for an inside/on-surface target. */
    float getClosestNormalX() const { return closestNormalX_; }
    /** @brief Surface-to-target normal Y, or zero for an inside/on-surface target. */
    float getClosestNormalY() const { return closestNormalY_; }
    /** @brief Surface-to-target normal Z, or zero for an inside/on-surface target. */
    float getClosestNormalZ() const { return closestNormalZ_; }
    /** @brief Non-negative distance from the target to the closest shape. */
    float getClosestDistance() const { return closestDistance_; }

    /** @brief Whether the last sphere, capsule, or box cast hit a body. */
    bool hasShapeCastHit() const { return shapeCastBodyId_ >= 0; }
    /** @brief Stable body id from the last shape cast, or -1. */
    int getShapeCastBodyId() const { return shapeCastBodyId_; }
    /** @brief Stable shape id from the last shape cast, or -1. */
    int getShapeCastShapeId() const { return shapeCastShapeId_; }
    /** @brief User tag of the shape from the last shape cast, or zero on no hit. */
    int getShapeCastShapeTag() const { return shapeCastShapeTag_; }
    /** @brief Surface material ID at the last shape-cast intersection, or zero. */
    int getShapeCastMaterialId() const { return shapeCastMaterialId_; }
    /** @brief Mesh/height-field triangle index from the latest shape cast, otherwise -1. */
    int getShapeCastTriangleIndex() const { return shapeCastTriangleIndex_; }
    /** @brief World-space contact point X from the last shape cast. */
    float getShapeCastX() const { return shapeCastX_; }
    /** @brief World-space contact point Y from the last shape cast. */
    float getShapeCastY() const { return shapeCastY_; }
    /** @brief World-space contact point Z from the last shape cast. */
    float getShapeCastZ() const { return shapeCastZ_; }
    /** @brief Contact normal X from the last shape cast. */
    float getShapeCastNormalX() const { return shapeCastNormalX_; }
    /** @brief Contact normal Y from the last shape cast. */
    float getShapeCastNormalY() const { return shapeCastNormalY_; }
    /** @brief Contact normal Z from the last shape cast. */
    float getShapeCastNormalZ() const { return shapeCastNormalZ_; }
    /** @brief Translation fraction in [0,1] at the earliest hit. */
    float getShapeCastFraction() const { return shapeCastFraction_; }
    /** @brief Number of results cached by the latest single or multi Shape Cast. */
    int getShapeCastResultCount() const { return static_cast<int>(shapeCastResults_.size()); }
    /** @brief Body ID of a sorted Shape Cast result. */
    int getShapeCastResultBodyId(int index) const;
    /** @brief Shape ID of a sorted Shape Cast result. */
    int getShapeCastResultShapeId(int index) const;
    /** @brief Shape Tag of a sorted Shape Cast result. */
    int getShapeCastResultShapeTag(int index) const;
    /** @brief Surface material ID at a sorted Shape Cast intersection. */
    int getShapeCastResultMaterialId(int index) const;
    /** @brief Mesh/height-field triangle index of a Shape Cast result, otherwise -1. */
    int getShapeCastResultTriangleIndex(int index) const;
    /** @brief World-space contact point X of a Shape Cast result. */
    float getShapeCastResultX(int index) const;
    /** @brief World-space contact point Y of a Shape Cast result. */
    float getShapeCastResultY(int index) const;
    /** @brief World-space contact point Z of a Shape Cast result. */
    float getShapeCastResultZ(int index) const;
    /** @brief Contact normal X of a Shape Cast result. */
    float getShapeCastResultNormalX(int index) const;
    /** @brief Contact normal Y of a Shape Cast result. */
    float getShapeCastResultNormalY(int index) const;
    /** @brief Contact normal Z of a Shape Cast result. */
    float getShapeCastResultNormalZ(int index) const;
    /** @brief Translation fraction in [0,1] of a Shape Cast result. */
    float getShapeCastResultFraction(int index) const;

    /**
     * @brief Move a capsule with continuous collision and multi-plane sliding.
     * Existing penetration is recovered before the desired translation is applied.
     * @return true when collision planes constrained the movement
     */
    bool moveCapsule(float ax, float ay, float az, float bx, float by, float bz, float radius,
                     float dx, float dy, float dz);
    /** @brief Resolved translation X from the last moveCapsule call. */
    float getMoverDeltaX() const { return moverDeltaX_; }
    /** @brief Resolved translation Y from the last moveCapsule call. */
    float getMoverDeltaY() const { return moverDeltaY_; }
    /** @brief Resolved translation Z from the last moveCapsule call. */
    float getMoverDeltaZ() const { return moverDeltaZ_; }
    /** @brief Representative blocking normal X from the last move. */
    float getMoverNormalX() const { return moverNormalX_; }
    /** @brief Representative blocking normal Y from the last move. */
    float getMoverNormalY() const { return moverNormalY_; }
    /** @brief Representative blocking normal Z from the last move. */
    float getMoverNormalZ() const { return moverNormalZ_; }
    /** @brief Number of collision planes considered during the last move. */
    int getMoverPlaneCount() const { return moverPlaneCount_; }
    /** @brief Number of outer solver passes used by the last move. */
    int getMoverIterations() const { return moverIterations_; }
    /** @brief Set the up direction used for grounded and slope classification. */
    void setMoverUp(float x, float y, float z);
    /** @brief Set maximum walkable slope in degrees, clamped to [0, 89.9]. */
    void setMoverSlopeLimit(float degrees);
    /** @brief True when the last move touched a walkable plane. */
    bool isMoverGrounded() const { return moverGrounded_; }
    /** @brief Largest dot product between mover up and a contact normal. */
    float getMoverGroundDot() const { return moverGroundDot_; }

    /** @brief Number of bodies hit by the last overlap query. */
    int getQueryCount() const { return static_cast<int>(queryBodyIds_.size()); }
    /** @brief Body id of the i-th query hit. */
    int getQueryBodyId(int index) const;
    /** @brief Number of individual shapes hit by the last overlap query. */
    int getQueryShapeCount() const { return static_cast<int>(queryShapes_.size()); }
    /** @brief Stable shape id of the i-th shape hit, sorted ascending. */
    int getQueryShapeId(int index) const;
    /** @brief User tag paired with the i-th shape hit. */
    int getQueryShapeTag(int index) const;

    /** @brief Number of non-sensor contacts that began during the latest step. */
    int getBeginContactCount() const { return static_cast<int>(beginContacts_.size()); }
    /** @brief Body ID on stable side A of a begin-contact event. */
    int getBeginContactBodyAId(int index) const;
    /** @brief Body ID on stable side B of a begin-contact event. */
    int getBeginContactBodyBId(int index) const;
    /** @brief Shape ID on stable side A of a begin-contact event. */
    int getBeginContactShapeAId(int index) const;
    /** @brief Shape ID on stable side B of a begin-contact event. */
    int getBeginContactShapeBId(int index) const;
    /** @brief Shape Tag on stable side A of a begin-contact event. */
    int getBeginContactShapeATag(int index) const;
    /** @brief Shape Tag on stable side B of a begin-contact event. */
    int getBeginContactShapeBTag(int index) const;
    /** @brief Number of non-sensor contacts that ended during the latest step. */
    int getEndContactCount() const { return static_cast<int>(endContacts_.size()); }
    /** @brief Body ID on stable side A of an end-contact event. */
    int getEndContactBodyAId(int index) const;
    /** @brief Body ID on stable side B of an end-contact event. */
    int getEndContactBodyBId(int index) const;
    /** @brief Shape ID on stable side A of an end-contact event. */
    int getEndContactShapeAId(int index) const;
    /** @brief Shape ID on stable side B of an end-contact event. */
    int getEndContactShapeBId(int index) const;
    /** @brief Shape Tag on stable side A of an end-contact event. */
    int getEndContactShapeATag(int index) const;
    /** @brief Shape Tag on stable side B of an end-contact event. */
    int getEndContactShapeBTag(int index) const;

    /** @brief Number of sensor overlaps that began during the latest step. */
    int getBeginTriggerCount() const { return static_cast<int>(beginTriggers_.size()); }
    /** @brief Sensor body ID of a begin-trigger event. */
    int getBeginTriggerSensorBodyId(int index) const;
    /** @brief Visitor body ID of a begin-trigger event. */
    int getBeginTriggerVisitorBodyId(int index) const;
    /** @brief Sensor shape ID of a begin-trigger event. */
    int getBeginTriggerSensorShapeId(int index) const;
    /** @brief Visitor shape ID of a begin-trigger event. */
    int getBeginTriggerVisitorShapeId(int index) const;
    /** @brief Sensor Shape Tag of a begin-trigger event. */
    int getBeginTriggerSensorShapeTag(int index) const;
    /** @brief Visitor Shape Tag of a begin-trigger event. */
    int getBeginTriggerVisitorShapeTag(int index) const;
    /** @brief Number of sensor overlaps that ended during the latest step. */
    int getEndTriggerCount() const { return static_cast<int>(endTriggers_.size()); }
    /** @brief Sensor body ID of an end-trigger event. */
    int getEndTriggerSensorBodyId(int index) const;
    /** @brief Visitor body ID of an end-trigger event. */
    int getEndTriggerVisitorBodyId(int index) const;
    /** @brief Sensor shape ID of an end-trigger event. */
    int getEndTriggerSensorShapeId(int index) const;
    /** @brief Visitor shape ID of an end-trigger event. */
    int getEndTriggerVisitorShapeId(int index) const;
    /** @brief Sensor Shape Tag of an end-trigger event. */
    int getEndTriggerSensorShapeTag(int index) const;
    /** @brief Visitor Shape Tag of an end-trigger event. */
    int getEndTriggerVisitorShapeTag(int index) const;
    /** @brief Clears all contact and trigger buffers before the next step. */
    void clearContactEvents();

    /**
     * @brief Caches current touching manifold points for one stable Body3D ID.
     * Results are oriented from the queried body toward the other body.
     * @param maxPoints Maximum cached points in [1,4096].
     * @return Number of cached points.
     */
    int queryBodyContacts(int bodyId, int maxPoints);
    /** @brief Number of points cached by queryBodyContacts. */
    int getContactPointCount() const { return static_cast<int>(contactPoints_.size()); }
    /** @brief Queried-body Shape ID of a contact point. */
    int getContactPointShapeId(int index) const;
    /** @brief Queried-body Shape Tag of a contact point. */
    int getContactPointShapeTag(int index) const;
    /** @brief Other Body ID of a contact point. */
    int getContactPointOtherBodyId(int index) const;
    /** @brief Other Shape ID of a contact point. */
    int getContactPointOtherShapeId(int index) const;
    /** @brief Other Shape Tag of a contact point. */
    int getContactPointOtherShapeTag(int index) const;
    /** @brief World-space contact point X on the queried body. */
    float getContactPointX(int index) const;
    /** @brief World-space contact point Y on the queried body. */
    float getContactPointY(int index) const;
    /** @brief World-space contact point Z on the queried body. */
    float getContactPointZ(int index) const;
    /** @brief Contact normal X from queried body toward the other body. */
    float getContactPointNormalX(int index) const;
    /** @brief Contact normal Y from queried body toward the other body. */
    float getContactPointNormalY(int index) const;
    /** @brief Contact normal Z from queried body toward the other body. */
    float getContactPointNormalZ(int index) const;
    /** @brief Signed surface separation; negative values indicate penetration. */
    float getContactPointSeparation(int index) const;
    /** @brief Normal impulse from the final physics substep. */
    float getContactPointNormalImpulse(int index) const;
    /** @brief Total normal impulse accumulated across all physics substeps. */
    float getContactPointTotalNormalImpulse(int index) const;
    /** @brief Pre-solve relative normal velocity; negative means approaching. */
    float getContactPointNormalVelocity(int index) const;
    /** @brief Whether this manifold point existed in the previous step. */
    bool isContactPointPersisted(int index) const;

    /** @brief Number of significant collision hits generated during the latest step. */
    int getHitCount() const { return static_cast<int>(hits_.size()); }
    /** @brief Stable body ID on side A of a hit. */
    int getHitBodyAId(int index) const;
    /** @brief Stable body ID on side B of a hit. */
    int getHitBodyBId(int index) const;
    /** @brief Stable Shape ID on side A of a hit. */
    int getHitShapeAId(int index) const;
    /** @brief Stable Shape ID on side B of a hit. */
    int getHitShapeBId(int index) const;
    /** @brief Shape Tag on side A of a hit. */
    int getHitShapeATag(int index) const;
    /** @brief Shape Tag on side B of a hit. */
    int getHitShapeBTag(int index) const;
    /** @brief World-space impact point X. */
    float getHitPointX(int index) const;
    /** @brief World-space impact point Y. */
    float getHitPointY(int index) const;
    /** @brief World-space impact point Z. */
    float getHitPointZ(int index) const;
    /** @brief Hit normal X, pointing from stable side A to B. */
    float getHitNormalX(int index) const;
    /** @brief Hit normal Y, pointing from stable side A to B. */
    float getHitNormalY(int index) const;
    /** @brief Hit normal Z, pointing from stable side A to B. */
    float getHitNormalZ(int index) const;
    /** @brief Positive pre-solve approach speed in m/s. */
    float getHitApproachSpeed(int index) const;
    /** @brief Total normal impulse accumulated across all manifolds and substeps. */
    float getHitNormalImpulse(int index) const;
    /** @brief Number of joints exceeding force/torque thresholds in the latest step. */
    int getJointStressCount() const { return static_cast<int>(jointStressEvents_.size()); }
    /** @brief Stable Joint3D ID of a stress event. */
    int getJointStressJointId(int index) const;
    /** @brief Stable body A ID of a stress event. */
    int getJointStressBodyAId(int index) const;
    /** @brief Stable body B ID of a stress event. */
    int getJointStressBodyBId(int index) const;
    /** @brief Joint kind code: 0 distance, 1 revolute, 2 prismatic, 3 spherical, 4 wheel. */
    int getJointStressKind(int index) const;
    /** @brief Constraint force X of a stress event. */
    float getJointStressForceX(int index) const;
    /** @brief Constraint force Y of a stress event. */
    float getJointStressForceY(int index) const;
    /** @brief Constraint force Z of a stress event. */
    float getJointStressForceZ(int index) const;
    /** @brief Constraint torque X of a stress event. */
    float getJointStressTorqueX(int index) const;
    /** @brief Constraint torque Y of a stress event. */
    float getJointStressTorqueY(int index) const;
    /** @brief Constraint torque Z of a stress event. */
    float getJointStressTorqueZ(int index) const;
    /** @brief Set category/mask bits for subsequent ray, overlap, cast and mover queries. */
    void setQueryFilter(int categoryBits, int maskBits);
    /** @brief Restore the query filter to accept all lower-32-bit categories. */
    void resetQueryFilter();
    /** @brief Query category bits currently in use. */
    int getQueryCategoryBits() const { return static_cast<int>(queryCategoryBits_); }
    /** @brief Query mask bits currently in use. */
    int getQueryMaskBits() const { return static_cast<int>(queryMaskBits_); }
    /** @brief Ignores one stable Body3D ID in subsequent spatial queries; -1 disables. */
    void setQueryIgnoredBodyId(int bodyId);
    /** @brief Ignores one stable Shape3D ID in subsequent spatial queries; -1 disables. */
    void setQueryIgnoredShapeId(int shapeId);
    /** @brief Stable Body3D ID currently ignored by queries, or -1. */
    int getQueryIgnoredBodyId() const { return queryIgnoredBodyId_; }
    /** @brief Stable Shape3D ID currently ignored by queries, or -1. */
    int getQueryIgnoredShapeId() const { return queryIgnoredShapeId_; }
    /** @brief Clears Body and Shape query exclusions without changing layer filtering. */
    void clearQueryIgnores();

    /** @brief True while the underlying Box3D world is alive. */
    bool      isValid() const;

    /**
     * @brief Probe the deepest non-sensor shape within `radius` of a meter-space point.
     * Returns false when nothing is hit. Used by Cloth3D for particle-vs-body collision.
     */
    bool pointProbe(float x, float y, float z, float radius, ClothContact3D *out) const;
    /** @brief Raw Box3D world id. */
    b3WorldId raw() const { return worldId_; }

    /** @brief Internal: wrapper teardown bookkeeping. */
    void forgetBody(Body3D *body);
    void forgetShape(Shape3D *shape);
    /** @brief Internal: removes a joint wrapper from ownership bookkeeping. */
    void forgetJoint(Joint3D *joint);
    /** @brief Internal: snapshots a backend handle for destruction-safe end events. */
    void registerShapeHandle(Shape3D *shape);
    /** @brief Internal: refreshes the tag snapshot for a live shape handle. */
    void updateShapeTag(Shape3D *shape);

    /** @brief Internal: next stable body id. */
    int nextBodyId();
    /** @brief Internal: next generation-qualified body handle. */
    PhysicsBodyHandle nextBodyRuntimeHandle();
    /** @brief Internal: next generation-qualified shape handle. */
    PhysicsShapeHandle nextShapeRuntimeHandle();
    /** @brief Internal: next generation-qualified joint handle. */
    PhysicsJointHandle nextJointRuntimeHandle();
    /** @brief Internal: next stable shape id. */
    int nextShapeId() { return nextShapeId_++; }
    /** @brief Internal: next stable joint id. */
    int nextJointId() { return nextJointId_++; }

    /** @brief Internal: collects Box3D contact events into the event buffers. */
    void emitContactEvents();

private:
    friend class Body3D;
    friend class Joint3D;
    friend class Shape3D;
    friend class TargetingLineOfSightAdapter;
    friend void registerCameraObstructionWorld(World3D *world);

    b3WorldId worldId_{};
    std::unique_ptr<ISimulationBackend> simulation_;
    PhysicsWorldHandle                  runtimeHandle_          = PhysicsWorldHandle::invalid();
    std::shared_ptr<const void>         queryLifetime_          = std::make_shared<int>(0);
    bool      destroyed_ = false;
    int       nextId_    = 1;
    int       nextShapeId_ = 1;
    int       nextJointId_ = 1;
    std::uint32_t                       nextBodyHandleIndex_    = 1u;
    std::uint32_t                       nextShapeHandleIndex_   = 1u;
    std::uint32_t                       nextJointHandleIndex_   = 1u;
    eve::SimulationTick                 simulationTick_         = eve::SimulationTick::zero();
    eve::Status                         backendSelectionStatus_ = eve::Status::success();
    bool                                backendFallback_        = false;

    std::unordered_set<Body3D *>  bodies_;
    std::unordered_set<Shape3D *> shapes_;
    std::unordered_set<Joint3D *> joints_;
    std::unordered_set<uint64_t> disabledBodyPairs_;
    std::unordered_set<uint64_t> disabledShapePairs_;
    std::unordered_map<uint64_t, EventShape> shapeRecords_;
    std::unordered_map<PhysicsShapeHandle, Shape3D *> shapeHandles_;
    std::unordered_map<uint64_t, PhysicsShapeHandle>  shapeRawHandles_;
    std::unordered_map<PhysicsJointHandle, Joint3D *> jointHandles_;
    std::vector<ContactEvent> beginContacts_;
    std::vector<ContactEvent> endContacts_;
    std::vector<TriggerEvent> beginTriggers_;
    std::vector<TriggerEvent> endTriggers_;
    std::vector<HitEvent> hits_;
    std::vector<JointStressEvent> jointStressEvents_;

    struct ContactPointResult {
        int shapeId = -1;
        int shapeTag = 0;
        int otherBodyId = -1;
        int otherShapeId = -1;
        int otherShapeTag = 0;
        uint32_t featureId = 0;
        float x = 0.f, y = 0.f, z = 0.f;
        float normalX = 0.f, normalY = 0.f, normalZ = 0.f;
        float separation = 0.f;
        float normalImpulse = 0.f;
        float totalNormalImpulse = 0.f;
        float normalVelocity = 0.f;
        bool persisted = false;
    };
    std::vector<ContactPointResult> contactPoints_;
    const ContactPointResult &contactPointAt(int index, const char *operation) const;
    static bool preSolveCallback(b3ShapeId shapeIdA, b3ShapeId shapeIdB, b3Pos point,
                                 b3Vec3 normal, void *context);
    static bool customFilterCallback(b3ShapeId shapeIdA, b3ShapeId shapeIdB,
                                     void *context);
    void refreshContactFilter(Shape3D *shape);
    void removeCollisionOverridesForBody(int bodyId);
    void removeCollisionOverridesForShape(int shapeId);

    EventShape eventShapeFrom(b3ShapeId shapeId) const;
    const ContactEvent &contactEventAt(const std::vector<ContactEvent> &events, int index,
                                       const char *operation) const;
    const TriggerEvent &triggerEventAt(const std::vector<TriggerEvent> &events, int index,
                                       const char *operation) const;
    const HitEvent &hitEventAt(int index, const char *operation) const;
    const JointStressEvent &jointStressEventAt(int index, const char *operation) const;

    int   rayHitBodyId_   = -1;
    int   rayHitShapeId_  = -1;
    int   rayHitShapeTag_ = 0;
    int   rayHitMaterialId_ = 0;
    int   rayHitTriangleIndex_ = -1;
    float rayHitX_        = 0.f;
    float rayHitY_        = 0.f;
    float rayHitZ_        = 0.f;
    float rayHitNormalX_  = 0.f;
    float rayHitNormalY_  = 0.f;
    float rayHitNormalZ_  = 0.f;
    float rayHitFraction_ = 0.f;

    struct RayResult {
        int bodyId = -1;
        int shapeId = -1;
        int shapeTag = 0;
        int materialId = 0;
        int triangleIndex = -1;
        float x = 0.f, y = 0.f, z = 0.f;
        float normalX = 0.f, normalY = 0.f, normalZ = 0.f;
        float fraction = 0.f;
    };
    std::vector<RayResult> rayResults_;
    const RayResult &rayResultAt(int index, const char *operation) const;

    std::vector<int> queryBodyIds_;
    std::vector<std::pair<int, int>> queryShapes_;

    int overlapProxy(b3Pos origin, const b3ShapeProxy &proxy);
    int castProxyAll(b3Pos origin, const b3ShapeProxy &proxy, b3Vec3 translation, int maxHits);
    int rayCastAllInternal(float x1, float y1, float z1, float x2, float y2, float z2,
                           int maxHits, b3QueryFilter filter);
    b3QueryFilter makeQueryFilter() const;
    bool shouldIgnoreQueryShape(b3ShapeId shapeId) const;

    int shapeCastBodyId_ = -1;
    int shapeCastShapeId_ = -1;
    int shapeCastShapeTag_ = 0;
    int shapeCastMaterialId_ = 0;
    int shapeCastTriangleIndex_ = -1;
    float shapeCastX_ = 0.f, shapeCastY_ = 0.f, shapeCastZ_ = 0.f;
    float shapeCastNormalX_ = 0.f, shapeCastNormalY_ = 0.f, shapeCastNormalZ_ = 0.f;
    float shapeCastFraction_ = 0.f;
    struct ShapeCastResult {
        int bodyId = -1;
        int shapeId = -1;
        int shapeTag = 0;
        int materialId = 0;
        int triangleIndex = -1;
        float x = 0.f, y = 0.f, z = 0.f;
        float normalX = 0.f, normalY = 0.f, normalZ = 0.f;
        float fraction = 0.f;
    };
    std::vector<ShapeCastResult> shapeCastResults_;
    const ShapeCastResult &shapeCastResultAt(int index, const char *operation) const;

    int closestBodyId_ = -1;
    int closestShapeId_ = -1;
    int closestShapeTag_ = 0;
    float closestX_ = 0.f, closestY_ = 0.f, closestZ_ = 0.f;
    float closestNormalX_ = 0.f, closestNormalY_ = 0.f, closestNormalZ_ = 0.f;
    float closestDistance_ = 0.f;

    float moverDeltaX_ = 0.f, moverDeltaY_ = 0.f, moverDeltaZ_ = 0.f;
    float moverNormalX_ = 0.f, moverNormalY_ = 0.f, moverNormalZ_ = 0.f;
    int moverPlaneCount_ = 0;
    int moverIterations_ = 0;
    b3Vec3 moverUp_ = b3Vec3{0.f, 1.f, 0.f};
    float moverSlopeCos_ = 0.6427876f;
    float moverGroundDot_ = -1.f;
    bool moverGrounded_ = false;

    float contactHertz_ = 30.f;
    float contactDampingRatio_ = 10.f;
    float contactPushOutSpeed_ = 3.f;

    struct ExplosionResult {
        int bodyId = -1;
        float deltaVX = 0.f, deltaVY = 0.f, deltaVZ = 0.f;
        float deltaWX = 0.f, deltaWY = 0.f, deltaWZ = 0.f;
    };
    std::vector<ExplosionResult> explosionResults_;
    const ExplosionResult &explosionResultAt(int index, const char *operation) const;

    uint32_t queryCategoryBits_ = 0xFFFFFFFFu;
    uint32_t queryMaskBits_ = 0xFFFFFFFFu;
    int queryIgnoredBodyId_ = -1;
    int queryIgnoredShapeId_ = -1;
};

}  // namespace eve::physics
