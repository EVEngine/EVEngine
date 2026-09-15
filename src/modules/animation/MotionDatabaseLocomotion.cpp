#include <array>
#include <cmath>
#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimTransformInternal.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionFeatureInternal.h"
#include "animation/MotionSchemaInternal.h"

namespace eve::animation {
eve::Result<void> MotionDatabase::setLocomotionFeatures(int leftFoot, int rightFoot, int pelvis) {
    const int count = skeleton_->getBoneCount();
    if (baked_ || leftFoot < 0 || rightFoot < 0 || pelvis < 0 || leftFoot >= count || rightFoot >= count ||
        pelvis >= count || leftFoot == rightFoot || leftFoot == pelvis || rightFoot == pelvis)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "locomotion features require three distinct valid bones before bake",
            "featureBones", {}, "animation"));
    std::vector<int> bones{leftFoot, rightFoot, pelvis};
    schema_.reset();
    featureBones_.swap(bones);
    locomotionFeatures_ = true;
    computeFeatureSize();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void MotionDatabase::extractLocomotionFeature(AnimClip* clip, float time, std::vector<float>& out, float& rootX,
                                              float& rootZ, float& rootYaw, float& velX, float& velZ) const {
    out.assign(detail::locomotionFeatureCount, 0.f);
    const float duration = clip->getDuration();
    const float delta    = 1.f / 60.f;
    auto        world    = [&](auto&& self, int bone, float t) -> TransformTRS {
        const auto local  = clip->sampleBone(bone, t, skeleton_->bindLocal(bone));
        const int  parent = skeleton_->getParent(bone);
        return parent < 0 ? local : detail::mulTRS(self(self, parent, t), local);
    };
    auto poseAt = [&](int bone, float t) { return world(world, bone, clampf(clip->wrapTime(t), 0.f, duration)); };
    // Accumulate complete root transforms at loop seams; turning cycles need
    // rotation composition, not repeated addition of an unrotated displacement.
    const auto  start = world(world, rootBone_, 0.f), end = world(world, rootBone_, duration);
    const float startYaw = detail::featureYaw(start);
    const float cycleYaw = std::remainder(detail::featureYaw(end) - startYaw, 6.28318530718f);
    auto        rootAt   = [&](float t) {
        auto root = poseAt(rootBone_, t);
        if (clip->getLoop() && duration > 1e-6f) {
            const int   cycles = static_cast<int>(std::floor(t / duration));
            float       x = root.px - start.px, z = root.pz - start.pz;
            const float dx = end.px - start.px, dz = end.pz - start.pz;
            const float cs = std::cos(cycleYaw), sn = std::sin(cycleYaw);
            for (int i = 0; i < std::abs(cycles); ++i) {
                if (cycles > 0) {
                    const float nextX = cs * x + sn * z + dx;
                    z                 = -sn * x + cs * z + dz;
                    x                 = nextX;
                } else {
                    x -= dx;
                    z -= dz;
                    const float nextX = cs * x - sn * z;
                    z                 = sn * x + cs * z;
                    x                 = nextX;
                }
            }
            root.px = start.px + x;
            root.pz = start.pz + z;
            root.py += cycles * (end.py - start.py);
            const float angle = detail::featureYaw(root) + cycles * cycleYaw;
            root.qx = root.qz = 0.f;
            root.qy           = std::sin(angle * .5f);
            root.qw           = std::cos(angle * .5f);
        } else if (duration > 1e-6f && (t < 0.f || t > duration)) {
            // Root trajectory extrapolation preserves velocity outside short
            // starts/stops, instead of inventing a stationary future endpoint.
            const float dt        = std::min(delta, duration);
            const auto  a         = world(world, rootBone_, t < 0.f ? 0.f : duration - dt);
            const auto  b         = world(world, rootBone_, t < 0.f ? dt : duration);
            const float extension = (t < 0.f ? t : t - duration) / dt;
            root.px += (b.px - a.px) * extension;
            root.py += (b.py - a.py) * extension;
            root.pz += (b.pz - a.pz) * extension;
            const float angle =
                detail::featureYaw(root) +
                std::remainder(detail::featureYaw(b) - detail::featureYaw(a), 6.28318530718f) * extension;
            root.qx = root.qz = 0.f;
            root.qy           = std::sin(angle * .5f);
            root.qw           = std::cos(angle * .5f);
        }
        return root;
    };
    struct Sample {
        float x, y, z, vx, vy, vz, yaw;
    };
    std::array<Sample, 5> samples{};
    const auto            current = rootAt(time);
    rootX                         = current.px;
    rootZ                         = current.pz;
    rootYaw                       = detail::featureYaw(current);
    for (int i = 0; i < 5; ++i) {
        const float t = time + detail::locomotionHorizons[i];
        const auto  r = rootAt(t), previous = rootAt(t - delta);
        samples[i] = {r.px - current.px,
                      r.py - current.py,
                      r.pz - current.pz,
                      (r.px - previous.px) / delta,
                      (r.py - previous.py) / delta,
                      (r.pz - previous.pz) / delta,
                      detail::featureYaw(r)};
    }
    velX = samples[1].vx;
    velZ = samples[1].vz;
    detail::encodeLocomotionTrajectory(std::span(out), samples, rootYaw);
    std::array<TransformTRS, 4> now{}, past{};
    now[0]  = poseAt(rootBone_, time);
    past[0] = poseAt(rootBone_, time - delta);
    for (int i = 0; i < 3; ++i) {
        now[i + 1]  = poseAt(featureBones_[i], time);
        past[i + 1] = poseAt(featureBones_[i], time - delta);
    }
    detail::encodeLocomotionPose(out, now, past, delta);
}

