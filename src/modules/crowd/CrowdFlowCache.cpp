#include <array>
#include <limits>
#include <tuple>
#include <type_traits>
#include "crowd/CrowdInternal.h"
namespace eve::crowd {
namespace {
template <class T>
Result<T> cacheFailure(DiagnosticCode code, const char* text) {
    return Result<T>::failure(Diagnostic::error(code, text, "flowCache"));
}
}  // namespace
Result<std::vector<int>> canonicalFlowCoordinates(std::span<const FlowGoal> goals, const CrowdField& field) {
    if (!field.valid() || goals.empty() || goals.size() > 1024)
        return cacheFailure<std::vector<int>>(DiagnosticCode::InvalidArgument,
                                              "Expected 1..1024 goals and a configured field");
    for (auto goal : goals)
        if (goal.x < 0 || goal.y < 0 || goal.x >= field.getWidth() || goal.y >= field.getHeight())
            return cacheFailure<std::vector<int>>(DiagnosticCode::InvalidArgument, "Goal outside grid");
    std::vector<FlowGoal> sorted(goals.begin(), goals.end());
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    std::vector<int> result;
    result.reserve(sorted.size() * 2);
    for (auto goal : sorted) {
        result.push_back(goal.x);
        result.push_back(goal.y);
    }
    return Result<std::vector<int>>::success(std::move(result));
}
bool isFlowPointReachable(const CrowdField& field, float x, float y) noexcept {
    if (!field.valid() || !field.isBuilt() || !std::isfinite(x) || !std::isfinite(y)) return false;
    const double cx = std::floor((double(x) - field.getOriginX()) / field.getCellSize());
    const double cy = std::floor((double(y) - field.getOriginY()) / field.getCellSize());
    return cx >= 0 && cy >= 0 && cx < field.getWidth() && cy < field.getHeight() &&
           field.isReachable(static_cast<int>(cx), static_cast<int>(cy));
}
std::uint64_t Crowd::Impl::retainedFlowBytes(const CrowdField& value) {
    return sizeof(CrowdField) + sizeof(FlowCacheEntry) +
           sizeof(float) * (value.cost_.capacity() + value.navigationCost_.capacity() + value.integ_.capacity() +
                            value.flowX_.capacity() + value.flowY_.capacity()) +
           sizeof(int) * value.goals_.capacity() + sizeof(std::pair<float, int>) * value.buildHeap_.capacity();
}
bool Crowd::Impl::isFlowKeyBefore(const FlowCacheEntry& a, const FlowCacheEntry& b) {
    const auto prefix = [](const auto& entry) {
        const auto& s = entry.surface;
        return std::tuple{entry.input,        s.surface.value(),   s.surfaceRevision, s.movement.value(),
                          s.movementRevision, s.costModelRevision, s.clearanceRadius, entry.algorithm};
    };
    if (prefix(a) != prefix(b)) return prefix(a) < prefix(b);
    return a.field->goals_ < b.field->goals_;
}
Result<std::vector<Crowd::Impl::FlowCacheEntry>> Crowd::Impl::stageFlowCache(std::uint64_t budget,
                                                                             std::uint64_t addedBytes,
                                                                             size_t        addedCount) const {
    if (addedBytes > budget)
        return cacheFailure<std::vector<FlowCacheEntry>>(DiagnosticCode::PreconditionViolation,
                                                         "Field exceeds cache storage budget");
    std::vector<bool> evicted(flowCache.size(), false);
    size_t            count = flowCache.size();
    std::uint64_t     bytes = 0;
    for (const auto& entry : flowCache) bytes += entry.bytes;
    while (bytes > budget - addedBytes || count + addedCount > 10000) {
        size_t victim = flowCache.size();
        for (size_t i = 0; i < flowCache.size(); ++i) {
            if (evicted[i] || !flowCache[i].pins.expired()) continue;
            if (victim == flowCache.size() || flowCache[i].access < flowCache[victim].access ||
                (flowCache[i].access == flowCache[victim].access && isFlowKeyBefore(flowCache[i], flowCache[victim])))
                victim = i;
        }
        if (victim == flowCache.size())
            return cacheFailure<std::vector<FlowCacheEntry>>(DiagnosticCode::PreconditionViolation,
                                                             "Cache pressure: all remaining fields are pinned");
        evicted[victim] = true;
        bytes -= flowCache[victim].bytes;
        --count;
    }
    std::vector<FlowCacheEntry> staged;
    staged.reserve(count + addedCount);
    for (size_t i = 0; i < flowCache.size(); ++i)
        if (!evicted[i]) staged.push_back(flowCache[i]);
    return Result<std::vector<FlowCacheEntry>>::success(std::move(staged));
}
FlowLease Crowd::Impl::leaseFor(FlowCacheEntry& entry) {
    auto pin = entry.pins.lock();
    if (!pin) pin = std::make_shared<const unsigned char>(0);
    entry.pins   = pin;
    entry.access = tick;
    FlowLease result;
    result.field_      = entry.field;
    result.pin_        = std::move(pin);
    result.owner_      = owner;
    result.input_      = entry.input;
    result.generation_ = entry.generation;
    result.algorithm_  = entry.algorithm;
    return result;
}
Result<FlowLease> Crowd::acquireFlowField(std::span<const FlowGoal> goals) try {
    auto coordinates = canonicalFlowCoordinates(goals, impl_->field);
    if (!coordinates) return Result<FlowLease>::failure(coordinates.status());
    const auto surface = getFieldStatus().surface;
    for (auto& entry : impl_->flowCache)
        if (entry.input == impl_->field.inputRevision() && entry.surface == surface &&
            entry.algorithm == flowBuildAlgorithmVersion && entry.field->goals_ == coordinates.value())
            return Result<FlowLease>::success(impl_->leaseFor(entry));
    return cacheFailure<FlowLease>(DiagnosticCode::NotFound, "No ready field for complete navigation key");
} catch (const std::exception& error) {
    return cacheFailure<FlowLease>(DiagnosticCode::Failed, error.what());
}
Result<FlowLease> Crowd::acquireFlowField(FlowBuildPlan&& plan) try {
    if (!plan.field_ || plan.owner_ != impl_->owner || plan.revision_ != impl_->field.inputRevision() ||
        (plan.state_ != FlowBuildState::Ready && plan.state_ != FlowBuildState::Unreachable))
        return cacheFailure<FlowLease>(DiagnosticCode::Conflict, "Incomplete, stale or foreign field publication");
    const auto surface = getFieldStatus().surface;
    for (auto& entry : impl_->flowCache) {
        if (entry.input == plan.revision_ && entry.surface == surface && entry.algorithm == flowBuildAlgorithmVersion &&
            entry.field->goals_ == plan.field_->goals_) {
            auto lease = impl_->leaseFor(entry);
            plan.field_.reset();
            plan.state_ = FlowBuildState::Committed;
            return Result<FlowLease>::success(std::move(lease));
        }
    }
    if (impl_->nextFlowGeneration == std::numeric_limits<std::uint64_t>::max())
        return cacheFailure<FlowLease>(DiagnosticCode::Conflict, "Flow generation exhausted");
    const auto bytes  = Impl::retainedFlowBytes(*plan.field_);
    auto       staged = impl_->stageFlowCache(impl_->flowCacheBudget, bytes, 1);
    if (!staged) return Result<FlowLease>::failure(staged.status());
    auto entries = std::move(staged).takeValue();
    static_assert(std::is_nothrow_move_constructible_v<CrowdField>);
    static_assert(std::is_nothrow_move_constructible_v<Impl::FlowCacheEntry>);
    // All metadata allocation and eviction validation precede the no-throw storage move.
    auto pin  = std::make_shared<const unsigned char>(0);
    auto data = std::make_shared<const CrowdField>(std::move(*plan.field_));
    entries.push_back({std::move(data), pin, surface, plan.revision_, impl_->nextFlowGeneration, bytes, impl_->tick,
                       flowBuildAlgorithmVersion});
    auto lease = impl_->leaseFor(entries.back());
    impl_->flowCache.swap(entries);
    ++impl_->nextFlowGeneration;
    plan.field_.reset();
    plan.state_ = FlowBuildState::Committed;
    return Result<FlowLease>::success(std::move(lease));
} catch (const std::exception& error) {
    return cacheFailure<FlowLease>(DiagnosticCode::Failed, error.what());
}
Result<void> Crowd::configureFlowCache(std::uint64_t budgetBytes) try {
    if (budgetBytes > 8ULL * 1024 * 1024 * 1024)
        return cacheFailure<void>(DiagnosticCode::InvalidArgument, "Cache budget exceeds eight GiB");
    auto staged = impl_->stageFlowCache(budgetBytes, 0, 0);
    if (!staged) return Result<void>::failure(staged.status());
    impl_->flowCache       = std::move(staged).takeValue();
    impl_->flowCacheBudget = budgetBytes;
    return Result<void>::success();
} catch (const std::exception& error) {
    return cacheFailure<void>(DiagnosticCode::Failed, error.what());
}
FlowCacheObservation Crowd::getFlowCacheStatus() const noexcept {
    FlowCacheObservation result;
    result.entries     = impl_->flowCache.size();
    result.budgetBytes = impl_->flowCacheBudget;
    for (const auto& entry : impl_->flowCache) result.residentBytes += entry.bytes;
    return result;
}
Result<FlowSample> Crowd::sampleFlowField(const FlowLease& lease, float x, float y) const {
    if (!std::isfinite(x) || !std::isfinite(y))
        return cacheFailure<FlowSample>(DiagnosticCode::InvalidArgument, "Nonfinite sample position");
    if (!impl_->isFlowLeaseCurrent(lease))
        return cacheFailure<FlowSample>(DiagnosticCode::Conflict, "Stale or foreign flow lease");
    FlowSample result;
    if (isFlowPointReachable(*lease.field_, x, y)) {
        result.state = FlowSampleState::Reachable;
        lease.field_->flowAtWorld(x, y, result.direction.x, result.direction.y);
    }
    return Result<FlowSample>::success(result);
}
}  // namespace eve::crowd
