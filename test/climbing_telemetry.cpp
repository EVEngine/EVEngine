#include "climbing/ClimbingTelemetry.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::climbing;

TEST_CASE("climbing.telemetry.fixedRingReportsExactPercentilesAndBudgets") {
    ClimbingTelemetryBuffer buffer;
    for (std::uint64_t index = 1; index <= 100; ++index) {
        ClimbingRuntimeCounters counters;
        counters.workload    = ClimbingWorkload::CandidateProbe;
        counters.queryCount  = static_cast<std::uint32_t>(index % 17);
        counters.queryBudget = ClimbingQueryBudgets::CandidateProbe;
        counters.budgetState = counters.queryCount > counters.queryBudget ? ClimbingQueryBudgetState::Exceeded
                                                                          : ClimbingQueryBudgetState::WithinBudget;
        buffer.record({SimulationTick(index), index * 100, counters});
    }

    const auto summary = buffer.summarize(ClimbingWorkload::CandidateProbe);
    CHECK(summary.sampleCount == 100);
    CHECK(summary.p50Nanoseconds == 5000);
    CHECK(summary.p95Nanoseconds == 9500);
    CHECK(summary.maxQueryCount == 16);
    CHECK(summary.budgetExceededCount == 0);
    CHECK(buffer.samples().size() == 100);
}

TEST_CASE("climbing.telemetry.ringOverwritesWithoutGrowing") {
    ClimbingTelemetryBuffer buffer;
    for (std::uint64_t index = 0; index < ClimbingTelemetryBuffer::Capacity + 20; ++index) {
        ClimbingRuntimeCounters counters;
        counters.workload    = ClimbingWorkload::Active;
        counters.queryBudget = ClimbingQueryBudgets::Active;
        counters.queryCount  = 9;
        counters.budgetState = ClimbingQueryBudgetState::Exceeded;
        buffer.record({SimulationTick(index + 1), index, counters});
    }
    const auto summary = buffer.summarize(ClimbingWorkload::Active);
    CHECK(buffer.samples().size() == ClimbingTelemetryBuffer::Capacity);
    CHECK(summary.sampleCount == ClimbingTelemetryBuffer::Capacity);
    CHECK(summary.budgetExceededCount == ClimbingTelemetryBuffer::Capacity);
}
