#pragma once

/** @file MontagePlayer.h @brief Deterministic presentation-only action montage playback. */

#include "action/Action.h"
#include "animation/AnimMath.h"
#include "common/Result.h"
#include "common/Time.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eve::animation {

class AnimClip;
class AnimPlayer;
class AnimPose;
class AnimSkeleton;

/** @brief Owning clip supplied while transactionally preparing a montage. */
struct MontageClipAsset {
    std::string               uri;
    std::unique_ptr<AnimClip> clip;
};

/**
 * @brief Synchronous ownership-transfer port used to resolve montage clip URIs.
 *
 * Implementations may cache source assets but must return a distinct owned clip
 * for each successful call. They must not retain the borrowed skeleton or invoke
 * scripts while loading.
 */
class IMontageClipProvider {
public:
    virtual ~IMontageClipProvider() = default;
    /** @brief Resolve one URI into a clip compatible with the borrowed skeleton. */
    [[nodiscard]] virtual Result<std::unique_ptr<AnimClip>> load(std::string_view    uri,
                                                                 const AnimSkeleton& skeleton) = 0;
};

/** @brief Selects which extracted root-motion components leave the presentation player. */
struct MontageRootMotionMask {
    bool translationX = true;
    bool translationY = true;
    bool translationZ = true;
    bool rotation     = true;
};

/** @brief Pose behavior while the authoritative action cursor is between authored sections. */
enum class MontageGapPolicy : std::uint8_t {
    /** @brief Keep the last evaluated pose until another section starts. */
    HoldPose,
    /** @brief Restore the skeleton bind pose on entering a gap. */
    BindPose,
};

/**
 * @brief Optional receiver for filtered per-step root motion.
 *
 * The callback runs synchronously on the montage owner thread after pose
 * evaluation. It must not throw or retain references to the borrowed delta.
 */
class IMontageRootMotionReceiver {
public:
    virtual ~IMontageRootMotionReceiver() = default;
    /** @brief Consume one borrowed filtered root-motion delta. */
    virtual void applyMontageRootMotion(const TransformTRS& delta) noexcept = 0;
};

/** @brief Compatibility name for the canonical action-owned active block projection. */
using MontageActiveBlock = action::ActionActiveBlock;

/** @brief Owning observation returned by one montage presentation step. */
struct MontageAdvance {
    Duration                                 previous = Duration::zero();
    Duration                                 current  = Duration::zero();
    std::optional<LogicalId>                 sectionId;
    std::vector<action::ActionTimelineEvent> events;
    std::vector<MontageActiveBlock>          activeBlocks;
    TransformTRS                             rootMotion = TransformTRS::identity();
    double                                   weight     = 1.0;
    bool                                     completed  = false;
};

/**
 * @brief Presentation-only montage player over an ActionTimeline.
 *
 * ActionRuntime remains the sole gameplay lifecycle authority. This object
 * owns all resolved clips and only keeps a derived presentation cursor. It is
 * owner-thread-only, reads no wall clock, and invokes the optional receiver
 * without holding a lock.
 */
class MontagePlayer {
public:
    /** @brief Borrow a skeleton that must outlive this player. */
    explicit MontagePlayer(AnimSkeleton& skeleton);
    ~MontagePlayer();

    MontagePlayer(const MontagePlayer&)            = delete;
    MontagePlayer& operator=(const MontagePlayer&) = delete;

    /**
     * @brief Validate and atomically replace the complete montage presentation.
     * @param timeline Canonical owning timeline copied into the player.
     * @param clips Clip ownership transferred only on success.
     */
    [[nodiscard]] Result<void> prepare(action::ActionTimeline timeline, std::vector<MontageClipAsset>&& clips);
    /**
     * @brief Resolve every distinct section URI and atomically prepare the montage.
     * @param provider Borrowed only for this synchronous call and never retained.
     */
    [[nodiscard]] Result<void> prepare(action::ActionTimeline timeline, IMontageClipProvider& provider);
    /**
     * @brief Transactionally reload every referenced clip while preserving playback state.
     * @param provider Borrowed synchronous provider; no replacement is committed unless all clips validate.
     */
    [[nodiscard]] Result<void> reloadClips(IMontageClipProvider& provider);
    /** @brief Transactionally replace one URI-backed clip and preserve the current cursor and slot weight. */
    [[nodiscard]] Result<void> replaceClip(std::string_view uri, std::unique_ptr<AnimClip> clip);
    /** @brief Bind presentation playback to one authoritative ActionRuntime execution. */
    [[nodiscard]] Result<void> play(action::ActionExecutionId executionId);
    /** @brief Rebind an already playing presentation after its external gameplay authority is recreated. */
    [[nodiscard]] Result<void> rebindExecution(action::ActionExecutionId executionId);
    /**
     * @brief Present one ActionRuntime-owned advance without advancing gameplay time locally.
     * @param advance Owning observation returned by ActionRuntime::advance.
     * @param tick Strictly increasing simulation tick associated with the observation.
     */
    [[nodiscard]] Result<MontageAdvance> present(const action::ActionAdvance& advance, SimulationTick tick);
    /**
     * @brief Atomically realign presentation and active state blocks to an externally authoritative timestamp.
     * @remarks This does not advance ActionRuntime; the caller must align its gameplay authority separately.
     */
    [[nodiscard]] Result<MontageAdvance> jumpToTime(action::ActionExecutionId executionId, Duration target,
                                                    SimulationTick tick);
    /** @brief Jump presentation to the start of one objective physical section. */
    [[nodiscard]] Result<MontageAdvance> jumpToSection(action::ActionExecutionId executionId, std::size_t sectionIndex,
                                                       SimulationTick tick);
    /** @brief Evaluate a normalized physical-section progress and atomically realign presentation. */
    [[nodiscard]] Result<MontageAdvance> evaluateSectionProgress(action::ActionExecutionId executionId,
                                                                 std::size_t sectionIndex, double progress,
                                                                 SimulationTick tick);
    /** @brief Resolve the objective physical section containing the current cursor. */
    [[nodiscard]] Result<std::size_t> physicalSectionIndex() const;
    /** @brief Return current normalized progress within an objective physical section. */
    [[nodiscard]] Result<double> physicalSectionProgress(std::size_t sectionIndex) const;
    /** @brief Interrupt playback and emit paired exits for every active state block. */
    [[nodiscard]] Result<MontageAdvance> interrupt(SimulationTick tick);
    /** @brief Enter continuous blend-out and emit exits for every active state block. */
    [[nodiscard]] Result<MontageAdvance> beginBlendOut(Duration duration, SimulationTick tick);
    /** @brief Advance an active blend-out without advancing gameplay or montage time. */
    [[nodiscard]] Result<MontageAdvance> advanceBlendOut(Duration delta, SimulationTick tick);
    /** @brief Stop playback and reset the derived cursor. */
    void stop() noexcept;

