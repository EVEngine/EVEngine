#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "procgen/PointSet.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainDetailLayer.h"
#include "procgen/heightmap/TerrainDetailPlacement.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
namespace {
[[maybe_unused]] int countAt(const TerrainDetailLayer& layer, int x, int z = 0) {
    auto count = layer.sample(x, z);
    REQUIRE(count.ok());
    return count.value();
}
}  // namespace
TEST_CASE("procgen.detail.modesThresholdAndNearestEven") {
    Heightmap fitness(5, 1);
    fitness.data() = {0, 0.5F, 0.625F, 0.875F, 1};
    TerrainDetailLayer    layer;
    TerrainDetailSettings settings;
    settings.fadeStart = 0.5F;
    settings.density   = 2;
    REQUIRE(layer.reset(5, 1, 7).ok());
    REQUIRE(layer.apply(fitness, settings, 1).ok());
    // Density 0.5 ties to zero; 1.5 ties to two. Equal threshold is excluded.
    const int expected[] = {0, 0, 0, 2, 2};
    for (int x = 0; x < 5; ++x) CHECK(countAt(layer, x) == expected[x]);
    REQUIRE(layer.reset(5, 1, 7).ok());
    settings.mode = TerrainDetailMode::Add;
    REQUIRE(layer.apply(fitness, settings, 1).ok());
    CHECK(countAt(layer, 0) == 7);
    CHECK(countAt(layer, 1) == 7);
    CHECK(countAt(layer, 2) == 2);
    settings.mode = TerrainDetailMode::Remove;
    REQUIRE(layer.apply(fitness, settings, 1).ok());
    CHECK(countAt(layer, 0) == 7);
    CHECK(countAt(layer, 1) == 7);
    CHECK(countAt(layer, 2) == 2);
    CHECK(countAt(layer, 3) == 0);
    CHECK(countAt(layer, 4) == 0);
}
TEST_CASE("procgen.detail.sourceConditionalRngAndColumnOrder") {
    TerrainDetailLayer    layer;
    TerrainDetailSettings settings;
    settings.minimumFitness = 0;
    settings.fadeStart      = 1;
    settings.density        = 8;
    Heightmap fitness(3, 4);
    std::fill(fitness.data().begin(), fitness.data().end(), 0.5F);
    REQUIRE(layer.reset(3, 4).ok());
    REQUIRE(layer.apply(fitness, settings, 1).ok());
    // X-major order; source seed-one draws accept sequence 0,1,4,9 at threshold 0.5.
    const int expected[] = {4, 4, 0, 4, 0, 4, 0, 0, 0, 0, 0, 0};
    for (int z = 0; z < 4; ++z)
        for (int x = 0; x < 3; ++x) CHECK(countAt(layer, x, z) == expected[z * 3 + x]);
    REQUIRE(layer.apply(fitness, settings, 0).ok());
    for (int z = 0; z < 4; ++z)
        for (int x = 0; x < 3; ++x) CHECK(countAt(layer, x, z) == expected[z * 3 + x]);
    // Neither below-threshold nor full-fitness cells consume a thinning draw.
    Heightmap sparse(4, 1);
    sparse.data() = {0, 1, 0.5F, 0.5F};
    REQUIRE(layer.reset(4, 1).ok());
    REQUIRE(layer.apply(sparse, settings, 1).ok());
    CHECK(countAt(layer, 0) == 0);
    CHECK(countAt(layer, 1) == 8);
    CHECK(countAt(layer, 2) == 4);
    CHECK(countAt(layer, 3) == 4);
}
TEST_CASE("procgen.detail.failedOperationAndMovePreserveCounts") {
    TerrainDetailLayer    layer;
    TerrainDetailSettings settings;
    Heightmap             fitness(2, 1);
    fitness.data() = {1, std::numeric_limits<float>::infinity()};
    REQUIRE(layer.reset(2, 1, 3).ok());
    CHECK(!layer.apply(fitness, settings, 1).ok());
    CHECK(countAt(layer, 0) == 3);
    CHECK(countAt(layer, 1) == 3);
    fitness.data()[1] = 1;
    settings.density  = float(std::numeric_limits<int>::max());
    CHECK(!layer.apply(fitness, settings, 1).ok());
    CHECK(countAt(layer, 0) == 3);
    CHECK(!layer.reset(std::numeric_limits<int>::max(), 2, 0).ok());
    settings.density = 16;
    settings.mode    = static_cast<TerrainDetailMode>(99);
    CHECK(!layer.apply(fitness, settings, 1).ok());
    TerrainDetailLayer moved = std::move(layer);
    CHECK(layer.getWidth() == 0);
    CHECK(countAt(moved, 0) == 3);
    CHECK(!layer.sample(0, 0).ok());
    REQUIRE(layer.reset(1, 1).ok());
}


