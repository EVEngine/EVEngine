#include "procgen/PointSet.h"
#include "procgen/Procgen.h"

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
