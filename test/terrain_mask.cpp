#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainEffect.h"
#include "procgen/heightmap/TerrainImageMask.h"
#include "procgen/heightmap/TerrainCollisionMask.h"
#include "procgen/heightmap/TerrainBakedMaskCache.h"
#include "procgen/heightmap/TerrainPolygonMask.h"
#include "procgen/heightmap/TerrainNoiseMask.h"
#include "procgen/heightmap/TerrainMask.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

using namespace eve::procgen;

TEST_CASE("procgen.mask.polygonOpenClosedBrushAndAtomicFailure") {
    TerrainPolygonMask polygon;
    REQUIRE(polygon.addNode(1, 1, 0.5F, 0.25F).ok());
    REQUIRE(polygon.addNode(3, 1, 0.5F, 0.5F).ok());
    REQUIRE(polygon.addNode(3, 3, 0.5F, 0.75F).ok());
    REQUIRE(polygon.addNode(1, 3, 0.5F, 1.0F).ok());
    Heightmap output(5, 5), zeroBrush(1, 1);
    TerrainSampleGrid grid;
    grid.width = grid.height = 5;
    REQUIRE(polygon.rasterize(output, zeroBrush, grid, TerrainPolygonMaskType::Open).ok());
    CHECK(output.data()[12] == 0.0F);
    REQUIRE(polygon.rasterize(output, zeroBrush, grid, TerrainPolygonMaskType::Closed).ok());
    CHECK(output.data()[12] == 1.0F);

    polygon.clear();
    REQUIRE(polygon.addNode(2, 2, 1.0F, 0.0F).ok());
    Heightmap brush(1, 1);
    brush.data()[0] = 1.0F;
    REQUIRE(polygon.rasterize(output, brush, grid, TerrainPolygonMaskType::Open).ok());
    CHECK(std::count(output.data().begin(), output.data().end(), 1.0F) == 9);
    auto before = output.data();
    grid.spacingX = 0;
    CHECK(!polygon.rasterize(output, brush, grid, TerrainPolygonMaskType::Open).ok());
    CHECK(output.data() == before);
    CHECK(polygon.getNodeCount() == 1);
}

TEST_CASE("procgen.mask.worldBiomeCacheIdentityDirtyRebuildAndResample") {
    TerrainBakedMaskCache cache;
    Heightmap west(2, 2), east(1, 1), output(1, 1);
    west.data() = {0, 1, 1, 0};
    east.data()[0] = 0.75F;
    output.data()[0] = 9.0F;
    REQUIRE(cache.store("west", "biome-guid", west).value() == 1);
    REQUIRE(cache.store("east", "biome-guid", east).value() == 2);
    REQUIRE(cache.copyMask("west", "biome-guid", output).ok());
    CHECK(output.data()[0] == 0.5F);
    REQUIRE(cache.markDirty("biome-guid").value() == 2);
    output.data()[0] = 7.0F;
    CHECK(!cache.copyMask("west", "biome-guid", output).ok());
    CHECK(output.data()[0] == 7.0F);
    REQUIRE(cache.store("west", "biome-guid", west).value() == 2);
    REQUIRE(cache.copyMask("west", "biome-guid", output).ok());
    CHECK(!cache.copyMask("east", "biome-guid", output).ok());
    REQUIRE(cache.erase("east", "biome-guid").value() == 1);
    CHECK(cache.getEntryCount() == 1);
    CHECK(!cache.erase("east", "biome-guid").ok());
    cache.clear();
    CHECK(cache.getEntryCount() == 0);
}

TEST_CASE("procgen.mask.worldContextAlignsHeightSampleCenters") {
    TerrainStampSettings world;
    world.originX  = 0.5;
    world.spacingX = 0.5;
    world.width    = 100;
    world.depth    = 100;
    TerrainSampleGrid grid;
    grid.width = 5;
    TerrainBrushBlendSettings mapping;
    REQUIRE(configureTerrainBrushWorld(mapping, world, 3, 1, grid).ok());
    Heightmap oldHeights(5, 1), newHeights(1, 1), brush(1, 1), output(3, 1);
    oldHeights.data() = {0, 10, 20, 30, 40};
    REQUIRE(blendTerrainBrush(output, oldHeights, newHeights, brush, mapping).ok());
    CHECK(std::abs(output.data()[0] - 5) < 1e-5F);
    CHECK(std::abs(output.data()[1] - 10) < 1e-5F);
    CHECK(std::abs(output.data()[2] - 15) < 1e-5F);
}

