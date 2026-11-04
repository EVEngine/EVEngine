#include "animation/AnimInspection.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"

namespace eve::animation {
AnimInspectionBuilder::AnimInspectionBuilder(std::vector<AnimInspectionTime> times) : times_(std::move(times)) {}

Result<std::vector<AnimInspectionFrame>> AnimInspectionBuilder::build(const AnimClip&     clip,
                                                                      const AnimSkeleton& skeleton,
                                                                      const AnimSkin&     skin) const {
    using Output = std::vector<AnimInspectionFrame>;
    auto fail    = [](const char* message) {
        return Result<Output>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, message, "animation.inspection"));
    };
    const int bones = skeleton.getBoneCount(), joints = skin.getBoneCount();
    if (times_.empty() || times_.size() > 16 || bones <= 0 || bones > 4096 || joints <= 0 || joints > 4096 ||
        clip.getTrackCount() > bones)
        return fail("Inspection requires 1..16 samples, 1..4096 bones/joints and compatible clip tracks");
    if (!std::isfinite(clip.getDuration()) || clip.getDuration() <= 0.f)
        return fail("Clip duration must be finite and positive");
    double previous = -1.;
    for (const auto& time : times_) {
        if (!std::isfinite(time.timelineTime) || !std::isfinite(time.clipTime) || time.timelineTime < 0. ||
            time.timelineTime > 1e6 || time.timelineTime <= previous || time.clipTime < 0. ||
            time.clipTime > double(clip.getDuration()))
            return fail("Sample times must be finite, strictly ordered on the timeline and inside the clip");
        previous = time.timelineTime;
    }
    std::vector<Mat4> inverseBinds(size_t(joints), Mat4::identity());
    for (int joint = 0; joint < joints; ++joint) {
        const int bone = skin.getSkeletonBone(joint);
        if (bone < 0 || bone >= bones || skin.getSkinBoneName(joint) != skeleton.getBoneName(bone))
            return fail("Skin joint identity does not match the supplied skeleton");
        for (int element = 0; element < 16; ++element) {
            const float value = skin.getInverseBindElement(joint, element);
            if (!std::isfinite(value)) return fail("Inverse bind matrix is non-finite");
            inverseBinds[size_t(joint)].m[element] = value;
        }
    }
    Output frames;
    frames.reserve(times_.size());
    AnimPose pose;
    for (const auto& time : times_) {
        // Use the canonical sampler with already-resolved time: do not change clip.loop
        // or call AnimPlayer::setTime/advance, which would affect live playback.
        clip.sampleClamped(float(time.clipTime), &pose, &skeleton);
        pose.computeWorld(&skeleton);
        AnimInspectionFrame frame{time, std::vector<float>(size_t(joints) * 16)};
        for (int joint = 0; joint < joints; ++joint) {
            const auto matrix =
                Mat4::mul(Mat4::fromTRS(pose.world(skin.getSkeletonBone(joint))), inverseBinds[size_t(joint)]);
            for (float value : matrix.m)
                if (!std::isfinite(value)) return fail("Sampled palette contains non-finite values");
            if (matrix.m[3] != 0.f || matrix.m[7] != 0.f || matrix.m[11] != 0.f || matrix.m[15] != 1.f)
                return fail("Sampled skin matrix must be affine");
            std::copy_n(matrix.m, 16, frame.palette.begin() + size_t(joint) * 16);
        }
        frames.push_back(std::move(frame));
    }
    return Result<Output>::success(std::move(frames));
}
}  // namespace eve::animation
