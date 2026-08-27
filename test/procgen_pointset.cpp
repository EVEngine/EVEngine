#include "procgen/PointSet.h"
#include "procgen/Procgen.h"
#include "procgen/heightmap/Heightmap.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;

namespace {

using PointSetHandle = ProcgenPointSetHandleRef;
using ContextHandle  = ProcgenContextHandleRef;

/** @brief Owns one module point-set handle for the duration of a test. */
struct PointSetLease {
    Procgen*      owner  = nullptr;
    PointSetHandle handle{};

    PointSetLease() = default;
    PointSetLease(Procgen& proc, PointSetHandle value) : owner(&proc), handle(value) {}
    PointSetLease(const PointSetLease&)            = delete;
    PointSetLease& operator=(const PointSetLease&) = delete;
    PointSetLease(PointSetLease&& other) noexcept
        : owner(other.owner), handle(other.handle) {
        other.owner = nullptr;
        other.handle = {};
    }
    PointSetLease& operator=(PointSetLease&& other) noexcept {
        if (this == &other) return *this;
        reset();
        owner        = other.owner;
        handle       = other.handle;
        other.owner  = nullptr;
        other.handle = {};
        return *this;
    }
    ~PointSetLease() { reset(); }

    void reset() noexcept {
        if (!owner || !handle.isValid()) return;
        auto result = owner->releasePointSet(handle);
        result.ignore("test point-set lease cleanup");
        owner  = nullptr;
        handle = {};
    }

    [[nodiscard]] eve::script::Borrowed<PointSet> view() const noexcept {
        return owner ? owner->resolvePointSet(handle) : eve::script::Borrowed<PointSet>();
    }
};

/** @brief Converts a successful point-set allocation into an owned test lease. */
PointSetLease requirePointSet(Procgen& proc, eve::Result<PointSetHandle>&& result) {
    const bool ok = result.ok();
    if (!ok) {
        const eve::Diagnostic* diagnostic = result.error();
        REQUIRE(diagnostic != nullptr);
        if (diagnostic) CHECK(diagnostic->isError());
    }
    REQUIRE(ok);
    return PointSetLease(proc, std::move(result).takeValue());
}

/** @brief Converts a successful rebuild-context allocation into a test lease. */
struct ContextLease {
    Procgen*     owner  = nullptr;
    ContextHandle handle{};

    ContextLease() = default;
    ContextLease(Procgen& proc, ContextHandle value) : owner(&proc), handle(value) {}
    ContextLease(const ContextLease&)            = delete;
    ContextLease& operator=(const ContextLease&) = delete;
    ContextLease(ContextLease&& other) noexcept
        : owner(other.owner), handle(other.handle) {
        other.owner = nullptr;
        other.handle = {};
    }
    ContextLease& operator=(ContextLease&& other) noexcept {
        if (this == &other) return *this;
        reset();
        owner        = other.owner;
        handle       = other.handle;
        other.owner  = nullptr;
        other.handle = {};
        return *this;
    }
    ~ContextLease() { reset(); }

    void reset() noexcept {
        if (!owner || !handle.isValid()) return;
        auto result = Procgen::release(handle);
        result.ignore("test context lease cleanup");
        owner  = nullptr;
        handle = {};
    }

    [[nodiscard]] eve::script::Borrowed<ProcgenContext> view() const noexcept {
        return owner ? Procgen::resolve(handle)
                     : eve::script::Borrowed<ProcgenContext>();
    }
};

ContextLease requireContext(Procgen& proc, eve::Result<ContextHandle>&& result) {
    const bool ok = result.ok();
    if (!ok) {
        const eve::Diagnostic* diagnostic = result.error();
        REQUIRE(diagnostic != nullptr);
        if (diagnostic) CHECK(diagnostic->isError());
    }
    REQUIRE(ok);
    return ContextLease(proc, std::move(result).takeValue());
}

template <typename T>
void checkResult(const eve::Result<T>& result, eve::DiagnosticCode expected = eve::DiagnosticCode::None) {
    const bool ok = result.ok();
    CHECK(ok);
    if (!ok) {
        const eve::Diagnostic* diagnostic = result.error();
        REQUIRE(diagnostic != nullptr);
        if (diagnostic && expected != eve::DiagnosticCode::None)
            CHECK_EQ(diagnostic->code(), expected);
    }
}

}  // namespace

