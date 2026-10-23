#include "crowd/CrowdInternal.h"

#include <new>

namespace eve::crowd {

Result<AgentBatchReceipt> Crowd::relocateAgents(const AgentRelocationBatch& batch) try {
    const auto failure = [](DiagnosticCode code, const char* message) {
        return Result<AgentBatchReceipt>::failure(Diagnostic::error(code, message, "relocationBatch"));
    };
    if (batch.agents.size() > 1024 || batch.maxChecks < 1 || batch.maxChecks > 10000000)
        return failure(DiagnosticCode::InvalidArgument, "Invalid relocation count or check budget");
    auto& d = *impl_;
    for (std::size_t index = 0; index < d.xs.size(); ++index)
        if (!std::isfinite(d.xs[index]) || !std::isfinite(d.ys[index]) || !std::isfinite(d.radii[index]) ||
            d.radii[index] < 0.f)
            return failure(DiagnosticCode::PreconditionViolation, "Existing crowd geometry is invalid");
    std::vector<int> slots;
    slots.reserve(batch.agents.size());
    for (const auto& request : batch.agents) {
        const int slot = d.resolveAgent(request.reference);
        if (slot < 0) return failure(DiagnosticCode::NotFound, "Stale or wrong-owner relocation");
        if (!std::isfinite(request.x) || !std::isfinite(request.y))
            return failure(DiagnosticCode::InvalidArgument, "Relocation position must be finite");
        if (std::find(slots.begin(), slots.end(), slot) != slots.end())
            return failure(DiagnosticCode::Conflict, "Duplicate agent relocation");
        slots.push_back(slot);
    }
    if (slots.empty()) return Result<AgentBatchReceipt>::success({}, Status::success(StatusCode::NoOp));
    auto xs = d.xs, ys = d.ys;
    for (std::size_t index = 0; index < slots.size(); ++index) {
        xs[std::size_t(slots[index])] = batch.agents[index].x;
        ys[std::size_t(slots[index])] = batch.agents[index].y;
    }
    int checks = 0;
    for (const int value : slots) {
        const auto slot = std::size_t(value);
        if (++checks > batch.maxChecks)
            return failure(DiagnosticCode::PreconditionViolation, "Relocation check budget exhausted");
        if (!isTerrainClear(d.field, d.clampToField, xs[slot], ys[slot], d.radii[slot]))
            return failure(DiagnosticCode::PreconditionViolation, "Relocation intersects terrain or field bounds");
        for (std::size_t other = 0; other < xs.size(); ++other) {
            if (++checks > batch.maxChecks)
                return failure(DiagnosticCode::PreconditionViolation, "Relocation check budget exhausted");
            if (slot == other || !d.canContact(slot, other)) continue;
            if (std::hypot(double(xs[slot]) - xs[other], double(ys[slot]) - ys[other]) <
                double(d.radii[slot]) + d.radii[other] - 0.001)
                return failure(DiagnosticCode::PreconditionViolation, "Relocation intersects another agent");
        }
    }
    for (const int value : slots) {
        const auto slot = std::size_t(value);
        d.xs[slot]      = xs[slot];
        d.ys[slot]      = ys[slot];
        d.vxs[slot] = d.vys[slot] = d.speeds[slot] = 0.f;
    }
    d.spatialIndex.clear();
    return Result<AgentBatchReceipt>::success({slots.size()}, Status::success(StatusCode::Applied));
} catch (const std::bad_alloc&) {
    return Result<AgentBatchReceipt>::failure(
        Diagnostic::error(DiagnosticCode::Failed, "Cannot allocate relocation workspace", "relocationBatch"));
}

}  // namespace eve::crowd
