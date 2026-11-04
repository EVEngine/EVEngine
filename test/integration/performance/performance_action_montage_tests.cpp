// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "performance",
  "module": "animation",
  "modules": [
    "action",
    "animation"
  ],
  "origins": [
    "action_montage_performance.cpp"
  ]
}
*/
// clang-format on

#include "action/Action.h"
#include "action/ActionTimeline.h"
#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"
#include "animation/MontagePlayer.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace {

#if defined(_MSC_VER)
std::size_t montageTrackedAllocations = 0;
bool        montageTrackAllocations   = false;

int __cdecl montageAllocationHook(int allocationType, void*, std::size_t, int, long, const unsigned char*, int) {
    if (montageTrackAllocations && allocationType == _HOOK_ALLOC) ++montageTrackedAllocations;
    return 1;
}
#endif

eve::LogicalId montageId(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

eve::action::ActionTimeline denseTimeline(std::size_t sectionCount) {
    eve::action::ActionTimeline timeline;
    timeline.actionId = montageId("benchmark:prepared-montage");
    timeline.duration = eve::Duration::fromNanoseconds(static_cast<std::int64_t>(sectionCount) * 10'000'000);
    timeline.animationSections.reserve(sectionCount);
    for (std::size_t index = 0; index < sectionCount; ++index) {
        const auto start = eve::Duration::fromNanoseconds(static_cast<std::int64_t>(index) * 10'000'000);
        const auto end   = eve::Duration::fromNanoseconds(static_cast<std::int64_t>(index + 1U) * 10'000'000);
        auto       id    = eve::LogicalId::fromParts("benchmark-section", std::to_string(index));
        REQUIRE(id.has_value());
        timeline.animationSections.push_back(
            {*id, "memory://clips/dense", start, end, eve::Duration::zero()});
    }
    return timeline;
}

std::vector<eve::animation::MontageClipAsset> denseClips() {
    auto clip = std::make_unique<eve::animation::AnimClip>("dense");
    clip->setDuration(1.0f);
    clip->setLoop(false);
    clip->addPositionKey(0, 0.0f, 0.0f, 0.0f, 0.0f);
    clip->addPositionKey(0, 1.0f, 1.0f, 0.0f, 0.0f);
    std::vector<eve::animation::MontageClipAsset> clips;
    clips.push_back({"memory://clips/dense", std::move(clip)});
    return clips;
}

}  // namespace

