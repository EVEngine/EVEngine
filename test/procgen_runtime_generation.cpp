#include "procgen/RuntimeGeneration.h"

#include <zeroerr.hpp>

using namespace eve::procgen;

TEST_CASE("procgen.runtimeGeneration.partitionsAndPublishesCells") {
    RuntimeGeneration runtime(42);
    const int nearLevel = runtime.addLevel(10.f, 8.f, 1.5f);
    const int farLevel  = runtime.addLevel(40.f, 30.f, 2.f);
    CHECK_EQ(nearLevel, 0);
    CHECK_EQ(farLevel, 1);
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

TEST_CASE("procgen.runtimeGeneration.enforcesResidentCellReservations") {
    RuntimeGeneration runtime(91);
    runtime.addLevel(10.f, 30.f, 1.5f);
    runtime.setMaxGenerating(100);
    runtime.setMaxActiveCells(2);
    CHECK_EQ(runtime.getMaxActiveCells(), 2);
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);

    ProcgenCellRequest* first = runtime.nextGenerate();
    ProcgenCellRequest* second = runtime.nextGenerate();
    REQUIRE(bool(first));
    REQUIRE(bool(second));
    CHECK(!runtime.nextGenerate());
    CHECK_EQ(runtime.getGeneratingCount(), 2);

    PointSet empty;
    CHECK(runtime.completeGeneration(first, &empty));
    CHECK(runtime.completeGeneration(second, &empty));
    CHECK_EQ(runtime.getActiveCellCount(), 2);
    CHECK(!runtime.nextGenerate());
    CHECK(runtime.debugReport().find("maxActive=2") != std::string::npos);
    delete second;
    delete first;

    runtime.setMaxActiveCells(0);
    CHECK_EQ(runtime.getMaxActiveCells(), 0);
    ProcgenCellRequest* unlimited = runtime.nextGenerate();
    REQUIRE(bool(unlimited));
    delete unlimited;
}

TEST_CASE("procgen.runtimeGeneration.enforcesPointMemoryBudgets") {
    RuntimeGeneration runtime(92);
    runtime.addLevel(10.f, 25.f, 1.5f);
    runtime.setMaxGenerating(100);
    runtime.setMaxPointsPerCell(2);
    runtime.setMaxResidentPoints(2);
    CHECK_EQ(runtime.getMaxPointsPerCell(), 2);
    CHECK_EQ(runtime.getMaxResidentPoints(), 2);
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);

    ProcgenCellRequest* first = runtime.nextGenerate();
    REQUIRE(bool(first));
    PointSet oversized;
    oversized.add(0.f, 0.f, 0.f);
    oversized.add(1.f, 0.f, 0.f);
    oversized.add(2.f, 0.f, 0.f);
    CHECK(!runtime.completeGeneration(first, &oversized));
    CHECK_EQ(runtime.getRejectedOutputCount(), 1);
    CHECK(runtime.failGeneration(first));
    delete first;

    ProcgenCellRequest* retry = runtime.nextGenerate();
    REQUIRE(bool(retry));
    PointSet accepted;
    accepted.add(0.f, 0.f, 0.f);
    accepted.add(1.f, 0.f, 0.f);
    CHECK(runtime.completeGeneration(retry, &accepted));
    delete retry;
    CHECK_EQ(runtime.getResidentPointCount(), 2);

    ProcgenCellRequest* second = runtime.nextGenerate();
    REQUIRE(bool(second));
    PointSet one;
    one.add(0.f, 0.f, 0.f);
    CHECK(!runtime.completeGeneration(second, &one));
    CHECK_EQ(runtime.getRejectedOutputCount(), 2);
    CHECK_EQ(runtime.getPendingCleanupCount(), 1);
    runtime.refreshGenerationSources();
    CHECK(!runtime.completeGeneration(second, &one));
    CHECK_EQ(runtime.getRejectedOutputCount(), 3);
    CHECK_EQ(runtime.getPendingCleanupCount(), 1);
    ProcgenCellRequest* cleanup = runtime.nextCleanup();
    REQUIRE(bool(cleanup));
    CHECK(runtime.completeCleanup(cleanup));
    delete cleanup;
    CHECK_EQ(runtime.getResidentPointCount(), 0);
    CHECK(runtime.completeGeneration(second, &one));
    CHECK_EQ(runtime.getResidentPointCount(), 1);
    CHECK(runtime.debugReport().find("residentPoints=1") != std::string::npos);
    delete second;
}

