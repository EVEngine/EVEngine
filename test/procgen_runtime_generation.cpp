#include "procgen/Procgen.h"
#include "procgen/RuntimeGeneration.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <memory>
#include <utility>

using namespace eve::procgen;

namespace {

/** @brief Owns a module-backed runtime-generation handle for one test. */
class RuntimeLease {
public:
    RuntimeLease() = default;
    RuntimeLease(Procgen& owner, ProcgenRuntimeGenerationHandleRef handle) : owner_(&owner), handle_(handle) {}

    RuntimeLease(const RuntimeLease&)            = delete;
    RuntimeLease& operator=(const RuntimeLease&) = delete;
    RuntimeLease(RuntimeLease&& other) noexcept : owner_(other.owner_), handle_(other.handle_) {
        other.owner_  = nullptr;
        other.handle_ = {};
    }
    RuntimeLease& operator=(RuntimeLease&& other) noexcept {
        if (this == &other) return *this;
        reset();
        owner_        = other.owner_;
        handle_       = other.handle_;
        other.owner_  = nullptr;
        other.handle_ = {};
        return *this;
    }
    ~RuntimeLease() { reset(); }

    /** @brief Releases the scheduler handle and explicitly observes the Result. */
    void reset() noexcept {
        if (!owner_ || !handle_.isValid()) return;
        auto result = owner_->release(handle_);
        result.ignore("test runtime-generation cleanup");
        owner_  = nullptr;
        handle_ = {};
    }

    /** @brief Provides a short-lived borrowed view of the scheduler. */
    [[nodiscard]] eve::script::Borrowed<RuntimeGeneration> view() const noexcept {
        return owner_ ? owner_->resolveRuntimeGeneration(handle_) : eve::script::Borrowed<RuntimeGeneration>();
    }

    /** @brief Preserves the concise `runtime->operation()` test syntax. */
    [[nodiscard]] eve::script::Borrowed<RuntimeGeneration> operator->() const noexcept { return view(); }

private:
    Procgen*                          owner_ = nullptr;
    ProcgenRuntimeGenerationHandleRef handle_{};
};

/** @brief Converts a checked scheduler allocation Result into a test lease. */
RuntimeLease requireRuntime(Procgen& proc, uint32_t worldSeed) {
    auto       result = proc.newRuntimeGenerationHandle(worldSeed);
    const bool ok     = result.ok();
    if (!ok) {
        const eve::Diagnostic* diagnostic = result.error();
        REQUIRE(diagnostic != nullptr);
    }
    REQUIRE(ok);
    return RuntimeLease(proc, std::move(result).takeValue());
}

/** @brief Gives an owning lifetime to a request allocated by the scheduler API. */
using RequestLease = std::unique_ptr<ProcgenCellRequest>;

[[nodiscard]] RequestLease ownRequest(ProcgenCellRequest* request) noexcept { return RequestLease(request); }

}  // namespace

TEST_CASE("procgen.runtimeGeneration.partitionsAndPublishesCells") {
    Procgen   proc;
    auto      runtime   = requireRuntime(proc, 42);
    const int nearLevel = runtime->addLevel(10.f, 8.f, 1.5f);
    const int farLevel  = runtime->addLevel(40.f, 30.f, 2.f);
    CHECK_EQ(nearLevel, 0);
    CHECK_EQ(farLevel, 1);
    CHECK_EQ(runtime->getLevelCount(), 2);
    CHECK_EQ(runtime->getLevelCleanupRadius(0), 12.f);
    runtime->setMaxGenerating(1);
    runtime->setFrameTimeBudget(2.f);
    CHECK_EQ(runtime->getFrameTimeBudget(), 2.f);
    runtime->updateSource(5.f, 5.f, 1.f, 0.f);
    CHECK(runtime->getPendingGenerateCount() > 0);
    runtime->beginFrame();

    auto request = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(request));
    CHECK_EQ(runtime->getGeneratingCount(), 1);
    CHECK(!runtime->nextGenerate());
    CHECK_NE(request->getSeed(), uint32_t(0));
    CHECK(request->getMaxX() > request->getMinX());

    const int level = request->getLevel();
    const int x     = request->getX();
    const int z     = request->getZ();
    PointSet output;
    output.add(request->getMinX(), 0.f, request->getMinZ());
    CHECK(runtime->completeGeneration(request.get(), &output));
    CHECK(runtime->hasCell(level, x, z));
    CHECK_EQ(runtime->getCellRevision(level, x, z), uint64_t(1));
    auto stored = std::unique_ptr<PointSet>(runtime->getCellOutput(level, x, z));
    REQUIRE(bool(stored));
    CHECK_EQ(stored->getCount(), 1);
}

