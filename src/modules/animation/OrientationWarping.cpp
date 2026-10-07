#include "animation/OrientationWarping.h"

#include "animation/AnimMath.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace eve::animation {
namespace {
constexpr int kMaxSpineBones = 32;
constexpr int kMaxIkBones    = 16;

eve::Result<void> fail(const char* message) {
    return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, message,
                                                             "orientationWarping", {}, "animation"));
}

eve::Result<void> ok(eve::StatusCode code) {
    return eve::Result<void>::success(eve::Status::success(code));
}

float wrapPi(float radians) { return std::remainder(radians, 6.28318530718f); }

float planarSpeed(float x, float z) { return std::sqrt(x * x + z * z); }

float signedPlanarAngle(float fromX, float fromZ, float toX, float toZ) {
    return wrapPi(std::atan2(toX, toZ) - std::atan2(fromX, fromZ));
}

float interpTo(float current, float target, float dt, float speed) {
    const float delta = wrapPi(target - current);
    if (speed <= 0.f) return target;
    const float step = std::clamp(dt * speed, 0.f, 1.f);
    return wrapPi(current + delta * step);
}

void setWorldRotation(AnimPose& pose, const AnimSkeleton& skeleton, int bone, float qx, float qy, float qz, float qw) {
    const int parent = skeleton.getParent(bone);
    float     lx, ly, lz, lw;
    if (parent < 0) {
        lx = qx;
        ly = qy;
        lz = qz;
        lw = qw;
    } else {
        const auto& parentWorld = pose.world(parent);
        multiplyQuat(-parentWorld.qx, -parentWorld.qy, -parentWorld.qz, parentWorld.qw, qx, qy, qz, qw, lx, ly, lz, lw);
    }
    pose.setLocalRotation(bone, lx, ly, lz, lw);
}

void addWorldYaw(AnimPose& pose, const AnimSkeleton& skeleton, int bone, float yaw) {
    pose.computeWorld(&skeleton);
    const auto& world = pose.world(bone);
    float       qx, qy, qz, qw;
    const float half = yaw * 0.5f;
    multiplyQuat(0.f, std::sin(half), 0.f, std::cos(half), world.qx, world.qy, world.qz, world.qw, qx, qy, qz, qw);
    setWorldRotation(pose, skeleton, bone, qx, qy, qz, qw);
}

bool validBoneList(const AnimSkeleton& skeleton, int root, std::span<const int> bones, int maxCount) {
    if (bones.size() > static_cast<std::size_t>(maxCount)) return false;
    for (std::size_t i = 0; i < bones.size(); ++i) {
        if (bones[i] < 0 || bones[i] >= skeleton.getBoneCount() || bones[i] == root) return false;
        for (std::size_t j = 0; j < i; ++j)
            if (bones[i] == bones[j]) return false;
    }
    return true;
}
}  // namespace

struct OrientationWarping::Impl {
    AnimSkeleton*    skeleton = nullptr;
    int              rootBone = -1;
    std::vector<int> spineBones;
    std::vector<int> ikBones;
    float            distributedAlpha = 0.5f;
    float            angleThreshold   = 2.35619449019f;  // 135 degrees
    float            interpSpeed      = 8.f;
    float            minSpeed         = 0.1f;
    float            appliedAngle     = 0.f;
    float            targetAngle      = 0.f;
    bool             enabled          = true;
};

OrientationWarping::OrientationWarping() : impl_(std::make_unique<Impl>()) {}
OrientationWarping::~OrientationWarping() = default;

eve::Result<void> OrientationWarping::configure(AnimSkeleton& skeleton, int rootBone, std::span<const int> spineBones,
                                                std::span<const int> ikBones) {
    if (rootBone < 0 || rootBone >= skeleton.getBoneCount()) return fail("root bone must belong to the skeleton");
    if (!validBoneList(skeleton, rootBone, spineBones, kMaxSpineBones))
        return fail("spine bones must be unique valid indices distinct from the root");
    if (!validBoneList(skeleton, rootBone, ikBones, kMaxIkBones))
        return fail("ik bones must be unique valid indices distinct from the root");
    for (int spine : spineBones)
        for (int ik : ikBones)
            if (spine == ik) return fail("a bone cannot be both spine and ik");
    impl_->skeleton = &skeleton;
    impl_->rootBone = rootBone;
    impl_->spineBones.assign(spineBones.begin(), spineBones.end());
    impl_->ikBones.assign(ikBones.begin(), ikBones.end());
    impl_->appliedAngle = 0.f;
    impl_->targetAngle  = 0.f;
    return ok(eve::StatusCode::Applied);
}

eve::Result<void> OrientationWarping::setDistributedAlpha(float alpha) {
    if (!std::isfinite(alpha) || alpha < 0.f || alpha > 1.f) return fail("distributed alpha must be finite in [0,1]");
    impl_->distributedAlpha = alpha;
    return ok(eve::StatusCode::Applied);
}

float OrientationWarping::getDistributedAlpha() const { return impl_->distributedAlpha; }

eve::Result<void> OrientationWarping::setAngleThreshold(float radians) {
    if (!std::isfinite(radians) || radians <= 0.f || radians > 3.14159265359f + 1e-6f)
        return fail("angle threshold must be finite in (0, pi]");
    impl_->angleThreshold = radians;
    return ok(eve::StatusCode::Applied);
}