TEST_CASE("procgen.mask.worldContextRebasedRotationAndAtomicFailure") {
    TerrainStampSettings world;
    world.centerX  = 1;
    world.centerZ  = 1;
    world.width    = 4;
    world.depth    = 4;
    world.rotation = std::numbers::pi / 2;
    TerrainSampleGrid grid;
    grid.width  = 3;
    grid.height = 3;
    TerrainBrushBlendSettings local, translated;
    local.strength      = 0.25F;
    translated.strength = 0.25F;
    REQUIRE(configureTerrainBrushWorld(local, world, 3, 3, grid).ok());
    const double u = 1.0 / 6, v = 5.0 / 6;
    CHECK(std::abs(local.brushXX * u + local.brushXZ * v + local.brushOffsetX - 0.75) < 1e-6);
    CHECK(std::abs(local.brushZX * u + local.brushZZ * v + local.brushOffsetZ - 0.75) < 1e-6);
    world.originX += 1e12;
    world.centerX += 1e12;
    grid.originX += 1e12;
    world.originZ -= 1e12;
    world.centerZ -= 1e12;
    grid.originZ -= 1e12;
    REQUIRE(configureTerrainBrushWorld(translated, world, 3, 3, grid).ok());
    CHECK(translated.brushOffsetX == local.brushOffsetX);
    CHECK(translated.brushOffsetZ == local.brushOffsetZ);
    CHECK(translated.heightOffsetX == local.heightOffsetX);
    CHECK(translated.strength == 0.25F);
    world.width = 0;
    CHECK(!configureTerrainBrushWorld(translated, world, 3, 3, grid).ok());
    CHECK(translated.brushOffsetX == local.brushOffsetX);
}

TEST_CASE("procgen.mask.imageChannelsFootprintCurveAndAlias") {
    Heightmap input(1, 1), r(1, 1), g(1, 1), b(1, 1), a(1, 1), curve(8, 1), output(1, 1);
    input.data() = {1};
    r.data()     = {0.25F};
    g.data()     = {0.5F};
    b.data()     = {0.75F};
    a.data()     = {0.375F};
    for (int i = 0; i < 8; ++i) curve.data()[i] = (i + 0.5F) / 8;
    TerrainImageMaskSettings s;
    const int                filters[]  = {0, 2, 3, 4, 5};
    const float              expected[] = {0.75F, 0.25F, 0.5F, 0.75F, 0.375F};
    for (int i = 0; i < 5; ++i) {
        s.filter = static_cast<TerrainImageFilter>(filters[i]);
        REQUIRE(generateTerrainImageMask(output, input, r, g, b, a, curve, s, TerrainMaskBlend::Multiply).ok());
        CHECK(output.data()[0] == expected[i]);
    }
    s.offsetX = 2;
    REQUIRE(generateTerrainImageMask(output, input, r, g, b, a, curve, s, TerrainMaskBlend::Multiply).ok());
    CHECK(output.data()[0] == 0.0625F);
    s.tiling = true;
    s.filter = TerrainImageFilter::Red;
    REQUIRE(generateTerrainImageMask(r, input, r, g, b, a, curve, s, TerrainMaskBlend::Multiply).ok());
    CHECK(r.data()[0] == 0.25F);
}

TEST_CASE("procgen.mask.imageColorAccuracyStrictBoundaryAndFailure") {
    Heightmap input(1, 1), r(1, 1), g(1, 1), b(1, 1), a(1, 1), curve(2, 1), output(1, 1);
    input.data() = {1};
    r.data()     = {1};
    g.data()     = {1};
    b.data()     = {1};
    a.data()     = {1};
    curve.data() = {0, 1};
    TerrainImageMaskSettings s;
    s.filter   = TerrainImageFilter::ColorSelection;
    s.accuracy = 0.5F;
    REQUIRE(generateTerrainImageMask(output, input, r, g, b, a, curve, s, TerrainMaskBlend::Multiply).ok());
    CHECK(output.data()[0] == 1);
    s.accuracy = 1;
    REQUIRE(generateTerrainImageMask(output, input, r, g, b, a, curve, s, TerrainMaskBlend::Multiply).ok());
    CHECK(output.data()[0] == 0);
    s.accuracy  = 0.5F;
    r.data()[0] = 0;
    g.data()[0] = 0;
    b.data()[0] = 0;
    REQUIRE(generateTerrainImageMask(output, input, r, g, b, a, curve, s, TerrainMaskBlend::Multiply).ok());
    CHECK(output.data()[0] == 0);
    s.scaleX = 0;
    CHECK(!generateTerrainImageMask(output, input, r, g, b, a, curve, s, TerrainMaskBlend::Multiply).ok());
    CHECK(output.data()[0] == 0);
}

