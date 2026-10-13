#include "animation/MotionMatcher.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimationTime.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionFeatureInternal.h"
#include "animation/MotionSchemaInternal.h"
#include "animation/PoseInertiaInternal.h"
#include "common/Exception.h"

// MotionMatcher.cpp
namespace eve::animation {

MotionMatcher::MotionMatcher(AnimSkeleton* skeleton, MotionDatabase* database)
    : skeleton_(skeleton), database_(database) {
    if (!skeleton_) throw Exception("MotionMatcher: skeleton is null");
    if (!database_) throw Exception("MotionMatcher: database is null");
    if (database_->getSkeleton() != skeleton_) {
        throw Exception("MotionMatcher: database skeleton mismatch");
    }
    pose_.resize(skeleton_->getBoneCount());
    inertia_ = std::make_unique<detail::PoseInertia>();
    matchedPose_.resize(skeleton_->getBoneCount());
    skeleton_->applyBindPose(&pose_);
}

MotionMatcher::~MotionMatcher() = default;

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

eve::Result<void> MotionMatcher::setPlayRateRange(float minimum, float maximum) {
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum <= 0.f || maximum < minimum || maximum > 10.f)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "play rate range requires finite 0 < minimum <= maximum <= 10",
            "playRateRange", {}, "animation"));
    playRateMin_ = minimum;
    playRateMax_ = maximum;
    playRate_    = std::clamp(playRate_, minimum, maximum);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void MotionMatcher::updatePlayRate(int frame) {
    float estimated = 1.f;
    if (database_->schema_ && frame >= 0) {
        const float poseSpeed = database_->frameAt(frame).trajectorySpeed;
        if (poseSpeed > 1e-4f) estimated = queryTrajectorySpeed_ / poseSpeed;
    }
    playRate_ = std::clamp(estimated, playRateMin_, playRateMax_);
}

eve::Result<void> MotionMatcher::setPoseReselectHistory(float seconds) {
    if (!std::isfinite(seconds) || seconds < 0.f || seconds > 10.f)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "pose reselect history must be finite and between zero and ten seconds",
                                   "poseReselectHistory", {}, "animation"));
    poseReselectHistory_ = seconds;
    if (seconds == 0.f) poseHistory_.clear();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

int MotionMatcher::getMatchedClipIndex() const {
    if (matchedFrame_ < 0 || !database_->isBaked()) return -1;
    return database_->getFrameClipIndex(matchedFrame_);
}

AnimPose* MotionMatcher::getPose() { return &pose_; }

void MotionMatcher::buildQuery(std::vector<float>& query) const {
    if (database_->hasFeatureLayout()) {
        buildSchemaQuery(query);
        return;
    }
    if (!database_->isBaked()) throw Exception("MotionMatcher: database not baked");
    if (database_->hasLocomotionFeatures()) {
        buildLocomotionQuery(query);
        return;
    }
    const int n = database_->getFeatureSize();
    query.assign(static_cast<size_t>(n), 0.f);

    // Character-space desired velocity (assume current facing = desiredYaw for query).
    const float cs = std::cos(desiredYaw_);
    const float sn = std::sin(desiredYaw_);
    query[0]       = desiredVelX_ * cs - desiredVelZ_ * sn;
    query[1]       = desiredVelX_ * sn + desiredVelZ_ * cs;

    // Desired trajectory: integrate constant velocity for 0.33/0.66/1.0s in char space.
    const float horizons[3] = {0.33f, 0.66f, 1.0f};
    for (int h = 0; h < 3; ++h) {
        query[static_cast<size_t>(2 + h * 2)]     = query[0] * horizons[h];
        query[static_cast<size_t>(2 + h * 2 + 1)] = query[1] * horizons[h];
        if (hasTrajectory_) {
            query[static_cast<size_t>(2 + h * 2)] = trajectory_[h].x * cs - trajectory_[h].z * sn;
            query[static_cast<size_t>(3 + h * 2)] = trajectory_[h].x * sn + trajectory_[h].z * cs;
        }
    }
    // Facing at +1s: same as desired (relative facing 0 → forward +Z in char space).
    float fx, fz;
    yawToForward(hasTrajectory_ ? trajectory_[2].yaw - desiredYaw_ : 0.f, fx, fz);
    query[8] = fx;
    query[9] = fz;

    // Pose features from current pose (character-relative).
    AnimPose cur;
    cur.copyFrom(hasQueryPose_ ? &queryPose_ : (playing_ ? &matchedPose_ : &pose_));
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
        const float lx                       = dx * c2 - dz * s2;
        const float lz                       = dx * s2 + dz * c2;
        query[static_cast<size_t>(base)]     = lx;
        query[static_cast<size_t>(base + 1)] = cur.getWorldPositionY(b);
        query[static_cast<size_t>(base + 2)] = lz;
        base += 3;
    }
    database_->normalizeFeature(query);
}

