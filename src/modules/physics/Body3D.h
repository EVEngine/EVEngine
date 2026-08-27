#pragma once

#include "physics/PhysicsHandles.h"

#include <string>
#include <vector>
#include <cstdint>

#include <box3d/id.h>

namespace eve::physics {

class World3D;
class Shape3D;

/**
 * @brief 3D rigid body (Box3D) in meter-space coordinates (+Y up by convention).
 * Owned by a World3D; create primitive or convex-hull shapes with the new*Shape APIs.
 */
class Body3D {
public:
    /** @brief Internal: wraps a Box3D body (use World3D::newBody). */
    Body3D(World3D *world, b3BodyId bodyId, int id, PhysicsBodyHandle runtimeHandle);
    ~Body3D();

    Body3D(const Body3D &)            = delete;
    Body3D &operator=(const Body3D &) = delete;

    /** @brief Stable id used by collision events. */
    int getId() const { return id_; }

    /** @brief Position in meters. */
    void  setPosition(float x, float y, float z);
    float getX() const;
    float getY() const;
    float getZ() const;

    /** @brief Orientation as quaternion (x, y, z, w). */
    void  setRotation(float qx, float qy, float qz, float qw);
    float getRotX() const;
    float getRotY() const;
    float getRotZ() const;
    float getRotW() const;

    /** @brief Linear velocity in m/s. */
    void  setLinearVelocity(float vx, float vy, float vz);
    float getLinearVelocityX() const;
    float getLinearVelocityY() const;
    float getLinearVelocityZ() const;

    /** @brief Body mass in kg. */
    float getMass() const;

    /** @brief Angular velocity in rad/s. */
    void  setAngularVelocity(float wx, float wy, float wz);
    float getAngularVelocityX() const;
    float getAngularVelocityY() const;
    float getAngularVelocityZ() const;

    /** @brief Converts a body-local point to world coordinates; returns {x,y,z}. */
    std::vector<float> localToWorldPoint(float x, float y, float z) const;
    /** @brief Converts a world point to body-local coordinates; returns {x,y,z}. */
    std::vector<float> worldToLocalPoint(float x, float y, float z) const;
    /** @brief Rotates a body-local direction/vector into world space; returns {x,y,z}. */
    std::vector<float> localToWorldVector(float x, float y, float z) const;
    /** @brief Rotates a world direction/vector into body-local space; returns {x,y,z}. */
    std::vector<float> worldToLocalVector(float x, float y, float z) const;
    /** @brief World-space velocity at a point expressed in body-local coordinates. */
    std::vector<float> getLocalPointVelocity(float x, float y, float z) const;
    /** @brief World-space velocity at the supplied world point. */
    std::vector<float> getWorldPointVelocity(float x, float y, float z) const;

    /** @brief Force applied at the center of mass. */
    void applyForce(float fx, float fy, float fz);
    /** @brief Force applied at a world position. */
    void applyForceAt(float fx, float fy, float fz, float x, float y, float z);
    /** @brief Torque applied in world space. */
    void applyTorque(float tx, float ty, float tz);
    /** @brief Instantaneous linear impulse. */
    void applyLinearImpulse(float ix, float iy, float iz);
    /** @brief Instantaneous linear impulse applied at a world position. */
    void applyLinearImpulseAt(float ix, float iy, float iz, float x, float y, float z);
    /** @brief Instantaneous angular impulse. */
    void applyAngularImpulse(float ix, float iy, float iz);
    /** @brief Body-local force applied at a body-local point. */
    void applyLocalForce(float fx, float fy, float fz, float x, float y, float z);
    /** @brief Body-local force applied at the center of mass. */
    void applyLocalForceToCenter(float fx, float fy, float fz);
    /** @brief Body-local torque. */
    void applyLocalTorque(float tx, float ty, float tz);
    /** @brief Body-local linear impulse applied at a body-local point. */
    void applyLocalLinearImpulse(float ix, float iy, float iz, float x, float y, float z);
    /** @brief Body-local linear impulse applied at the center of mass. */
    void applyLocalLinearImpulseToCenter(float ix, float iy, float iz);
    /** @brief Body-local angular impulse. */
    void applyLocalAngularImpulse(float ix, float iy, float iz);