TEST_CASE("procgen.detail.pointsResourceProjectionAndStableIdentity") {
    TerrainDetailLayer layer;
    REQUIRE(layer.reset(2, 1, 2).ok());
    Heightmap heights(2, 2);
    heights.data() = {0, 2, 4, 6};
    TerrainDetailPlacementSettings settings;
    settings.asset       = "grass/test";
    settings.width       = 2;
    settings.depth       = 2;
    settings.heightScale = 3;
    PointSet points;
    REQUIRE(exportTerrainDetailPoints(points, layer, heights, settings).ok());
    REQUIRE(points.getCount() == 4);
    for (int i = 0; i < 4; ++i) {
        const auto& p = points.points()[i];
        CHECK(p.id != 0);
        CHECK(p.x >= 0);
        CHECK(p.x < 2);
        CHECK(p.z >= 0);
        CHECK(p.z < 2);
        CHECK(std::abs(p.y - (p.x + 2 * p.z) * 3) < 0.00001F);
        CHECK(points.getStringAttribute(i, "asset", "") == "grass/test");
        CHECK(points.getIntAttribute(i, "detailCell", -1) == i / 2);
        CHECK(points.getIntAttribute(i, "detailOrdinal", -1) == i % 2);
    }
    Heightmap fitness(2, 1);
    fitness.data() = {1, 0};
    TerrainDetailSettings density;
    density.mode    = TerrainDetailMode::Add;
    density.density = 3;
    REQUIRE(layer.apply(fitness, density, 1).ok());
    PointSet changed;
    REQUIRE(exportTerrainDetailPoints(changed, layer, heights, settings).ok());
    REQUIRE(changed.getCount() == 5);
    CHECK(changed.points()[3].id == points.points()[2].id);
    CHECK(changed.points()[3].x == points.points()[2].x);
    CHECK(changed.points()[3].z == points.points()[2].z);
    CHECK(changed.points()[3].yaw == points.points()[2].yaw);
    settings.maxPoints = 4;
    CHECK(!exportTerrainDetailPoints(changed, layer, heights, settings).ok());
    CHECK(changed.getCount() == 5);
    settings.maxPoints = 10;
    heights.data()[3]  = std::numeric_limits<float>::infinity();
    CHECK(!exportTerrainDetailPoints(changed, layer, heights, settings).ok());
    CHECK(changed.getCount() == 5);
}


TEST_CASE("procgen.detail.independentDimensionsPreservePlacement") {
    TerrainDetailLayer layer;
    REQUIRE(layer.reset(1, 1, 12).ok());
    Heightmap                      heights(1, 1);
    TerrainDetailPlacementSettings settings;
    settings.asset = "grass/test";
    PointSet before, after;
    REQUIRE(exportTerrainDetailPoints(before, layer, heights, settings).ok());
    settings.minimumWidth  = 0.2F;
    settings.maximumWidth  = 0.4F;
    settings.minimumHeight = 2;
    settings.maximumHeight = 3;
    REQUIRE(exportTerrainDetailPoints(after, layer, heights, settings).ok());
    for (int i = 0; i < 12; ++i) {
        CHECK(after.getPointId(i) == before.getPointId(i));
        CHECK(after.getX(i) == before.getX(i));
        CHECK(after.getZ(i) == before.getZ(i));
        CHECK(after.getScaleX(i) >= 0.2F);
        CHECK(after.getScaleX(i) <= 0.4F);
        CHECK(after.getScaleZ(i) == after.getScaleX(i));
        CHECK(after.getScaleY(i) >= 2);
        CHECK(after.getScaleY(i) <= 3);
    }
    settings.maximumWidth = 0.1F;
    CHECK(!exportTerrainDetailPoints(after, layer, heights, settings).ok());
    CHECK(after.getCount() == 12);
    settings.minimumWidth = std::numeric_limits<float>::max();
    settings.maximumWidth = settings.minimumWidth;
    settings.minimumScale = 2;
    settings.maximumScale = 2;
    CHECK(!exportTerrainDetailPoints(after, layer, heights, settings).ok());
    CHECK(after.getScaleX(0) < 1);
}

TEST_CASE("procgen.detail.healthyDryColorNoiseIsStableAndAtomic") {
    TerrainDetailLayer layer;
    REQUIRE(layer.reset(2, 2, 3).ok());
    Heightmap                      heights(1, 1);
    TerrainDetailPlacementSettings settings;
    settings.asset       = "grass/color";
    settings.healthyR    = 0.8F;
    settings.healthyG    = 1.0F;
    settings.healthyB    = 0.4F;
    settings.dryR        = 0.2F;
    settings.dryG        = 0.3F;
    settings.dryB        = 0.1F;
    settings.noiseSpread = 0.7F;
    settings.noiseSeed   = -17;
    PointSet first, repeat;
    REQUIRE(exportTerrainDetailPoints(first, layer, heights, settings).ok());
    REQUIRE(exportTerrainDetailPoints(repeat, layer, heights, settings).ok());
    REQUIRE(first.getCount() == 12);
    bool varied = false;
    for (int i = 0; i < first.getCount(); ++i) {
        CHECK(first.getPointId(i) == repeat.getPointId(i));
        CHECK(first.getColorR(i) == repeat.getColorR(i));
        CHECK(first.getColorG(i) == repeat.getColorG(i));
        CHECK(first.getColorB(i) == repeat.getColorB(i));
        CHECK(first.getColorR(i) >= settings.dryR);
        CHECK(first.getColorR(i) <= settings.healthyR);
        if (i > 0 && first.getColorR(i) != first.getColorR(0)) varied = true;
    }
    CHECK(varied);
    PointSet recolored;
    settings.noiseSpread = 0;
    REQUIRE(exportTerrainDetailPoints(recolored, layer, heights, settings).ok());
    for (int i = 0; i < recolored.getCount(); ++i) {
        CHECK(recolored.getPointId(i) == first.getPointId(i));
        CHECK(recolored.getX(i) == first.getX(i));
        CHECK(recolored.getZ(i) == first.getZ(i));
        CHECK(std::abs(recolored.getColorR(i) - 0.5F) < 0.000001F);
        CHECK(std::abs(recolored.getColorG(i) - 0.65F) < 0.000001F);
    }
    const auto before = recolored.getColorR(0);
    settings.healthyR = 1.1F;
    CHECK(!exportTerrainDetailPoints(recolored, layer, heights, settings).ok());
    CHECK(recolored.getCount() == 12);
    CHECK(recolored.getColorR(0) == before);
}
