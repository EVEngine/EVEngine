#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelBinding.h"
#include "rts/RTS.h"
#include "rts/RTSScriptInternal.h"
#include "transaction/AtomicBatch.h"

namespace eve::rts::script_internal {
namespace {
Result<void> bind(RTS& rts, transaction::AtomicBatch& batch, Faction& faction) {
    auto account = rts.scriptAccount(faction);
    if (!account) return batch.reject(account.status());
    const auto handle   = ecs::handle_of(&faction);
    const auto tick     = rts.scriptTick();
    const auto expected = reinterpret_cast<std::uintptr_t>(&account.value().get());
    auto       guarded  = batch.guard([&rts, handle, tick, expected]() -> Result<void> {
        auto* faction = handle.table == ecs::current() ? dynamic_cast<Faction*>(ecs::try_get(handle)) : nullptr;
        if (!faction || rts.scriptTick() != tick)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::StaleHandle, "batch simulation context changed", "account"));
        auto current = rts.scriptAccount(*faction);
        if (!current) return Result<void>::failure(current.status());
        if (reinterpret_cast<std::uintptr_t>(&current.value().get()) != expected)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::StaleHandle, "batch account was replaced", "account"));
        return Result<void>::success();
    });
    if (!guarded) return guarded;
    return batch.bindAccount(account.value().get(), [&rts, handle](std::string_view name) -> std::optional<Value> {
        auto* faction = handle.table == ecs::current() ? dynamic_cast<Faction*>(ecs::try_get(handle)) : nullptr;
        if (!faction) return std::nullopt;
        auto balance = rts.scriptResource(*faction, name);
        if (!balance) {
            balance.ignore("condition resource is unavailable");
            return std::nullopt;
        }
        return Value(balance.value());
    });
}

}  // namespace
void exposeStageBindings(ssq::Class& cls) {
    const auto vm = cls.getHandle();
    cls.addFunc("stageAbility", [vm](RTS* self, transaction::AtomicBatch* batch, const std::string& casterText,
                                     const std::string& ability, const std::string& targetText, float x, float y) {
        auto result = [&]() -> Result<void> {
            auto retained = batch->retainScriptOwner(vm);
            if (!retained) return retained;
            auto id = parseScriptSubject(casterText, "caster");
            if (!id) return batch->reject(id.status());
            auto* caster = self->findUnit(id.value());
            if (!caster)
                return batch->reject(
                    Status::failure(Diagnostic::error(DiagnosticCode::NotFound, "caster is absent", "caster")));
            auto* faction = dynamic_cast<Faction*>(caster->faction()->link.resolve());
            if (!faction)
                return batch->reject(
                    Status::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "faction is stale", "caster")));
            auto payer = bind(*self, *batch, *faction);
            if (!payer) return payer;
            SubjectRef target;
            if (!targetText.empty()) {
                auto parsed = parseScriptSubject(targetText, "target");
                if (!parsed) return batch->reject(parsed.status());
                target = parsed.value();
            }
            auto staged = self->prepareScriptAbility(*caster, ability, target, {x, y});
            if (!staged) return batch->reject(staged.status());
            auto                                                               stage = std::move(staged).takeValue();
            std::vector<std::unique_ptr<transaction::ITransactionParticipant>> effects;
            effects.push_back(std::move(stage.effect));
            return batch->add(std::move(effects), std::move(stage.cost));
        }();
        return script::projectResult(vm, std::move(result));
    });
    cls.addFunc("stageProduction", [vm](RTS* self, transaction::AtomicBatch* batch, const std::string& producerText,
                                        const std::string& unitText, const std::string& definitionText) {
        auto result = [&]() -> Result<void> {
            auto retained = batch->retainScriptOwner(vm);
            if (!retained) return retained;
            auto producerId = parseScriptSubject(producerText, "producer"),
                 unitId     = parseScriptSubject(unitText, "unit");
            if (!producerId) return batch->reject(producerId.status());
            if (!unitId) return batch->reject(unitId.status());
            auto definition = LogicalId::parse(definitionText);
            if (!definition)
                return batch->reject(Status::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "definition is invalid", "definition")));
            auto* producer = self->findBuilding(producerId.value());
            if (!producer)
                return batch->reject(
                    Status::failure(Diagnostic::error(DiagnosticCode::NotFound, "producer is absent", "producer")));
            auto* faction = dynamic_cast<Faction*>(producer->faction()->link.resolve());
            if (!faction)
                return batch->reject(
                    Status::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "faction is stale", "producer")));
            auto payer = bind(*self, *batch, *faction);
            if (!payer) return payer;
            auto staged = self->prepareScriptUnit(*producer, unitId.value(), *definition);
            if (!staged) return batch->reject(staged.status());
            auto stage = std::move(staged).takeValue();
            for (const auto& [name, policy] : faction->productionPolicy()->resourceReserves) {
                if (policy.minimumPriority <= 0) continue;
                auto floor = resource::ResourceCost::create(name, policy.amount);
                if (!floor) return batch->reject(floor.status());
                auto protectedCost = batch->protect(std::move(floor).takeValue());
                if (!protectedCost) return protectedCost;
            }
            return batch->add(std::move(stage.stage.effects), std::move(stage.cost));
        }();
        return script::projectResult(vm, std::move(result));
    });
    cls.addFunc("stagePayment", [vm](RTS* self, transaction::AtomicBatch* batch, const std::string& factionText,
                                     const std::string& json) {
        auto result = [&]() -> Result<void> {
            auto retained = batch->retainScriptOwner(vm);
            if (!retained) return retained;
            auto id = parseScriptSubject(factionText, "faction");
            if (!id) return batch->reject(id.status());
            auto* faction = self->findFaction(id.value());
            if (!faction)
                return batch->reject(
                    Status::failure(Diagnostic::error(DiagnosticCode::NotFound, "faction is absent", "faction")));
            auto payer = bind(*self, *batch, *faction);
            if (!payer) return payer;
            auto cost = transaction::AtomicBatch::parseCost(json);
            if (!cost) return batch->reject(cost.status());
            return batch->add({}, std::move(cost).takeValue());
        }();
        return script::projectResult(vm, std::move(result));
    });
}
}  // namespace eve::rts::script_internal
