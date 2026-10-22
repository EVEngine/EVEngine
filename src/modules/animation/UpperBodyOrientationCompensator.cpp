#include "animation/UpperBodyOrientationCompensator.h"

#include "animation/AnimSkeleton.h"

#include <cmath>
#include <utility>

namespace eve::animation {
namespace {

struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

Quat rotation(const TransformTRS& value) { return {value.qx, value.qy, value.qz, value.qw}; }
Quat inverse(const Quat& value) { return {-value.x, -value.y, -value.z, value.w}; }
Quat multiply(const Quat& a, const Quat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Result<void> profileError(std::string message, std::string path) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

bool isDescendant(const AnimSkeleton& skeleton, int bone, int ancestor) {
    for (int current = bone; current >= 0; current = skeleton.getParent(current))
        if (current == ancestor) return true;
    return false;
}

}  // namespace

UpperBodyOrientationCompensator::UpperBodyOrientationCompensator(const AnimSkeleton& skeleton) noexcept
    : skeleton_(skeleton) {}

Result<void> UpperBodyOrientationCompensator::configure(const UpperBodyOrientationProfile& profile) {
    const int root  = skeleton_.findBone(profile.rootBone);
    const int hips  = skeleton_.findBone(profile.hipsBone);
    const int spine = skeleton_.findBone(profile.spineBone);
    if (root < 0) return profileError("upper-body orientation root bone was not found", "profile.rootBone");
    if (hips < 0) return profileError("upper-body orientation hips bone was not found", "profile.hipsBone");
    if (spine < 0) return profileError("upper-body orientation spine bone was not found", "profile.spineBone");
    if (!isDescendant(skeleton_, hips, root))
        return profileError("upper-body orientation hips bone must descend from root", "profile.hipsBone");
    if (skeleton_.getParent(spine) != hips)
        return profileError("upper-body orientation spine bone must be a direct child of hips", "profile.spineBone");

    rootBone_   = root;
    hipsBone_   = hips;
    spineBone_  = spine;
    configured_ = true;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

void UpperBodyOrientationCompensator::clear() noexcept {
    rootBone_ = hipsBone_ = spineBone_ = -1;
    configured_                     = false;
}

Result<void> UpperBodyOrientationCompensator::apply(AnimPose& pose, AnimPose& upperBodyPose, float weight) const {
    if (!configured_)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                                        "upper-body orientation profile is not configured",
                                                        "profile"));
    if (pose.getBoneCount() != skeleton_.getBoneCount() ||
        upperBodyPose.getBoneCount() != skeleton_.getBoneCount())
        return profileError("upper-body orientation poses must match the configured skeleton", "pose");
    if (!std::isfinite(weight) || weight < 0.0f || weight > 1.0f)
        return profileError("upper-body orientation weight must be finite and within [0,1]", "weight");
    if (weight <= 0.0f) return Result<void>::success(Status::success(StatusCode::NoOp));

    upperBodyPose.computeWorld(&skeleton_);
    pose.computeWorld(&skeleton_);
    const Quat targetInRoot = multiply(inverse(rotation(upperBodyPose.world(rootBone_))),
                                       rotation(upperBodyPose.world(spineBone_)));
    const Quat targetWorld = multiply(rotation(pose.world(rootBone_)), targetInRoot);
    const Quat targetLocal = multiply(inverse(rotation(pose.world(hipsBone_))), targetWorld);

    TransformTRS& spine = pose.local(spineBone_);
    slerpQuat(spine.qx, spine.qy, spine.qz, spine.qw, targetLocal.x, targetLocal.y, targetLocal.z, targetLocal.w,
              weight, spine.qx, spine.qy, spine.qz, spine.qw);
    spine.normalizeRotation();
    pose.computeWorld(&skeleton_);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

}  // namespace eve::animation