TEST_CASE("procgen.mask.imageLabGrayDistanceAndFiveBlendModes") {
    Heightmap input(1, 1), gray(1, 1), curve(16, 1), output(1, 1);
    input.data() = {1};
    gray.data()  = {0.5F};
    for (int i = 0; i < 16; ++i) curve.data()[i] = (i + 0.5F) / 16;
    TerrainImageMaskSettings s;
    s.filter   = TerrainImageFilter::ColorSelection;
    s.accuracy = 0.5F;
    REQUIRE(generateTerrainImageMask(output, input, gray, gray, gray, gray, curve, s, TerrainMaskBlend::Multiply).ok());
    CHECK(std::abs(output.data()[0] - 0.766945F) < 0.00002F);
    s.filter               = TerrainImageFilter::Red;
    input.data()[0]        = 0.25F;
    const float expected[] = {0.125F, 0.5F, 0.25F, 0.75F, -0.25F};
    for (int mode = 0; mode < 5; ++mode) {
        REQUIRE(generateTerrainImageMask(output, input, gray, gray, gray, gray, curve, s,
                                         static_cast<TerrainMaskBlend>(mode))
                    .ok());
        CHECK(output.data()[0] == expected[mode]);
    }
}

TEST_CASE("procgen.mask.concavityBowlSignBorderAndIndexedCurve") {
    Heightmap heights(9, 9), input(9, 9), curve(5, 1), output(9, 9);
    for (int z = 0; z < 9; ++z)
        for (int x = 0; x < 9; ++x) heights.data()[z * 9 + x] = float((x - 4) * (x - 4) + (z - 4) * (z - 4));
    std::fill(input.data().begin(), input.data().end(), 1.0F);
    curve.data() = {0.1F, 0.2F, 0.3F, 0.4F, 0.5F};
    TerrainConcavitySettings s;
    s.featureSize = 1;
    s.concavity   = 0.25F;
    REQUIRE(generateTerrainConcavityMask(output, input, heights, curve, s, TerrainMaskBlend::Multiply).ok());
    CHECK(output.data()[40] == 0.2F);
    CHECK(output.data()[0] == 0.1F);
    s.concavity = -0.25F;
    REQUIRE(generateTerrainConcavityMask(output, input, heights, curve, s, TerrainMaskBlend::Multiply).ok());
    CHECK(output.data()[40] == 0.1F);
    for (auto& h : heights.data()) h = -h;
    REQUIRE(generateTerrainConcavityMask(heights, input, heights, curve, s, TerrainMaskBlend::Multiply).ok());
    CHECK(heights.data()[40] == 0.2F);
}

TEST_CASE("procgen.mask.concavityFlatModesAndInvalidAreAtomic") {
    Heightmap heights(2, 2), input(2, 2), curve(1, 1), output(2, 2);
    std::fill(input.data().begin(), input.data().end(), 0.25F);
    curve.data() = {0.5F};
    TerrainConcavitySettings s;
    const float              expected[] = {0.125F, 0.5F, 0.25F, 0.75F, -0.25F};
    for (int mode = 0; mode < 5; ++mode) {
        REQUIRE(
            generateTerrainConcavityMask(output, input, heights, curve, s, static_cast<TerrainMaskBlend>(mode)).ok());
        CHECK(output.data()[0] == expected[mode]);
    }
    const auto original = output.data();
    s.featureSize       = 0;
    CHECK(!generateTerrainConcavityMask(output, input, heights, curve, s, TerrainMaskBlend::Multiply).ok());
    CHECK(output.data() == original);
}

TEST_CASE("procgen.mask.concavityResolutionMappingUsesWidthRatio") {
    Heightmap heights(18, 18), fullInput(18, 18), smallInput(9, 6), full(18, 18), small(9, 6), curve(8, 1);
    for (int z = 0; z < 18; ++z)
        for (int x = 0; x < 18; ++x) heights.data()[z * 18 + x] = float((x - 8) * (x - 8) + (z - 8) * (z - 8));
    std::fill(fullInput.data().begin(), fullInput.data().end(), 1.0F);
    std::fill(smallInput.data().begin(), smallInput.data().end(), 1.0F);
    for (int i = 0; i < 8; ++i) curve.data()[i] = float(i) / 7;
    TerrainConcavitySettings s;
    s.featureSize = 1;
    s.concavity   = 0.25F;
    REQUIRE(generateTerrainConcavityMask(full, fullInput, heights, curve, s, TerrainMaskBlend::Multiply).ok());
    REQUIRE(generateTerrainConcavityMask(small, smallInput, heights, curve, s, TerrainMaskBlend::Multiply).ok());
    for (int z = 0; z < 6; ++z)
        for (int x = 0; x < 9; ++x) CHECK(small.height(x, z) == full.height(x * 2, z * 2));
}

