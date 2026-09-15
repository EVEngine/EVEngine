#include "action/input/ActionComboWindowState.h"

#include "common/Capability.h"

#include <algorithm>
#include <utility>

namespace eve::action::input {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

}  // namespace

ActionComboWindowState::ActionComboWindowState(ComboSubjectResolver resolver) : resolver_(std::move(resolver)) {}

ActionComboWindowState::~ActionComboWindowState() { cap::removeListener<IActionStateWindowSink>(this); }

bool ActionComboWindowState::enabled() const {
    for (std::size_t index = 0; index < cap::listenerCount<IActionStateWindowSink>(); ++index)
        if (cap::listenerAt<IActionStateWindowSink>(index) == this) return true;
    return false;
}

void ActionComboWindowState::setEnabled(bool value) {
    const bool current = enabled();
    if (value && !current)
        cap::addListener<IActionStateWindowSink>(this);
    else if (!value && current)
        cap::removeListener<IActionStateWindowSink>(this);
}

bool ActionComboWindowState::supports(ActionStateWindowKind kind) const noexcept {
    return kind == ActionStateWindowKind::Combo;
}

Result<void> ActionComboWindowState::enter(const ActionStateWindowBinding& binding,
                                           const ActionTimelineEvent& event,
                                           const ActionNotifyContext& context) {
    if (!supports(binding.kind))
        return failure<void>(DiagnosticCode::Unsupported, "input adapter does not own this window kind", "kind");
    if (!resolver_) return failure<void>(DiagnosticCode::NotFound, "combo subject resolver is unavailable", "resolver");
    std::optional<ecs::EntityHandle> handle;
    if (binding.targetIndex) {
        if (*binding.targetIndex >= context.targets.size())
            return failure<void>(DiagnosticCode::NotFound, "combo target index is unavailable", "targetIndex");
        handle = context.targets[*binding.targetIndex];
    } else {
        handle = context.source;
    }
    if (!handle) return failure<void>(DiagnosticCode::NotFound, "combo window has no source or target", "subject");
    auto subject = resolver_(*handle);
    if (!subject) return Result<void>::failure(subject.status());
    if (!subject.value().isValid())
        return failure<void>(DiagnosticCode::InvalidArgument, "combo-window subject is nil", "subject");

    ActiveKey key{context.executionId, event.itemId.format()};
    const auto found = active_.find(key);
    if (found != active_.end()) {
        if (found->second.subject == subject.value() && found->second.input == binding.resource)
            return Result<void>::success(Status::success(StatusCode::NoOp));
        return failure<void>(DiagnosticCode::Conflict, "combo-window key has different active data", "itemId");
    }
    active_.emplace(std::move(key), ActiveCombo{event.itemId, subject.value(), binding.resource});
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> ActionComboWindowState::exit(const ActionStateWindowBinding& binding,
                                          const ActionTimelineEvent& event,
                                          const ActionNotifyContext& context) {
    if (!supports(binding.kind))
        return failure<void>(DiagnosticCode::Unsupported, "input adapter does not own this window kind", "kind");
    const ActiveKey key{context.executionId, event.itemId.format()};
    const auto found = active_.find(key);
    if (found == active_.end()) return Result<void>::success(Status::success(StatusCode::NoOp));
    active_.erase(found);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

std::vector<std::string> ActionComboWindowState::availableInputs(SubjectRef subject) const {
    std::vector<std::string> result;
    for (const auto& [key, combo] : active_) {
        (void)key;
        if (combo.subject == subject) result.push_back(combo.input);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

Result<ActionComboMatch> ActionComboWindowState::match(SubjectRef subject, std::string_view input) const {
    if (!subject.isValid())
        return failure<ActionComboMatch>(DiagnosticCode::InvalidArgument, "combo subject is nil", "subject");
    if (input.empty()) return failure<ActionComboMatch>(DiagnosticCode::InvalidArgument, "combo input is empty", "input");
    for (const auto& [key, combo] : active_) {
        if (combo.subject == subject && combo.input == input)
            return Result<ActionComboMatch>::success(
                ActionComboMatch{key.first, combo.itemId, combo.subject, combo.input});
    }
    return failure<ActionComboMatch>(DiagnosticCode::NotFound, "no active combo window accepts the input", "input");
}

}  // namespace eve::action::input
