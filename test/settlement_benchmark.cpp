#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "settlement/SettlementRules.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#include <Psapi.h>
#if defined(_DEBUG)
#include <crtdbg.h>
#endif
#else
#include <sys/resource.h>
#endif

namespace {

#if defined(_MSC_VER) && defined(_DEBUG)
std::size_t settlementBenchmarkAllocations = 0;
bool        settlementBenchmarkTracksAllocations = false;

int __cdecl settlementBenchmarkAllocationHook(int allocationType, void*, std::size_t, int, long,
                                               const unsigned char*, int) {
    if (settlementBenchmarkTracksAllocations && allocationType == _HOOK_ALLOC)
        ++settlementBenchmarkAllocations;
    return TRUE;
}
#endif

std::optional<std::uint64_t> peakResidentBytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) return std::nullopt;
    return static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return std::nullopt;
#if defined(__APPLE__)
    return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024u;
#endif
#endif
}

template <class Operation>
std::optional<double> measureAllocations(std::size_t operations, Operation operation) {
#if defined(_MSC_VER) && defined(_DEBUG)
    settlementBenchmarkAllocations            = 0;
    const _CRT_ALLOC_HOOK previousHook         = _CrtSetAllocHook(&settlementBenchmarkAllocationHook);
    settlementBenchmarkTracksAllocations       = true;
    for (std::size_t index = 0; index < operations; ++index) operation();
    settlementBenchmarkTracksAllocations = false;
    _CrtSetAllocHook(previousHook);
    return static_cast<double>(settlementBenchmarkAllocations) / static_cast<double>(operations);
#else
    (void)operations;
    (void)operation;
    return std::nullopt;
#endif
}

void writeOptionalNumber(std::optional<double> value) {
    if (value)
        std::cout << *value;
    else
        std::cout << "null";
}

void writeOptionalInteger(std::optional<std::uint64_t> value) {
    if (value)
        std::cout << *value;
    else
        std::cout << "null";
}

eve::SubjectRef benchmarkSubject(std::size_t suffix) {
    eve::PersistentId::Bytes bytes{};
    for (std::size_t index = 0; index < sizeof(suffix); ++index) {
        bytes[bytes.size() - 1 - index] = static_cast<std::uint8_t>(suffix >> (index * 8));
    }
    return eve::SubjectRef::fromPersistentId(eve::PersistentId(bytes));
}

class BenchmarkPolicy final : public eve::settlement::ISettlementPolicy {
public:
    explicit BenchmarkPolicy(std::uint32_t triggerDepth = 0) : triggerDepth_(triggerDepth) {}

