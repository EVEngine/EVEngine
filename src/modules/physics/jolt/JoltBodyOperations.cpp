// Jolt requires its platform header before any other native header.
#include <Jolt/Jolt.h>

// Provider implementation dependencies.
#include <Jolt/Physics/Body/MotionProperties.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include "physics/jolt/JoltInternal.h"
#include "physics/jolt/JoltJoint.h"
#include "physics/jolt/JoltWorld.h"
namespace eve::physics::jolt {
eve::Result<void> BodyBackend::setPosition(float x, float y, float z) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    if (!isFinite(x, y, z)) return rejected<void>("Invalid position", eve::DiagnosticCode::InvalidArgument);
    body->api().SetPosition(body->id, {x, y, z}, JPH::EActivation::Activate);
    return success();
}
eve::Result<float> BodyBackend::getX() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(static_cast<float>(body->api().GetPosition(body->id).GetX()));
}
eve::Result<float> BodyBackend::getY() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(static_cast<float>(body->api().GetPosition(body->id).GetY()));
}
eve::Result<float> BodyBackend::getZ() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(static_cast<float>(body->api().GetPosition(body->id).GetZ()));
}
eve::Result<void> BodyBackend::setRotation(float qx, float qy, float qz, float qw) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    JPH::Quat q(qx, qy, qz, qw);
    if (!isFinite(qx, qy, qz) || !std::isfinite(qw) || !std::isfinite(q.LengthSq()) || q.LengthSq() < 1.e-12f)
        return rejected<void>("Invalid rotation", eve::DiagnosticCode::InvalidArgument);
    body->api().SetRotation(body->id, q.Normalized(), JPH::EActivation::Activate);
    return success();
}
eve::Result<float> BodyBackend::getRotX() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(body->api().GetRotation(body->id).GetX());
}
eve::Result<float> BodyBackend::getRotY() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(body->api().GetRotation(body->id).GetY());
}
eve::Result<float> BodyBackend::getRotZ() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(body->api().GetRotation(body->id).GetZ());
}
eve::Result<float> BodyBackend::getRotW() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(body->api().GetRotation(body->id).GetW());
}
eve::Result<void> BodyBackend::setLinearVelocity(float vx, float vy, float vz) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    if (!isFinite(vx, vy, vz)) return rejected<void>("Invalid velocity", eve::DiagnosticCode::InvalidArgument);
    body->api().SetLinearVelocity(body->id, {vx, vy, vz});
    return success();
}
eve::Result<float> BodyBackend::getLinearVelocityX() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(body->api().GetLinearVelocity(body->id).GetX());
}
eve::Result<float> BodyBackend::getLinearVelocityY() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(body->api().GetLinearVelocity(body->id).GetY());
}
eve::Result<float> BodyBackend::getLinearVelocityZ() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(body->api().GetLinearVelocity(body->id).GetZ());
}
eve::Result<float> BodyBackend::getMass() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    return R::success(native->IsDynamic() ? body->mass.mMass : 0.f);
}
eve::Result<void> BodyBackend::setAngularVelocity(float wx, float wy, float wz) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    if (!isFinite(wx, wy, wz)) return rejected<void>("Invalid velocity", eve::DiagnosticCode::InvalidArgument);
    body->api().SetAngularVelocity(body->id, {wx, wy, wz});
    return success();
}
eve::Result<float> BodyBackend::getAngularVelocityX() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(body->api().GetAngularVelocity(body->id).GetX());
}
eve::Result<float> BodyBackend::getAngularVelocityY() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(body->api().GetAngularVelocity(body->id).GetY());
}
eve::Result<float> BodyBackend::getAngularVelocityZ() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(body->api().GetAngularVelocity(body->id).GetZ());
}
eve::Result<std::vector<float>> BodyBackend::localToWorldPoint(float x, float y, float z) const {
    using R [[maybe_unused]] = eve::Result<std::vector<float>>;
    if (!isValid()) return rejected<std::vector<float>>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    if (!isFinite(x, y, z)) return rejected<std::vector<float>>("Invalid vector", eve::DiagnosticCode::InvalidArgument);
    auto v = native->GetWorldTransform() * JPH::Vec3(x, y, z);
    return R::success({static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())});
}
eve::Result<PhysicsVector3D> BodyBackend::localToWorldPointOwned(float x, float y, float z) const {
    using R [[maybe_unused]] = eve::Result<PhysicsVector3D>;
    if (!isValid()) return rejected<PhysicsVector3D>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    if (!isFinite(x, y, z)) return rejected<PhysicsVector3D>("Invalid vector", eve::DiagnosticCode::InvalidArgument);
    auto v = native->GetWorldTransform() * JPH::Vec3(x, y, z);
    return R::success({static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())});
}
eve::Result<std::vector<float>> BodyBackend::worldToLocalPoint(float x, float y, float z) const {
    using R [[maybe_unused]] = eve::Result<std::vector<float>>;
    if (!isValid()) return rejected<std::vector<float>>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    if (!isFinite(x, y, z)) return rejected<std::vector<float>>("Invalid vector", eve::DiagnosticCode::InvalidArgument);
    auto v = native->GetWorldTransform().Inversed() * JPH::RVec3(x, y, z);
    return R::success({static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())});
}
eve::Result<PhysicsVector3D> BodyBackend::worldToLocalPointOwned(float x, float y, float z) const {
    using R [[maybe_unused]] = eve::Result<PhysicsVector3D>;
    if (!isValid()) return rejected<PhysicsVector3D>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    if (!isFinite(x, y, z)) return rejected<PhysicsVector3D>("Invalid vector", eve::DiagnosticCode::InvalidArgument);
    auto v = native->GetWorldTransform().Inversed() * JPH::RVec3(x, y, z);
    return R::success({static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())});
}
eve::Result<std::vector<float>> BodyBackend::localToWorldVector(float x, float y, float z) const {
    using R [[maybe_unused]] = eve::Result<std::vector<float>>;
    if (!isValid()) return rejected<std::vector<float>>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    if (!isFinite(x, y, z)) return rejected<std::vector<float>>("Invalid vector", eve::DiagnosticCode::InvalidArgument);
    auto v = native->GetRotation() * JPH::Vec3(x, y, z);
    return R::success({static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())});
}
eve::Result<PhysicsVector3D> BodyBackend::localToWorldVectorOwned(float x, float y, float z) const {
    using R [[maybe_unused]] = eve::Result<PhysicsVector3D>;
    if (!isValid()) return rejected<PhysicsVector3D>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    if (!isFinite(x, y, z)) return rejected<PhysicsVector3D>("Invalid vector", eve::DiagnosticCode::InvalidArgument);
    auto v = native->GetRotation() * JPH::Vec3(x, y, z);
    return R::success({static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())});
}
eve::Result<std::vector<float>> BodyBackend::worldToLocalVector(float x, float y, float z) const {
    using R [[maybe_unused]] = eve::Result<std::vector<float>>;
    if (!isValid()) return rejected<std::vector<float>>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    if (!isFinite(x, y, z)) return rejected<std::vector<float>>("Invalid vector", eve::DiagnosticCode::InvalidArgument);
    auto v = native->GetRotation().Conjugated() * JPH::Vec3(x, y, z);
    return R::success({static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())});
}
eve::Result<std::vector<float>> BodyBackend::getLocalPointVelocity(float x, float y, float z) const {
    using R [[maybe_unused]] = eve::Result<std::vector<float>>;
    if (!isValid()) return rejected<std::vector<float>>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    if (!isFinite(x, y, z)) return rejected<std::vector<float>>("Invalid vector", eve::DiagnosticCode::InvalidArgument);
    auto v = native->GetPointVelocity(native->GetWorldTransform() * JPH::Vec3(x, y, z));
    return R::success({static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())});
}
eve::Result<PhysicsVector3D> BodyBackend::getLocalPointVelocityOwned(float x, float y, float z) const {
    using R [[maybe_unused]] = eve::Result<PhysicsVector3D>;
    if (!isValid()) return rejected<PhysicsVector3D>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    if (!isFinite(x, y, z)) return rejected<PhysicsVector3D>("Invalid vector", eve::DiagnosticCode::InvalidArgument);
    auto v = native->GetPointVelocity(native->GetWorldTransform() * JPH::Vec3(x, y, z));
    return R::success({static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())});
}
eve::Result<std::vector<float>> BodyBackend::getWorldPointVelocity(float x, float y, float z) const {
    using R [[maybe_unused]] = eve::Result<std::vector<float>>;
    if (!isValid()) return rejected<std::vector<float>>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    if (!isFinite(x, y, z)) return rejected<std::vector<float>>("Invalid vector", eve::DiagnosticCode::InvalidArgument);
    auto v = native->GetPointVelocity(JPH::RVec3(x, y, z));
    return R::success({static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())});
}
eve::Result<void> BodyBackend::applyForce(float fx, float fy, float fz) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    if (!isFinite(fx, fy, fz)) return rejected<void>("Invalid force or impulse", eve::DiagnosticCode::InvalidArgument);
    if (native->IsDynamic()) body->api().AddForce(body->id, {fx, fy, fz});
    return success();
}
eve::Result<void> BodyBackend::applyForceAt(float fx, float fy, float fz, float x, float y, float z) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    if (!isFinite(fx, fy, fz) || !isFinite(x, y, z))
        return rejected<void>("Invalid force or impulse", eve::DiagnosticCode::InvalidArgument);
    if (native->IsDynamic()) body->api().AddForce(body->id, {fx, fy, fz}, JPH::RVec3(x, y, z));
    return success();
}
eve::Result<void> BodyBackend::applyTorque(float tx, float ty, float tz) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    if (!isFinite(tx, ty, tz)) return rejected<void>("Invalid force or impulse", eve::DiagnosticCode::InvalidArgument);
    if (native->IsDynamic()) body->api().AddTorque(body->id, {tx, ty, tz});
    return success();
}
eve::Result<void> BodyBackend::applyLinearImpulse(float ix, float iy, float iz) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    if (!isFinite(ix, iy, iz)) return rejected<void>("Invalid force or impulse", eve::DiagnosticCode::InvalidArgument);
    if (native->IsDynamic()) body->api().AddImpulse(body->id, {ix, iy, iz});
    return success();
}
eve::Result<void> BodyBackend::applyLinearImpulseAt(float ix, float iy, float iz, float x, float y, float z) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    if (!isFinite(ix, iy, iz) || !isFinite(x, y, z))
        return rejected<void>("Invalid force or impulse", eve::DiagnosticCode::InvalidArgument);
    if (native->IsDynamic()) body->api().AddImpulse(body->id, {ix, iy, iz}, JPH::RVec3(x, y, z));
    return success();
}
eve::Result<void> BodyBackend::applyAngularImpulse(float ix, float iy, float iz) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    if (!isFinite(ix, iy, iz)) return rejected<void>("Invalid force or impulse", eve::DiagnosticCode::InvalidArgument);
    if (native->IsDynamic()) body->api().AddAngularImpulse(body->id, {ix, iy, iz});
    return success();
}
eve::Result<void> BodyBackend::applyLocalForce(float vx, float vy, float vz, float x, float y, float z) {
    if (!isValid()) return rejected<void>("Body expired", eve::DiagnosticCode::StaleHandle);
    if (!isFinite(vx, vy, vz) || !isFinite(x, y, z))
        return rejected<void>("Invalid local force", eve::DiagnosticCode::InvalidArgument);
    const auto v = body->api().GetRotation(body->id) * JPH::Vec3(vx, vy, vz);
    const auto p = body->api().GetWorldTransform(body->id) * JPH::Vec3(x, y, z);
    return applyForceAt(v.GetX(), v.GetY(), v.GetZ(), float(p.GetX()), float(p.GetY()), float(p.GetZ()));
}
eve::Result<void> BodyBackend::applyLocalForceToCenter(float vx, float vy, float vz) {
    if (!isValid()) return rejected<void>("Body expired", eve::DiagnosticCode::StaleHandle);
    if (!isFinite(vx, vy, vz)) return rejected<void>("Invalid local force", eve::DiagnosticCode::InvalidArgument);
    const auto v = body->api().GetRotation(body->id) * JPH::Vec3(vx, vy, vz);
    return applyForce(v.GetX(), v.GetY(), v.GetZ());
}
eve::Result<void> BodyBackend::applyLocalTorque(float vx, float vy, float vz) {
    if (!isValid()) return rejected<void>("Body expired", eve::DiagnosticCode::StaleHandle);
    if (!isFinite(vx, vy, vz)) return rejected<void>("Invalid local force", eve::DiagnosticCode::InvalidArgument);
    const auto v = body->api().GetRotation(body->id) * JPH::Vec3(vx, vy, vz);
    return applyTorque(v.GetX(), v.GetY(), v.GetZ());
}
eve::Result<void> BodyBackend::applyLocalLinearImpulse(float vx, float vy, float vz, float x, float y, float z) {
    if (!isValid()) return rejected<void>("Body expired", eve::DiagnosticCode::StaleHandle);
    if (!isFinite(vx, vy, vz) || !isFinite(x, y, z))
        return rejected<void>("Invalid local force", eve::DiagnosticCode::InvalidArgument);
    const auto v = body->api().GetRotation(body->id) * JPH::Vec3(vx, vy, vz);
    const auto p = body->api().GetWorldTransform(body->id) * JPH::Vec3(x, y, z);
    return applyLinearImpulseAt(v.GetX(), v.GetY(), v.GetZ(), float(p.GetX()), float(p.GetY()), float(p.GetZ()));
}
eve::Result<void> BodyBackend::applyLocalLinearImpulseToCenter(float vx, float vy, float vz) {
    if (!isValid()) return rejected<void>("Body expired", eve::DiagnosticCode::StaleHandle);
    if (!isFinite(vx, vy, vz)) return rejected<void>("Invalid local force", eve::DiagnosticCode::InvalidArgument);
    const auto v = body->api().GetRotation(body->id) * JPH::Vec3(vx, vy, vz);
    return applyLinearImpulse(v.GetX(), v.GetY(), v.GetZ());
}
eve::Result<void> BodyBackend::applyLocalAngularImpulse(float vx, float vy, float vz) {
    if (!isValid()) return rejected<void>("Body expired", eve::DiagnosticCode::StaleHandle);
    if (!isFinite(vx, vy, vz)) return rejected<void>("Invalid local force", eve::DiagnosticCode::InvalidArgument);
    const auto v = body->api().GetRotation(body->id) * JPH::Vec3(vx, vy, vz);
    return applyAngularImpulse(v.GetX(), v.GetY(), v.GetZ());
}
eve::Result<void> BodyBackend::setTargetTransform(float x, float y, float z, float qx, float qy, float qz, float qw,
                                                  float timeStep) {
    if (!isValid()) return rejected<void>("Body expired", eve::DiagnosticCode::StaleHandle);
    JPH::Quat q(qx, qy, qz, qw);
    if (!isFinite(x, y, z) || !std::isfinite(timeStep) || timeStep <= 0 || !std::isfinite(q.LengthSq()) ||
        q.LengthSq() < 1.e-12f)
        return rejected<void>("Invalid target transform", eve::DiagnosticCode::InvalidArgument);
    if (body->api().GetMotionType(body->id) != JPH::EMotionType::Kinematic)
        return rejected<void>("Target transform requires a kinematic body", eve::DiagnosticCode::InvalidArgument);
    body->api().MoveKinematic(body->id, {x, y, z}, q.Normalized(), timeStep);
    return success();
}
eve::Result<void> BodyBackend::setLinearDamping(float damping) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    if (!std::isfinite(damping) || damping < 0)
        return rejected<void>("Invalid damping", eve::DiagnosticCode::InvalidArgument);
    native->GetMotionPropertiesUnchecked()->SetLinearDamping(damping);
    return success();
}
eve::Result<float> BodyBackend::getLinearDamping() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    return R::success(native->GetMotionPropertiesUnchecked()->GetLinearDamping());
}
eve::Result<void> BodyBackend::setAngularDamping(float damping) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    if (!std::isfinite(damping) || damping < 0)
        return rejected<void>("Invalid damping", eve::DiagnosticCode::InvalidArgument);
    native->GetMotionPropertiesUnchecked()->SetAngularDamping(damping);
    return success();
}
eve::Result<float> BodyBackend::getAngularDamping() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    return R::success(native->GetMotionPropertiesUnchecked()->GetAngularDamping());
}
eve::Result<void> BodyBackend::setGravityScale(float scale) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    if (!std::isfinite(scale)) return rejected<void>("Invalid gravity scale", eve::DiagnosticCode::InvalidArgument);
    body->api().SetGravityFactor(body->id, scale);
    return success();
}
eve::Result<float> BodyBackend::getGravityScale() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(body->api().GetGravityFactor(body->id));
}
eve::Result<void> BodyBackend::setSleepEnabled(bool enabled) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native       = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    body->sleepEnabled = enabled;
    body->quietSeconds = 0;
    native->SetAllowSleeping(enabled && !body->customSleepThreshold);
    return success();
}
eve::Result<bool> BodyBackend::isSleepEnabled() const {
    using R [[maybe_unused]] = eve::Result<bool>;
    if (!isValid()) return rejected<bool>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    return R::success(body->customSleepThreshold ? body->sleepEnabled : native->GetAllowSleeping());
}
eve::Result<void> BodyBackend::setSleepThreshold(float threshold) {
    if (!isValid()) return rejected<void>("Body expired", eve::DiagnosticCode::StaleHandle);
    if (!std::isfinite(threshold) || threshold < 0)
        return rejected<void>("Invalid sleep threshold", eve::DiagnosticCode::InvalidArgument);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    if (!body->customSleepThreshold) body->sleepEnabled = native->GetAllowSleeping();
    body->customSleepThreshold = true;
    body->sleepThreshold       = threshold;
    body->quietSeconds         = 0;
    native->SetAllowSleeping(false);
    return success();
}
eve::Result<float> BodyBackend::getSleepThreshold() const {
    if (!isValid()) return rejected<float>("Body expired", eve::DiagnosticCode::StaleHandle);
    return eve::Result<float>::success(
        body->customSleepThreshold ? body->sleepThreshold
                                   : body->world.lock()->system.GetPhysicsSettings().mPointVelocitySleepThreshold);
}

