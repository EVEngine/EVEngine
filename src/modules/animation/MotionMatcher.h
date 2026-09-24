#pragma once
#include "common/Export.h"


#include "animation/AnimPose.h"
#include "common/Time.h"

#include <array>
#include <memory>
#include <span>
#include <vector>

namespace eve::animation {

namespace detail {
struct PoseInertia;
}
class AnimSkeleton;
class MotionDatabase;
struct MotionFeatureTrajectorySample;
struct MotionFeatureCurveSample;

/** @brief Copied clip-time interval forbidding new matches, with inclusive start and exclusive end. */
struct MotionTransitionBlock {
    float start = 0.f;
    float end = 0.f;
};
/** @brief Copied clip-time interval overriding a matching cost, with inclusive start and exclusive end. */
struct MotionCostOverride {
    float start = 0.f;
    float end = 0.f;
    float costBias = 0.f;
};
/** @brief Owning candidate interval in seconds in a database clip; endpoints inclusive. */
struct MotionSearchRange {
    int   clipIndex          = 0;
    float start              = 0.f;
    float end                = 0.f;
    float costBias           = 0.f;
    bool  disableReselection = false;
    /** @brief Owned transition blocks, unioned across all ranges of this clip; continuation remains eligible. */
    std::vector<MotionTransitionBlock> transitionBlocks;
    /** @brief Default extra cost for continuing a pose selected through this range. */
    float continuingCostBias = 0.f;
    /** @brief Owned absolute candidate-cost overrides, evaluated in order so the last active interval wins. */
    std::vector<MotionCostOverride> costOverrides;
    /** @brief Owned continuing-cost overrides, evaluated in order so the last active interval wins. */
    std::vector<MotionCostOverride> continuingCostOverrides;
};
/** @brief Predicted world-space displacement and facing at a fixed future horizon. */
struct MotionTrajectorySample {
    float x = 0.f, z = 0.f, yaw = 0.f;
};
/** @brief Copied world displacement, velocity (metres/second), and yaw for a locomotion horizon. */
struct MotionLocomotionSample {
    float x = 0.f, y = 0.f, z = 0.f;
    float vx = 0.f, vy = 0.f, vz = 0.f;
    float yaw = 0.f;
};

/**
 * @brief Runtime motion matching player: builds a query feature from current pose +
 * desired trajectory, searches MotionDatabase, inertializes into the moving best match.
 * Script type: `MotionMatcher`.
 */
class EVENGINE_API_WORLD MotionMatcher {
public:
    MotionMatcher(AnimSkeleton* skeleton, MotionDatabase* database);
    ~MotionMatcher();

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
    /**
     * @brief Configure the inclusive playback-rate interval used to reconcile query and selected trajectory speed.
     * @param minimum Finite positive lower rate.
     * @param maximum Finite upper rate greater than or equal to minimum; both values are at most ten.
     * @return Applied, or InvalidArgument without changing the prior interval or current rate.
     * @details Variable-layout databases sum every unnormalized trajectory velocity channel, matching UE Pose Search.
     * Other layouts use rate one clamped to this interval. Configuration and playback are deterministic for the same
     * simulation/query sequence. The matcher owns all values and retains no caller memory.
     * @thread Owner simulation thread outside advance/search; not reentrant.
     */
    [[nodiscard]] eve::Result<void> setPlayRateRange(float minimum, float maximum);
    float getPlayRateMinimum() const { return playRateMin_; }
    float getPlayRateMaximum() const { return playRateMax_; }
    float getPlayRate() const { return playRate_; }
    void  setTrajectoryWeight(float w);
    float getTrajectoryWeight() const { return trajWeight_; }
    void  setPoseWeight(float w);
    float getPoseWeight() const { return poseWeight_; }
    void  setVelocityWeight(float w);
    float getVelocityWeight() const { return velWeight_; }

