#include "physics/box3d/Box3DBody.h"
#include <box3d/box3d.h>
#include <cmath>
#include "common/Exception.h"
#include "physics/Body3D.h"
#include "physics/box3d/Box3DShape.h"
namespace eve::physics::box3d {
namespace {
b3BodyType parseBodyType(const std::string &type) {
    if (type == "static") return b3_staticBody;
    if (type == "kinematic") return b3_kinematicBody;
    if (type == "dynamic") return b3_dynamicBody;
    throw eve::Exception("Body3D.setType: unknown body type '%s'", type.c_str());
}

const char *bodyTypeName(b3BodyType t) {
    switch (t) {
        case b3_staticBody: return "static";
        case b3_kinematicBody: return "kinematic";
        case b3_dynamicBody: return "dynamic";
        default: return "static";
    }
}

void requireFinite(float value, const char *operation, const char *parameter) {
    if (!std::isfinite(value)) throw eve::Exception("%s: %s must be finite", operation, parameter);
}

void requireNonNegative(float value, const char *operation, const char *parameter) {
    requireFinite(value, operation, parameter);
    if (value < 0.f) throw eve::Exception("%s: %s must be >= 0", operation, parameter);
}

b3Vec3 checkedVector(float x, float y, float z, const char *operation, const char *parameter) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        throw eve::Exception("%s: %s components must be finite", operation, parameter);
    return {x, y, z};
}

b3Quat normalizedQuaternion(float qx, float qy, float qz, float qw, const char *operation) {
    requireFinite(qx, operation, "qx");
    requireFinite(qy, operation, "qy");
    requireFinite(qz, operation, "qz");
    requireFinite(qw, operation, "qw");
    const float lengthSquared = qx * qx + qy * qy + qz * qz + qw * qw;
    if (lengthSquared <= 1e-16f) throw eve::Exception("%s: quaternion length must be > 0", operation);
    const float inverseLength = 1.f / std::sqrt(lengthSquared);
    return b3Quat{{qx * inverseLength, qy * inverseLength, qz * inverseLength}, qw * inverseLength};
}


class Box3DBody final : public IBodyBackend3D {
    template <class F>
    auto checked(const char *name, F &&operation) const -> decltype(operation()) {
        using R = decltype(operation());
        if (!isValid())
            return R::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "Body is no longer valid", name));
        try {
            return operation();
        } catch (const eve::Exception &error) {
            return R::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, error.what(), name));
        }
    }