    /**
     * @brief Drives a kinematic body to a target pose over a positive time step.
     * The target quaternion is normalized internally.
     */
    void setTargetTransform(float x, float y, float z, float qx, float qy, float qz, float qw,
                            float timeStep);

    /** @brief Linear damping coefficient, finite and non-negative. */
    void  setLinearDamping(float damping);
    /** @brief Current linear damping coefficient. */
    float getLinearDamping() const;
    /** @brief Angular damping coefficient, finite and non-negative. */
    void  setAngularDamping(float damping);
    /** @brief Current angular damping coefficient. */
    float getAngularDamping() const;
    /** @brief Multiplier applied to world gravity; may be negative. */
    void  setGravityScale(float scale);
    /** @brief Current world-gravity multiplier. */
    float getGravityScale() const;

    /** @brief Enables or disables automatic sleeping for this body. */
    void setSleepEnabled(bool enabled);
    /** @brief Whether automatic sleeping is enabled. */
    bool isSleepEnabled() const;
    /** @brief Sleep velocity threshold, finite and non-negative. */
    void  setSleepThreshold(float threshold);
    /** @brief Current sleep velocity threshold. */
    float getSleepThreshold() const;

    /**
     * @brief Atomically locks translation and rotation on individual local solver axes.
     */
    void setMotionLocks(bool linearX, bool linearY, bool linearZ, bool angularX, bool angularY,
                        bool angularZ);
    /** @brief Whether translation along X is locked. */
    bool isLinearXLocked() const;
    /** @brief Whether translation along Y is locked. */
    bool isLinearYLocked() const;
    /** @brief Whether translation along Z is locked. */
    bool isLinearZLocked() const;
    /** @brief Whether rotation around X is locked. */
    bool isAngularXLocked() const;
    /** @brief Whether rotation around Y is locked. */
    bool isAngularYLocked() const;
    /** @brief Whether rotation around Z is locked. */
    bool isAngularZLocked() const;

    /**
     * @brief Overrides mass, local center of mass, and the symmetric local inertia tensor.
     * @param mass Positive mass in kilograms.
     * @param centerX Local center of mass X coordinate.
     * @param centerY Local center of mass Y coordinate.
     * @param centerZ Local center of mass Z coordinate.
     * @param inertiaXX Inertia tensor XX component about the center of mass.
     * @param inertiaYY Inertia tensor YY component about the center of mass.
     * @param inertiaZZ Inertia tensor ZZ component about the center of mass.
     * @param inertiaXY Symmetric inertia tensor XY component.
     * @param inertiaXZ Symmetric inertia tensor XZ component.
     * @param inertiaYZ Symmetric inertia tensor YZ component.
     * @throws eve::Exception unless the body is dynamic and the tensor is positive definite.
     */
    void setMassProperties(float mass, float centerX, float centerY, float centerZ,
                           float inertiaXX, float inertiaYY, float inertiaZZ,
                           float inertiaXY = 0.f, float inertiaXZ = 0.f,
                           float inertiaYZ = 0.f);
    /** @brief Restores automatic mass, center, and inertia calculation from attached shapes. */
    void resetMassProperties();
    /** @brief Local inertia tensor XX component about the center of mass. */
    float getInertiaXX() const;
    /** @brief Local inertia tensor YY component about the center of mass. */
    float getInertiaYY() const;
    /** @brief Local inertia tensor ZZ component about the center of mass. */
    float getInertiaZZ() const;
    /** @brief Local symmetric inertia tensor XY component. */
    float getInertiaXY() const;
    /** @brief Local symmetric inertia tensor XZ component. */
    float getInertiaXZ() const;
    /** @brief Local symmetric inertia tensor YZ component. */
    float getInertiaYZ() const;
    /** @brief Local center of mass X coordinate. */
    float getLocalCenterX() const;
    /** @brief Local center of mass Y coordinate. */
    float getLocalCenterY() const;
    /** @brief Local center of mass Z coordinate. */
    float getLocalCenterZ() const;
    /** @brief World center of mass X coordinate. */
    float getWorldCenterX() const;
    /** @brief World center of mass Y coordinate. */
    float getWorldCenterY() const;
    /** @brief World center of mass Z coordinate. */
    float getWorldCenterZ() const;

