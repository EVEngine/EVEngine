#include <algorithm>
#include <cmath>
#include "animation/MotionDatabase.h"
#include "animation/MotionFeatureInternal.h"
#include "animation/MotionMatcher.h"
#include "common/Exception.h"

namespace eve::animation {
eve::Result<void> MotionMatcher::setLocomotionQuery(const AnimPose& current, const AnimPose& previous,
                                                    float                                   elapsedSeconds,
                                                    std::span<const MotionLocomotionSample> samples) {
    auto finitePose = [](const AnimPose& pose) {
        for (int i = 0; i < pose.getBoneCount(); ++i) {
            const auto& t = pose.local(i);
            for (float v : {t.px, t.py, t.pz, t.qx, t.qy, t.qz, t.qw, t.sx, t.sy, t.sz})
                if (!std::isfinite(v)) return false;
        }
        return true;
    };
    if (!database_->hasLocomotionFeatures() || current.getBoneCount() != pose_.getBoneCount() ||
        previous.getBoneCount() != pose_.getBoneCount() || !std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.f ||
        elapsedSeconds > 1.f || samples.size() != 5 || !finitePose(current) || !finitePose(previous) ||
        std::any_of(samples.begin(), samples.end(), [](const auto& s) {
            for (float v : {s.x, s.y, s.z, s.vx, s.vy, s.vz, s.yaw})
                if (!std::isfinite(v)) return true;
            return false;
        }))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "locomotion query requires the locomotion layout, finite matching poses, five samples, and dt in (0,1]",
            "locomotionQuery", {}, "animation"));
    AnimPose nextCurrent, nextPrevious;
    nextCurrent.copyFrom(&current);
    nextPrevious.copyFrom(&previous);
    queryPose_         = std::move(nextCurrent);
    previousQueryPose_ = std::move(nextPrevious);
    std::copy(samples.begin(), samples.end(), locomotionTrajectory_.begin());
    queryPoseInterval_  = elapsedSeconds;
    hasLocomotionQuery_ = true;
    hasQueryPose_       = true;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void MotionMatcher::buildLocomotionQuery(std::vector<float>& query) const {
    if (!hasLocomotionQuery_) throw Exception("MotionMatcher: configure a locomotion query before searching");
    query.assign(detail::locomotionFeatureCount, 0.f);
    detail::encodeLocomotionTrajectory(std::span(query), locomotionTrajectory_, desiredYaw_);
    AnimPose current, previous;
    current.copyFrom(&queryPose_);
    previous.copyFrom(&previousQueryPose_);
    current.computeWorld(skeleton_);
    previous.computeWorld(skeleton_);
    std::array<TransformTRS, 4> now{}, past{};
    now[0]  = current.world(database_->getRootBone());
    past[0] = previous.world(database_->getRootBone());
    for (int i = 0; i < 3; ++i) {
        const int bone = database_->getFeatureBone(i);
        now[i + 1]     = current.world(bone);
        past[i + 1]    = previous.world(bone);
    }
    detail::encodeLocomotionPose(query, now, past, queryPoseInterval_);
    database_->normalizeFeature(query);
}
}  // namespace eve::animation