float OrientationWarping::getAngleThreshold() const { return impl_->angleThreshold; }

eve::Result<void> OrientationWarping::setRotationInterpSpeed(float speed) {
    if (!std::isfinite(speed) || speed < 0.f || speed > 100.f)
        return fail("rotation interp speed must be finite in [0,100]");
    impl_->interpSpeed = speed;
    return ok(eve::StatusCode::Applied);
}

float OrientationWarping::getRotationInterpSpeed() const { return impl_->interpSpeed; }

eve::Result<void> OrientationWarping::setMinRootMotionSpeed(float metresPerSecond) {
    if (!std::isfinite(metresPerSecond) || metresPerSecond < 0.f || metresPerSecond > 100.f)
        return fail("minimum root-motion speed must be finite in [0,100]");
    impl_->minSpeed = metresPerSecond;
    return ok(eve::StatusCode::Applied);
}

float OrientationWarping::getMinRootMotionSpeed() const { return impl_->minSpeed; }

void OrientationWarping::setEnabled(bool enabled) { impl_->enabled = enabled; }
bool OrientationWarping::isEnabled() const { return impl_->enabled; }

void OrientationWarping::reset() {
    impl_->appliedAngle = 0.f;
    impl_->targetAngle  = 0.f;
}

eve::Result<void> OrientationWarping::apply(AnimPose& pose, float locomotionX, float locomotionZ, float animatedX,
                                            float animatedZ, float dt) {
    if (!impl_->skeleton) return fail("configure a skeleton before applying orientation warp");
    if (pose.getBoneCount() != impl_->skeleton->getBoneCount()) return fail("pose bone count must match the skeleton");
    if (!std::isfinite(dt) || dt < 0.f || dt > 1.f) return fail("dt must be finite in [0,1]");
    for (float value : {locomotionX, locomotionZ, animatedX, animatedZ})
        if (!std::isfinite(value)) return fail("locomotion and animated velocities must be finite");
    for (int i = 0; i < pose.getBoneCount(); ++i) {
        const auto& t = pose.local(i);
        for (float value : {t.px, t.py, t.pz, t.qx, t.qy, t.qz, t.qw, t.sx, t.sy, t.sz})
            if (!std::isfinite(value)) return fail("pose locals must be finite");
    }

    float target = 0.f;
    if (planarSpeed(locomotionX, locomotionZ) >= impl_->minSpeed &&
        planarSpeed(animatedX, animatedZ) >= impl_->minSpeed) {
        const float angle = signedPlanarAngle(animatedX, animatedZ, locomotionX, locomotionZ);
        if (std::fabs(angle) <= impl_->angleThreshold) target = angle;
    }
    const float applied = interpTo(impl_->appliedAngle, target, dt, impl_->interpSpeed);
    if (!impl_->enabled) {
        impl_->targetAngle  = target;
        impl_->appliedAngle = applied;
        return ok(eve::StatusCode::NoOp);
    }
    if (std::fabs(applied) <= 1e-6f) {
        impl_->targetAngle  = target;
        impl_->appliedAngle = 0.f;
        return ok(eve::StatusCode::NoOp);
    }

    AnimPose next;
    next.copyFrom(&pose);
    next.computeWorld(impl_->skeleton);
    struct SavedRotation {
        int   bone = 0;
        float qx = 0.f, qy = 0.f, qz = 0.f, qw = 1.f;
    };
    std::vector<SavedRotation> ikWorld;
    ikWorld.reserve(impl_->ikBones.size());
    for (int bone : impl_->ikBones) {
        const auto& world = next.world(bone);
        ikWorld.push_back({bone, world.qx, world.qy, world.qz, world.qw});
    }

    const float spineShare = impl_->spineBones.empty() ? 0.f : impl_->distributedAlpha;
    addWorldYaw(next, *impl_->skeleton, impl_->rootBone, applied * (1.f - spineShare));
    if (!impl_->spineBones.empty()) {
        const float perBone = applied * spineShare / static_cast<float>(impl_->spineBones.size());
        for (int bone : impl_->spineBones) addWorldYaw(next, *impl_->skeleton, bone, perBone);
    }
    next.computeWorld(impl_->skeleton);
    for (const auto& saved : ikWorld)
        setWorldRotation(next, *impl_->skeleton, saved.bone, saved.qx, saved.qy, saved.qz, saved.qw);
    next.computeWorld(impl_->skeleton);

    pose.copyFrom(&next);
    impl_->targetAngle  = target;
    impl_->appliedAngle = applied;
    return ok(eve::StatusCode::Applied);
}

float OrientationWarping::getAppliedAngle() const { return impl_->appliedAngle; }
float OrientationWarping::getTargetAngle() const { return impl_->targetAngle; }
AnimSkeleton* OrientationWarping::getSkeleton() const { return impl_->skeleton; }
int           OrientationWarping::getRootBone() const { return impl_->rootBone; }
int           OrientationWarping::getSpineBoneCount() const { return static_cast<int>(impl_->spineBones.size()); }
int           OrientationWarping::getIkBoneCount() const { return static_cast<int>(impl_->ikBones.size()); }
}  // namespace eve::animation