void MotionDatabase::normalizeLocomotionFeatures() {
    featureMean_.assign(detail::locomotionFeatureCount, 0.f);
    featureInvStd_.assign(detail::locomotionFeatureCount, 1.f);
    // Separate channels use vector mean deviation (isotropic XZ/XYZ); the two
    // feet share a centroid and scale, as the authored FeetVelZ group specifies.
    const std::array<std::array<int, 3>, 12> channels{{{0, 2, 1},
                                                       {2, 2, 1},
                                                       {4, 2, 1},
                                                       {6, 2, 1},
                                                       {8, 2, 1},
                                                       {10, 2, 1},
                                                       {12, 2, 1},
                                                       {14, 2, 1},
                                                       {16, 3, 1},
                                                       {19, 3, 1},
                                                       {22, 3, 2},
                                                       {28, 2, 1}}};
    for (const auto& channel : channels) {
        const int             base = channel[0], dimensions = channel[1], copies = channel[2];
        std::array<double, 3> mean{};
        const double          count = static_cast<double>(frames_.size()) * copies;
        for (const auto& frame : frames_)
            for (int copy = 0; copy < copies; ++copy)
                for (int axis = 0; axis < dimensions; ++axis)
                    mean[axis] += frame.feature[base + copy * dimensions + axis];
        for (double& value : mean) value /= count;
        double deviation = 0;
        for (const auto& frame : frames_)
            for (int copy = 0; copy < copies; ++copy) {
                double square = 0;
                for (int axis = 0; axis < dimensions; ++axis) {
                    const double d = frame.feature[base + copy * dimensions + axis] - mean[axis];
                    square += d * d;
                }
                deviation += std::sqrt(square);
            }
        deviation /= count;
        const bool   unitVector = base == 4 || base == 8 || base == 14 || base == 16 || base == 28;
        const double minimum    = unitVector ? 0.1 : 0.001;  // UE 0.1 cm, converted to metres.
        const float  inverse = static_cast<float>(deviation > minimum ? 1.0 / deviation : (unitVector ? 1.0 : 100.0));
        for (int copy = 0; copy < copies; ++copy)
            for (int axis = 0; axis < dimensions; ++axis) {
                const int i       = base + copy * dimensions + axis;
                featureMean_[i]   = static_cast<float>(mean[axis]);
                featureInvStd_[i] = inverse;
            }
    }
    for (auto& frame : frames_) normalizeFeature(frame.feature);
}
}  // namespace eve::animation
