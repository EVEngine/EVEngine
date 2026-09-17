#include <algorithm>
#include <cmath>
#include <limits>
#include "animation/AnimClip.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionMatcher.h"

namespace eve::animation {
eve::Result<int> MotionMatcher::setCandidateRanges(std::span<const MotionSearchRange> ranges) {
    auto fail = [](const char* message) {
        return eve::Result<int>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, message, "candidateRanges", {}, "animation"));
    };
    if (!database_->isBaked() || ranges.empty() || ranges.size() > 100000)
        return fail("candidate selection requires a baked database and bounded nonempty ranges");
    std::vector<std::vector<MotionSearchRange>> perClip(database_->getClipCount());
    std::size_t intervalCount = 0;
    for (const auto& r : ranges) {
        if (r.clipIndex < 0 || r.clipIndex >= database_->getClipCount() || !std::isfinite(r.start) ||
            !std::isfinite(r.end) || !std::isfinite(r.costBias) || r.start < 0.f || r.end < r.start ||
            r.end > database_->getClip(r.clipIndex)->getDuration() + 0.001f)
            return fail("invalid candidate clip, time interval, or cost bias");
        intervalCount += r.transitionBlocks.size() + r.costOverrides.size() + r.continuingCostOverrides.size();
        if (intervalCount > 100000) return fail("too many candidate metadata intervals");
        for (const auto& block : r.transitionBlocks)
            if (!std::isfinite(block.start) || !std::isfinite(block.end) || block.start < 0.f ||
                block.end <= block.start)
                return fail("invalid transition block interval");
        if (!std::isfinite(r.continuingCostBias)) return fail("invalid continuing cost bias");
        for (const auto* overrides : {&r.costOverrides, &r.continuingCostOverrides})
            for (const auto& cost : *overrides)
                if (!std::isfinite(cost.start) || !std::isfinite(cost.end) || !std::isfinite(cost.costBias) ||
                    cost.start < 0.f || cost.end <= cost.start)
                    return fail("invalid cost override interval");
        perClip[r.clipIndex].push_back(r);
    }
    const int                  count = database_->getFrameCount();
    std::vector<int>           selected;
    std::vector<float>         bias(count, 0.f);
    std::vector<float>         continuingBias(count, 0.f);
    std::vector<unsigned char> mask(count, 0), noReselect(database_->getClipCount(), 0);
    std::vector<unsigned char> blocked(count, 0);
    for (int clipIndex = 0; clipIndex < database_->getClipCount(); ++clipIndex) {
        const auto& clipRanges = perClip[clipIndex];
        if (clipRanges.empty()) continue;
        const int begin = database_->clipFrameOffsets_[static_cast<std::size_t>(clipIndex)];
        const int end = database_->clipFrameOffsets_[static_cast<std::size_t>(clipIndex + 1)];
        for (int i = begin; i < end; ++i) {
            const auto& frame = database_->frameAt(i);
            float       best  = std::numeric_limits<float>::infinity();
            for (const auto& r : clipRanges) {
                for (const auto& block : r.transitionBlocks)
                    if (frame.time >= block.start && frame.time < block.end) blocked[i] = 1;
                if (frame.time + 1e-5f < r.start || frame.time - 1e-5f > r.end) continue;
                float rangeBias = r.costBias;
                for (const auto& cost : r.costOverrides)
                    if (frame.time >= cost.start && frame.time < cost.end) rangeBias = cost.costBias;
                float rangeContinuingBias = r.continuingCostBias;
                for (const auto& cost : r.continuingCostOverrides)
                    if (frame.time >= cost.start && frame.time < cost.end) rangeContinuingBias = cost.costBias;
                if (rangeBias < best) {
                    best = rangeBias;
                    continuingBias[i] = rangeContinuingBias;
                }
                if (r.disableReselection) noReselect[frame.clipIndex] = 1;
            }
            if (!std::isfinite(best)) continue;
            selected.push_back(i);
            mask[i] = 1;
            bias[i] = best;
        }
    }
    if (selected.empty()) return fail("candidate selection contains no sampled frames");
    candidateFrames_.swap(selected);
    candidateBias_.swap(bias);
    candidateContinuingBias_.swap(continuingBias);
    candidateMask_.swap(mask);
    transitionBlocked_.swap(blocked);
    disableReselection_.swap(noReselect);
    filtered_ = true;
    return eve::Result<int>::success(static_cast<int>(candidateFrames_.size()),
                                     eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> MotionMatcher::setQueryPose(const AnimPose& pose) {
    if (database_->hasLocomotionFeatures() || database_->hasFeatureLayout() || pose.getBoneCount() != pose_.getBoneCount())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "basic query pose requires the basic layout and matching bone count",
                                                                 "queryPose", {}, "animation"));
    queryPose_.copyFrom(&pose);
    hasQueryPose_ = true;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> MotionMatcher::setTrajectory(std::span<const MotionTrajectorySample> samples) {
    if (database_->hasLocomotionFeatures() || database_->hasFeatureLayout() || samples.size() != 3 || std::any_of(samples.begin(), samples.end(), [](const auto& s) {
            return !std::isfinite(s.x) || !std::isfinite(s.z) || !std::isfinite(s.yaw);
        }))
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "trajectory requires three finite samples",
                                                                 "trajectory", {}, "animation"));
    std::copy(samples.begin(), samples.end(), trajectory_.begin());
    hasTrajectory_ = true;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}
}  // namespace eve::animation