TEST_CASE("procgen.pointSet.composableDeterministicPipeline") {
    Procgen        proc;
    const uint32_t forestSeed = proc.deriveSeed(42, "forest");
    CHECK_NE(forestSeed, proc.deriveSeed(42, "rocks"));
    CHECK_EQ(forestSeed, proc.deriveSeed(42, "forest"));

    auto source = requirePointSet(proc, proc.sampleGridHandle(8, 6, 2.f, forestSeed, 0.f));
    auto sourceView = source.view();
    REQUIRE(sourceView.isBound());
    PointSet* sourcePtr = sourceView.get();
    CHECK_EQ(sourcePtr->getCount(), 48);
    CHECK_EQ(sourcePtr->getX(1), 2.f);
    CHECK_EQ(sourcePtr->getZ(8), 2.f);

    for (int i = 0; i < sourcePtr->getCount(); ++i) {
        sourcePtr->setPosition(i, sourcePtr->getX(i), float(i % 4), sourcePtr->getZ(i));
        sourcePtr->setDensity(i, float(i % 5) / 4.f);
        sourcePtr->setStringAttribute(i, "biome", "forest");
    }

    auto height = requirePointSet(proc, proc.filterHeightHandle(source.handle, 1.f, 2.f));
    auto heightView = height.view();
    REQUIRE(heightView.isBound());
    CHECK_EQ(heightView->getCount(), 24);
    auto dense = requirePointSet(proc, proc.filterDensityHandle(height.handle, 0.5f, 1.f));
    auto denseView = dense.view();
    REQUIRE(denseView.isBound());
    CHECK(denseView->getCount() < heightView->getCount());
    CHECK(denseView->getCount() > 0);
    CHECK_EQ(denseView->getStringAttribute(0, "biome", ""), std::string("forest"));

    auto excluded = requirePointSet(proc, proc.excludeRadiusHandle(dense.handle, 0.f, 0.f, 3.f));
    auto excludedView = excluded.view();
    REQUIRE(excludedView.isBound());
    auto pruned = requirePointSet(proc, proc.selfPruneHandle(excluded.handle, 2.1f));
    auto prunedView = pruned.view();
    REQUIRE(prunedView.isBound());
    CHECK(prunedView->getCount() <= excludedView->getCount());

    const uint32_t jitterSeed = proc.deriveSeed(forestSeed, "jitter");
    auto jitterA = requirePointSet(proc, proc.jitterPointsHandle(pruned.handle, jitterSeed, 0.5f, 0.5f));
    auto jitterB = requirePointSet(proc, proc.jitterPointsHandle(pruned.handle, jitterSeed, 0.5f, 0.5f));
    auto jitterAView = jitterA.view();
    auto jitterBView = jitterB.view();
    REQUIRE(jitterAView.isBound());
    REQUIRE(jitterBView.isBound());
    CHECK_EQ(jitterAView->getCount(), jitterBView->getCount());
    for (int i = 0; i < jitterAView->getCount(); ++i) {
        CHECK_EQ(jitterAView->getX(i), jitterBView->getX(i));
        CHECK_EQ(jitterAView->getZ(i), jitterBView->getZ(i));
    }
}

TEST_CASE("procgen.pointSet.invalidInputIsReported") {
    Procgen proc;
    auto failed = proc.filterHeightHandle({}, 0.f, 1.f);
    CHECK(!failed.ok());
    const eve::Diagnostic* diagnostic = failed.error();
    REQUIRE(diagnostic != nullptr);
    CHECK_EQ(diagnostic->code(), eve::DiagnosticCode::StaleHandle);
}

