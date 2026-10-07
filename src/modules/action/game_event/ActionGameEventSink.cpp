#include "action/game_event/ActionGameEventSink.h"

#include "common/Capability.h"

#include <utility>

namespace eve::action::game_event_adapter {
  // namespace

ActionGameEventSink::ActionGameEventSink(eve::game_event::GameEventLog& log, ActionSubjectResolver resolver)
    : log_(&log), resolver_(std::move(resolver)) {}

ActionGameEventSink::~ActionGameEventSink() { cap::removeListener<IActionGameplayEventSink>(this); }

bool ActionGameEventSink::enabled() const {
    for (std::size_t index = 0; index < cap::listenerCount<IActionGameplayEventSink>(); ++index)
        if (cap::listenerAt<IActionGameplayEventSink>(index) == this) return true;
    return false;
}

void ActionGameEventSink::setEnabled(bool value) {
    const bool current = enabled();
    if (value && !current)
        cap::addListener<IActionGameplayEventSink>(this);
    else if (!value && current)
        cap::removeListener<IActionGameplayEventSink>(this);
}

Result<void> ActionGameEventSink::emit(const ActionGameplayEventBinding& binding,
                                       const ActionNotifyContext& context) {
    if (!log_) return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, std::move("game event log is unavailable"), std::move("log")));
    if (!resolver_) return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, std::move("action subject resolver is unavailable"), std::move("resolver")));

    std::optional<SubjectRef> source;
    if (context.source) {
        auto resolved = resolver_(*context.source);
        if (!resolved) return Result<void>::failure(resolved.status());
        source = resolved.value();
    }
    SubjectRef subject = SubjectRef::nil();
    if (binding.targetIndex) {
        if (*binding.targetIndex >= context.targets.size())
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, std::move("gameplay event target index is unavailable"), std::move("targetIndex")));
        auto resolved = resolver_(context.targets[*binding.targetIndex]);
        if (!resolved) return Result<void>::failure(resolved.status());
        subject = resolved.value();
    } else if (source) {
        subject = *source;
    } else {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, std::move("gameplay event has no source or target subject"), std::move("subject")));
    }

    auto payload = binding.data.toJson();
    if (!payload) return Result<void>::failure(payload.status());
    auto schema = LogicalId::parse("action:gameplay-event-v1");
    if (!schema) return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, std::move("built-in gameplay event schema is invalid"), std::move("schemaId")));

    eve::game_event::GameEvent envelope;
    envelope.type = binding.tag;
    envelope.source = source ? source->format() : std::string{};
    envelope.subject = subject.format();
    envelope.schemaId = *schema;
    envelope.schemaVersion = SchemaVersion(1);
    envelope.tick = context.tick;
    envelope.payload = std::move(payload).takeValue();
    auto appended = log_->append(std::move(envelope));
    if (!appended) return Result<void>::failure(appended.status());
    lastSequence_ = appended.value();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

std::optional<eve::game_event::EventSequence> ActionGameEventSink::lastSequence() const noexcept {
    return lastSequence_;
}

}  // namespace eve::action::game_event_adapter