eve::Result<void> BodyBackend::setMotionLocks(bool linearX, bool linearY, bool linearZ, bool angularX, bool angularY,
                                              bool angularZ) {
    if (!isValid()) return rejected<void>("Body expired", eve::DiagnosticCode::StaleHandle);
    int bits = (!linearX ? 1 : 0) | (!linearY ? 2 : 0) | (!linearZ ? 4 : 0) | (!angularX ? 8 : 0) |
               (!angularY ? 16 : 0) | (!angularZ ? 32 : 0);
    if (!bits) return rejected<void>("Jolt requires at least one movable degree of freedom; use a static body");
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    body->dofs   = static_cast<JPH::EAllowedDOFs>(bits);
    if (!native->GetShape()->MustBeStatic())
        native->GetMotionPropertiesUnchecked()->SetMassProperties(body->dofs, body->mass);
    return success();
}
eve::Result<bool> BodyBackend::isLinearXLocked() const {
    if (!isValid()) return rejected<bool>("Body expired", eve::DiagnosticCode::StaleHandle);
    return eve::Result<bool>::success((static_cast<int>(body->dofs) & 1) == 0);
}
eve::Result<bool> BodyBackend::isLinearYLocked() const {
    if (!isValid()) return rejected<bool>("Body expired", eve::DiagnosticCode::StaleHandle);
    return eve::Result<bool>::success((static_cast<int>(body->dofs) & 2) == 0);
}
eve::Result<bool> BodyBackend::isLinearZLocked() const {
    if (!isValid()) return rejected<bool>("Body expired", eve::DiagnosticCode::StaleHandle);
    return eve::Result<bool>::success((static_cast<int>(body->dofs) & 4) == 0);
}
eve::Result<bool> BodyBackend::isAngularXLocked() const {
    if (!isValid()) return rejected<bool>("Body expired", eve::DiagnosticCode::StaleHandle);
    return eve::Result<bool>::success((static_cast<int>(body->dofs) & 8) == 0);
}
eve::Result<bool> BodyBackend::isAngularYLocked() const {
    if (!isValid()) return rejected<bool>("Body expired", eve::DiagnosticCode::StaleHandle);
    return eve::Result<bool>::success((static_cast<int>(body->dofs) & 16) == 0);
}
eve::Result<bool> BodyBackend::isAngularZLocked() const {
    if (!isValid()) return rejected<bool>("Body expired", eve::DiagnosticCode::StaleHandle);
    return eve::Result<bool>::success((static_cast<int>(body->dofs) & 32) == 0);
}
eve::Result<void> BodyBackend::setMassProperties(float mass, float cx, float cy, float cz, float xx, float yy, float zz,
                                                 float xy, float xz, float yz) {
    if (!isValid()) return rejected<void>("Body expired", eve::DiagnosticCode::StaleHandle);
    if (body->api().GetMotionType(body->id) != JPH::EMotionType::Dynamic || !std::isfinite(mass) || mass <= 0 ||
        !isFinite(cx, cy, cz) || !isFinite(xx, yy, zz) || !isFinite(xy, xz, yz))
        return rejected<void>("Invalid mass properties", eve::DiagnosticCode::InvalidArgument);
    JPH::MassProperties properties;
    properties.mMass = mass;
    properties.mInertia =
        JPH::Mat44(JPH::Vec4(xx, xy, xz, 0), JPH::Vec4(xy, yy, yz, 0), JPH::Vec4(xz, yz, zz, 0), JPH::Vec4(0, 0, 0, 1));
    JPH::Mat44 rotation;
    JPH::Vec3  diagonal;
    if (!properties.DecomposePrincipalMomentsOfInertia(rotation, diagonal) ||
        !isFinite(diagonal.GetX(), diagonal.GetY(), diagonal.GetZ()) || diagonal.ReduceMin() <= 0)
        return rejected<void>("Inertia must be positive definite", eve::DiagnosticCode::InvalidArgument);
    auto       original = body->api().GetShape(body->id);
    const auto previous = original->GetCenterOfMass();
    if (original->GetSubType() == JPH::EShapeSubType::OffsetCenterOfMass)
        original = static_cast<const JPH::OffsetCenterOfMassShape*>(original.GetPtr())->GetInnerShape();
    JPH::RefConst<JPH::Shape> adjusted =
        new JPH::OffsetCenterOfMassShape(original, JPH::Vec3(cx, cy, cz) - original->GetCenterOfMass());
    body->api().SetShape(body->id, adjusted, false, JPH::EActivation::Activate);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    native->GetMotionPropertiesUnchecked()->SetMassProperties(body->dofs, properties);
    body->mass = properties;
    notifyJointShapeChanged(*body->world.lock(), body->id, adjusted->GetCenterOfMass() - previous);
    return success();
}
eve::Result<void> BodyBackend::resetMassProperties() {
    if (!isValid()) return rejected<void>("Body expired", eve::DiagnosticCode::StaleHandle);
    return replaceShapes(*body, body->shapes);
}
eve::Result<float> BodyBackend::getInertiaXX() const {
    if (!isValid()) return rejected<float>("Body expired", eve::DiagnosticCode::StaleHandle);
    return eve::Result<float>::success(
        body->api().GetMotionType(body->id) == JPH::EMotionType::Dynamic ? body->mass.mInertia(0, 0) : 0.f);
}
eve::Result<float> BodyBackend::getInertiaYY() const {
    if (!isValid()) return rejected<float>("Body expired", eve::DiagnosticCode::StaleHandle);
    return eve::Result<float>::success(
        body->api().GetMotionType(body->id) == JPH::EMotionType::Dynamic ? body->mass.mInertia(1, 1) : 0.f);
}
eve::Result<float> BodyBackend::getInertiaZZ() const {
    if (!isValid()) return rejected<float>("Body expired", eve::DiagnosticCode::StaleHandle);
    return eve::Result<float>::success(
        body->api().GetMotionType(body->id) == JPH::EMotionType::Dynamic ? body->mass.mInertia(2, 2) : 0.f);
}
eve::Result<float> BodyBackend::getInertiaXY() const {
    if (!isValid()) return rejected<float>("Body expired", eve::DiagnosticCode::StaleHandle);
    return eve::Result<float>::success(
        body->api().GetMotionType(body->id) == JPH::EMotionType::Dynamic ? body->mass.mInertia(0, 1) : 0.f);
}
eve::Result<float> BodyBackend::getInertiaXZ() const {
    if (!isValid()) return rejected<float>("Body expired", eve::DiagnosticCode::StaleHandle);
    return eve::Result<float>::success(
        body->api().GetMotionType(body->id) == JPH::EMotionType::Dynamic ? body->mass.mInertia(0, 2) : 0.f);
}
eve::Result<float> BodyBackend::getInertiaYZ() const {
    if (!isValid()) return rejected<float>("Body expired", eve::DiagnosticCode::StaleHandle);
    return eve::Result<float>::success(
        body->api().GetMotionType(body->id) == JPH::EMotionType::Dynamic ? body->mass.mInertia(1, 2) : 0.f);
}
eve::Result<float> BodyBackend::getLocalCenterX() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    return R::success(native->GetShape()->GetCenterOfMass().GetX());
}
eve::Result<float> BodyBackend::getLocalCenterY() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    return R::success(native->GetShape()->GetCenterOfMass().GetY());
}
eve::Result<float> BodyBackend::getLocalCenterZ() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    return R::success(native->GetShape()->GetCenterOfMass().GetZ());
}
eve::Result<float> BodyBackend::getWorldCenterX() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(static_cast<float>(body->api().GetCenterOfMassPosition(body->id).GetX()));
}
eve::Result<float> BodyBackend::getWorldCenterY() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(static_cast<float>(body->api().GetCenterOfMassPosition(body->id).GetY()));
}
eve::Result<float> BodyBackend::getWorldCenterZ() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(static_cast<float>(body->api().GetCenterOfMassPosition(body->id).GetZ()));
}
eve::Result<void> BodyBackend::setType(const std::string& bodyType) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    if (bodyType != "static" && bodyType != "dynamic" && bodyType != "kinematic")
        return rejected<void>("Invalid body type", eve::DiagnosticCode::InvalidArgument);
    auto type = bodyType == "static"    ? JPH::EMotionType::Static
                : bodyType == "dynamic" ? JPH::EMotionType::Dynamic
                                        : JPH::EMotionType::Kinematic;
    if (type != JPH::EMotionType::Static)
        for (const auto& s : body->shapes)
            if (s->native->MustBeStatic())
                return rejected<void>("Concave shapes require a static body", eve::DiagnosticCode::InvalidArgument);
    if (body->api().GetMotionType(body->id) == type) return success();
    // Box3D recomputes mass when changing type, including any deferred density edits.
    auto rebuilt = replaceShapes(*body, body->shapes);
    if (!rebuilt) return rebuilt;
    body->api().SetMotionType(body->id, type, JPH::EActivation::Activate);
    body->api().SetObjectLayer(body->id, type == JPH::EMotionType::Static ? 0 : 1);
    return success();
}
eve::Result<std::string> BodyBackend::getType() const {
    using R [[maybe_unused]] = eve::Result<std::string>;
    if (!isValid()) return rejected<std::string>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    return R::success(native->IsStatic() ? "static" : native->IsKinematic() ? "kinematic" : "dynamic");
}
eve::Result<void> BodyBackend::setFixedRotation(bool fixed) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto*     native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    const int next   = fixed ? (static_cast<int>(body->dofs) & 7) : (static_cast<int>(body->dofs) | 56);
    if (next == 0) return rejected<void>("Jolt requires at least one movable degree of freedom; use a static body");
    body->dofs = static_cast<JPH::EAllowedDOFs>(next);
    if (!native->GetShape()->MustBeStatic())
        native->GetMotionPropertiesUnchecked()->SetMassProperties(body->dofs, body->mass);
    return success();
}
eve::Result<bool> BodyBackend::isFixedRotation() const {
    using R [[maybe_unused]] = eve::Result<bool>;
    if (!isValid()) return rejected<bool>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success((static_cast<int>(body->dofs) & 56) == 0);
}
eve::Result<void> BodyBackend::setActive(bool active) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    if (active != body->active) {
        if (active)
            body->api().AddBody(body->id, JPH::EActivation::Activate);
        else
            body->api().RemoveBody(body->id);
        body->active = active;
    }
    return success();
}
eve::Result<bool> BodyBackend::isActive() const {
    using R [[maybe_unused]] = eve::Result<bool>;
    if (!isValid()) return rejected<bool>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(body->active);
}
eve::Result<void> BodyBackend::setBullet(bool bullet) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    body->bullet = bullet;
    body->api().SetMotionQuality(body->id, bullet && body->world.lock()->continuousCollision
                                               ? JPH::EMotionQuality::LinearCast
                                               : JPH::EMotionQuality::Discrete);
    return success();
}
eve::Result<bool> BodyBackend::isBullet() const {
    using R [[maybe_unused]] = eve::Result<bool>;
    if (!isValid()) return rejected<bool>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(body->bullet);
}
eve::Result<void> BodyBackend::setAwake(bool awake) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto* native = body->world.lock()->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
    if (body->active && !native->IsStatic()) {
        if (awake)
            body->api().ActivateBody(body->id);
        else
            body->api().DeactivateBody(body->id);
    }
    return success();
}
eve::Result<bool> BodyBackend::isAwake() const {
    using R [[maybe_unused]] = eve::Result<bool>;
    if (!isValid()) return rejected<bool>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(body->api().IsActive(body->id));
}
}  // namespace eve::physics::jolt
