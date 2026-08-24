#include "procgen/PointSet.h"
#include "procgen/Procgen.h"
#include "procgen/heightmap/Heightmap.h"

#include <zeroerr.hpp>

using namespace eve::procgen;

TEST_CASE("procgen.pointSet.composableDeterministicPipeline") {
    Procgen        proc;
    const uint32_t forestSeed = proc.deriveSeed(42, "forest");
    CHECK_NE(forestSeed, proc.deriveSeed(42, "rocks"));
    CHECK_EQ(forestSeed, proc.deriveSeed(42, "forest"));

    PointSet* source = proc.sampleGrid(8, 6, 2.f, forestSeed, 0.f);
    REQUIRE(bool(source));
    CHECK_EQ(source->getCount(), 48);
    CHECK_EQ(source->getX(1), 2.f);
    CHECK_EQ(source->getZ(8), 2.f);

    for (int i = 0; i < source->getCount(); ++i) {
        source->setPosition(i, source->getX(i), float(i % 4), source->getZ(i));
        source->setDensity(i, float(i % 5) / 4.f);
        source->setStringAttribute(i, "biome", "forest");
    }

    PointSet* height = proc.filterHeight(source, 1.f, 2.f);
    REQUIRE(bool(height));
    CHECK_EQ(height->getCount(), 24);
    PointSet* dense = proc.filterDensity(height, 0.5f, 1.f);
    REQUIRE(bool(dense));
    CHECK(dense->getCount() < height->getCount());
    CHECK(dense->getCount() > 0);
    CHECK_EQ(dense->getStringAttribute(0, "biome", ""), std::string("forest"));

    PointSet* excluded = proc.excludeRadius(dense, 0.f, 0.f, 3.f);
    REQUIRE(bool(excluded));
    PointSet* pruned = proc.selfPrune(excluded, 2.1f);
    REQUIRE(bool(pruned));
    CHECK(pruned->getCount() <= excluded->getCount());

    PointSet* jitterA = proc.jitterPoints(pruned, proc.deriveSeed(forestSeed, "jitter"), 0.5f, 0.5f);
    PointSet* jitterB = proc.jitterPoints(pruned, proc.deriveSeed(forestSeed, "jitter"), 0.5f, 0.5f);
    REQUIRE(bool(jitterA));
    REQUIRE(bool(jitterB));
    CHECK_EQ(jitterA->getCount(), jitterB->getCount());
    for (int i = 0; i < jitterA->getCount(); ++i) {
        CHECK_EQ(jitterA->getX(i), jitterB->getX(i));
        CHECK_EQ(jitterA->getZ(i), jitterB->getZ(i));
    }

    delete jitterB;
    delete jitterA;
    delete pruned;
    delete excluded;
    delete dense;
    delete height;
    delete source;
}

TEST_CASE("procgen.pointSet.invalidInputIsReported") {
    Procgen proc;
    CHECK(!proc.filterHeight(nullptr, 0.f, 1.f));
    CHECK_EQ(proc.lastError(), std::string("filterHeight: null input"));
}

TEST_CASE("procgen.pointSet.spatialFiltersAndHeightmapProjection") {
    Procgen   proc;
    PointSet source;
    source.add(0.f, 0.f, 0.f);
    source.add(1.f, 0.f, 1.f);
    source.add(2.f, 0.f, 2.f);

    PointSet* inside = proc.filterBox(&source, 0.5f, -1.f, 0.5f, 1.5f, 1.f, 1.5f);
    REQUIRE(bool(inside));
    CHECK_EQ(inside->getCount(), 1);
    PointSet* outside = proc.excludeBox(&source, 0.5f, -1.f, 0.5f, 1.5f, 1.f, 1.5f);
    REQUIRE(bool(outside));
    CHECK_EQ(outside->getCount(), 2);

    Heightmap heightmap(3, 3);
    for (int z = 0; z < 3; ++z)
        for (int x = 0; x < 3; ++x) heightmap.setHeight(x, z, float(x));
    PointSet* projected = proc.projectToHeightmap(&source, &heightmap, 0.f, 0.f, 1.f, 2.f);
    REQUIRE(bool(projected));
    CHECK_EQ(projected->getY(0), 0.f);
    CHECK_EQ(projected->getY(1), 2.f);
    CHECK_EQ(projected->getY(2), 4.f);
    CHECK(projected->getNormalX(1) < 0.f);
    CHECK(projected->getNormalY(1) > 0.f);

    PointSet* gentle = proc.filterSlope(projected, 0.f, 80.f);
    REQUIRE(bool(gentle));
    CHECK_EQ(gentle->getCount(), 3);
    PointSet* flat = proc.filterSlope(projected, 0.f, 1.f);
    REQUIRE(bool(flat));
    CHECK_EQ(flat->getCount(), 0);

    delete flat;
    delete gentle;
    delete projected;
    delete outside;
    delete inside;
}

