#pragma once
#include "common/Export.h"

/** @file FpsGait.h @brief First-person walk/sprint/crouch gait pose (no physics). */

#include "common/Result.h"

#include <cstdint>

namespace eve::combat {

/** @brief Discrete first-person locomotion gait. */
enum class FpsGaitMode : std::uint8_t { Walk, Sprint, Crouch };

/** @brief Tunable speeds and capsule/eye heights for one FPS body. */
struct FpsGaitProfile {
    double walkSpeed       = 5.5;
    double sprintSpeed     = 8.5;
    double crouchSpeed     = 2.8;
    double standHeight     = 1.80;
    double crouchHeight    = 1.10;
    double standEyeHeight  = 1.65;
    double crouchEyeHeight = 0.95;
    double capsuleRadius   = 0.30;

    /** @brief Reject non-finite or non-positive sizes/speeds. */
    [[nodiscard]] Result<void> validate() const;
};

/** @brief Per-frame intent and environment facts owned by the game. */
struct FpsGaitInput {
    bool wantSprint = false;
    bool wantCrouch = false;
    bool grounded   = true;
    /** @brief False when standing would collide (low ceiling); keeps crouch latched. */
    bool canStand = true;
};

/** @brief Resolved gait pose for movers and cameras. */
struct FpsGaitPose {
    FpsGaitMode mode           = FpsGaitMode::Walk;
    double      moveSpeed      = 0.0;
    double      capsuleHeight  = 0.0;
    double      eyeHeight      = 0.0;
    double      capsuleRadius  = 0.0;
};

/**
 * @brief Deterministic Walk / Sprint / Crouch state machine.
 *
 * Priority: Crouch > Sprint > Walk. Sprint requires grounded and not crouching.
 * Crouch latches while `!canStand` so stand-up cannot teleport into geometry.
 * Does not integrate motion or query physics — callers drive `moveCapsule`.
 */
class EVENGINE_API_BACKENDS FpsGait {
public:
    /** @brief Replace the profile; invalid profiles leave the previous one intact. */
    [[nodiscard]] Result<void> setProfile(const FpsGaitProfile& profile);

    /** @brief Current profile (defaults until setProfile succeeds). */
    [[nodiscard]] const FpsGaitProfile& profile() const { return profile_; }

    /** @brief Last committed mode. */
    [[nodiscard]] FpsGaitMode mode() const { return mode_; }

    /**
     * @brief Evaluate one frame and commit the mode.
     * @cost Cheap; O(1) per call. Amortize by calling once per simulation step.
     */
    [[nodiscard]] Result<FpsGaitPose> evaluate(const FpsGaitInput& input);

private:
    FpsGaitProfile profile_{};
    FpsGaitMode    mode_ = FpsGaitMode::Walk;
};

/** @brief Head-bob / landing dip inputs. */
struct FpsHeadBobRequest {
    double stridePhase   = 0.0;  ///< Accumulated seconds * frequency scale.
    double moveSpeed     = 0.0;  ///< Horizontal speed this frame (m/s).
    double referenceSpeed = 5.5;
    bool   grounded      = true;
    bool   enabled       = true;
    double amplitude     = 0.035;
    double landDip       = 0.0;  ///< Remaining negative eye offset from landing.
};

/** @brief Eye-local vertical offset from bob + landing dip. */
struct FpsHeadBobOffset {
    double eyeY = 0.0;
};

/**
 * @brief Parameterized first-person head bob (presentation only).
 * @cost Cheap; O(1). Call after gait/move resolve.
 */
class EVENGINE_API_BACKENDS FpsHeadBob {
public:
    /** @brief Solve eye Y offset; does not mutate request. */
    [[nodiscard]] static Result<FpsHeadBobOffset> solve(const FpsHeadBobRequest& request);
};

}  // namespace eve::combat
