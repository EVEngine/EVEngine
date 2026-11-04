// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "performance",
  "module": "animation",
  "modules": [
    "animation",
    "emergence",
    "settlement",
    "stylize"
  ],
  "origins": [
    "emergence_perf.cpp",
    "settlement_benchmark.cpp",
    "stylize_mesh_vfx_benchmark.cpp"
  ],
  "cases": {
    "render-window": [
      "stylize.meshVfx.sceneColorRefractionSamplesBoundTexture"
    ]
  }
}
*/
// clang-format on

// Related integration contracts share one translation unit to amortize header parsing.

// Contract section: emergence perf
#include "PrivateTestScope.h"
#include "zeroerr/assert.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>
#include "common/Capability.h"
#include "common/Condition.h"
#include "common/Diagnostic.h"
#include "common/IEconomy.h"
#include "common/IEmergenceActionHandler.h"
#include "common/Value.h"
#include "emergence/RuleEngine.h"
#include "zeroerr/unittest.h"
// emergence_perf.cpp
namespace test_emergence_perf {
// emergence_perf.cpp
TEST_CASE("emergence.perf.tenThousandRulesSingleKeyWakeIsSublinear") {
    using namespace eve::condition;
    using namespace eve::emergence;

    constexpr int               kRules = 10000;
    RuleEngine                  engine;
    std::vector<RuleDefinition> rules;
    rules.reserve(static_cast<std::size_t>(kRules));
    for (int i = 0; i < kRules; ++i) {
        RuleDefinition rule;
        rule.id = "rule." + std::to_string(i);
        // Each rule watches a distinct value key — changing one key must evaluate ~1 rule.
        rule.condition = Condition::compare("flag." + std::to_string(i), CompareOperator::Equal, eve::Value(true));
        rule.actions.push_back(EmergenceAction{"emit", eve::Value::Object{{"type", eve::Value(rule.id)}}});
        rules.push_back(std::move(rule));
    }
    // One shared hot key watched by 100 rules to verify fan-out stays proportional to W(k).
    for (int i = 0; i < 100; ++i) {
        RuleDefinition rule;
        rule.id        = "shared." + std::to_string(i);
        rule.condition = Condition::compare("shared.hot", CompareOperator::GreaterEqual, eve::Value(1));
        rule.once      = true;
        rule.actions.push_back(EmergenceAction{"emit", eve::Value::Object{{"type", eve::Value(rule.id)}}});
        rules.push_back(std::move(rule));
    }

    auto committed = engine.replaceCatalogue(std::move(rules));
    REQUIRE(committed.ok());
    CHECK_EQ(committed.value(), kRules + 100);

    const auto t0 = std::chrono::steady_clock::now();
    REQUIRE(engine.setValue("flag.42", eve::Value(true)).ok());
    auto       drained = engine.drain(1);
    const auto t1      = std::chrono::steady_clock::now();
    REQUIRE(drained.ok());
    CHECK_EQ(drained.value(), 1);
    CHECK_EQ(engine.lastDrainEvaluations(), 1u);
    const auto coldNs = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

    engine.clearActivations();
    const auto t2 = std::chrono::steady_clock::now();
    REQUIRE(engine.setValue("shared.hot", eve::Value(5)).ok());
    drained       = engine.drain(2);
    const auto t3 = std::chrono::steady_clock::now();
    REQUIRE(drained.ok());
    CHECK_EQ(drained.value(), 100);
    CHECK_EQ(engine.lastDrainEvaluations(), 100u);
    const auto hotNs = std::chrono::duration_cast<std::chrono::nanoseconds>(t3 - t2).count();

    // Full-scan baseline would evaluate ~10100 rules; require at least 50x fewer evaluations.
    CHECK(engine.lastDrainEvaluations() * 50 < static_cast<std::uint64_t>(kRules + 100));

    std::fprintf(stderr, "[emergence.perf] rules=%d cold_eval=%llu cold_ns=%lld hot_eval=%llu hot_ns=%lld\n", kRules,
                 static_cast<unsigned long long>(1), static_cast<long long>(coldNs),
                 static_cast<unsigned long long>(100), static_cast<long long>(hotNs));
}

// emergence_rule_engine.cpp
namespace {

class QuestNotifyHandler final : public eve::IEmergenceActionHandler {
public:
    std::vector<std::string> topics;

