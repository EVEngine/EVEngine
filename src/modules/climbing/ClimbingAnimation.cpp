#include "climbing/ClimbingAnimation.h"

#include "animation/AnimClip.h"
#include "animation/AnimGraph.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimSkeleton.h"
#include "animation/MotionMatcher.h"
#include "common/Exception.h"

#include <cmath>
#include <optional>
#include <utility>

namespace eve::climbing {
namespace {

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path), {},
                                                          "climbing.animation"));
}

std::optional<ClimbingNotifyKind> parseNotify(std::string_view name) {
    if (name == "contact.left_hand") return ClimbingNotifyKind::ContactLeftHand;
    if (name == "contact.right_hand") return ClimbingNotifyKind::ContactRightHand;
    if (name == "collision.compact") return ClimbingNotifyKind::CollisionCompact;
    if (name == "branch.open") return ClimbingNotifyKind::BranchOpen;
    if (name == "branch.close") return ClimbingNotifyKind::BranchClose;
    if (name == "land") return ClimbingNotifyKind::Land;
    return std::nullopt;
}

bool finite(Vec3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

}  // namespace

eve::Result<void> beginClimbingAnimation(animation::AnimPlayer& player, animation::AnimClip& clip,
                                         int rootMotionBone) {
    animation::AnimSkeleton* skeleton = player.getSkeleton();
    if (!skeleton || rootMotionBone < 0 || rootMotionBone >= skeleton->getBoneCount())
        return failure<void>(eve::DiagnosticCode::InvalidArgument, "root-motion bone is invalid", "rootMotionBone");
    if (!std::isfinite(clip.getDuration()) || clip.getDuration() <= 0.f)
        return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                             "climbing animation clip must have a positive finite duration", "clip.duration");
    player.setRootMotionBone(rootMotionBone);
    player.setLoop(false);
    player.play(&clip);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> beginClimbingAnimation(animation::AnimPlayer& player, animation::AnimClip& clip,
                                         const ClimbingAnimationBinding& binding) {
    if (!binding.clipId.empty() && clip.getName() != binding.clipId)
        return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                             "animation clip does not match the climbing binding", "binding.clipId");
    animation::AnimSkeleton* skeleton = player.getSkeleton();
    if (!skeleton)
        return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                             "animation player has no skeleton", "player.skeleton");
    const int rootMotionBone = skeleton->findBone(binding.rootBone);
    if (rootMotionBone < 0)
        return failure<void>(eve::DiagnosticCode::NotFound,
                             "climbing root-motion bone does not exist", "binding.rootBone");
    return beginClimbingAnimation(player, clip, rootMotionBone);
}

eve::Result<ClimbingAnimationFrame> advanceClimbingAnimation(animation::AnimPlayer& player,
                                                              const eve::SimulationStep& step, Vec3 facing,
                                                              Vec3 pelvisOffset) {
    if (!finite(facing) || !finite(pelvisOffset))
        return failure<ClimbingAnimationFrame>(eve::DiagnosticCode::InvalidArgument,
                                               "facing and pelvis offset must be finite", "animationInput");
    auto advanced = player.advance(step);
    if (!advanced) return eve::Result<ClimbingAnimationFrame>::failure(advanced.status());

    ClimbingAnimationFrame frame;
    frame.motion.rootTranslation = {player.getRootMotionX(), player.getRootMotionY(), player.getRootMotionZ()};
    frame.motion.facing           = facing;
    frame.motion.pelvisOffset     = pelvisOffset;
    const float qx                = player.getRootMotionRotationX();
    const float qy                = player.getRootMotionRotationY();
    const float qz                = player.getRootMotionRotationZ();
    const float qw                = player.getRootMotionRotationW();
    frame.motion.rootYawRadians   = std::atan2(2.f * (qw * qy + qx * qz), 1.f - 2.f * (qy * qy + qz * qz));
    frame.motion.hasRootMotion    = player.isPlaying();
    if (!finite(frame.motion.rootTranslation) || !std::isfinite(frame.motion.rootYawRadians))
        return failure<ClimbingAnimationFrame>(eve::DiagnosticCode::InvariantViolation,
                                               "animation player produced non-finite root motion", "rootMotion");

    for (int index = 0; index < player.getEventCount(); ++index) {
        const auto parsed = parseNotify(player.getEventName(index));
        if (parsed) {
            frame.motion.notifies.push_back(*parsed);
            ++frame.recognizedNotifyCount;
        } else {
            ++frame.ignoredNotifyCount;
        }
    }
    return eve::Result<ClimbingAnimationFrame>::success(std::move(frame),
                                                         eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<ClimbingGraphProvider> driveClimbingGraph(animation::AnimGraph& graph, int oneShotNode,
                                                       ClimbingExecutionId executionId, ClimbingGraphState& state) {
    if (executionId.isZero())
        return failure<ClimbingGraphProvider>(eve::DiagnosticCode::InvalidArgument,
                                              "graph projection requires a committed execution id", "executionId");
    if (oneShotNode < 0 || oneShotNode >= graph.getNodeCount())
        return failure<ClimbingGraphProvider>(eve::DiagnosticCode::InvalidArgument,
                                              "climbing graph one-shot node is invalid", "oneShotNode");
    if (state.activeExecutionId == executionId && state.provider == ClimbingGraphProvider::AnimGraph)
        return eve::Result<ClimbingGraphProvider>::success(ClimbingGraphProvider::AnimGraph,
                                                            eve::Status::success(eve::StatusCode::NoOp));
    try {
        graph.trigger(oneShotNode);
    } catch (const eve::Exception& exception) {
        return failure<ClimbingGraphProvider>(eve::DiagnosticCode::PreconditionViolation, exception.what(),
                                              "oneShotNode");
    }
    state.activeExecutionId = executionId;
    state.provider          = ClimbingGraphProvider::AnimGraph;
    return eve::Result<ClimbingGraphProvider>::success(state.provider,
                                                        eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<ClimbingGraphProvider> driveClimbingMotionMatching(
    animation::MotionMatcher& matcher, const eve::SimulationStep& step, Vec3 desiredVelocity,
    float desiredYaw, ClimbingGraphState& state) {
    if (!finite(desiredVelocity) || !std::isfinite(desiredYaw))
        return failure<ClimbingGraphProvider>(eve::DiagnosticCode::InvalidArgument,
                                              "motion-matching intent must be finite", "motionMatching.intent");
    matcher.setDesiredVelocity(desiredVelocity.x, desiredVelocity.z);
    matcher.setDesiredYaw(desiredYaw);
    auto advanced = matcher.advance(step);
    if (!advanced) return eve::Result<ClimbingGraphProvider>::failure(advanced.status());
    state.activeExecutionId = ClimbingExecutionId::zero();
    state.provider = ClimbingGraphProvider::MotionMatching;
    return eve::Result<ClimbingGraphProvider>::success(state.provider,
                                                        eve::Status::success(eve::StatusCode::Applied));
}

ClimbingGraphProvider useDirectClimbingPose(ClimbingExecutionId executionId, ClimbingGraphState& state) noexcept {
    state.activeExecutionId = executionId;
    state.provider          = ClimbingGraphProvider::DirectPoseConstraints;
    return state.provider;
}

}  // namespace eve::climbing
