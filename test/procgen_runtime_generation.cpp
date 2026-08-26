#include "procgen/RuntimeGeneration.h"

#include <zeroerr.hpp>

using namespace eve::procgen;

TEST_CASE("procgen.runtimeGeneration.partitionsAndPublishesCells") {
    RuntimeGeneration runtime(42);
    CHECK_EQ(runtime.addLevel(10.f, 8.f, 1.5f), 0);
    CHECK_EQ(runtime.addLevel(40.f, 30.f, 2.f), 1);
    CHECK_EQ(runtime.getLevelCount(), 2);
    CHECK_EQ(runtime.getLevelCleanupRadius(0), 12.f);
    runtime.setMaxGenerating(1);
    runtime.setFrameTimeBudget(2.f);
    CHECK_EQ(runtime.getFrameTimeBudget(), 2.f);
    runtime.beginFrame();
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);
    CHECK(runtime.getPendingGenerateCount() > 0);

    ProcgenCellRequest* request = runtime.nextGenerate();
    REQUIRE(bool(request));
    CHECK_EQ(runtime.getGeneratingCount(), 1);
    CHECK(!runtime.nextGenerate());
    CHECK_NE(request->getSeed(), uint32_t(0));
    CHECK(request->getMaxX() > request->getMinX());

    const int level = request->getLevel();
    const int x     = request->getX();
    const int z     = request->getZ();
    PointSet output;
    output.add(request->getMinX(), 0.f, request->getMinZ());
    CHECK(runtime.completeGeneration(request, &output));
    CHECK(runtime.hasCell(level, x, z));
    CHECK_EQ(runtime.getCellRevision(level, x, z), uint64_t(1));
    PointSet* stored = runtime.getCellOutput(level, x, z);
    REQUIRE(bool(stored));
    CHECK_EQ(stored->getCount(), 1);

    delete stored;
    delete request;
}

TEST_CASE("procgen.runtimeGeneration.cleansBeyondHysteresisRadius") {
    RuntimeGeneration runtime(7);
    runtime.addLevel(10.f, 6.f, 2.f);
    runtime.updateSource(5.f, 5.f, 0.f, 1.f);
    ProcgenCellRequest* generated = runtime.nextGenerate();
    REQUIRE(bool(generated));
    const int cellX = generated->getX();
    const int cellZ = generated->getZ();
    PointSet points;
    points.add(5.f, 0.f, 5.f);
    CHECK(runtime.completeGeneration(generated, &points));
    delete generated;

    runtime.updateSource(100.f, 100.f, 0.f, 1.f);
    CHECK(runtime.getPendingCleanupCount() > 0);
    bool cleanedTarget = false;
    while (ProcgenCellRequest* cleanup = runtime.nextCleanup()) {
        if (cleanup->getX() == cellX && cleanup->getZ() == cellZ) cleanedTarget = true;
        CHECK(runtime.completeCleanup(cleanup));
        delete cleanup;
    }
    CHECK(cleanedTarget);
    CHECK(!runtime.hasCell(0, cellX, cellZ));
}

TEST_CASE("procgen.runtimeGeneration.failedWorkCanRetryWithStableSeed") {
    RuntimeGeneration runtime(99);
    runtime.addLevel(8.f, 5.f, 1.25f);
    runtime.updateSource(4.f, 4.f, 1.f, 0.f);
    ProcgenCellRequest* first = runtime.nextGenerate();
    REQUIRE(bool(first));
    const uint32_t seed = first->getSeed();
    CHECK(runtime.failGeneration(first));
    delete first;

    ProcgenCellRequest* retry = runtime.nextGenerate();
    REQUIRE(bool(retry));
    CHECK_EQ(retry->getSeed(), seed);
    CHECK_EQ(retry->getLevel(), 0);
    delete retry;
}

