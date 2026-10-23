#pragma once
#include "physics/backend/BodyBackend3D.h"
namespace eve::physics::jolt {
struct State;
struct BodyState;
struct ShapeState;
// Internal provider implementation; all operations inherit the neutral interface contract.
class BodyBackend final : public IBodyBackend3D {
public:
    std::shared_ptr<BodyState> body;
    explicit BodyBackend(std::shared_ptr<BodyState> value) : body(std::move(value)) {}
    ~BodyBackend() override;
    eve::Result<std::unique_ptr<IShapeBackend3D>> createShape(const ShapeGeometry3D& geometry, float density,
                                                              float friction, float restitution) override;
    void                                          bindOwner(Body3D& owner) noexcept override;
    bool                                          isValid() const noexcept override;
    void                                          destroy() noexcept override;
    void                                          invalidate() noexcept override;
    eve::Result<void>                             setPosition(float x, float y, float z) override;
    eve::Result<float>                            getX() const override;
    eve::Result<float>                            getY() const override;
    eve::Result<float>                            getZ() const override;
    eve::Result<void>                             setRotation(float qx, float qy, float qz, float qw) override;
    eve::Result<float>                            getRotX() const override;
    eve::Result<float>                            getRotY() const override;
    eve::Result<float>                            getRotZ() const override;
    eve::Result<float>                            getRotW() const override;
    eve::Result<void>                             setLinearVelocity(float vx, float vy, float vz) override;
    eve::Result<float>                            getLinearVelocityX() const override;
    eve::Result<float>                            getLinearVelocityY() const override;
    eve::Result<float>                            getLinearVelocityZ() const override;
    eve::Result<float>                            getMass() const override;
    eve::Result<void>                             setAngularVelocity(float wx, float wy, float wz) override;
    eve::Result<float>                            getAngularVelocityX() const override;
    eve::Result<float>                            getAngularVelocityY() const override;
    eve::Result<float>                            getAngularVelocityZ() const override;
    eve::Result<std::vector<float>>               localToWorldPoint(float x, float y, float z) const override;
    eve::Result<PhysicsVector3D>                  localToWorldPointOwned(float x, float y, float z) const override;
    eve::Result<std::vector<float>>               worldToLocalPoint(float x, float y, float z) const override;
    eve::Result<PhysicsVector3D>                  worldToLocalPointOwned(float x, float y, float z) const override;
    eve::Result<std::vector<float>>               localToWorldVector(float x, float y, float z) const override;
    eve::Result<PhysicsVector3D>                  localToWorldVectorOwned(float x, float y, float z) const override;
    eve::Result<std::vector<float>>               worldToLocalVector(float x, float y, float z) const override;
    eve::Result<std::vector<float>>               getLocalPointVelocity(float x, float y, float z) const override;
    eve::Result<PhysicsVector3D>                  getLocalPointVelocityOwned(float x, float y, float z) const override;
    eve::Result<std::vector<float>>               getWorldPointVelocity(float x, float y, float z) const override;
    eve::Result<void>                             applyForce(float fx, float fy, float fz) override;
    eve::Result<void>        applyForceAt(float fx, float fy, float fz, float x, float y, float z) override;
    eve::Result<void>        applyTorque(float tx, float ty, float tz) override;
    eve::Result<void>        applyLinearImpulse(float ix, float iy, float iz) override;
    eve::Result<void>        applyLinearImpulseAt(float ix, float iy, float iz, float x, float y, float z) override;
    eve::Result<void>        applyAngularImpulse(float ix, float iy, float iz) override;
    eve::Result<void>        applyLocalForce(float fx, float fy, float fz, float x, float y, float z) override;
    eve::Result<void>        applyLocalForceToCenter(float fx, float fy, float fz) override;
    eve::Result<void>        applyLocalTorque(float tx, float ty, float tz) override;
    eve::Result<void>        applyLocalLinearImpulse(float ix, float iy, float iz, float x, float y, float z) override;
    eve::Result<void>        applyLocalLinearImpulseToCenter(float ix, float iy, float iz) override;
    eve::Result<void>        applyLocalAngularImpulse(float ix, float iy, float iz) override;
    eve::Result<void>        setTargetTransform(float x, float y, float z, float qx, float qy, float qz, float qw,
                                                float timeStep) override;
    eve::Result<void>        setLinearDamping(float damping) override;
    eve::Result<float>       getLinearDamping() const override;
    eve::Result<void>        setAngularDamping(float damping) override;
    eve::Result<float>       getAngularDamping() const override;
    eve::Result<void>        setGravityScale(float scale) override;
    eve::Result<float>       getGravityScale() const override;
    eve::Result<void>        setSleepEnabled(bool enabled) override;
    eve::Result<bool>        isSleepEnabled() const override;
    eve::Result<void>        setSleepThreshold(float threshold) override;
    eve::Result<float>       getSleepThreshold() const override;
    eve::Result<void>        setMotionLocks(bool linearX, bool linearY, bool linearZ, bool angularX, bool angularY,
                                            bool angularZ) override;
    eve::Result<bool>        isLinearXLocked() const override;
    eve::Result<bool>        isLinearYLocked() const override;
    eve::Result<bool>        isLinearZLocked() const override;
    eve::Result<bool>        isAngularXLocked() const override;
    eve::Result<bool>        isAngularYLocked() const override;
    eve::Result<bool>        isAngularZLocked() const override;
    eve::Result<void>        setMassProperties(float mass, float centerX, float centerY, float centerZ, float inertiaXX,
                                               float inertiaYY, float inertiaZZ, float inertiaXY, float inertiaXZ,
                                               float inertiaYZ) override;
    eve::Result<void>        resetMassProperties() override;
    eve::Result<float>       getInertiaXX() const override;
    eve::Result<float>       getInertiaYY() const override;
    eve::Result<float>       getInertiaZZ() const override;
    eve::Result<float>       getInertiaXY() const override;
    eve::Result<float>       getInertiaXZ() const override;
    eve::Result<float>       getInertiaYZ() const override;
    eve::Result<float>       getLocalCenterX() const override;
    eve::Result<float>       getLocalCenterY() const override;
    eve::Result<float>       getLocalCenterZ() const override;
    eve::Result<float>       getWorldCenterX() const override;
    eve::Result<float>       getWorldCenterY() const override;
    eve::Result<float>       getWorldCenterZ() const override;
    eve::Result<void>        setType(const std::string& bodyType) override;
    eve::Result<std::string> getType() const override;
    eve::Result<void>        setFixedRotation(bool fixed) override;
    eve::Result<bool>        isFixedRotation() const override;
    eve::Result<void>        setActive(bool active) override;
    eve::Result<bool>        isActive() const override;
    eve::Result<void>        setBullet(bool bullet) override;
    eve::Result<bool>        isBullet() const override;
    eve::Result<void>        setAwake(bool awake) override;
    eve::Result<bool>        isAwake() const override;
};
}  // namespace eve::physics::jolt
