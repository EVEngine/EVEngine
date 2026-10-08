#include "action/input/ActionCancelWindowState.h"

#include "common/Capability.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace eve::action::input {
namespace {

std::vector<std::string> splitAllows(const std::string& resource) {
    std::vector<std::string> allows;
    std::stringstream stream(resource);
    std::string item;
    while (std::getline(stream, item, ';')) {
        if (!item.empty()) allows.push_back(item);
    }
    std::sort(allows.begin(), allows.end());
    allows.erase(std::unique(allows.begin(), allows.end()), allows.end());
    return allows;
}

}  // namespace

ActionCancelWindowState::ActionCancelWindowState(CancelSubjectResolver resolver) : resolver_(std::move(resolver)) {}

ActionCancelWindowState::~ActionCancelWindowState() { cap::removeListener<IActionStateWindowSink>(this); }

bool ActionCancelWindowState::enabled() const {
    for (std::size_t index = 0; index < cap::listenerCount<IActionStateWindowSink>(); ++index)
        if (cap::listenerAt<IActionStateWindowSink>(index) == this) return true;
    return false;
}

void ActionCancelWindowState::setEnabled(bool value) {
    const bool current = enabled();
    if (value && !current)
        cap::addListener<IActionStateWindowSink>(this);
    else if (!value && current)
        cap::removeListener<IActionStateWindowSink>(this);
}

bool ActionCancelWindowState::supports(ActionStateWindowKind kind) const noexcept {
    return kind == ActionStateWindowKind::Cancel;
}

Result<void> ActionCancelWindowState::enter(const ActionStateWindowBinding& binding,
                                            const ActionTimelineEvent& event,
                                            const ActionNotifyContext& context) {
    if (!supports(binding.kind))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Unsupported, "input adapter does not own this window kind", "kind"));
    if (!resolver_)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "cancel subject resolver is unavailable", "resolver"));
    auto allows = splitAllows(binding.resource);
    if (allows.empty())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "cancel allow-list is empty", "allows"));
    std::optional<ecs::EntityHandle> handle;
    if (binding.targetIndex) {
        if (*binding.targetIndex >= context.targets.size())
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "cancel target index is unavailable", "targetIndex"));
        handle = context.targets[*binding.targetIndex];
    } else {
        handle = context.source;
    }
    if (!handle)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "cancel window has no source or target", "subject"));
    auto subject = resolver_(*handle);
    if (!subject) return Result<void>::failure(subject.status());
    if (!subject.value().isValid())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "cancel-window subject is nil", "subject"));

    ActiveKey key{context.executionId, event.itemId.format()};
    const auto found = active_.find(key);
    if (found != active_.end()) {
        if (found->second.subject == subject.value() && found->second.allows == allows &&
            found->second.priority == binding.priority)
            return Result<void>::success(Status::success(StatusCode::NoOp));
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "cancel-window key has different active data", "itemId"));
    }
    active_.emplace(std::move(key),
                    ActiveCancel{event.itemId, subject.value(), std::move(allows), binding.priority});
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> ActionCancelWindowState::exit(const ActionStateWindowBinding& binding,
                                           const ActionTimelineEvent& event,
                                           const ActionNotifyContext& context) {
    if (!supports(binding.kind))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Unsupported, "input adapter does not own this window kind", "kind"));
    const ActiveKey key{context.executionId, event.itemId.format()};
    const auto found = active_.find(key);
    if (found == active_.end()) return Result<void>::success(Status::success(StatusCode::NoOp));
    active_.erase(found);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

std::vector<std::string> ActionCancelWindowState::availableCancels(SubjectRef subject) const {
    std::vector<std::string> result;
    for (const auto& [key, window] : active_) {
        (void)key;
        if (window.subject != subject) continue;
        result.insert(result.end(), window.allows.begin(), window.allows.end());
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

Result<ActionCancelMatch> ActionCancelWindowState::match(SubjectRef subject, std::string_view input) const {
    const ActiveCancel* best = nullptr;
    ActionExecutionId bestExecution{};
    for (const auto& [key, window] : active_) {
        if (window.subject != subject) continue;
        if (std::find(window.allows.begin(), window.allows.end(), input) == window.allows.end()) continue;
        if (!best || window.priority > best->priority ||
            (window.priority == best->priority && key.first.value() < bestExecution.value())) {
            best = &window;
            bestExecution = key.first;
        }
    }
    if (!best)
        return Result<ActionCancelMatch>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "no cancel window matched input", "input"));
    return Result<ActionCancelMatch>::success(
        ActionCancelMatch{bestExecution, best->itemId, best->subject, std::string(input), best->priority});
}

}  // namespace eve::action::input
