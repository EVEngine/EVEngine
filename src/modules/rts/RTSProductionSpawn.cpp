#include "crowd/Crowd.h"
#include "rts/RTS.h"

namespace eve::rts {

Result<ProductionSpawnOutcome> RTS::placeProducedUnit(Unit& unit, WorldPosition requested) {
    auto provider = checkCrowdLifetime();
    if (!provider) return Result<ProductionSpawnOutcome>::failure(provider.status());
    if (crowd_ == nullptr || !unit.crowd()->link.isBound()) {
        unit.motion()->x = requested.x;
        unit.motion()->y = requested.y;
        return Result<ProductionSpawnOutcome>::success({ProductionSpawnState::Created, &unit});
    }
    const auto& key = unit.crowd()->link.key();
    if (crowd_->getNamedAgentIndex(key) >= 0)
        return Result<ProductionSpawnOutcome>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "Produced unit already has a Crowd projection", "production.spawn"));
    std::string                                         bindingKey;
    std::vector<crowd::AgentRef>                        references;
    std::vector<std::pair<Unit::Motion*, Unit::Crowd*>> projections;
    crowd::AgentFrame                                   frame;
    try {
        bindingKey = key;
        auto view  = ecs::View<Unit, Unit::Motion, Unit::Crowd, Unit::Containment>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [motion, settings, containment] = *it;
            if (settings == &*unit.crowd() || !settings->link.isBound() || containment->container.isBound()) continue;
            const crowd::AgentRef ref{settings->runtimeAgent, settings->runtimeOwner};
            if (!crowd_->isAgentAlive(ref)) continue;
            references.push_back(ref);
            projections.push_back({motion, settings});
        }
        references.reserve(references.size() + 1);
        projections.reserve(projections.size() + 1);
        frame.agents.reserve(references.size() + 1);
    } catch (const std::bad_alloc&) {
        return Result<ProductionSpawnOutcome>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "Cannot prepare production projection storage", "production.spawn"));
    }
    crowd::SpawnBatch batch;
    batch.policy = crowd::SpawnPolicy::PushNeighbors;
    batch.agents.push_back({key, requested.x, requested.y, unit.crowd()->heading, unit.crowd()->radius, {}});
    auto placed = crowd_->createAgents(batch);
    if (!placed) {
        const auto* diagnostic = placed.status().primaryDiagnostic();
        if (diagnostic != nullptr && (diagnostic->code() == DiagnosticCode::Conflict ||
                                      diagnostic->code() == DiagnosticCode::PreconditionViolation))
            return Result<ProductionSpawnOutcome>::success({ProductionSpawnState::Blocked, nullptr},
                                                           Status::success(StatusCode::Pending));
        return Result<ProductionSpawnOutcome>::failure(placed.status());
    }
    auto       receipt              = std::move(placed).takeValue();
    const auto reference            = receipt.created.front().reference;
    unit.crowd()->runtimeAgent      = reference.handle;
    unit.crowd()->runtimeOwner      = reference.owner;
    unit.crowd()->runtimeBindingKey = std::move(bindingKey);
    // Publish displaced peers immediately, using numeric bindings rather than name lookups.
    references.push_back(reference);
    projections.push_back({&*unit.motion(), &*unit.crowd()});
    auto copied = crowd_->copyAgentStates(references, frame);
    if (!copied) return Result<ProductionSpawnOutcome>::failure(copied.status());
    for (std::size_t index = 0; index < projections.size(); ++index) {
        const auto state        = frame.agents[index].state;
        auto [motion, settings] = projections[index];
        motion->x         = state.x;
        motion->y         = state.y;
        settings->heading = state.heading;
    }
    return Result<ProductionSpawnOutcome>::success({ProductionSpawnState::Created, &unit});
}

}  // namespace eve::rts
