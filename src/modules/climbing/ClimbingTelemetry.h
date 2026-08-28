#pragma once

/**
 * @file ClimbingTelemetry.h
 * @brief Fixed-capacity, allocation-free performance telemetry for climbing workloads.
 */

#include "climbing/ClimbingPrimitives.h"
#include "common/Time.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace eve::climbing {

/** @brief Stable workload classes used by the climbing performance budgets. */
enum class ClimbingWorkload : std::uint8_t { Ordinary, CandidateProbe, Active };

/** @brief Named result of comparing one workload sample with its query budget. */
enum class ClimbingQueryBudgetState : std::uint8_t { WithinBudget, Exceeded };

/** @brief Query budgets for one 60 Hz primary-character simulation tick. */
struct ClimbingQueryBudgets {
    static constexpr std::uint32_t Ordinary = 4;
    static constexpr std::uint32_t CandidateProbe = 16;
    static constexpr std::uint32_t Active = 8;

    /** @brief Return the fixed narrow-query budget for a workload. */
    [[nodiscard]] static constexpr std::uint32_t forWorkload(ClimbingWorkload workload) noexcept {
        switch (workload) {
            case ClimbingWorkload::Ordinary: return Ordinary;
            case ClimbingWorkload::CandidateProbe: return CandidateProbe;
            case ClimbingWorkload::Active: return Active;
        }
        return 0;
    }
};

/** @brief Allocation-free counters produced by the latest runtime workload. */
struct ClimbingRuntimeCounters {
    ClimbingWorkload         workload = ClimbingWorkload::Ordinary;
    std::uint32_t            broadPhaseQueryCount = 0;
    std::uint32_t            broadPhaseHitCount = 0;
    std::uint32_t            queryCount = 0;
    std::uint32_t            rejectCount = 0;
    std::int64_t             selectedCost = 0;
    Vec3                     warpResidual;
    std::uint32_t            moverIterations = 0;
    std::uint32_t            queryBudget = ClimbingQueryBudgets::Ordinary;
    ClimbingQueryBudgetState budgetState = ClimbingQueryBudgetState::WithinBudget;
};

/** @brief One profiler sample; elapsed time is observational and never feeds simulation state. */
struct ClimbingTelemetrySample {
    eve::SimulationTick     tick = eve::SimulationTick::zero();
    std::uint64_t           elapsedNanoseconds = 0;
    ClimbingRuntimeCounters counters;
};

/** @brief Fixed percentile summary for one workload without retaining dynamic storage. */
struct ClimbingTelemetrySummary {
    ClimbingWorkload workload = ClimbingWorkload::Ordinary;
    std::uint32_t    sampleCount = 0;
    std::uint64_t    p50Nanoseconds = 0;
    std::uint64_t    p95Nanoseconds = 0;
    std::uint32_t    p50QueryCount = 0;
    std::uint32_t    p95QueryCount = 0;
    std::uint32_t    maxQueryCount = 0;
    std::uint32_t    budgetExceededCount = 0;
};

/**
 * @brief Runtime-owned fixed ring of performance samples.
 *
 * Recording overwrites the oldest sample at capacity and performs no allocation. Percentile summaries use
 * fixed stack arrays and therefore remain safe to request while diagnostic string capture is disabled.
 */
class ClimbingTelemetryBuffer {
public:
    static constexpr std::size_t Capacity = 256;

    /** @brief Append one sample, overwriting the oldest sample when full. */
    void record(ClimbingTelemetrySample sample) noexcept;
    /** @brief Replace counters on the most recent sample after a caller completes deferred hard validation. */
    void updateLatestCounters(ClimbingRuntimeCounters counters) noexcept;
    /** @brief Return current samples in physical storage order; order is irrelevant to percentile summaries. */
    [[nodiscard]] std::span<const ClimbingTelemetrySample> samples() const noexcept {
        return {samples_.data(), size_};
    }
    /** @brief Compute exact nearest-rank p50/p95 values for one workload. */
    [[nodiscard]] ClimbingTelemetrySummary summarize(ClimbingWorkload workload) const noexcept;
    /** @brief Clear samples without releasing storage. */
    void clear() noexcept {
        size_ = 0;
        next_ = 0;
    }

private:
    std::array<ClimbingTelemetrySample, Capacity> samples_{};
    std::size_t                                   size_ = 0;
    std::size_t                                   next_ = 0;
};

}  // namespace eve::climbing