TEST_CASE("procgen.runtimeGeneration.cleansBeyondHysteresisRadius") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 7);
    runtime->addLevel(10.f, 6.f, 2.f);
    runtime->updateSource(5.f, 5.f, 0.f, 1.f);
    auto generated = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(generated));
    const int cellX = generated->getX();
    const int cellZ = generated->getZ();
    PointSet points;
    points.add(5.f, 0.f, 5.f);
    CHECK(runtime->completeGeneration(generated.get(), &points));

    runtime->updateSource(100.f, 100.f, 0.f, 1.f);
    CHECK(runtime->getPendingCleanupCount() > 0);
    bool cleanedTarget = false;
    while (auto cleanup = ownRequest(runtime->nextCleanup())) {
        if (cleanup->getX() == cellX && cleanup->getZ() == cellZ) cleanedTarget = true;
        CHECK(runtime->completeCleanup(cleanup.get()));
    }
    CHECK(cleanedTarget);
    CHECK(!runtime->hasCell(0, cellX, cellZ));
}

TEST_CASE("procgen.runtimeGeneration.failedWorkCanRetryWithStableSeed") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 99);
    runtime->addLevel(8.f, 5.f, 1.25f);
    runtime->updateSource(4.f, 4.f, 1.f, 0.f);
    auto first = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(first));
    const uint32_t seed = first->getSeed();
    CHECK(runtime->failGeneration(first.get()));

    auto retry = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(retry));
    CHECK_EQ(retry->getSeed(), seed);
    CHECK_EQ(retry->getLevel(), 0);
}

TEST_CASE("procgen.runtimeGeneration.unionsNamedSourcesAndCleansAfterLastSource") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 123);
    runtime->addLevel(10.f, 16.f, 1.5f);
    runtime->setMaxGenerating(100);
    CHECK(runtime->setGenerationSource("player", 5.f, 5.f, 1.f, 0.f, 1.f));
    CHECK(runtime->setGenerationSource("quest", 105.f, 5.f, 0.f, 0.f, 1.f));
    CHECK_EQ(runtime->getGenerationSourceCount(), 2);
    CHECK_EQ(runtime->getGenerationSourceId(0), std::string("player"));
    CHECK_EQ(runtime->getGenerationSourceId(1), std::string("quest"));

    bool generatedNearPlayer = false;
    bool generatedNearQuest  = false;
    PointSet empty;
    while (auto request = ownRequest(runtime->nextGenerate())) {
        if (request->getX() >= -1 && request->getX() <= 1) generatedNearPlayer = true;
        if (request->getX() >= 9 && request->getX() <= 11) generatedNearQuest = true;
        CHECK(runtime->completeGeneration(request.get(), &empty));
    }
    CHECK(generatedNearPlayer);
    CHECK(generatedNearQuest);

    CHECK(runtime->removeGenerationSource("player"));
    bool cleanupNearPlayer = false;
    bool cleanupNearQuest  = false;
    while (auto request = ownRequest(runtime->nextCleanup())) {
        if (request->getX() >= -1 && request->getX() <= 1) cleanupNearPlayer = true;
        if (request->getX() >= 9 && request->getX() <= 11) cleanupNearQuest = true;
        CHECK(runtime->completeCleanup(request.get()));
    }
    CHECK(cleanupNearPlayer);
    CHECK(!cleanupNearQuest);
}

TEST_CASE("procgen.runtimeGeneration.frustumCullsFarBehindButKeepsNearCells") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 9);
    runtime->addLevel(10.f, 35.f, 1.25f);
    runtime->setMaxGenerating(100);
    runtime->setFrustumCulling(true, 45.f, 11.f);
    CHECK(runtime->isFrustumCullingEnabled());
    CHECK_EQ(runtime->getFrustumHalfAngle(), 45.f);
    CHECK_EQ(runtime->getFrustumBehindRadius(), 11.f);
    CHECK(runtime->setGenerationSource("camera", 5.f, 5.f, 1.f, 0.f, 1.f));

    bool foundFarBehind = false;
    bool foundNearBehind = false;
    bool foundForward = false;
    PointSet empty;
    while (auto request = ownRequest(runtime->nextGenerate())) {
        const float centerX = (request->getMinX() + request->getMaxX()) * 0.5f;
        if (centerX < -6.f) foundFarBehind = true;
        if (centerX == -5.f) foundNearBehind = true;
        if (centerX > 5.f) foundForward = true;
        CHECK(runtime->completeGeneration(request.get(), &empty));
    }
    CHECK(!foundFarBehind);
    CHECK(foundNearBehind);
    CHECK(foundForward);
}

TEST_CASE("procgen.runtimeGeneration.rejectsStaleAsyncGenerationTickets") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 77);
    runtime->addLevel(10.f, 6.f, 1.5f);
    runtime->updateSource(5.f, 5.f, 1.f, 0.f);
    auto stale = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(stale));
    const uint64_t staleTicket = stale->getTicket();
    CHECK(runtime->isRequestCurrent(stale.get()));

    runtime->updateSource(100.f, 100.f, 1.f, 0.f);
    CHECK(!runtime->isRequestCurrent(stale.get()));
    CHECK_EQ(runtime->getCancelledGenerationCount(), 1);
    runtime->updateSource(5.f, 5.f, 1.f, 0.f);
    auto current = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(current));
    CHECK(runtime->isRequestCurrent(current.get()));
    CHECK_NE(current->getTicket(), staleTicket);
    PointSet output;
    CHECK(!runtime->completeGeneration(stale.get(), &output));
    CHECK(runtime->completeGeneration(current.get(), &output));
    CHECK(!runtime->isRequestCurrent(current.get()));
    CHECK(runtime->debugReport().find("cancelledGeneration=1") != std::string::npos);
}

