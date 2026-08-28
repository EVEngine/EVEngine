#include "editor/EditorSkeletonOverlay.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace eve::editor {
namespace {

bool finite(const SkeletonOverlayBone& bone) {
    for (double value : bone.position)
        if (!std::isfinite(value)) return false;
    for (double value : bone.rotation)
        if (!std::isfinite(value)) return false;
    return std::isfinite(bone.maskWeight) && bone.maskWeight >= 0.0 && bone.maskWeight <= 1.0;
}

std::array<double, 3> axis(const std::array<double, 4>& source, int index) {
    double x = source[0], y = source[1], z = source[2], w = source[3];
    const double length = std::sqrt(x * x + y * y + z * z + w * w);
    if (length <= 1e-12) return index == 0 ? std::array<double, 3>{1.0, 0.0, 0.0}
                                           : index == 1 ? std::array<double, 3>{0.0, 1.0, 0.0}
                                                        : std::array<double, 3>{0.0, 0.0, 1.0};
    x /= length; y /= length; z /= length; w /= length;
    if (index == 0) return {1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y + w * z),
                            2.0 * (x * z - w * y)};
    if (index == 1) return {2.0 * (x * y - w * z), 1.0 - 2.0 * (x * x + z * z),
                            2.0 * (y * z + w * x)};
    return {2.0 * (x * z + w * y), 2.0 * (y * z - w * x),
            1.0 - 2.0 * (x * x + y * y)};
}

std::array<double, 4> color(const SkeletonOverlayBone& bone) {
    std::array<double, 4> result{0.35, 0.7, 1.0, 1.0};
    if (bone.selected) result = {1.0, 0.78, 0.15, 1.0};
    else if (bone.retarget == SkeletonRetargetState::Unmatched) result = {1.0, 0.2, 0.2, 1.0};
    else if (bone.retarget == SkeletonRetargetState::Ambiguous) result = {1.0, 0.45, 0.1, 1.0};
    else if (bone.retarget == SkeletonRetargetState::Matched) result = {0.25, 0.9, 0.55, 1.0};
    const double intensity = 0.25 + 0.75 * bone.maskWeight;
    for (size_t index = 0; index < 3; ++index) result[index] *= intensity;
    return result;
}

EditorGizmoSnapshot failure(std::string target, Revision revision, const char* rule,
                            std::string message) {
    EditorGizmoSnapshot result;
    result.target = std::move(target);
    result.targetRevision = revision;
    result.diagnostics.push_back({RuleId(rule), DiagnosticSeverity::Error, std::move(message)});
    return result;
}

}  // namespace

EditorGizmoSnapshot SkeletonOverlayBuilder::build(
    std::string target, Revision revision, const std::vector<SkeletonOverlayBone>& bones,
    const SkeletonOverlayOptions& options) const {
    if (target.empty() || bones.empty())
        return failure(std::move(target), revision, "editor.skeleton.overlay-empty",
                       "Skeleton overlay requires a target and at least one bone");
    if (options.maximumBones == 0 || bones.size() > options.maximumBones ||
        !std::isfinite(options.jointRadius) || options.jointRadius <= 0.0 ||
        !std::isfinite(options.axisLength) || options.axisLength <= 0.0)
        return failure(std::move(target), revision, "editor.skeleton.overlay-budget",
                       "Skeleton overlay exceeds its bone or geometry budget");
    std::map<StableId, const SkeletonOverlayBone*> indexed;
    for (const SkeletonOverlayBone& bone : bones) {
        if (bone.id.empty() || !finite(bone) || !indexed.emplace(bone.id, &bone).second)
            return failure(std::move(target), revision, "editor.skeleton.overlay-bone",
                           "Skeleton overlay contains duplicate, empty or non-finite bone data");
        if (bone.constrained &&
            (bone.minimumAngles[0] > bone.maximumAngles[0] ||
             bone.minimumAngles[1] > bone.maximumAngles[1]))
            return failure(std::move(target), revision, "editor.skeleton.overlay-constraint",
                           "Skeleton constraint minimum exceeds maximum");
    }
    for (const SkeletonOverlayBone& bone : bones) {
        if (!bone.parent.empty() && !indexed.contains(bone.parent))
            return failure(std::move(target), revision, "editor.skeleton.overlay-parent",
                           "Skeleton bone references a missing parent: " + bone.id.value());
        std::set<StableId> ancestors;
        const SkeletonOverlayBone* current = &bone;
        while (current && !current->parent.empty()) {
            if (!ancestors.insert(current->id).second)
                return failure(std::move(target), revision, "editor.skeleton.overlay-cycle",
                               "Skeleton hierarchy contains a cycle");
            current = indexed.at(current->parent);
        }
    }

    EditorGizmoSnapshot result;
    result.target = std::move(target);
    result.targetRevision = revision;
    for (const SkeletonOverlayBone& bone : bones) {
        const auto rgba = color(bone);
        result.primitives.push_back({bone.id.value() + ":joint", "joint", bone.position, {}, {},
                                     rgba, options.jointRadius, 0.0,
                                     bone.retarget == SkeletonRetargetState::Unmatched});
        if (!bone.parent.empty()) {
            const SkeletonOverlayBone& parent = *indexed.at(bone.parent);
            std::array<double, 3> direction;
            double lengthSquared = 0.0;
            for (size_t index = 0; index < 3; ++index) {
                direction[index] = bone.position[index] - parent.position[index];
                lengthSquared += direction[index] * direction[index];
            }
            const double length = std::sqrt(lengthSquared);
            if (length > 1e-12) {
                for (double& value : direction) value /= length;
                result.primitives.push_back({bone.id.value() + ":bone", "bone-line",
                                             parent.position, {}, direction, rgba, 0.0, length,
                                             bone.retarget == SkeletonRetargetState::Unmatched});
            } else {
                result.diagnostics.push_back(
                    {RuleId("editor.skeleton.zero-length"), DiagnosticSeverity::Warning,
                     "Bone has the same world position as its parent: " + bone.name});
            }
        }
        if (options.showAxes && (!options.axesForSelectedOnly || bone.selected)) {
            static const std::array<std::array<double, 4>, 3> colors{{
                {1.0, 0.2, 0.2, 1.0}, {0.2, 1.0, 0.2, 1.0}, {0.2, 0.45, 1.0, 1.0}}};
            static const char* names[] = {"x", "y", "z"};
            for (int index = 0; index < 3; ++index)
                result.primitives.push_back({bone.id.value() + ":axis-" + names[index], "axis",
                                             bone.position, {}, axis(bone.rotation, index), colors[index],
                                             0.0, options.axisLength, false});
        }
        if (options.showConstraints && bone.constrained) {
            result.primitives.push_back({bone.id.value() + ":constraint-yaw", "constraint-arc",
                                         bone.position,
                                         {bone.minimumAngles[0], bone.maximumAngles[0], 0.0},
                                         axis(bone.rotation, 1), {1.0, 0.45, 0.1, 1.0},
                                         options.axisLength, 0.0, true});
            result.primitives.push_back({bone.id.value() + ":constraint-pitch", "constraint-arc",
                                         bone.position,
                                         {bone.minimumAngles[1], bone.maximumAngles[1], 0.0},
                                         axis(bone.rotation, 0), {0.8, 0.3, 1.0, 1.0},
                                         options.axisLength, 0.0, true});
        }
    }
    result.status = EditorStatus::Applied;
    return result;
}

}  // namespace eve::editor
