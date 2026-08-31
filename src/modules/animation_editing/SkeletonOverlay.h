#pragma once

#include "editing/EditingGizmo.h"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace eve::animation {
class AnimPose;
class AnimSkeleton;
}

namespace eve::animation_editing {
using DiagnosticSeverity=editing::DiagnosticSeverity; using Revision=editing::Revision;
using EditorStatus=editing::Status; using RuleId=editing::RuleId; using StableId=editing::StableId;

/** @brief Retarget state used to color one skeleton overlay bone. */
enum class SkeletonRetargetState { Unspecified, Matched, Unmatched, Ambiguous };

/** @brief Immutable renderer-neutral world-space bone snapshot. */
struct SkeletonOverlayBone {
    StableId id;
    StableId parent;
    std::string name;
    std::array<double, 3> position{0.0, 0.0, 0.0};
    std::array<double, 4> rotation{0.0, 0.0, 0.0, 1.0};
    bool selected = false;
    bool constrained = false;
    std::array<double, 2> minimumAngles{0.0, 0.0};
    std::array<double, 2> maximumAngles{0.0, 0.0};
    SkeletonRetargetState retarget = SkeletonRetargetState::Unspecified;
    double maskWeight = 1.0;
};

/** @brief Overlay density, feature toggles and hard safety budgets. */
struct SkeletonOverlayOptions {
    std::size_t maximumBones = 4096;
    double jointRadius = 0.025;
    double axisLength = 0.15;
    bool showAxes = true;
    bool axesForSelectedOnly = true;
    bool showConstraints = true;
};

/** @brief Builds bone lines, joints, local axes and constraint arcs without a renderer dependency. */
class SkeletonOverlayBuilder {
public:
    /** @brief Build a deterministic overlay or structured hierarchy/value diagnostics. */
    editing::GizmoSnapshot build(std::string target, Revision revision,
                              const std::vector<SkeletonOverlayBone>& bones,
                              const SkeletonOverlayOptions& options = {}) const;
};

/** @brief Optional bridge extracting an overlay from a real animation pose. */
class AnimationSkeletonOverlayAdapter {
public:
    /**
     * @brief Compute pose world cache and build an immutable overlay.
     * @param skeleton Borrowed skeleton.
     * @param pose Borrowed pose; only its derived world cache is updated.
     * @param target Stable editor target id.
     * @param revision Authoritative source revision.
     * @param selectedBone Optional stable bone name to highlight.
     * @param retarget Source-name to target-name mapping; empty target marks unmatched.
     * @param mask Optional normalized per-bone blend mask.
     */
    editing::GizmoSnapshot build(animation::AnimSkeleton* skeleton, animation::AnimPose* pose,
                              std::string target, Revision revision,
                              const std::string& selectedBone = {},
                              const std::map<std::string, std::string>& retarget = {},
                              const std::map<std::string, double>& mask = {},
                              const SkeletonOverlayOptions& options = {}) const;
};

}  // namespace eve::animation_editing