TEST_CASE("procgen.runtimeGeneration.rejectsStaleAsyncCleanupTickets") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 78);
    runtime->addLevel(10.f, 6.f, 1.5f);
    runtime->updateSource(5.f, 5.f, 1.f, 0.f);
    auto generated = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(generated));
    PointSet output;
    CHECK(runtime->completeGeneration(generated.get(), &output));

    runtime->updateSource(100.f, 100.f, 1.f, 0.f);
    auto stale = ownRequest(runtime->nextCleanup());
    REQUIRE(bool(stale));
    runtime->updateSource(5.f, 5.f, 1.f, 0.f);
    runtime->updateSource(100.f, 100.f, 1.f, 0.f);
    std::vector<const ProcgenCellRequest*> staleRequests{stale.get()};
    auto                                   staleResult = runtime->completeCleanupsAtomic(staleRequests);
    CHECK(!staleResult.ok());
    CHECK(!runtime->completeCleanup(stale.get()));
    auto current = ownRequest(runtime->nextCleanup());
    REQUIRE(bool(current));
    CHECK_NE(current->getTicket(), stale->getTicket());
    CHECK(runtime->completeCleanup(current.get()));
}

TEST_CASE("procgen.runtimeGeneration.completesCleanupTicketsAtomically") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 79);
    runtime->addLevel(10.f, 16.f, 1.5f);
    runtime->setMaxGenerating(8);
    runtime->updateSource(5.f, 5.f, 1.f, 0.f);

    auto firstGenerated  = ownRequest(runtime->nextGenerate());
    auto secondGenerated = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(firstGenerated));
    REQUIRE(bool(secondGenerated));
    PointSet output;
    REQUIRE(runtime->completeGeneration(firstGenerated.get(), &output));
    REQUIRE(runtime->completeGeneration(secondGenerated.get(), &output));
    runtime->updateSource(1000.f, 1000.f, 1.f, 0.f);

    auto firstCleanup  = ownRequest(runtime->nextCleanup());
    auto secondCleanup = ownRequest(runtime->nextCleanup());
    REQUIRE(bool(firstCleanup));
    REQUIRE(bool(secondCleanup));
    std::vector<const ProcgenCellRequest*> empty;
    auto                                   emptyResult = runtime->completeCleanupsAtomic(empty);
    REQUIRE(!emptyResult.ok());
    CHECK(runtime->isRequestCurrent(firstCleanup.get()));
    CHECK(runtime->isRequestCurrent(secondCleanup.get()));
    std::vector<const ProcgenCellRequest*> duplicate{firstCleanup.get(), firstCleanup.get()};
    auto                                   duplicateResult = runtime->completeCleanupsAtomic(duplicate);
    REQUIRE(!duplicateResult.ok());
    CHECK(runtime->isRequestCurrent(firstCleanup.get()));
    CHECK(runtime->isRequestCurrent(secondCleanup.get()));

    auto otherRuntime = requireRuntime(proc, 80);
    otherRuntime->addLevel(10.f, 16.f, 1.5f);
    std::vector<const ProcgenCellRequest*> wrongOwner{firstCleanup.get()};
    auto                                   wrongOwnerResult = otherRuntime->completeCleanupsAtomic(wrongOwner);
    REQUIRE(!wrongOwnerResult.ok());
    CHECK(runtime->isRequestCurrent(firstCleanup.get()));

    std::vector<const ProcgenCellRequest*> requests{firstCleanup.get(), secondCleanup.get()};
    auto                                   completed = runtime->completeCleanupsAtomic(requests);
    REQUIRE(completed.ok());
    CHECK_EQ(completed.value(), uint64_t(2));
    CHECK(!runtime->isRequestCurrent(firstCleanup.get()));
    CHECK(!runtime->isRequestCurrent(secondCleanup.get()));
}

