#include "graphics/material/editing/MaterialTarget.h"

namespace eve::material_editing {
namespace {

}  // namespace

MaterialPublishingTarget::MaterialPublishingTarget(std::string id, IMaterialRuntimeSink* sink)
    : document_(std::move(id)), sink_(sink) {}

TargetDescriptor MaterialPublishingTarget::describe() const {
    TargetDescriptor descriptor = document_.describe();
    descriptor.type = "material-runtime";
    return descriptor;
}

void* MaterialPublishingTarget::queryCapability(const CapabilityId& capability) {
    return document_.queryCapability(capability);
}

EditorResult<void> MaterialPublishingTarget::applyDomainOperation(
    const DomainOperation& operation) {
    if (staging_) return document_.applyDomainOperation(operation);
    auto candidate = cloneDomainState();
    auto applied = candidate->applyDomainOperation(operation);
    if (!applied.ok()) return applied;
    return commitDomainState(std::move(candidate));
}

std::unique_ptr<IDomainOperationTarget> MaterialPublishingTarget::cloneDomainState() const {
    auto candidate = std::make_unique<MaterialPublishingTarget>(*this);
    candidate->staging_ = true;
    return candidate;
}

EditorResult<void> MaterialPublishingTarget::commitDomainState(
    std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* typed = dynamic_cast<MaterialPublishingTarget*>(candidate.get());
    if (!typed || typed->targetId() != targetId() || typed->sink_ != sink_ || !typed->staging_)
        return eve::editing::failed<void>(EditorStatus::Conflict,
                                          RuleId("editor.material.publishing-candidate-mismatch"),
                                          "Material candidate belongs to another live target");
    if (!sink_)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.material.publishing-sink-missing"),
                                          "Material publishing target requires a live runtime sink");
    EditorResult<void> published = sink_->publish(typed->document_);
    if (!published.ok()) return published;
    document_ = typed->document_;
    return eve::editing::applied<void>();
}

EditorResult<void> MaterialPublishingTarget::reloadSnapshot(const EditorValue& snapshot) {
    auto candidate = std::make_unique<MaterialPublishingTarget>(*this);
    candidate->staging_ = true;
    auto loaded = candidate->document_.loadSnapshot(snapshot);
    if (!loaded.ok()) return loaded;
    return commitDomainState(std::move(candidate));
}

}  // namespace eve::material_editing
