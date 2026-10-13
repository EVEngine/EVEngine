#include <algorithm>
#include <cmath>
#include "rts/RTSScriptInternal.h"
#include "rts/RTSSystemMovementInternal.h"
#include "rts/RTSSystems.h"
namespace eve::rts {
using systems_internal::distanceSquared;
using systems_internal::entityPosition;
using systems_internal::isFinitePosition;
Result<void> AbilitySystem::validateCast(Unit& caster, const AbilitySpec& spec, ecs::EntityHandle target,
                                         WorldPosition point) {
    if (spec.id.empty() || !std::isfinite(spec.range) || spec.range < 0.0f || !std::isfinite(spec.radius) ||
        spec.radius < 0.0f || !std::isfinite(spec.cooldown) || spec.cooldown < 0.0f || !std::isfinite(spec.damage) ||
        spec.damage < 0.0f || !std::isfinite(spec.healing) || spec.healing < 0.0f || !std::isfinite(spec.castTime) ||
        spec.castTime < 0.0f || !std::isfinite(spec.channelTickInterval) || spec.channelTickInterval < 0.0f ||
        spec.resourceCost < 0 || (spec.resourceCost > 0 && spec.resourceType.empty()) ||
        (spec.channelTickInterval > 0.0f && spec.castTime <= 0.0f) || !isFinitePosition(point))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "RTS ability definition or target point is invalid", "ability"));
    if (spec.casterDefinition.isValid() && caster.definition()->id != spec.casterDefinition)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "unit definition cannot cast this ability", "caster"));
    if (!caster.durability()->alive)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict, "ability caster is dead", "caster"));
    if (caster.abilities()->channel)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "unit is already casting", "ability.channel"));
    for (const auto& cooldown : caster.abilities()->cooldowns)
        if (cooldown.id == spec.id && cooldown.remaining > 0)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "ability is on cooldown", "ability.cooldown"));
    WorldPosition destination = point;
    if (spec.target == AbilityTarget::Self)
        destination = {caster.motion()->x, caster.motion()->y};
    else if (spec.target != AbilityTarget::Point) {
        auto position = entityPosition(target);
        if (!position)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::StaleHandle, "ability target is stale", "target"));
        destination          = *position;
        auto*        entity  = ecs::try_get(target);
        FactionLink* faction = nullptr;
        SubjectRef   subject;
        bool         alive = false;
        if (auto* unit = dynamic_cast<Unit*>(entity)) {
            faction = &unit->faction()->link;
            subject = unit->identity()->subject;
            alive   = unit->durability()->alive;
        } else if (auto* building = dynamic_cast<Building*>(entity)) {
            faction = &building->faction()->link;
            subject = building->identity()->subject;
            alive   = building->integrity()->alive;
        }
        if (!alive || !faction)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::StaleHandle, "ability target is not live", "target"));
        const bool allied = FactionRelationSystem::isAllied(*faction, caster.faction()->link);
        if ((spec.target == AbilityTarget::Ally && !allied) || (spec.target == AbilityTarget::Enemy && allied))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "ability target relationship is invalid", "target"));
        if (spec.target == AbilityTarget::Enemy &&
            !FactionIntelSystem::isTargetable(dynamic_cast<Faction*>(caster.faction()->link.resolve()), subject))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "enemy ability target is not currently visible and detected", "target"));
    }
    if (distanceSquared(caster.motion()->x, caster.motion()->y, destination.x, destination.y) > spec.range * spec.range)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "ability target is outside cast range", "target"));
    return Result<void>::success();
}