public:
    explicit Box3DBody(b3BodyId id) : bodyId_(id) {}
    ~Box3DBody() override { destroy(); }
    void bindOwner(Body3D &owner) noexcept override {
        if (isValid()) b3Body_SetUserData(bodyId_, &owner);
    }
    bool isValid() const noexcept override { return b3Body_IsValid(bodyId_); }
    void destroy() noexcept override {
        if (isValid()) {
            b3Body_SetUserData(bodyId_, nullptr);
            b3DestroyBody(bodyId_);
        }
        bodyId_ = {};
    }
    void invalidate() noexcept override {
        if (isValid()) b3Body_SetUserData(bodyId_, nullptr);
        bodyId_ = {};
    }
    eve::Result<std::unique_ptr<IShapeBackend3D>> createShape(const ShapeGeometry3D &geometry, float density,
                                                              float friction, float restitution) override {
        return checked("physics.body3d.createShape",
                       [&] { return makeShape(bodyId_, geometry, density, friction, restitution); });
    }
    b3BodyId          nativeId() const { return bodyId_; }
    eve::Result<void> setPosition(float x, float y, float z) override {
        return checked("physics.body3d.setPosition", [&] {
            b3Body_SetTransform(bodyId_, b3Pos{x, y, z}, b3Body_GetRotation(bodyId_));

            return eve::Result<void>::success();
        });
    }
    eve::Result<float> getX() const override {
        return checked("physics.body3d.getX",
                       [&] { return eve::Result<float>::success(static_cast<float>(b3Body_GetPosition(bodyId_).x)); });
    }
    eve::Result<float> getY() const override {
        return checked("physics.body3d.getY",
                       [&] { return eve::Result<float>::success(static_cast<float>(b3Body_GetPosition(bodyId_).y)); });
    }
    eve::Result<float> getZ() const override {
        return checked("physics.body3d.getZ",
                       [&] { return eve::Result<float>::success(static_cast<float>(b3Body_GetPosition(bodyId_).z)); });
    }
    eve::Result<void> setRotation(float qx, float qy, float qz, float qw) override {
        return checked("physics.body3d.setRotation", [&] {
            b3Quat q;
            q.v       = b3Vec3{qx, qy, qz};
            q.s       = qw;
            float len = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
            if (len > 1e-8f) {
                q.v.x /= len;
                q.v.y /= len;
                q.v.z /= len;
                q.s /= len;
            } else {
                q = b3Quat_identity;
            }
            b3Body_SetTransform(bodyId_, b3Body_GetPosition(bodyId_), q);

            return eve::Result<void>::success();
        });
    }
    eve::Result<float> getRotX() const override {
        return checked("physics.body3d.getRotX",
                       [&] { return eve::Result<float>::success(b3Body_GetRotation(bodyId_).v.x); });
    }
    eve::Result<float> getRotY() const override {
        return checked("physics.body3d.getRotY",
                       [&] { return eve::Result<float>::success(b3Body_GetRotation(bodyId_).v.y); });
    }
    eve::Result<float> getRotZ() const override {
        return checked("physics.body3d.getRotZ",
                       [&] { return eve::Result<float>::success(b3Body_GetRotation(bodyId_).v.z); });
    }
    eve::Result<float> getRotW() const override {
        return checked("physics.body3d.getRotW",
                       [&] { return eve::Result<float>::success(b3Body_GetRotation(bodyId_).s); });
    }
    eve::Result<void> setLinearVelocity(float vx, float vy, float vz) override {
        return checked("physics.body3d.setLinearVelocity", [&] {
            b3Body_SetLinearVelocity(bodyId_, b3Vec3{vx, vy, vz});

            return eve::Result<void>::success();
        });
    }
    eve::Result<float> getLinearVelocityX() const override {
        return checked("physics.body3d.getLinearVelocityX",
                       [&] { return eve::Result<float>::success(b3Body_GetLinearVelocity(bodyId_).x); });
    }
    eve::Result<float> getLinearVelocityY() const override {
        return checked("physics.body3d.getLinearVelocityY",
                       [&] { return eve::Result<float>::success(b3Body_GetLinearVelocity(bodyId_).y); });
    }
    eve::Result<float> getLinearVelocityZ() const override {
        return checked("physics.body3d.getLinearVelocityZ",
                       [&] { return eve::Result<float>::success(b3Body_GetLinearVelocity(bodyId_).z); });
    }
    eve::Result<float> getMass() const override {
        return checked("physics.body3d.getMass", [&] { return eve::Result<float>::success(b3Body_GetMass(bodyId_)); });
    }
    eve::Result<void> setAngularVelocity(float wx, float wy, float wz) override {
        return checked("physics.body3d.setAngularVelocity", [&] {
            b3Body_SetAngularVelocity(bodyId_, b3Vec3{wx, wy, wz});

            return eve::Result<void>::success();
        });
    }
    eve::Result<float> getAngularVelocityX() const override {
        return checked("physics.body3d.getAngularVelocityX",
                       [&] { return eve::Result<float>::success(b3Body_GetAngularVelocity(bodyId_).x); });
    }
    eve::Result<float> getAngularVelocityY() const override {
        return checked("physics.body3d.getAngularVelocityY",
                       [&] { return eve::Result<float>::success(b3Body_GetAngularVelocity(bodyId_).y); });
    }
    eve::Result<float> getAngularVelocityZ() const override {
        return checked("physics.body3d.getAngularVelocityZ",
                       [&] { return eve::Result<float>::success(b3Body_GetAngularVelocity(bodyId_).z); });
    }
    eve::Result<std::vector<float>> localToWorldPoint(float x, float y, float z) const override {
        return checked("physics.body3d.localToWorldPoint", [&] {
            const b3Pos value =
                b3Body_GetWorldPoint(bodyId_, checkedVector(x, y, z, "Body3D.localToWorldPoint", "point"));
            return eve::Result<std::vector<float>>::success(
                {static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z)});
        });
    }
    eve::Result<PhysicsVector3D> localToWorldPointOwned(float x, float y, float z) const override {
        return checked("physics.body3d.localToWorldPointOwned", [&] {
            checkedVector(x, y, z, "Body3D.localToWorldPointOwned", "value");
            const b3Pos value = b3Body_GetWorldPoint(bodyId_, b3Vec3{x, y, z});
            return eve::Result<PhysicsVector3D>::success(
                {static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z)});
        });
    }
    eve::Result<std::vector<float>> worldToLocalPoint(float x, float y, float z) const override {
        return checked("physics.body3d.worldToLocalPoint", [&] {
            const b3Vec3 value =
                b3Body_GetLocalPoint(bodyId_, checkedVector(x, y, z, "Body3D.worldToLocalPoint", "point"));
            return eve::Result<std::vector<float>>::success({value.x, value.y, value.z});
        });
    }
    eve::Result<PhysicsVector3D> worldToLocalPointOwned(float x, float y, float z) const override {
        return checked("physics.body3d.worldToLocalPointOwned", [&] {
            checkedVector(x, y, z, "Body3D.worldToLocalPointOwned", "value");
            const b3Vec3 value = b3Body_GetLocalPoint(bodyId_, b3Vec3{x, y, z});
            return eve::Result<PhysicsVector3D>::success({value.x, value.y, value.z});
        });
    }
    eve::Result<std::vector<float>> localToWorldVector(float x, float y, float z) const override {
        return checked("physics.body3d.localToWorldVector", [&] {
            const b3Vec3 value =
                b3Body_GetWorldVector(bodyId_, checkedVector(x, y, z, "Body3D.localToWorldVector", "vector"));
            return eve::Result<std::vector<float>>::success({value.x, value.y, value.z});
        });
    }
    eve::Result<PhysicsVector3D> localToWorldVectorOwned(float x, float y, float z) const override {
        return checked("physics.body3d.localToWorldVectorOwned", [&] {
            checkedVector(x, y, z, "Body3D.localToWorldVectorOwned", "value");
            const b3Vec3 value = b3Body_GetWorldVector(bodyId_, b3Vec3{x, y, z});
            return eve::Result<PhysicsVector3D>::success({value.x, value.y, value.z});
        });
    }
    eve::Result<std::vector<float>> worldToLocalVector(float x, float y, float z) const override {
        return checked("physics.body3d.worldToLocalVector", [&] {
            const b3Vec3 value =
                b3Body_GetLocalVector(bodyId_, checkedVector(x, y, z, "Body3D.worldToLocalVector", "vector"));
            return eve::Result<std::vector<float>>::success({value.x, value.y, value.z});
        });
    }
    eve::Result<std::vector<float>> getLocalPointVelocity(float x, float y, float z) const override {
        return checked("physics.body3d.getLocalPointVelocity", [&] {
            const b3Vec3 value =
                b3Body_GetLocalPointVelocity(bodyId_, checkedVector(x, y, z, "Body3D.getLocalPointVelocity", "point"));
            return eve::Result<std::vector<float>>::success({value.x, value.y, value.z});
        });
    }
    eve::Result<PhysicsVector3D> getLocalPointVelocityOwned(float x, float y, float z) const override {
        return checked("physics.body3d.getLocalPointVelocityOwned", [&] {
            checkedVector(x, y, z, "Body3D.getLocalPointVelocityOwned", "value");
            const b3Vec3 value = b3Body_GetLocalPointVelocity(bodyId_, b3Vec3{x, y, z});
            return eve::Result<PhysicsVector3D>::success({value.x, value.y, value.z});
        });
    }
    eve::Result<std::vector<float>> getWorldPointVelocity(float x, float y, float z) const override {
        return checked("physics.body3d.getWorldPointVelocity", [&] {
            const b3Vec3 value =
                b3Body_GetWorldPointVelocity(bodyId_, checkedVector(x, y, z, "Body3D.getWorldPointVelocity", "point"));
            return eve::Result<std::vector<float>>::success({value.x, value.y, value.z});
        });
    }
    eve::Result<void> applyForce(float fx, float fy, float fz) override {
        return checked("physics.body3d.applyForce", [&] {
            b3Body_ApplyForceToCenter(bodyId_, b3Vec3{fx, fy, fz}, true);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> applyForceAt(float fx, float fy, float fz, float x, float y, float z) override {
        return checked("physics.body3d.applyForceAt", [&] {
            b3Body_ApplyForce(bodyId_, b3Vec3{fx, fy, fz}, b3Pos{x, y, z}, true);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> applyTorque(float tx, float ty, float tz) override {
        return checked("physics.body3d.applyTorque", [&] {
            b3Body_ApplyTorque(bodyId_, b3Vec3{tx, ty, tz}, true);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> applyLinearImpulse(float ix, float iy, float iz) override {
        return checked("physics.body3d.applyLinearImpulse", [&] {
            b3Body_ApplyLinearImpulseToCenter(bodyId_, b3Vec3{ix, iy, iz}, true);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> applyLinearImpulseAt(float ix, float iy, float iz, float x, float y, float z) override {
        return checked("physics.body3d.applyLinearImpulseAt", [&] {
            b3Body_ApplyLinearImpulse(bodyId_, b3Vec3{ix, iy, iz}, b3Pos{x, y, z}, true);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> applyAngularImpulse(float ix, float iy, float iz) override {
        return checked("physics.body3d.applyAngularImpulse", [&] {
            b3Body_ApplyAngularImpulse(bodyId_, b3Vec3{ix, iy, iz}, true);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> applyLocalForce(float fx, float fy, float fz, float x, float y, float z) override {
        return checked("physics.body3d.applyLocalForce", [&] {
            const b3Vec3 force =
                b3Body_GetWorldVector(bodyId_, checkedVector(fx, fy, fz, "Body3D.applyLocalForce", "force"));
            const b3Pos point =
                b3Body_GetWorldPoint(bodyId_, checkedVector(x, y, z, "Body3D.applyLocalForce", "point"));
            b3Body_ApplyForce(bodyId_, force, point, true);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> applyLocalForceToCenter(float fx, float fy, float fz) override {
        return checked("physics.body3d.applyLocalForceToCenter", [&] {
            const b3Vec3 force = checkedVector(fx, fy, fz, "Body3D.applyLocalForceToCenter", "force");
            b3Body_ApplyForceToCenter(bodyId_, b3Body_GetWorldVector(bodyId_, force), true);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> applyLocalTorque(float tx, float ty, float tz) override {
        return checked("physics.body3d.applyLocalTorque", [&] {
            const b3Vec3 torque = checkedVector(tx, ty, tz, "Body3D.applyLocalTorque", "torque");
            b3Body_ApplyTorque(bodyId_, b3Body_GetWorldVector(bodyId_, torque), true);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> applyLocalLinearImpulse(float ix, float iy, float iz, float x, float y, float z) override {
        return checked("physics.body3d.applyLocalLinearImpulse", [&] {
            const b3Vec3 impulse =
                b3Body_GetWorldVector(bodyId_, checkedVector(ix, iy, iz, "Body3D.applyLocalLinearImpulse", "impulse"));
            const b3Pos point =
                b3Body_GetWorldPoint(bodyId_, checkedVector(x, y, z, "Body3D.applyLocalLinearImpulse", "point"));
            b3Body_ApplyLinearImpulse(bodyId_, impulse, point, true);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> applyLocalLinearImpulseToCenter(float ix, float iy, float iz) override {
        return checked("physics.body3d.applyLocalLinearImpulseToCenter", [&] {
            const b3Vec3 impulse = checkedVector(ix, iy, iz, "Body3D.applyLocalLinearImpulseToCenter", "impulse");
            b3Body_ApplyLinearImpulseToCenter(bodyId_, b3Body_GetWorldVector(bodyId_, impulse), true);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> applyLocalAngularImpulse(float ix, float iy, float iz) override {
        return checked("physics.body3d.applyLocalAngularImpulse", [&] {
            const b3Vec3 impulse = checkedVector(ix, iy, iz, "Body3D.applyLocalAngularImpulse", "impulse");
            b3Body_ApplyAngularImpulse(bodyId_, b3Body_GetWorldVector(bodyId_, impulse), true);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> setTargetTransform(float x, float y, float z, float qx, float qy, float qz, float qw,
                                         float timeStep) override {
        return checked("physics.body3d.setTargetTransform", [&] {
            requireFinite(x, "Body3D.setTargetTransform", "x");
            requireFinite(y, "Body3D.setTargetTransform", "y");
            requireFinite(z, "Body3D.setTargetTransform", "z");
            requireFinite(timeStep, "Body3D.setTargetTransform", "timeStep");
            if (timeStep <= 0.f) throw eve::Exception("Body3D.setTargetTransform: timeStep must be > 0");
            const b3Quat rotation = normalizedQuaternion(qx, qy, qz, qw, "Body3D.setTargetTransform");
            b3Body_SetTargetTransform(bodyId_, b3WorldTransform{b3Pos{x, y, z}, rotation}, timeStep, true);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> setLinearDamping(float damping) override {
        return checked("physics.body3d.setLinearDamping", [&] {
            requireNonNegative(damping, "Body3D.setLinearDamping", "damping");
            b3Body_SetLinearDamping(bodyId_, damping);

            return eve::Result<void>::success();
        });
    }
    eve::Result<float> getLinearDamping() const override {
        return checked("physics.body3d.getLinearDamping",
                       [&] { return eve::Result<float>::success(isValid() ? b3Body_GetLinearDamping(bodyId_) : 0.f); });
    }
    eve::Result<void> setAngularDamping(float damping) override {
        return checked("physics.body3d.setAngularDamping", [&] {
            requireNonNegative(damping, "Body3D.setAngularDamping", "damping");
            b3Body_SetAngularDamping(bodyId_, damping);

            return eve::Result<void>::success();
        });
    }
    eve::Result<float> getAngularDamping() const override {
        return checked("physics.body3d.getAngularDamping", [&] {
            return eve::Result<float>::success(isValid() ? b3Body_GetAngularDamping(bodyId_) : 0.f);
        });
    }
    eve::Result<void> setGravityScale(float scale) override {
        return checked("physics.body3d.setGravityScale", [&] {
            requireFinite(scale, "Body3D.setGravityScale", "scale");
            b3Body_SetGravityScale(bodyId_, scale);

            return eve::Result<void>::success();
        });
    }
    eve::Result<float> getGravityScale() const override {
        return checked("physics.body3d.getGravityScale",
                       [&] { return eve::Result<float>::success(isValid() ? b3Body_GetGravityScale(bodyId_) : 0.f); });
    }
    eve::Result<void> setSleepEnabled(bool enabled) override {
        return checked("physics.body3d.setSleepEnabled", [&] {
            b3Body_EnableSleep(bodyId_, enabled);

            return eve::Result<void>::success();
        });
    }
    eve::Result<bool> isSleepEnabled() const override {
        return checked("physics.body3d.isSleepEnabled",
                       [&] { return eve::Result<bool>::success(isValid() ? b3Body_IsSleepEnabled(bodyId_) : false); });
    }
    eve::Result<void> setSleepThreshold(float threshold) override {
        return checked("physics.body3d.setSleepThreshold", [&] {
            requireNonNegative(threshold, "Body3D.setSleepThreshold", "threshold");
            b3Body_SetSleepThreshold(bodyId_, threshold);

            return eve::Result<void>::success();
        });
    }
    eve::Result<float> getSleepThreshold() const override {
        return checked("physics.body3d.getSleepThreshold", [&] {
            return eve::Result<float>::success(isValid() ? b3Body_GetSleepThreshold(bodyId_) : 0.f);
        });
    }
    eve::Result<void> setMotionLocks(bool linearX, bool linearY, bool linearZ, bool angularX, bool angularY,
                                     bool angularZ) override {
        return checked("physics.body3d.setMotionLocks", [&] {
            b3Body_SetMotionLocks(bodyId_, b3MotionLocks{linearX, linearY, linearZ, angularX, angularY, angularZ});

            return eve::Result<void>::success();
        });
    }
    eve::Result<bool> isLinearXLocked() const override {
        return checked("physics.body3d.isLinearXLocked", [&] {
            return eve::Result<bool>::success(isValid() ? b3Body_GetMotionLocks(bodyId_).linearX : false);
        });
    }
    eve::Result<bool> isLinearYLocked() const override {
        return checked("physics.body3d.isLinearYLocked", [&] {
            return eve::Result<bool>::success(isValid() ? b3Body_GetMotionLocks(bodyId_).linearY : false);
        });
    }
    eve::Result<bool> isLinearZLocked() const override {
        return checked("physics.body3d.isLinearZLocked", [&] {
            return eve::Result<bool>::success(isValid() ? b3Body_GetMotionLocks(bodyId_).linearZ : false);
        });
    }
    eve::Result<bool> isAngularXLocked() const override {
        return checked("physics.body3d.isAngularXLocked", [&] {
            return eve::Result<bool>::success(isValid() ? b3Body_GetMotionLocks(bodyId_).angularX : false);
        });
    }
    eve::Result<bool> isAngularYLocked() const override {
        return checked("physics.body3d.isAngularYLocked", [&] {
            return eve::Result<bool>::success(isValid() ? b3Body_GetMotionLocks(bodyId_).angularY : false);
        });
    }
    eve::Result<bool> isAngularZLocked() const override {
        return checked("physics.body3d.isAngularZLocked", [&] {
            return eve::Result<bool>::success(isValid() ? b3Body_GetMotionLocks(bodyId_).angularZ : false);
        });
    }
    eve::Result<void> setMassProperties(float mass, float centerX, float centerY, float centerZ, float inertiaXX,
                                        float inertiaYY, float inertiaZZ, float inertiaXY, float inertiaXZ,
                                        float inertiaYZ) override {
        return checked("physics.body3d.setMassProperties", [&] {
            constexpr const char *operation = "Body3D.setMassProperties";
            if (b3Body_GetType(bodyId_) != b3_dynamicBody) throw eve::Exception("%s: body must be dynamic", operation);
            requireFinite(mass, operation, "mass");
            requireFinite(centerX, operation, "centerX");
            requireFinite(centerY, operation, "centerY");
            requireFinite(centerZ, operation, "centerZ");
            requireFinite(inertiaXX, operation, "inertiaXX");
            requireFinite(inertiaYY, operation, "inertiaYY");
            requireFinite(inertiaZZ, operation, "inertiaZZ");
            requireFinite(inertiaXY, operation, "inertiaXY");
            requireFinite(inertiaXZ, operation, "inertiaXZ");
            requireFinite(inertiaYZ, operation, "inertiaYZ");
            if (!(mass > 0.f)) throw eve::Exception("%s: mass must be > 0", operation);

            // Sylvester's criterion for a symmetric positive-definite 3x3 tensor.
            const double xx = inertiaXX, yy = inertiaYY, zz = inertiaZZ;
            const double xy = inertiaXY, xz = inertiaXZ, yz = inertiaYZ;
            const double minor2      = xx * yy - xy * xy;
            const double determinant = xx * (yy * zz - yz * yz) - xy * (xy * zz - yz * xz) + xz * (xy * yz - yy * xz);
            if (!(xx > 0.0 && minor2 > 0.0 && determinant > 0.0) || !std::isfinite(minor2) ||
                !std::isfinite(determinant))
                throw eve::Exception("%s: inertia tensor must be positive definite", operation);

            b3MassData data{};
            data.mass    = mass;
            data.center  = {centerX, centerY, centerZ};
            data.inertia = {{inertiaXX, inertiaXY, inertiaXZ},
                            {inertiaXY, inertiaYY, inertiaYZ},
                            {inertiaXZ, inertiaYZ, inertiaZZ}};
            b3Body_SetMassData(bodyId_, data);

            return eve::Result<void>::success();
        });
    }
    eve::Result<void> resetMassProperties() override {
        return checked("physics.body3d.resetMassProperties", [&] {
            b3Body_ApplyMassFromShapes(bodyId_);

            return eve::Result<void>::success();
        });
    }
    eve::Result<float> getInertiaXX() const override {
        return checked("physics.body3d.getInertiaXX", [&] {
            return eve::Result<float>::success(isValid() ? b3Body_GetMassData(bodyId_).inertia.cx.x : 0.f);
        });
    }
    eve::Result<float> getInertiaYY() const override {
        return checked("physics.body3d.getInertiaYY", [&] {
            return eve::Result<float>::success(isValid() ? b3Body_GetMassData(bodyId_).inertia.cy.y : 0.f);
        });
    }
    eve::Result<float> getInertiaZZ() const override {
        return checked("physics.body3d.getInertiaZZ", [&] {
            return eve::Result<float>::success(isValid() ? b3Body_GetMassData(bodyId_).inertia.cz.z : 0.f);
        });
    }
    eve::Result<float> getInertiaXY() const override {
        return checked("physics.body3d.getInertiaXY", [&] {
            return eve::Result<float>::success(isValid() ? b3Body_GetMassData(bodyId_).inertia.cx.y : 0.f);
        });
    }
    eve::Result<float> getInertiaXZ() const override {
        return checked("physics.body3d.getInertiaXZ", [&] {
            return eve::Result<float>::success(isValid() ? b3Body_GetMassData(bodyId_).inertia.cx.z : 0.f);
        });
    }
    eve::Result<float> getInertiaYZ() const override {
        return checked("physics.body3d.getInertiaYZ", [&] {
            return eve::Result<float>::success(isValid() ? b3Body_GetMassData(bodyId_).inertia.cy.z : 0.f);
        });
    }
    eve::Result<float> getLocalCenterX() const override {
        return checked("physics.body3d.getLocalCenterX", [&] {
            return eve::Result<float>::success(isValid() ? static_cast<float>(b3Body_GetLocalCenterOfMass(bodyId_).x)
                                                         : 0.f);
        });
    }
    eve::Result<float> getLocalCenterY() const override {
        return checked("physics.body3d.getLocalCenterY", [&] {
            return eve::Result<float>::success(isValid() ? static_cast<float>(b3Body_GetLocalCenterOfMass(bodyId_).y)
                                                         : 0.f);
        });
    }
    eve::Result<float> getLocalCenterZ() const override {
        return checked("physics.body3d.getLocalCenterZ", [&] {
            return eve::Result<float>::success(isValid() ? static_cast<float>(b3Body_GetLocalCenterOfMass(bodyId_).z)
                                                         : 0.f);
        });
    }
    eve::Result<float> getWorldCenterX() const override {
        return checked("physics.body3d.getWorldCenterX", [&] {
            return eve::Result<float>::success(isValid() ? static_cast<float>(b3Body_GetWorldCenterOfMass(bodyId_).x)
                                                         : 0.f);
        });
    }
    eve::Result<float> getWorldCenterY() const override {
        return checked("physics.body3d.getWorldCenterY", [&] {
            return eve::Result<float>::success(isValid() ? static_cast<float>(b3Body_GetWorldCenterOfMass(bodyId_).y)
                                                         : 0.f);
        });
    }
    eve::Result<float> getWorldCenterZ() const override {
        return checked("physics.body3d.getWorldCenterZ", [&] {
            return eve::Result<float>::success(isValid() ? static_cast<float>(b3Body_GetWorldCenterOfMass(bodyId_).z)
                                                         : 0.f);
        });
    }
    eve::Result<void> setType(const std::string &bodyType) override {
        return checked("physics.body3d.setType", [&] {
            const b3BodyType type = parseBodyType(bodyType);
            b3Body_SetType(bodyId_, type);

            return eve::Result<void>::success();
        });
    }
    eve::Result<std::string> getType() const override {
        return checked("physics.body3d.getType",
                       [&] { return eve::Result<std::string>::success(bodyTypeName(b3Body_GetType(bodyId_))); });
    }
    eve::Result<void> setFixedRotation(bool fixed) override {
        return checked("physics.body3d.setFixedRotation", [&] {
            b3MotionLocks locks = b3Body_GetMotionLocks(bodyId_);
            locks.angularX      = fixed;
            locks.angularY      = fixed;
            locks.angularZ      = fixed;
            b3Body_SetMotionLocks(bodyId_, locks);

            return eve::Result<void>::success();
        });
    }
    eve::Result<bool> isFixedRotation() const override {
        return checked("physics.body3d.isFixedRotation", [&] {
            b3MotionLocks locks = b3Body_GetMotionLocks(bodyId_);
            return eve::Result<bool>::success(locks.angularX && locks.angularY && locks.angularZ);
        });
    }
    eve::Result<void> setActive(bool active) override {
        return checked("physics.body3d.setActive", [&] {
            if (active)
                b3Body_Enable(bodyId_);
            else
                b3Body_Disable(bodyId_);

            return eve::Result<void>::success();
        });
    }
    eve::Result<bool> isActive() const override {
        return checked("physics.body3d.isActive",
                       [&] { return eve::Result<bool>::success(isValid() ? b3Body_IsEnabled(bodyId_) : false); });
    }
    eve::Result<void> setBullet(bool bullet) override {
        return checked("physics.body3d.setBullet", [&] {
            b3Body_SetBullet(bodyId_, bullet);

            return eve::Result<void>::success();
        });
    }
    eve::Result<bool> isBullet() const override {
        return checked("physics.body3d.isBullet",
                       [&] { return eve::Result<bool>::success(isValid() ? b3Body_IsBullet(bodyId_) : false); });
    }
    eve::Result<void> setAwake(bool awake) override {
        return checked("physics.body3d.setAwake", [&] {
            b3Body_SetAwake(bodyId_, awake);

            return eve::Result<void>::success();
        });
    }
    eve::Result<bool> isAwake() const override {
        return checked("physics.body3d.isAwake",
                       [&] { return eve::Result<bool>::success(isValid() ? b3Body_IsAwake(bodyId_) : false); });
    }

private:
    b3BodyId bodyId_{};
};
}  // namespace
std::unique_ptr<IBodyBackend3D> adoptBody(b3BodyId id) { return std::make_unique<Box3DBody>(id); }
b3BodyId                        Access::bodyId(const Body3D &body) {
    const auto *backend = dynamic_cast<const Box3DBody *>(body.backend_.get());
    EV_ASSERT(backend != nullptr, "Box3D composition requires a Box3D body");
    return backend->nativeId();
}
}  // namespace eve::physics::box3d