TEST_CASE("procgen.pointSet.polygonAndSplineQueries") {
    Procgen   proc;
    PointSet polygon;
    polygon.add(0.f, 0.f, 0.f);
    polygon.add(10.f, 0.f, 0.f);
    polygon.add(10.f, 0.f, 10.f);
    polygon.add(0.f, 0.f, 10.f);

    PointSet candidates;
    candidates.add(5.f, 0.f, 5.f);
    candidates.add(15.f, 0.f, 5.f);
    PointSet* inside = proc.filterPolygon(&candidates, &polygon);
    PointSet* outside = proc.excludePolygon(&candidates, &polygon);
    REQUIRE(bool(inside));
    REQUIRE(bool(outside));
    CHECK_EQ(inside->getCount(), 1);
    CHECK_EQ(outside->getCount(), 1);
    CHECK_EQ(inside->getX(0), 5.f);
    CHECK_EQ(outside->getX(0), 15.f);

    PointSet control;
    control.add(0.f, 0.f, 0.f);
    control.add(10.f, 0.f, 0.f);
    control.add(10.f, 0.f, 10.f);
    PointSet* samples = proc.sampleSpline(&control, 5.f, 42, 0.f);
    REQUIRE(bool(samples));
    CHECK_EQ(samples->getCount(), 5);
    CHECK_EQ(samples->getX(1), 5.f);
    CHECK_EQ(samples->getZ(3), 5.f);
    CHECK_EQ(samples->getYaw(0), 0.f);
    CHECK_EQ(samples->getYaw(3), 90.f);

    PointSet nearby;
    nearby.add(5.f, 0.f, 2.f);
    nearby.add(5.f, 0.f, 5.f);
    nearby.add(12.f, 0.f, 5.f);
    PointSet* corridor = proc.filterSplineDistance(&nearby, &control, 0.f, 2.1f);
    REQUIRE(bool(corridor));
    CHECK_EQ(corridor->getCount(), 2);

    delete corridor;
    delete samples;
    delete outside;
    delete inside;
}

TEST_CASE("procgen.system.commitIsAtomicAndFailureKeepsPreviousSnapshot") {
    Procgen  proc;
    PointSet first;
    first.add(1.f, 2.f, 3.f);

    ProcgenContext* initial = proc.beginSystem("forest", 42);
    REQUIRE(bool(initial));
    CHECK(initial->publish("trees", &first));
    CHECK(initial->captureDebug("candidates", &first));
    CHECK_EQ(initial->getDebugStageCount(), 1);
    CHECK_EQ(initial->getDebugStageName(0), std::string("candidates"));
    initial->trace("sample", 0, 1, 0.25f);
    CHECK(proc.commitSystem(initial));
    CHECK(proc.hasSystem("forest"));
    CHECK_EQ(proc.getSystemRevision("forest"), uint64_t(1));
    CHECK_EQ(proc.getSystemDebugStageCount("forest"), 1);
    CHECK_EQ(proc.getSystemDebugStageName("forest", 0), std::string("candidates"));

    PointSet* debug = proc.getSystemDebugStage("forest", "candidates");
    REQUIRE(bool(debug));
    CHECK_EQ(debug->getCount(), 1);
    delete debug;

    PointSet* committed = proc.getSystemOutput("forest", "trees");
    REQUIRE(bool(committed));
    CHECK_EQ(committed->getCount(), 1);
    delete committed;

    PointSet replacement;
    replacement.add(9.f, 9.f, 9.f);
    replacement.add(8.f, 8.f, 8.f);
    ProcgenContext* failed = proc.beginSystem("forest", 99);
    REQUIRE(bool(failed));
    CHECK(failed->publish("trees", &replacement));
    failed->fail("script error");
    CHECK(!proc.commitSystem(failed));
    CHECK_EQ(proc.getSystemRevision("forest"), uint64_t(1));
    CHECK_EQ(proc.getSystemSeed("forest"), uint32_t(42));
    CHECK_EQ(proc.getSystemDebugStageCount("forest"), 1);

    committed = proc.getSystemOutput("forest", "trees");
    REQUIRE(bool(committed));
    CHECK_EQ(committed->getCount(), 1);
    CHECK(proc.getSystemDebugReport("forest").find("sample") != std::string::npos);

    delete committed;
    delete failed;
    delete initial;
}

TEST_CASE("procgen.system.cachedBuildReusesCommittedSnapshot") {
    Procgen  proc;
    PointSet points;
    points.add(1.f, 0.f, 2.f);

    ProcgenContext* miss = proc.beginCachedSystem("rocks", 7, "layout-v1:size=32");
    REQUIRE(bool(miss));
    CHECK(!miss->isCacheHit());
    CHECK(miss->isActive());
    CHECK_EQ(miss->getBuildKey(), std::string("layout-v1:size=32"));
    CHECK(miss->publish("rocks", &points));
    CHECK(proc.commitSystem(miss));
    CHECK_EQ(proc.getSystemRevision("rocks"), uint64_t(1));
    CHECK_EQ(proc.getSystemBuildKey("rocks"), std::string("layout-v1:size=32"));

    ProcgenContext* hit = proc.beginCachedSystem("rocks", 7, "layout-v1:size=32");
    REQUIRE(bool(hit));
    CHECK(hit->isCacheHit());
    CHECK(!hit->isActive());
    CHECK_EQ(proc.getSystemRevision("rocks"), uint64_t(1));

    ProcgenContext* changed = proc.beginCachedSystem("rocks", 7, "layout-v1:size=64");
    REQUIRE(bool(changed));
    CHECK(!changed->isCacheHit());
    CHECK(changed->isActive());

    delete changed;
    delete hit;
    delete miss;
}