TEST_CASE("procgen.pointSet.spatialFiltersAndHeightmapProjection") {
    Procgen  proc;
    PointSet source;
    source.add(0.f, 0.f, 0.f);
    source.add(1.f, 0.f, 1.f);
    source.add(2.f, 0.f, 2.f);

    auto sourceResult = proc.newPointSetHandle();
    auto sourceHandle = std::move(sourceResult).takeValue();
    auto sourceView = proc.resolvePointSet(sourceHandle);
    REQUIRE(sourceView.isBound());
    *sourceView = source;
    auto inside = requirePointSet(proc, proc.filterBoxHandle(sourceHandle, 0.5f, -1.f, 0.5f, 1.5f, 1.f, 1.5f));
    auto insideView = inside.view();
    REQUIRE(insideView.isBound());
    CHECK_EQ(insideView->getCount(), 1);
    auto outside = requirePointSet(proc, proc.filterBoxHandle(sourceHandle, 0.5f, -1.f, 0.5f, 1.5f, 1.f, 1.5f, true));
    auto outsideView = outside.view();
    REQUIRE(outsideView.isBound());
    CHECK_EQ(outsideView->getCount(), 2);

    Heightmap heightmap(3, 3);
    for (int z = 0; z < 3; ++z)
        for (int x = 0; x < 3; ++x) heightmap.setHeight(x, z, float(x));
    auto heightmapResult = proc.newHeightmapHandle(3, 3);
    REQUIRE(heightmapResult.ok());
    auto heightmapHandle = std::move(heightmapResult).takeValue();
    auto heightmapView = proc.resolveHeightmap(heightmapHandle);
    REQUIRE(heightmapView.isBound());
    for (int z = 0; z < 3; ++z)
        for (int x = 0; x < 3; ++x) heightmapView->setHeight(x, z, float(x));
    auto projected = requirePointSet(proc, proc.projectToHeightmapHandle(
        sourceHandle, heightmapHandle, 0.f, 0.f, 1.f, 2.f));
    auto projectedView = projected.view();
    REQUIRE(projectedView.isBound());
    CHECK_EQ(projectedView->getY(0), 0.f);
    CHECK_EQ(projectedView->getY(1), 2.f);
    CHECK_EQ(projectedView->getY(2), 4.f);
    CHECK(projectedView->getNormalX(1) < 0.f);
    CHECK(projectedView->getNormalY(1) > 0.f);

    auto gentle = requirePointSet(proc, proc.filterSlopeHandle(projected.handle, 0.f, 80.f));
    auto gentleView = gentle.view();
    REQUIRE(gentleView.isBound());
    CHECK_EQ(gentleView->getCount(), 3);
    auto flat = requirePointSet(proc, proc.filterSlopeHandle(projected.handle, 0.f, 1.f));
    auto flatView = flat.view();
    REQUIRE(flatView.isBound());
    CHECK_EQ(flatView->getCount(), 0);
    auto heightmapRelease = proc.releaseHeightmap(heightmapHandle);
    heightmapRelease.ignore("test heightmap cleanup");
    auto sourceCleanup = proc.releasePointSet(sourceHandle);
    sourceCleanup.ignore("test point-set setup cleanup");
}

TEST_CASE("procgen.pointSet.polygonAndSplineQueries") {
    Procgen  proc;
    PointSet polygon;
    polygon.add(0.f, 0.f, 0.f);
    polygon.add(10.f, 0.f, 0.f);
    polygon.add(10.f, 0.f, 10.f);
    polygon.add(0.f, 0.f, 10.f);

    PointSet candidates;
    candidates.add(5.f, 0.f, 5.f);
    candidates.add(15.f, 0.f, 5.f);
    auto polygonResult = proc.newPointSetHandle();
    REQUIRE(polygonResult.ok());
    auto polygonHandle = std::move(polygonResult).takeValue();
    auto polygonView = proc.resolvePointSet(polygonHandle);
    REQUIRE(polygonView.isBound());
    *polygonView = polygon;
    auto candidatesResult = proc.newPointSetHandle();
    REQUIRE(candidatesResult.ok());
    auto candidatesHandle = std::move(candidatesResult).takeValue();
    auto candidatesView = proc.resolvePointSet(candidatesHandle);
    REQUIRE(candidatesView.isBound());
    *candidatesView = candidates;
    auto inside = requirePointSet(proc, proc.filterPolygonHandle(candidatesHandle, polygonHandle));
    auto outside = requirePointSet(proc, proc.filterPolygonHandle(candidatesHandle, polygonHandle, true));
    auto insideView = inside.view();
    auto outsideView = outside.view();
    REQUIRE(insideView.isBound());
    REQUIRE(outsideView.isBound());
    CHECK_EQ(insideView->getCount(), 1);
    CHECK_EQ(outsideView->getCount(), 1);
    CHECK_EQ(insideView->getX(0), 5.f);
    CHECK_EQ(outsideView->getX(0), 15.f);

    PointSet control;
    control.add(0.f, 0.f, 0.f);
    control.add(10.f, 0.f, 0.f);
    control.add(10.f, 0.f, 10.f);
    auto controlResult = proc.newPointSetHandle();
    REQUIRE(controlResult.ok());
    auto controlHandle = std::move(controlResult).takeValue();
    auto controlView = proc.resolvePointSet(controlHandle);
    REQUIRE(controlView.isBound());
    *controlView = control;
    auto samples = requirePointSet(proc, proc.sampleSplineHandle(controlHandle, 5.f, 42, 0.f));
    auto samplesView = samples.view();
    REQUIRE(samplesView.isBound());
    CHECK_EQ(samplesView->getCount(), 5);
    CHECK_EQ(samplesView->getX(1), 5.f);
    CHECK_EQ(samplesView->getZ(3), 5.f);
    CHECK_EQ(samplesView->getYaw(0), 0.f);
    CHECK_EQ(samplesView->getYaw(3), 90.f);

    PointSet nearby;
    nearby.add(5.f, 0.f, 2.f);
    nearby.add(5.f, 0.f, 5.f);
    nearby.add(12.f, 0.f, 5.f);
    auto nearbyResult = proc.newPointSetHandle();
    REQUIRE(nearbyResult.ok());
    auto nearbyHandle = std::move(nearbyResult).takeValue();
    auto nearbyView = proc.resolvePointSet(nearbyHandle);
    REQUIRE(nearbyView.isBound());
    *nearbyView = nearby;
    auto corridor = requirePointSet(proc, proc.filterSplineDistanceHandle(
        nearbyHandle, controlHandle, 0.f, 2.1f));
    auto corridorView = corridor.view();
    REQUIRE(corridorView.isBound());
    CHECK_EQ(corridorView->getCount(), 2);
    auto polygonCleanup = proc.releasePointSet(polygonHandle);
    polygonCleanup.ignore("test polygon cleanup");
    auto candidatesCleanup = proc.releasePointSet(candidatesHandle);
    candidatesCleanup.ignore("test candidates cleanup");
    auto controlCleanup = proc.releasePointSet(controlHandle);
    controlCleanup.ignore("test control cleanup");
    auto nearbyCleanup = proc.releasePointSet(nearbyHandle);
    nearbyCleanup.ignore("test nearby cleanup");
}