TEST_CASE("procgen.runtimeGeneration.squirrelCompletesCleanupTicketsAtomically") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local procgen = eve.Procgen();
        local runtimeResult = procgen.newRuntimeGeneration(81);
        local pointsResult = procgen.sampleGrid(1, 1, 1.0, 18, 0.0);
        if (runtimeResult.ok && pointsResult.ok) {
            local runtime = runtimeResult.value;
            local points = pointsResult.value;
            runtime.addLevel(10.0, 16.0, 1.5);
            runtime.setMaxGenerating(8);
            runtime.updateSource(5.0, 5.0, 1.0, 0.0);
            local first = runtime.nextGenerate();
            local second = runtime.nextGenerate();
            if (first != null && second != null && runtime.completeGeneration(first, points) &&
                runtime.completeGeneration(second, points)) {
                runtime.updateSource(1000.0, 1000.0, 1.0, 0.0);
                local firstCleanup = runtime.nextCleanup();
                local secondCleanup = runtime.nextCleanup();
                local completed = runtime.completeCleanupsAtomic([firstCleanup, secondCleanup]);
                if (completed.ok && completed.value == "2" &&
                    !runtime.isRequestCurrent(firstCleanup) && !runtime.isRequestCurrent(secondCleanup))
                    result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("procgen.runtimeGeneration.enforcesResidentCellReservations") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 91);
    runtime->addLevel(10.f, 30.f, 1.5f);
    runtime->setMaxGenerating(100);
    runtime->setMaxActiveCells(2);
    CHECK_EQ(runtime->getMaxActiveCells(), 2);
    runtime->updateSource(5.f, 5.f, 1.f, 0.f);

    auto first  = ownRequest(runtime->nextGenerate());
    auto second = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(first));
    REQUIRE(bool(second));
    CHECK(!runtime->nextGenerate());
    CHECK_EQ(runtime->getGeneratingCount(), 2);

    PointSet empty;
    CHECK(runtime->completeGeneration(first.get(), &empty));
    CHECK(runtime->completeGeneration(second.get(), &empty));
    CHECK_EQ(runtime->getActiveCellCount(), 2);
    CHECK(!runtime->nextGenerate());
    CHECK(runtime->debugReport().find("maxActive=2") != std::string::npos);

    runtime->setMaxActiveCells(0);
    CHECK_EQ(runtime->getMaxActiveCells(), 0);
    auto unlimited = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(unlimited));
}

TEST_CASE("procgen.runtimeGeneration.enforcesPointMemoryBudgets") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 92);
    runtime->addLevel(10.f, 25.f, 1.5f);
    runtime->setMaxGenerating(100);
    runtime->setMaxPointsPerCell(2);
    runtime->setMaxResidentPoints(2);
    CHECK_EQ(runtime->getMaxPointsPerCell(), 2);
    CHECK_EQ(runtime->getMaxResidentPoints(), 2);
    runtime->updateSource(5.f, 5.f, 1.f, 0.f);

    auto first = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(first));
    PointSet oversized;
    oversized.add(0.f, 0.f, 0.f);
    oversized.add(1.f, 0.f, 0.f);
    oversized.add(2.f, 0.f, 0.f);
    CHECK(!runtime->completeGeneration(first.get(), &oversized));
    CHECK_EQ(runtime->getRejectedOutputCount(), 1);
    CHECK(runtime->failGeneration(first.get()));

    auto retry = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(retry));
    PointSet accepted;
    accepted.add(0.f, 0.f, 0.f);
    accepted.add(1.f, 0.f, 0.f);
    CHECK(runtime->completeGeneration(retry.get(), &accepted));
    CHECK_EQ(runtime->getResidentPointCount(), 2);

    auto second = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(second));
    PointSet one;
    one.add(0.f, 0.f, 0.f);
    CHECK(!runtime->completeGeneration(second.get(), &one));
    CHECK_EQ(runtime->getRejectedOutputCount(), 2);
    CHECK_EQ(runtime->getPendingCleanupCount(), 1);
    runtime->refreshGenerationSources();
    CHECK(!runtime->completeGeneration(second.get(), &one));
    CHECK_EQ(runtime->getRejectedOutputCount(), 3);
    CHECK_EQ(runtime->getPendingCleanupCount(), 1);
    auto cleanup = ownRequest(runtime->nextCleanup());
    REQUIRE(bool(cleanup));
    CHECK(runtime->completeCleanup(cleanup.get()));
    CHECK_EQ(runtime->getResidentPointCount(), 0);
    CHECK(runtime->completeGeneration(second.get(), &one));
    CHECK_EQ(runtime->getResidentPointCount(), 1);
    CHECK(runtime->debugReport().find("residentPoints=1") != std::string::npos);
}

TEST_CASE("procgen.runtimeGeneration.appliesIdentityDeltaAtomically") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 930);
    runtime->addLevel(10.f, 6.f, 1.5f);
    runtime->updateSource(5.f, 5.f, 1.f, 0.f);
    auto generated = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(generated));
    const int level = generated->getLevel();
    const int x     = generated->getX();
    const int z     = generated->getZ();

    PointSet initial = sampleGridPoints(2, 2, 1.f, 11, 0.f);
    CHECK(runtime->completeGeneration(generated.get(), &initial));
    PointSet updated;
    updated.appendPointFrom(initial, 2).expect("runtime delta reordered point");
    updated.appendPointFrom(initial, 0).expect("runtime delta updated point");
    updated.setPosition(1, 7.f, 2.f, -3.f);
    ProcgenPoint added;
    added.id   = derivePointId(991, 1);
    added.seed = 88;
    const int addedIndex = updated.appendPoint(added);
    (void)addedIndex;

    auto committed = runtime->applyCellUpdate(level, x, z, 1, updated);
    REQUIRE(committed.ok());
    CHECK_EQ(committed.value(), std::uint64_t(2));
    CHECK_EQ(runtime->getCellRevision(level, x, z), std::uint64_t(2));
    auto delta = std::unique_ptr<PointDelta>(runtime->getCellDelta(level, x, z));
    REQUIRE(bool(delta));
    CHECK_EQ(delta->added.getCount(), 1);
    CHECK_EQ(delta->updated.getCount(), 1);
    CHECK_EQ(delta->removed.size(), std::size_t(2));
    auto stored = std::unique_ptr<PointSet>(runtime->getCellOutput(level, x, z));
    REQUIRE(bool(stored));
    auto expectedFingerprint = fingerprintPointSet(updated);
    auto actualFingerprint   = fingerprintPointSet(*stored);
    REQUIRE(expectedFingerprint.ok());
    REQUIRE(actualFingerprint.ok());
    CHECK_EQ(actualFingerprint.value(), expectedFingerprint.value());
}

