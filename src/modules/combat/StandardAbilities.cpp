#include "combat/StandardAbilities.h"

#include "common/Diagnostic.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

namespace eve::combat {
namespace {

using action::AbilityActivationGroup;
using action::AbilityInstancingPolicy;

struct AbilitySpec {
    std::string_view        name;
    std::string_view        adapter;
    std::int64_t            windupMs;
    std::int64_t            activeMs;
    std::int64_t            recoverMs;
    std::int64_t            cooldownMs;
    AbilityInstancingPolicy instancing;
    AbilityActivationGroup  group;
};

constexpr std::array<AbilitySpec, 20> kSpecs{{
    {"light-attack", "melee", 120, 100, 180, 250, AbilityInstancingPolicy::PerExecution,
     AbilityActivationGroup::ExclusiveReplaceable},
    {"heavy-attack", "melee", 350, 160, 420, 650, AbilityInstancingPolicy::PerExecution,
     AbilityActivationGroup::ExclusiveReplaceable},
    {"charge-attack", "charge", 700, 180, 450, 900, AbilityInstancingPolicy::PerExecution,
     AbilityActivationGroup::ExclusiveReplaceable},
    {"aerial-attack", "melee-air", 140, 180, 260, 400, AbilityInstancingPolicy::PerExecution,
     AbilityActivationGroup::ExclusiveReplaceable},
    {"hold-attack", "hold", 200, 600, 250, 500, AbilityInstancingPolicy::PerExecution,
     AbilityActivationGroup::ExclusiveReplaceable},
    {"block", "guard", 80, 1000, 100, 0, AbilityInstancingPolicy::PerOwner,
     AbilityActivationGroup::ExclusiveBlocking},
    {"parry", "guard", 70, 180, 420, 600, AbilityInstancingPolicy::PerExecution,
     AbilityActivationGroup::ExclusiveBlocking},
    {"dodge", "movement", 80, 280, 220, 500, AbilityInstancingPolicy::PerExecution,
     AbilityActivationGroup::ExclusiveReplaceable},
    {"dash", "movement", 40, 240, 100, 350, AbilityInstancingPolicy::PerExecution,
     AbilityActivationGroup::ExclusiveReplaceable},
    {"jump", "movement-air", 40, 120, 80, 0, AbilityInstancingPolicy::PerExecution,
     AbilityActivationGroup::Independent},
    {"double-jump", "movement-air", 20, 120, 80, 0, AbilityInstancingPolicy::PerExecution,
     AbilityActivationGroup::Independent},
    {"teleport", "movement-warp", 180, 20, 220, 1200, AbilityInstancingPolicy::PerExecution,
     AbilityActivationGroup::ExclusiveReplaceable},
    {"throw", "projectile", 200, 100, 260, 500, AbilityInstancingPolicy::PerExecution,
     AbilityActivationGroup::ExclusiveReplaceable},
    {"ranged-attack", "projectile", 160, 80, 220, 350, AbilityInstancingPolicy::PerExecution,
     AbilityActivationGroup::ExclusiveReplaceable},
    {"equip", "equipment", 300, 80, 180, 0, AbilityInstancingPolicy::PerExecution,
     AbilityActivationGroup::ExclusiveBlocking},
    {"unequip", "equipment", 260, 60, 140, 0, AbilityInstancingPolicy::PerExecution,
     AbilityActivationGroup::ExclusiveBlocking},
    {"heal", "effect", 500, 80, 300, 1500, AbilityInstancingPolicy::PerExecution,
     AbilityActivationGroup::ExclusiveReplaceable},
    {"buff", "effect", 240, 80, 220, 800, AbilityInstancingPolicy::PerExecution,
     AbilityActivationGroup::Independent},
    {"hit-reaction", "reaction", 0, 240, 160, 0, AbilityInstancingPolicy::PerExecution,
     AbilityActivationGroup::ExclusiveReplaceable},
    {"death", "reaction", 0, 1200, 0, 0, AbilityInstancingPolicy::PerOwner,
     AbilityActivationGroup::ExclusiveBlocking},
}};

Result<LogicalId> id(std::string_view domain, std::string_view name) {
    auto parsed = LogicalId::fromParts(domain, name);
    if (!parsed)
        return Result<LogicalId>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Standard ability logical id is invalid",
                              std::string(domain) + ":" + std::string(name)));
    return Result<LogicalId>::success(std::move(*parsed));
}

Duration milliseconds(std::int64_t value) { return Duration::fromNanoseconds(value * 1000000); }

}  // namespace

Result<std::vector<action::AbilityDefinition>> standardCombatAbilities() {
    std::vector<action::AbilityDefinition> result;
    result.reserve(kSpecs.size());
    for (const AbilitySpec& spec : kSpecs) {
        auto abilityId = id("combat-ability", spec.name);
        if (!abilityId) return Result<std::vector<action::AbilityDefinition>>::failure(abilityId.status());
        auto actionId = id("combat-action", spec.name);
        if (!actionId) return Result<std::vector<action::AbilityDefinition>>::failure(actionId.status());

        action::AbilityDefinition definition;
        definition.id                     = std::move(abilityId).takeValue();
        definition.action.id              = std::move(actionId).takeValue();
        definition.action.timing.windup   = milliseconds(spec.windupMs);
        definition.action.timing.active   = milliseconds(spec.activeMs);
        definition.action.timing.recover  = milliseconds(spec.recoverMs);
        definition.action.metadata["combat.adapter"] = std::string(spec.adapter);
        definition.cooldown               = milliseconds(spec.cooldownMs);
        definition.instancing             = spec.instancing;
        definition.activationGroup        = spec.group;
        auto valid = definition.validate();
        if (!valid) return Result<std::vector<action::AbilityDefinition>>::failure(valid.status());
        result.push_back(std::move(definition));
    }
    return Result<std::vector<action::AbilityDefinition>>::success(std::move(result));
}

}  // namespace eve::combat
