/** @file CrowdNavigationCapture.cpp
 * @brief crowd module-interface: exact native external navigation capture and candidate-owned restoration.
 * @remarks Uses the existing Crowd owner; no new capability provider, ECS entity/Link or event stream.
 * Script request bindings remain unchanged; adds no tool family; crowd:navigation v1 is owned by CrowdSnapshotPersistence.
 * CPU facts share immutable snapshot provenance; prepared leases never retain source cache pins.
 * Owner-thread checkpoint boundary only, with no callbacks, graph advancement or live backend publication.
 */
#include <unordered_map>
#include "crowd/CrowdInternal.h"
namespace eve::crowd {
Crowd::NavigationCapture::NavigationCapture(std::unique_ptr<NavigationData> data) : data_(std::move(data)) {}
Crowd::NavigationCapture::~NavigationCapture()                                              = default;
Crowd::NavigationCapture::NavigationCapture(NavigationCapture&&) noexcept                   = default;
Crowd::NavigationCapture& Crowd::NavigationCapture::operator=(NavigationCapture&&) noexcept = default;
std::size_t               Crowd::NavigationCapture::size() const noexcept { return data_ ? data_->work.size() : 0; }
Result<Crowd::NavigationCapture> Crowd::Snapshot::captureNavigation(std::span<const NavigationWorkView> work) const {
    if (!state_)
        return Result<NavigationCapture>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Empty navigation backend capture", "snapshot"));
    std::unordered_map<std::uint64_t, const Impl::FlowCacheEntry*> entries;
    entries.reserve(state_->state.flowCache.size());
    for (const auto& entry : state_->state.flowCache) entries.emplace(entry.generation, &entry);
    for (const auto& row : work) {
        const auto& lease = row.lease.get();
        if (!lease.field_ || lease.owner_ != state_->state.owner ||
            lease.input_ != state_->state.field.inputRevision() || lease.algorithm_ != flowBuildAlgorithmVersion)
            continue;  // A foreign/stale observation never grants a current candidate binding.
        const auto found = entries.find(lease.generation_);
        if (found == entries.end() || !Impl::matchesFlowLease(*found->second, lease))
            return Result<NavigationCapture>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "External lease is not closed over the captured backend cache", "snapshot"));
    }
    auto data    = std::make_unique<NavigationData>();
    data->source = state_;
    data->work.reserve(work.size());
    for (const auto& row : work) {
        NavigationWork copy;
        copy.plan  = cloneFlowBuildPlan(row.plan.get());
        copy.lease = row.lease.get();
        copy.lease.pin_.reset();
        data->work.push_back(std::move(copy));
    }
    return Result<NavigationCapture>::success(NavigationCapture(std::move(data)));
}
Result<std::vector<Crowd::NavigationWork>> Crowd::RestorePlan::prepareNavigation(const NavigationCapture& captured) {
    using R = Result<std::vector<NavigationWork>>;
    if (!state_ || !captured.data_ || state_->source != captured.data_->source)
        return R::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Empty or foreign navigation snapshot", "snapshot"));
    std::vector<NavigationWork> result;
    result.reserve(captured.data_->work.size());
    for (const auto& row : captured.data_->work) {
        NavigationWork copy;
        copy.plan         = cloneFlowBuildPlan(row.plan);
        copy.plan.owner_  = copy.plan.owner_ == state_->sourceOwner ? state_->state->owner : 0;
        copy.lease        = row.lease;
        copy.lease.owner_ = copy.lease.owner_ == state_->sourceOwner ? state_->state->owner : 0;
        result.push_back(std::move(copy));
    }
    std::vector<FlowLease*> leases;
    leases.reserve(result.size());
    for (auto& row : result) leases.push_back(&row.lease);
    state_->state->attachFlowPins(leases);
    return R::success(std::move(result));
}
}  // namespace eve::crowd
