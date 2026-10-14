// Jolt requires its platform header before any other native header.
#include <Jolt/Jolt.h>

// Provider implementation dependencies.
#include <Jolt/Physics/Body/MotionProperties.h>
#include "physics/jolt/JoltInternal.h"
#include "physics/jolt/JoltJoint.h"
#include "physics/jolt/JoltWorld.h"
namespace eve::physics::jolt {
eve::Result<void> WorldBackend::setContinuousCollisionEnabled(bool enabled) {
    if (!isValid()) return rejected<void>("World expired", eve::DiagnosticCode::StaleHandle);
    state->continuousCollision = enabled;
    for (const auto& [id, body] : state->bodies)
        body->api().SetMotionQuality(
            body->id, enabled && body->bullet ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete);
    return success();
}
eve::Result<bool> WorldBackend::isContinuousCollisionEnabled() const {
    if (!isValid()) return rejected<bool>("World expired", eve::DiagnosticCode::StaleHandle);
    return eve::Result<bool>::success(state->continuousCollision);
}
eve::Result<void> WorldBackend::setRestitutionThreshold(float speed) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    if (!std::isfinite(speed) || speed < 0)
        return rejected<void>("Invalid solver setting", eve::DiagnosticCode::InvalidArgument);
    auto settings                       = state->system.GetPhysicsSettings();
    settings.mMinVelocityForRestitution = speed;
    state->system.SetPhysicsSettings(settings);
    return success();
}
eve::Result<float> WorldBackend::getRestitutionThreshold() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(state->system.GetPhysicsSettings().mMinVelocityForRestitution);
}
eve::Result<void> WorldBackend::setContactTuning(float, float, float) {
    using R [[maybe_unused]] = eve::Result<void>;
    return rejected<void>("setContactTuning is not supported");
}
eve::Result<void> WorldBackend::setContactRecycleDistance(float distance) {
    if (!isValid()) return rejected<void>("World expired", eve::DiagnosticCode::StaleHandle);
    if (!std::isfinite(distance) || distance < 0 || double(distance) * distance > FLT_MAX)
        return rejected<void>("Invalid contact persistence distance", eve::DiagnosticCode::InvalidArgument);
    auto settings                                 = state->system.GetPhysicsSettings();
    settings.mContactPointPreserveLambdaMaxDistSq = distance * distance;
    settings.mUseBodyPairContactCache             = distance > 0;
    state->system.SetPhysicsSettings(settings);
    for (const auto& [id, body] : state->bodies) body->api().InvalidateContactCache(body->id);
    return success();
}
eve::Result<float> WorldBackend::getContactRecycleDistance() const {
    if (!isValid()) return rejected<float>("World expired", eve::DiagnosticCode::StaleHandle);
    return eve::Result<float>::success(
        std::sqrt(state->system.GetPhysicsSettings().mContactPointPreserveLambdaMaxDistSq));
}
eve::Result<void> WorldBackend::setMaximumLinearSpeed(float speed) {
    if (!isValid()) return rejected<void>("World expired", eve::DiagnosticCode::StaleHandle);
    if (!std::isfinite(speed) || speed <= 0)
        return rejected<void>("Speed must be positive and finite", eve::DiagnosticCode::InvalidArgument);
    for (const auto& [id, body] : state->bodies) {
        auto* native = state->system.GetBodyLockInterfaceNoLock().TryGetBody(body->id);
        native->GetMotionPropertiesUnchecked()->SetMaxLinearVelocity(speed);
    }
    state->maximumLinearSpeed = speed;
    return success();
}
eve::Result<float> WorldBackend::getMaximumLinearSpeed() const {
    if (!isValid()) return rejected<float>("World expired", eve::DiagnosticCode::StaleHandle);
    return eve::Result<float>::success(state->maximumLinearSpeed);
}
eve::Result<void> WorldBackend::setWarmStartingEnabled(bool enabled) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    auto settings                 = state->system.GetPhysicsSettings();
    settings.mConstraintWarmStart = enabled;
    state->system.SetPhysicsSettings(settings);
    return success();
}
eve::Result<bool> WorldBackend::isWarmStartingEnabled() const {
    using R [[maybe_unused]] = eve::Result<bool>;
    if (!isValid()) return rejected<bool>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(state->system.GetPhysicsSettings().mConstraintWarmStart);
}
eve::Result<float> WorldBackend::getBoundsMinX() const {
    if (!isValid()) return rejected<float>("World expired", eve::DiagnosticCode::StaleHandle);
    const auto bounds = state->system.GetBroadPhaseQuery().GetBounds();
    return eve::Result<float>::success(bounds.IsValid() ? bounds.mMin.GetX() : 0.f);
}
eve::Result<float> WorldBackend::getBoundsMinY() const {
    if (!isValid()) return rejected<float>("World expired", eve::DiagnosticCode::StaleHandle);
    const auto bounds = state->system.GetBroadPhaseQuery().GetBounds();
    return eve::Result<float>::success(bounds.IsValid() ? bounds.mMin.GetY() : 0.f);
}
eve::Result<float> WorldBackend::getBoundsMinZ() const {
    if (!isValid()) return rejected<float>("World expired", eve::DiagnosticCode::StaleHandle);
    const auto bounds = state->system.GetBroadPhaseQuery().GetBounds();
    return eve::Result<float>::success(bounds.IsValid() ? bounds.mMin.GetZ() : 0.f);
}
eve::Result<float> WorldBackend::getBoundsMaxX() const {
    if (!isValid()) return rejected<float>("World expired", eve::DiagnosticCode::StaleHandle);
    const auto bounds = state->system.GetBroadPhaseQuery().GetBounds();
    return eve::Result<float>::success(bounds.IsValid() ? bounds.mMax.GetX() : 0.f);
}
eve::Result<float> WorldBackend::getBoundsMaxY() const {
    if (!isValid()) return rejected<float>("World expired", eve::DiagnosticCode::StaleHandle);
    const auto bounds = state->system.GetBroadPhaseQuery().GetBounds();
    return eve::Result<float>::success(bounds.IsValid() ? bounds.mMax.GetY() : 0.f);
}
eve::Result<float> WorldBackend::getBoundsMaxZ() const {
    if (!isValid()) return rejected<float>("World expired", eve::DiagnosticCode::StaleHandle);
    const auto bounds = state->system.GetBroadPhaseQuery().GetBounds();
    return eve::Result<float>::success(bounds.IsValid() ? bounds.mMax.GetZ() : 0.f);
}
eve::Result<int> WorldBackend::getBodyCount() const {
    using R [[maybe_unused]] = eve::Result<int>;
    if (!isValid()) return rejected<int>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(static_cast<int>(state->system.GetNumBodies()));
}
eve::Result<int> WorldBackend::getShapeCount() const {
    using R [[maybe_unused]] = eve::Result<int>;
    if (!isValid()) return rejected<int>("Object expired", eve::DiagnosticCode::StaleHandle);
    int count = 0;
    for (const auto& [id, body] : state->bodies) count += static_cast<int>(body->shapes.size());
    return R::success(count);
}
eve::Result<int> WorldBackend::getContactCount() const {
    using R [[maybe_unused]] = eve::Result<int>;
    if (!isValid()) return rejected<int>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(static_cast<int>(state->listener->contacts.size()));
}
eve::Result<int> WorldBackend::getJointCount() const {
    using R [[maybe_unused]] = eve::Result<int>;
    if (!isValid()) return rejected<int>("Object expired", eve::DiagnosticCode::StaleHandle);
    int count = 0;
    for (const auto& weak : state->joints)
        if (auto joint = weak.lock(); joint && joint->alive) ++count;
    return R::success(count);
}
eve::Result<int> WorldBackend::getIslandCount() const {
    using R [[maybe_unused]] = eve::Result<int>;
    return rejected<int>("getIslandCount is not supported");
}
eve::Result<int> WorldBackend::getAwakeContactCount() const {
    if (!isValid()) return rejected<int>("World expired", eve::DiagnosticCode::StaleHandle);
    int count = 0;
    for (const auto& [key, pair] : state->listener->contacts) {
        const auto* a = state->system.GetBodyLockInterfaceNoLock().TryGetBody(key.GetBody1ID());
        const auto* b = state->system.GetBodyLockInterfaceNoLock().TryGetBody(key.GetBody2ID());
        if (!pair.sensorA && !pair.sensorB && a && b && (a->IsActive() || b->IsActive())) ++count;
    }
    return eve::Result<int>::success(count);
}
eve::Result<int> WorldBackend::getRecycledContactCount() const {
    using R [[maybe_unused]] = eve::Result<int>;
    return rejected<int>("getRecycledContactCount is not supported");
}
eve::Result<int> WorldBackend::getStaticTreeHeight() const {
    using R [[maybe_unused]] = eve::Result<int>;
    return rejected<int>("getStaticTreeHeight is not supported");
}
eve::Result<int> WorldBackend::getDynamicTreeHeight() const {
    using R [[maybe_unused]] = eve::Result<int>;
    return rejected<int>("getDynamicTreeHeight is not supported");
}
eve::Result<int> WorldBackend::getMemoryByteCount() const {
    using R [[maybe_unused]] = eve::Result<int>;
    return rejected<int>("getMemoryByteCount is not supported");
}
eve::Result<int> WorldBackend::getAwakeBodyCount() const {
    using R [[maybe_unused]] = eve::Result<int>;
    if (!isValid()) return rejected<int>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(static_cast<int>(state->system.GetNumActiveBodies(JPH::EBodyType::RigidBody)));
}
eve::Result<float> WorldBackend::getProfileStepMs() const {
    if (!isValid()) return rejected<float>("World expired", eve::DiagnosticCode::StaleHandle);
    return eve::Result<float>::success(state->stepMilliseconds);
}
eve::Result<float> WorldBackend::getProfilePairsMs() const {
    using R [[maybe_unused]] = eve::Result<float>;
    return rejected<float>("getProfilePairsMs is not supported");
}
eve::Result<float> WorldBackend::getProfileCollideMs() const {
    using R [[maybe_unused]] = eve::Result<float>;
    return rejected<float>("getProfileCollideMs is not supported");
}
eve::Result<float> WorldBackend::getProfileSolveMs() const {
    using R [[maybe_unused]] = eve::Result<float>;
    return rejected<float>("getProfileSolveMs is not supported");
}
eve::Result<float> WorldBackend::getProfileBulletsMs() const {
    using R [[maybe_unused]] = eve::Result<float>;
    return rejected<float>("getProfileBulletsMs is not supported");
}
eve::Result<float> WorldBackend::getProfileSensorsMs() const {
    using R [[maybe_unused]] = eve::Result<float>;
    return rejected<float>("getProfileSensorsMs is not supported");
}
eve::Result<void> WorldBackend::setGravity(float gx, float gy, float gz) {
    using R [[maybe_unused]] = eve::Result<void>;
    if (!isValid()) return rejected<void>("Object expired", eve::DiagnosticCode::StaleHandle);
    if (!isFinite(gx, gy, gz)) return rejected<void>("Invalid gravity", eve::DiagnosticCode::InvalidArgument);
    state->system.SetGravity({gx, gy, gz});
    return success();
}
eve::Result<float> WorldBackend::getGravityX() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(state->system.GetGravity().GetX());
}
eve::Result<float> WorldBackend::getGravityY() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(state->system.GetGravity().GetY());
}
eve::Result<float> WorldBackend::getGravityZ() const {
    using R [[maybe_unused]] = eve::Result<float>;
    if (!isValid()) return rejected<float>("Object expired", eve::DiagnosticCode::StaleHandle);
    return R::success(state->system.GetGravity().GetZ());
}
eve::Result<void> WorldBackend::setHitEventThreshold(float speed) {
    if (!isValid()) return rejected<void>("World expired", eve::DiagnosticCode::StaleHandle);
    if (!std::isfinite(speed) || speed < 0)
        return rejected<void>("Invalid hit threshold", eve::DiagnosticCode::InvalidArgument);
    state->hitThreshold = speed;
    return success();
}
eve::Result<float> WorldBackend::getHitEventThreshold() const {
    if (!isValid()) return rejected<float>("World expired", eve::DiagnosticCode::StaleHandle);
    return eve::Result<float>::success(state->hitThreshold);
}
}  // namespace eve::physics::jolt
