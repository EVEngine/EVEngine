#include "combat/ActionDamageSink.h"

#include "common/Capability.h"

#include <utility>

namespace eve::combat {
namespace {

Result<void> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

}  // namespace

CombatActionDamageSink::CombatActionDamageSink(ActionCombatStateResolver resolver)
    : resolver_(std::move(resolver)) {}

CombatActionDamageSink::~CombatActionDamageSink() { cap::removeListener<action::IActionDamageSink>(this); }

bool CombatActionDamageSink::enabled() const {
    for (std::size_t index = 0; index < cap::listenerCount<action::IActionDamageSink>(); ++index)
        if (cap::listenerAt<action::IActionDamageSink>(index) == this) return true;
    return false;
}

void CombatActionDamageSink::setEnabled(bool value) {
    const bool current = enabled();
    if (value && !current)
        cap::addListener<action::IActionDamageSink>(this);
    else if (!value && current)
        cap::removeListener<action::IActionDamageSink>(this);
}

bool CombatActionDamageSink::supports(ecs::EntityHandle target) const {
    return resolver_ && resolver_(target).has_value();
}

Result<void> CombatActionDamageSink::apply(const action::ActionDamageBinding& binding,
                                           const action::ActionNotifyContext& context) {
    if (!resolver_) return failure(DiagnosticCode::NotFound, "combat-state resolver is unavailable", "resolver");
    if (binding.targetIndex >= context.targets.size())
        return failure(DiagnosticCode::NotFound, "damage target index is unavailable", "targetIndex");
    auto target = resolver_(context.targets[binding.targetIndex]);
    if (!target) return failure(DiagnosticCode::NotFound, "damage target has no combat state", "target");

    SubjectRef source = SubjectRef::nil();
    if (context.source) {
        auto resolvedSource = resolver_(*context.source);
        if (resolvedSource) source = resolvedSource->get().subject;
    }
    DamageRequest request;
    request.source = source;
    request.target = target->get().subject;
    request.actionExecution = context.executionId;
    request.damageType = binding.damageType;
    request.healthDamage = binding.amount;
    request.poiseDamage = binding.poiseAmount;
    request.knockback = {binding.knockback.x, binding.knockback.y, binding.knockback.z};
    auto outcome = runtime_.apply(target->get(), request);
    if (!outcome) return Result<void>::failure(outcome.status());
    lastOutcome_ = std::move(outcome).takeValue();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

std::optional<DamageOutcome> CombatActionDamageSink::lastOutcome() const { return lastOutcome_; }

}  // namespace eve::combat