TEST_CASE("procgen.runtimeGeneration.unionsNamedSourcesAndCleansAfterLastSource") {
    RuntimeGeneration runtime(123);
    runtime.addLevel(10.f, 16.f, 1.5f);
    runtime.setMaxGenerating(100);
    CHECK(runtime.setGenerationSource("player", 5.f, 5.f, 1.f, 0.f, 1.f));
    CHECK(runtime.setGenerationSource("quest", 105.f, 5.f, 0.f, 0.f, 1.f));
    CHECK_EQ(runtime.getGenerationSourceCount(), 2);
    CHECK_EQ(runtime.getGenerationSourceId(0), std::string("player"));
    CHECK_EQ(runtime.getGenerationSourceId(1), std::string("quest"));

    bool generatedNearPlayer = false;
    bool generatedNearQuest  = false;
    PointSet empty;
    while (ProcgenCellRequest* request = runtime.nextGenerate()) {
        if (request->getX() >= -1 && request->getX() <= 1) generatedNearPlayer = true;
        if (request->getX() >= 9 && request->getX() <= 11) generatedNearQuest = true;
        CHECK(runtime.completeGeneration(request, &empty));
        delete request;
    }
    CHECK(generatedNearPlayer);
    CHECK(generatedNearQuest);

    CHECK(runtime.removeGenerationSource("player"));
    bool cleanupNearPlayer = false;
    bool cleanupNearQuest  = false;
    while (ProcgenCellRequest* request = runtime.nextCleanup()) {
        if (request->getX() >= -1 && request->getX() <= 1) cleanupNearPlayer = true;
        if (request->getX() >= 9 && request->getX() <= 11) cleanupNearQuest = true;
        CHECK(runtime.completeCleanup(request));
        delete request;
    }
    CHECK(cleanupNearPlayer);
    CHECK(!cleanupNearQuest);
}

TEST_CASE("procgen.runtimeGeneration.frustumCullsFarBehindButKeepsNearCells") {
    RuntimeGeneration runtime(9);
    runtime.addLevel(10.f, 35.f, 1.25f);
    runtime.setMaxGenerating(100);
    runtime.setFrustumCulling(true, 45.f, 11.f);
    CHECK(runtime.isFrustumCullingEnabled());
    CHECK_EQ(runtime.getFrustumHalfAngle(), 45.f);
    CHECK_EQ(runtime.getFrustumBehindRadius(), 11.f);
    CHECK(runtime.setGenerationSource("camera", 5.f, 5.f, 1.f, 0.f, 1.f));

    bool foundFarBehind = false;
    bool foundNearBehind = false;
    bool foundForward = false;
    PointSet empty;
    while (ProcgenCellRequest* request = runtime.nextGenerate()) {
        const float centerX = (request->getMinX() + request->getMaxX()) * 0.5f;
        if (centerX < -6.f) foundFarBehind = true;
        if (centerX == -5.f) foundNearBehind = true;
        if (centerX > 5.f) foundForward = true;
        CHECK(runtime.completeGeneration(request, &empty));
        delete request;
    }
    CHECK(!foundFarBehind);
    CHECK(foundNearBehind);
    CHECK(foundForward);
}

TEST_CASE("procgen.runtimeGeneration.rejectsStaleAsyncGenerationTickets") {
    RuntimeGeneration runtime(77);
    runtime.addLevel(10.f, 6.f, 1.5f);
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);
    ProcgenCellRequest* stale = runtime.nextGenerate();
    REQUIRE(bool(stale));
    const uint64_t staleTicket = stale->getTicket();

    runtime.updateSource(100.f, 100.f, 1.f, 0.f);
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);
    ProcgenCellRequest* current = runtime.nextGenerate();
    REQUIRE(bool(current));
    CHECK_NE(current->getTicket(), staleTicket);
    PointSet output;
    CHECK(!runtime.completeGeneration(stale, &output));
    CHECK(runtime.completeGeneration(current, &output));
    delete current;
    delete stale;
}

TEST_CASE("procgen.runtimeGeneration.rejectsStaleAsyncCleanupTickets") {
    RuntimeGeneration runtime(78);
    runtime.addLevel(10.f, 6.f, 1.5f);
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);
    ProcgenCellRequest* generated = runtime.nextGenerate();
    REQUIRE(bool(generated));
    PointSet output;
    CHECK(runtime.completeGeneration(generated, &output));
    delete generated;

    runtime.updateSource(100.f, 100.f, 1.f, 0.f);
    ProcgenCellRequest* stale = runtime.nextCleanup();
    REQUIRE(bool(stale));
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);
    runtime.updateSource(100.f, 100.f, 1.f, 0.f);
    CHECK(!runtime.completeCleanup(stale));
    ProcgenCellRequest* current = runtime.nextCleanup();
    REQUIRE(bool(current));
    CHECK_NE(current->getTicket(), stale->getTicket());
    CHECK(runtime.completeCleanup(current));
    delete current;
    delete stale;
}
