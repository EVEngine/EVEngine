#include "procgen/Biome.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;

TEST_CASE("procgen.biome.appliesPriorityExclusionAndAssetMetadata") {
    BiomeRules rules;
    SpatialData domain = SpatialData::box(0.f, 0.f, 0.f, 4.f, 0.f, 4.f);
    SpatialData forest = domain;
    SpatialData town   = SpatialData::sphere(2.f, 0.f, 2.f, 1.1f);
    SpatialData noSpawn = SpatialData::sphere(0.f, 0.f, 0.f, 0.5f);
    CHECK(rules.addLayer("forest", &forest, 1, 1.f));
    CHECK(rules.addLayer("town", &town, 10, 1.f));
    CHECK(!rules.addLayer("forest", &forest, 2, 1.f));
    CHECK(rules.addAsset("forest", "oak", 3.f, 0.8f, 1.2f, true));
    CHECK(rules.addAsset("forest", "pine", 1.f, 1.f, 1.f, true));
    CHECK(rules.addAsset("town", "lamp", 1.f, 1.f, 1.f, false));
    CHECK(rules.addExclusion(&noSpawn));

    PointSet* first = rules.generate(&domain, 2.f, 42, 0.f);
    PointSet* second = rules.generate(&domain, 2.f, 42, 0.f);
    REQUIRE(bool(first));
    REQUIRE(bool(second));
    CHECK_EQ(first->getCount(), 8);
    CHECK_EQ(second->getCount(), first->getCount());
    int townCount = 0;
    for (int i = 0; i < first->getCount(); ++i) {
        CHECK_EQ(first->getStringAttribute(i, "asset", ""),
                 second->getStringAttribute(i, "asset", ""));
        CHECK_EQ(first->getYaw(i), second->getYaw(i));
        CHECK_EQ(first->getScaleX(i), second->getScaleX(i));
        if (first->getStringAttribute(i, "biome", "") == "town") {
            ++townCount;
            CHECK_EQ(first->getStringAttribute(i, "asset", ""), std::string("lamp"));
            CHECK_EQ(first->getYaw(i), 0.f);
        }
    }
    CHECK_EQ(townCount, 1);
    CHECK(rules.debugReport().find("output=8") != std::string::npos);

    delete second;
    delete first;
}

TEST_CASE("procgen.biome.validatesRulesAndEmptyDensity") {
    BiomeRules rules;
    SpatialData domain = SpatialData::box(0.f, 0.f, 0.f, 2.f, 0.f, 2.f);
    CHECK(!rules.generate(&domain, 1.f, 1, 0.f));
    CHECK(rules.getError().find("no biome layers") != std::string::npos);
    CHECK(rules.addLayer("empty", &domain, 0, 0.f));
    CHECK(!rules.addAsset("missing", "asset", 1.f, 1.f, 1.f, true));
    CHECK(rules.addAsset("empty", "asset", 1.f, 1.f, 1.f, true));
    PointSet* result = rules.generate(&domain, 1.f, 1, 0.f);
    REQUIRE(bool(result));
    CHECK_EQ(result->getCount(), 0);
    delete result;
}
