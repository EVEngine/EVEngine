#include "tactics/TacticsSettlement.h"

#include <cmath>
#include <utility>

namespace eve::tactics {

Result<settlement::SettlementRequest> makeSettlementRequest(const AbilitySettlementRequest& request) {
    if (!request.ability.actor.isValid())
        return Result<settlement::SettlementRequest>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "tactical ability actor is invalid", "ability.actor"));
    const SubjectRef target = request.target.isValid() ? request.target : request.ability.targetUnit;
    if (!target.isValid())
        return Result<settlement::SettlementRequest>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "tactical settlement requires a stable unit or cell target", "target"));
    if (request.kind.empty() || !std::isfinite(request.magnitude) || request.magnitude < 0.0)
        return Result<settlement::SettlementRequest>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "tactical settlement kind or magnitude is invalid", "settlement"));

    settlement::SettlementRequest common;
    common.source    = request.ability.actor;
    common.target    = target;
    common.kind      = request.kind;
    common.magnitude = request.magnitude;
    common.tags      = request.tags;
    common.tags.push_back("tactics:ability");
    common.tick    = request.tick;
    common.context = request.context;
    return Result<settlement::SettlementRequest>::success(std::move(common));
}

Result<void> TacticsSettlementRuntime::configureSettlementRules(const settlement::SettlementRuleSet& rules) {
    settlement::SettlementPipeline candidate;
    auto installed = rules.install(candidate);
    if (!installed) return installed;
    settlement_ = std::move(candidate);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<settlement::SettlementResult> TacticsSettlementRuntime::settle(
    const AbilitySettlementRequest& request, settlement::ISettlementPolicy& policy,
    game_event::GameEventLog* eventLog) const {
    auto common = makeSettlementRequest(request);
    if (!common) return Result<settlement::SettlementResult>::failure(common.status());
    return settlement_.settle(common.value(), policy, eventLog);
}

Result<std::vector<settlement::SettlementResult>> TacticsSettlementRuntime::settleAtomic(
    std::span<const AbilitySettlementRequest> requests, std::span<settlement::ISettlementPolicy*> policies,
    game_event::GameEventLog* eventLog) const {
    std::vector<settlement::SettlementRequest> common;
    common.reserve(requests.size());
    for (const auto& request : requests) {
        auto projected = makeSettlementRequest(request);
        if (!projected) return Result<std::vector<settlement::SettlementResult>>::failure(projected.status());
        common.push_back(std::move(projected).takeValue());
    }
    return settlement_.settleAtomic(common, policies, eventLog);
}

}  // namespace eve::tactics