TEST_CASE("procgen.mask.curvatureRadialImpulseAndAtomicDomainFailure") {
    Heightmap heights(3, 3), input(3, 3), curve(8, 1), output(3, 3);
    heights.data()[4] = 1;
    std::fill(input.data().begin(), input.data().end(), 1.0F);
    for (int i = 0; i < 8; ++i) curve.data()[i] = (i + 0.5F) / 8;
    TerrainCurvatureSettings s;
    s.radius     = 1.0F / 3;
    s.worldUnits = 1;
    s.intensity  = 1;
    s.steps      = 1;
    s.directions = 4;
    REQUIRE(generateTerrainCurvatureMask(output, input, heights, curve, s, TerrainMaskBlend::Multiply).ok());
    CHECK(std::abs(output.data()[4] - 0.8F) < 1e-6F);
    CHECK(std::abs(output.data()[1] - 0.2F) < 1e-6F);
    CHECK(std::abs(output.data()[3] - 0.2F) < 1e-6F);
    CHECK(output.data()[0] == 0.0625F);
    const auto original = output.data();
    s.intensity         = 0.7F;
    CHECK(!generateTerrainCurvatureMask(output, input, heights, curve, s, TerrainMaskBlend::Multiply).ok());
    CHECK(output.data() == original);
}

TEST_CASE("procgen.mask.curvatureFiveSourceModesAndAlias") {
    Heightmap heights(1, 1), input(1, 1), curve(1, 1), output(1, 1);
    input.data() = {0.25F};
    curve.data() = {0.6F};
    TerrainCurvatureSettings s;
    s.steps                = 1;
    s.directions           = 1;
    const float expected[] = {0.15F, 0.6F, 0.6F, 0.85F, -0.35F};
    for (int mode = 0; mode < 5; ++mode) {
        REQUIRE(
            generateTerrainCurvatureMask(output, input, heights, curve, s, static_cast<TerrainMaskBlend>(mode)).ok());
        CHECK(std::abs(output.data()[0] - expected[mode]) < 1e-6F);
    }
    input.data()[0] = 0.8F;
    curve.data()[0] = 0.3F;
    REQUIRE(generateTerrainCurvatureMask(input, input, heights, curve, s, TerrainMaskBlend::Minimum).ok());
    CHECK(input.data()[0] == 0.8F);
    s.steps      = std::numeric_limits<int>::max();
    s.directions = 2;
    CHECK(!generateTerrainCurvatureMask(input, input, heights, curve, s, TerrainMaskBlend::Minimum).ok());
    CHECK(input.data()[0] == 0.8F);
}

TEST_CASE("procgen.mask.growShrinkRadialGoldenAndZeroCurve") {
    Heightmap source(4, 1), curve(8, 1), output(4, 1);
    source.data() = {0, 1, 1, 0};
    for (int i = 0; i < 8; ++i) curve.data()[i] = (i + 0.5F) / 8;
    REQUIRE(growShrinkTerrainMask(output, source, curve, 0.25F).ok());
    CHECK(std::abs(output.data()[0] - 0.20710678F) < 1e-6F);
    CHECK(output.data()[1] == 0.9375F);
    CHECK(output.data()[2] == 0.9375F);
    CHECK(std::abs(output.data()[3] - 0.20710678F) < 1e-6F);
    REQUIRE(growShrinkTerrainMask(output, source, curve, -0.25F).ok());
    CHECK(output.data()[0] == 0.0625F);
    CHECK(std::abs(output.data()[1] - 0.79289322F) < 1e-6F);
    CHECK(std::abs(output.data()[2] - 0.79289322F) < 1e-6F);
    CHECK(output.data()[3] == 0.0625F);
    REQUIRE(growShrinkTerrainMask(source, source, curve, 0).ok());
    CHECK(source.data() == std::vector<float>({0.0625F, 0.9375F, 0.9375F, 0.0625F}));
}