TEST_CASE("procgen.runtimeGeneration.rejectsStaleAndOverBudgetDeltaWithoutMutation") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 931);
    runtime->addLevel(10.f, 6.f, 1.5f);
    runtime->setMaxPointsPerCell(2);
    runtime->updateSource(5.f, 5.f, 1.f, 0.f);
    auto generated = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(generated));
    const int level = generated->getLevel();
    const int x     = generated->getX();
    const int z     = generated->getZ();
    PointSet initial = sampleGridPoints(2, 1, 1.f, 12, 0.f);
    CHECK(runtime->completeGeneration(generated.get(), &initial));

    PointSet changed = initial;
    changed.setPosition(0, 4.f, 0.f, 0.f);
    auto stale = runtime->applyCellUpdate(level, x, z, 9, changed);
    CHECK(!stale.ok());
    CHECK_EQ(runtime->getCellRevision(level, x, z), std::uint64_t(1));

    ProcgenPoint extra;
    extra.id = derivePointId(932, 1);
    const int extraIndex = changed.appendPoint(extra);
    (void)extraIndex;
    auto overBudget = runtime->applyCellUpdate(level, x, z, 1, changed);
    CHECK(!overBudget.ok());
    CHECK_EQ(runtime->getCellRevision(level, x, z), std::uint64_t(1));
    CHECK(!runtime->getCellDelta(level, x, z));
    auto stored = std::unique_ptr<PointSet>(runtime->getCellOutput(level, x, z));
    REQUIRE(bool(stored));
    CHECK_EQ(stored->getX(0), initial.getX(0));
}

TEST_CASE("procgen.runtimeGeneration.migratesLegacyCellIdsBeforeIncrementalUpdates") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 933);
    runtime->addLevel(10.f, 6.f, 1.5f);
    runtime->updateSource(5.f, 5.f, 1.f, 0.f);
    auto generated = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(generated));
    const int level = generated->getLevel();
    const int x     = generated->getX();
    const int z     = generated->getZ();
    PointSet legacy;
    legacy.add(1.f, 0.f, 2.f);
    legacy.add(3.f, 0.f, 4.f);
    CHECK(runtime->completeGeneration(generated.get(), &legacy));
    auto rejected = runtime->applyCellUpdate(level, x, z, 1, legacy);
    CHECK(!rejected.ok());

    auto migrated = runtime->migrateCellPointIds(level, x, z, 1);
    REQUIRE(migrated.ok());
    CHECK_EQ(migrated.value(), std::uint64_t(2));
    auto stored = std::unique_ptr<PointSet>(runtime->getCellOutput(level, x, z));
    REQUIRE(bool(stored));
    CHECK_NE(stored->getPointId(0), std::uint64_t(0));
    CHECK_NE(stored->getPointId(1), std::uint64_t(0));
    CHECK_NE(stored->getPointId(0), stored->getPointId(1));
}