    /** @brief "static" | "kinematic" | "dynamic". */
    void        setType(const std::string &bodyType);
    std::string getType() const;

    /** @brief Lock all angular axes (Box3D motion locks). */
    void setFixedRotation(bool fixed);
    bool isFixedRotation() const;

    /** @brief Disables/enables the body and its shapes. */
    void setActive(bool active);
    bool isActive() const;

    /** @brief CCD bullet mode. */
    void setBullet(bool bullet);
    bool isBullet() const;

    /** @brief Wakes / sleeps the body manually. */
    void setAwake(bool awake);
    bool isAwake() const;

    /**
     * @brief Creates a box shape in meter-space units.
     * @return Borrowed nullable shape owned by this body/world.
     * @ownership The Box3D world owns the shape; callers must destroy it through this API.
     * @lifetime Valid until shape/body/world destruction; use PhysicsShapeHandle across frames.
     * @thread Call on the owning physics thread.
     * @reentrancy Creation does not invoke user callbacks; do not re-enter world mutation while using the result.
     */
    Shape3D *newBoxShape(float width, float height, float depth, float density = 1.f,
                         float friction = 0.2f, float restitution = 0.f);
    /**
     * @brief Creates a sphere shape in meter-space units.
     * @return Borrowed nullable shape owned by this body/world.
     * @ownership The Box3D world owns the shape; callers must destroy it through this API.
     * @lifetime Valid until shape/body/world destruction; use PhysicsShapeHandle across frames.
     * @thread Call on the owning physics thread.
     * @reentrancy Creation does not invoke user callbacks; do not re-enter world mutation while using the result.
     */
    Shape3D *newSphereShape(float radius, float density = 1.f, float friction = 0.2f,
                            float restitution = 0.f);
    /**
     * @brief Creates a capsule along local Y.
     * @return Borrowed nullable shape owned by this body/world.
     * @ownership The Box3D world owns the shape; callers must destroy it through this API.
     * @lifetime Valid until shape/body/world destruction; use PhysicsShapeHandle across frames.
     * @thread Call on the owning physics thread.
     * @reentrancy Creation does not invoke user callbacks; do not re-enter world mutation while using the result.
     */
    Shape3D *newCapsuleShape(float height, float radius, float density = 1.f,
                             float friction = 0.2f, float restitution = 0.f);
    /**
     * @brief Creates a convex hull from packed local XYZ vertices.
     * @param vertices At least four finite, non-coplanar points (x0,y0,z0,...).
     * @param maxVertices Hull simplification budget in [4, 254].
     * @param density Density in kg/m^3.
     * @param friction Surface friction.
     * @param restitution Surface restitution.
     * @throws eve::Exception for malformed or degenerate input.
     * @return Borrowed nullable shape owned by this body/world.
     * @ownership The Box3D world owns the shape; callers must destroy it through this API.
     * @lifetime Valid until shape/body/world destruction; use PhysicsShapeHandle across frames.
     * @thread Call on the owning physics thread.
     * @reentrancy Creation does not invoke user callbacks; do not re-enter world mutation while using the result.
     */
    Shape3D *newConvexHullShape(const std::vector<float> &vertices, int maxVertices = 64,
                                float density = 1.f, float friction = 0.2f,
                                float restitution = 0.f);
    /**
     * @brief Creates a static concave triangle-mesh collider from packed arrays.
     * @param vertices Finite local XYZ values.
     * @param indices Triangle indices, three per counter-clockwise face.
     * @param weldVertices Whether nearby source vertices are welded.
     * @param weldTolerance Non-negative weld distance in meters.
     * @param identifyEdges Whether shared edges are classified for smooth collision.
     * @param useMedianSplit Faster BVH construction for regular grid-like meshes.
     * @throws eve::Exception unless this is a static Body or data is invalid/degenerate.
     * @return Borrowed nullable shape owned by this body/world.
     * @ownership The Box3D world owns the shape; callers must destroy it through this API.
     * @lifetime Valid until shape/body/world destruction; use PhysicsShapeHandle across frames.
     * @thread Call on the owning physics thread.
     * @reentrancy Creation does not invoke user callbacks; do not re-enter world mutation while using the result.
     */
    Shape3D *newTriangleMeshShape(const std::vector<float> &vertices,
                                  const std::vector<int32_t> &indices,
                                  bool weldVertices = true, float weldTolerance = 0.001f,
                                  bool identifyEdges = true, bool useMedianSplit = false);
    /**
     * @brief Creates a compressed static height-field collider extending along +X/+Z.
     * @param countX Number of samples along local X, at least two.
     * @param countZ Number of samples along local Z, at least two.
     * @param cellSizeX Positive spacing in meters along X.
     * @param cellSizeZ Positive spacing in meters along Z.
     * @param heights countX*countZ row-major world-meter heights.
     * @param globalMin Shared quantization minimum; heights must lie in range.
     * @param globalMax Shared quantization maximum.
     * @param clockwiseWinding Inverts the collidable side when true.
     * @return Borrowed nullable shape owned by this body/world.
     * @ownership The Box3D world owns the shape; callers must destroy it through this API.
     * @lifetime Valid until shape/body/world destruction; use PhysicsShapeHandle across frames.
     * @thread Call on the owning physics thread.
     * @reentrancy Creation does not invoke user callbacks; do not re-enter world mutation while using the result.
     */
    Shape3D *newHeightFieldShape(int countX, int countZ, float cellSizeX, float cellSizeZ,
                                 const std::vector<float> &heights, float globalMin,
                                 float globalMax, bool clockwiseWinding = false);

