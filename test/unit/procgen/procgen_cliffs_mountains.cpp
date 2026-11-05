// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "procgen_cliffs_mountains.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_cliffs_mountains)
#include "procgen/MeshBuild.h"
#include "procgen/PointSet.h"
#include "procgen/heightmap/CliffBandMesh.h"
#include "procgen/heightmap/CliffDressing.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/MountainRecipe.h"
#include "procgen/heightmap/TerrainObjectPlacement.h"

#include <cmath>
#include <string>
#include <vector>

namespace test_procgen_cliffs_mountains {
using namespace eve::procgen;

TEST_CASE("procgen.mountain.recipe_replaysSeedAndWritesCliffMask") {
    MountainRecipeSettings settings;
    settings.preset              = MountainPreset::AlpineRidge;
    settings.width               = 33;
    settings.height              = 33;
    settings.seed                = 20261105u;
    settings.spacingX            = 1.f;
    settings.spacingZ            = 1.f;
    settings.heightScale         = 24.f;
    settings.frequencyOverride   = 1.f / 18.f;  // Steeper local gradients on a small map.
    settings.ridgeOverride       = 0.7f;
    settings.cliffSlopeMin       = 0.18f;
    settings.thermalIterations   = 2;
    settings.hydraulicIterations = 1;
    settings.fluvialIterations   = 1;

    Heightmap first, replay, slope, cliff;
    REQUIRE(bakeMountainRecipe(first, settings, {&slope, &cliff}).ok());
    REQUIRE(bakeMountainRecipe(replay, settings, {}).ok());
    CHECK_EQ(first.getWidth(), 33);
    CHECK_EQ(first.getHeight(), 33);
    CHECK_EQ(first.data(), replay.data());
    CHECK_EQ(slope.getWidth(), 33);
    CHECK_EQ(cliff.getWidth(), 33);
    float minH = first.height(0, 0), maxH = minH, maxSlope = 0.f;
    int   cliffSamples = 0;
    for (float h : first.data()) {
        minH = std::min(minH, h);
        maxH = std::max(maxH, h);
    }
    for (float s : slope.data()) maxSlope = std::max(maxSlope, s);
    for (float c : cliff.data())
        if (c > 0.05f) ++cliffSamples;
    CHECK(maxH > minH);
    CHECK(maxSlope > settings.cliffSlopeMin);
    CHECK(cliffSamples > 0);
}

TEST_CASE("procgen.cliff.band_flatIsEmpty_stepEmitsWall_replayStable") {
    CliffBandMeshSettings settings;
    settings.spacingX     = 1.f;
    settings.spacingZ     = 1.f;
    settings.heightScale  = 1.f;
    settings.slopeMin     = 0.35f;
    settings.subdivisions = 0;
    settings.seed         = 7;

    Heightmap flat(8, 8);
    flat.data().assign(64, 0.2f);
    MeshBuild empty;
    auto      emptyResult = generateCliffBandMesh(empty, flat, settings, nullptr);
    REQUIRE(emptyResult.ok());
    CHECK_EQ(emptyResult.value(), 0);
    CHECK(empty.empty());

    Heightmap step(8, 8);
    for (int z = 0; z < 8; ++z)
        for (int x = 0; x < 8; ++x) step.setHeight(x, z, x < 4 ? 0.f : 6.f);
    MeshBuild first, replay;
    auto      a = generateCliffBandMesh(first, step, settings, nullptr);
    auto      b = generateCliffBandMesh(replay, step, settings, nullptr);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(a.value() > 0);
    CHECK_EQ(a.value(), b.value());
    CHECK_EQ(first.getVertexCount(), replay.getVertexCount());
    CHECK_EQ(first.positions(), replay.positions());
    CHECK_EQ(first.getMeta("algorithm", ""), std::string("mesh.cliffband"));
    // Wall strips must span the terrace relief and face horizontally (±X), not
    // lie coplanar with the heightfield slope.
    float minY = first.positions()[1], maxY = minY;
    for (size_t i = 1; i < first.positions().size(); i += 3) {
        minY = std::min(minY, first.positions()[i]);
        maxY = std::max(maxY, first.positions()[i]);
    }
    CHECK(maxY - minY > 5.f);
    const auto& normals = first.normals();
    REQUIRE(normals.size() >= 3);
    float avgAbsNy = 0.f;
    int   countN   = 0;
    for (size_t i = 1; i < normals.size(); i += 3) {
        avgAbsNy += std::abs(normals[i]);
        ++countN;
    }
    CHECK(countN > 0);
    avgAbsNy /= float(countN);
    CHECK(avgAbsNy < 0.35f);
}

TEST_CASE("procgen.cliff.dressing_configuresRockScatterDefaults") {
    TerrainObjectPlacementSettings placement;
    auto                           configured =
        configureCliffRockScatterDefaults(placement, 0.f, 0.f, 32.f, 32.f, 1.f, 11u, 8.f, 0.5f, "mesh.rock:cliff");
    REQUIRE(configured.ok());
    CHECK_EQ(configured.value(), 1);
    CHECK_EQ(placement.instances.size(), static_cast<size_t>(1));
    CHECK_EQ(placement.instances[0].asset, std::string("mesh.rock:cliff"));
    CHECK(placement.instances[0].rotateToSlope);

    Heightmap fitness(17, 17), heights(17, 17);
    for (int z = 0; z < 17; ++z)
        for (int x = 0; x < 17; ++x) {
            heights.setHeight(x, z, x < 8 ? 0.1f : 0.8f);
            fitness.setHeight(x, z, x >= 7 && x <= 9 ? 0.9f : 0.05f);
        }
    PointSet points;
    auto     exported = exportTerrainObjectPoints(points, fitness, heights, placement);
    REQUIRE(exported.ok());
    CHECK(exported.value() >= 0);
}

}  // namespace test_procgen_cliffs_mountains
#endif