TEST_CASE("procgen.system.commitIsAtomicAndFailureKeepsPreviousSnapshot") {
    // Static handle factories resolve against the manager-owned singleton.
    // Keep commit/read operations on that same owner and epoch.
    Procgen& proc = *Procgen::create();
    PointSet first;
    first.add(1.f, 2.f, 3.f);

    auto initial = requireContext(proc, Procgen::beginSystemHandle("forest", 42));
    auto initialView = initial.view();
    REQUIRE(initialView.isBound());
    CHECK(initialView->publish("trees", &first));
    CHECK(initialView->captureDebug("candidates", &first));
    CHECK_EQ(initialView->getDebugStageCount(), 1);
    CHECK_EQ(initialView->getDebugStageName(0), std::string("candidates"));
    initialView->trace("sample", 0, 1, 0.25f);
    auto initialCommit = proc.commitSystem(initial.handle);
    CHECK(initialCommit.ok());
    CHECK(proc.hasSystem("forest"));
    CHECK_EQ(proc.getSystemRevision("forest"), uint64_t(1));
    CHECK_EQ(proc.getSystemDebugStageCount("forest"), 1);
    CHECK_EQ(proc.getSystemDebugStageName("forest", 0), std::string("candidates"));

    auto debug = requirePointSet(proc, proc.getSystemDebugStageHandle("forest", "candidates"));
    auto debugView = debug.view();
    REQUIRE(debugView.isBound());
    CHECK_EQ(debugView->getCount(), 1);
    auto committed = requirePointSet(proc, proc.getSystemOutputHandle("forest", "trees"));
    auto committedView = committed.view();
    REQUIRE(committedView.isBound());
    CHECK_EQ(committedView->getCount(), 1);

    PointSet replacement;
    replacement.add(9.f, 9.f, 9.f);
    replacement.add(8.f, 8.f, 8.f);
    auto failed = requireContext(proc, Procgen::beginSystemHandle("forest", 99));
    auto failedView = failed.view();
    REQUIRE(failedView.isBound());
    CHECK(failedView->publish("trees", &replacement));
    failedView->fail("script error");
    auto failedCommit = proc.commitSystem(failed.handle);
    CHECK(!failedCommit.ok());
    const eve::Diagnostic* failure = failedCommit.error();
    REQUIRE(failure != nullptr);
    CHECK(failure->isError());
    CHECK_EQ(proc.getSystemRevision("forest"), uint64_t(1));
    CHECK_EQ(proc.getSystemSeed("forest"), uint32_t(42));
    CHECK_EQ(proc.getSystemDebugStageCount("forest"), 1);

    auto committedAgain = requirePointSet(proc, proc.getSystemOutputHandle("forest", "trees"));
    auto committedAgainView = committedAgain.view();
    REQUIRE(committedAgainView.isBound());
    CHECK_EQ(committedAgainView->getCount(), 1);
    CHECK(proc.getSystemDebugReport("forest").find("sample") != std::string::npos);
}

