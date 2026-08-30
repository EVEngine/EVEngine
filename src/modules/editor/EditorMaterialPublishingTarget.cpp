#include "editor/EditorMaterialTarget.h"

namespace eve::editor {
namespace {

template <class T>
EditorResult<T> publishingError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

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
    if (!applied.isAccepted()) return applied;
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
        return publishingError<void>(EditorStatus::Conflict,
                                     "editor.material.publishing-candidate-mismatch",
                                     "Material candidate belongs to another live target");
    if (!sink_)
        return publishingError<void>(EditorStatus::Rejected,
                                     "editor.material.publishing-sink-missing",
                                     "Material publishing target requires a live runtime sink");
    EditorResult<void> published = sink_->publish(typed->document_);
    if (!published.isAccepted()) return published;
    document_ = typed->document_;
    return EditorResult<void>::applied();
}

}  // namespace eve::editor