    /** @brief Destroys the body inside its world. */
    void destroy();

    /**
     * @brief Returns the owning world, or null after invalidation.
     * @return Borrowed nullable World3D pointer owned by the physics registry.
     * @ownership Body3D does not own the world; callers must not delete it.
     * @lifetime Valid until world destruction; use PhysicsWorldHandle across frames.
     * @thread Call on the owning physics thread.
     * @reentrancy The accessor invokes no callbacks and is invalid across world mutation.
     */
    World3D  *getWorld() { return world_; }
    /**
     * @brief Returns the owning world as a read-only borrowed pointer.
     * @return Borrowed nullable World3D pointer.
     * @ownership Body3D does not own the world; callers must not delete it.
     * @lifetime Valid until world destruction; use PhysicsWorldHandle across frames.
     * @thread Call on the owning physics thread.
     * @reentrancy The accessor invokes no callbacks and is invalid across world mutation.
     */
    const World3D *getWorld() const { return world_; }
    b3BodyId  raw() const { return bodyId_; }
    /** @brief True while the underlying Box3D body is still alive. */
    bool      isValid() const;

    /**
     * @brief Returns the process-local solver handle used by PhysicsLink.
     * @return Generation-qualified handle; it becomes stale on destruction.
     */
    [[nodiscard]] PhysicsBodyHandle runtimeHandle() const noexcept { return runtimeHandle_; }

    /** @brief Internal: marks the wrapper invalid after world destruction. */
    void invalidate();

private:
    friend class World3D;
    friend class Shape3D;

    World3D *world_  = nullptr;
    b3BodyId bodyId_{};
    int      id_ = 0;
    PhysicsBodyHandle runtimeHandle_ = PhysicsBodyHandle::invalid();
};

}  // namespace eve::physics