    /**
     * @brief Install a borrowed root-motion receiver.
     * @param receiver Receiver that must outlive this player or be cleared before destruction.
     * @lifetime The player retains a non-owning observer until clearRootMotionReceiver() is called.
     */
    void setRootMotionReceiver(IMontageRootMotionReceiver& receiver) noexcept { receiver_ = &receiver; }
    /** @brief Clear the borrowed root-motion receiver before its owner is destroyed. */
    void clearRootMotionReceiver() noexcept { receiver_ = nullptr; }
    /** @brief Configure root-motion filtering. */
    void setRootMotionMask(MontageRootMotionMask mask) noexcept { rootMotionMask_ = mask; }
    /** @brief Select the explicit pose policy used in gaps between animation sections. */
    void setGapPolicy(MontageGapPolicy policy) noexcept { gapPolicy_ = policy; }
    /** @brief Borrow the current evaluated pose. */
    [[nodiscard]] AnimPose& pose();
    /** @brief Current derived montage presentation time. */
    [[nodiscard]] Duration time() const noexcept { return time_; }
    /** @brief Whether the prepared montage is actively playing. */
    [[nodiscard]] bool isPlaying() const noexcept { return playing_; }
    /** @brief Whether playback is in externally requested blend-out. */
    [[nodiscard]] bool isBlendingOut() const noexcept { return blendingOut_; }
    /** @brief Current continuous montage slot weight. */
    [[nodiscard]] double weight() const noexcept { return weight_; }
    /** @brief Authored output layer requested by the prepared montage, or zero before preparation. */
    [[nodiscard]] std::size_t animationLayer() const noexcept {
        return timeline_ ? timeline_->montage.animationLayer : 0U;
    }
    /** @brief Whether the host should run its terrain-specific foot-IK provider after this pose. */
    [[nodiscard]] bool footIkEnabled() const noexcept { return timeline_ && timeline_->montage.footIk; }

private:
    /** @brief Resolve an owned clip. @return Borrowed clip or null. @lifetime Until presentation replacement. */
    [[nodiscard]] AnimClip* clipFor(std::string_view uri) const noexcept;
    /** @brief Resolve a section. @return Borrowed section or null. @lifetime Until prepare or destruction. */
    [[nodiscard]] const action::ActionAnimationSection* sectionAt(Duration time) const noexcept;
    void activate(const action::ActionAnimationSection& section, Duration localTime);
    void accumulateRootMotion(TransformTRS& total) const;
    [[nodiscard]] std::vector<action::ActionTimelineEvent> realignStateEvents(Duration target) const;
    [[nodiscard]] Result<std::vector<MontageActiveBlock>>  activeBlocksAt(Duration target) const;

    AnimSkeleton&                         skeleton_;
    std::unique_ptr<AnimPlayer>           player_;
    std::optional<action::ActionTimeline> timeline_;
    std::vector<MontageClipAsset>         clips_;
    const action::ActionAnimationSection* activeSection_ = nullptr;
    IMontageRootMotionReceiver*           receiver_      = nullptr;
    MontageRootMotionMask                 rootMotionMask_;
    MontageGapPolicy                      gapPolicy_ = MontageGapPolicy::HoldPose;
    action::ActionExecutionId             executionId_{};
    Duration                              time_                = Duration::zero();
    SimulationTick                        lastTick_            = SimulationTick::zero();
    bool                                  hasLastTick_         = false;
    bool                                  started_             = false;
    bool                                  playing_             = false;
    bool                                  blendingOut_         = false;
    Duration                              blendOutDuration_    = Duration::zero();
    Duration                              blendOutElapsed_     = Duration::zero();
    double                                blendOutStartWeight_ = 1.0;
    double                                weight_              = 1.0;
};

}  // namespace eve::animation