    /** @brief Keep continuous playback within this frame radius of the live playhead, including loop seams. */
    void setIgnoreRadius(int frames);
    int  getIgnoreRadius() const { return ignoreRadius_; }
    /** @brief Set how long an already selected baked pose is excluded from new candidates.
     * @param seconds Finite duration in [0,10]. Zero clears and disables history.
     * @return Applied, or InvalidArgument preserving the previous duration and history.
     * @details The continuing pose remains eligible. History is owned by this matcher and
     * advances only through positive simulation steps. No callbacks or retained inputs.
     * @thread Owner thread outside search/advance; not reentrant.
     */
    [[nodiscard]] eve::Result<void> setPoseReselectHistory(float seconds);
    float getPoseReselectHistory() const { return poseReselectHistory_; }

    int   getMatchedFrame() const { return matchedFrame_; }
    int   getMatchedClipIndex() const;
    float getMatchedTime() const { return matchedTime_; }
    float getLastSearchCost() const { return lastCost_; }

    AnimPose* getPose();

    /** @brief Atomically replace searchable intervals, retaining original database frame IDs.
     * @param ranges
     * Borrowed for this synchronous call only; copied into an owning frame selection.
     * @return Applied candidate
     * count, or a diagnostic leaving the old selection unchanged.
     * @details Empty, nonfinite, invalid clip/time
     * ranges and ranges containing no frames are rejected.
     * A fresh matcher searches all frames until configured.
     * Transition blocks only reject new matches, not eligible continuation. Their
     * finite nonnegative intervals must have start < end; an end beyond clip
     * duration can protect the terminal sample. At most 100000 blocks are accepted.
     * The returned count includes blocked frames eligible for continuation.
     * Continuation is retained only while the live playhead is represented by
     * a frame in the replacement ranges; another interval of the same clip
     * cannot retain an out-of-range playhead.
     * Candidate and continuing cost overrides use the same interval semantics;
     * the last active override in a range replaces its corresponding default.
     * Duplicate ranges retain the continuing cost paired with the lowest
     * candidate cost for that sampled frame.
     * Database must stay immutable thereafter.
     * @thread Owner thread outside advance/search; no callbacks,
     * reentrancy, or retained input pointers.
     */
    [[nodiscard]] eve::Result<int> setCandidateRanges(std::span<const MotionSearchRange> ranges);
    /** @brief Copy the character's current local pose for the next search, including action handoffs.
     * @param
     * pose Borrowed pose with matching bone count; never retained by reference.
     * @return Applied or a diagnostic
     * leaving the previous query unchanged.
     * @thread Owner thread outside advance/search; no callbacks or
     * reentrancy.
     */
    [[nodiscard]] eve::Result<void> setQueryPose(const AnimPose& pose);
    /** @brief Set predictions at 0.33, 0.66, and 1.0 seconds instead of constant-velocity prediction.
     * @param
     * samples Borrowed exactly three finite world displacements and world yaw angles, copied here.
     * @return
     * Applied or diagnostic without changing existing predictions.
     * @thread Owner thread outside advance/search;
     * no callbacks, reentrancy, or retained input pointers.
     */
    [[nodiscard]] eve::Result<void> setTrajectory(std::span<const MotionTrajectorySample> samples);

    /** @brief Atomically copy pose history and trajectory for a locomotion database.
     * @param current Current
     * local pose, matching this skeleton's bone count.
     * @param previous Previous local pose; a copy of current is
     * valid at a cold start.
     * @param elapsedSeconds Positive finite simulation interval between poses, at most
     * one second.
     * @param samples Exactly five finite world displacements/velocities/yaws at -0.05, 0, 0.35, 0.7,
     * 1 seconds.
     * @return Applied, or a diagnostic leaving every query input unchanged.
     * @details Pose
     * samples supply cold-start/action-handoff features. Eligible continuing poses
     * supply pose features during
     * playback; trajectory always comes from character prediction.
     * All inputs are copied; database and skeleton
     * are borrowed and must outlive this matcher.
     * Deterministic for the same ordered simulation samples within
     * floating-point tolerance.
     * @thread Owner thread outside search/advance; no callbacks or reentrancy.
     */
    [[nodiscard]] eve::Result<void> setLocomotionQuery(const AnimPose& current, const AnimPose& previous,
                                                       float                                   elapsedSeconds,
                                                       std::span<const MotionLocomotionSample> samples);

