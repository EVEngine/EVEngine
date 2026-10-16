#include <algorithm>
#include <cmath>
#include "common/Profile.h"
#include "rts/RTSSystemMovementInternal.h"
namespace eve::rts {
using systems_internal::isSameHandle;
using systems_internal::readCurrent;
Result<std::size_t> ConstructionSystem::step(const SimulationStep& step, const LifecycleEventSink& events) {
    EV_PROFILE_MODULE("rts", "ConstructionSystem::step");
    if (step.delta.nanoseconds() < 0)
        return Result<std::size_t>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "RTS construction step delta must be non-negative", "step.delta"));
    std::size_t processed = 0;
    auto        buildings = ecs::View<Building, Building::Identity, Building::Construction, Building::Faction>();
    for (auto it = buildings.begin(); it != buildings.end(); ++it) {
        auto [identity, construction, faction] = *it;
        auto* building = identity == nullptr ? nullptr : dynamic_cast<Building*>(ecs::try_get(identity->self));
        if (building == nullptr || &*building->identity() != identity) continue;
        if (!std::isfinite(construction->progress) || !std::isfinite(construction->buildTimeSeconds) ||
            construction->buildTimeSeconds < 0.0f)
            return Result<std::size_t>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "invalid RTS construction state", "building.construction"));
        if (construction->progress >= 1.0f || construction->paused) continue;

        construction->builders.clear();
        float totalRate = 0.0f;
        auto  units     = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Orders, Unit::Worker, Unit::Faction>();
        for (auto unitIt = units.begin(); unitIt != units.end(); ++unitIt) {
            auto [unitIdentity, motion, orders, worker, unitFaction] = *unitIt;
            auto* unit = unitIdentity == nullptr ? nullptr : dynamic_cast<Unit*>(ecs::try_get(unitIdentity->self));
            if (unit == nullptr || !motion->arrived || worker->buildRate <= 0.0f ||
                unitFaction->link.resolve() != faction->link.resolve())
                continue;
            auto current = readCurrent(orders->values);
            if (!current) return Result<std::size_t>::failure(current.status());
            auto record = std::move(current).takeValue();
            if (!record || record->kind != OrderKind::Build || !isSameHandle(record->targetEntity, identity->self))
                continue;
            construction->builders.push_back(unitIdentity->self);
            totalRate += worker->buildRate;
        }
        if (totalRate <= 0.0f) continue;
        if (construction->buildTimeSeconds == 0.0f)
            construction->progress = 1.0f;
        else
            construction->progress =
                std::min(1.0f, construction->progress + totalRate * static_cast<float>(step.delta.seconds()) /
                                                            construction->buildTimeSeconds);
        ++processed;
        if (construction->progress < 1.0f) continue;
        if (events)
            events({LifecycleEventKind::ConstructionCompleted,
                    identity->subject,
                    {},
                    building->definition()->id.format(),
                    1.0},
                   step.tick);
        for (const auto& builderHandle : construction->builders) {
            auto* unit = dynamic_cast<Unit*>(ecs::try_get(builderHandle));
            if (unit == nullptr) continue;
            auto current = readCurrent(unit->orders()->values);
            if (!current) return Result<std::size_t>::failure(current.status());
            auto record = std::move(current).takeValue();
            if (record && record->kind == OrderKind::Build && isSameHandle(record->targetEntity, identity->self)) {
                auto completed = unit->orders()->values.complete(record->id);
                if (!completed) return Result<std::size_t>::failure(completed.status());
            }
        }
        construction->builders.clear();
    }
    return Result<std::size_t>::success(processed,
                                        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

}  // namespace eve::rts