TEST_CASE("actionMontage.preparedTopologyHandlesDenseSectionSweepsAndJumps") {
    constexpr std::size_t sectionCount = 256;
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    eve::animation::MontagePlayer player(skeleton);
    auto timeline = denseTimeline(sectionCount);
    REQUIRE(player.prepare(timeline, denseClips()).ok());
    const eve::action::ActionExecutionId execution(9001);
    REQUIRE(player.play(execution).ok());

    eve::action::ActionAdvance advance;
    advance.id           = execution;
    advance.phase        = eve::action::ActionPhase::Active;
    advance.totalElapsed = timeline.duration;
    auto swept = player.present(advance, eve::SimulationTick(1));
    REQUIRE(swept.ok());
    CHECK_EQ(swept.value().current, timeline.duration);

    REQUIRE(player.play(execution).ok());
    auto jumped = player.evaluateSectionProgress(execution, 0, 0.5, eve::SimulationTick(2));
    REQUIRE(jumped.ok());
    CHECK_EQ(jumped.value().current, eve::Duration::fromNanoseconds(1'280'000'000));
}

TEST_CASE("actionMontage.rvaluePresentTransfersOwningTimelineProjectionStorage") {
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    eve::animation::MontagePlayer player(skeleton);
    auto timeline = denseTimeline(4);
    REQUIRE(player.prepare(timeline, denseClips()).ok());
    const eve::action::ActionExecutionId execution(9003);
    REQUIRE(player.play(execution).ok());

    eve::action::ActionAdvance advance;
    advance.id           = execution;
    advance.phase        = eve::action::ActionPhase::Active;
    advance.totalElapsed = eve::Duration::fromNanoseconds(5'000'000);
    advance.timelineEvents.resize(2);
    advance.activeBlocks.resize(3);
    const auto* eventStorage = advance.timelineEvents.data();
    const auto* blockStorage = advance.activeBlocks.data();

    auto presented = player.present(std::move(advance), eve::SimulationTick(1));
    REQUIRE(presented.ok());
    CHECK_EQ(presented.value().events.data(), eventStorage);
    CHECK_EQ(presented.value().activeBlocks.data(), blockStorage);
}

TEST_CASE("actionMontage.benchmarkPreparedPresentReportsP50P95AndMeasuredAllocationBoundary") {
    if (!std::getenv("EVENGINE_MONTAGE_BENCHMARK")) return;
    constexpr std::size_t sectionCount = 256;
    constexpr std::size_t sampleCount  = 240;
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    eve::animation::MontagePlayer player(skeleton);
    auto timeline = denseTimeline(sectionCount);
    REQUIRE(player.prepare(timeline, denseClips()).ok());
    const eve::action::ActionExecutionId execution(9002);
    REQUIRE(player.play(execution).ok());

    std::vector<double> microseconds;
    microseconds.reserve(sampleCount);
    for (std::size_t sample = 1; sample <= sampleCount; ++sample) {
        eve::action::ActionAdvance advance;
        advance.id           = execution;
        advance.phase        = eve::action::ActionPhase::Active;
        advance.totalElapsed = eve::Duration::fromNanoseconds(static_cast<std::int64_t>(sample) * 10'000'000);
        const auto started    = std::chrono::steady_clock::now();
        auto       presented  = player.present(advance, eve::SimulationTick(sample));
        const auto stopped    = std::chrono::steady_clock::now();
        REQUIRE(presented.ok());
        microseconds.push_back(std::chrono::duration<double, std::micro>(stopped - started).count());
    }
    std::sort(microseconds.begin(), microseconds.end());
    const double p50 = microseconds[microseconds.size() / 2U];
    const double p95 = microseconds[microseconds.size() * 95U / 100U];

#if defined(_MSC_VER)
    REQUIRE(player.play(execution).ok());
    eve::action::ActionAdvance allocationProbe;
    allocationProbe.id           = execution;
    allocationProbe.phase        = eve::action::ActionPhase::Active;
    allocationProbe.totalElapsed = eve::Duration::fromNanoseconds(5'000'000);
    allocationProbe.timelineEvents.resize(4);
    allocationProbe.activeBlocks.resize(4);
    const auto* eventStorage              = allocationProbe.timelineEvents.data();
    const auto* blockStorage              = allocationProbe.activeBlocks.data();
    montageTrackedAllocations             = 0;
    const _CRT_ALLOC_HOOK previousHook    = _CrtSetAllocHook(&montageAllocationHook);
    montageTrackAllocations               = true;
    auto allocationResult = player.present(std::move(allocationProbe), eve::SimulationTick(sampleCount + 1U));
    montageTrackAllocations               = false;
    _CrtSetAllocHook(previousHook);
    REQUIRE(allocationResult.ok());
    CHECK_EQ(montageTrackedAllocations, std::size_t(0));
    CHECK_EQ(allocationResult.value().events.data(), eventStorage);
    CHECK_EQ(allocationResult.value().activeBlocks.data(), blockStorage);
    const std::size_t measuredAllocations = montageTrackedAllocations;
#else
    // Portable timing runs everywhere; CRT allocation counting is currently available on MSVC only.
#endif

    std::cout << "MONTAGE_BENCHMARK_JSON={\"sections\":" << sectionCount << ",\"samples\":" << sampleCount
              << ",\"p50Us\":" << p50 << ",\"p95Us\":" << p95;
#if defined(_MSC_VER)
    std::cout << ",\"allocationMeasurement\":\"msvc-crt\",\"steadyStateAllocations\":"
              << measuredAllocations;
#else
    std::cout << ",\"allocationMeasurement\":\"unavailable\",\"steadyStateAllocations\":null";
#endif
    std::cout << "}\n";
    CHECK(p50 > 0.0);
    CHECK(p95 >= p50);
}