TEST_CASE("procgen.mask.growShrinkAliasesAndRejectedRadius") {
    Heightmap source(2, 2), curve(2, 1), expected(2, 2);
    source.data() = {0, 0.25F, 0.75F, 1};
    curve.data()  = {0, 1};
    REQUIRE(growShrinkTerrainMask(expected, source, curve, 0.3F).ok());
    REQUIRE(growShrinkTerrainMask(source, source, curve, 0.3F).ok());
    CHECK(source.data() == expected.data());
    const auto previous = source.data();
    CHECK(!growShrinkTerrainMask(source, source, curve, std::numeric_limits<float>::max()).ok());
    CHECK(source.data() == previous);
    CHECK(!growShrinkTerrainMask(source, source, curve, std::numeric_limits<float>::quiet_NaN()).ok());
    CHECK(source.data() == previous);
}

TEST_CASE("procgen.mask.strengthFiveSourcePasses") {
    Heightmap source(1, 1), curve(1, 1), output(1, 1);
    source.data()          = {0.25F};
    curve.data()           = {0.75F};
    const float expected[] = {0.5F, 0.5F, 0.25F, 0.625F, -0.125F};
    for (int mode = 0; mode < 5; ++mode) {
        REQUIRE(applyTerrainStrength(output, source, curve, static_cast<TerrainStrengthMode>(mode), 0.5F, false).ok());
        CHECK(output.data()[0] == expected[mode]);
    }
    REQUIRE(applyTerrainStrength(output, source, curve, TerrainStrengthMode::Replace, 1, true).ok());
    CHECK(output.data()[0] == 0.25F);
    CHECK(!applyTerrainStrength(output, source, curve, static_cast<TerrainStrengthMode>(9), 1, false).ok());
    CHECK(output.data()[0] == 0.25F);
}

TEST_CASE("procgen.mask.erosionCompositionInvertsAfterCurveAndRollsBack") {
    Heightmap oldHeights(1, 1), erosion(1, 1), brush(1, 1), curve(2, 1), output(1, 1);
    oldHeights.data() = {0.2F};
    erosion.data()    = {0.8F};
    brush.data()      = {0.5F};
    curve.data()      = {0.2F, 0.4F};
    output.data()     = {42};
    TerrainBrushBlendSettings spatial;
    REQUIRE(generateTerrainErosionMask(output, oldHeights, erosion, brush, spatial, curve, TerrainStrengthMode::Replace,
                                       1, false)
                .ok());
    CHECK(std::abs(output.data()[0] - 0.7F) < 1e-6F);
    REQUIRE(generateTerrainErosionMask(output, oldHeights, erosion, brush, spatial, curve, TerrainStrengthMode::Replace,
                                       1, true)
                .ok());
    CHECK(std::abs(output.data()[0] - 0.3F) < 1e-6F);
    const auto previous = output.data();
    curve.data()[0]     = std::numeric_limits<float>::quiet_NaN();
    CHECK(!generateTerrainErosionMask(output, oldHeights, erosion, brush, spatial, curve, TerrainStrengthMode::Replace,
                                      1, false)
               .ok());
    CHECK(output.data() == previous);
}

TEST_CASE("procgen.mask.brushAffineSamplingAndAliasing") {
    Heightmap oldHeights(2, 1), newHeights(1, 1), brush(1, 1), output(4, 1);
    oldHeights.data() = {0, 8};
    newHeights.data() = {10};
    brush.data()      = {0.5F};
    TerrainBrushBlendSettings settings;
    settings.brushXX      = 2;
    settings.brushOffsetX = -0.5F;
    REQUIRE(blendTerrainBrush(output, oldHeights, newHeights, brush, settings).ok());
    CHECK(output.data() == std::vector<float>({0, 6, 8, 8}));
    Heightmap original = output, expected = output;
    settings.heightXX      = -1;
    settings.heightOffsetX = 1;
    REQUIRE(blendTerrainBrush(expected, original, newHeights, brush, settings).ok());
    REQUIRE(blendTerrainBrush(output, output, newHeights, brush, settings).ok());
    CHECK(output.data() == expected.data());
}

TEST_CASE("procgen.mask.brushUnclampedStrengthAndAtomicFailure") {
    Heightmap oldHeights(1, 1), newHeights(1, 1), brush(1, 1), output(1, 1);
    oldHeights.data() = {2};
    newHeights.data() = {4};
    brush.data()      = {1};
    TerrainBrushBlendSettings settings;
    settings.strength = 2;
    REQUIRE(blendTerrainBrush(output, oldHeights, newHeights, brush, settings).ok());
    CHECK(output.data()[0] == 6);
    newHeights.data()[0] = std::numeric_limits<float>::max();
    CHECK(!blendTerrainBrush(output, oldHeights, newHeights, brush, settings).ok());
    CHECK(output.data()[0] == 6);
    settings.brushXX = std::numeric_limits<float>::quiet_NaN();
    CHECK(!blendTerrainBrush(output, oldHeights, newHeights, brush, settings).ok());
    CHECK(output.data()[0] == 6);
}

