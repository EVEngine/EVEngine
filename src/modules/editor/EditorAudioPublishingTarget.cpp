#include "editor/EditorAudioTarget.h"

namespace eve::editor {
namespace {

template <class T>
EditorResult<T> publishingError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

}  // namespace

AudioSourcePublishingTarget::AudioSourcePublishingTarget(
    std::string id, IAudioSourceRuntimeSink* sink)
    : document_(std::move(id)), sink_(sink) {}

EditorResult<void> AudioSourcePublishingTarget::applyDomainOperation(
    const DomainOperation& operation) {
    if (staging_) return document_.applyDomainOperation(operation);
    auto candidate = cloneDomainState();
    auto applied = candidate->applyDomainOperation(operation);
    if (!applied.accepted()) return applied;
    return commitDomainState(std::move(candidate));
}

std::unique_ptr<IDomainOperationTarget> AudioSourcePublishingTarget::cloneDomainState() const {
    auto candidate = std::make_unique<AudioSourcePublishingTarget>(*this);
    candidate->staging_ = true;
    return candidate;
}

EditorResult<void> AudioSourcePublishingTarget::commitDomainState(
    std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* typed = dynamic_cast<AudioSourcePublishingTarget*>(candidate.get());
    if (!typed || typed->targetId() != targetId() || typed->sink_ != sink_ || !typed->staging_)
        return publishingError<void>(EditorStatus::Conflict,
                                     "editor.audio.publishing-candidate-mismatch",
                                     "Audio publishing candidate belongs to another live target");
    if (!sink_)
        return publishingError<void>(EditorStatus::Rejected,
                                     "editor.audio.publishing-sink-missing",
                                     "Audio publishing target requires a live runtime sink");
    EditorResult<void> published = sink_->publish(typed->document_);
    if (!published.accepted()) return published;
    document_ = typed->document_;
    return EditorResult<void>::applied();
}

}  // namespace eve::editor