    [[nodiscard]] eve::Result<bool> tryHandle(std::string_view kind, const eve::Value &args) override {
        if (kind != "quest.notify") return eve::Result<bool>::success(false);
        const auto *object = args.getIf<eve::Value::Object>();
        REQUIRE(object != nullptr);
        const auto it = object->find("topic");
        REQUIRE(it != object->end());
        REQUIRE(it->second.isString());
        topics.push_back(it->second.asString());
        return eve::Result<bool>::success(true);
    }
};

class FailingHandler final : public eve::IEmergenceActionHandler {
public:
    [[nodiscard]] eve::Result<bool> tryHandle(std::string_view kind, const eve::Value &) override {
        if (kind != "fail.always") return eve::Result<bool>::success(false);
        return eve::Result<bool>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed, "forced action failure",
                                                                 "fail.always", {}, "emergence.test"));
    }
};

class StubEconomy final : public eve::economy::IEconomy {
public:
    int  credit(int, const std::string &, int amount) override { return amount; }
    bool debit(int, const std::string &, int) override { return true; }
    int  get(int, const std::string &) const override { return 0; }
    int  getCap(int, const std::string &) const override { return 0; }
    int  getWasted(int, const std::string &) const override { return 0; }
    int  getIncome(int, const std::string &) const override { return 0; }
    int  getExpense(int, const std::string &) const override { return 0; }
};

[[maybe_unused]] eve::emergence::RuleDefinition makeGoldRule(std::string id, int threshold, std::string emitType) {
    using namespace eve::condition;
    using namespace eve::emergence;
    RuleDefinition rule;
    rule.id        = std::move(id);
    rule.condition = Condition::compare("gold", CompareOperator::GreaterEqual, eve::Value(threshold));
    rule.actions.push_back(EmergenceAction{"emit", eve::Value::Object{{"type", eve::Value(std::move(emitType))}}});
    return rule;
}

}  // namespace
}  // namespace test_emergence_perf

