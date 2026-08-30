#include "animation_editing/SkeletonOverlay.h"

#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"

namespace eve::animation_editing {

editing::GizmoSnapshot AnimationSkeletonOverlayAdapter::build(
    animation::AnimSkeleton* skeleton, animation::AnimPose* pose, std::string target,
    Revision revision, const std::string& selectedBone,
    const std::map<std::string, std::string>& retarget,
    const std::map<std::string, double>& mask, const SkeletonOverlayOptions& options) const {
    if (!skeleton || !pose || skeleton->getBoneCount() != pose->getBoneCount()) {
        editing::GizmoSnapshot result;
        result.target = std::move(target);
        result.targetRevision = revision;
        result.diagnostics.push_back(
            {RuleId("editor.skeleton.runtime-input"), DiagnosticSeverity::Error,
             "Animation skeleton and matching pose are required"});
        return result;
    }
    pose->computeWorld(skeleton);
    std::vector<SkeletonOverlayBone> bones;
    bones.reserve(static_cast<size_t>(skeleton->getBoneCount()));
    for (int index = 0; index < skeleton->getBoneCount(); ++index) {
        SkeletonOverlayBone bone;
        bone.name = skeleton->getBoneName(index);
        bone.id = StableId(bone.name);
        const int parent = skeleton->getParent(index);
        if (parent >= 0) bone.parent = StableId(skeleton->getBoneName(parent));
        bone.position = {pose->getWorldPositionX(index), pose->getWorldPositionY(index),
                         pose->getWorldPositionZ(index)};
        bone.rotation = {pose->getWorldRotationX(index), pose->getWorldRotationY(index),
                         pose->getWorldRotationZ(index), pose->getWorldRotationW(index)};
        bone.selected = bone.name == selectedBone;
        const auto mapped = retarget.find(bone.name);
        if (mapped != retarget.end())
            bone.retarget = mapped->second.empty() ? SkeletonRetargetState::Unmatched
                                                   : SkeletonRetargetState::Matched;
        const auto weight = mask.find(bone.name);
        if (weight != mask.end()) bone.maskWeight = weight->second;
        bones.push_back(std::move(bone));
    }
    return SkeletonOverlayBuilder().build(std::move(target), revision, bones, options);
}

}  // namespace eve::animation_editing
