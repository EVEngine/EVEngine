#include <future>
#include <memory>
#include <type_traits>
#include "procgen/RuntimeGeneration.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;

static_assert(std::is_copy_constructible_v<ProcgenGenerationJob>);
static_assert(!std::is_copy_constructible_v<RuntimeGeneration>);
static_assert(!std::is_move_constructible_v<RuntimeGeneration>);

TEST_CASE("procgen.worker.rejectsJobsAfterClearAndOwnerDestruction") {
    ProcgenGenerationJob job;
    {
        RuntimeGeneration original(42);
        REQUIRE(original.addLevel(10.f, 4.f, 2.f) == 0);
        original.updateSource(5.f, 5.f, 1.f, 0.f);
        auto issued = original.nextGenerationJob();
        REQUIRE(issued.ok());
        auto value = std::move(issued).takeValue();
        REQUIRE(value.has_value());
        job = *value;
        original.clear();
        auto stale = original.completeGenerationJob({job, PointSet{}});
        CHECK(!stale.ok());
    }
    RuntimeGeneration replacement(42);
    REQUIRE(replacement.addLevel(10.f, 4.f, 2.f) == 0);
    replacement.updateSource(5.f, 5.f, 1.f, 0.f);
    auto issued = replacement.nextGenerationJob();
    REQUIRE(issued.ok());
    REQUIRE(std::move(issued).takeValue().has_value());
    auto stale = replacement.completeGenerationJob({job, PointSet{}});
    CHECK(!stale.ok());
    CHECK(replacement.getGeneratingCount() == 1);
}

TEST_CASE("procgen.worker.rejectsForeignAndReplayedJobs") {
    RuntimeGeneration first(42), second(42);
    for (auto* runtime : {&first, &second}) {
        REQUIRE(runtime->addLevel(10.f, 4.f, 2.f) == 0);
        runtime->updateSource(5.f, 5.f, 1.f, 0.f);
    }
    auto a = first.nextGenerationJob();
    auto b = second.nextGenerationJob();
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    auto job   = std::move(a).takeValue();
    auto other = std::move(b).takeValue();
    REQUIRE(job.has_value());
    REQUIRE(other.has_value());
    REQUIRE(job->getTicket() == other->getTicket());
    auto foreign = second.completeGenerationJob({*job, PointSet{}});
    CHECK(!foreign.ok());
    CHECK(second.getGeneratingCount() == 1);
    auto accepted = first.completeGenerationJob({*job, PointSet{}});
    REQUIRE(accepted.ok());
    auto replay = first.completeGenerationJob({*job, PointSet{}});
    CHECK(!replay.ok());
    auto own = second.completeGenerationJob({*other, PointSet{}});
    CHECK(own.ok());
}

TEST_CASE("procgen.worker.legacyIssueRejectsWrongThread") {
    RuntimeGeneration runtime(42);
    REQUIRE(runtime.addLevel(10.f, 4.f, 2.f) == 0);
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);
    auto worker = std::async(std::launch::async, [&runtime] {
        std::unique_ptr<ProcgenCellRequest> request(runtime.nextGenerate());
        auto                                checked = runtime.nextGenerationJob();
        return !request && !checked.ok();
    });
    CHECK(worker.get());
    CHECK(runtime.getGeneratingCount() == 0);
    auto issued = runtime.nextGenerationJob();
    REQUIRE(issued.ok());
    CHECK(std::move(issued).takeValue().has_value());
}

TEST_CASE("procgen.runtimeGeneration.movesValueJobsAcrossWorkerBoundary") {
    RuntimeGeneration runtime(4201);
    REQUIRE(runtime.isOwnerThread());
    REQUIRE_EQ(runtime.addLevel(10.f, 4.f, 2.f), 0);
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);

    auto issued = runtime.nextGenerationJob();
    REQUIRE(issued.ok());
    auto optionalJob = std::move(issued).takeValue();
    REQUIRE(optionalJob.has_value());
    ProcgenGenerationJob job   = *optionalJob;
    const int            level = job.getLevel();
    const int            x     = job.getX();
    const int            z     = job.getZ();

    auto worker    = std::async(std::launch::async, [job] {
        PointSet output;
        output.add(job.getMinX(), 0.f, job.getMinZ());
        return ProcgenGenerationCompletion(job, std::move(output));
    });
    auto committed = runtime.completeGenerationJob(worker.get());
    REQUIRE(committed.ok());
    const auto revision = std::move(committed).takeValue();
    CHECK_EQ(revision, uint64_t(1));
    CHECK(runtime.hasCell(level, x, z));
}

TEST_CASE("procgen.runtimeGeneration.rejectsWorkerThreadSchedulerMutation") {
    RuntimeGeneration runtime(4202);
    REQUIRE_EQ(runtime.addLevel(10.f, 4.f, 2.f), 0);
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);
    auto issued = runtime.nextGenerationJob();
    REQUIRE(issued.ok());
    auto optionalJob = std::move(issued).takeValue();
    REQUIRE(optionalJob.has_value());
    const ProcgenGenerationJob job = *optionalJob;

    auto workerAccepted = std::async(std::launch::async, [&runtime, job] {
        PointSet output;
        output.add(0.f, 0.f, 0.f);
        auto result = runtime.completeGenerationJob(ProcgenGenerationCompletion(job, std::move(output)));
        return result.ok();
    });
    CHECK(!workerAccepted.get());
    CHECK_EQ(runtime.getGeneratingCount(), 1);

    PointSet output;
    output.add(0.f, 0.f, 0.f);
    auto committed = runtime.completeGenerationJob(ProcgenGenerationCompletion(job, std::move(output)));
    REQUIRE(committed.ok());
}
