#include <algorithm>
#include <cmath>
#include "sensing/EnvironmentQuery.h"
#include "sensing/Sensing.h"

namespace eve::sensing {

Result<EnvironmentRequest> SensingWorld::environmentInput(const QueryOrigin& origin, const QuerySpec& spec) const {
    auto collected = collectCandidates(origin, spec);
    if (!collected) return Result<EnvironmentRequest>::failure(collected.status());
    // The spatial index's traversal order is not an identity. Canonicalize once at admission.
    auto candidates = std::move(collected).takeValue().candidates;
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    EnvironmentRequest request;
    for (const auto& candidate : candidates) {
        auto location = WorldPoint::world2D(candidate.x, candidate.y);
        if (!location) return Result<EnvironmentRequest>::failure(location.status());
        request.input.push_back({request.input.size(), TargetLocation{location.value()}, candidate.id});
    }
    return Result<EnvironmentRequest>::success(std::move(request));
}

Result<EnvironmentQueryResult> SensingWorld::executeEnvironmentQuery(const EnvironmentQueryPlan& plan,
                                                                     EnvironmentRequest          request,
                                                                     const EnvironmentServices&  services) {
    auto prepared = plan.instantiate(std::move(request));
    if (!prepared) {
        EnvironmentTrace trace;
        trace.state       = "rejected";
        trace.diagnostics = prepared.diagnostics();
        auto encoded      = trace.toValue().toJson();
        if (!encoded) return Result<EnvironmentQueryResult>::failure(encoded.status());
        lastEnvironmentQuery_ = std::move(encoded).takeValue();
        return Result<EnvironmentQueryResult>::failure(prepared.status());
    }
    auto       instance = std::move(prepared).takeValue();
    auto       result   = instance.run(services);
    const bool ok       = result.ok();
    auto       document = instance.trace().toValue();
    if (ok) document.set("result", result.value().toValue());
    auto encoded = document.toJson();
    if (!encoded) return Result<EnvironmentQueryResult>::failure(encoded.status());
    lastEnvironmentQuery_ = std::move(encoded).takeValue();
    return result;
}

Result<Value> SensingWorld::queryPositions(float originX, float originY, float targetX, float targetY, float radius,
                                           int samples, int limit, bool requireLineOfSight) {
    if (samples <= 0 || limit <= 0)
        return Result<Value>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "samples and limit must be positive", "queryPositions", {}, "sensing"));
    auto origin = WorldPoint::world2D(originX, originY);
    if (!origin) return Result<Value>::failure(origin.status());
    auto target = WorldPoint::world2D(targetX, targetY);
    if (!target) return Result<Value>::failure(target.status());
    EnvironmentQueryDefinition definition;
    definition.id         = "sensing.positions.ring";
    definition.contexts   = {"self", "target"};
    definition.maxResults = static_cast<std::uint32_t>(limit);
    definition.mode       = limit == 1 ? EnvironmentRunMode::Best : EnvironmentRunMode::All;
    definition.trace      = EnvironmentTraceLevel::Detailed;
    EnvironmentOption option;
    option.id                = "ring";
    option.generator.kind    = EnvironmentGeneratorKind::Ring;
    option.generator.context = "target";
    option.generator.radius  = radius;
    option.generator.samples = static_cast<std::uint32_t>(samples);
    EnvironmentTest distance;
    distance.id      = "travelDistance";
    distance.context = "self";
    distance.purpose = EnvironmentPurpose::Score;
    // Fixed metric scale preserves ordering when the terminal LOS filter is deferred.
    distance.scoreMaximum = std::hypot(double(originX) - targetX, double(originY) - targetY) + double(radius) + 1;
    option.tests.push_back(distance);
    if (requireLineOfSight) {
        EnvironmentTest los;
        los.id      = "visibleFromTarget";
        los.context = "target";
        los.metric  = EnvironmentMetricKind::LineOfSight;
        los.purpose = EnvironmentPurpose::Filter;
        los.minimum = los.maximum = 1;
        option.tests.push_back(los);
    }
    definition.options.push_back(std::move(option));
    auto plan = EnvironmentQueryPlan::compile(definition);
    if (!plan) return Result<Value>::failure(plan.status());
    EnvironmentRequest request;
    request.contexts = {{"self", {TargetLocation{origin.value()}}}, {"target", {TargetLocation{target.value()}}}};
    EnvironmentServices services;
    if (requireLineOfSight) {
        // Resolve once at the compatibility/script composition boundary, never per item.
        if (const auto* los = cap::query<ILineOfSightQuery>()) services.lineOfSight = std::cref(*los);
    }
    auto result = executeEnvironmentQuery(plan.value(), std::move(request), services);
    if (!result) return Result<Value>::failure(result.status());
    return Result<Value>::success(result.value().toValue());
}

}  // namespace eve::sensing