    eve::Result<void> validate(eve::settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> sourceModifiers(eve::settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> targetMitigation(eve::settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> armorShield(eve::settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> clamp(eve::settlement::SettlementContext& context) override {
        return context.setClampMax(1'000'000.0);
    }
    eve::Result<eve::settlement::PreparedApply> prepareApply(
        const eve::settlement::SettlementContext&) override {
        return eve::Result<eve::settlement::PreparedApply>::success(
            eve::settlement::PreparedApply([] { return eve::Result<void>::success(); }, [] {}));
    }

    eve::Result<std::vector<eve::settlement::SettlementRequest>> prepareTrigger(
        const eve::settlement::SettlementContext& context, const eve::settlement::SettlementResult&) override {
        if (context.request().chain.depth >= triggerDepth_)
            return eve::Result<std::vector<eve::settlement::SettlementRequest>>::success({});
        auto child    = context.request();
        child.trigger = "benchmark.depth." + std::to_string(context.request().chain.depth + 1);
        child.chain   = {};
        return eve::Result<std::vector<eve::settlement::SettlementRequest>>::success({std::move(child)});
    }

private:
    std::uint32_t triggerDepth_ = 0;
};

eve::settlement::SettlementPipeline benchmarkPipeline(std::size_t ruleCount, std::size_t matchingRuleCount) {
    std::vector<eve::settlement::SettlementRule> configured;
    configured.reserve(ruleCount);
    for (std::size_t index = 0; index < ruleCount; ++index) {
        eve::settlement::SettlementRule rule;
        rule.id           = "benchmark.rule." + std::to_string(index);
        rule.source       = "benchmark";
        rule.stage        = index < ruleCount / 2 ? eve::settlement::StageKind::SourceModifiers
                                                  : eve::settlement::StageKind::TargetMitigation;
        rule.priority     = static_cast<int>(index);
        rule.operation    = eve::settlement::RuleOperation::Multiply;
        rule.value        = 1.0;
        rule.filter.kinds = {"damage"};
        rule.when         = index < matchingRuleCount ? "context.enabled && magnitude > 0" : "false";
        configured.push_back(std::move(rule));
    }
    eve::settlement::SettlementRuleSet rules;
    rules.configure(std::move(configured)).ignore("benchmark fixture configuration");
    eve::settlement::SettlementPipeline pipeline;
    rules.install(pipeline).ignore("benchmark fixture installation");
    return pipeline;
}

double percentile(std::vector<double> samples, double fraction) {
    std::sort(samples.begin(), samples.end());
    const auto index = static_cast<std::size_t>(fraction * static_cast<double>(samples.size() - 1));
    return samples[index];
}

template <class Operation>
std::array<double, 2> measure(std::size_t samples, std::size_t operationsPerSample, Operation operation) {
    std::vector<double> microseconds;
    microseconds.reserve(samples);
    for (std::size_t sample = 0; sample < samples; ++sample) {
        const auto started = std::chrono::steady_clock::now();
        for (std::size_t index = 0; index < operationsPerSample; ++index) operation();
        const auto elapsed = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started);
        microseconds.push_back(elapsed.count() / static_cast<double>(operationsPerSample));
    }
    return {percentile(microseconds, 0.50), percentile(microseconds, 0.95)};
}

}  // namespace

TEST_CASE("settlement.benchmark.traceAndIndependentBatchReportP50P95") {
    if (!std::getenv("EVENGINE_SETTLEMENT_BENCHMARK")) return;
    constexpr std::size_t samples = 7;
    const auto            peakResidentBytesBefore = peakResidentBytes();
    BenchmarkPolicy       policy;
    eve::settlement::SettlementRequest request;
    request.source    = benchmarkSubject(1);
    request.target    = benchmarkSubject(2);
    request.kind      = "damage";
    request.resource  = "hp";
    request.magnitude = 10.0;
    request.context   = eve::Value::Object{{"enabled", true}};

    constexpr std::array<std::size_t, 3> ruleCounts    = {10, 100, 1000};
    constexpr std::array<std::size_t, 3> matchPercents = {0, 50, 100};
    constexpr std::array<eve::settlement::SettlementTraceLevel, 3> traceLevels = {
        eve::settlement::SettlementTraceLevel::Off,
        eve::settlement::SettlementTraceLevel::Summary,
        eve::settlement::SettlementTraceLevel::Full,
    };
    constexpr std::array<const char*, 3> traceNames = {"off", "summary", "full"};

    std::cout << "SETTLEMENT_BENCHMARK_JSON={\"samples\":" << samples << ",\"matrix\":[";
    bool first = true;
    for (const auto ruleCount : ruleCounts) {
        const std::size_t operationsPerSample = ruleCount == 10 ? 25 : (ruleCount == 100 ? 5 : 1);
        for (const auto matchPercent : matchPercents) {
            auto pipeline = benchmarkPipeline(ruleCount, ruleCount * matchPercent / 100);
            for (std::size_t traceIndex = 0; traceIndex < traceLevels.size(); ++traceIndex) {
                request.trace = traceLevels[traceIndex];
                const auto timing = measure(samples, operationsPerSample, [&] {
                    auto settled = pipeline.settle(request, policy);
                    settled.ignore("settlement rule/trace matrix benchmark sample");
                });
                const auto allocations = measureAllocations(operationsPerSample, [&] {
                    auto settled = pipeline.settle(request, policy);
                    settled.ignore("settlement rule/trace allocation benchmark sample");
                });
                if (!first) std::cout << ',';
                first = false;
                std::cout << "{\"rules\":" << ruleCount << ",\"matchPercent\":" << matchPercent
                          << ",\"trace\":\"" << traceNames[traceIndex]
                          << "\",\"operationsPerSample\":" << operationsPerSample << ",\"p50Us\":"
                          << timing[0] << ",\"p95Us\":" << timing[1] << ",\"allocationsPerOperation\":";
                writeOptionalNumber(allocations);
                std::cout << '}';
                CHECK(timing[0] > 0.0);
                CHECK(timing[1] >= timing[0]);
            }
        }
    }

    constexpr std::size_t batchSize = 1000;
    std::vector<eve::settlement::SettlementRequest> requests(batchSize, request);
    std::vector<eve::settlement::ISettlementPolicy*> policies(batchSize, &policy);
    for (std::size_t index = 0; index < batchSize; ++index) {
        requests[index].target = benchmarkSubject(index + 2);
        requests[index].trace  = eve::settlement::SettlementTraceLevel::Off;
    }
    auto                  pipeline         = benchmarkPipeline(10, 5);
    constexpr std::size_t batchesPerSample = 1;
    const auto independent = measure(samples, batchesPerSample, [&] {
        auto settled = pipeline.settleIndependent(requests, policies);
        settled.ignore("settlement independent batch benchmark sample");
    });
    const auto independentAllocations = measureAllocations(batchesPerSample, [&] {
        auto settled = pipeline.settleIndependent(requests, policies);
        settled.ignore("settlement independent batch allocation benchmark sample");
    });
    const double independentP50 = independent[0] / static_cast<double>(batchSize);
    const double independentP95 = independent[1] / static_cast<double>(batchSize);

    std::cout << "],\"independent\":{\"rules\":10,\"matchPercent\":50,\"batchSize\":" << batchSize
              << ",\"batchesPerSample\":" << batchesPerSample
              << ",\"independentP50UsPerItem\":" << independentP50
              << ",\"independentP95UsPerItem\":" << independentP95
              << ",\"allocationsPerItem\":";
    if (independentAllocations)
        writeOptionalNumber(*independentAllocations / static_cast<double>(batchSize));
    else
        writeOptionalNumber(std::nullopt);
    std::cout << "},\"triggerDepths\":[";
    CHECK(independentP95 >= independentP50);

    constexpr std::array<std::uint32_t, 3> triggerDepths = {1, 4, 8};
    for (std::size_t index = 0; index < triggerDepths.size(); ++index) {
        const auto      depth = triggerDepths[index];
        BenchmarkPolicy triggerPolicy(depth);
        eve::settlement::SettlementPipeline::RequestExecutor execute =
            [&](const eve::settlement::SettlementRequest& chained) { return pipeline.settle(chained, triggerPolicy); };
        request.trace = eve::settlement::SettlementTraceLevel::Off;
        const auto timing = measure(samples, 5, [&] {
            auto settled = pipeline.settleChain(request, execute, depth, depth + 1);
            settled.ignore("settlement trigger-depth benchmark sample");
        });
        const auto allocations = measureAllocations(5, [&] {
            auto settled = pipeline.settleChain(request, execute, depth, depth + 1);
            settled.ignore("settlement trigger-depth allocation benchmark sample");
        });
        if (index != 0) std::cout << ',';
        std::cout << "{\"depth\":" << depth << ",\"p50Us\":" << timing[0] << ",\"p95Us\":" << timing[1]
                  << ",\"allocationsPerChain\":";
        writeOptionalNumber(allocations);
        std::cout << '}';
        CHECK(timing[0] > 0.0);
        CHECK(timing[1] >= timing[0]);
    }
    const auto peakResidentBytesAfter = peakResidentBytes();
    if (peakResidentBytesBefore && peakResidentBytesAfter)
        CHECK(*peakResidentBytesAfter >= *peakResidentBytesBefore);
    std::cout << "],\"memory\":{\"peakResidentBytesBefore\":";
    writeOptionalInteger(peakResidentBytesBefore);
    std::cout << ",\"peakResidentBytesAfter\":";
    writeOptionalInteger(peakResidentBytesAfter);
    std::cout << "}}" << std::endl;
}
