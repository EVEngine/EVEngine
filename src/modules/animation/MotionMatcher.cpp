#include "animation/MotionMatcher.h"

#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimationTime.h"
#include "animation/MotionDatabase.h"

#include "common/Exception.h"

#include <cmath>
#include <limits>

namespace eve::animation {

MotionMatcher::MotionMatcher(AnimSkeleton* skeleton, MotionDatabase* database)
    : skeleton_(skeleton), database_(database) {
    if (!skeleton_) throw Exception("MotionMatcher: skeleton is null");
    if (!database_) throw Exception("MotionMatcher: database is null");
    if (database_->getSkeleton() != skeleton_) {
        throw Exception("MotionMatcher: database skeleton mismatch");
    }
    pose_.resize(skeleton_->getBoneCount());
    fromPose_.resize(skeleton_->getBoneCount());
    matchedPose_.resize(skeleton_->getBoneCount());
    skeleton_->applyBindPose(&pose_);
}

void MotionMatcher::setDesiredVelocity(float x, float z) {
    desiredVelX_ = x;
    desiredVelZ_ = z;
}

void MotionMatcher::setDesiredYaw(float yaw) { desiredYaw_ = yaw; }

void MotionMatcher::setSearchInterval(float seconds) {
    if (seconds < 0.f) throw Exception("MotionMatcher.setSearchInterval: must be >= 0");
    searchInterval_ = seconds;
}

void MotionMatcher::setBlendTime(float seconds) {
    if (seconds < 0.f) throw Exception("MotionMatcher.setBlendTime: must be >= 0");
    blendTime_ = seconds;
}

void MotionMatcher::setTrajectoryWeight(float w) {
    if (w < 0.f) throw Exception("MotionMatcher.setTrajectoryWeight: must be >= 0");
    trajWeight_ = w;
}

void MotionMatcher::setPoseWeight(float w) {
    if (w < 0.f) throw Exception("MotionMatcher.setPoseWeight: must be >= 0");
    poseWeight_ = w;
}

void MotionMatcher::setVelocityWeight(float w) {
    if (w < 0.f) throw Exception("MotionMatcher.setVelocityWeight: must be >= 0");
    velWeight_ = w;
}

void MotionMatcher::setIgnoreRadius(int frames) {
    if (frames < 0) throw Exception("MotionMatcher.setIgnoreRadius: must be >= 0");
    ignoreRadius_ = frames;
}

int MotionMatcher::getMatchedClipIndex() const {
    if (matchedFrame_ < 0 || !database_->isBaked()) return -1;
    return database_->getFrameClipIndex(matchedFrame_);
}

AnimPose* MotionMatcher::getPose() { return &pose_; }

void MotionMatcher::buildQuery(std::vector<float>& query) const {
    if (!database_->isBaked()) throw Exception("MotionMatcher: database not baked");
    const int n = database_->getFeatureSize();
    query.assign(static_cast<size_t>(n), 0.f);

    // Character-space desired velocity (assume current facing = desiredYaw for query).
    const float cs = std::cos(desiredYaw_);
    const float sn = std::sin(desiredYaw_);
    query[0]       = desiredVelX_ * cs + desiredVelZ_ * sn;
    query[1]       = -desiredVelX_ * sn + desiredVelZ_ * cs;

    // Desired trajectory: integrate constant velocity for 0.33/0.66/1.0s in char space.
    const float horizons[3] = {0.33f, 0.66f, 1.0f};
    for (int h = 0; h < 3; ++h) {
        query[static_cast<size_t>(2 + h * 2)]     = query[0] * horizons[h];
        query[static_cast<size_t>(2 + h * 2 + 1)] = query[1] * horizons[h];
    }
    // Facing at +1s: same as desired (relative facing 0 → forward +Z in char space).
    float fx, fz;
    yawToForward(0.f, fx, fz);
    query[8] = fx;
    query[9] = fz;

    // Pose features from current pose (character-relative).
    AnimPose cur;
    cur.copyFrom(&pose_);
    cur.computeWorld(skeleton_);
    const int   root  = database_->getRootBone();
    const float rootX = cur.getWorldPositionX(root);
    const float rootZ = cur.getWorldPositionZ(root);
    // Estimate current yaw from root rotation.
    const float qy  = cur.getWorldRotationY(root);
    const float qw  = cur.getWorldRotationW(root);
    const float yaw = std::atan2(2.f * (qw * qy), 1.f - 2.f * (qy * qy));
    const float c2  = std::cos(yaw);
    const float s2  = std::sin(yaw);

    int base = 10;
    for (int i = 0; i < database_->getFeatureBoneCount(); ++i) {
        const int   b                        = database_->getFeatureBone(i);
        const float dx                       = cur.getWorldPositionX(b) - rootX;
        const float dz                       = cur.getWorldPositionZ(b) - rootZ;
        const float lx                       = dx * c2 + dz * s2;
        const float lz                       = -dx * s2 + dz * c2;
        query[static_cast<size_t>(base)]     = lx;
        query[static_cast<size_t>(base + 1)] = cur.getWorldPositionY(b);
        query[static_cast<size_t>(base + 2)] = lz;
        base += 3;
    }
    database_->normalizeFeature(query);
}

float MotionMatcher::cost(const std::vector<float>& query, const std::vector<float>& cand, float upperBound) const {
    const int n   = static_cast<int>(query.size());
    float     sum = 0.f;
    for (int i = 0; i < n; ++i) {
        const float d = query[static_cast<size_t>(i)] - cand[static_cast<size_t>(i)];
        float       w = poseWeight_;
        if (i < 2)
            w = velWeight_;
        else if (i < 10)
            w = trajWeight_;
        sum += w * d * d;
        if (sum >= upperBound) return sum;
    }
    return sum;
}

void MotionMatcher::sampleMatched(AnimPose* out) const {
    if (matchedFrame_ < 0) {
        skeleton_->applyBindPose(out);
        return;
    }
    const int ci   = database_->getFrameClipIndex(matchedFrame_);
    AnimClip* clip = database_->getClip(ci);
    clip->sample(matchedTime_, out, skeleton_);
}

void MotionMatcher::search() {
    if (!database_->isBaked()) throw Exception("MotionMatcher.search: database not baked");
    if (database_->getFrameCount() == 0) {
        throw Exception("MotionMatcher.search: empty database");
    }

    std::vector<float> query;
    buildQuery(query);

    float bestCost = std::numeric_limits<float>::infinity();
    int   best     = 0;
    for (int i = 0; i < database_->getFrameCount(); ++i) {
        if (matchedFrame_ >= 0 && std::abs(i - matchedFrame_) <= ignoreRadius_) {
            // Still allow if different clip continues — but prefer diversity slightly.
            // Skip only exact-neighborhood of current match when already playing.
            if (playing_ && database_->getFrameClipIndex(i) == getMatchedClipIndex()) {
                continue;
            }
        }
        const auto& f = database_->frameAt(i);
        const float c = cost(query, f.feature, bestCost);
        if (c < bestCost) {
            bestCost = c;
            best     = i;
        }
    }

    // If everything skipped, fall back to exhaustive.
    if (!std::isfinite(bestCost)) {
        for (int i = 0; i < database_->getFrameCount(); ++i) {
            const auto& f = database_->frameAt(i);
            const float c = cost(query, f.feature, bestCost);
            if (c < bestCost) {
                bestCost = c;
                best     = i;
            }
        }
    }

    lastCost_ = bestCost;
    if (best != matchedFrame_) {
        fromPose_.copyFrom(&pose_);
        matchedFrame_ = best;
        matchedTime_  = database_->getFrameTime(best);
        sampleMatched(&matchedPose_);
        if (blendTime_ > 1e-6f && playing_) {
            blending_     = true;
            blendElapsed_ = 0.f;
        } else {
            pose_.copyFrom(&matchedPose_);
            blending_ = false;
        }
        playing_ = true;
    }
}

void MotionMatcher::updateUnchecked(float dt) {
    if (dt < 0.f) throw Exception("MotionMatcher.update: dt must be >= 0");
    if (!database_->isBaked()) return;

    if (!playing_) {
        search();
        searchTimer_ = 0.f;
    } else {
        searchTimer_ += dt;
        if (searchTimer_ >= searchInterval_) {
            searchTimer_ = 0.f;
            search();
        }
    }

    if (matchedFrame_ >= 0) {
        matchedTime_ += dt;
        sampleMatched(&matchedPose_);
    }

    if (blending_) {
        blendElapsed_ += dt;
        float t = blendTime_ > 1e-8f ? blendElapsed_ / blendTime_ : 1.f;
        if (t >= 1.f) {
            blending_ = false;
            pose_.copyFrom(&matchedPose_);
        } else {
            pose_.blendFrom(&fromPose_, &matchedPose_, t);
        }
    } else if (matchedFrame_ >= 0) {
        pose_.copyFrom(&matchedPose_);
    }
}

eve::Result<void> MotionMatcher::advance(const eve::SimulationStep& step) {
    auto seconds = detail::secondsForStep(step, hasLastTick_, lastTick_, "MotionMatcher");
    if (!seconds) return eve::Result<void>::failure(seconds.status());
    updateUnchecked(std::move(seconds).takeValue());
    lastTick_    = step.tick;
    hasLastTick_ = true;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void MotionMatcher::update(float dt) {
    auto step = detail::legacyStep(dt, hasLastTick_, lastTick_, "MotionMatcher");
    if (!step) {
        step.ignore("legacy MotionMatcher update");
        return;
    }
    advance(std::move(step).takeValue()).ignore("legacy MotionMatcher update");
}

}  // namespace eve::animation