namespace script_internal {
namespace {
class AbilityAdmission final : public transaction::ITransactionParticipant {
public:
    AbilityAdmission(Unit& caster, AbilitySpec spec, ecs::EntityHandle target, WorldPosition point)
        : caster_(ecs::handle_of(&caster)), spec_(std::move(spec)), target_(target), point_(point) {}
    std::string_view name() const noexcept override { return "rts-ability-admission"; }
    Result<void>     prepare(const transaction::TransactionContext&) override {
        if (phase_ != Phase::Idle) return conflict("ability stage is already in flight");
        auto* caster = resolve();
        if (!caster) return conflict("ability caster is stale");
        auto valid = AbilitySystem::validateCast(*caster, spec_, target_, point_);
        if (!valid) return valid;
        before_    = *caster->abilities();
        after_     = before_;
        auto found = std::find_if(after_.cooldowns.begin(), after_.cooldowns.end(),
                                      [&](const auto& c) { return c.id == spec_.id; });
        if (found == after_.cooldowns.end())
            after_.cooldowns.push_back({spec_.id, spec_.cooldown});
        else
            found->remaining = spec_.cooldown;
        after_.channel = Unit::Abilities::Channel{
            spec_,          target_,
            point_,         caster->durability()->state.health,
            spec_.castTime, spec_.channelTickInterval > 0 ? spec_.channelTickInterval : spec_.castTime};
        phase_ = Phase::Prepared;
        return Result<void>::success();
    }
    Result<void> commit(const transaction::TransactionContext&) override {
        if (phase_ != Phase::Prepared) return conflict("ability stage is not prepared");
        auto* caster = resolve();
        if (!caster) return conflict("ability caster is stale");
        auto valid = AbilitySystem::validateCast(*caster, spec_, target_, point_);
        if (!valid) return valid;
        if (!sameCooldowns(caster->abilities()->cooldowns, before_.cooldowns))
            return conflict("ability state changed during admission");
        std::swap(*caster->abilities(), after_);
        phase_ = Phase::Committed;
        return Result<void>::success(Status::success(StatusCode::Applied));
    }
    Result<void> rollback(const transaction::TransactionContext&) override {
        if (phase_ != Phase::Prepared) return conflict("ability stage is not prepared");
        phase_ = Phase::Terminal;
        return Result<void>::success();
    }
    Result<void> compensate(const transaction::TransactionContext&) override {
        if (phase_ != Phase::Committed) return conflict("ability stage is not committed");
        auto* caster = resolve();
        if (!caster) return conflict("ability caster is stale during compensation");
        const auto& channel = caster->abilities()->channel;
        if (!channel || channel->spec.id != spec_.id || channel->remaining != spec_.castTime)
            return conflict("ability was advanced before compensation");
        std::swap(*caster->abilities(), after_);
        phase_ = Phase::Terminal;
        return Result<void>::success();
    }

private:
    enum class Phase { Idle, Prepared, Committed, Terminal };
    static Result<void> conflict(std::string message) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, std::move(message), "ability.admission"));
    }
    static bool sameCooldowns(const std::vector<Unit::Abilities::Cooldown>& a,
                              const std::vector<Unit::Abilities::Cooldown>& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a[i].id != b[i].id || a[i].remaining != b[i].remaining) return false;
        return true;
    }
    Unit* resolve() const {
        return caster_.table == ecs::current() ? dynamic_cast<Unit*>(ecs::try_get(caster_)) : nullptr;
    }
    ecs::EntityHandle caster_, target_;
    AbilitySpec       spec_;
    WorldPosition     point_;
    Unit::Abilities   before_, after_;
    Phase             phase_ = Phase::Idle;
};
}  // namespace
Result<std::unique_ptr<transaction::ITransactionParticipant>> stageAbility(Unit& caster, AbilitySpec spec,
                                                                           ecs::EntityHandle target,
                                                                           WorldPosition     point) {
    auto valid = AbilitySystem::validateCast(caster, spec, target, point);
    if (!valid) return Result<std::unique_ptr<transaction::ITransactionParticipant>>::failure(valid.status());
    return Result<std::unique_ptr<transaction::ITransactionParticipant>>::success(
        std::make_unique<AbilityAdmission>(caster, std::move(spec), target, point));
}
}  // namespace script_internal
}  // namespace eve::rts
