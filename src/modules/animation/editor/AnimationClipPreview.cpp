#include "animation/editor/AnimationClipPreview.h"
#include "animation/editor/AnimationClipEditor.h"
#include "editor/presentation/AsyncPreview.h"

#include <map>

namespace eve::animation_editor {
namespace {
class NativeClipProvider final : public IAnimationClipPreviewProvider {
public:
    animation_editing::Result<AnimationPreviewPublication> generate(
        const animation_editing::AnimationClipDocumentTarget& document, double time,
        const std::vector<AnimationPreviewBone>& skeleton, const std::string& selectedBone,
        const editing::TaskContext& context) const override {
        std::vector<std::string>           names;
        std::map<std::string, std::string> parents;
        for (const auto& bone : skeleton) {
            names.push_back(bone.name);
            parents.emplace(bone.name, bone.parent);
        }
        auto sampled = document.preview(time, names);
        if (sampled.status != editing::Status::Applied)
            return editing::failed<AnimationPreviewPublication>(
                sampled.status, editing::RuleId("editor.animation.preview"), "Could not sample animation pose");
        std::vector<animation_editing::SkeletonOverlayBone> bones;
        bones.reserve(sampled.bones.size());
        for (const auto& sample : sampled.bones) {
            if (context.isCancellationRequested())
                return editing::failed<AnimationPreviewPublication>(
                    editing::Status::Cancelled, editing::RuleId("editor.animation.preview-cancelled"),
                    "Pose request cancelled");
            animation_editing::SkeletonOverlayBone bone;
            bone.id         = animation_editing::StableId(sample.bone);
            bone.parent     = animation_editing::StableId(parents.at(sample.bone));
            bone.name       = sample.bone;
            bone.position   = {sample.positionX, sample.positionY, sample.positionZ};
            bone.rotation   = {sample.rotationX, sample.rotationY, sample.rotationZ, sample.rotationW};
            bone.selected   = sample.bone == selectedBone;
            bone.maskWeight = sample.maskWeight;
            bones.push_back(std::move(bone));
        }
        animation_editing::SkeletonOverlayOptions options;
        options.showAxes            = true;
        options.axesForSelectedOnly = true;
        options.showConstraints     = false;
        auto overlay                = animation_editing::SkeletonOverlayBuilder{}.build(document.targetId().value(),
                                                                                        document.revision(), bones, options);
        if (overlay.status != editing::Status::Applied)
            return editing::failed<AnimationPreviewPublication>(
                overlay.status, editing::RuleId("editor.animation.overlay"), "Could not build skeleton overlay");
        return editing::applied(AnimationPreviewPublication{std::move(sampled), std::move(overlay)});
    }
};
struct Publication {
    AnimationPreviewPublication result;
    std::string                 selected;
    double                      requestedTime = 0;
};
}  // namespace
std::shared_ptr<const IAnimationClipPreviewProvider> makeAnimationClipPreviewProvider() {
    return std::make_shared<NativeClipProvider>();
}
struct AnimationClipEditor::PreviewSession {
    editor::AsyncPreview<Publication>                    async;
    std::shared_ptr<const IAnimationClipPreviewProvider> provider = makeAnimationClipPreviewProvider();
};
void AnimationClipEditor::PreviewSessionDeleter::operator()(PreviewSession* value) const { delete value; }
void AnimationClipEditor::initializePreview() { previewSession_.reset(new PreviewSession); }
AnimationClipEditor::~AnimationClipEditor() { closePreview(); }
editor::PreviewState AnimationClipEditor::previewState() const noexcept { return previewSession_->async.state(); }
animation_editing::Result<void> AnimationClipEditor::setPreviewProvider(
    std::shared_ptr<const IAnimationClipPreviewProvider> provider) {
    auto changed = previewSession_->async.replaceProvider(provider != nullptr);
    if (!changed.ok()) return changed;
    previewSession_->provider = std::move(provider);
    return editing::applied<void>();
}
animation_editing::Result<void> AnimationClipEditor::cancelPreview() { return previewSession_->async.cancel(); }
void                            AnimationClipEditor::closePreview() {
    previewSession_->async.close();
    previewSession_->provider.reset();
}
animation_editing::Result<void> AnimationClipEditor::requestPreview() {
    auto provider = previewSession_->provider;
    if (!provider)
        return editing::failed<void>(editing::Status::Unsupported, editing::RuleId("editor.animation.preview-provider"),
                                     "Pose provider is unavailable");
    std::vector<AnimationPreviewBone> skeleton;
    for (const auto& bone : skeleton_) skeleton.push_back({bone.name, bone.parent});
    previewDirty_ = false;
    // Copy the authoritative document once; the worker never borrows the editor or its target.
    return previewSession_->async.request(
        [provider = std::move(provider), document = target_, time = playhead_, skeleton = std::move(skeleton),
         selected = selectedBone_](const editing::TaskContext& context) -> editing::Result<Publication> {
            auto result = provider->generate(document, time, skeleton, selected, context);
            if (!result.ok()) return editing::Result<Publication>::failure(result.status());
            return editing::applied(Publication{std::move(result).takeValue(), selected, time});
        });
}
animation_editing::Result<void> AnimationClipEditor::pollPreview() {
    auto result = previewSession_->async.poll();
    if (!result) return previewState().phase == editor::PreviewPhase::Pending ? editing::pending() : editing::noOp();
    if (!result->ok()) return editing::Result<void>::failure(result->status());
    auto  publication = std::move(*result).takeValue();
    auto& pose        = publication.result.pose;
    auto& overlay     = publication.result.overlay;
    if (pose.documentRevision != revision() || publication.requestedTime != playhead_ ||
        publication.selected != selectedBone_ || pose.status != editing::Status::Applied ||
        overlay.status != editing::Status::Applied || overlay.targetRevision != revision() ||
        overlay.target != target_.targetId().value()) {
        previewSession_->async.rejectPublication();
        return editing::failed<void>(editing::Status::Conflict, editing::RuleId("editor.animation.stale-preview"),
                                     "Pose inputs changed before publication");
    }
    preview_ = std::move(pose);
    overlay_ = std::move(overlay);
    return editing::applied<void>();
}
}  // namespace eve::animation_editor
