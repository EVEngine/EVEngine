#include <new>
#include "crowd/CrowdInternal.h"
namespace eve::crowd {
struct Crowd::AgentUpdatePlan::Data {
    explicit Data(std::uint64_t identity) : owner(identity), updates(0) {}
    std::uint64_t            owner;
    std::vector<AgentUpdate> updates;
};
Crowd::AgentUpdatePlan::AgentUpdatePlan()                                                    = default;
Crowd::AgentUpdatePlan::~AgentUpdatePlan()                                                   = default;
Crowd::AgentUpdatePlan::AgentUpdatePlan(AgentUpdatePlan&&) noexcept                          = default;
Crowd::AgentUpdatePlan&        Crowd::AgentUpdatePlan::operator=(AgentUpdatePlan&&) noexcept = default;
Result<Crowd::AgentUpdatePlan> Crowd::prepareAgentUpdates(const AgentUpdateBatch& batch) const try {
    if (impl_->budgetActive)
        return Result<AgentUpdatePlan>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Cannot prepare during advancement", "agentBatch"));
    std::vector<std::uint64_t> identities(0);
    identities.reserve(batch.agents.size());
    for (const auto& update : batch.agents) {
        if (!isAgentAlive(update.reference))
            return Result<AgentUpdatePlan>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "Stale update reference", "agentBatch"));
        if (!validAgentUpdate(update))
            return Result<AgentUpdatePlan>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid agent update", "agentBatch"));
        if (update.flow && update.flow->generation() != 0 && !impl_->isFlowLeaseCurrent(*update.flow))
            return Result<AgentUpdatePlan>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "Stale flow lease", "agentBatch"));
        identities.push_back(update.reference.handle.packed());
    }
    std::sort(identities.begin(), identities.end());
    if (std::adjacent_find(identities.begin(), identities.end()) != identities.end())
        return Result<AgentUpdatePlan>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Duplicate agent update", "agentBatch"));
    AgentUpdatePlan plan;
    plan.data_          = std::make_unique<AgentUpdatePlan::Data>(impl_->owner);
    plan.data_->updates = batch.agents;
    return Result<AgentUpdatePlan>::success(std::move(plan));
} catch (const std::bad_alloc&) {
    return Result<AgentUpdatePlan>::failure(
        Diagnostic::error(DiagnosticCode::Failed, "Cannot prepare agent updates", "agentBatch"));
}
Result<AgentBatchReceipt> Crowd::commitAgentUpdates(AgentUpdatePlan&& plan, std::span<const std::uint8_t> selection) {
    const auto fail = [](DiagnosticCode code, const char* message) {
        return Result<AgentBatchReceipt>::failure(Diagnostic::error(code, message, "agentBatch"));
    };
    if (impl_->budgetActive || !plan.data_ || plan.data_->owner != impl_->owner)
        return fail(DiagnosticCode::Conflict, "Consumed, foreign or active update plan");
    const auto& updates = plan.data_->updates;
    if (!selection.empty() && selection.size() != updates.size())
        return fail(DiagnosticCode::InvalidArgument, "Invalid update selection length");
    for (std::size_t i = 0; i < updates.size(); ++i) {
        if (!selection.empty() && selection[i] > 1)
            return fail(DiagnosticCode::InvalidArgument, "Update selection must contain zero or one");
        if (impl_->resolveAgent(updates[i].reference) < 0)
            return fail(DiagnosticCode::NotFound, "Update plan reference became stale");
        if (updates[i].flow && updates[i].flow->generation() != 0 && !impl_->isFlowLeaseCurrent(*updates[i].flow))
            return fail(DiagnosticCode::Conflict, "Update plan flow lease became stale");
    }
    std::size_t count = 0;
    for (std::size_t i = 0; i < updates.size(); ++i) {
        if (!selection.empty() && selection[i] == 0) continue;
        impl_->applyAgentUpdate(updates[i], std::size_t(impl_->resolveAgent(updates[i].reference)));
        ++count;
    }
    plan.data_.reset();
    return Result<AgentBatchReceipt>::success({count});
}
}  // namespace eve::crowd
