#include "combat/GuardWindowState.h"

#include "common/Capability.h"

#include <utility>

namespace eve::combat {
namespace {

Result<void> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

Result<GuardMode> parseMode(const std::string& resource) {
    if (resource == "block") return Result<GuardMode>::success(GuardMode::Block);
    if (resource == "parry") return Result<GuardMode>::success(GuardMode::Parry);
    return Result<GuardMode>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "guard mode must be block or parry", "mode"));
}

}  // namespace

GuardWindowState::GuardWindowState(GuardSubjectResolver resolver) : resolver_(std::move(resolver)) {}

GuardWindowState::~GuardWindowState() { cap::removeListener<action::IActionStateWindowSink>(this); }

bool GuardWindowState::enabled() const {
    for (std::size_t index = 0; index < cap::listenerCount<action::IActionStateWindowSink>(); ++index)
        if (cap::listenerAt<action::IActionStateWindowSink>(index) == this) return true;
    return false;
}

void GuardWindowState::setEnabled(bool value) {
    const bool current = enabled();
    if (value && !current)
        cap::addListener<action::IActionStateWindowSink>(this);
    else if (!value && current)
        cap::removeListener<action::IActionStateWindowSink>(this);
}

bool GuardWindowState::supports(action::ActionStateWindowKind kind) const noexcept {
    return kind == action::ActionStateWindowKind::Guard;
}

Result<void> GuardWindowState::enter(const action::ActionStateWindowBinding& binding,
                                     const action::ActionTimelineEvent& event,
                                     const action::ActionNotifyContext& context) {
    if (!supports(binding.kind))
        return failure(DiagnosticCode::Unsupported, "combat guard does not own this state-window kind", "kind");
    if (!resolver_) return failure(DiagnosticCode::NotFound, "guard subject resolver is unavailable", "resolver");
    auto mode = parseMode(binding.resource);
    if (!mode) return Result<void>::failure(mode.status());
    std::optional<ecs::EntityHandle> handle;
    if (binding.targetIndex) {
        if (*binding.targetIndex >= context.targets.size())
            return failure(DiagnosticCode::NotFound, "guard target index is unavailable", "targetIndex");
        handle = context.targets[*binding.targetIndex];
    } else {
        handle = context.source;
    }
    if (!handle) return failure(DiagnosticCode::NotFound, "guard window has no source or target", "subject");
    auto subject = resolver_(*handle);
    if (!subject) return Result<void>::failure(subject.status());
    if (!subject.value().isValid())
        return failure(DiagnosticCode::InvalidArgument, "guard-window subject is nil", "subject");

    ActiveKey key{context.executionId, event.itemId.format()};
    const auto found = active_.find(key);
    if (found != active_.end()) {
        const bool same = found->second.subject == subject.value() && found->second.mode == mode.value();
        if (same) return Result<void>::success(Status::success(StatusCode::NoOp));
        return failure(DiagnosticCode::Conflict, "guard-window key is already active with different data", "itemId");
    }
    active_.emplace(std::move(key), ActiveGuard{binding, subject.value(), mode.value()});
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> GuardWindowState::exit(const action::ActionStateWindowBinding& binding,
                                    const action::ActionTimelineEvent& event,
                                    const action::ActionNotifyContext& context) {
    if (!supports(binding.kind))
        return failure(DiagnosticCode::Unsupported, "combat guard does not own this state-window kind", "kind");
    const ActiveKey key{context.executionId, event.itemId.format()};
    const auto found = active_.find(key);
    if (found == active_.end()) return Result<void>::success(Status::success(StatusCode::NoOp));
    if (found->second.binding.kind != binding.kind)
        return failure(DiagnosticCode::Conflict, "guard-window exit kind does not match active key", "kind");
    active_.erase(found);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<GuardEvaluation> GuardWindowState::evaluate(SubjectRef defender, SubjectRef attacker) const {
    if (!defender.isValid() || !attacker.isValid())
        return Result<GuardEvaluation>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "guard subjects must be valid", "subject"));
    GuardEvaluation evaluation;
    evaluation.defender = defender;
    evaluation.attacker = attacker;
    bool hasBlock = false;
    bool hasParry = false;
    for (const auto& [key, window] : active_) {
        (void)key;
        if (window.subject != defender) continue;
        if (window.mode == GuardMode::Parry) hasParry = true;
        if (window.mode == GuardMode::Block) hasBlock = true;
    }
    if (hasParry) {
        evaluation.result = GuardResult::Parried;
        evaluation.mode = GuardMode::Parry;
    } else if (hasBlock) {
        evaluation.result = GuardResult::Blocked;
        evaluation.mode = GuardMode::Block;
    }
    return Result<GuardEvaluation>::success(evaluation);
}

Result<GuardEvaluation> GuardWindowState::mitigate(SubjectRef defender, DamageRequest& request) const {
    auto evaluation = evaluate(defender, request.source);
    if (!evaluation) return evaluation;
    if (evaluation.value().result == GuardResult::None) return evaluation;
    request.healthDamage = 0.0;
    request.poiseDamage = 0.0;
    request.knockback = {};
    return evaluation;
}

std::vector<GuardMode> GuardWindowState::activeModes(SubjectRef subject) const {
    std::vector<GuardMode> modes;
    for (const auto& [key, window] : active_) {
        (void)key;
        if (window.subject == subject) modes.push_back(window.mode);
    }
    return modes;
}

}  // namespace eve::combat
