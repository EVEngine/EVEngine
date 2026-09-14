#include "stylize/MeshVfxBatchSubmission.h"

#include <vector>

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::stylize;

namespace {

class RecordingSink final : public IMeshVfxBatchSink {
public:
    MeshVfxSubmitStatus beginResult = MeshVfxSubmitStatus::Accepted;
    MeshVfxSubmitStatus drawResult = MeshVfxSubmitStatus::Accepted;
    MeshVfxSubmitStatus endResult = MeshVfxSubmitStatus::Accepted;
    std::vector<std::uint64_t> draws;
    std::uint32_t begins = 0;
    std::uint32_t ends = 0;

    MeshVfxSubmitStatus beginBatch(const MeshVfxBatchKey&) override {
        ++begins;
        return beginResult;
    }

    MeshVfxSubmitStatus submitDraw(const MeshVfxBatchedDraw& draw) override {
        draws.push_back(draw.stableInstanceId);
        return drawResult;
    }

    MeshVfxSubmitStatus endBatch() override {
        ++ends;
        return endResult;
    }
};

MeshVfxRenderQueue queueWithTwoDraws() {
    MeshVfxRenderQueue queue;
    MeshVfxRenderBatch batch;
    batch.draws.push_back(MeshVfxBatchedDraw{1, MeshVfxLodTier::Full, MeshVfxMeshUpdate::Refresh, 1});
    batch.draws.push_back(MeshVfxBatchedDraw{2, MeshVfxLodTier::Reduced, MeshVfxMeshUpdate::Reuse, 2});
    queue.batches.push_back(std::move(batch));
    return queue;
}

} // namespace

TEST_CASE("stylize.mesh_vfx_submission executes ordered batches") {
    RecordingSink sink;
    const auto report = MeshVfxBatchExecutor{}.submit(queueWithTwoDraws(), sink);
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
    sink.beginResult = MeshVfxSubmitStatus::Skipped;
    const auto report = MeshVfxBatchExecutor{}.submit(queueWithTwoDraws(), sink);
    REQUIRE_EQ(static_cast<int>(report.status), static_cast<int>(MeshVfxQueueSubmitStatus::Partial));
    REQUIRE_EQ(report.skippedBatches, 1u);
    REQUIRE_EQ(report.skippedDraws, 2u);
    REQUIRE_EQ(sink.ends, 0u);
    REQUIRE(sink.draws.empty());
}

TEST_CASE("stylize.mesh_vfx_submission stops immediately when device is unavailable") {
    RecordingSink sink;
    sink.drawResult = MeshVfxSubmitStatus::DeviceUnavailable;
    const auto report = MeshVfxBatchExecutor{}.submit(queueWithTwoDraws(), sink);
    REQUIRE_EQ(static_cast<int>(report.status), static_cast<int>(MeshVfxQueueSubmitStatus::DeviceUnavailable));
    REQUIRE_EQ(report.acceptedDraws, 0u);
    REQUIRE_EQ(sink.draws.size(), 1u);
    REQUIRE_EQ(sink.ends, 0u);
}