TEST_CASE("procgen.runtimeGeneration.squirrelAppliesAndInspectsCellDelta") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local procgen = eve.Procgen();
        local runtimeResult = procgen.newRuntimeGeneration(934);
        local pointsResult = procgen.sampleGrid(2, 1, 1.0, 17, 0.0);
        if (runtimeResult.ok && pointsResult.ok) {
            local runtime = runtimeResult.value;
            local points = pointsResult.value;
            runtime.addLevel(10.0, 6.0, 1.5);
            runtime.updateSource(5.0, 5.0, 1.0, 0.0);
            local request = runtime.nextGenerate();
            if (request != null && runtime.completeGeneration(request, points)) {
                points.setPosition(0, 9.0, 2.0, -3.0);
                local update = runtime.applyCellUpdate(
                    request.getLevel(), request.getX(), request.getZ(), "1", points);
                local delta = runtime.getCellDelta(request.getLevel(), request.getX(), request.getZ());
                if (update.ok && update.value == "2" && delta != null &&
                    delta.getAddedCount() == 0 && delta.getUpdatedCount() == 1 &&
                    delta.getRemovedCount() == 0 && delta.getTargetCount() == 2 &&
                    delta.getBaseFingerprint() != delta.getTargetFingerprint() &&
                    delta.getUpdated().getPointId(0) == points.getPointId(0))
                    result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("procgen.runtimeGeneration.breaksEqualPriorityTiesDeterministically") {
    Procgen proc;
    auto    first  = requireRuntime(proc, 17);
    auto    second = requireRuntime(proc, 17);
    for (RuntimeLease* runtime : {&first, &second}) {
        (*runtime)->addLevel(10.f, 25.f, 1.5f);
        (*runtime)->setMaxGenerating(100);
        (*runtime)->updateSource(5.f, 5.f, 0.f, 0.f);
    }
    for (int index = 0; index < 12; ++index) {
        auto a = ownRequest(first->nextGenerate());
        auto b = ownRequest(second->nextGenerate());
        REQUIRE(bool(a));
        REQUIRE(bool(b));
        CHECK_EQ(a->getLevel(), b->getLevel());
        CHECK_EQ(a->getX(), b->getX());
        CHECK_EQ(a->getZ(), b->getZ());
    }
}

TEST_CASE("procgen.runtimeGeneration.issuesLargeQueuesWithConstantTimeCounters") {
    RuntimeGeneration runtime(1701);
    runtime.addLevel(1.f, 64.f, 1.25f);
    runtime.setMaxGenerating(20000);
    runtime.updateSource(0.5f, 0.5f, 0.f, 0.f);
    const int pending = runtime.getPendingGenerateCount();
    REQUIRE(pending > 12000);

    std::vector<std::unique_ptr<ProcgenCellRequest>> requests;
    requests.reserve(size_t(pending));
    while (auto request = ownRequest(runtime.nextGenerate())) requests.push_back(std::move(request));
    CHECK_EQ(int(requests.size()), pending);
    CHECK_EQ(runtime.getPendingGenerateCount(), 0);
    CHECK_EQ(runtime.getGeneratingCount(), pending);

    PointSet empty;
    for (auto& request : requests) CHECK(runtime.completeGeneration(request.get(), &empty));
    CHECK_EQ(runtime.getGeneratingCount(), 0);
    CHECK_EQ(runtime.getActiveCellCount(), pending);

    runtime.updateSource(10000.f, 10000.f, 0.f, 0.f);
    CHECK_EQ(runtime.getActiveCellCount(), 0);
    CHECK_EQ(runtime.getPendingCleanupCount(), pending);
}

TEST_CASE("procgen.runtimeGeneration.stagesBudgetedRefreshUntilAtomicCommit") {
    RuntimeGeneration runtime(1702);
    runtime.addLevel(10.f, 25.f, 1.5f);
    runtime.setRefreshWorkBudget(3);
    CHECK_EQ(runtime.getRefreshWorkBudget(), 3);
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);
    CHECK(runtime.isRefreshPending());
    CHECK_EQ(runtime.getCommittedRefreshRevision(), uint64_t(0));
    CHECK_EQ(runtime.getPendingGenerateCount(), 0);

    uint64_t processed = 0;
    while (runtime.isRefreshPending()) {
        auto advanced = runtime.continueGenerationRefresh();
        REQUIRE(advanced.ok());
        CHECK(advanced.value() <= uint64_t(3));
        processed += advanced.value();
    }
    CHECK(processed > 3);
    CHECK_EQ(runtime.getCommittedRefreshRevision(), uint64_t(1));
    CHECK(runtime.getPendingGenerateCount() > 0);
}

TEST_CASE("procgen.runtimeGeneration.coalescesMovingSourcesWithoutRefreshStarvation") {
    RuntimeGeneration runtime(1703);
    runtime.addLevel(10.f, 6.f, 1.5f);
    runtime.setRefreshWorkBudget(1);
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);
    for (int index = 1; index <= 12; ++index) runtime.updateSource(float(index * 100 + 5), 5.f, 1.f, 0.f);
    CHECK(runtime.getCommittedRefreshRevision() > uint64_t(0));
    CHECK(runtime.isRefreshPending());

    runtime.updateSource(1005.f, 5.f, 1.f, 0.f);
    int steps = 0;
    while (runtime.isRefreshPending() && steps++ < 64) REQUIRE(runtime.continueGenerationRefresh().ok());
    REQUIRE(!runtime.isRefreshPending());
    auto latest = ownRequest(runtime.nextGenerate());
    REQUIRE(bool(latest));
    CHECK(latest->getX() >= 99);
}