TEST_CASE("procgen.mask.brushRotationAndInclusiveEdges") {
    Heightmap oldHeights(1, 1), newHeights(1, 1), brush(2, 2), output(2, 2);
    newHeights.data() = {8};
    brush.data()      = {0, 0.25F, 0.5F, 1};
    TerrainBrushBlendSettings settings;
    settings.brushXX      = 0;
    settings.brushXZ      = 1;
    settings.brushZX      = -1;
    settings.brushZZ      = 0;
    settings.brushOffsetZ = 1;
    REQUIRE(blendTerrainBrush(output, oldHeights, newHeights, brush, settings).ok());
    CHECK(output.data() == std::vector<float>({4, 0, 8, 2}));
    settings.brushXZ      = 0;
    settings.brushZX      = 0;
    settings.brushOffsetX = 1;
    settings.brushOffsetZ = 1;
    REQUIRE(blendTerrainBrush(output, oldHeights, newHeights, brush, settings).ok());
    CHECK(output.data() == std::vector<float>({8, 8, 8, 8}));
    settings.brushOffsetX = 1.01F;
    REQUIRE(blendTerrainBrush(output, oldHeights, newHeights, brush, settings).ok());
    CHECK(output.data() == std::vector<float>({0, 0, 0, 0}));
}
namespace {
Heightmap row(std::initializer_list<float> values) {
    Heightmap map(int(values.size()), 1);
    map.data() = values;
    return map;
}
}  // namespace

TEST_CASE("procgen.mask.curveTextureUsesTexelCenters") {
    auto       values = row({-1, 0, 0.25F, 0.5F, 0.75F, 1, 2});
    const auto curve  = row({0, 1});
    auto       r      = transformTerrainMask(values, values, curve);
    REQUIRE(r.ok());
    CHECK(values.data() == row({0, 0, 0, 0.5F, 1, 1, 1}).data());
}

TEST_CASE("procgen.mask.twoStageRangeMatchesShaderEquations") {
    auto       values = row({0, 25, 50, 75, 100});
    const auto filter = row({0, 1}), strength = row({1, 0});
    auto       r = generateTerrainRangeMask(values, values, 0, 100, filter, strength);
    REQUIRE(r.ok());
    CHECK(values.data() == row({1, 1, 0.5F, 0, 0}).data());
}

TEST_CASE("procgen.mask.slopeUsesWorldSpacingAndNormalMetric") {
    Heightmap plane(5, 3), slope(5, 3);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x) plane.setHeight(x, y, float(x));
    auto r = deriveTerrainSlope(slope, plane, 2, 3, 2);
    REQUIRE(r.ok());
    for (float value : slope.data()) CHECK(std::abs(value - float(1 - 1 / std::sqrt(2.0))) < 1e-6F);
    r = deriveTerrainSlope(plane, plane, 2, 3, 2);
    REQUIRE(r.ok());
    CHECK(plane.data() == slope.data());
    auto singleton = row({9});
    r              = deriveTerrainSlope(singleton, singleton, 1, 1);
    REQUIRE(r.ok());
    CHECK(singleton.height(0, 0) == 0);
}

TEST_CASE("procgen.mask.distanceAxesRotationAndRepeat") {
    Heightmap                   mask(3, 3);
    const auto                  curve = row({0, 1});
    TerrainDistanceMaskSettings s;
    s.axis = TerrainDistanceAxis::X;
    auto r = generateTerrainDistanceMask(mask, s, curve, curve);
    REQUIRE(r.ok());
    for (int y = 0; y < 3; ++y) {
        CHECK(mask.height(0, y) == 0);
        CHECK(mask.height(1, y) == 0.5F);
        CHECK(mask.height(2, y) == 1);
    }
    s.rotation = float(std::numbers::pi / 2);
    r          = generateTerrainDistanceMask(mask, s, curve, curve);
    REQUIRE(r.ok());
    CHECK(mask.height(1, 0) == 0);
    CHECK(mask.height(1, 2) == 1);
    s.rotation = 0;
    s.offsetX  = 1;
    s.tiling   = true;
    r          = generateTerrainDistanceMask(mask, s, curve, curve);
    REQUIRE(r.ok());
    CHECK(mask.height(0, 1) == 0);
    CHECK(mask.height(1, 1) == 0.5F);
    CHECK(mask.height(2, 1) == 1);
}

