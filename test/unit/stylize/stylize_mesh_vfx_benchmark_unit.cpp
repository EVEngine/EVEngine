#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <glm/mat4x4.hpp>
#include <iostream>
#include <random>
#include <vector>
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
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// stylize_mesh_vfx_benchmark.cpp
using namespace eve::stylize;


// stylize_mesh_vfx_refraction.cpp
using namespace eve::graphics;
using namespace eve::stylize;


// stylize_mesh_vfx_scalability.cpp
using namespace eve::stylize;

TEST_CASE("stylize.mesh_vfx_scalability selects deterministic tiers within budget") {
    MeshVfxScalabilityPolicy policy;
    policy.workUnitBudget = 7;
    MeshVfxScalabilityPlanner planner(policy);

    const std::array candidates{
        MeshVfxLodCandidate{30, 8.0f, 120.0f, 0, true},
        MeshVfxLodCandidate{10, 4.0f, 120.0f, 10, true},
        MeshVfxLodCandidate{20, 6.0f, 60.0f, 5, true},
        MeshVfxLodCandidate{40, 10.0f, 20.0f, -1, true},
    };

    const auto decisions = planner.plan(candidates);
    REQUIRE_EQ(decisions.size(), candidates.size());
    REQUIRE_EQ(static_cast<int>(decisions[0].tier), static_cast<int>(MeshVfxLodTier::Minimal));
    REQUIRE_EQ(static_cast<int>(decisions[1].tier), static_cast<int>(MeshVfxLodTier::Full));
    REQUIRE_EQ(static_cast<int>(decisions[2].tier), static_cast<int>(MeshVfxLodTier::Reduced));
    REQUIRE_EQ(static_cast<int>(decisions[3].tier), static_cast<int>(MeshVfxLodTier::Culled));
    REQUIRE_EQ(decisions[2].trailSampleStride, 2u);
    REQUIRE_EQ(decisions[0].meshUpdateInterval, 4u);
    REQUIRE_EQ(decisions[0].workUnits, 1u);
    REQUIRE_EQ(decisions[1].workUnits, 4u);
    REQUIRE_EQ(decisions[2].workUnits, 2u);
    REQUIRE_EQ(decisions[3].workUnits, 0u);
    std::uint32_t usedWorkUnits = 0;
    for (const auto &decision : decisions) {
        usedWorkUnits += decision.workUnits;
    }
    REQUIRE_EQ(usedWorkUnits, policy.workUnitBudget);
}

TEST_CASE("stylize.mesh_vfx_scalability is independent of candidate iteration order") {
    MeshVfxScalabilityPolicy policy;
    policy.workUnitBudget = 4;
    MeshVfxScalabilityPlanner planner(policy);

    const std::array forward{
        MeshVfxLodCandidate{2, 5.0f, 100.0f, 0, true},
        MeshVfxLodCandidate{1, 5.0f, 100.0f, 0, true},
    };
    const std::array reverse{forward[1], forward[0]};
    const auto       a = planner.plan(forward);
    const auto       b = planner.plan(reverse);

    REQUIRE_EQ(static_cast<int>(a[0].tier), static_cast<int>(MeshVfxLodTier::Culled));
    REQUIRE_EQ(static_cast<int>(a[1].tier), static_cast<int>(MeshVfxLodTier::Full));
    REQUIRE_EQ(static_cast<int>(b[0].tier), static_cast<int>(MeshVfxLodTier::Full));
    REQUIRE_EQ(static_cast<int>(b[1].tier), static_cast<int>(MeshVfxLodTier::Culled));
}

TEST_CASE("stylize.mesh_vfx_scalability culls invalid and out-of-range candidates") {
    MeshVfxScalabilityPlanner planner;
    const std::array          candidates{
        MeshVfxLodCandidate{1, 1.0f, 100.0f, 0, false},
        MeshVfxLodCandidate{2, 81.0f, 100.0f, 0, true},
        MeshVfxLodCandidate{3, -1.0f, 100.0f, 0, true},
    };
    const auto decisions = planner.plan(candidates);
    for (const auto &decision : decisions) {
        REQUIRE_EQ(static_cast<int>(decision.tier), static_cast<int>(MeshVfxLodTier::Culled));
        REQUIRE_EQ(decision.workUnits, 0u);
    }
}

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

TEST_CASE("stylize.mesh_vfx_submission executes ordered batches") {
    RecordingSink sink;
    const auto    report = MeshVfxBatchExecutor{}.submit(queueWithTwoDraws(), sink);
    REQUIRE_EQ(static_cast<int>(report.status), static_cast<int>(MeshVfxQueueSubmitStatus::Complete));
    REQUIRE_EQ(report.acceptedBatches, 1u);
    REQUIRE_EQ(report.acceptedDraws, 2u);
    REQUIRE_EQ(sink.begins, 1u);
    REQUIRE_EQ(sink.ends, 1u);
    REQUIRE_EQ(sink.draws[0], 1u);
    REQUIRE_EQ(sink.draws[1], 2u);
}

TEST_CASE("stylize.mesh_vfx_submission skips unsupported batches atomically") {
    RecordingSink sink;
    sink.beginResult  = MeshVfxSubmitStatus::Skipped;
    const auto report = MeshVfxBatchExecutor{}.submit(queueWithTwoDraws(), sink);
    REQUIRE_EQ(static_cast<int>(report.status), static_cast<int>(MeshVfxQueueSubmitStatus::Partial));
    REQUIRE_EQ(report.skippedBatches, 1u);
    REQUIRE_EQ(report.skippedDraws, 2u);
    REQUIRE_EQ(sink.ends, 0u);
    REQUIRE(sink.draws.empty());
}

TEST_CASE("stylize.mesh_vfx_submission stops immediately when device is unavailable") {
    RecordingSink sink;
    sink.drawResult   = MeshVfxSubmitStatus::DeviceUnavailable;
    const auto report = MeshVfxBatchExecutor{}.submit(queueWithTwoDraws(), sink);
    REQUIRE_EQ(static_cast<int>(report.status), static_cast<int>(MeshVfxQueueSubmitStatus::DeviceUnavailable));
    REQUIRE_EQ(report.acceptedDraws, 0u);
    REQUIRE_EQ(sink.draws.size(), 1u);
    REQUIRE_EQ(sink.ends, 0u);
}