TEST_CASE("procgen.runtimeGeneration.squirrelAdvancesBudgetedRefresh") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local procgen = eve.Procgen();
        local created = procgen.newRuntimeGeneration(1704);
        if (created.ok) {
            local runtime = created.value;
            runtime.addLevel(10.0, 25.0, 1.5);
            runtime.setRefreshWorkBudget(2);
            runtime.updateSource(5.0, 5.0, 1.0, 0.0);
            local total = 0;
            while (runtime.isRefreshPending()) {
                local advanced = runtime.continueGenerationRefresh();
                if (!advanced.ok) break;
                total += advanced.value.tointeger();
            }
            if (runtime.getRefreshWorkBudget() == 2 &&
                runtime.getCommittedRefreshRevision() == 1 &&
                runtime.getPendingGenerateCount() > 0 && total > 0)
                result = "ok";
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("procgen.runtimeGeneration.trimsLowestPriorityCellsDeterministically") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 18);
    runtime->addLevel(10.f, 25.f, 1.5f);
    runtime->setMaxGenerating(100);
    runtime->updateSource(5.f, 5.f, 0.f, 0.f);
    int farX = 0;
    int farZ = 0;
    PointSet one;
    one.add(0.f, 0.f, 0.f);
    for (int index = 0; index < 3; ++index) {
        auto request = ownRequest(runtime->nextGenerate());
        REQUIRE(bool(request));
        farX = request->getX();
        farZ = request->getZ();
        CHECK(runtime->completeGeneration(request.get(), &one));
    }
    CHECK_EQ(runtime->getResidentPointCount(), 3);
    const int trimmedCells = runtime->trimToResidentPoints(2);
    CHECK_EQ(trimmedCells, 1);
    CHECK_EQ(runtime->getResidentPointCount(), 3);
    auto cleanup = ownRequest(runtime->nextCleanup());
    REQUIRE(bool(cleanup));
    CHECK_EQ(cleanup->getX(), farX);
    CHECK_EQ(cleanup->getZ(), farZ);
    CHECK(runtime->completeCleanup(cleanup.get()));
    CHECK_EQ(runtime->getResidentPointCount(), 2);
}

TEST_CASE("procgen.runtimeGeneration.boundsRetriesAndExplicitlyRecoversFailures") {
    RuntimeGeneration runtime(29);
    runtime.addLevel(10.f, 6.f, 1.5f);
    runtime.setMaxGenerationRetries(1);
    CHECK_EQ(runtime.getMaxGenerationRetries(), 1);
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);

    ProcgenCellRequest* initial = runtime.nextGenerate();
    REQUIRE(bool(initial));
    CHECK(runtime.failGeneration(initial));
    delete initial;
    CHECK_EQ(runtime.getFailedCellCount(), 0);

    ProcgenCellRequest* retry = runtime.nextGenerate();
    REQUIRE(bool(retry));
    CHECK(runtime.failGeneration(retry));
    delete retry;
    CHECK_EQ(runtime.getFailedCellCount(), 1);
    CHECK(runtime.debugReport().find("failed=1") != std::string::npos);

    const int retriedCells = runtime.retryFailedCells();
    CHECK_EQ(retriedCells, 1);
    CHECK_EQ(runtime.getFailedCellCount(), 0);
    ProcgenCellRequest* recovered = runtime.nextGenerate();
    REQUIRE(bool(recovered));
    PointSet empty;
    CHECK(runtime.completeGeneration(recovered, &empty));
    CHECK_EQ(runtime.getActiveCellCount(), 1);
    delete recovered;
}