TEST_CASE("procgen.mask.distanceCircleAndSquareOutsideRules") {
    Heightmap                   mask(3, 3);
    const auto                  curve = row({0, 1}), reverse = row({1, 0});
    TerrainDistanceMaskSettings s;
    auto                        r = generateTerrainDistanceMask(mask, s, curve, curve);
    REQUIRE(r.ok());
    CHECK(mask.height(1, 1) == 0);
    CHECK(mask.height(0, 0) == 1);
    s.axis = TerrainDistanceAxis::RoundedSquare;
    r      = generateTerrainDistanceMask(mask, s, curve, curve);
    REQUIRE(r.ok());
    CHECK(mask.height(1, 1) == 0.5F);
    s.offsetX = 2;
    r         = generateTerrainDistanceMask(mask, s, curve, reverse);
    REQUIRE(r.ok());
    for (float value : mask.data()) CHECK(value == 1);  // Outside is transformed, not forcibly black.
}

TEST_CASE("procgen.mask.rejectedInputsDoNotPublish") {
    auto       target = row({4, 5}), source = row({0, 1}), curve = row({0, 1});
    const auto before = target;
    auto       r      = generateTerrainRangeMask(target, source, 1, 1, curve, curve);
    CHECK(!r.ok());
    CHECK(target.data() == before.data());
    r = deriveTerrainSlope(target, source, 0, 1);
    CHECK(!r.ok());
    CHECK(target.data() == before.data());
    TerrainDistanceMaskSettings s;
    s.scaleX = std::numeric_limits<float>::quiet_NaN();
    r        = generateTerrainDistanceMask(target, s, curve, curve);
    CHECK(!r.ok());
    CHECK(target.data() == before.data());
    curve.setHeight(1, 0, std::numeric_limits<float>::infinity());
    r = transformTerrainMask(target, source, curve);
    CHECK(!r.ok());
    CHECK(target.data() == before.data());
}

TEST_CASE("procgen.effect.contrastUsesSnapshotAndNineTapWeights") {
    Heightmap target(3, 3), mask(3, 3);
    std::fill(mask.data().begin(), mask.data().end(), 1.0F);
    target.setHeight(1, 1, 1);
    auto r = applyTerrainContrast(target, mask, 1, 1);
    REQUIRE(r.ok());
    CHECK(target.height(1, 1) == 1.4375F);
    CHECK(target.height(1, 0) == -0.0625F);
    CHECK(target.height(0, 0) == -0.046875F);
    auto before = target.data();
    r           = applyTerrainContrast(target, mask, 1, 0);
    REQUIRE(r.ok());
    CHECK(target.data() == before);
    mask.setHeight(2, 2, 2);
    r = applyTerrainContrast(target, mask, 1, 1);
    CHECK(!r.ok());
    CHECK(target.data() == before);
}
TEST_CASE("procgen.effect.contrastFractionalOffsetAndAlias") {
    auto target = row({0, 1, 0});
    auto mask   = row({1, 1, 1});
    auto r      = applyTerrainContrast(target, mask, 1, 0.5F);
    REQUIRE(r.ok());
    CHECK(target.height(1, 0) == 1.15625F);
    auto alias = row({0, 1, 0});
    r          = applyTerrainContrast(alias, alias, 1, 1);
    REQUIRE(r.ok());
    CHECK(alias.data() == row({0, 1.3125F, 0}).data());
}

TEST_CASE("procgen.mask.globalSpawnerScalarOutputUsesImageTransformAndBlend") {
    Heightmap input(2, 1), source(1, 1), curve(1, 1), output(2, 1);
    input.data()  = {0.8F, 0.4F};
    source.data() = {0.5F};
    curve.data()  = {0.5F};
    TerrainImageMaskSettings settings;
    settings.filter = static_cast<TerrainImageFilter>(99);
    REQUIRE(applyTerrainGlobalSpawnerMask(output, input, source, curve, settings,
                                          TerrainMaskBlend::Multiply).ok());
    CHECK(output.data()[0] == 0.4F);
    CHECK(output.data()[1] == 0.2F);
    auto before = output.data();
    settings.scaleX = 0;
    CHECK(!applyTerrainGlobalSpawnerMask(output, input, source, curve, settings,
                                         TerrainMaskBlend::Multiply).ok());
    CHECK(output.data() == before);
}

