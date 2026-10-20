#include "crowd/Crowd.h"
#include "rts/RTS.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace eve::rts {

Result<std::size_t> RTS::relocateUnits(const UnitRelocationBatch& batch) try {
    auto provider = checkCrowdLifetime();
    if (!provider) return Result<std::size_t>::failure(provider.status());
    const auto failure = [](DiagnosticCode code, const char* message) {
        return Result<std::size_t>::failure(Diagnostic::error(code, message, "relocationBatch"));
    };
    if (batch.units.size() > 1024 || batch.maxChecks < 1 || batch.maxChecks > 10000000)
        return failure(DiagnosticCode::InvalidArgument, "Invalid relocation count or work budget");
    std::vector<Unit*>          units;
    crowd::AgentRelocationBatch placements;
    placements.maxChecks = batch.maxChecks;
    units.reserve(batch.units.size());
    placements.agents.reserve(batch.units.size());
    for (const auto& request : batch.units) {
        auto* unit = findUnit(request.subject);
        if (unit == nullptr) return failure(DiagnosticCode::NotFound, "Relocation unit not owned by this RTS");
        if (!std::isfinite(request.position.x) || !std::isfinite(request.position.y))
            return failure(DiagnosticCode::InvalidArgument, "Relocation coordinates must be finite");
        if (std::find(units.begin(), units.end(), unit) != units.end())
            return failure(DiagnosticCode::Conflict, "Duplicate unit relocation");
        if (unit->containment()->container.isBound())
            return failure(DiagnosticCode::PreconditionViolation, "Contained unit cannot be relocated independently");
        if (unit->crowd()->link.isBound()) {
            const crowd::AgentRef reference{unit->crowd()->runtimeAgent, unit->crowd()->runtimeOwner};
            if (crowd_ == nullptr || !crowd_->isAgentAlive(reference))
                return failure(DiagnosticCode::PreconditionViolation, "Relocation requires a current Crowd projection");
            placements.agents.push_back({reference, request.position.x, request.position.y});
        }
        units.push_back(unit);
    }
    if (!placements.agents.empty()) {
        auto placed = crowd_->relocateAgents(placements);
        if (!placed) return Result<std::size_t>::failure(placed.status());
    }
    for (std::size_t index = 0; index < units.size(); ++index) {
        auto* unit              = units[index];
        unit->motion()->x       = batch.units[index].position.x;
        unit->motion()->y       = batch.units[index].position.y;
        unit->motion()->arrived = false;
        unit->navigation()->waypoints.clear();
        unit->navigation()->plannedOrderId.clear();
        unit->navigation()->formationTarget.reset();
        unit->navigation()->combatSlotTarget.reset();
        unit->navigation()->combatSlotWaiting = false;
        unit->navigation()->trafficRecoveryTarget.reset();
    }
    return Result<std::size_t>::success(units.size(),
                                        Status::success(units.empty() ? StatusCode::NoOp : StatusCode::Applied));
} catch (const std::bad_alloc&) {
    return Result<std::size_t>::failure(
        Diagnostic::error(DiagnosticCode::Failed, "Cannot allocate relocation preparation", "relocationBatch"));
}

}  // namespace eve::rts
