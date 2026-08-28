#pragma once

/**
 * @file ClimbingAnimation.h
 * @brief Borrowed Animation player/graph adapters for climbing root motion and semantic notifies.
 */

#include "climbing/Climbing.h"

namespace eve::animation {
class AnimClip;
class AnimGraph;
class AnimPlayer;
class MotionMatcher;
}  // namespace eve::animation

namespace eve::climbing {

/** @brief Presentation provider selected for the climbing action pose. */
enum class ClimbingGraphProvider : std::uint8_t { MotionMatching, AnimGraph, DirectPoseConstraints };

/** @brief Non-authoritative graph projection state; contains no borrowed pointers. */
struct ClimbingGraphState {
    ClimbingExecutionId activeExecutionId = ClimbingExecutionId::zero();
    ClimbingGraphProvider provider = ClimbingGraphProvider::DirectPoseConstraints;
};

/** @brief Owning result of extracting one animation frame for authoritative climbing motion. */
struct ClimbingAnimationFrame {
    ClimbingMotionInput motion;
    std::uint32_t       recognizedNotifyCount = 0;
    std::uint32_t       ignoredNotifyCount = 0;
};

/**
 * @brief Starts a borrowed clip in a borrowed player and selects its root-motion bone.
 * @param player Borrowed synchronously and never retained.
 * @param clip Borrowed synchronously; AnimPlayer retains it according to Animation's clip lifetime contract.
 * @param rootMotionBone Valid skeleton bone index used for delta extraction.
 * @thread Animation owner thread.
 * @reentrancy Does not invoke callbacks or scripts.
 */
[[nodiscard]] eve::Result<void> beginClimbingAnimation(animation::AnimPlayer& player, animation::AnimClip& clip,
                                                       int rootMotionBone);

/**
 * @brief Starts an action-bound clip after validating clip identity and resolving the named root bone.
 * @param binding Owning definition metadata borrowed only for this synchronous call.
 */
[[nodiscard]] eve::Result<void> beginClimbingAnimation(animation::AnimPlayer& player, animation::AnimClip& clip,
                                                       const ClimbingAnimationBinding& binding);

/**
 * @brief Advances a borrowed player once and extracts root motion plus known semantic notifies.
 * @param player Borrowed synchronously and never retained.
 * @param step Scheduler-owned deterministic step; duplicate ticks are rejected by AnimPlayer.
 * @param facing Current authoritative horizontal facing supplied by gameplay.
 * @param pelvisOffset Current animation pelvis-to-capsule deviation supplied by the pose adapter.
 * @thread Animation owner thread before ClimbingRuntime::advance for the same tick.
 * @reentrancy Does not invoke callbacks or scripts.
 */
[[nodiscard]] eve::Result<ClimbingAnimationFrame> advanceClimbingAnimation(animation::AnimPlayer& player,
                                                                           const eve::SimulationStep& step,
                                                                           Vec3 facing, Vec3 pelvisOffset = {});

/**
 * @brief Triggers a graph one-shot exactly once for a new climbing execution.
 * @param graph Borrowed synchronously and never retained; its scheduler remains responsible for graph advancement.
 * @param oneShotNode Valid AnimGraph one-shot node.
 * @param executionId Non-zero committed climbing execution identity.
 * @param state Caller-owned non-authoritative projection state.
 */
[[nodiscard]] eve::Result<ClimbingGraphProvider> driveClimbingGraph(animation::AnimGraph& graph, int oneShotNode,
                                                                    ClimbingExecutionId executionId,
                                                                    ClimbingGraphState& state);

/**
 * @brief Advances ordinary locomotion Motion Matching and marks it as the current presentation provider.
 * @param desiredVelocity Camera/world planar velocity; gameplay remains authoritative for actual movement.
 * @param desiredYaw Desired Y-up facing in radians.
 * @remarks Call only outside an active ClimbingExecution; action clips/graphs supersede this provider explicitly.
 */
[[nodiscard]] eve::Result<ClimbingGraphProvider> driveClimbingMotionMatching(
    animation::MotionMatcher& matcher, const eve::SimulationStep& step, Vec3 desiredVelocity,
    float desiredYaw, ClimbingGraphState& state);

/** @brief Selects the explicit no-graph degradation path without mutating gameplay authority. */
[[nodiscard]] ClimbingGraphProvider useDirectClimbingPose(ClimbingExecutionId executionId,
                                                          ClimbingGraphState& state) noexcept;

}  // namespace eve::climbing
