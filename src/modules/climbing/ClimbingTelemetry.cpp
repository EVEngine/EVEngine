#include "climbing/ClimbingTelemetry.h"

#include <algorithm>

namespace eve::climbing {
namespace {

template <class T>
T nearestRank(const std::array<T, ClimbingTelemetryBuffer::Capacity>& values, std::size_t count,
              std::size_t percentile) noexcept {
    if (count == 0) return {};
    const std::size_t rank = std::min(count - 1, (count * percentile + 99) / 100 - 1);
    return values[rank];
}

}  // namespace

void ClimbingTelemetryBuffer::record(ClimbingTelemetrySample sample) noexcept {
    samples_[next_] = sample;
    next_ = (next_ + 1) % Capacity;
    size_ = std::min(size_ + 1, Capacity);
}

void ClimbingTelemetryBuffer::updateLatestCounters(ClimbingRuntimeCounters counters) noexcept {
    if (size_ == 0) return;
    const std::size_t latest = next_ == 0 ? Capacity - 1 : next_ - 1;
    samples_[latest].counters = counters;
}

ClimbingTelemetrySummary ClimbingTelemetryBuffer::summarize(ClimbingWorkload workload) const noexcept {
    std::array<std::uint64_t, Capacity> durations{};
    std::array<std::uint32_t, Capacity> queries{};
    std::size_t                        count = 0;
    std::uint32_t                      exceeded = 0;
    std::uint32_t                      maximum = 0;
    for (std::size_t index = 0; index < size_; ++index) {
        const ClimbingTelemetrySample& sample = samples_[index];
        if (sample.counters.workload != workload) continue;
        durations[count] = sample.elapsedNanoseconds;
        queries[count] = sample.counters.queryCount;
        maximum = std::max(maximum, sample.counters.queryCount);
        if (sample.counters.budgetState == ClimbingQueryBudgetState::Exceeded) ++exceeded;
        ++count;
    }
    std::sort(durations.begin(), durations.begin() + static_cast<std::ptrdiff_t>(count));
    std::sort(queries.begin(), queries.begin() + static_cast<std::ptrdiff_t>(count));

    ClimbingTelemetrySummary summary;
    summary.workload = workload;
    summary.sampleCount = static_cast<std::uint32_t>(count);
    summary.p50Nanoseconds = nearestRank(durations, count, 50);
    summary.p95Nanoseconds = nearestRank(durations, count, 95);
    summary.p50QueryCount = nearestRank(queries, count, 50);
    summary.p95QueryCount = nearestRank(queries, count, 95);
    summary.maxQueryCount = maximum;
    summary.budgetExceededCount = exceeded;
    return summary;
}

}  // namespace eve::climbing