TEST_CASE("procgen.mask.noiseFamiliesFractionalFractalWarpAndDeterminism") {
    Heightmap input(7, 5), curve(4, 1), first(7, 5), second(7, 5);
    std::fill(input.data().begin(), input.data().end(), 1.0F);
    curve.data() = {0.0F, 0.33333334F, 0.6666667F, 1.0F};
    TerrainNoiseMaskSettings settings;
    settings.octaves = 3.5F;
    settings.warpIterations = 1.5F;
    settings.warpStrength = 0.2F;
    settings.rotation = 0.3F;
    settings.seed = 42;
    std::vector<std::vector<float>> families;
    for (int type = 0; type <= 4; ++type) {
        settings.type = static_cast<TerrainNoiseType>(type);
        REQUIRE(generateTerrainNoiseMask(first, input, curve, settings, TerrainMaskBlend::Multiply).ok());
        REQUIRE(generateTerrainNoiseMask(second, input, curve, settings, TerrainMaskBlend::Multiply).ok());
        CHECK(first.data() == second.data());
        CHECK(std::all_of(first.data().begin(), first.data().end(), [](float v) { return std::isfinite(v); }));
        families.push_back(first.data());
    }
    CHECK(families[0] != families[1]);
    auto before = first.data();
    settings.octaves = 17;
    CHECK(!generateTerrainNoiseMask(first, input, curve, settings, TerrainMaskBlend::Multiply).ok());
    CHECK(first.data() == before);
}

TEST_CASE("procgen.mask.smoothTwoPassVerticalityRectangularAndAtomic") {
    Heightmap input(15, 15), output(15, 15);
    input.setHeight(7, 7, 1);
    REQUIRE(smoothTerrainMask(output, input, 0, 1).ok());
    CHECK(std::abs(output.height(7, 7) - 1.0 / 57.76) < 1e-7);
    CHECK(std::abs(output.height(8, 9) - 0.95 * 0.85 / 57.76) < 1e-7);
    REQUIRE(smoothTerrainMask(output, input, -1, 1).ok());
    for (size_t i = 0; i < output.data().size(); ++i) CHECK(output.data()[i] <= input.data()[i]);
    REQUIRE(smoothTerrainMask(output, input, 1, 1).ok());
    for (size_t i = 0; i < output.data().size(); ++i) CHECK(output.data()[i] >= input.data()[i]);

    Heightmap rectangular(2, 4), rectangularOutput(2, 4);
    rectangular.setHeight(0, 1, 1);
    rectangular.setHeight(1, 1, 1);
    REQUIRE(smoothTerrainMask(rectangularOutput, rectangular, 0, 0.5F).ok());
    CHECK(std::abs(rectangularOutput.height(0, 1) - 1.0 / 7.6) < 1e-7);
    CHECK(std::abs(rectangularOutput.height(0, 2) - 0.95 / 7.6) < 1e-7);
    const auto before = rectangularOutput.data();
    CHECK(!smoothTerrainMask(rectangularOutput, rectangular, 2, 1).ok());
    CHECK(rectangularOutput.data() == before);
}

TEST_CASE("procgen.mask.collisionStackMatchesRadiusAndLayerInvertConventions") {
    auto input = row({0.5F, 0.5F});
    auto target = row({9, 9});
    auto curve = row({0, 1});
    auto radius = row({1, 1});
    auto layer = row({0, 1});
    auto inactive = row({0, 0});
    TerrainCollisionMaskStack stack;
    REQUIRE(stack.addLayer(radius, TerrainCollisionMaskType::RadiusTree, true, false).ok());
    REQUIRE(stack.addLayer(layer, TerrainCollisionMaskType::LayerGameObject, true, false).ok());
    REQUIRE(stack.addLayer(inactive, TerrainCollisionMaskType::RadiusTag, false, false).ok());
    CHECK(stack.getLayerCount() == 3);
    REQUIRE(stack.apply(target, input, curve, TerrainMaskBlend::Multiply).ok());
    CHECK(target.data() == row({0.5F, 0}).data());
    const auto before = target.data();
    CHECK(!stack.addLayer(radius, static_cast<TerrainCollisionMaskType>(99), true, false).ok());
    CHECK(!stack.apply(target, input, curve, static_cast<TerrainMaskBlend>(99)).ok());
    CHECK(target.data() == before);
    stack.clear();
    CHECK(stack.getLayerCount() == 0);
}
