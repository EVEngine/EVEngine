#include "editor/EditorPhysicsTarget.h"

namespace eve::editor {
namespace {

template <class T>
EditorResult<T> publishingError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

}  // namespace

PhysicsColliderPublishingTarget::PhysicsColliderPublishingTarget(
    std::string id, int dimensions, IPhysicsColliderRuntimeSink* sink)
    : document_(std::move(id), dimensions), sink_(sink) {}

EditorResult<void> PhysicsColliderPublishingTarget::applyDomainOperation(
    const DomainOperation& operation) {
    if (staging_) return document_.applyDomainOperation(operation);
    auto candidate = cloneDomainState();
    auto applied = candidate->applyDomainOperation(operation);
    if (!applied.accepted()) return applied;
    return commitDomainState(std::move(candidate));
}

std::unique_ptr<IDomainOperationTarget> PhysicsColliderPublishingTarget::cloneDomainState() const {
    auto candidate = std::make_unique<PhysicsColliderPublishingTarget>(*this);
    candidate->staging_ = true;
    return candidate;
}

EditorResult<void> PhysicsColliderPublishingTarget::commitDomainState(
    std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* typed = dynamic_cast<PhysicsColliderPublishingTarget*>(candidate.get());
    if (!typed || typed->targetId() != targetId() || typed->sink_ != sink_ || !typed->staging_ ||
        typed->document_.describe().type != document_.describe().type)
        return publishingError<void>(EditorStatus::Conflict,
                                     "editor.physics.publishing-candidate-mismatch",
                                     "Collider candidate belongs to another live target");
    if (!sink_)
        return publishingError<void>(EditorStatus::Rejected,
                                     "editor.physics.publishing-sink-missing",
                                     "Collider publishing target requires a live runtime sink");
    EditorResult<void> published = sink_->publish(typed->document_);
    if (!published.accepted()) return published;
    document_ = typed->document_;
    return EditorResult<void>::applied();
}

}  // namespace eve::editor