float MotionMatcher::cost(const std::vector<float>& query, const std::vector<float>& cand, float upperBound) const {
    const int   n            = static_cast<int>(query.size());
    float       sum          = 0.f;
    const auto* schema       = database_->schema_.get();
    const float schemaWeight = schema ? schema->weightSums[0] * poseWeight_ + schema->weightSums[1] * velWeight_ +
                                            schema->weightSums[2] * trajWeight_
                                      : 0.f;
    for (int i = 0; i < n; ++i) {
        const float d = query[static_cast<size_t>(i)] - cand[static_cast<size_t>(i)];
        float       w = poseWeight_;
        if (schema) {
            w = schema->sources[i] == MotionFeatureSource::Pose
                    ? poseWeight_
                    : (schema->kinds[i] == MotionFeatureKind::Velocity ? velWeight_ : trajWeight_);
            w *= schema->weights[i] / std::max(1e-8f, schemaWeight);
        } else if (database_->hasLocomotionFeatures()) {
            w = i < detail::locomotionPoseOffset ? trajWeight_ : poseWeight_;
            if (i < 2 || (i >= 12 && i < 14) || (i >= 16 && i < 19)) w = velWeight_;
            w *= detail::locomotionWeight(i);
            w /= std::max(1e-8f, 8.5f * velWeight_ + 10.6f * trajWeight_ + 5.f * poseWeight_);
        } else if (i < 2)
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

    // Compare against the live playhead, not the original entry frame.
    // Equal-cost searches must keep advancing instead of restarting a blend.
    int         continuation = -1;
    float       nearest      = std::numeric_limits<float>::infinity();
    const int   currentClip  = getMatchedClipIndex();
    const auto* currentAsset = currentClip >= 0 ? database_->getClip(currentClip) : nullptr;
    const bool  ended        = currentAsset && !currentAsset->getLoop() && matchedTime_ >= currentAsset->getDuration();
    if (playing_ && currentClip >= 0 && !ended) {
        const auto* clip  = database_->getClip(currentClip);
        const float time  = clip->wrapTime(matchedTime_);
        const int   count = filtered_ ? static_cast<int>(candidateFrames_.size()) : database_->getFrameCount();
        for (int candidate = 0; candidate < count; ++candidate) {
            const int i = filtered_ ? candidateFrames_[candidate] : candidate;
            if (database_->getFrameClipIndex(i) != currentClip) continue;
            float distance = std::abs(database_->getFrameTime(i) - time);
            if (clip->getLoop()) distance = std::min(distance, clip->getDuration() - distance);
            if (distance < nearest) {
                nearest      = distance;
                continuation = i;
            }
        }
        // Candidate ranges are a live search context (for example a Chooser
        // changing from grounded to airborne). A different valid interval in
        // the same clip must not make an out-of-range playhead eligible as a
        // continuing pose. At the bake rate, a live time is at most half a
        // sample from its representing frame.
        if (filtered_ && continuation >= 0) {
            const float rate = currentAsset->getSampleRate() > 0.f ? currentAsset->getSampleRate() : 30.f;
            if (nearest > .5f / rate + 1e-5f) continuation = -1;
        }
    }
    float bestCost = std::numeric_limits<float>::infinity();
    if (continuation >= 0) {
        auto feature = database_->frameAt(continuation).feature;
        // Continuing has no pose discontinuity. Use the evaluated playhead
        // pose rather than its nearest baked sample (which is quantized).
        if (database_->schema_) {
            for (std::size_t i = 0; i < query.size(); ++i)
                if (database_->schema_->queries[i] == MotionFeatureQuery::Continuing) query[i] = feature[i];
        } else if (database_->hasLocomotionFeatures()) {
            // Authored pose channels use UseContinuingPose; trajectory channels
            // still use the character's actual/predicted movement.
            std::copy(feature.begin() + detail::locomotionPoseOffset, feature.end(),
                      query.begin() + detail::locomotionPoseOffset);
        } else
            std::copy(query.begin() + 10, query.end(), feature.begin() + 10);
        bestCost = cost(query, feature, bestCost);
        if (filtered_) bestCost += candidateBias_[continuation] + candidateContinuingBias_[continuation];
    }
    int       best        = continuation;
    float     switchCost  = bestCost;
    const int searchCount = filtered_ ? static_cast<int>(candidateFrames_.size()) : database_->getFrameCount();
    for (int candidate = 0; candidate < searchCount; ++candidate) {
        const int i = filtered_ ? candidateFrames_[candidate] : candidate;
        // Authored protected phases can be played through, but never jumped into.
        if (filtered_ && transitionBlocked_[i]) continue;
        if (std::any_of(poseHistory_.begin(), poseHistory_.end(), [i](const auto& item) { return item.first == i; }))
            continue;
        // DisableReselection belongs to the current asset, including its end.
        // An expired one-shot cannot become its own fresh candidate again.
        if (playing_ && filtered_ && disableReselection_[currentClip] && database_->getFrameClipIndex(i) == currentClip)
            continue;
        if (continuation >= 0 && database_->getFrameClipIndex(i) == currentClip) {
            if (filtered_ && disableReselection_[currentClip]) continue;
            const auto* clip     = database_->getClip(currentClip);
            float       distance = std::abs(database_->getFrameTime(i) - clip->wrapTime(matchedTime_));
            if (clip->getLoop()) distance = std::min(distance, clip->getDuration() - distance);
            const float rate = clip->getSampleRate() > 0.f ? clip->getSampleRate() : 30.f;
            if (distance <= static_cast<float>(ignoreRadius_) / rate + 1e-5f) continue;
        }
        const float bias = filtered_ ? candidateBias_[i] : 0.f;
        const float c    = cost(query, database_->frameAt(i).feature, switchCost - bias) + bias;
        if (c + 1e-5f < switchCost) {
            bestCost   = c;
            switchCost = c;
            best       = i;
        }
    }
    if (best == continuation && continuation >= 0) {
        matchedFrame_ = continuation;
        lastCost_     = bestCost;
        updatePlayRate(continuation);
        return;
    }

    // A database containing only an exhausted, non-reselectable one-shot holds
    // its terminal pose. It remains searchable after the caller changes candidates.
    if (best < 0) return;
    lastCost_ = bestCost;
    updatePlayRate(best);
    if (best != matchedFrame_ || ended) {
        matchedFrame_ = best;
        matchedTime_  = database_->getFrameTime(best);
        sampleMatched(&matchedPose_);
        if (blendTime_ > 1e-6f && playing_) {
            AnimPose previousTarget;
            database_->getClip(getMatchedClipIndex())
                ->sample(matchedTime_ - inertia_->historyInterval * playRate_, &previousTarget, skeleton_);
            inertia_->begin(pose_, matchedPose_, previousTarget, blendTime_);
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
        const auto* clip  = matchedFrame_ >= 0 ? database_->getClip(getMatchedClipIndex()) : nullptr;
        const bool  ended = clip && !clip->getLoop() && matchedTime_ >= clip->getDuration();
        if (searchTimer_ >= searchInterval_ || ended) {
            searchTimer_ = 0.f;
            search();
        }
    }

    inertia_->remember(pose_, dt);
    if (matchedFrame_ >= 0) {
        matchedTime_ += dt * playRate_;
        const auto* clip = database_->getClip(getMatchedClipIndex());
        if (!clip->getLoop()) matchedTime_ = std::min(matchedTime_, clip->getDuration());
        sampleMatched(&matchedPose_);
    }

    if (blending_) {
        blendElapsed_ += dt;
        if (blendElapsed_ >= inertia_->duration) {
            blending_ = false;
            pose_.copyFrom(&matchedPose_);
        } else {
            inertia_->apply(matchedPose_, blendElapsed_, pose_);
        }
    } else if (matchedFrame_ >= 0) {
        pose_.copyFrom(&matchedPose_);
    }
    updatePoseHistory(dt);
}

void MotionMatcher::updatePoseHistory(float dt) {
    if (poseReselectHistory_ <= 0.f || dt <= 0.f || matchedFrame_ < 0) return;
    for (auto& item : poseHistory_) item.second += dt;
    std::erase_if(poseHistory_, [&](const auto& item) { return item.second > poseReselectHistory_; });
    const int   clipIndex    = getMatchedClipIndex();
    const auto* clip         = database_->getClip(clipIndex);
    const float time         = clip->wrapTime(matchedTime_);
    int         nearestFrame = -1;
    float       nearest      = std::numeric_limits<float>::infinity();
    const int   count        = filtered_ ? static_cast<int>(candidateFrames_.size()) : database_->getFrameCount();
    for (int candidate = 0; candidate < count; ++candidate) {
        const int i = filtered_ ? candidateFrames_[candidate] : candidate;
        if (database_->getFrameClipIndex(i) != clipIndex) continue;
        float distance = std::abs(database_->getFrameTime(i) - time);
        if (clip->getLoop()) distance = std::min(distance, clip->getDuration() - distance);
        if (distance < nearest) {
            nearest      = distance;
            nearestFrame = i;
        }
    }
    if (nearestFrame < 0) return;
    auto found = std::find_if(poseHistory_.begin(), poseHistory_.end(),
                              [nearestFrame](const auto& item) { return item.first == nearestFrame; });
    if (found == poseHistory_.end())
        poseHistory_.push_back({nearestFrame, 0.f});
    else
        found->second = 0.f;
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

// MotionMatcherLocomotion.cpp
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

// MotionMatcherSchema.cpp
namespace eve::animation {
eve::Result<void> MotionMatcher::setFeatureQuery(const AnimPose& current, const AnimPose& previous, float dt,
                                                 std::span<const MotionFeatureTrajectorySample> samples,
                                                 std::span<const MotionFeatureCurveSample>      curves) {
    auto fail = [](const char* message) {
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, message, "featureQuery", {}, "animation"));
    };
    if (!database_->schema_ || !database_->isBaked() || !std::isfinite(dt) || dt <= 0.f || dt > 1.f ||
        current.getBoneCount() != skeleton_->getBoneCount() || previous.getBoneCount() != skeleton_->getBoneCount() ||
        samples.size() > 256 || curves.size() > 256)
        return fail("feature query requires a baked variable layout, matching poses and finite dt in (0,1]");
    for (const auto* pose : {&current, &previous})
        for (int i = 0; i < pose->getBoneCount(); ++i) {
            const auto& t = pose->local(i);
            for (float v : {t.px, t.py, t.pz, t.qx, t.qy, t.qz, t.qw, t.sx, t.sy, t.sz})
                if (!std::isfinite(v)) return fail("feature query poses must be finite");
        }
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto& s = samples[i];
        for (float v : {s.seconds, s.x, s.y, s.z, s.vx, s.vy, s.vz, s.yaw})
            if (!std::isfinite(v)) return fail("trajectory samples must be finite");
        for (std::size_t j = 0; j < i; ++j)
            if (std::abs(s.seconds - samples[j].seconds) < 1e-6f) return fail("trajectory sample times must be unique");
    }
    const auto& schema = *database_->schema_;
    for (std::size_t i = 0; i < curves.size(); ++i) {
        const auto& c = curves[i];
        if (c.curve.empty() || c.curve.size() > 4096 || !std::isfinite(c.seconds) || !std::isfinite(c.value))
            return fail("curve queries require bounded names and finite times/values");
        for (std::size_t j = 0; j < i; ++j)
            if (c.curve == curves[j].curve && std::abs(c.seconds - curves[j].seconds) < 1e-6f)
                return fail("curve query names and times must be unique");
    }
    std::vector<float> scalarValues(schema.layout.channels.size(), 0.f);
    for (std::size_t i = 0; i < schema.layout.channels.size(); ++i) {
        const auto& c = schema.layout.channels[i];
        if (c.kind != MotionFeatureKind::Curve) continue;
        auto sample = std::find_if(curves.begin(), curves.end(), [&](const auto& s) {
            return s.curve == c.curve && std::abs(s.seconds - c.sampleTime) < 1e-6f;
        });
        if (sample == curves.end()) return fail("curve query does not cover a configured channel/time");
        scalarValues[i] = sample->value;
    }
    std::vector<const MotionFeatureTrajectorySample*> selected;
    for (const auto& c : schema.layout.channels) {
        const MotionFeatureTrajectorySample* sample = nullptr;
        if (c.source == MotionFeatureSource::Trajectory) {
            for (const auto& s : samples)
                if (std::abs(s.seconds - c.sampleTime) < 1e-6f) {
                    sample = &s;
                    break;
                }
            if (!sample) return fail("trajectory query does not cover a configured sample time");
        }
        selected.push_back(sample);
    }
    AnimPose now, past;
    now.copyFrom(&current);
    past.copyFrom(&previous);
    now.computeWorld(skeleton_);
    past.computeWorld(skeleton_);
    const auto&  root         = now.world(database_->getRootBone());
    const auto&  previousRoot = past.world(database_->getRootBone());
    TransformTRS character;
    character.qy = std::sin(desiredYaw_ * .5f);
    character.qw = std::cos(desiredYaw_ * .5f);
    std::vector<float> query(schema.dimension, 0.f);
    for (std::size_t i = 0; i < schema.layout.channels.size(); ++i) {
        const auto&          c = schema.layout.channels[i];
        std::array<float, 3> value{};
        if (c.kind == MotionFeatureKind::Curve)
            value[0] = scalarValues[i];
        else if (const auto* sample = selected[i]) {
            if (c.kind == MotionFeatureKind::Position)
                value = detail::schemaRotate({sample->x, sample->y, sample->z}, character, true);
            else if (c.kind == MotionFeatureKind::Velocity)
                value = detail::schemaRotate({sample->vx, sample->vy, sample->vz}, character, true);
            else {
                TransformTRS rotation;
                rotation.qy = std::sin(sample->yaw * .5f);
                rotation.qw = std::cos(sample->yaw * .5f);
                value       = detail::schemaHeading(rotation, character, c.headingAxis);
            }
        } else {
            const auto& bone = now.world(c.bone);
            if (c.kind == MotionFeatureKind::Position)
                value = detail::schemaDifference(bone, now.world(c.origin), root);
            else if (c.kind == MotionFeatureKind::Heading)
                value = detail::schemaHeading(bone, root, c.headingAxis);
            else if (c.characterSpaceVelocity) {
                value          = detail::schemaDifference(bone, now.world(c.origin), root);
                const auto old = detail::schemaDifference(past.world(c.bone), past.world(c.origin), previousRoot);
                for (int axis = 0; axis < 3; ++axis) value[axis] = (value[axis] - old[axis]) / dt;
            } else {
                value = detail::schemaDifference(bone, past.world(c.bone), root);
                for (float& v : value) v /= dt;
            }
        }
        detail::schemaEncode(std::span(query).subspan(schema.offsets[i]), c, value,
                             schema.layout.normalizationLengthScale);
    }
    for (float v : query)
        if (!std::isfinite(v)) return fail("encoded feature query exceeds finite range");
    const float queryTrajectorySpeed = database_->schemaTrajectorySpeed(query);
    database_->normalizeFeature(query);
    for (float v : query)
        if (!std::isfinite(v)) return fail("normalized feature query exceeds finite range");
    schemaQuery_.swap(query);
    queryTrajectorySpeed_ = queryTrajectorySpeed;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void MotionMatcher::buildSchemaQuery(std::vector<float>& query) const {
    if (schemaQuery_.empty()) throw Exception("MotionMatcher: configure a feature query before searching");
    query = schemaQuery_;
}
}  // namespace eve::animation

// MotionMatcherSelection.cpp
namespace eve::animation {
eve::Result<int> MotionMatcher::setCandidateRanges(std::span<const MotionSearchRange> ranges) {
    auto fail = [](const char* message) {
        return eve::Result<int>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, message, "candidateRanges", {}, "animation"));
    };
    if (!database_->isBaked() || ranges.empty() || ranges.size() > 100000)
        return fail("candidate selection requires a baked database and bounded nonempty ranges");
    std::vector<std::vector<MotionSearchRange>> perClip(database_->getClipCount());
    std::size_t                                 intervalCount = 0;
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
        const int end   = database_->clipFrameOffsets_[static_cast<std::size_t>(clipIndex + 1)];
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
                    best              = rangeBias;
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
    if (database_->hasLocomotionFeatures() || database_->hasFeatureLayout() ||
        pose.getBoneCount() != pose_.getBoneCount())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "basic query pose requires the basic layout and matching bone count",
            "queryPose", {}, "animation"));
    queryPose_.copyFrom(&pose);
    hasQueryPose_ = true;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> MotionMatcher::setTrajectory(std::span<const MotionTrajectorySample> samples) {
    if (database_->hasLocomotionFeatures() || database_->hasFeatureLayout() || samples.size() != 3 ||
        std::any_of(samples.begin(), samples.end(),
                    [](const auto& s) { return !std::isfinite(s.x) || !std::isfinite(s.z) || !std::isfinite(s.yaw); }))
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "trajectory requires three finite samples",
                                                                 "trajectory", {}, "animation"));
    std::copy(samples.begin(), samples.end(), trajectory_.begin());
    hasTrajectory_ = true;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}
}  // namespace eve::animation