TEST_CASE("procgen.runtimeGeneration.persistsAttributedCellCachesAtomically") {
    RuntimeGeneration source(404);
    source.addLevel(10.f, 6.f, 1.5f);
    source.updateSource(5.f, 5.f, 1.f, 0.f);
    ProcgenCellRequest* generated = source.nextGenerate();
    REQUIRE(bool(generated));
    const int level = generated->getLevel();
    const int x = generated->getX();
    const int z = generated->getZ();
    PointSet points;
    const int point = points.add(1.25f, 2.5f, 3.75f);
    points.setNormal(point, 0.f, 0.f, 1.f);
    points.setRotation(point, 15.f, 45.f, 25.f);
    points.setScale(point, 2.f, 3.f, 4.f);
    points.setBounds(point, -1.f, -2.f, -3.f, 1.f, 2.f, 3.f);
    points.setColor(point, 0.1f, 0.2f, 0.3f, 0.4f);
    points.setSteepness(point, 0.65f);
    points.setDensity(point, 0.75f);
    points.setPointSeed(point, 1234);
    points.trySetPointId(point, 18446744073709551614ull).expect("runtime generation test point id");
    points.setFloatAttribute(point, "slope", 12.5f);
    points.setFloatAttribute(point, "roughness", 0.4f);
    points.setIntAttribute(point, "variant", 7);
    points.setBoolAttribute(point, "hero", true);
    points.setVectorAttribute(point, "wind", 1.f, 2.f, 3.f);
    points.setStringAttribute(point, "asset", "oak \"hero\"");
    points.setStringAttribute(point, "biome", "forest");
    points.add(9.f, 8.f, 7.f);
    CHECK(source.completeGeneration(generated, &points));
    delete generated;

    const std::string persisted = source.serializeCell(level, x, z);
    CHECK(persisted.find("EVPCG_CELL 3 404") == 0);
    RuntimeGeneration equivalent(404);
    equivalent.addLevel(10.f, 6.f, 1.5f);
    equivalent.updateSource(5.f, 5.f, 1.f, 0.f);
    ProcgenCellRequest* equivalentRequest = equivalent.nextGenerate();
    REQUIRE(bool(equivalentRequest));
    PointSet equivalentPoints = points;
    equivalentPoints.clearPointAttributes(0).expect("runtime generation test metadata reset");
    equivalentPoints.setFloatAttribute(0, "roughness", 0.4f);
    equivalentPoints.setFloatAttribute(0, "slope", 12.5f);
    equivalentPoints.setIntAttribute(0, "variant", 7);
    equivalentPoints.setBoolAttribute(0, "hero", true);
    equivalentPoints.setVectorAttribute(0, "wind", 1.f, 2.f, 3.f);
    equivalentPoints.setStringAttribute(0, "biome", "forest");
    equivalentPoints.setStringAttribute(0, "asset", "oak \"hero\"");
    CHECK(equivalent.completeGeneration(equivalentRequest, &equivalentPoints));
    delete equivalentRequest;
    CHECK_EQ(equivalent.serializeCell(level, x, z), persisted);
    RuntimeGeneration restored(404);
    restored.addLevel(10.f, 6.f, 1.5f);
    CHECK(restored.deserializeCell(persisted));
    CHECK(restored.hasCell(level, x, z));
    CHECK_EQ(restored.getCellRevision(level, x, z), uint64_t(1));
    PointSet* loaded = restored.getCellOutput(level, x, z);
    REQUIRE(bool(loaded));
    CHECK_EQ(loaded->getCount(), 2);
    CHECK_EQ(loaded->getX(0), 1.25f);
    CHECK_EQ(loaded->getNormalZ(0), 1.f);
    CHECK_EQ(loaded->getPitch(0), 15.f);
    CHECK_EQ(loaded->getRoll(0), 25.f);
    CHECK_EQ(loaded->getScaleY(0), 3.f);
    CHECK_EQ(loaded->getBoundsMinY(0), -2.f);
    CHECK_EQ(loaded->getColorA(0), 0.4f);
    CHECK_EQ(loaded->getSteepness(0), 0.65f);
    CHECK_EQ(loaded->getPointId(0), 18446744073709551614ull);
    CHECK_EQ(loaded->getFloatAttribute(0, "slope", -1.f), 12.5f);
    CHECK_EQ(loaded->getIntAttribute(0, "variant", -1), 7);
    CHECK(loaded->getBoolAttribute(0, "hero", false));
    CHECK_EQ(loaded->getVectorAttributeZ(0, "wind", -1.f), 3.f);
    CHECK_EQ(loaded->getStringAttribute(0, "asset", ""), std::string("oak \"hero\""));
    delete loaded;

    CHECK(!restored.deserializeCell(persisted + "TRAILING"));
    CHECK(restored.hasCell(level, x, z));
    RuntimeGeneration wrongWorld(405);
    wrongWorld.addLevel(10.f, 6.f, 1.5f);
    CHECK(!wrongWorld.deserializeCell(persisted));
    RuntimeGeneration constrained(404);
    constrained.addLevel(10.f, 6.f, 1.5f);
    constrained.setMaxPointsPerCell(1);
    CHECK(!constrained.deserializeCell(persisted));
    CHECK_EQ(constrained.getRejectedOutputCount(), 1);

    RuntimeGeneration legacy(404);
    legacy.addLevel(10.f, 6.f, 1.5f);
    const std::string legacyV1 =
        "EVPCG_CELL 1 404 0 0 0 1 10\nPOINTS 1\n"
        "POINT 1 2 3 0 1 0 90 1 1 1 0.5 77\nEND\n";
    CHECK(legacy.deserializeCell(legacyV1));
    PointSet* legacyPoints = legacy.getCellOutput(0, 0, 0);
    REQUIRE(bool(legacyPoints));
    CHECK_EQ(legacyPoints->getYaw(0), 90.f);
    CHECK_EQ(legacyPoints->getPitch(0), 0.f);
    CHECK_EQ(legacyPoints->getColorR(0), 1.f);
    CHECK_EQ(legacyPoints->getSteepness(0), 0.5f);
    CHECK_EQ(legacyPoints->getPointId(0), uint64_t(0));
    delete legacyPoints;

    RuntimeGeneration legacyV2Runtime(404);
    legacyV2Runtime.addLevel(10.f, 6.f, 1.5f);
    const std::string legacyV2 =
        "EVPCG_CELL 2 404 0 0 0 1 10\nPOINTS 1\n"
        "POINT 1 2 3 0 1 0 10 90 20 1 1 1 0.5 77 0 0 0 0 0 0 1 1 1 1 0.5\nEND\n";
    CHECK(legacyV2Runtime.deserializeCell(legacyV2));
    PointSet* legacyV2Points = legacyV2Runtime.getCellOutput(0, 0, 0);
    REQUIRE(bool(legacyV2Points));
    CHECK_EQ(legacyV2Points->getPointId(0), uint64_t(0));
    delete legacyV2Points;
}
