#pragma once

#include "animation/AnimPose.h"
#include "common/Time.h"

#include <vector>

namespace eve::animation {

class AnimSkeleton;
class MotionDatabase;

/**
 * @brief Runtime motion matching player: builds a query feature from current pose +
 * desired trajectory, searches MotionDatabase, cross-fades to best match.
 * Script type: `MotionMatcher`.
 */
class MotionMatcher {
public:
    MotionMatcher(AnimSkeleton* skeleton, MotionDatabase* database);
    ~MotionMatcher() = default;

    MotionMatcher(const MotionMatcher&)            = delete;
    MotionMatcher& operator=(const MotionMatcher&) = delete;

    AnimSkeleton*   getSkeleton() const { return skeleton_; }
    MotionDatabase* getDatabase() const { return database_; }

    /** @brief Desired planar velocity in character/world XZ (units/sec). */
    void  setDesiredVelocity(float x, float z);
    float getDesiredVelocityX() const { return desiredVelX_; }
    float getDesiredVelocityZ() const { return desiredVelZ_; }

    /** @brief Desired facing yaw (radians, Y-up). */
    void  setDesiredYaw(float yaw);
    float getDesiredYaw() const { return desiredYaw_; }

    void  setSearchInterval(float seconds);
    float getSearchInterval() const { return searchInterval_; }
    void  setBlendTime(float seconds);
    float getBlendTime() const { return blendTime_; }
    void  setTrajectoryWeight(float w);
    float getTrajectoryWeight() const { return trajWeight_; }
    void  setPoseWeight(float w);
    float getPoseWeight() const { return poseWeight_; }
    void  setVelocityWeight(float w);
    float getVelocityWeight() const { return velWeight_; }

    /** @brief Skip rematching the same frame / nearby frames to reduce jitter. */
    void setIgnoreRadius(int frames);
    int  getIgnoreRadius() const { return ignoreRadius_; }

    int   getMatchedFrame() const { return matchedFrame_; }
    int   getMatchedClipIndex() const;
    float getMatchedTime() const { return matchedTime_; }
    float getLastSearchCost() const { return lastCost_; }

    AnimPose* getPose();

    /** @brief Force an immediate search (also called periodically from update). */
    void search();

    /** @brief Advance matching and pose blending by one scheduler step. */
    [[nodiscard]] eve::Result<void> advance(const eve::SimulationStep& step);
    /** @brief Legacy seconds facade; explicitly forwards to advance(). */
    void update(float dt);

private:
    void  buildQuery(std::vector<float>& query) const;
    float cost(const std::vector<float>& query, const std::vector<float>& cand, float upperBound) const;
    void  sampleMatched(AnimPose* out) const;

    AnimSkeleton*   skeleton_ = nullptr;
    MotionDatabase* database_ = nullptr;
    AnimPose        pose_;
    AnimPose        fromPose_;
    AnimPose        matchedPose_;

    float desiredVelX_ = 0.f;
    float desiredVelZ_ = 0.f;
    float desiredYaw_  = 0.f;

    float searchInterval_ = 0.1f;
    float searchTimer_    = 0.f;
    float blendTime_      = 0.15f;
    float blendElapsed_   = 0.f;
    bool  blending_       = false;

    float trajWeight_   = 1.f;
    float poseWeight_   = 1.f;
    float velWeight_    = 1.f;
    int   ignoreRadius_ = 2;

    int   matchedFrame_ = -1;
    float matchedTime_  = 0.f;
    float lastCost_     = 0.f;
    bool  playing_      = false;
    eve::SimulationTick lastTick_ = eve::SimulationTick::zero();
    bool hasLastTick_ = false;

    void updateUnchecked(float dt);
};

}  // namespace eve::animation
