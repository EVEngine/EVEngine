#include "combat/ActionWindowState.h"

#include "combat/MeleeHit.h"
#include "common/Capability.h"

#include <utility>

namespace eve::combat {
  // namespace

CombatActionWindowState::CombatActionWindowState(ActionWindowSubjectResolver resolver)
    : resolver_(std::move(resolver)) {}

CombatActionWindowState::~CombatActionWindowState() { cap::removeListener<action::IActionStateWindowSink>(this); }

bool CombatActionWindowState::enabled() const {
    for (std::size_t index = 0; index < cap::listenerCount<action::IActionStateWindowSink>(); ++index)
        if (cap::listenerAt<action::IActionStateWindowSink>(index) == this) return true;
    return false;
}

void CombatActionWindowState::setEnabled(bool value) {
    const bool current = enabled();
    if (value && !current)
        cap::addListener<action::IActionStateWindowSink>(this);
    else if (!value && current)
        cap::removeListener<action::IActionStateWindowSink>(this);
}

void CombatActionWindowState::setMeleeHitRuntime(MeleeHitRuntime& melee) noexcept { melee_ = &melee; }

void CombatActionWindowState::clearMeleeHitRuntime() noexcept { melee_ = nullptr; }

bool CombatActionWindowState::supports(action::ActionStateWindowKind kind) const noexcept {
    return kind == action::ActionStateWindowKind::Hitbox || kind == action::ActionStateWindowKind::Invulnerability;
}

Result<void> CombatActionWindowState::enter(const action::ActionStateWindowBinding& binding,
                                            const action::ActionTimelineEvent& event,
                                            const action::ActionNotifyContext& context) {
    if (!supports(binding.kind))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("combat does not own this state-window kind"), std::move("kind")));
    if (!resolver_) return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, std::move("window subject resolver is unavailable"), std::move("resolver")));
    std::optional<ecs::EntityHandle> handle;
    if (binding.targetIndex) {
        if (*binding.targetIndex >= context.targets.size())
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, std::move("state-window target index is unavailable"), std::move("targetIndex")));
        handle = context.targets[*binding.targetIndex];
    } else {
        handle = context.source;
    }
    if (!handle) return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, std::move("state window has no source or target"), std::move("subject")));
    auto subject = resolver_(*handle);
    if (!subject) return Result<void>::failure(subject.status());
    if (!subject.value().isValid())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move("state-window subject is nil"), std::move("subject")));

    ActiveKey key{context.executionId, event.itemId.format()};
    const auto found = active_.find(key);
    if (found != active_.end()) {
        const bool same = found->second.subject == subject.value() &&
                          found->second.binding.kind == binding.kind &&
                          found->second.binding.resource == binding.resource;
        if (same) return Result<void>::success(Status::success(StatusCode::NoOp));
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict, std::move("state-window key is already active with different data"), std::move("itemId")));
    }
    if (binding.kind == action::ActionStateWindowKind::Hitbox && melee_) {
        auto armed = melee_->armHitbox(subject.value(), binding.resource, context.executionId);
        if (!armed) return armed;
    }
    active_.emplace(std::move(key), ActiveWindow{binding, subject.value()});
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> CombatActionWindowState::exit(const action::ActionStateWindowBinding& binding,
                                           const action::ActionTimelineEvent& event,
                                           const action::ActionNotifyContext& context) {
    if (!supports(binding.kind))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("combat does not own this state-window kind"), std::move("kind")));
    const ActiveKey key{context.executionId, event.itemId.format()};
    const auto found = active_.find(key);
    if (found == active_.end()) return Result<void>::success(Status::success(StatusCode::NoOp));
    if (found->second.binding.kind != binding.kind)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict, std::move("state-window exit kind does not match active key"), std::move("kind")));
    const auto subject  = found->second.subject;
    const auto resource = found->second.binding.resource;
    const auto kind     = found->second.binding.kind;
    active_.erase(found);
    if (kind == action::ActionStateWindowKind::Hitbox && melee_) {
        auto disarmed = melee_->disarmHitbox(subject, resource, context.executionId);
        if (!disarmed) return disarmed;
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

bool CombatActionWindowState::isInvulnerable(SubjectRef subject) const noexcept {
    for (const auto& [key, window] : active_) {
        (void)key;
        if (window.subject == subject && window.binding.kind == action::ActionStateWindowKind::Invulnerability)
            return true;
    }
    return false;
}

std::vector<std::string> CombatActionWindowState::activeHitboxes(SubjectRef subject) const {
    std::vector<std::string> result;
    for (const auto& [key, window] : active_) {
        (void)key;
        if (window.subject == subject && window.binding.kind == action::ActionStateWindowKind::Hitbox)
            result.push_back(window.binding.resource);
    }
    return result;
}

}  // namespace eve::combat
