#include "rts/RTSSystems.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace eve::rts {

Result<std::size_t> BuildingProductionSystem::step(const SimulationStep& step, const ProductionSpawn& spawn,
                                                   const ProductionSpawnPosition& position,
                                                   const LifecycleEventSink&      events) {
    if (step.delta.nanoseconds() < 0)
        return Result<std::size_t>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "RTS production step delta must be non-negative", "step.delta"));
    std::size_t processed = 0;
    struct Settlement {
        ecs::EntityHandle          building;
        production::ProductionTask task;
    };
    std::vector<Settlement>     settlements;
    std::vector<LifecycleEvent> clearedEvents;
    {
        auto view = ecs::View<Building, Building::Identity, Building::Production>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [identity, production] = *it;
            Building* building = identity == nullptr ? nullptr : dynamic_cast<Building*>(ecs::try_get(identity->self));
            if (building == nullptr || &*building->identity() != identity) continue;
            auto advanced = production->values.advance(step);
            if (!advanced) return Result<std::size_t>::failure(advanced.status());
            advanced.value();
            ++processed;
            if (!spawn) continue;
            std::vector<production::ProductionTask> completed = production->values.readyToSettle("unit");
            for (int index = 0; index < production->values.taskCount(); ++index) {
                auto task = production->values.taskAt(index);
                if (task && task->get().kind == "unit" && task->get().state == production::TaskState::Completed &&
                    !task->get().settlementRequired)
                    completed.push_back(task->get());
            }
            if (building->rally()->productionSpawnBlocked &&
                std::none_of(completed.begin(), completed.end(),
                             [&](const auto& task) { return task.id == building->rally()->blockedProductionTask; })) {
                building->rally()->productionSpawnBlocked = false;
                building->rally()->blockedProductionTask.clear();
                clearedEvents.push_back({LifecycleEventKind::ProductionSpawnCleared, identity->subject, {}, {}, 0.0});
            }
            for (const auto& task : completed) {
                settlements.push_back({identity->self, task});
            }
        }
    }
    // End the ECS deferred-mutation scope before factories publish stable unit pointers.
    if (events)
        for (const auto& event : clearedEvents) events(event, step.tick);
    for (const auto& settlement : settlements) {
        auto* building = dynamic_cast<Building*>(ecs::try_get(settlement.building));
        if (building == nullptr)
            return Result<std::size_t>::failure(Diagnostic::error(
                DiagnosticCode::StaleHandle, "RTS producer disappeared during production settlement", "production"));
        auto& settled = building->rally()->settledProductionTasks;
        if (std::find(settled.begin(), settled.end(), settlement.task.id) != settled.end()) {
            if (settlement.task.settlementRequired) {
                production::ProductionSettlementReceipt receipt;
                receipt.settlementId = "rts.unit:" + settlement.task.id;
                auto committed       = building->production()->values.settle(settlement.task.id, std::move(receipt));
                if (!committed) return Result<std::size_t>::failure(committed.status());
            }
            continue;
        }
        std::optional<WorldPosition> spawnPosition;
        if (position) {
            auto available = position(*building, settlement.task);
            if (!available) return Result<std::size_t>::failure(available.status());
            spawnPosition = std::move(available).takeValue();
            if (!spawnPosition) {
                const bool newlyBlocked                   = !building->rally()->productionSpawnBlocked;
                building->rally()->productionSpawnBlocked = true;
                building->rally()->blockedProductionTask  = settlement.task.id;
                if (newlyBlocked && events)
                    events({LifecycleEventKind::ProductionSpawnBlocked,
                            building->identity()->subject,
                            {},
                            settlement.task.product,
                            0.0},
                           step.tick);
                continue;
            }
        }
        const WorldPosition requested =
            spawnPosition.value_or(WorldPosition{building->placement()->worldX, building->placement()->worldY});
        auto created = spawn(*building, settlement.task, requested);
        if (!created) return Result<std::size_t>::failure(created.status());
        const auto outcome = std::move(created).takeValue();
        if (outcome.state == ProductionSpawnState::Blocked) {
            if (outcome.unit != nullptr)
                return Result<std::size_t>::failure(Diagnostic::error(
                    DiagnosticCode::InvariantViolation, "Blocked production published a unit", "production.spawn"));
            const bool newlyBlocked                   = !building->rally()->productionSpawnBlocked;
            building->rally()->productionSpawnBlocked = true;
            building->rally()->blockedProductionTask  = settlement.task.id;
            if (newlyBlocked && events)
                events({LifecycleEventKind::ProductionSpawnBlocked,
                        building->identity()->subject,
                        {},
                        settlement.task.product,
                        0.0},
                       step.tick);
            continue;
        }
        Unit* unit = outcome.unit;
        if (outcome.state != ProductionSpawnState::Created || unit == nullptr)
            return Result<std::size_t>::failure(Diagnostic::error(
                DiagnosticCode::Failed, "RTS production factory returned a null unit", "production.spawn"));
        const bool wasBlocked                     = building->rally()->productionSpawnBlocked;
        building->rally()->productionSpawnBlocked = false;
        building->rally()->blockedProductionTask.clear();
        if (wasBlocked && events)
            events({LifecycleEventKind::ProductionSpawnCleared, building->identity()->subject,
                    unit->identity()->subject, settlement.task.product, 0.0},
                   step.tick);
        unit->tactics()->combatGroup = building->rally()->combatGroup;
        building->rally()->reinforcements.push_back(unit->identity()->self);

        bool  boarded   = false;
        auto* transport = dynamic_cast<Unit*>(ecs::try_get(building->rally()->transport));
        if (transport != nullptr && transport->durability()->alive && transport->containment()->capacity > 0 &&
            transport->containment()->occupants.size() < transport->containment()->capacity &&
            FactionRelationSystem::isAllied(transport->faction()->link, building->faction()->link)) {
            auto link = ContainerLink::bind(transport->identity()->self);
            if (!link) return Result<std::size_t>::failure(link.status());
            unit->containment()->container = std::move(link).takeValue();
            transport->containment()->occupants.push_back(unit->identity()->self);
            boarded = true;
        }
        if (!boarded && building->rally()->enabled) {
            auto queued = unit->orders()->values.replace(building->rally()->command);
            if (!queued) return Result<std::size_t>::failure(queued.status());
            std::move(queued).takeValue();
        }
        settled.push_back(settlement.task.id);
        if (settlement.task.settlementRequired) {
            production::ProductionSettlementReceipt receipt;
            receipt.settlementId = "rts.unit:" + settlement.task.id;
            auto committed       = building->production()->values.settle(settlement.task.id, std::move(receipt));
            if (!committed) return Result<std::size_t>::failure(committed.status());
        }
        if (events)
            events({LifecycleEventKind::UnitProduced, building->identity()->subject, unit->identity()->subject,
                    settlement.task.product, 1.0},
                   step.tick);
        ++processed;
    }
    return Result<std::size_t>::success(processed, Status::success(StatusCode::Applied));
}

}  // namespace eve::rts