TEST_CASE("procgen.runtimeGeneration.breaksEqualPriorityTiesDeterministically") {
    RuntimeGeneration first(17);
    RuntimeGeneration second(17);
    for (RuntimeGeneration* runtime : {&first, &second}) {
        runtime->addLevel(10.f, 25.f, 1.5f);
        runtime->setMaxGenerating(100);
        runtime->updateSource(5.f, 5.f, 0.f, 0.f);
    }
    for (int index = 0; index < 12; ++index) {
        ProcgenCellRequest* a = first.nextGenerate();
        ProcgenCellRequest* b = second.nextGenerate();
        REQUIRE(bool(a));
        REQUIRE(bool(b));
        CHECK_EQ(a->getLevel(), b->getLevel());
        CHECK_EQ(a->getX(), b->getX());
        CHECK_EQ(a->getZ(), b->getZ());
        delete b;
        delete a;
    }
}

TEST_CASE("procgen.runtimeGeneration.trimsLowestPriorityCellsDeterministically") {
    RuntimeGeneration runtime(18);
    runtime.addLevel(10.f, 25.f, 1.5f);
    runtime.setMaxGenerating(100);
    runtime.updateSource(5.f, 5.f, 0.f, 0.f);
    int farX = 0;
    int farZ = 0;
    PointSet one;
    one.add(0.f, 0.f, 0.f);
    for (int index = 0; index < 3; ++index) {
        ProcgenCellRequest* request = runtime.nextGenerate();
        REQUIRE(bool(request));
        farX = request->getX();
        farZ = request->getZ();
        CHECK(runtime.completeGeneration(request, &one));
        delete request;
    }
    CHECK_EQ(runtime.getResidentPointCount(), 3);
    const int trimmedCells = runtime.trimToResidentPoints(2);
    CHECK_EQ(trimmedCells, 1);
    CHECK_EQ(runtime.getResidentPointCount(), 3);
    ProcgenCellRequest* cleanup = runtime.nextCleanup();
    REQUIRE(bool(cleanup));
    CHECK_EQ(cleanup->getX(), farX);
    CHECK_EQ(cleanup->getZ(), farZ);
    CHECK(runtime.completeCleanup(cleanup));
    delete cleanup;
    CHECK_EQ(runtime.getResidentPointCount(), 2);
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
    points.setYaw(point, 45.f);
    points.setScale(point, 2.f, 3.f, 4.f);
    points.setDensity(point, 0.75f);
    points.setPointSeed(point, 1234);
    points.setFloatAttribute(point, "slope", 12.5f);
    points.setFloatAttribute(point, "roughness", 0.4f);
    points.setStringAttribute(point, "asset", "oak \"hero\"");
    points.setStringAttribute(point, "biome", "forest");
    points.add(9.f, 8.f, 7.f);
    CHECK(source.completeGeneration(generated, &points));
    delete generated;

    const std::string persisted = source.serializeCell(level, x, z);
    CHECK(persisted.find("EVPCG_CELL 1 404") == 0);
    RuntimeGeneration equivalent(404);
    equivalent.addLevel(10.f, 6.f, 1.5f);
    equivalent.updateSource(5.f, 5.f, 1.f, 0.f);
    ProcgenCellRequest* equivalentRequest = equivalent.nextGenerate();
    REQUIRE(bool(equivalentRequest));
    PointSet equivalentPoints = points;
    equivalentPoints.points()[0].floatAttributes.clear();
    equivalentPoints.points()[0].stringAttributes.clear();
    equivalentPoints.setFloatAttribute(0, "roughness", 0.4f);
    equivalentPoints.setFloatAttribute(0, "slope", 12.5f);
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
    CHECK_EQ(loaded->getScaleY(0), 3.f);
    CHECK_EQ(loaded->getFloatAttribute(0, "slope", -1.f), 12.5f);
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
}