TEST_CASE("procgen.system.cachedBuildReusesCommittedSnapshot") {
    Procgen  proc;
    PointSet points;
    points.add(1.f, 0.f, 2.f);

    auto miss = requireContext(proc, Procgen::beginCachedSystemHandle("rocks", 7, "layout-v1:size=32"));
    auto missView = miss.view();
    REQUIRE(missView.isBound());
    CHECK(!missView->isCacheHit());
    CHECK(missView->isActive());
    CHECK_EQ(missView->getBuildKey(), std::string("layout-v1:size=32"));
    CHECK(missView->publish("rocks", &points));
    auto missCommit = proc.commitSystem(miss.handle);
    CHECK(missCommit.ok());
    CHECK_EQ(proc.getSystemRevision("rocks"), uint64_t(1));
    CHECK_EQ(proc.getSystemBuildKey("rocks"), std::string("layout-v1:size=32"));

    auto hit = requireContext(proc, Procgen::beginCachedSystemHandle("rocks", 7, "layout-v1:size=32"));
    auto hitView = hit.view();
    REQUIRE(hitView.isBound());
    CHECK(hitView->isCacheHit());
    CHECK(!hitView->isActive());
    CHECK_EQ(proc.getSystemRevision("rocks"), uint64_t(1));

    auto changed = requireContext(proc, Procgen::beginCachedSystemHandle("rocks", 7, "layout-v1:size=64"));
    auto changedView = changed.view();
    REQUIRE(changedView.isBound());
    CHECK(!changedView->isCacheHit());
    CHECK(changedView->isActive());
}

TEST_CASE("procgen.system.stageCacheIsTransactional") {
    Procgen& proc = *Procgen::create();
    PointSet first;
    first.add(3.f, 0.f, 4.f);

    auto initial = requireContext(proc, Procgen::beginSystemHandle("village", 11));
    auto initialView = initial.view();
    REQUIRE(initialView.isBound());
    CHECK(initialView->cacheStage("lots", "lots-v1:size=16", &first));
    CHECK(initialView->publish("lots", &first));
    auto initialCommit = proc.commitSystem(initial.handle);
    CHECK(initialCommit.ok());

    auto rebuild = requireContext(proc, Procgen::beginSystemHandle("village", 11));
    auto rebuildView = rebuild.view();
    REQUIRE(rebuildView.isBound());
    PointSet* reused = rebuildView->reuseStage("lots", "lots-v1:size=16");
    REQUIRE(bool(reused));
    CHECK_EQ(reused->getCount(), 1);
    CHECK_EQ(rebuildView->getStageCacheHitCount(), 1);
    CHECK(!rebuildView->reuseStage("lots", "lots-v1:size=32"));
    CHECK_EQ(rebuildView->getStageCacheMissCount(), 1);

    PointSet replacement;
    replacement.add(8.f, 0.f, 8.f);
    replacement.add(9.f, 0.f, 9.f);
    CHECK(rebuildView->cacheStage("lots", "lots-v1:size=32", &replacement));
    rebuildView->fail("downstream failed");
    auto rebuildCommit = proc.commitSystem(rebuild.handle);
    CHECK(!rebuildCommit.ok());

    auto retry = requireContext(proc, Procgen::beginSystemHandle("village", 11));
    auto retryView = retry.view();
    REQUIRE(retryView.isBound());
    PointSet* preserved = retryView->reuseStage("lots", "lots-v1:size=16");
    REQUIRE(bool(preserved));
    CHECK_EQ(preserved->getCount(), 1);
    CHECK(!retryView->reuseStage("lots", "lots-v1:size=32"));
}