    /** @brief Atomically copy a query for the database's variable feature layout.
     * @param current Current local pose, borrowed for this call.
     * @param previous Previous local pose, borrowed for this call; may equal current at cold start.
     * @param elapsedSeconds Finite positive simulation interval, at most one second.
     * @param samples Borrowed trajectory samples with unique times covering every
     * configured trajectory channel; all values must be finite. Copied, not retained.
     * @param curves Borrowed evaluated scalar values covering each configured curve/time,
     * copied during this call. Missing authored curves must be supplied as zero.
     * @return Applied, or InvalidArgument preserving the complete previous query.
     * @details Values use metres/seconds. Trajectories are encoded using the desired
     * yaw at this call; setDesiredYaw must precede this setter each frame.
     * Pose channels use the same encoder as
     * database baking; continuing-policy channels use a valid continuing frame
     * during search. The caller-owned skeleton/database must outlive this matcher
     * and remain immutable. No callbacks or reentrancy.
     * @thread Owner thread, outside search/advance.
     */
    [[nodiscard]] eve::Result<void> setFeatureQuery(const AnimPose& current, const AnimPose& previous,
                                                    float elapsedSeconds,
                                                    std::span<const MotionFeatureTrajectorySample> samples,
                                                    std::span<const MotionFeatureCurveSample> curves = {});

    /** @brief Force an immediate search (also called periodically from update). */
    void search();

    /** @brief Advance matching and pose blending by one scheduler step. */
    [[nodiscard]] eve::Result<void> advance(const eve::SimulationStep& step);
    /** @brief Legacy seconds facade; explicitly forwards to advance(). */
    void update(float dt);

private:
    void buildSchemaQuery(std::vector<float>& query) const;
    void  buildLocomotionQuery(std::vector<float>& query) const;
    void  buildQuery(std::vector<float>& query) const;
    float cost(const std::vector<float>& query, const std::vector<float>& cand, float upperBound) const;
    void  sampleMatched(AnimPose* out) const;

    AnimSkeleton*   skeleton_ = nullptr;
    MotionDatabase* database_ = nullptr;
    AnimPose        pose_;
    std::unique_ptr<detail::PoseInertia> inertia_;
    AnimPose        matchedPose_;

    float desiredVelX_ = 0.f;
    float desiredVelZ_ = 0.f;
    float desiredYaw_  = 0.f;

    float searchInterval_ = 0.1f;
    float searchTimer_    = 0.f;
    float blendTime_      = 0.15f;
    float blendElapsed_   = 0.f;
    bool  blending_       = false;
    float playRateMin_    = 1.f;
    float playRateMax_    = 1.f;
    float playRate_       = 1.f;
    float queryTrajectorySpeed_ = 0.f;

    float trajWeight_   = 1.f;
    float poseWeight_   = 1.f;
    float velWeight_    = 1.f;
    int   ignoreRadius_ = 2;
    float poseReselectHistory_ = 0.f;
    std::vector<std::pair<int, float>> poseHistory_;

    int   matchedFrame_ = -1;
    float matchedTime_  = 0.f;
    float lastCost_     = 0.f;
    bool  playing_      = false;
    eve::SimulationTick lastTick_     = eve::SimulationTick::zero();
    bool                hasLastTick_  = false;

    std::vector<int>                      candidateFrames_;
    std::vector<float>                    candidateBias_;
    std::vector<float>                    candidateContinuingBias_;
    std::vector<unsigned char>            candidateMask_;
    std::vector<unsigned char>            transitionBlocked_;
    std::vector<unsigned char>            disableReselection_;
    bool                                  filtered_ = false;
    AnimPose                              queryPose_;
    bool                                  hasQueryPose_ = false;
    std::array<MotionTrajectorySample, 3> trajectory_{};
    bool                                  hasTrajectory_ = false;
    AnimPose                              previousQueryPose_;
    float                                 queryPoseInterval_ = 0.f;
    std::array<MotionLocomotionSample, 5> locomotionTrajectory_{};
    bool                                  hasLocomotionQuery_ = false;
    std::vector<float>                     schemaQuery_;

    void updateUnchecked(float dt);
    void updatePoseHistory(float dt);
    void updatePlayRate(int frame);
};

}  // namespace eve::animation