// Contract section: settlement benchmark
#include "PrivateTestScope.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include "settlement/SettlementRules.h"
#if defined(_WIN32)
#include <Windows.h>
#endif
#if defined(_WIN32)
#include <Psapi.h>
#endif
#if defined(_WIN32)
#if defined(_DEBUG)
#include <crtdbg.h>
#endif
#endif
#if defined(_WIN32)
#else
#include <sys/resource.h>
#endif
// settlement_benchmark.cpp
namespace test_settlement_benchmark {
#if defined(_WIN32)


#if defined(_DEBUG)

#endif
#else

#endif

namespace {

#if defined(_MSC_VER) && defined(_DEBUG)
std::size_t settlementBenchmarkAllocations       = 0;
bool        settlementBenchmarkTracksAllocations = false;

int __cdecl settlementBenchmarkAllocationHook(int allocationType, void *, std::size_t, int, long, const unsigned char *,
                                              int) {
    if (settlementBenchmarkTracksAllocations && allocationType == _HOOK_ALLOC) ++settlementBenchmarkAllocations;
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
    settlementBenchmarkAllocations       = 0;
    const _CRT_ALLOC_HOOK previousHook   = _CrtSetAllocHook(&settlementBenchmarkAllocationHook);
    settlementBenchmarkTracksAllocations = true;
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

    eve::Result<void> validate(eve::settlement::SettlementContext &) override { return eve::Result<void>::success(); }
    eve::Result<void> sourceModifiers(eve::settlement::SettlementContext &) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> targetMitigation(eve::settlement::SettlementContext &) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> armorShield(eve::settlement::SettlementContext &) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> clamp(eve::settlement::SettlementContext &context) override {
        return context.setClampMax(1'000'000.0);
    }
    eve::Result<eve::settlement::PreparedApply> prepareApply(const eve::settlement::SettlementContext &) override {
        return eve::Result<eve::settlement::PreparedApply>::success(
            eve::settlement::PreparedApply([] { return eve::Result<void>::success(); }, [] {}));
    }

    eve::Result<std::vector<eve::settlement::SettlementRequest>> prepareTrigger(
        const eve::settlement::SettlementContext &context, const eve::settlement::SettlementResult &) override {
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
    constexpr std::size_t              samples                 = 7;
    const auto                         peakResidentBytesBefore = peakResidentBytes();
    BenchmarkPolicy                    policy;
    eve::settlement::SettlementRequest request;
    request.source    = benchmarkSubject(1);
    request.target    = benchmarkSubject(2);
    request.kind      = "damage";
    request.resource  = "hp";
    request.magnitude = 10.0;
    request.context   = eve::Value::Object{{"enabled", true}};

    constexpr std::array<std::size_t, 3>                           ruleCounts    = {10, 100, 1000};
    constexpr std::array<std::size_t, 3>                           matchPercents = {0, 50, 100};
    constexpr std::array<eve::settlement::SettlementTraceLevel, 3> traceLevels   = {
        eve::settlement::SettlementTraceLevel::Off,
        eve::settlement::SettlementTraceLevel::Summary,
        eve::settlement::SettlementTraceLevel::Full,
    };
    constexpr std::array<const char *, 3> traceNames = {"off", "summary", "full"};

    std::cout << "SETTLEMENT_BENCHMARK_JSON={\"samples\":" << samples << ",\"matrix\":[";
    bool first = true;
    for (const auto ruleCount : ruleCounts) {
        const std::size_t operationsPerSample = ruleCount == 10 ? 25 : (ruleCount == 100 ? 5 : 1);
        for (const auto matchPercent : matchPercents) {
            auto pipeline = benchmarkPipeline(ruleCount, ruleCount * matchPercent / 100);
            for (std::size_t traceIndex = 0; traceIndex < traceLevels.size(); ++traceIndex) {
                request.trace          = traceLevels[traceIndex];
                const auto timing      = measure(samples, operationsPerSample, [&] {
                    auto settled = pipeline.settle(request, policy);
                    settled.ignore("settlement rule/trace matrix benchmark sample");
                });
                const auto allocations = measureAllocations(operationsPerSample, [&] {
                    auto settled = pipeline.settle(request, policy);
                    settled.ignore("settlement rule/trace allocation benchmark sample");
                });
                if (!first) std::cout << ',';
                first = false;
                std::cout << "{\"rules\":" << ruleCount << ",\"matchPercent\":" << matchPercent << ",\"trace\":\""
                          << traceNames[traceIndex] << "\",\"operationsPerSample\":" << operationsPerSample
                          << ",\"p50Us\":" << timing[0] << ",\"p95Us\":" << timing[1]
                          << ",\"allocationsPerOperation\":";
                writeOptionalNumber(allocations);
                std::cout << '}';
                CHECK(timing[0] > 0.0);
                CHECK(timing[1] >= timing[0]);
            }
        }
    }

    constexpr std::size_t                             batchSize = 1000;
    std::vector<eve::settlement::SettlementRequest>   requests(batchSize, request);
    std::vector<eve::settlement::ISettlementPolicy *> policies(batchSize, &policy);
    for (std::size_t index = 0; index < batchSize; ++index) {
        requests[index].target = benchmarkSubject(index + 2);
        requests[index].trace  = eve::settlement::SettlementTraceLevel::Off;
    }
    auto                  pipeline               = benchmarkPipeline(10, 5);
    constexpr std::size_t batchesPerSample       = 1;
    const auto            independent            = measure(samples, batchesPerSample, [&] {
        auto settled = pipeline.settleIndependent(requests, policies);
        settled.ignore("settlement independent batch benchmark sample");
    });
    const auto            independentAllocations = measureAllocations(batchesPerSample, [&] {
        auto settled = pipeline.settleIndependent(requests, policies);
        settled.ignore("settlement independent batch allocation benchmark sample");
    });
    const double          independentP50         = independent[0] / static_cast<double>(batchSize);
    const double          independentP95         = independent[1] / static_cast<double>(batchSize);

    std::cout << "],\"independent\":{\"rules\":10,\"matchPercent\":50,\"batchSize\":" << batchSize
              << ",\"batchesPerSample\":" << batchesPerSample << ",\"independentP50UsPerItem\":" << independentP50
              << ",\"independentP95UsPerItem\":" << independentP95 << ",\"allocationsPerItem\":";
    if (independentAllocations)
        writeOptionalNumber(*independentAllocations / static_cast<double>(batchSize));
    else
        writeOptionalNumber(std::nullopt);
    std::cout << "},\"triggerDepths\":[";
    CHECK(independentP95 >= independentP50);

    constexpr std::array<std::uint32_t, 3> triggerDepths = {1, 4, 8};
    for (std::size_t index = 0; index < triggerDepths.size(); ++index) {
        const auto                                           depth = triggerDepths[index];
        BenchmarkPolicy                                      triggerPolicy(depth);
        eve::settlement::SettlementPipeline::RequestExecutor execute =
            [&](const eve::settlement::SettlementRequest &chained) { return pipeline.settle(chained, triggerPolicy); };
        request.trace          = eve::settlement::SettlementTraceLevel::Off;
        const auto timing      = measure(samples, 5, [&] {
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
    if (peakResidentBytesBefore && peakResidentBytesAfter) CHECK(*peakResidentBytesAfter >= *peakResidentBytesBefore);
    std::cout << "],\"memory\":{\"peakResidentBytesBefore\":";
    writeOptionalInteger(peakResidentBytesBefore);
    std::cout << ",\"peakResidentBytesAfter\":";
    writeOptionalInteger(peakResidentBytesAfter);
    std::cout << "}}" << std::endl;
}
}  // namespace test_settlement_benchmark

// Contract section: stylize mesh vfx benchmark
#include "PrivateTestScope.h"

#include <glm/mat4x4.hpp>
#include <random>
#include "Fixtures.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Texture.h"
#include "stylize/MeshEffect.h"
#include "stylize/MeshEffectRenderer.h"
#include "stylize/MeshVfxBatchSubmission.h"
#include "stylize/MeshVfxRenderBatch.h"
#include "stylize/MeshVfxScalability.h"
#include "stylize/TrailEffect.h"
#include "window/Window.h"
// stylize_mesh_vfx_benchmark.cpp
namespace test_stylize_mesh_vfx_benchmark {
// stylize_mesh_vfx_benchmark.cpp
using namespace eve::stylize;

TEST_CASE("stylize.mesh_vfx_benchmark") {
    if (!std::getenv("EVENGINE_MESH_VFX_BENCHMARK")) return;

    constexpr std::size_t                 instanceCount = 50'000;
    constexpr int                         sampleCount   = 12;
    std::mt19937                          rng(0x4d565846u);
    std::uniform_real_distribution<float> distance(0.0f, 120.0f);
    std::uniform_real_distribution<float> pixels(2.0f, 180.0f);
    std::uniform_int_distribution<int>    priority(-4, 12);

    std::vector<MeshVfxLodCandidate> candidates;
    std::vector<MeshVfxRenderItem>   items;
    candidates.reserve(instanceCount);
    items.reserve(instanceCount);
    for (std::size_t index = 0; index < instanceCount; ++index) {
        const auto  id = static_cast<std::uint64_t>(index + 1);
        const float d  = distance(rng);
        candidates.push_back({id, d, pixels(rng), priority(rng), index % 19 != 0});
        MeshVfxBatchKey key;
        key.pipelineId = index % 3;
        key.materialId = index % 24;
        key.meshId     = index % 64;
        key.renderPass = static_cast<std::uint32_t>(index % 2);
        key.blend      = index % 5 == 0 ? MeshVfxBatchBlend::Alpha : MeshVfxBatchBlend::Additive;
        items.push_back({id, key, d});
    }

    MeshVfxScalabilityPolicy policy;
    policy.workUnitBudget = 20'000;
    MeshVfxScalabilityPlanner lodPlanner(policy);
    MeshVfxRenderBatchPlanner batchPlanner;
    std::vector<double>       samples;
    samples.reserve(sampleCount);
    MeshVfxRenderQueue lastQueue;

    for (int sample = 0; sample < sampleCount; ++sample) {
        const auto start     = std::chrono::steady_clock::now();
        const auto decisions = lodPlanner.plan(candidates);
        lastQueue            = batchPlanner.build(items, decisions, static_cast<std::uint64_t>(sample));
        const auto end       = std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }

    std::sort(samples.begin(), samples.end());
    const double medianMs = samples[samples.size() / 2];
    REQUIRE_EQ(lastQueue.stats.inputItems, static_cast<std::uint32_t>(instanceCount));
    REQUIRE(lastQueue.stats.queuedDraws > 0);
    REQUIRE(lastQueue.stats.culledDraws > 0);
    REQUIRE(lastQueue.stats.dynamicMeshRefreshes <= lastQueue.stats.queuedDraws);
    REQUIRE(lastQueue.batches.size() <= lastQueue.stats.queuedDraws);

    std::cout << "MESH_VFX_BENCHMARK_JSON={\"instances\":" << instanceCount << ",\"samples\":" << sampleCount
              << ",\"medianMs\":" << medianMs << ",\"queuedDraws\":" << lastQueue.stats.queuedDraws
              << ",\"batches\":" << lastQueue.batches.size() << ",\"culledDraws\":" << lastQueue.stats.culledDraws
              << ",\"meshRefreshes\":" << lastQueue.stats.dynamicMeshRefreshes << "}\n";
}

// stylize_mesh_vfx_refraction.cpp
using namespace eve::graphics;
using namespace eve::stylize;

TEST_CASE("stylize.meshVfx.sceneColorRefractionSamplesBoundTexture") {
    eve::window::Window *window   = nullptr;
    Graphics            *graphics = nullptr;
    openGfxWindow(window, graphics, 64, 64);
    REQUIRE(graphics != nullptr);

    const uint8_t greenPixel[4] = {0, 255, 0, 255};
    Texture      *sceneColor    = graphics->newTexture(1, 1, greenPixel);
    Canvas       *target        = graphics->newCanvas(64, 64);
    REQUIRE(sceneColor != nullptr);
    REQUIRE(target != nullptr);

    MeshEffectInstance effect("slash");
    effect.style().setFloat("refractionStrength", 1.f);
    effect.style().setFloat("flowWarp", 0.f);
    effect.style().setFloat("edgeDistortion", 0.f);
    effect.play();
    effect.update(0.1f);

    TrailMeshSnapshot ribbon;
    ribbon.vertices = {
        {{-0.8f, -0.8f, 0.f}, {0.f, 0.f}, 1.f},
        {{-0.8f, 0.8f, 0.f}, {0.f, 1.f}, 1.f},
        {{0.8f, -0.8f, 0.f}, {1.f, 0.f}, 1.f},
        {{0.8f, 0.8f, 0.f}, {1.f, 1.f}, 1.f},
    };
    ribbon.indices = {0, 1, 2, 2, 1, 3};

    MeshEffectRenderer renderer(*graphics);
    graphics->setMesh3DSceneColor(sceneColor);
    graphics->setMesh3DViewProj(glm::mat4(1.f));
    graphics->begin3DFrameToCanvas(target);
    auto submitted = renderer.submitTrail(effect, ribbon);
    REQUIRE(submitted.hasValue());
    REQUIRE(static_cast<int>(submitted.value()) == static_cast<int>(MeshEffectSubmitStatus::Drawn));
    graphics->end3DFrameToCanvas();
    graphics->setMesh3DSceneColor(nullptr);

    const Color center = target->getPixel(32, 32);
    REQUIRE(center.g > 0.05f);
    REQUIRE(center.g > center.r * 2.f);
    REQUIRE(center.g > center.b * 2.f);
    window->close();
}

// stylize_mesh_vfx_scalability.cpp
using namespace eve::stylize;


// stylize_mesh_vfx_submission.cpp
using namespace eve::stylize;

namespace {

class RecordingSink final : public IMeshVfxBatchSink {
public:
    MeshVfxSubmitStatus        beginResult = MeshVfxSubmitStatus::Accepted;
    MeshVfxSubmitStatus        drawResult  = MeshVfxSubmitStatus::Accepted;
    MeshVfxSubmitStatus        endResult   = MeshVfxSubmitStatus::Accepted;
    std::vector<std::uint64_t> draws;
    std::uint32_t              begins = 0;
    std::uint32_t              ends   = 0;

    MeshVfxSubmitStatus beginBatch(const MeshVfxBatchKey &) override {
        ++begins;
        return beginResult;
    }

    MeshVfxSubmitStatus submitDraw(const MeshVfxBatchedDraw &draw) override {
        draws.push_back(draw.stableInstanceId);
        return drawResult;
    }

    MeshVfxSubmitStatus endBatch() override {
        ++ends;
        return endResult;
    }
};

[[maybe_unused]] MeshVfxRenderQueue queueWithTwoDraws() {
    MeshVfxRenderQueue queue;
    MeshVfxRenderBatch batch;
    batch.draws.push_back(MeshVfxBatchedDraw{1, MeshVfxLodTier::Full, MeshVfxMeshUpdate::Refresh, 1});
    batch.draws.push_back(MeshVfxBatchedDraw{2, MeshVfxLodTier::Reduced, MeshVfxMeshUpdate::Reuse, 2});
    queue.batches.push_back(std::move(batch));
    return queue;
}

}  // namespace
}  // namespace test_stylize_mesh_vfx_benchmark
