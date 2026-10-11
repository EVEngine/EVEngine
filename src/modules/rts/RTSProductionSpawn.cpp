#include "crowd/Crowd.h"
#include "rts/RTS.h"

namespace eve::rts {

Result<ProductionSpawnOutcome> RTS::placeProducedUnit(Unit& unit, WorldPosition requested) {
    if (crowd_ == nullptr || !unit.crowd()->link.isBound()) {
        unit.motion()->x = requested.x;
        unit.motion()->y = requested.y;
        return Result<ProductionSpawnOutcome>::success({ProductionSpawnState::Created, &unit});
    }
    const auto& key = unit.crowd()->link.key();
    if (crowd_->getNamedAgentIndex(key) >= 0)
        return Result<ProductionSpawnOutcome>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "Produced unit already has a Crowd projection", "production.spawn"));
    crowd::SpawnBatch batch;
    batch.policy = crowd::SpawnPolicy::PushNeighbors;
    batch.agents.push_back({key, requested.x, requested.y, unit.crowd()->heading, unit.crowd()->radius, {}});
    auto placed = crowd_->applySpawnBatch(batch);
    if (!placed) {
        const auto* diagnostic = placed.status().primaryDiagnostic();
        if (diagnostic != nullptr && (diagnostic->code() == DiagnosticCode::Conflict ||
                                      diagnostic->code() == DiagnosticCode::PreconditionViolation))
            return Result<ProductionSpawnOutcome>::success({ProductionSpawnState::Blocked, nullptr},
                                                           Status::success(StatusCode::Pending));
        return Result<ProductionSpawnOutcome>::failure(placed.status());
    }
    std::move(placed).takeValue();
    // Publish displaced peers immediately: the next movement phase imports ECS
    // motion and must not overwrite the just-committed Crowd displacement.
    auto view = ecs::View<Unit, Unit::Motion, Unit::Crowd, Unit::Containment>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [motion, settings, containment] = *it;
        if (!settings->link.isBound() || containment->container.isBound()) continue;
        const int agent = crowd_->getNamedAgentIndex(settings->link.key());
        if (agent < 0) continue;
        const auto state  = crowd_->getAgentState(agent);
        motion->x         = state.x;
        motion->y         = state.y;
        settings->heading = state.heading;
    }
    return Result<ProductionSpawnOutcome>::success({ProductionSpawnState::Created, &unit});
}

}  // namespace eve::rts