TEST_CASE("procgen.system.automaticTraceTimingIsBalanced") {
    Procgen  proc;
    PointSet points;
    points.add(1.f, 0.f, 1.f);

    auto measured = requireContext(proc, Procgen::beginSystemHandle("timed", 5));
    auto measuredView = measured.view();
    REQUIRE(measuredView.isBound());
    CHECK(measuredView->beginTrace("outer", 0));
    CHECK(measuredView->beginTrace("inner", 1));
    CHECK_EQ(measuredView->getOpenTraceCount(), 2);
    CHECK(measuredView->endTrace(1));
    CHECK(measuredView->endTrace(2));
    CHECK_EQ(measuredView->getOpenTraceCount(), 0);
    CHECK_EQ(measuredView->getTraceCount(), 2);
    CHECK_EQ(measuredView->getTraceName(0), std::string("inner"));
    CHECK(measuredView->getTraceMilliseconds(0) >= 0.f);
    CHECK(measuredView->publish("points", &points));
    auto measuredCommit = proc.commitSystem(measured.handle);
    CHECK(measuredCommit.ok());

    auto unfinished = requireContext(proc, Procgen::beginSystemHandle("timed", 5));
    auto unfinishedView = unfinished.view();
    REQUIRE(unfinishedView.isBound());
    CHECK(unfinishedView->beginTrace("forgotten", 1));
    CHECK(unfinishedView->publish("points", &points));
    auto unfinishedCommit = proc.commitSystem(unfinished.handle);
    CHECK(!unfinishedCommit.ok());
    const eve::Diagnostic* unfinishedDiagnostic = unfinishedCommit.error();
    REQUIRE(unfinishedDiagnostic != nullptr);
    CHECK_EQ(unfinishedDiagnostic->code(), eve::DiagnosticCode::PreconditionViolation);
    CHECK_EQ(proc.getSystemRevision("timed"), uint64_t(1));

    auto unmatched = requireContext(proc, Procgen::beginSystemHandle("unmatched", 5));
    auto unmatchedView = unmatched.view();
    REQUIRE(unmatchedView.isBound());
    CHECK(!unmatchedView->endTrace(0));
    CHECK_EQ(unmatchedView->getError(), std::string("endTrace: no stage timer is active"));
}

TEST_CASE("procgen.system.previousRevisionSupportsHotReloadDiffs") {
    Procgen& proc = *Procgen::create();
    PointSet first;
    first.add(1.f, 0.f, 1.f);
    auto revision1 = requireContext(proc, Procgen::beginSystemHandle("diff", 1));
    auto revision1View = revision1.view();
    REQUIRE(revision1View.isBound());
    CHECK(revision1View->captureDebug("trees", &first));
    auto revision1Commit = proc.commitSystem(revision1.handle);
    CHECK(revision1Commit.ok());
    CHECK_EQ(proc.getPreviousSystemRevision("diff"), uint64_t(0));
    auto missingPrevious = proc.getPreviousSystemDebugStageHandle("diff", "trees");
    CHECK(!missingPrevious.ok());
    const eve::Diagnostic* missingDiagnostic = missingPrevious.error();
    REQUIRE(missingDiagnostic != nullptr);
    CHECK_EQ(missingDiagnostic->code(), eve::DiagnosticCode::NotFound);

    PointSet second = first;
    second.add(2.f, 0.f, 2.f);
    auto revision2 = requireContext(proc, Procgen::beginSystemHandle("diff", 2));
    auto revision2View = revision2.view();
    REQUIRE(revision2View.isBound());
    CHECK(revision2View->captureDebug("trees", &second));
    CHECK(revision2View->captureDebug("roads", &first));
    auto revision2Commit = proc.commitSystem(revision2.handle);
    CHECK(revision2Commit.ok());
    CHECK_EQ(proc.getPreviousSystemRevision("diff"), uint64_t(1));
    auto previousTrees = requirePointSet(proc, proc.getPreviousSystemDebugStageHandle("diff", "trees"));
    auto previousTreesView = previousTrees.view();
    REQUIRE(previousTreesView.isBound());
    CHECK_EQ(previousTreesView->getCount(), 1);
    const std::string report = proc.getSystemDebugDiffReport("diff");
    CHECK(report.find("revision=1 -> 2") != std::string::npos);
    CHECK(report.find("debug trees points=2 delta=+1") != std::string::npos);
    CHECK(report.find("debug roads points=1 delta=+1") != std::string::npos);

    auto failed = requireContext(proc, Procgen::beginSystemHandle("diff", 3));
    auto failedView = failed.view();
    REQUIRE(failedView.isBound());
    failedView->fail("broken reload");
    auto failedCommit = proc.commitSystem(failed.handle);
    CHECK(!failedCommit.ok());
    CHECK_EQ(proc.getPreviousSystemRevision("diff"), uint64_t(1));
    auto removeResult = proc.removeSystem("diff");
    CHECK(removeResult.ok());
    CHECK_EQ(proc.getPreviousSystemRevision("diff"), uint64_t(0));

}
