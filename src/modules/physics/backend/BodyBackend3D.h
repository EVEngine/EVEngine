#pragma once
#include <string>
#include <vector>
#include "common/Result.h"
#include "physics/backend/RigidTypes3D.h"
#include "physics/backend/ShapeBackend3D.h"
namespace eve::physics {
class Body3D;
/**
 * @brief Solver-independent control of a rigid body, exclusively owned by its facade.
 * @ownership Implementation owns the native body; parent invalidation detaches before world teardown.
 * @thread Owning simulation thread only, between steps; no callbacks or reentrant mutation.
 * @lifetime Results own their data. No borrowed native state escapes this interface.
 * @remarks Failed commands return StaleHandle, InvalidArgument or Unsupported before mutation.
 * Units are metres, seconds and kilograms; rotations use XYZW quaternions.
 */
class IBodyBackend3D {
public:
    /** @brief Creates and owns one shape for this body; no input references are retained.
     * @cost One geometry cook per creation; call during topology changes, not during solver traversal.
     * @return Owned backend or InvalidArgument, Unsupported, StaleHandle, or Failed without adding a shape.
     */
    [[nodiscard]] virtual eve::Result<std::unique_ptr<IShapeBackend3D>> createShape(const ShapeGeometry3D& geometry,
                                                                                    float density, float friction,
                                                                                    float restitution) = 0;
    /** @brief Binds its exclusive facade owner for internal attribution, on the simulation thread.
     * @ownership Borrowed owner remains alive until invalidate; no external callbacks are invoked.
     */
    virtual void bindOwner(Body3D& owner) noexcept = 0;
    virtual ~IBodyBackend3D()                      = default;
    /** @brief Whether the native body remains alive. */
    virtual bool isValid() const noexcept = 0;
    /** @brief Releases the native body idempotently without callbacks; children must be detached first. */
    virtual void destroy() noexcept = 0;
    /** @brief Detaches before its parent world destroys the native body. */
    virtual void invalidate() noexcept = 0;
    /** @brief setPosition; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setPosition(float x, float y, float z) = 0;
    /** @brief getX; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getX() const = 0;
    /** @brief getY; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getY() const = 0;
    /** @brief getZ; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getZ() const = 0;
    /** @brief setRotation; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setRotation(float qx, float qy, float qz, float qw) = 0;
    /** @brief getRotX; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getRotX() const = 0;
    /** @brief getRotY; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getRotY() const = 0;
    /** @brief getRotZ; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getRotZ() const = 0;
    /** @brief getRotW; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getRotW() const = 0;
    /** @brief setLinearVelocity; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setLinearVelocity(float vx, float vy, float vz) = 0;
    /** @brief getLinearVelocityX; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getLinearVelocityX() const = 0;
    /** @brief getLinearVelocityY; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getLinearVelocityY() const = 0;
    /** @brief getLinearVelocityZ; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getLinearVelocityZ() const = 0;
    /** @brief getMass; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getMass() const = 0;
    /** @brief setAngularVelocity; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setAngularVelocity(float wx, float wy, float wz) = 0;
    /** @brief getAngularVelocityX; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getAngularVelocityX() const = 0;
    /** @brief getAngularVelocityY; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getAngularVelocityY() const = 0;
    /** @brief getAngularVelocityZ; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getAngularVelocityZ() const = 0;
    /** @brief localToWorldPoint; inherits the class lifetime and failure contract. @cost Three scalar values copied per
     * query; no geometry cooking or solver step. */
    [[nodiscard]] virtual eve::Result<std::vector<float>> localToWorldPoint(float x, float y, float z) const = 0;
    /** @brief localToWorldPointOwned; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<PhysicsVector3D> localToWorldPointOwned(float x, float y, float z) const = 0;
    /** @brief worldToLocalPoint; inherits the class lifetime and failure contract. @cost Three scalar values copied per
     * query; no geometry cooking or solver step. */
    [[nodiscard]] virtual eve::Result<std::vector<float>> worldToLocalPoint(float x, float y, float z) const = 0;
    /** @brief worldToLocalPointOwned; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<PhysicsVector3D> worldToLocalPointOwned(float x, float y, float z) const = 0;
    /** @brief localToWorldVector; inherits the class lifetime and failure contract. @cost Three scalar values copied
     * per query; no geometry cooking or solver step. */
    [[nodiscard]] virtual eve::Result<std::vector<float>> localToWorldVector(float x, float y, float z) const = 0;
    /** @brief localToWorldVectorOwned; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<PhysicsVector3D> localToWorldVectorOwned(float x, float y, float z) const = 0;
    /** @brief worldToLocalVector; inherits the class lifetime and failure contract. @cost Three scalar values copied
     * per query; no geometry cooking or solver step. */
    [[nodiscard]] virtual eve::Result<std::vector<float>> worldToLocalVector(float x, float y, float z) const = 0;
    /** @brief getLocalPointVelocity; inherits the class lifetime and failure contract. @cost Three scalar values copied
     * per query; no geometry cooking or solver step. */
    [[nodiscard]] virtual eve::Result<std::vector<float>> getLocalPointVelocity(float x, float y, float z) const = 0;
    /** @brief getLocalPointVelocityOwned; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<PhysicsVector3D> getLocalPointVelocityOwned(float x, float y, float z) const = 0;
    /** @brief getWorldPointVelocity; inherits the class lifetime and failure contract. @cost Three scalar values copied
     * per query; no geometry cooking or solver step. */
    [[nodiscard]] virtual eve::Result<std::vector<float>> getWorldPointVelocity(float x, float y, float z) const = 0;
    /** @brief applyForce; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> applyForce(float fx, float fy, float fz) = 0;
    /** @brief applyForceAt; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> applyForceAt(float fx, float fy, float fz, float x, float y, float z) = 0;
    /** @brief applyTorque; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> applyTorque(float tx, float ty, float tz) = 0;
    /** @brief applyLinearImpulse; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> applyLinearImpulse(float ix, float iy, float iz) = 0;
    /** @brief applyLinearImpulseAt; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> applyLinearImpulseAt(float ix, float iy, float iz, float x, float y,
                                                                 float z) = 0;
    /** @brief applyAngularImpulse; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> applyAngularImpulse(float ix, float iy, float iz) = 0;
    /** @brief applyLocalForce; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> applyLocalForce(float fx, float fy, float fz, float x, float y,
                                                            float z) = 0;
    /** @brief applyLocalForceToCenter; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> applyLocalForceToCenter(float fx, float fy, float fz) = 0;
    /** @brief applyLocalTorque; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> applyLocalTorque(float tx, float ty, float tz) = 0;
    /** @brief applyLocalLinearImpulse; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> applyLocalLinearImpulse(float ix, float iy, float iz, float x, float y,
                                                                    float z) = 0;
    /** @brief applyLocalLinearImpulseToCenter; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> applyLocalLinearImpulseToCenter(float ix, float iy, float iz) = 0;
    /** @brief applyLocalAngularImpulse; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> applyLocalAngularImpulse(float ix, float iy, float iz) = 0;
    /** @brief setTargetTransform; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setTargetTransform(float x, float y, float z, float qx, float qy, float qz,
                                                               float qw, float timeStep) = 0;
    /** @brief setLinearDamping; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setLinearDamping(float damping) = 0;
    /** @brief getLinearDamping; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getLinearDamping() const = 0;
    /** @brief setAngularDamping; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setAngularDamping(float damping) = 0;
    /** @brief getAngularDamping; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getAngularDamping() const = 0;
    /** @brief setGravityScale; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setGravityScale(float scale) = 0;
    /** @brief getGravityScale; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getGravityScale() const = 0;
    /** @brief setSleepEnabled; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setSleepEnabled(bool enabled) = 0;
    /** @brief isSleepEnabled; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<bool> isSleepEnabled() const = 0;
    /** @brief setSleepThreshold; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setSleepThreshold(float threshold) = 0;
    /** @brief getSleepThreshold; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getSleepThreshold() const = 0;
    /** @brief setMotionLocks; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setMotionLocks(bool linearX, bool linearY, bool linearZ, bool angularX,
                                                           bool angularY, bool angularZ) = 0;
    /** @brief isLinearXLocked; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<bool> isLinearXLocked() const = 0;
    /** @brief isLinearYLocked; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<bool> isLinearYLocked() const = 0;
    /** @brief isLinearZLocked; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<bool> isLinearZLocked() const = 0;
    /** @brief isAngularXLocked; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<bool> isAngularXLocked() const = 0;
    /** @brief isAngularYLocked; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<bool> isAngularYLocked() const = 0;
    /** @brief isAngularZLocked; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<bool> isAngularZLocked() const = 0;
    /** @brief setMassProperties; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setMassProperties(float mass, float centerX, float centerY, float centerZ,
                                                              float inertiaXX, float inertiaYY, float inertiaZZ,
                                                              float inertiaXY, float inertiaXZ, float inertiaYZ) = 0;
    /** @brief resetMassProperties; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> resetMassProperties() = 0;
    /** @brief getInertiaXX; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getInertiaXX() const = 0;
    /** @brief getInertiaYY; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getInertiaYY() const = 0;
    /** @brief getInertiaZZ; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getInertiaZZ() const = 0;
    /** @brief getInertiaXY; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getInertiaXY() const = 0;
    /** @brief getInertiaXZ; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getInertiaXZ() const = 0;
    /** @brief getInertiaYZ; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getInertiaYZ() const = 0;
    /** @brief getLocalCenterX; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getLocalCenterX() const = 0;
    /** @brief getLocalCenterY; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getLocalCenterY() const = 0;
    /** @brief getLocalCenterZ; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getLocalCenterZ() const = 0;
    /** @brief getWorldCenterX; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getWorldCenterX() const = 0;
    /** @brief getWorldCenterY; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getWorldCenterY() const = 0;
    /** @brief getWorldCenterZ; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getWorldCenterZ() const = 0;
    /** @brief setType; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setType(const std::string& bodyType) = 0;
    /** @brief getType; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<std::string> getType() const = 0;
    /** @brief setFixedRotation; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setFixedRotation(bool fixed) = 0;
    /** @brief isFixedRotation; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<bool> isFixedRotation() const = 0;
    /** @brief setActive; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setActive(bool active) = 0;
    /** @brief isActive; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<bool> isActive() const = 0;
    /** @brief setBullet; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setBullet(bool bullet) = 0;
    /** @brief isBullet; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<bool> isBullet() const = 0;
    /** @brief setAwake; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setAwake(bool awake) = 0;
    /** @brief isAwake; inherits the class lifetime and failure contract. */
    [[nodiscard]] virtual eve::Result<bool> isAwake() const = 0;
};
}  // namespace eve::physics
