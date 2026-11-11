#include "crowd/CrowdInternal.h"

#include <new>
#include <unordered_map>

namespace eve::crowd {
Crowd::FlowBuildPlan Crowd::cloneFlowBuildPlan(const FlowBuildPlan& source) {
    FlowBuildPlan copy;
    copy.owner_    = source.owner_;
    copy.revision_ = source.revision_;
    copy.state_    = source.state_;
    if (source.field_) {
        copy.field_ = std::make_unique<CrowdField>(*source.field_);
        // Retained capacities, including an exhausted heap, determine later cache admission cost.
        const auto& original = *source.field_;
        auto&       field    = *copy.field_;
        field.cost_.reserve(original.cost_.capacity());
        field.navigationCost_.reserve(original.navigationCost_.capacity());
        field.integ_.reserve(original.integ_.capacity());
        field.flowX_.reserve(original.flowX_.capacity());
        field.flowY_.reserve(original.flowY_.capacity());
        field.goals_.reserve(original.goals_.capacity());
        field.buildHeap_.reserve(original.buildHeap_.capacity());
    }
    return copy;
}
std::unique_ptr<Crowd::NavigationRequests> Crowd::cloneNavigationRequests(const NavigationRequests* source) {
    if (!source) return {};
    auto result = std::make_unique<NavigationRequests>();
    result->entries.reserve(source->entries.size());
    for (const auto& entry : source->entries) {
        NavigationRequests::Entry copy;
        copy.plan       = cloneFlowBuildPlan(entry.plan);
        copy.lease      = entry.lease;
        copy.generation = entry.generation;
        copy.occupied   = entry.occupied;
        result->entries.push_back(std::move(copy));
    }
    return result;
}
bool Crowd::Impl::matchesFlowLease(const FlowCacheEntry& entry, const FlowLease& lease) noexcept {
    return lease.generation_ == entry.generation && lease.field_ == entry.field && lease.input_ == entry.input &&
           lease.algorithm_ == entry.algorithm;
}
void Crowd::Impl::attachFlowPins(std::span<FlowLease*> leases) {
    auto                                               staged = flowCache;
    std::unordered_map<std::uint64_t, FlowCacheEntry*> entries;
    entries.reserve(staged.size());
    for (auto& entry : staged) entries.emplace(entry.generation, &entry);
    for (auto* flow : leases) {
        if (!flow || !flow->field_ || flow->owner_ != owner) continue;
        const auto found = entries.find(flow->generation_);
        if (found == entries.end()) continue;
        auto& entry = *found->second;
        if (!matchesFlowLease(entry, *flow)) continue;
        auto pin = entry.pins.lock();
        if (!pin) pin = std::make_shared<const unsigned char>(0);
        flow->pin_ = pin;
        entry.pins = std::move(pin);
    }
    flowCache.swap(staged);
}
void Crowd::Impl::detachFlowPins(NavigationRequests* requests) {
    std::vector<FlowLease*> leases;
    leases.reserve(flows.size() + (requests ? requests->entries.size() : 0));
    for (auto& flow : flows) leases.push_back(&flow);
    if (requests)
        for (auto& entry : requests->entries)
            if (entry.lease) leases.push_back(&*entry.lease);
    for (auto* flow : leases) flow->pin_.reset();
    for (auto& entry : flowCache) entry.pins.reset();
    attachFlowPins(leases);
}
Result<AgentRef> Crowd::Snapshot::resolveNamedAgent(const std::string& key) const {
    if (state_) {
        const auto& s     = state_->state;
        auto        found = s.namedAgents.find(key);
        if (found != s.namedAgents.end() && found->second >= 0 &&
            std::size_t(found->second) < s.denseToRegistry.size()) {
            const auto index = s.denseToRegistry[std::size_t(found->second)];
            return Result<AgentRef>::success({AgentHandle(index, s.registry[index].generation), s.owner});
        }
    }
    return Result<AgentRef>::failure(Diagnostic::error(DiagnosticCode::NotFound, "Missing snapshot named agent"));
}
std::uint64_t Crowd::Snapshot::referenceScope() const noexcept { return state_ ? state_->state.owner : 0; }
Result<void>  Crowd::Snapshot::validateGoalSet(std::span<const FlowGoal> goals, std::uint64_t inputRevision) const {
    if (!state_ || inputRevision == 0 || goals.empty() || goals.size() > 1024)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid historical goal context"));
    if (inputRevision != state_->state.field.inputRevision())
        return Result<void>::success(Status::success(StatusCode::NoOp));
    auto canonical = canonicalFlowCoordinates(goals, state_->state.field);
    if (!canonical) return Result<void>::failure(canonical.status());
    return Result<void>::success();
}
Result<AgentFrame> Crowd::Snapshot::frame() const {
    if (!state_)
        return Result<AgentFrame>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Empty Crowd snapshot"));
    AgentFrame frame;
    frame.agents.reserve(state_->state.xs.size());
    state_->state.fillAgentFrame(frame);
    return Result<AgentFrame>::success(std::move(frame));
}
Crowd::RestorePlan::RestorePlan()                                         = default;
Crowd::RestorePlan::~RestorePlan()                                        = default;
Crowd::RestorePlan::RestorePlan(RestorePlan&&) noexcept                   = default;
Crowd::RestorePlan& Crowd::RestorePlan::operator=(RestorePlan&&) noexcept = default;

std::uint64_t Crowd::RestorePlan::owner() const noexcept { return state_ ? state_->state->owner : 0; }

std::size_t Crowd::RestorePlan::agentCount() const noexcept { return state_ ? state_->state->xs.size() : 0; }

Result<AgentFrameEntry> Crowd::RestorePlan::inspectAgent(std::size_t row) const {
    if (!state_ || row >= state_->state->xs.size())
        return Result<AgentFrameEntry>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Empty plan or invalid prepared agent row", "snapshot"));
    const auto index = state_->state->denseToRegistry[row];
    return Result<AgentFrameEntry>::success(
        {{AgentHandle(index, state_->state->registry[index].generation), state_->state->owner},
         state_->state->agentState(row)});
}

Result<FlowRequestRef> Crowd::RestorePlan::remapFlowRequest(FlowRequestRef source) const {
    if (!state_ || !source.isValid() || source.owner != state_->sourceOwner || !state_->requests ||
        source.handle.index() >= state_->requests->entries.size())
        return Result<FlowRequestRef>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Empty plan, foreign or missing source request", "snapshot"));
    const auto& entry = state_->requests->entries[source.handle.index()];
    if (!entry.occupied || entry.generation != source.handle.generation())
        return Result<FlowRequestRef>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Released or stale source request", "snapshot"));
    return Result<FlowRequestRef>::success({source.handle, state_->state->owner});
}

Result<Crowd::Snapshot> Crowd::captureSnapshot() const try {
    if (impl_->budgetActive)
        return Result<Snapshot>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Cannot capture during Crowd advancement", "snapshot"));
    Snapshot snapshot;
    auto     state  = std::make_shared<SnapshotData>(*impl_);
    state->requests = cloneNavigationRequests(navigationRequests_.get());
    state->state.workScheduler.reset();
    state->state.execution = CrowdExecution::Serial;
    state->state.detachFlowPins(state->requests.get());
    snapshot.state_ = std::move(state);
    return Result<Snapshot>::success(std::move(snapshot));
} catch (const std::bad_alloc&) {
    return Result<Snapshot>::failure(
        Diagnostic::error(DiagnosticCode::Failed, "Cannot allocate Crowd checkpoint", "snapshot"));
}

Result<Crowd::RestorePlan> Crowd::prepareRestore(const Snapshot& snapshot) const try {
    if (!snapshot.state_ || impl_->budgetActive)
        return Result<RestorePlan>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Invalid snapshot or active Crowd step", "snapshot"));
    RestorePlan plan;
    plan.state_              = std::make_unique<RestoreData>();
    plan.state_->state       = std::make_unique<Impl>();
    const auto owner         = plan.state_->state->owner;
    *plan.state_->state      = snapshot.state_->state;
    plan.state_->sourceOwner = snapshot.state_->state.owner;
    plan.state_->source      = snapshot.state_;
    plan.state_->requests    = cloneNavigationRequests(snapshot.state_->requests.get());
    plan.state_->state->detachFlowPins(plan.state_->requests.get());
    plan.state_->state->owner = owner;
    for (auto& lease : plan.state_->state->flows)
        if (lease.field_) lease.owner_ = lease.owner_ == plan.state_->sourceOwner ? owner : 0;
    if (plan.state_->requests)
        for (auto& entry : plan.state_->requests->entries) {
            if (entry.plan.owner_ == plan.state_->sourceOwner) entry.plan.owner_ = owner;
            if (entry.lease && entry.lease->owner_ == plan.state_->sourceOwner) entry.lease->owner_ = owner;
        }
    plan.state_->state->budgetActive = false;
    plan.state_->state->spatialIndex.clear();
    plan.state_->state->contactPairs.clear();
    plan.state_->state->contactDeltas.clear();
    plan.state_->state->predictionNeighbors.clear();
    plan.state_->state->preferredNeighbors.clear();
    plan.state_->state->preferredStarts.clear();
    plan.state_->state->preferredCounts.clear();
    plan.state_->state->preferredFlows.clear();
    plan.state_->state->incidentStarts.clear();
    plan.state_->state->incidentPairs.clear();
    plan.state_->state->workReport = {};
    plan.targetOwner_              = impl_->owner;
    return Result<RestorePlan>::success(std::move(plan));
} catch (const std::exception& error) {
    return Result<RestorePlan>::failure(Diagnostic::error(DiagnosticCode::Failed, error.what(), "snapshot"));
}

Result<void> Crowd::commitRestore(RestorePlan&& plan) {
    if (!plan.state_ || plan.targetOwner_ != impl_->owner || impl_->budgetActive)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Empty, foreign or stale Crowd restore plan", "snapshot"));
    plan.state_->state->execution     = impl_->execution;
    plan.state_->state->workScheduler = impl_->workScheduler;
    impl_.swap(plan.state_->state);
    navigationRequests_.swap(plan.state_->requests);
    plan.state_.reset();
    plan.targetOwner_ = 0;
    return Result<void>::success(Status::success(StatusCode::Applied));
}
}  // namespace eve::crowd
