#include "editor/EditorAnimationClip.h"

#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"

namespace eve::editor {

EditorResult<animation::AnimClip*> AnimationClipRuntimeBuilder::build(
    const AnimationClipDocumentTarget& document, const animation::AnimSkeleton* skeleton) const {
    if (!skeleton)
        return EditorResult<animation::AnimClip*>::error(EditorStatus::Rejected,
            RuleId("editor.animation.skeleton-required"), "Runtime clip building requires a target skeleton");
    std::vector<std::string> bones;
    for (int index = 0; index < skeleton->getBoneCount(); ++index) bones.push_back(skeleton->getBoneName(index));
    const auto diagnostics = document.validate(bones);
    for (const auto& diagnostic : diagnostics)
        if (diagnostic.severity == DiagnosticSeverity::Error)
            return EditorResult<animation::AnimClip*>::error(EditorStatus::Rejected, diagnostic.rule, diagnostic.message);

    const auto snapshot = document.snapshotValue();
    const auto* root = snapshot.getIf<EditorValue::Object>();
    const auto* settings = root ? root->at("settings").getIf<EditorValue::Object>() : nullptr;
    if (!settings)
        return EditorResult<animation::AnimClip*>::error(EditorStatus::Failed,
            RuleId("editor.animation.runtime-snapshot"), "Clip settings are unavailable");
    auto* clip = new animation::AnimClip(document.targetId());
    clip->setDuration(static_cast<float>(*settings->at("duration").getIf<double>()));
    clip->setSampleRate(static_cast<float>(*settings->at("sampleRate").getIf<double>()));
    clip->setLoop(*settings->at("loop").getIf<bool>());
    for (const auto& track : document.tracks()) {
        const int bone = skeleton->findBone(track.bone);
        for (const auto& key : track.keys) {
            clip->addPositionKey(bone, static_cast<float>(key.time), static_cast<float>(key.positionX),
                                 static_cast<float>(key.positionY), static_cast<float>(key.positionZ));
            clip->addRotationKey(bone, static_cast<float>(key.time), static_cast<float>(key.rotationX),
                                 static_cast<float>(key.rotationY), static_cast<float>(key.rotationZ),
                                 static_cast<float>(key.rotationW));
            clip->addScaleKey(bone, static_cast<float>(key.time), static_cast<float>(key.scaleX),
                              static_cast<float>(key.scaleY), static_cast<float>(key.scaleZ));
        }
    }
    for (const auto& event : document.events())
        clip->addEvent(static_cast<float>(event.time), event.name, event.payload);
    return EditorResult<animation::AnimClip*>::applied(clip);
}

}  // namespace eve::editor
