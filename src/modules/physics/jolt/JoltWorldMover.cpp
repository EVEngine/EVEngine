// Jolt requires its platform header before any other native header.
#include <Jolt/Jolt.h>

// Provider implementation dependencies.
#include "physics/World3D.h"
#include "physics/jolt/JoltQueryInternal.h"
#include "physics/jolt/JoltWorld.h"
namespace eve::physics::jolt {
eve::Result<void> WorldBackend::setMoverUp(float x, float y, float z) {
    const JPH::Vec3 up(x, y, z);
    if (!isFinite(x, y, z) || !std::isfinite(up.LengthSq()) || up.LengthSq() < 1.e-12f)
        return rejected<void>("Invalid mover up", eve::DiagnosticCode::InvalidArgument);
    const auto unit = up.Normalized();
    moverUp         = {unit.GetX(), unit.GetY(), unit.GetZ()};
    return success();
}
eve::Result<CapsuleMove3D> WorldBackend::moveCapsuleOwned(float ax, float ay, float az, float bx, float by, float bz,
                                                          float radius, float dx, float dy, float dz,
                                                          QueryFilter3D filter, CapsuleMovePolicy3D policy) {
    if (!isValid()) return rejected<CapsuleMove3D>("World expired", eve::DiagnosticCode::StaleHandle);
    auto cooked = capsuleVolume(ax, ay, az, bx, by, bz, radius);
    if (!cooked) return eve::Result<CapsuleMove3D>::failure(cooked.status());
    JPH::Vec3 up(policy.upX, policy.upY, policy.upZ), desired(dx, dy, dz);
    if (!isFinite(dx, dy, dz) || !std::isfinite(desired.LengthSq()) || !isFinite(policy.upX, policy.upY, policy.upZ) ||
        !std::isfinite(up.LengthSq()) || up.LengthSq() < 1.e-12f || !std::isfinite(policy.maxSlopeRadians) ||
        policy.maxSlopeRadians < 0 || policy.maxSlopeRadians >= JPH::JPH_PI * .5f)
        return rejected<CapsuleMove3D>("Invalid mover policy or displacement", eve::DiagnosticCode::InvalidArgument);
    up                   = up.Normalized();
    auto          volume = std::move(cooked).takeValue();
    const auto    start  = volume.transform.GetTranslation();
    const auto    target = start + desired;
    CapsuleMove3D result;
    result.world         = owner->runtimeHandle();
    JPH::Vec3 bestNormal = JPH::Vec3::sZero();
    float     bestDot = 0.f, groundDot = -1.f;
    auto      record = [&](JPH::Vec3Arg normal) {
        ++result.planeCount;
        result.constrained = true;
        groundDot          = std::max(groundDot, normal.Dot(up));
        if (normal.Dot(desired) < bestDot) {
            bestDot    = normal.Dot(desired);
            bestNormal = normal;
        }
    };
    // Query-only controller: no body is created, no impulses or callbacks are dispatched.
    // Each pass resolves initial penetration, clips against nearby planes, then sweeps continuously.
    for (int pass = 0; pass < 8; ++pass) {
        result.iterations    = pass + 1;
        auto      overlaps   = overlapVolume(*state, volume, .002f);
        JPH::Vec3 correction = JPH::Vec3::sZero();
        auto      remaining  = JPH::Vec3(target - volume.transform.GetTranslation());
        for (const auto& hit : overlaps.mHits) {
            if (!isAcceptedHit(*state, hit.mBodyID2, hit.mSubShapeID2, filter)) continue;
            const auto normal = -hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY());
            record(normal);
            const float needed = hit.mPenetrationDepth + .0001f - correction.Dot(normal);
            if (needed > 0) correction += needed * normal;
            const float inward = remaining.Dot(normal);
            if (inward < 0) remaining -= inward * normal;
        }
        volume.transform.SetTranslation(volume.transform.GetTranslation() + correction);
        if (remaining.LengthSq() < 1.e-10f) break;
        auto      hits     = sweepVolume(*state, volume, remaining);
        float     fraction = 1.f;
        JPH::Vec3 normal   = JPH::Vec3::sZero();
        for (const auto& hit : hits.mHits) {
            if (!isAcceptedHit(*state, hit.mBodyID2, hit.mSubShapeID2, filter)) continue;
            auto n = -hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY());
            if (n.Dot(remaining) >= -1.e-7f) continue;
            fraction = std::max(0.f, hit.mFraction - .0001f / remaining.Length());
            normal   = n;
            record(n);
            break;
        }
        volume.transform.SetTranslation(volume.transform.GetTranslation() + remaining * fraction);
        if (fraction >= 1.f) break;
        // The next overlap includes the swept surface and clips the remaining displacement along it.
        if (fraction == 0 && correction.LengthSq() < 1.e-12f &&
            std::abs(normal.Dot(remaining)) >= remaining.Length() - .0001f)
            break;
    }
    // Classify ground at the final position too, including a purely tangential movement.
    for (const auto& hit : overlapVolume(*state, volume, .002f).mHits)
        if (isAcceptedHit(*state, hit.mBodyID2, hit.mSubShapeID2, filter))
            groundDot = std::max(groundDot, -hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY()).Dot(up));
    auto delta             = JPH::Vec3(volume.transform.GetTranslation() - start);
    result.deltaX          = delta.GetX();
    result.deltaY          = delta.GetY();
    result.deltaZ          = delta.GetZ();
    result.normalX         = bestNormal.GetX();
    result.normalY         = bestNormal.GetY();
    result.normalZ         = bestNormal.GetZ();
    result.grounded        = groundDot >= std::cos(policy.maxSlopeRadians);
    owner->moverGroundDot_ = groundDot;
    return eve::Result<CapsuleMove3D>::success(result);
}
eve::Result<bool> WorldBackend::moveCapsule(float ax, float ay, float az, float bx, float by, float bz, float radius,
                                            float dx, float dy, float dz) {
    auto moved = moveCapsuleOwned(
        ax, ay, az, bx, by, bz, radius, dx, dy, dz,
        {owner->queryCategoryBits_, owner->queryMaskBits_, owner->queryIgnoredBodyId_, owner->queryIgnoredShapeId_},
        {moverUp.x, moverUp.y, moverUp.z, std::acos(owner->moverSlopeCos_)});
    if (!moved) return eve::Result<bool>::failure(moved.status());
    const auto& r           = moved.value();
    owner->moverDeltaX_     = r.deltaX;
    owner->moverDeltaY_     = r.deltaY;
    owner->moverDeltaZ_     = r.deltaZ;
    owner->moverNormalX_    = r.normalX;
    owner->moverNormalY_    = r.normalY;
    owner->moverNormalZ_    = r.normalZ;
    owner->moverPlaneCount_ = r.planeCount;
    owner->moverIterations_ = r.iterations;
    owner->moverGrounded_   = r.grounded;
    return eve::Result<bool>::success(r.constrained);
}
}  // namespace eve::physics::jolt
