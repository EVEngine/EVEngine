#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_pcg_terrain_watcher)
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "PrivateTestScope.h"
#include "procgen/PointSet.h"
#include "procgen/heightmap/PcgTerrainWatcher.h"
#include "procgen/heightmap/PcgTreeManager.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "zeroerr/unittest.h"
// pcg_terrain_watcher.cpp
namespace test_pcg_terrain_watcher_unit {
// pcg_terrain_watcher.cpp
using namespace eve::procgen;

TEST_CASE("procgen.pcgTerrainWatcher.bootstrapsAndOrdersChanges") {
    PcgTerrainWatcher watcher;
    watcher.beginScan();
    REQUIRE(watcher.addTerrain("west").ok());
    REQUIRE(watcher.addTerrain("east").ok());
    auto initial = watcher.commitScan();
    REQUIRE(initial.ok());
    CHECK(initial.value() == 0);
    CHECK(watcher.getTerrainCount() == 2);

    watcher.beginScan();
    REQUIRE(watcher.addTerrain("east").ok());
    REQUIRE(watcher.addTerrain("north").ok());
    auto changed = watcher.commitScan();
    REQUIRE(changed.ok());
    CHECK(changed.value() == 2);
    REQUIRE(watcher.getChangeTerrainId(0).ok());
    CHECK(watcher.getChangeTerrainId(0).value() == "north");
    CHECK(watcher.getChangeType(0).value() == 0);
    CHECK(watcher.getChangeTerrainId(1).value() == "west");
    CHECK(watcher.getChangeType(1).value() == 1);
}

TEST_CASE("procgen.pcgTerrainWatcher.invalidAndCancelledScansPreservePublishedState") {
    PcgTerrainWatcher watcher;
    CHECK(!watcher.addTerrain("orphan").ok());
    CHECK(!watcher.commitScan().ok());
    watcher.beginScan();
    REQUIRE(watcher.addTerrain("stable").ok());
    CHECK(!watcher.addTerrain("stable").ok());
    watcher.cancelScan();
    CHECK(watcher.getTerrainCount() == 0);
    CHECK(watcher.getChangeCount() == 0);
    CHECK(!watcher.getChangeTerrainId(0).ok());
}


// pcg_tree_manager.cpp
using namespace eve::procgen;

TEST_CASE("procgen.pcgTreeManager.aggregatesWorldTreesAndUsesRectangularRange") {
    PcgTreeManager manager;
    REQUIRE(manager.reset(-10.f, -20.f, 30.f, 40.f).ok());
    REQUIRE(manager.addTree(-2.f, 3.f, 0).ok());
    PointSet tile;
    tile.add(1.f, 8.f, 1.f);
    tile.add(4.f, 9.f, 4.f);
    REQUIRE(manager.addTrees(tile, 2).ok());
    CHECK(manager.getCount() == 3);
    auto near = manager.countInRange(1.f, 1.f, 3.f);
    REQUIRE(near.ok());
    CHECK(near.value() == 3);  // inclusive square, including (4,4)
    CHECK(manager.countInRange(1.f, 1.f, 2.9f).value() == 1);
}

TEST_CASE("procgen.pcgTreeManager.failedBatchIsAtomic") {
    PcgTreeManager manager;
    CHECK(!manager.addTree(0, 0, 0).ok());
    REQUIRE(manager.reset(0, 0, 10, 10).ok());
    REQUIRE(manager.addTree(2, 2, 0).ok());
    PointSet invalid;
    invalid.add(3, 0, 3);
    invalid.add(10, 0, 3);
    CHECK(!manager.addTrees(invalid, 1).ok());
    CHECK(manager.getCount() == 1);
    CHECK(!manager.countInRange(0, 0, -1).ok());
    CHECK(!manager.addTree(std::numeric_limits<float>::infinity(), 0, 0).ok());
}
}  // namespace test_pcg_terrain_watcher_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_hex_terrain)
#include <cstdint>
#include <string>
#include "PrivateTestScope.h"
#include "hexmap/HexMap.h"
#include "hexmap/HexMapGenerator.h"
#include "hexmap/HexSphereMap.h"
#include "hexmap/HexTerrainBake.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/Grid2D.h"
#include "procgen/Params.h"
#include "procgen/algorithms/HexTerrainBindings.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// procgen_hex_terrain.cpp
namespace test_procgen_hex_terrain {
using eve::procgen::generateHexSphere;
using eve::procgen::generateHexTerrain;
using eve::procgen::GeneratorRegistry;
using eve::procgen::Grid2D;
using eve::procgen::Params;

namespace {

[[nodiscard]] Params planarParams(std::uint32_t seed, int width, int height) {
    Params params;
    params.setSeed(seed);
    params.setSize(width, height);
    params.setInt("landPercentage", 50);
    params.setInt("waterLevel", 3);
    params.setInt("mapBorderX", 0);
    params.setInt("mapBorderZ", 0);
    params.setInt("regionBorder", 0);
    return params;
}

}  // namespace

TEST_CASE("procgen.hexTerrain.bakesAPlanarMapMatchingGenerateHexMap") {
    GeneratorRegistry::instance().registerBuiltins();
    Params params = planarParams(4242u, 20, 15);
    auto   baked  = generateHexTerrain(params);
    REQUIRE(baked.ok());

    auto bake = eve::hexmap::hexTerrainBakeFromValue(baked.value());
    REQUIRE(bake.ok());
    REQUIRE_EQ(bake.value().kind, eve::hexmap::HexTerrainBake::Kind::Planar);
    REQUIRE_EQ(bake.value().cellCountX, 20);
    REQUIRE_EQ(bake.value().cellCountZ, 15);
    REQUIRE_EQ(bake.value().seed, 4242u);

    eve::hexmap::HexMap expected;
    REQUIRE(expected.reset(20, 15, 4242u).ok());
    eve::hexmap::HexMapGeneratorSettings settings;
    settings.seed           = 4242u;
    settings.landPercentage = 50;
    settings.waterLevel     = 3;
    settings.mapBorderX     = 0;
    settings.mapBorderZ     = 0;
    settings.regionBorder   = 0;
    REQUIRE(eve::hexmap::generateHexMap(expected, settings).ok());

    eve::hexmap::HexMap applied;
    REQUIRE(eve::hexmap::applyHexTerrain(applied, bake.value()).ok());
    REQUIRE_EQ(applied.cellCount(), expected.cellCount());
    for (std::int32_t index = 0; index < expected.cellCount(); ++index) {
        const auto coordinates = expected.coordinatesAt(index);
        REQUIRE_EQ(applied.values(coordinates).raw(), expected.values(coordinates).raw());
        REQUIRE_EQ(applied.flags(coordinates).raw(), expected.flags(coordinates).raw());
    }
}

TEST_CASE("procgen.hexTerrain.bakesADeterministicSphere") {
    GeneratorRegistry::instance().registerBuiltins();
    Params params;
    params.setSeed(20260918u);
    params.setInt("subdivision", 1);
    params.setFloat("radius", 100.f);
    params.setInt("landPercentage", 45);
    params.setInt("waterLevel", 0);

    auto first  = generateHexSphere(params);
    auto second = generateHexSphere(params);
    REQUIRE(first.ok());
    REQUIRE(second.ok());

    auto a = eve::hexmap::hexTerrainBakeFromValue(first.value());
    auto b = eve::hexmap::hexTerrainBakeFromValue(second.value());
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE_EQ(a.value().kind, eve::hexmap::HexTerrainBake::Kind::Sphere);
    REQUIRE_EQ(a.value().cellCount, b.value().cellCount);
    REQUIRE_EQ(a.value().cells.size(), b.value().cells.size());
    for (std::size_t i = 0; i < a.value().cells.size(); ++i) {
        REQUIRE_EQ(a.value().cells[i].values.raw(), b.value().cells[i].values.raw());
        REQUIRE_EQ(a.value().cells[i].flags.raw(), b.value().cells[i].flags.raw());
    }
}

TEST_CASE("procgen.hexTerrain.schemaDefaultsAndGenerateToRefuseTheBake") {
    GeneratorRegistry::instance().registerBuiltins();
    REQUIRE(GeneratorRegistry::instance().has("hex.terrain"));
    REQUIRE(GeneratorRegistry::instance().has("hex.sphere"));

    const auto* terrain = GeneratorRegistry::instance().descriptor("hex.terrain");
    REQUIRE(terrain != nullptr);
    const auto* width  = terrain->find("width");
    const auto* height = terrain->find("height");
    REQUIRE(width != nullptr);
    REQUIRE(height != nullptr);
    REQUIRE_EQ(width->defaultValue, "20");
    REQUIRE_EQ(height->defaultValue, "15");

    // Params always owns seed/width/height, so applyDefaults fills only the
    // generator tunables. Callers must setSize() to a 5x5 chunk multiple.
    Params defaults;
    REQUIRE(GeneratorRegistry::instance().applyDefaults("hex.terrain", defaults));
    REQUIRE_EQ(defaults.getWidth(), 32);
    REQUIRE_EQ(defaults.getHeight(), 32);
    REQUIRE_EQ(defaults.getInt("landPercentage", 0), 50);

    Grid2D      grid;
    std::string error;
    CHECK(!GeneratorRegistry::instance().generate("hex.terrain", defaults, grid, error));
    CHECK(error.find("generateHexTerrain") != std::string::npos);

    const auto* sphere = GeneratorRegistry::instance().descriptor("hex.sphere");
    REQUIRE(sphere != nullptr);
    const auto* subdivision = sphere->find("subdivision");
    REQUIRE(subdivision != nullptr);
    REQUIRE_EQ(subdivision->defaultValue, "4");

    Params sphereDefaults;
    REQUIRE(GeneratorRegistry::instance().applyDefaults("hex.sphere", sphereDefaults));
    REQUIRE_EQ(sphereDefaults.getInt("subdivision", -1), 4);
    CHECK(!GeneratorRegistry::instance().generate("hex.sphere", sphereDefaults, grid, error));
    CHECK(error.find("generateHexSphere") != std::string::npos);
}

TEST_CASE("procgen.hexTerrain.rejectsASizeThatIsNotAChunkMultiple") {
    Params params = planarParams(1u, 7, 7);
    auto   baked  = generateHexTerrain(params);
    CHECK(!baked.ok());
}
}  // namespace test_procgen_hex_terrain
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_curve_texture)
#include <zeroerr/unittest.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include "PrivateTestScope.h"
#include "image/ImageData.h"
#include "procgen/PointSet.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainAsset.h"
#include "procgen/heightmap/TerrainCurveTexture.h"
#include "procgen/heightmap/TerrainDerivedMap.h"
#include "procgen/heightmap/TerrainFile.h"
#include "procgen/heightmap/TerrainObjectPlacement.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "procgen/heightmap/TerrainStreaming.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// terrain_curve_texture.cpp
namespace test_terrain_curve_texture_unit {
// terrain_curve_texture.cpp
TEST_CASE("procgen.terrainCurveTexture.preservesPcgFirstPixelAndRangeContract") {
    eve::procgen::Heightmap curve(3, 1);
    curve.setHeight(0, 0, 0.25F);
    curve.setHeight(1, 0, 1.0F);
    curve.setHeight(2, 0, -0.5F);
    eve::image::ImageData output(4, 1, "RGBA32F");
    output.setPixel(0, 0, {1, 1, 1, 1});
    auto baked = eve::procgen::bakeTerrainCurveTexture(output, curve);
    REQUIRE(baked.ok());
    CHECK(baked.value().writtenPixels == 4);
    CHECK(baked.value().minimum == 0.25F);
    CHECK(baked.value().maximum == 1.0F);
    CHECK(output.getPixel(0, 0).r == 0.0F);
    CHECK(output.getPixel(1, 0).r == 0.625F);
    CHECK(output.getPixel(2, 0).r == 1.0F);
    CHECK(output.getPixel(3, 0).r == 0.25F);
    CHECK(output.getPixel(3, 0).a == 0.0F);
}

TEST_CASE("procgen.terrainCurveTexture.failureIsAtomic") {
    eve::procgen::Heightmap curve(2, 1);
    curve.setHeight(0, 0, 0);
    curve.setHeight(1, 0, std::numeric_limits<float>::quiet_NaN());
    eve::image::ImageData output(2, 1, "RGBA32F");
    output.setPixel(1, 0, {0.75F, 0, 0, 1});
    CHECK(!eve::procgen::bakeTerrainCurveTexture(output, curve).ok());
    CHECK(output.getPixel(1, 0).r == 0.75F);
    eve::procgen::Heightmap valid(2, 1);
    eve::image::ImageData   twoRows(2, 2, "RGBA32F");
    CHECK(!eve::procgen::bakeTerrainCurveTexture(twoRows, valid).ok());
    CHECK(twoRows.getWidth() == 2);
    CHECK(twoRows.getHeight() == 2);
}


// terrain_derived_map.cpp
using namespace eve::procgen;

TEST_CASE("procgen.derivedMap.aspectSourceConventions") {
    Heightmap plane(5, 5), output(5, 5);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x) plane.setHeight(x, y, float(x) / 4.F);
    REQUIRE(generateTerrainHeightmapAspect(output, plane, TerrainHeightmapAspect::Aspect).ok());
    CHECK(std::abs(output.height(2, 2) - 0.75F) < 0.000001F);
    REQUIRE(generateTerrainHeightmapAspect(output, plane, TerrainHeightmapAspect::Northerness).ok());
    CHECK(std::abs(output.height(2, 2) - 0.5F) < 0.000001F);
    REQUIRE(generateTerrainHeightmapAspect(output, plane, TerrainHeightmapAspect::Easterness).ok());
    CHECK(std::abs(output.height(2, 2)) < 0.000001F);

    Heightmap flat(3, 3);
    flat.data().assign(9, 0.2F);
    REQUIRE(generateTerrainHeightmapAspect(flat, flat, TerrainHeightmapAspect::Aspect).ok());
    CHECK(flat.height(1, 1) == 0.5F);
}

TEST_CASE("procgen.derivedMap.curvatureModesAndAtomicFailure") {
    Heightmap bowl(5, 5), average(5, 5), horizontal(5, 5), vertical(5, 5);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x) {
            const float dx = float(x - 2), dy = float(y - 2);
            bowl.setHeight(x, y, 0.02F * (dx * dx + 2.F * dy * dy));
        }
    REQUIRE(generateTerrainHeightmapCurvature(horizontal, bowl, TerrainHeightmapCurvature::Horizontal).ok());
    REQUIRE(generateTerrainHeightmapCurvature(vertical, bowl, TerrainHeightmapCurvature::Vertical).ok());
    REQUIRE(generateTerrainHeightmapCurvature(average, bowl, TerrainHeightmapCurvature::Average).ok());
    CHECK(std::abs(average.height(3, 2) - (horizontal.height(3, 2) + vertical.height(3, 2)) * 0.5F) < 0.000001F);
    CHECK(horizontal.height(3, 2) != vertical.height(3, 2));
    const auto before = average.data();
    CHECK(!generateTerrainHeightmapCurvature(average, bowl, static_cast<TerrainHeightmapCurvature>(9)).ok());
    CHECK(average.data() == before);
    Heightmap wrong(2, 2);
    CHECK(!generateTerrainHeightmapAspect(wrong, bowl, TerrainHeightmapAspect::Aspect).ok());
}


TEST_CASE("procgen.neighborhood.exactInPlaceTraversal") {
    Heightmap grow(2, 2), shrink(2, 2), output(2, 2);
    grow.data() = {0.F, 1.F, 0.F, 0.F};
    auto result = filterTerrainHeightmapNeighborhood(output, grow, 1, TerrainHeightmapNeighborhood::GrowEdges);
    REQUIRE(result.ok());
    const std::vector<float> expectedGrow{0.5F, 1.F, 0.5F, 0.5F};
    CHECK(output.data() == expectedGrow);

    shrink.data() = {0.F, 1.F, 1.F, 1.F};
    result        = filterTerrainHeightmapNeighborhood(output, shrink, 1, TerrainHeightmapNeighborhood::ShrinkEdges);
    REQUIRE(result.ok());
    const std::vector<float> expectedShrink{0.F, 0.5F, 0.5F, 0.5F};
    CHECK(output.data() == expectedShrink);

    Heightmap spike(3, 3);
    spike.data() = {0.F, 0.F, 0.F, 0.F, 1.F, 0.F, 0.F, 0.F, 0.F};
    REQUIRE(filterTerrainHeightmapNeighborhood(spike, spike, 1, TerrainHeightmapNeighborhood::DeNoise).ok());
    CHECK(spike.height(1, 1) == 0.F);
}

TEST_CASE("procgen.neighborhood.invalidInputIsAtomic") {
    Heightmap source(3, 3), target(3, 3);
    source.data().assign(9, 0.25F);
    target.data().assign(9, 0.75F);
    const auto before = target.data();
    CHECK(!filterTerrainHeightmapNeighborhood(target, source, 0, TerrainHeightmapNeighborhood::DeNoise).ok());
    CHECK(target.data() == before);
    CHECK(!filterTerrainHeightmapNeighborhood(target, source, 1, static_cast<TerrainHeightmapNeighborhood>(7)).ok());
    CHECK(target.data() == before);
}

TEST_CASE("procgen.heightmapFilter.sourceOrderAndRadiusQuirk") {
    Heightmap source(2, 2), output(2, 2);
    source.data() = {0.F, 1.F, 0.F, 0.F};
    REQUIRE(smoothTerrainHeightmap(output, source, 1).ok());
    const std::vector<float> expected{0.25F, 0.5625F, 0.0625F, 0.15625F};
    CHECK(output.data() == expected);

    Heightmap wide(11, 12), radiusOutput(11, 12);
    wide.data().assign(132, 1.F);
    REQUIRE(smoothTerrainHeightmapRadius(radiusOutput, wide, 0).ok());
    CHECK(radiusOutput.height(5, 5) == 1.F);
    CHECK(std::abs(radiusOutput.height(5, 6) - 1.F) < 0.00001F);
}


TEST_CASE("procgen.heightmapSlope.exactPlaneAndEdges") {
    Heightmap plane(3, 3), slope(3, 3);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) plane.setHeight(x, y, float(x) * 0.5F);
    REQUIRE(generateTerrainHeightmapSlope(slope, plane).ok());
    CHECK(std::abs(slope.height(1, 1) - float(0.5 / std::sqrt(1.25))) < 0.000001F);
    CHECK(slope.height(0, 1) < slope.height(1, 1));
    CHECK(slope.height(2, 1) < slope.height(1, 1));
}


TEST_CASE("procgen.heightmapArithmetic.scalarAndClamp") {
    Heightmap source(2, 1), output(2, 1);
    source.data() = {0.25F, 0.75F};
    REQUIRE(
        applyTerrainHeightmapScalarArithmetic(output, source, 0.5F, TerrainHeightmapArithmetic::Add, false, 0.F, 0.F)
            .ok());
    const std::vector<float> expectedAdd{0.75F, 1.25F};
    CHECK(output.data() == expectedAdd);
    REQUIRE(applyTerrainHeightmapScalarArithmetic(output, source, 2.F, TerrainHeightmapArithmetic::Multiply, true, 0.2F,
                                                  1.F)
                .ok());
    const std::vector<float> expectedMultiply{0.5F, 1.F};
    CHECK(output.data() == expectedMultiply);
    const auto before = output.data();
    CHECK(
        !applyTerrainHeightmapScalarArithmetic(output, source, 0.F, TerrainHeightmapArithmetic::Divide, false, 0.F, 1.F)
             .ok());
    CHECK(output.data() == before);
}


TEST_CASE("procgen.heightmapCopy.conditionalAndResampled") {
    Heightmap target(4, 1), source(2, 1);
    target.data() = {0.25F, 0.25F, 0.75F, 0.75F};
    source.data() = {0.F, 1.F};
    REQUIRE(copyTerrainHeightmap(target, source, TerrainHeightmapCopy::IfLess).ok());
    const std::vector<float> expectedLess{0.F, 0.25F, 0.75F, 0.75F};
    CHECK(target.data() == expectedLess);
    REQUIRE(copyTerrainHeightmap(target, source, TerrainHeightmapCopy::IfGreater).ok());
    const std::vector<float> expectedGreater{0.F, 0.5F, 1.F, 1.F};
    CHECK(target.data() == expectedGreater);
}


// terrain_file_raw.cpp
using namespace eve::procgen;

TEST_CASE("procgen.terrainFileRaw.byteDepthEndianAndTraversal") {
    const std::array<std::uint8_t, 4> raw8{0, 64, 128, 255};
    auto                              eight = decodeTerrainFile(raw8, TerrainFileFormat::Raw8);
    REQUIRE(eight.ok());
    CHECK(eight.value().format == "raw8");
    CHECK(eight.value().heightmap.height(0, 0) == 0.F);
    CHECK(std::abs(eight.value().heightmap.height(0, 1) - 64.F / 255.F) < 0.000001F);
    CHECK(std::abs(eight.value().heightmap.height(1, 0) - 128.F / 255.F) < 0.000001F);
    CHECK(eight.value().heightmap.height(1, 1) == 1.F);

    const std::array<std::uint8_t, 8> little{0, 0, 0, 64, 0, 128, 255, 255};
    const std::array<std::uint8_t, 8> big{0, 0, 64, 0, 128, 0, 255, 255};
    auto                              le = decodeTerrainFile(little, TerrainFileFormat::Raw16LE);
    auto                              be = decodeTerrainFile(big, TerrainFileFormat::Raw16BE);
    REQUIRE(le.ok());
    REQUIRE(be.ok());
    CHECK(le.value().heightmap.data() == be.value().heightmap.data());
    CHECK(std::abs(le.value().heightmap.height(1, 0) - 32768.F / 65535.F) < 0.000001F);
}

TEST_CASE("procgen.terrainFileRaw.requiresExplicitExactSquareFormat") {
    const std::array<std::uint8_t, 3> malformed{0, 1, 2};
    CHECK(!decodeTerrainFile(malformed, TerrainFileFormat::Raw8).ok());
    CHECK(!decodeTerrainFile(malformed, TerrainFileFormat::Raw16LE).ok());
    const std::array<std::uint8_t, 4> raw{0, 1, 2, 3};
    CHECK(!decodeTerrainFile(raw, TerrainFileFormat::Auto).ok());
    CHECK(int(parseTerrainFileFormat("RAW8")) == int(TerrainFileFormat::Raw8));
    CHECK(int(parseTerrainFileFormat("raw16le")) == int(TerrainFileFormat::Raw16LE));
    CHECK(int(parseTerrainFileFormat("raw16be")) == int(TerrainFileFormat::Raw16BE));
}

// terrain_heightmap_access.cpp
using namespace eve::procgen;

TEST_CASE("procgen.heightmapAccess.safeNormalizedAndShapeQueries") {
    Heightmap map(2, 2);
    map.data() = {0, 1, 2, 3};
    auto safe  = sampleTerrainHeightmapSafe(map, -7, 9);
    REQUIRE(safe.ok());
    CHECK(safe.value() == 2);
    auto middle = sampleTerrainHeightmapNormalized(map, 0.25F, 0.25F);
    REQUIRE(middle.ok());
    CHECK(middle.value() == 1.5F);
    auto edge = sampleTerrainHeightmapNormalized(map, 1, 1);
    REQUIRE(edge.ok());
    CHECK(edge.value() == 3);
    REQUIRE(terrainHeightmapHasData(map).ok());
    CHECK(terrainHeightmapHasData(map).value());
    REQUIRE(terrainHeightmapIsPowerOfTwo(map).ok());
    CHECK(terrainHeightmapIsPowerOfTwo(map).value());
    Heightmap rectangular(2, 3), empty;
    REQUIRE(terrainHeightmapIsPowerOfTwo(rectangular).ok());
    CHECK(!terrainHeightmapIsPowerOfTwo(rectangular).value());
    REQUIRE(terrainHeightmapHasData(empty).ok());
    CHECK(!terrainHeightmapHasData(empty).value());
    CHECK(!sampleTerrainHeightmapNormalized(map, -0.1F, 0).ok());
}


// terrain_object.cpp
using namespace eve::procgen;

namespace {
[[maybe_unused]] Heightmap filled(int width, int height, float value) {
    Heightmap result(width, height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) result.setHeight(x, y, value);
    return result;
}
[[maybe_unused]] TerrainObjectPlacementSettings settings() {
    TerrainObjectPlacementSettings value;
    value.width = value.depth = 2;
    value.spacing             = 1;
    value.spawnDensity        = 1;
    value.jitterPercent = value.failureRate = value.minimumFitness = value.minimumInstanceFitness = 0;
    value.boundsRadius                                                                            = 0.2F;
    value.prototype                                                                               = "rocks";
    value.namespaceId                                                                             = 71;
    value.seed                                                                                    = 5;
    TerrainObjectInstanceSettings instance;
    instance.asset            = "rock-a";
    instance.minimumInstances = instance.maximumInstances = 1;
    value.instances.push_back(instance);
    return value;
}
}  // namespace

TEST_CASE("procgen.terrainObject.exportsStableAttributedTransformsAndRemoval") {
    auto     fitness = filled(3, 3, 1), heights = filled(2, 2, 0.25F);
    auto     config = settings();
    PointSet first, second;
    auto     emitted = exportTerrainObjectPoints(first, fitness, heights, config);
    REQUIRE(emitted.ok());
    CHECK(emitted.value() == 9);
    REQUIRE(exportTerrainObjectPoints(second, fitness, heights, config).ok());
    CHECK(first.getCount() == second.getCount());
    for (int i = 0; i < first.getCount(); ++i) {
        CHECK(first.getPointId(i) == second.getPointId(i));
        CHECK(first.getX(i) == second.getX(i));
        CHECK(first.getY(i) == 0.25F);
        CHECK(first.getStringAttribute(i, "asset", "") == "rock-a");
        CHECK(first.getStringAttribute(i, "objectPrototype", "") == "rocks");
        CHECK(first.hasIntAttribute(i, "objectCandidate"));
        CHECK(first.getIntAttribute(i, "objectResource", -1) == 0);
    }
    PointSet removed;
    auto     count = removeTerrainObjectPoints(removed, first, fitness, config, 0.5F);
    REQUIRE(count.ok());
    CHECK(count.value() == 9);
    CHECK(removed.empty());
}




TEST_CASE("procgen.terrainObject.multiTileSharesRngCollisionAndOwnsHistory") {
    PointSet empty, west, east;
    auto     heights = filled(2, 2, 0.25F), fitness = filled(4, 2, 1);
    auto     config             = settings();
    config.boundsCollisionCheck = true;
    config.boundsRadius         = 0.6F;
    config.maxPoints            = 100;
    TerrainStampSettings bounds;
    bounds.centerX = 2;
    bounds.centerZ = 1;
    bounds.width   = 4;
    bounds.depth   = 2;
    TerrainMultiObjectWorkspace workspace;
    REQUIRE(workspace.addTile("west", empty, heights, 0, 0, 2, 2, 2, 2).ok());
    REQUIRE(workspace.addTile("east", empty, heights, 2, 0, 2, 2, 2, 2).ok());
    auto added = workspace.apply(fitness, config, bounds, TerrainObjectOperationMode::Add);
    REQUIRE(added.ok());
    REQUIRE(workspace.copyTile("west", west).ok());
    REQUIRE(workspace.copyTile("east", east).ok());
    CHECK(added.value() == west.getCount() + east.getCount());
    CHECK(added.value() > 0);
    for (const auto& a : west.points())
        for (const auto& b : east.points()) CHECK(std::hypot(double(a.x) - b.x, double(a.z) - b.z) >= 1.2 - 1e-5);
    REQUIRE(workspace.undo().ok());
    REQUIRE(workspace.copyTile("west", west).ok());
    CHECK(west.empty());
    REQUIRE(workspace.redo().ok());
    CHECK(workspace.getAppliedCount() == 1);
    REQUIRE(workspace.apply(fitness, config, bounds, TerrainObjectOperationMode::Replace).ok());
    CHECK(!workspace.redo().ok());
}


// terrain_performance.cpp
using namespace eve;
using namespace eve::procgen;

namespace {
constexpr std::size_t kHeaderSize = 44, kEntrySize = 36;
[[maybe_unused]] void put16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(std::uint8_t(value));
    out.push_back(std::uint8_t(value >> 8));
}
[[maybe_unused]] void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) out.push_back(std::uint8_t(value >> shift));
}
[[maybe_unused]] void put64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) out.push_back(std::uint8_t(value >> shift));
}
[[maybe_unused]] void putFloat(std::vector<std::uint8_t>& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    put32(out, bits);
}
[[maybe_unused]] std::uint32_t checksum(const std::vector<std::uint8_t>& bytes) {
    std::uint32_t value = 2166136261U;
    for (auto byte : bytes) {
        value ^= byte;
        value *= 16777619U;
    }
    return value;
}
[[maybe_unused]] double milliseconds(std::chrono::steady_clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

/** Full EVTR address space backed only for metadata and the chunks actually read. */
class SparseTerrainArchiveSource final : public ITerrainArchiveSource {
public:
    explicit SparseTerrainArchiveSource(int dimension) {
        constexpr int chunkSize = 64;
        for (int index = 0; index < chunkSize * chunkSize; ++index) {
            put16(chunk_, std::uint16_t(index));
            chunk_.insert(chunk_.end(), {255, 0, 128, 128, 0, 0, 128, 128, 0, 2});
        }
        const int  columns = (dimension + chunkSize - 1) / chunkSize;
        const auto count   = std::uint32_t(columns * columns);
        payloadStart_      = kHeaderSize + std::uint64_t(count) * kEntrySize;
        metadata_.insert(metadata_.end(), {'E', 'V', 'T', 'R'});
        put16(metadata_, 5);
        put16(metadata_, 0);
        put32(metadata_, dimension);
        put32(metadata_, dimension);
        put32(metadata_, chunkSize);
        put32(metadata_, count);
        putFloat(metadata_, 0);
        putFloat(metadata_, 1);
        putFloat(metadata_, 1);
        put64(metadata_, kHeaderSize);
        const auto    chunkChecksum = checksum(chunk_);
        const auto    chunkBytes    = static_cast<std::uint32_t>(chunk_.size());
        std::uint64_t offset        = payloadStart_;
        for (int y = 0; y < columns; ++y)
            for (int x = 0; x < columns; ++x) {
                put32(metadata_, x);
                put32(metadata_, y);
                put16(metadata_, chunkSize);
                put16(metadata_, chunkSize);
                metadata_.insert(metadata_.end(), {0, 0, 0, 0});
                put64(metadata_, offset);
                put32(metadata_, chunkBytes);
                put32(metadata_, chunkBytes);
                put32(metadata_, chunkChecksum);
                offset += chunk_.size();
            }
        size_ = offset;
    }
    [[nodiscard]] std::uint64_t                     size() const noexcept override { return size_; }
    [[nodiscard]] Result<std::vector<std::uint8_t>> read(std::uint64_t offset, std::size_t length) const override {
        bytesRead_ += length;
        ++reads_;
        if (offset <= metadata_.size() && length <= metadata_.size() - offset) {
            auto begin = metadata_.begin() + std::ptrdiff_t(offset);
            return Result<std::vector<std::uint8_t>>::success({begin, begin + std::ptrdiff_t(length)});
        }
        if (offset >= payloadStart_ && length == chunk_.size() && (offset - payloadStart_) % chunk_.size() == 0 &&
            offset + length <= size_)
            return Result<std::vector<std::uint8_t>>::success(chunk_);
        return Result<std::vector<std::uint8_t>>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                            "sparse terrain read is out of range", {},
                                                                            {}, "procgen.terrain-performance"));
    }
    [[nodiscard]] std::uint64_t bytesRead() const noexcept { return bytesRead_; }
    [[nodiscard]] std::uint64_t reads() const noexcept { return reads_; }

private:
    std::uint64_t             payloadStart_ = 0, size_ = 0;
    std::vector<std::uint8_t> metadata_, chunk_;
    mutable std::uint64_t     bytesRead_ = 0, reads_ = 0;
};
}  // namespace
}  // namespace test_terrain_curve_texture_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_detail)
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "PrivateTestScope.h"
#include "procgen/PointSet.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainDetailLayer.h"
#include "procgen/heightmap/TerrainDetailPlacement.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "zeroerr/unittest.h"
// terrain_detail.cpp
namespace test_terrain_detail_unit {
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
}  // namespace test_terrain_detail_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_effect)
#include <algorithm>
#include <cmath>
#include <limits>
#include "PrivateTestScope.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainEffect.h"
#include "zeroerr/unittest.h"
// terrain_effect.cpp
namespace test_terrain_effect {
using namespace eve::procgen;
namespace {
Heightmap filled(int width, int height, float value) {
    Heightmap map(width, height);
    std::fill(map.data().begin(), map.data().end(), value);
    return map;
}
Heightmap row(std::initializer_list<float> values) {
    Heightmap map(int(values.size()), 1);
    map.data() = values;
    return map;
}
}  // namespace

TEST_CASE("procgen.effect.contrastAllowsPcgDefaultAndExtrapolation") {
    auto map = row({0, 1, 0}), mask = row({1, 1, 1});
    auto r = applyTerrainContrast(map, mask, 2, 1);
    REQUIRE(r.ok());
    CHECK(map.data() == row({-0.3125F, 1.625F, -0.3125F}).data());
    map = row({0, 1, 0});
    r   = applyTerrainContrast(map, mask, 10, 1);
    REQUIRE(r.ok());
    CHECK(map.height(1, 0) == 4.125F);
    map               = row({0, std::numeric_limits<float>::max(), 0});
    const auto before = map.data();
    r                 = applyTerrainContrast(map, mask, 2, 1);
    CHECK(!r.ok());
    CHECK(map.data() == before);
}

TEST_CASE("procgen.effect.smoothSeparableImpulseAndVerticality") {
    auto original = filled(15, 15, 0), mask = filled(15, 15, 1);
    original.setHeight(7, 7, 1);
    auto                  map = original;
    TerrainSmoothSettings s;
    s.radius = 1;
    auto r   = applyTerrainSmooth(map, mask, s);
    REQUIRE(r.ok());
    CHECK(std::abs(map.height(7, 7) - 1.0 / 57.76) < 1e-7);
    CHECK(std::abs(map.height(8, 9) - 0.95 * 0.85 / 57.76) < 1e-7);
    s.verticality = -1;
    map           = original;
    r             = applyTerrainSmooth(map, mask, s);
    REQUIRE(r.ok());
    for (size_t i = 0; i < map.data().size(); ++i) CHECK(map.data()[i] <= original.data()[i]);
    CHECK(std::abs(map.height(7, 7) - 1.0 / 57.76) < 1e-7);
    s.verticality = 1;
    map           = original;
    r             = applyTerrainSmooth(map, mask, s);
    REQUIRE(r.ok());
    for (size_t i = 0; i < map.data().size(); ++i) CHECK(map.data()[i] >= original.data()[i]);
}

TEST_CASE("procgen.effect.smoothRectangularRasterUsesXTexelForVertical") {
    auto map = filled(2, 4, 0), mask = filled(2, 4, 1);
    map.setHeight(0, 1, 1);
    map.setHeight(1, 1, 1);
    TerrainSmoothSettings s;
    s.radius = 0.5F;
    auto r   = applyTerrainSmooth(map, mask, s);
    REQUIRE(r.ok());
    CHECK(std::abs(map.height(0, 1) - 1.0 / 7.6) < 1e-7);
    CHECK(std::abs(map.height(0, 2) - 0.95 / 7.6) < 1e-7);
    CHECK(std::abs(map.height(0, 3) - 0.85 / 7.6) < 1e-7);
}

TEST_CASE("procgen.effect.smoothMaskRemainsSnapshotAcrossPasses") {
    auto original = filled(3, 3, 0.2F);
    original.setHeight(1, 1, 0.8F);
    auto                  map = original, alias = original;
    TerrainSmoothSettings s;
    s.radius = 0.5F;
    auto r   = applyTerrainSmooth(map, original, s);
    REQUIRE(r.ok());
    r = applyTerrainSmooth(alias, alias, s);
    REQUIRE(r.ok());
    CHECK(map.data() == alias.data());
    auto before = map.data();
    s.radius    = 0;
    r           = applyTerrainSmooth(map, original, s);
    REQUIRE(r.ok());
    CHECK(map.data() == before);
    s.radius = -1;
    r        = applyTerrainSmooth(map, original, s);
    CHECK(!r.ok());
    CHECK(map.data() == before);
}

TEST_CASE("procgen.effect.ridgesApplyHorizontalThenVerticalAndClipAfterMask") {
    auto map = filled(3, 3, 0.5F), mask = filled(3, 3, 1);
    map.setHeight(0, 1, 0);
    map.setHeight(2, 1, 1);
    map.setHeight(1, 0, 0.2F);
    map.setHeight(1, 2, 0.8F);
    TerrainRidgeSettings s;
    s.exponent    = 2;
    s.passes      = 1;
    s.maximum     = 1;
    s.mixStrength = 1;
    auto r        = applyTerrainRidges(map, mask, s);
    REQUIRE(r.ok());
    CHECK(std::abs(map.height(1, 1) - 0.2041666667) < 1e-7);
    map  = row({-2, 2});
    mask = row({0, 0});
    r    = applyTerrainRidges(map, mask, s);
    REQUIRE(r.ok());
    CHECK(map.data() == row({0, 1}).data());
}

TEST_CASE("procgen.effect.ridgePassCountAndAliasUseStableMask") {
    const auto           original = row({0.1F, 0.4F, 0.2F, 0.5F});
    auto                 batch = original, repeated = original, alias = original;
    TerrainRidgeSettings s;
    s.passes = 3;
    auto r   = applyTerrainRidges(batch, original, s);
    REQUIRE(r.ok());
    r = applyTerrainRidges(alias, alias, s);
    REQUIRE(r.ok());
    CHECK(batch.data() == alias.data());
    s.passes = 1;
    for (int i = 0; i < 3; ++i) {
        r = applyTerrainRidges(repeated, original, s);
        REQUIRE(r.ok());
    }
    CHECK(batch.data() == repeated.data());
    s.passes = 0;
    r        = applyTerrainRidges(batch, original, s);
    REQUIRE(r.ok());
    CHECK(batch.data() == repeated.data());
    s.passes = -1;
    r        = applyTerrainRidges(batch, original, s);
    CHECK(!r.ok());
    CHECK(batch.data() == repeated.data());
}

TEST_CASE("procgen.effect.terraceNearestEvenAndOneSidedBevel") {
    auto                   map  = row({-2.5F, -1.5F, -0.5F, 0.5F, 1.5F, 2.5F});
    auto                   mask = filled(6, 1, 1);
    TerrainTerraceSettings s;
    s.count = 1;
    auto r  = applyTerrainTerrace(map, mask, s);
    REQUIRE(r.ok());
    CHECK(map.data() == row({-2, -2, 0, 0, 2, 2}).data());
    map     = row({0.25F, 0.375F, 0.75F});
    mask    = filled(3, 1, 1);
    s.bevel = 0.75F;
    r       = applyTerrainTerrace(map, mask, s);
    REQUIRE(r.ok());
    CHECK(map.data() == row({0, 0.375F, 1}).data());
    auto before = map.data();
    s.count     = 0;
    r           = applyTerrainTerrace(map, mask, s);
    CHECK(!r.ok());
    CHECK(map.data() == before);
}

TEST_CASE("procgen.effect.powerUsesFourMinusControlAndAtomicDomainValidation") {
    auto map = row({0.25F, 0.5F, 1}), mask = row({1, 0.5F, 1});
    auto r = applyTerrainPower(map, mask, 2);
    REQUIRE(r.ok());
    CHECK(map.data() == row({0.0625F, 0.375F, 1}).data());
    map  = row({0.25F, 0.5F, 1});
    mask = row({1, 1, 1});
    r    = applyTerrainPower(map, mask, 5);
    REQUIRE(r.ok());
    CHECK(map.data() == row({4, 2, 1}).data());
    map         = row({0.25F, 0.5F, -1});
    auto before = map.data();
    r           = applyTerrainPower(map, mask, 2);
    CHECK(!r.ok());
    CHECK(map.data() == before);
    mask.setHeight(2, 0, 0);
    r = applyTerrainPower(map, mask, 2);
    REQUIRE(r.ok());
    CHECK(map.data() == row({0.0625F, 0.25F, -1}).data());
    map    = row({0, 1, 1});
    before = map.data();
    r      = applyTerrainPower(map, mask, 4);
    CHECK(!r.ok());
    CHECK(map.data() == before);
}

TEST_CASE("procgen.effect.heightCurveUsesRangeWithoutAddingMinimum") {
    auto map = row({2, 3, 4}), mask = row({1, 0.5F, 1}), curve = row({0, 1});
    auto r = applyTerrainHeightCurve(map, mask, curve, 2, 4);
    REQUIRE(r.ok());
    CHECK(map.data() == row({0, 2, 2}).data());
    map           = row({0, 0.5F, 1});
    mask          = row({1, 1, 1});
    auto original = map;
    auto expected = map;
    r             = applyTerrainHeightCurve(expected, mask, original, 0, 1);
    REQUIRE(r.ok());
    r = applyTerrainHeightCurve(map, mask, map, 0, 1);
    REQUIRE(r.ok());
    CHECK(map.data() == expected.data());
    curve       = row({0, 0, std::numeric_limits<float>::max()});
    map         = row({0, 1, 2});
    auto before = map.data();
    r           = applyTerrainHeightCurve(map, mask, curve, 0, 2);
    CHECK(!r.ok());
    CHECK(map.data() == before);
}

TEST_CASE("procgen.effect.heightMixLocalGlobalStrengthAndClipping") {
    auto                     map = row({0.25F, 0.25F, 0.25F}), local = row({0, 0.5F, 1}), global = row({1, 1, 0.25F});
    TerrainHeightMixSettings s;
    s.strength = 2;
    auto r     = applyTerrainHeightMix(map, local, global, s);
    REQUIRE(r.ok());
    CHECK(map.data() == row({0, 0.25F, 0.5F}).data());
    map    = row({-1, 2, 0.2F});
    global = row({0, 0, 0});
    r      = applyTerrainHeightMix(map, local, global, s);
    REQUIRE(r.ok());
    CHECK(map.data() == row({0, 0.5F, 0.2F}).data());
    auto original = row({0.1F, 0.2F, 0.3F}), alias = original, expected = original;
    r = applyTerrainHeightMix(expected, original, original, s);
    REQUIRE(r.ok());
    r = applyTerrainHeightMix(alias, alias, alias, s);
    REQUIRE(r.ok());
    CHECK(alias.data() == expected.data());
    s.clipMaximum = s.clipMinimum;
    auto before   = alias.data();
    r             = applyTerrainHeightMix(alias, local, global, s);
    CHECK(!r.ok());
    CHECK(alias.data() == before);
}

TEST_CASE("procgen.effect.smoothPreservesFiniteConstantExtrema") {
    const auto mask = filled(3, 2, 1);
    for (float value : {0.0F, -1000.0F, std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()}) {
        auto                  map    = filled(3, 2, value);
        const auto            before = map.data();
        TerrainSmoothSettings s;
        auto                  r = applyTerrainSmooth(map, mask, s);
        REQUIRE(r.ok());
        CHECK(map.data() == before);
    }
}

TEST_CASE("procgen.effect.ridgesRespectNativeWorldHeightBounds") {
    auto scalar = row({0.1F, 0.25F, 0.4F}), world = scalar, mask = row({1, 1, 1});
    for (float& value : world.data()) value = value * 200 + 100;
    TerrainRidgeSettings s;
    s.passes   = 2;
    s.exponent = 2;
    auto r     = applyTerrainRidges(scalar, mask, s);
    REQUIRE(r.ok());
    s.minimum = 100;
    s.maximum = 200;
    r         = applyTerrainRidges(world, mask, s);
    REQUIRE(r.ok());
    for (int x = 0; x < 3; ++x) CHECK(std::abs(world.height(x, 0) - (scalar.height(x, 0) * 200 + 100)) < 1e-4F);
}
}  // namespace test_terrain_effect
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_erosion)
#include <algorithm>
#include <cmath>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "PrivateTestScope.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainErosion.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "zeroerr/unittest.h"
// terrain_erosion.cpp
namespace test_terrain_erosion_unit {
using namespace eve::procgen;
namespace {
[[maybe_unused]] Heightmap filled(int width, int height, float value) {
    Heightmap result(width, height);
    std::fill(result.data().begin(), result.data().end(), value);
    return result;
}
}  // namespace

TEST_CASE("procgen.erosion.thermalEightNeighborShaderGolden") {
    auto height = filled(3, 3, 1), sediment = filled(3, 3, 0);
    height.setHeight(1, 1, 2);
    const auto             before = height.data();
    TerrainThermalSettings s;
    s.iterations  = 1;
    s.dt          = 0.1F;
    s.reposeSlope = 0;
    auto r        = applyTerrainThermal(height, sediment, s);
    REQUIRE(r.ok());
    CHECK(std::abs(height.height(1, 1) - 1.957325F) < 1e-6F);
    CHECK(std::abs(height.height(1, 0) - 1.00625F) < 1e-6F);
    CHECK(std::abs(height.height(0, 0) - 1.00441875F) < 1e-6F);
    for (size_t i = 0; i < before.size(); ++i)
        CHECK(std::abs(height.data()[i] + sediment.data()[i] - before[i]) < 1e-6F);
}

TEST_CASE("procgen.erosion.thermalWorldSlopeAndStrictThreshold") {
    auto height = filled(3, 3, 1), sediment = filled(3, 3, 0);
    height.setHeight(1, 1, 2);
    const auto             original = height;
    TerrainThermalSettings s;
    s.iterations  = 1;
    s.dt          = 1;
    s.reposeSlope = 1.5F;
    s.spacingX    = 1;
    s.spacingZ    = 100;
    s.heightScale = 2;
    auto r        = applyTerrainThermal(height, sediment, s);
    REQUIRE(r.ok());
    CHECK(height.height(1, 1) == 1.75F);
    height        = original;
    sediment      = filled(3, 3, 0);
    s.reposeSlope = 2;
    r             = applyTerrainThermal(height, sediment, s);
    REQUIRE(r.ok());
    CHECK(height.data() == original.data());
}

TEST_CASE("procgen.erosion.thermalMovementCapIncludesZeroFloor") {
    auto height = filled(3, 3, 0), sediment = filled(3, 3, 0);
    height.setHeight(1, 1, 2);
    TerrainThermalSettings s;
    s.iterations  = 1;
    s.dt          = 100;
    s.reposeSlope = 0;
    auto r        = applyTerrainThermal(height, sediment, s);
    REQUIRE(r.ok());
    CHECK(height.height(1, 1) == 1);
    CHECK(sediment.height(1, 1) == 1);
    CHECK(height.height(0, 1) == 0);
    CHECK(sediment.height(0, 1) == 0);
    auto singleton = filled(1, 1, 2), singleSediment = filled(1, 1, -1);
    r = applyTerrainThermal(singleton, singleSediment, s);
    REQUIRE(r.ok());
    CHECK(singleton.height(0, 0) == 2);
    CHECK(singleSediment.height(0, 0) == -1);
}

TEST_CASE("procgen.erosion.thermalIterationsResumeBothOutputs") {
    auto batch = filled(4, 3, 0.2F), batchSediment = filled(4, 3, 0.1F);
    batch.setHeight(2, 1, 1);
    auto                   repeated = batch, repeatedSediment = batchSediment;
    TerrainThermalSettings s;
    s.iterations  = 3;
    s.dt          = 0.1F;
    s.reposeSlope = 0;
    auto r        = applyTerrainThermal(batch, batchSediment, s);
    REQUIRE(r.ok());
    s.iterations = 1;
    for (int i = 0; i < 3; ++i) {
        r = applyTerrainThermal(repeated, repeatedSediment, s);
        REQUIRE(r.ok());
    }
    CHECK(batch.data() == repeated.data());
    CHECK(batchSediment.data() == repeatedSediment.data());
    const auto before = batch.data(), beforeSediment = batchSediment.data();
    s.dt = 0;
    r    = applyTerrainThermal(batch, batchSediment, s);
    REQUIRE(r.ok());
    CHECK(batch.data() == before && batchSediment.data() == beforeSediment);
}

TEST_CASE("procgen.erosion.thermalRejectsAliasesShapesAndOverflowAtomically") {
    const float largest = std::numeric_limits<float>::max();
    auto        height = filled(3, 1, 1), sediment = filled(3, 1, largest);
    height.setHeight(2, 0, largest);
    const auto             before = height.data(), beforeSediment = sediment.data();
    TerrainThermalSettings s;
    s.iterations  = 2;
    s.dt          = 100;
    s.reposeSlope = 0;
    auto r        = applyTerrainThermal(height, sediment, s);
    CHECK(!r.ok());
    CHECK(height.data() == before && sediment.data() == beforeSediment);
    r = applyTerrainThermal(height, height, s);
    CHECK(!r.ok());
    CHECK(height.data() == before);
    auto bad = filled(1, 1, 0);
    r        = applyTerrainThermal(height, bad, s);
    CHECK(!r.ok());
    CHECK(height.data() == before && bad.height(0, 0) == 0);
    s.dt = -1;
    r    = applyTerrainThermal(height, sediment, s);
    CHECK(!r.ok());
    CHECK(height.data() == before && sediment.data() == beforeSediment);
}
}  // namespace test_terrain_erosion_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_generation_session)
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "PrivateTestScope.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainEffect.h"
#include "procgen/heightmap/TerrainErosion.h"
#include "procgen/heightmap/TerrainGenerationSession.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "procgen/heightmap/TerrainWaterField.h"
#include "zeroerr/unittest.h"
// terrain_generation_session.cpp
namespace test_terrain_generation_session_unit {
using namespace eve::procgen;

TEST_CASE("procgen.session.mixedHistoryFailureKeepsEnabledState") {
    Heightmap baseline(1, 1), stamp(1, 1), one(1, 1), out(1, 1);
    baseline.data() = {-1};
    stamp.data()    = {4};
    one.data()      = {1};
    TerrainGenerationSession session;
    TerrainStampSettings     settings;
    settings.operation = TerrainStampOperation::Set;
    REQUIRE(session.reset(baseline).ok());
    REQUIRE(session.stamp(stamp, settings, one, one).ok());
    REQUIRE(session.power(one, 3.5F).ok());
    CHECK(!session.setOperationEnabled(0, false).ok());
    auto enabled = session.operationEnabled(0);
    REQUIRE(enabled.ok());
    CHECK(enabled.value());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data()[0] == 2);
}


TEST_CASE("procgen.session.effectHistoryMatchesDirectComposition") {
    Heightmap baseline(3, 3), mask(3, 3), curve(3, 1), out(3, 3);
    baseline.data() = {0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F, 0.8F, 0.7F, 0.6F};
    std::fill(mask.data().begin(), mask.data().end(), 0.5F);
    curve.data()                      = {0, 0.5F, 1};
    Heightmap                expected = baseline;
    TerrainGenerationSession session;
    REQUIRE(session.reset(baseline).ok());
    REQUIRE(applyTerrainContrast(expected, mask, 2, 1).ok());
    REQUIRE(session.contrast(mask, 2, 1).ok());
    TerrainSmoothSettings smooth;
    smooth.radius = 0.4F;
    REQUIRE(applyTerrainSmooth(expected, mask, smooth).ok());
    REQUIRE(session.smooth(mask, smooth).ok());
    TerrainRidgeSettings ridge;
    ridge.passes   = 1;
    ridge.exponent = 1.1F;
    ridge.maximum  = 2;
    REQUIRE(applyTerrainRidges(expected, mask, ridge).ok());
    REQUIRE(session.ridges(mask, ridge).ok());
    TerrainTerraceSettings terrace;
    terrace.count = 3;
    REQUIRE(applyTerrainTerrace(expected, mask, terrace).ok());
    REQUIRE(session.terrace(mask, terrace).ok());
    REQUIRE(applyTerrainPower(expected, mask, 3.5F).ok());
    REQUIRE(session.power(mask, 3.5F).ok());
    REQUIRE(applyTerrainHeightCurve(expected, mask, curve, 0, 2).ok());
    REQUIRE(session.heightCurve(mask, curve, 0, 2).ok());
    TerrainHeightMixSettings mix;
    mix.clipMaximum = 2;
    REQUIRE(applyTerrainHeightMix(expected, mask, mask, mix).ok());
    REQUIRE(session.heightMix(mask, mask, mix).ok());
    CHECK(session.getOperationCount() == 7);
    std::fill(mask.data().begin(), mask.data().end(), 0.0F);
    std::fill(curve.data().begin(), curve.data().end(), 0.0F);
    REQUIRE(session.undo().ok());
    REQUIRE(session.redo().ok());
    REQUIRE(session.replay().ok());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data() == expected.data());
}


TEST_CASE("procgen.session.ownedInputsUndoRedoAndBranch") {
    Heightmap baseline(1, 1), stamp(1, 1), one(1, 1), out(1, 1);
    stamp.data() = {2};
    one.data()   = {1};
    TerrainStampSettings s;
    s.operation = TerrainStampOperation::Add;
    TerrainGenerationSession session;
    REQUIRE(session.reset(baseline).ok());
    REQUIRE(session.stamp(stamp, s, one, one).ok());
    stamp.data()[0] = 3;
    REQUIRE(session.stamp(stamp, s, one, one).ok());
    stamp.data()[0]    = 99;
    one.data()[0]      = 0;
    baseline.data()[0] = 100;
    REQUIRE(session.replay().ok());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data()[0] == 5);
    REQUIRE(session.undo().ok());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data()[0] == 2);
    s.width = 0;
    CHECK(!session.stamp(stamp, s, one, one).ok());
    CHECK(session.getOperationCount() == 2);
    CHECK(session.getAppliedCount() == 1);
    REQUIRE(session.redo().ok());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data()[0] == 5);
    REQUIRE(session.undo().ok());
    s.width         = 1;
    one.data()[0]   = 1;
    stamp.data()[0] = 4;
    REQUIRE(session.stamp(stamp, s, one, one).ok());
    CHECK(!session.redo().ok());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data()[0] == 6);
}

TEST_CASE("procgen.session.enabledReplayFailurePreservesHistoryAndTerrain") {
    Heightmap baseline(1, 1), stamp(1, 1), one(1, 1), out(1, 1);
    baseline.data()[0] = std::numeric_limits<float>::max() * 0.8F;
    one.data()[0]      = 1;
    TerrainGenerationSession session;
    REQUIRE(session.reset(baseline).ok());
    TerrainStampSettings s;
    s.operation = TerrainStampOperation::Set;
    REQUIRE(session.stamp(stamp, s, one, one).ok());
    s.operation     = TerrainStampOperation::Add;
    stamp.data()[0] = std::numeric_limits<float>::max() * 0.4F;
    REQUIRE(session.stamp(stamp, s, one, one).ok());
    CHECK(!session.setOperationEnabled(0, false).ok());
    auto enabled = session.operationEnabled(0);
    REQUIRE(enabled.ok());
    CHECK(enabled.value());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data()[0] == stamp.data()[0]);
    REQUIRE(session.setOperationEnabled(1, false).ok());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data()[0] == 0);
    REQUIRE(session.setOperationEnabled(0, false).ok());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data()[0] == baseline.data()[0]);
}

TEST_CASE("procgen.session.lockMoveAndSnapshotIsolation") {
    TerrainGenerationSession session;
    Heightmap                baseline(1, 1), out(1, 1);
    baseline.data()[0] = 7;
    REQUIRE(session.reset(baseline).ok());
    REQUIRE(session.setAccess(TerrainSessionAccess::Locked).ok());
    CHECK(!session.reset(baseline).ok());
    CHECK(!session.replay().ok());
    REQUIRE(session.copyTerrain(out).ok());
    out.data()[0] = 99;
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data()[0] == 7);
    TerrainGenerationSession moved = std::move(session);
    CHECK(moved.getAccess() == TerrainSessionAccess::Locked);
    CHECK(!session.copyTerrain(out).ok());
    REQUIRE(session.reset(baseline).ok());
    REQUIRE(moved.setAccess(TerrainSessionAccess::Editable).ok());
    REQUIRE(moved.replay().ok());
    CHECK(!moved.undo().ok());
    CHECK(!moved.redo().ok());
}


TEST_CASE("procgen.session.erosionHistoryRestoresAllChannels") {
    Heightmap baseline(3, 3), out(3, 3), expectedSediment(3, 3);
    baseline.data()                   = {1, 2, 1, 2, 5, 2, 1, 2, 1};
    Heightmap                expected = baseline;
    TerrainGenerationSession session;
    TerrainWaterField        water;
    TerrainThermalSettings   thermal;
    thermal.reposeSlope = 0;
    thermal.dt          = 0.1F;
    thermal.iterations  = 2;
    TerrainWaterSettings flow;
    flow.precipitation = 0.2F;
    flow.evaporation   = 0;
    flow.dt            = 0.1F;
    TerrainSedimentSettings reaction;
    REQUIRE(session.reset(baseline).ok());
    REQUIRE(session.resetSimulation(1).ok());
    REQUIRE(water.reset(3, 3, 1).ok());
    REQUIRE(session.thermal(thermal).ok());
    REQUIRE(applyTerrainThermal(expected, expectedSediment, thermal).ok());
    const auto beforeHeight = expected.data(), beforeSediment = expectedSediment.data();
    REQUIRE(session.hydraulic(flow, reaction, thermal, 2).ok());
    REQUIRE(water.advanceHydraulic(expected, expectedSediment, flow, reaction, thermal, 2).ok());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data() == expected.data());
    REQUIRE(session.copySediment(out).ok());
    CHECK(out.data() == expectedSediment.data());
    for (int channel = 0; channel < 7; ++channel) {
        Heightmap reference(3, 3);
        REQUIRE(water.exportChannel(reference, static_cast<TerrainWaterChannel>(channel)).ok());
        REQUIRE(session.exportWater(out, static_cast<TerrainWaterChannel>(channel)).ok());
        CHECK(out.data() == reference.data());
    }
    REQUIRE(session.undo().ok());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data() == beforeHeight);
    REQUIRE(session.copySediment(out).ok());
    CHECK(out.data() == beforeSediment);
    for (int channel = 0; channel < 7; ++channel) {
        REQUIRE(session.exportWater(out, static_cast<TerrainWaterChannel>(channel)).ok());
        for (float value : out.data()) CHECK(value == (channel == 0 ? 1.0F : 0.0F));
    }
    // Mutating caller settings cannot alter an already recorded command.
    flow.dt              = -1;
    thermal.dt           = -1;
    reaction.depositRate = -1;
    CHECK(!session.hydraulic(flow, reaction, thermal, 2).ok());
    CHECK(session.getOperationCount() == 3);
    REQUIRE(session.redo().ok());
    REQUIRE(session.replay().ok());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data() == expected.data());
    REQUIRE(session.copySediment(out).ok());
    CHECK(out.data() == expectedSediment.data());
    REQUIRE(session.resetSimulation(0.5F).ok());
    REQUIRE(session.copySediment(out).ok());
    for (float value : out.data()) CHECK(value == 0);
    REQUIRE(session.undo().ok());
    REQUIRE(session.copySediment(out).ok());
    CHECK(out.data() == expectedSediment.data());
    REQUIRE(session.setAccess(TerrainSessionAccess::Locked).ok());
    CHECK(!session.resetSimulation(0).ok());
    CHECK(!session.thermal(thermal).ok());
    REQUIRE(session.exportWater(out, TerrainWaterChannel::Depth).ok());
    const auto saved = out.data();
    CHECK(!session.exportWater(out, static_cast<TerrainWaterChannel>(99)).ok());
    CHECK(out.data() == saved);
}

TEST_CASE("procgen.session.erosionReplayFailurePreservesSimulation") {
    Heightmap baseline(1, 1), stamp(1, 1), mask(1, 1), out(1, 1);
    baseline.data() = {-1};
    stamp.data()    = {1};
    mask.data()     = {1};
    TerrainGenerationSession session;
    TerrainStampSettings     settings;
    settings.operation = TerrainStampOperation::Set;
    TerrainWaterSettings flow;
    flow.precipitation = 1;
    flow.evaporation   = 0;
    TerrainThermalSettings thermal;
    thermal.iterations = 0;
    TerrainSedimentSettings sediment;
    REQUIRE(session.reset(baseline).ok());
    REQUIRE(session.stamp(stamp, settings, mask, mask).ok());
    REQUIRE(session.hydraulic(flow, sediment, thermal, 2).ok());
    REQUIRE(session.exportWater(out, TerrainWaterChannel::Depth).ok());
    const auto water = out.data();
    REQUIRE(session.copySediment(out).ok());
    const auto material = out.data();
    REQUIRE(session.copyTerrain(out).ok());
    const auto terrain = out.data();
    CHECK(!session.setOperationEnabled(0, false).ok());
    auto enabled = session.operationEnabled(0);
    REQUIRE(enabled.ok());
    CHECK(enabled.value());
    CHECK(!session.resetSimulation(-1).ok());
    CHECK(session.getOperationCount() == 2);
    REQUIRE(session.exportWater(out, TerrainWaterChannel::Depth).ok());
    CHECK(out.data() == water);
    REQUIRE(session.copySediment(out).ok());
    CHECK(out.data() == material);
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data() == terrain);
}


TEST_CASE("procgen.session.versionedSnapshotRetainsEveryCommandCursorAccessAndIsAtomic") {
    Heightmap baseline(3, 3), mask(3, 3), stamp(1, 1), curve(2, 1), before(3, 3), after(3, 3);
    std::fill(baseline.data().begin(), baseline.data().end(), 0.5F);
    std::fill(mask.data().begin(), mask.data().end(), 1.0F);
    stamp.data()[0] = 0.6F;
    curve.data()    = {0, 1};
    TerrainGenerationSession source;
    TerrainStampSettings     stampSettings;
    stampSettings.operation = TerrainStampOperation::Set;
    REQUIRE(source.reset(baseline).ok());
    REQUIRE(source.stamp(stamp, stampSettings, mask, mask).ok());
    REQUIRE(source.contrast(mask, 0, 1).ok());
    TerrainSmoothSettings smooth;
    smooth.radius = 0;
    REQUIRE(source.smooth(mask, smooth).ok());
    TerrainRidgeSettings ridge;
    ridge.passes = 0;
    REQUIRE(source.ridges(mask, ridge).ok());
    TerrainTerraceSettings terrace;
    terrace.strength = 0;
    REQUIRE(source.terrace(mask, terrace).ok());
    REQUIRE(source.power(mask, 4).ok());
    REQUIRE(source.heightCurve(mask, curve, 0, 1).ok());
    TerrainHeightMixSettings mix;
    mix.strength    = 0;
    mix.clipMaximum = 2;
    REQUIRE(source.heightMix(mask, mask, mix).ok());
    REQUIRE(source.resetSimulation(1).ok());
    TerrainThermalSettings thermal;
    thermal.iterations = 0;
    REQUIRE(source.thermal(thermal).ok());
    TerrainWaterSettings    water;
    TerrainSedimentSettings sediment;
    REQUIRE(source.hydraulic(water, sediment, thermal, 0).ok());
    REQUIRE(source.setOperationEnabled(2, false).ok());
    REQUIRE(source.undo().ok());
    REQUIRE(source.setAccess(TerrainSessionAccess::Locked).ok());
    REQUIRE(source.copyTerrain(before).ok());
    auto json = source.snapshotJson();
    REQUIRE(json.ok());
    TerrainGenerationSession restored;
    REQUIRE(restored.restoreJson(json.value()).ok());
    CHECK(restored.getOperationCount() == 11);
    CHECK(restored.getAppliedCount() == 10);
    CHECK(restored.getAccess() == TerrainSessionAccess::Locked);
    CHECK(!restored.operationEnabled(2).value());
    REQUIRE(restored.copyTerrain(after).ok());
    CHECK(after.data() == before.data());
    CHECK(restored.snapshotJson().value() == json.value());
    REQUIRE(restored.setAccess(TerrainSessionAccess::Editable).ok());
    const auto stable = restored.snapshotJson().value();
    CHECK(
        !restored
             .restoreJson(
                 "{\"schema\":\"eve.procgen.terrain-generation-session\",\"version\":1,\"payload\":\"00\",\"extra\":1}")
             .ok());
    CHECK(restored.snapshotJson().value() == stable);
}


TEST_CASE("procgen.session.stagedReplayLifecycleIsAtomicAndCancellable") {
    Heightmap baseline(1, 1), stamp(1, 1), mask(1, 1), out(1, 1);
    baseline.data()[0] = 1;
    stamp.data()[0] = mask.data()[0] = 1;
    TerrainStampSettings settings;
    settings.operation = TerrainStampOperation::Add;
    TerrainGenerationSession session;
    REQUIRE(session.reset(baseline).ok());
    REQUIRE(session.stamp(stamp, settings, mask, mask).ok());
    REQUIRE(session.stamp(stamp, settings, mask, mask).ok());
    REQUIRE(session.beginReplay().ok());
    CHECK(session.getReplayStatus() == TerrainSessionRunStatus::Pending);
    CHECK(session.getReplayCompletedOperations() == 0);
    auto firstStep = session.stepReplay(1);
    REQUIRE(firstStep.ok());
    CHECK(firstStep.value() == TerrainSessionRunStatus::Pending);
    CHECK(session.getReplayCompletedOperations() == 1);
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data()[0] == 3);
    CHECK(!session.undo().ok());
    CHECK(!session.setAccess(TerrainSessionAccess::Locked).ok());
    auto cancelled = session.cancelReplay();
    REQUIRE(cancelled.ok());
    CHECK(cancelled.value() == TerrainSessionRunStatus::Cancelled);
    CHECK(!session.stepReplay(1).ok());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data()[0] == 3);

    REQUIRE(session.beginReplay().ok());
    CHECK(!session.stepReplay(0).ok());
    auto completed = session.stepReplay(8);
    REQUIRE(completed.ok());
    CHECK(completed.value() == TerrainSessionRunStatus::Completed);
    CHECK(session.getReplayCompletedOperations() == 2);
    CHECK(!session.cancelReplay().ok());
    REQUIRE(session.undo().ok());
    CHECK(session.getReplayStatus() == TerrainSessionRunStatus::Idle);
}
}  // namespace test_terrain_generation_session_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_legacy_hydraulic)
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "PrivateTestScope.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainErosion.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "zeroerr/unittest.h"
// terrain_legacy_hydraulic.cpp
namespace test_terrain_legacy_hydraulic_unit {
// terrain_legacy_hydraulic.cpp
using namespace eve::procgen;

TEST_CASE("procgen.legacyHydraulic.sourceOneStepAndHardness") {
    Heightmap heights(3, 1), sediment(3, 1), hardness(3, 1), rain(3, 1);
    heights.data() = {0, 1, 0};
    rain.data()[1] = 1;
    TerrainLegacyHydraulicSettings settings;
    settings.sedimentDissolveRate = 1;
    REQUIRE(applyTerrainLegacyHydraulic(heights, sediment, hardness, rain, settings).ok());
    CHECK(heights.data() == std::vector<float>({0.2F, 0.6F, 0.2F}));
    CHECK(sediment.data() == std::vector<float>({0.2F, -0.4F, 0.2F}));

    Heightmap protectedHeight(3, 1), protectedSediment(3, 1);
    protectedHeight.data() = {0, 1, 0};
    hardness.data()[1]     = 1;
    REQUIRE(applyTerrainLegacyHydraulic(protectedHeight, protectedSediment, hardness, rain, settings).ok());
    CHECK(protectedHeight.data() == std::vector<float>({0, 1, 0}));
    CHECK(protectedSediment.data() == std::vector<float>({0, 0, 0}));
}


// terrain_legacy_thermal.cpp
using namespace eve::procgen;

TEST_CASE("procgen.legacyThermal.distributedSourceFormula") {
    Heightmap map(3, 3);
    map.setHeight(1, 1, 1);
    TerrainLegacyDistributedErosionSettings settings;
    settings.minimumThreshold = 0.1F;
    REQUIRE(applyTerrainLegacyDistributedErosion(map, settings).ok());
    CHECK(map.height(1, 1) == 0.5F);
    CHECK(map.height(1, 2) == 0.125F);
    CHECK(map.height(0, 1) == 0.125F);
    CHECK(map.height(2, 1) == 0.125F);
    CHECK(map.height(1, 0) == 0.125F);
}


TEST_CASE("procgen.legacyThermal.invalidInputIsAtomic") {
    Heightmap map(3, 3), hardness(1, 1);
    map.setHeight(1, 1, 1);
    const auto                              original = map.data();
    TerrainLegacyDistributedErosionSettings distributed;
    distributed.minimumThreshold = distributed.maximumThreshold;
    CHECK(!applyTerrainLegacyDistributedErosion(map, distributed).ok());
    CHECK(map.data() == original);
    TerrainLegacySteepestErosionSettings steepest;
    steepest.talusMaximum = 2;
    CHECK(!applyTerrainLegacySteepestErosion(map, hardness, steepest).ok());
    CHECK(map.data() == original);
}
}  // namespace test_terrain_legacy_hydraulic_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_mask)
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include "PrivateTestScope.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainBakedMaskCache.h"
#include "procgen/heightmap/TerrainCollisionMask.h"
#include "procgen/heightmap/TerrainEffect.h"
#include "procgen/heightmap/TerrainImageMask.h"
#include "procgen/heightmap/TerrainMask.h"
#include "procgen/heightmap/TerrainNoiseMask.h"
#include "procgen/heightmap/TerrainPolygonMask.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "zeroerr/unittest.h"
// terrain_mask.cpp
namespace test_terrain_mask {
using namespace eve::procgen;

TEST_CASE("procgen.mask.polygonOpenClosedBrushAndAtomicFailure") {
    TerrainPolygonMask polygon;
    REQUIRE(polygon.addNode(1, 1, 0.5F, 0.25F).ok());
    REQUIRE(polygon.addNode(3, 1, 0.5F, 0.5F).ok());
    REQUIRE(polygon.addNode(3, 3, 0.5F, 0.75F).ok());
    REQUIRE(polygon.addNode(1, 3, 0.5F, 1.0F).ok());
    Heightmap         output(5, 5), zeroBrush(1, 1);
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
    auto before   = output.data();
    grid.spacingX = 0;
    CHECK(!polygon.rasterize(output, brush, grid, TerrainPolygonMaskType::Open).ok());
    CHECK(output.data() == before);
    CHECK(polygon.getNodeCount() == 1);
}

TEST_CASE("procgen.mask.worldBiomeCacheIdentityDirtyRebuildAndResample") {
    TerrainBakedMaskCache cache;
    Heightmap             west(2, 2), east(1, 1), output(1, 1);
    west.data()      = {0, 1, 1, 0};
    east.data()[0]   = 0.75F;
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
    REQUIRE(applyTerrainGlobalSpawnerMask(output, input, source, curve, settings, TerrainMaskBlend::Multiply).ok());
    CHECK(output.data()[0] == 0.4F);
    CHECK(output.data()[1] == 0.2F);
    auto before     = output.data();
    settings.scaleX = 0;
    CHECK(!applyTerrainGlobalSpawnerMask(output, input, source, curve, settings, TerrainMaskBlend::Multiply).ok());
    CHECK(output.data() == before);
}

TEST_CASE("procgen.mask.noiseFamiliesFractionalFractalWarpAndDeterminism") {
    Heightmap input(7, 5), curve(4, 1), first(7, 5), second(7, 5);
    std::fill(input.data().begin(), input.data().end(), 1.0F);
    curve.data() = {0.0F, 0.33333334F, 0.6666667F, 1.0F};
    TerrainNoiseMaskSettings settings;
    settings.octaves        = 3.5F;
    settings.warpIterations = 1.5F;
    settings.warpStrength   = 0.2F;
    settings.rotation       = 0.3F;
    settings.seed           = 42;
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
    auto before      = first.data();
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
    auto                      input    = row({0.5F, 0.5F});
    auto                      target   = row({9, 9});
    auto                      curve    = row({0, 1});
    auto                      radius   = row({1, 1});
    auto                      layer    = row({0, 1});
    auto                      inactive = row({0, 0});
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
}  // namespace test_terrain_mask
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_multitile)
#include <algorithm>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include <vector>
#include "PrivateTestScope.h"
#include "procgen/PointSet.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainDetailLayer.h"
#include "procgen/heightmap/TerrainMultiTile.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "procgen/heightmap/TerrainTreePlacement.h"
#include "zeroerr/unittest.h"
// terrain_multitile.cpp
namespace test_terrain_multitile_unit {
using namespace eve::procgen;
namespace {
[[maybe_unused]] Heightmap filled(int width, int height, float value) {
    Heightmap result(width, height);
    std::fill(result.data().begin(), result.data().end(), value);
    return result;
}
[[maybe_unused]] TerrainStampSettings setStamp(double x, double z, double width, double depth) {
    TerrainStampSettings settings;
    settings.centerX   = x;
    settings.centerZ   = z;
    settings.width     = width;
    settings.depth     = depth;
    settings.operation = TerrainStampOperation::Set;
    return settings;
}
}  // namespace

TEST_CASE("procgen.multitile.pcgHeightmapSeamPixelMappingAndAtomicCommit") {
    Heightmap a = filled(3, 3, 0), b = filled(3, 3, 0), stamp = filled(1, 1, 7), one = filled(1, 1, 1);
    std::vector<TerrainHeightTile> tiles{{"west", &a, 0, 0, 2, 2, false}, {"east", &b, 2, 0, 2, 2, false}};
    auto                           report = applyTerrainStampMultiTile(tiles, stamp, setStamp(2, 1, 2, 2), one, one);
    REQUIRE(report.ok());
    CHECK(report.value().affectedTiles == 2);
    CHECK(report.value().operationX == 1);
    CHECK(report.value().operationWidth == 3);
    REQUIRE(report.value().mappings.size() == 2);
    CHECK(report.value().mappings[0].localX == 1);
    CHECK(report.value().mappings[0].operationX == 0);
    CHECK(report.value().mappings[0].width == 2);
    CHECK(report.value().mappings[1].localX == 0);
    CHECK(report.value().mappings[1].operationX == 1);
    CHECK(report.value().mappings[1].width == 2);
    CHECK(a.height(2, 1) == 7);
    CHECK(b.height(0, 1) == 7);
}

TEST_CASE("procgen.multitile.operationDomainsUsePcgSeamStride") {
    TerrainStampSettings              settings = setStamp(2, 1, 2, 2);
    std::vector<TerrainOperationTile> tiles    = {
        {"west", 0, 0, 2, 2, 3, 3, false},
        {"east", 2, 0, 2, 2, 3, 3, false},
    };
    auto height = mapTerrainOperationMultiTile(tiles, settings, TerrainOperationDomain::Heightmap);
    REQUIRE(height.ok());
    CHECK(height.value().operationX == 1);
    CHECK(height.value().operationWidth == 3);
    REQUIRE(height.value().mappings.size() == 2);
    CHECK(height.value().mappings[0].operationX == 0);
    CHECK(height.value().mappings[0].width == 2);
    CHECK(height.value().mappings[1].operationX == 1);
    CHECK(height.value().mappings[1].width == 2);

    auto detail = mapTerrainOperationMultiTile(tiles, settings, TerrainOperationDomain::TerrainDetail);
    REQUIRE(detail.ok());
    CHECK(detail.value().operationX == 1);
    CHECK(detail.value().operationWidth == 4);
    REQUIRE(detail.value().mappings.size() == 2);
    CHECK(detail.value().mappings[0].operationX == 0);
    CHECK(detail.value().mappings[0].width == 2);
    CHECK(detail.value().mappings[1].operationX == 2);
    CHECK(detail.value().mappings[1].width == 2);

    tiles[1].worldMap = true;
    auto ordinary     = mapTerrainOperationMultiTile(tiles, settings, TerrainOperationDomain::Texture);
    REQUIRE(ordinary.ok());
    CHECK(ordinary.value().affectedTiles == 1);
    auto selectedWorld =
        mapTerrainOperationMultiTile(tiles, settings, TerrainOperationDomain::GameObject, true, {"east"});
    REQUIRE(selectedWorld.ok());
    CHECK(selectedWorld.value().affectedTiles == 1);
    CHECK(selectedWorld.value().mappings[0].terrainName == "east");

    tiles[1].name = "west";
    CHECK(!mapTerrainOperationMultiTile(tiles, settings, TerrainOperationDomain::Tree).ok());
    tiles[1].name    = "east";
    tiles[1].originX = 2.25;
    CHECK(!mapTerrainOperationMultiTile(tiles, settings, TerrainOperationDomain::BakedMask).ok());
}

TEST_CASE("procgen.multitile.detailReplaceUsesSharedWindowAndAtomicPublish") {
    TerrainDetailLayer west, east;
    REQUIRE(west.reset(3, 3, 1).ok());
    REQUIRE(east.reset(3, 3, 1).ok());
    std::vector<TerrainDetailTile> tiles = {
        {"west", &west, 0, 0, 2, 2, false},
        {"east", &east, 2, 0, 2, 2, false},
    };
    Heightmap             fitness = filled(4, 3, 1);
    TerrainDetailSettings detail;
    detail.minimumFitness = 0;
    detail.fadeStart      = 0;
    detail.density        = 4;
    detail.mode           = TerrainDetailMode::Replace;
    auto result           = applyTerrainDetailMultiTile(tiles, fitness, detail, setStamp(2, 1, 2, 2), 77);
    REQUIRE(result.ok());
    CHECK(result.value().affectedTiles == 2);
    CHECK(result.value().changedSamples == 18);
    CHECK(west.sample(0, 1).value() == 0);
    CHECK(west.sample(1, 1).value() == 4);
    CHECK(west.sample(2, 1).value() == 4);
    CHECK(east.sample(0, 1).value() == 4);
    CHECK(east.sample(1, 1).value() == 4);
    CHECK(east.sample(2, 1).value() == 0);

    Heightmap wrong      = filled(3, 3, 0);
    const int beforeWest = west.sample(1, 1).value();
    const int beforeEast = east.sample(0, 1).value();
    CHECK(!applyTerrainDetailMultiTile(tiles, wrong, detail, setStamp(2, 1, 2, 2), 77).ok());
    CHECK(west.sample(1, 1).value() == beforeWest);
    CHECK(east.sample(0, 1).value() == beforeEast);

    tiles[1].layer = &west;
    CHECK(!applyTerrainDetailMultiTile(tiles, fitness, detail, setStamp(2, 1, 2, 2), 77).ok());
    CHECK(west.sample(1, 1).value() == beforeWest);
}


TEST_CASE("procgen.multitile.treeSharedStreamAddRemoveAndAtomicFailure") {
    PointSet west, east;
    Heightmap heights = filled(2, 2, 0.25F);
    Heightmap fitness = filled(4, 2, 1);
    std::vector<TerrainTreeTile> tiles = {
        {"west", &west, &heights, 0, 0, 2, 2, 2, 2, false},
        {"east", &east, &heights, 2, 0, 2, 2, 2, 2, false},
    };
    TerrainTreePlacementSettings tree;
    tree.spacing = tree.spawnDensity = 1;
    tree.jitterPercent = tree.failureRate = tree.minimumFitness = 0;
    tree.scaleMode = TerrainTreeScaleMode::Fixed;
    tree.minimumWidth = tree.maximumWidth = tree.minimumHeight = tree.maximumHeight = 1;
    tree.asset = "oak";
    tree.namespaceId = 42;
    tree.maxPoints = 100;
    tree.seed = 19;
    auto bounds = setStamp(2, 1, 4, 2);
    auto added = applyTerrainTreesMultiTile(tiles, fitness, tree, bounds, TerrainTreeOperationMode::Add);
    REQUIRE(added.ok());
    REQUIRE(added.value().affectedTiles == 2);
    REQUIRE(added.value().changedSamples == 8);
    REQUIRE(west.getCount() == 4);
    REQUIRE(east.getCount() == 4);
    REQUIRE(west.hasIntAttribute(0, "treeCandidate"));
    REQUIRE(east.hasIntAttribute(0, "treeCandidate"));
    REQUIRE(west.getIntAttribute(0, "treeCandidate", -1) != east.getIntAttribute(0, "treeCandidate", -1));
    std::vector<std::uint64_t> ids;
    for (const auto& point : west.points()) ids.push_back(point.id);
    for (const auto& point : east.points()) ids.push_back(point.id);
    std::sort(ids.begin(), ids.end());
    REQUIRE(std::adjacent_find(ids.begin(), ids.end()) == ids.end());

    Heightmap wrong = filled(3, 2, 1);
    REQUIRE(!applyTerrainTreesMultiTile(tiles, wrong, tree, bounds, TerrainTreeOperationMode::Add).ok());
    REQUIRE(west.getCount() == 4);
    REQUIRE(east.getCount() == 4);
    PointSet existing;
    existing.add(99, 0, 99);
    PointSet budgetEast        = east;
    tiles[1].trees             = &budgetEast;
    tiles[0].trees = &existing;
    tree.maxPoints = 8;
    auto budgetIgnoresExisting = applyTerrainTreesMultiTile(tiles, fitness, tree, bounds, TerrainTreeOperationMode::Add);
    REQUIRE(budgetIgnoresExisting.ok());
    REQUIRE(existing.getCount() == 5);
    tree.maxPoints = 100;
    tiles[0].trees = &west;
    tiles[1].trees = &east;
    REQUIRE_EQ(east.getCount(), 4);
    auto removed = applyTerrainTreesMultiTile(tiles, fitness, tree, bounds, TerrainTreeOperationMode::Remove);
    REQUIRE(removed.ok());
    REQUIRE(removed.value().changedSamples == 8);
    REQUIRE(west.empty());
    REQUIRE(east.empty());
}


TEST_CASE("procgen.multitile.touchOnlyAllowListAndWorldMapDomain") {
    Heightmap                      a = filled(3, 3, 0), b = filled(3, 3, 0), world = filled(3, 3, 0);
    Heightmap                      stamp = filled(1, 1, 4), one = filled(1, 1, 1);
    std::vector<TerrainHeightTile> tiles{
        {"a", &a, 0, 0, 2, 2, false}, {"b", &b, 2, 0, 2, 2, false}, {"world", &world, 0, 0, 2, 2, true}};
    auto ordinary = applyTerrainStampMultiTile(tiles, stamp, setStamp(1, 1, 2, 2), one, one, false, {"a", "b"});
    REQUIRE(ordinary.ok());
    CHECK(ordinary.value().affectedTiles == 1);
    CHECK(a.height(1, 1) == 4);
    CHECK(b.height(0, 1) == 0);
    CHECK(world.height(1, 1) == 0);
    auto worldOnly = applyTerrainStampMultiTile(tiles, stamp, setStamp(1, 1, 2, 2), one, one, true);
    REQUIRE(worldOnly.ok());
    CHECK(worldOnly.value().affectedTiles == 1);
    CHECK(world.height(1, 1) == 4);
}

TEST_CASE("procgen.multitile.lateFailureAndDescriptorErrorsPreserveEveryTile") {
    Heightmap a = filled(2, 2, 1), b = filled(2, 2, std::numeric_limits<float>::max() * 0.8F);
    Heightmap stamp = filled(1, 1, std::numeric_limits<float>::max() * 0.4F), one = filled(1, 1, 1);
    std::vector<TerrainHeightTile> tiles{{"a", &a, 0, 0, 1, 1, false}, {"b", &b, 1, 0, 1, 1, false}};
    TerrainStampSettings           settings = setStamp(1, 0.5, 2, 1);
    settings.operation                      = TerrainStampOperation::Add;
    const auto beforeA = a.data(), beforeB = b.data();
    CHECK(!applyTerrainStampMultiTile(tiles, stamp, settings, one, one).ok());
    CHECK(a.data() == beforeA);
    CHECK(b.data() == beforeB);
    tiles[1].heightmap = &a;
    CHECK(!applyTerrainStampMultiTile(tiles, stamp, settings, one, one).ok());
    CHECK(a.data() == beforeA);
    CHECK(b.data() == beforeB);
    tiles[1] = {"b", &b, 1.25, 0, 1, 1, false};
    CHECK(!applyTerrainStampMultiTile(tiles, stamp, settings, one, one).ok());
    CHECK(a.data() == beforeA);
    CHECK(b.data() == beforeB);
}

TEST_CASE("procgen.multitile.workspaceOwnsInputsAndPublishesReport") {
    Heightmap source = filled(3, 3, 1), out = filled(3, 3, 0), stamp = filled(1, 1, 6), one = filled(1, 1, 1);
    TerrainMultiTileWorkspace workspace;
    REQUIRE(workspace.addTile("tile", source, 0, 0, 2, 2).ok());
    source.data()[4] = 99;
    REQUIRE(workspace.stamp(stamp, setStamp(1, 1, 2, 2), one, one).ok());
    CHECK(workspace.getTileCount() == 1);
    CHECK(workspace.getLastAffectedTiles() == 1);
    CHECK(workspace.getLastMappingCount() == 1);
    REQUIRE(workspace.copyTile("tile", out).ok());
    CHECK(out.height(1, 1) == 6);
    CHECK(workspace.getOperationCount() == 1);
    CHECK(workspace.getAppliedCount() == 1);
    REQUIRE(workspace.undo().ok());
    CHECK(workspace.getAppliedCount() == 0);
    REQUIRE(workspace.copyTile("tile", out).ok());
    CHECK(out.height(1, 1) == 1);
    REQUIRE(workspace.redo().ok());
    REQUIRE(workspace.copyTile("tile", out).ok());
    CHECK(out.height(1, 1) == 6);
    CHECK(!workspace.addTile("tile", source, 2, 0, 2, 2).ok());
    REQUIRE(workspace.copyTile("tile", out).ok());
    CHECK(out.height(1, 1) == 6);
}


TEST_CASE("procgen.terrainStitcher.matchesPcgSeamAndFailsAtomically") {
    Heightmap                   south = filled(5, 5, 0), north = filled(5, 5, 1);
    TerrainHeightTile           a{"south", &south, 0, 0, 4, 4, false};
    TerrainHeightTile           b{"north", &north, 0, 4, 4, 4, false};
    TerrainHeightStitchSettings settings;
    settings.extraSeamSize = 1;
    auto stitched          = stitchTerrainHeightmaps(a, b, settings);
    REQUIRE(stitched.ok());
    CHECK(stitched.value() == 10);
    for (int x = 0; x < 5; ++x) {
        CHECK(south.height(x, 4) == 0.5F);
        CHECK(north.height(x, 0) == 0.5F);
        CHECK(south.height(x, 3) == 0);
        CHECK(north.height(x, 1) == 1);
    }
    const auto beforeSouth = south.data(), beforeNorth = north.data();
    b.originZ = 9;
    CHECK(!stitchTerrainHeightmaps(a, b, settings).ok());
    CHECK(south.data() == beforeSouth);
    CHECK(north.data() == beforeNorth);
}
}  // namespace test_terrain_multitile_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_probe)
#include <algorithm>
#include <cmath>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "common/ProcgenProbeSink.h"
#include "common/Value.h"
#include "image/ImageData.h"
#include "procgen/PointSet.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainBiomePreset.h"
#include "procgen/heightmap/TerrainDetailLayer.h"
#include "procgen/heightmap/TerrainImageAdapter.h"
#include "procgen/heightmap/TerrainMultiTile.h"
#include "procgen/heightmap/TerrainObjectPlacement.h"
#include "procgen/heightmap/TerrainProbePlacement.h"
#include "procgen/heightmap/TerrainSpawnPlan.h"
#include "procgen/heightmap/TerrainSplatmap.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "procgen/heightmap/TerrainTerraceRemover.h"
#include "procgen/heightmap/TerrainTreePlacement.h"
#include "procgen/heightmap/TerrainWorldWorkspace.h"
#include "zeroerr/unittest.h"
// terrain_probe.cpp
namespace test_terrain_probe_unit {
// terrain_probe.cpp
using namespace eve::procgen;

TEST_CASE("procgen.probe.reflectionLightHeightMetadataDeterminismAndAtomicFailure") {
    Heightmap fitness(2, 2), heights(2, 2);
    std::fill(fitness.data().begin(), fitness.data().end(), 1.0F);
    std::fill(heights.data().begin(), heights.data().end(), 0.5F);
    TerrainProbePlacementSettings settings;
    settings.name  = "forest-probe";
    settings.width = settings.depth = 2;
    settings.spacing                = 1;
    settings.jitterPercent          = 0;
    settings.minimumFitness         = 0;
    settings.heightScale            = 10;
    settings.seaLevelActive         = true;
    settings.seaLevel               = 4;
    settings.reflectionOffset       = 2;
    settings.namespaceId            = 901;
    PointSet first, second;
    REQUIRE(exportTerrainProbePoints(first, fitness, heights, settings).ok());
    REQUIRE(exportTerrainProbePoints(second, fitness, heights, settings).ok());
    REQUIRE(first.getCount() == 9);
    REQUIRE(second.getCount() == first.getCount());
    for (int i = 0; i < first.getCount(); ++i) {
        CHECK(first.points()[i].id == second.points()[i].id);
        CHECK(first.points()[i].x == second.points()[i].x);
        CHECK(first.points()[i].z == second.points()[i].z);
    }
    CHECK(first.points()[0].y == 7);
    CHECK(first.getStringAttribute(0, "probeResource", "") == "forest-probe");
    CHECK(first.getIntAttribute(0, "probeType", -1) == 0);
    settings.type        = TerrainProbeType::Light;
    settings.lightOffset = 2.5F;
    REQUIRE(exportTerrainProbePoints(second, fitness, heights, settings).ok());
    CHECK(second.points()[0].y == 7.5F);
    CHECK(second.getIntAttribute(0, "probeType", -1) == 1);
    const int  beforeCount        = second.getCount();
    const auto beforeId           = second.points()[0].id;
    settings.reflectionResolution = 24;
    CHECK(!exportTerrainProbePoints(second, fitness, heights, settings).ok());
    CHECK(second.getCount() == beforeCount);
    CHECK(second.points()[0].id == beforeId);
}


TEST_CASE("procgen.probe.multiTileAddReplaceRemoveAndAtomicFailure") {
    Heightmap fitness(4, 2), leftHeight(3, 3), rightHeight(3, 3);
    std::fill(fitness.data().begin(), fitness.data().end(), 1.0F);
    std::fill(leftHeight.data().begin(), leftHeight.data().end(), 0.25F);
    std::fill(rightHeight.data().begin(), rightHeight.data().end(), 0.75F);
    PointSet                      left, right;
    std::vector<TerrainProbeTile> tiles{{"left", &left, &leftHeight, -2, -1, 2, 2, 2, 2, false},
                                        {"right", &right, &rightHeight, 0, -1, 2, 2, 2, 2, false}};
    TerrainProbePlacementSettings settings;
    settings.name          = "world-probes";
    settings.spacing       = 1;
    settings.jitterPercent = settings.minimumFitness = 0;
    settings.heightScale                             = 8;
    settings.seaLevelActive                          = true;
    settings.reflectionOffset                        = 1;
    settings.namespaceId                             = 903;
    settings.maxPoints                               = 100;
    TerrainStampSettings operation;
    operation.width = 4;
    operation.depth = 2;
    auto added      = applyTerrainProbesMultiTile(tiles, fitness, settings, operation, TerrainProbeOperationMode::Add);
    REQUIRE(added.ok());
    CHECK(added.value().affectedTiles == 2);
    CHECK(!left.empty());
    CHECK(!right.empty());
    CHECK(left.points()[0].y == 3);
    CHECK(right.points()[0].y == 7);
    settings.type = TerrainProbeType::Light;
    REQUIRE(applyTerrainProbesMultiTile(tiles, fitness, settings, operation, TerrainProbeOperationMode::Replace).ok());
    CHECK(left.getIntAttribute(0, "probeType", -1) == 1);
    REQUIRE(applyTerrainProbesMultiTile(tiles, fitness, settings, operation, TerrainProbeOperationMode::Remove).ok());
    CHECK(left.empty());
    CHECK(right.empty());
    settings.name = "atomic";
    REQUIRE(applyTerrainProbesMultiTile(tiles, fitness, settings, operation, TerrainProbeOperationMode::Add).ok());
    const auto before             = left.points()[0].id;
    settings.reflectionResolution = 24;
    CHECK(!applyTerrainProbesMultiTile(tiles, fitness, settings, operation, TerrainProbeOperationMode::Replace).ok());
    CHECK(left.points()[0].id == before);
}

// terrain_spawn_plan.cpp


using namespace eve::procgen;

namespace {
[[maybe_unused]] TerrainWorldCreationSettings worldSettings() {
    TerrainWorldCreationSettings settings;
    settings.tilesX                   = 2;
    settings.tilesZ                   = 1;
    settings.tileSize                 = 2;
    settings.heightmapResolution      = 3;
    settings.controlTextureResolution = settings.detailResolution = 2;
    settings.treeResolution = settings.objectResolution = 2;
    settings.splatLayers                                = 2;
    return settings;
}
[[maybe_unused]] TerrainStampSettings bounds() {
    TerrainStampSettings value;
    value.centerX = value.centerZ = 0;
    value.width                   = 4;
    value.depth                   = 2;
    return value;
}
[[maybe_unused]] Heightmap fitness() {
    Heightmap value(4, 2);
    std::fill(value.data().begin(), value.data().end(), 1.0F);
    return value;
}
[[maybe_unused]] TerrainTreePlacementSettings trees() {
    TerrainTreePlacementSettings value;
    value.spacing = value.spawnDensity = 1;
    value.jitterPercent = value.failureRate = value.minimumFitness = 0;
    value.minimumWidth = value.maximumWidth = value.minimumHeight = value.maximumHeight = 1;
    value.asset                                                                         = "oak";
    value.namespaceId                                                                   = 1201;
    value.maxPoints                                                                     = 100;
    return value;
}
[[maybe_unused]] TerrainObjectPlacementSettings objects() {
    TerrainObjectPlacementSettings value;
    value.spacing = value.spawnDensity = 1;
    value.jitterPercent = value.failureRate = value.minimumFitness = value.minimumInstanceFitness = 0;
    value.boundsRadius                                                                            = 0.2F;
    value.prototype                                                                               = "rocks";
    value.namespaceId                                                                             = 1301;
    value.maxPoints                                                                               = 100;
    TerrainObjectInstanceSettings child;
    child.asset = "rock";
    REQUIRE(value.addInstance(child).ok());
    return value;
}
}  // namespace

TEST_CASE("procgen.spawnPlan.mixedRulesPublishAndUndoAsOneWorldOperation") {
    TerrainWorldWorkspace world;
    REQUIRE(world.create(worldSettings()).ok());
    auto                  map       = fitness();
    auto                  operation = bounds();
    TerrainDetailSettings detail;
    detail.minimumFitness = detail.fadeStart = 0;
    detail.density                           = 3;
    detail.namespaceId                       = 1101;
    auto                          tree       = trees();
    auto                          object     = objects();
    TerrainProbePlacementSettings probe;
    probe.name          = "reflection-grid";
    probe.spacing       = 1;
    probe.jitterPercent = probe.minimumFitness = 0;
    probe.namespaceId                          = 1401;
    probe.maxPoints                            = 100;
    Heightmap modifier(1, 1), one(1, 1);
    modifier.data()[0] = one.data()[0] = 1;
    auto modifierOperation             = operation;
    modifierOperation.operation        = TerrainStampOperation::Set;
    TerrainSpawnPlan plan;
    REQUIRE(plan.addModifierStamp("modifier-guid", modifier, modifierOperation, one, one).ok());
    REQUIRE(plan.addSplat("texture-guid", map, 1, operation).ok());
    REQUIRE(plan.addDetail("detail-guid", map, detail, operation, TerrainDetailMode::Replace, 11).ok());
    REQUIRE(plan.addTrees("tree-guid", map, tree, operation, TerrainTreeOperationMode::Add).ok());
    REQUIRE(plan.addObjects("object-guid", map, object, operation, TerrainObjectOperationMode::Add).ok());
    REQUIRE(plan.addProbes("probe-guid", map, probe, operation, TerrainProbeOperationMode::Add).ok());
    CHECK(!plan.addTrees("tree-guid", map, tree, operation, TerrainTreeOperationMode::Add).ok());
    auto encoded = plan.snapshotJson();
    REQUIRE(encoded.ok());
    TerrainSpawnPlan roundTrip;
    REQUIRE(roundTrip.restoreJson(encoded.value()).ok());
    CHECK(roundTrip.snapshotJson().value() == encoded.value());
    std::fill(map.data().begin(), map.data().end(), 0.0F);
    tree.namespaceId = 0;
    REQUIRE(world.spawn(roundTrip).ok());
    CHECK(world.getOperationCount() == 1);
    TerrainSplatmap    splatOut;
    TerrainDetailLayer detailOut;
    PointSet           treeOut, objectOut, probeOut;
    Heightmap          heightsOut;
    REQUIRE(world.copyHeightmap(0, heightsOut).ok());
    CHECK(std::any_of(heightsOut.data().begin(), heightsOut.data().end(), [](float value) { return value > 0; }));
    REQUIRE(world.copySplatmap(0, splatOut).ok());
    CHECK(splatOut.sample(1, 0, 0).value() == 1.0F);
    REQUIRE(world.copyDetail(0, detailOut).ok());
    CHECK(detailOut.sampleResource(1101, 0, 0).value() == 3);
    REQUIRE(world.copyTrees(0, treeOut).ok());
    REQUIRE(world.copyObjects(0, objectOut).ok());
    REQUIRE(world.copyProbes(0, probeOut).ok());
    CHECK(!treeOut.empty());
    CHECK(!objectOut.empty());
    CHECK(!probeOut.empty());
    REQUIRE(world.undo().ok());
    REQUIRE(world.copySplatmap(0, splatOut).ok());
    CHECK(splatOut.sample(0, 0, 0).value() == 1.0F);
    CHECK(splatOut.sample(1, 0, 0).value() == 0.0F);
    REQUIRE(world.copyDetail(0, detailOut).ok());
    REQUIRE(world.copyTrees(0, treeOut).ok());
    REQUIRE(world.copyObjects(0, objectOut).ok());
    REQUIRE(world.copyProbes(0, probeOut).ok());
    CHECK(detailOut.sample(0, 0).value() == 0);
    CHECK(treeOut.empty());
    CHECK(objectOut.empty());
    CHECK(probeOut.empty());
}

TEST_CASE("procgen.spawnPlan.failureAndDisabledRulesAreAtomic") {
    TerrainWorldWorkspace world;
    REQUIRE(world.create(worldSettings()).ok());
    auto                  map       = fitness();
    auto                  operation = bounds();
    TerrainDetailSettings detail;
    detail.minimumFitness = detail.fadeStart = 0;
    detail.density                           = 4;
    detail.namespaceId                       = 2101;
    auto invalidObject                       = objects();
    invalidObject.namespaceId                = 0;
    TerrainSpawnPlan failing;
    REQUIRE(failing.addDetail("detail", map, detail, operation, TerrainDetailMode::Add, 2).ok());
    REQUIRE(failing.addObjects("bad-object", map, invalidObject, operation, TerrainObjectOperationMode::Add).ok());
    CHECK(!world.spawn(failing).ok());
    CHECK(world.getOperationCount() == 0);
    TerrainDetailLayer output;
    REQUIRE(world.copyDetail(0, output).ok());
    CHECK(output.sample(0, 0).value() == 0);

    TerrainSpawnPlan disabled;
    REQUIRE(disabled.addDetail("off", map, detail, operation, TerrainDetailMode::Add, 2).ok());
    REQUIRE(disabled.setEnabled("off", false).ok());
    CHECK(!world.spawn(disabled).ok());
    CHECK(!disabled.setEnabled("missing", true).ok());
}


TEST_CASE("procgen.spawnPlan.stagedWorldSpawnPublishesOnceAndFailureRollsBack") {
    auto                  map       = fitness();
    auto                  operation = bounds();
    TerrainDetailSettings detail;
    detail.minimumFitness = detail.fadeStart = 0;
    detail.density                           = 7;
    detail.namespaceId                       = 6101;
    TerrainSpawnPlan plan;
    REQUIRE(plan.addSplat("texture", map, 1, operation).ok());
    REQUIRE(plan.addDetail("detail", map, detail, operation, TerrainDetailMode::Replace, 31).ok());
    TerrainWorldWorkspace world;
    REQUIRE(world.create(worldSettings()).ok());
    REQUIRE(world.beginSpawn(plan).ok());
    auto pending = world.stepSpawn(1);
    REQUIRE(pending.ok());
    CHECK(pending.value() == TerrainWorldRunStatus::Pending);
    CHECK(world.getSpawnCompletedRules() == 1);
    TerrainSplatmap    splat;
    TerrainDetailLayer details;
    REQUIRE(world.copySplatmap(0, splat).ok());
    REQUIRE(world.copyDetail(0, details).ok());
    CHECK(splat.sample(1, 0, 0).value() == 0.0F);
    CHECK(details.sample(0, 0).value() == 0);
    CHECK(!world.undo().ok());
    REQUIRE(world.cancelSpawn().ok());
    CHECK(world.getSpawnStatus() == TerrainWorldRunStatus::Cancelled);
    CHECK(world.getOperationCount() == 0);
    REQUIRE(world.beginSpawn(plan).ok());
    REQUIRE(plan.setEnabled("detail", false).ok());
    auto completed = world.stepSpawn(8);
    REQUIRE(completed.ok());
    CHECK(completed.value() == TerrainWorldRunStatus::Completed);
    CHECK(world.getOperationCount() == 1);
    REQUIRE(world.copySplatmap(0, splat).ok());
    REQUIRE(world.copyDetail(0, details).ok());
    CHECK(splat.sample(1, 0, 0).value() == 1.0F);
    CHECK(details.sampleResource(6101, 0, 0).value() == 7);

    auto invalidObject        = objects();
    invalidObject.namespaceId = 0;
    TerrainSpawnPlan failing;
    REQUIRE(failing.addSplat("candidate-texture", map, 1, operation).ok());
    REQUIRE(failing.addObjects("invalid", map, invalidObject, operation, TerrainObjectOperationMode::Add).ok());
    TerrainWorldWorkspace clean;
    REQUIRE(clean.create(worldSettings()).ok());
    REQUIRE(clean.beginSpawn(failing).ok());
    CHECK(!clean.stepSpawn(2).ok());
    CHECK(clean.getSpawnStatus() == TerrainWorldRunStatus::Failed);
    CHECK(clean.getOperationCount() == 0);
    REQUIRE(clean.copySplatmap(0, splat).ok());
    CHECK(splat.sample(1, 0, 0).value() == 0.0F);
}

TEST_CASE("procgen.biomePreset.activationScopesOwnershipAndStableComposition") {
    auto                  map       = fitness();
    auto                  operation = bounds();
    TerrainDetailSettings detail;
    detail.minimumFitness = detail.fadeStart = 0;
    detail.density                           = 5;
    detail.namespaceId                       = 5101;
    TerrainSpawnPlan ground, canopy;
    REQUIRE(ground.addSplat("shared", map, 1, operation).ok());
    REQUIRE(ground.addDetail("detail", map, detail, operation, TerrainDetailMode::Replace, 23).ok());
    REQUIRE(canopy.addTrees("shared", map, trees(), operation, TerrainTreeOperationMode::Add).ok());
    TerrainBiomePreset preset;
    REQUIRE(preset.addSpawner("ground", ground, true, false, true).ok());
    REQUIRE(preset.addSpawner("canopy", canopy, false, true, false).ok());
    CHECK(!preset.addSpawner("ground", canopy, true, true, true).ok());
    REQUIRE(preset.getAutoAssignResources("ground").ok());
    CHECK(preset.getAutoAssignResources("ground").value());
    CHECK(!preset.getAutoAssignResources("canopy").value());
    auto biome   = preset.compileForBiome();
    auto stamper = preset.compileForStamper();
    REQUIRE(biome.ok());
    REQUIRE(stamper.ok());
    CHECK(biome.value().getRuleCount() == 2);
    CHECK(stamper.value().getRuleCount() == 1);

    REQUIRE(ground.addObjects("late", map, objects(), operation, TerrainObjectOperationMode::Add).ok());
    CHECK(preset.compileForBiome().value().getRuleCount() == 2);
    TerrainWorldWorkspace world;
    REQUIRE(world.create(worldSettings()).ok());
    REQUIRE(world.spawnBiome(preset).ok());
    TerrainSplatmap    splat;
    TerrainDetailLayer details;
    PointSet           treePoints;
    REQUIRE(world.copySplatmap(0, splat).ok());
    REQUIRE(world.copyDetail(0, details).ok());
    REQUIRE(world.copyTrees(0, treePoints).ok());
    CHECK(splat.sample(1, 0, 0).value() == 1.0F);
    CHECK(details.sampleResource(5101, 0, 0).value() == 5);
    CHECK(treePoints.empty());
    REQUIRE(world.spawnStamper(preset).ok());
    REQUIRE(world.copyTrees(0, treePoints).ok());
    CHECK(!treePoints.empty());
    REQUIRE(world.undo().ok());
    REQUIRE(world.copyTrees(0, treePoints).ok());
    CHECK(treePoints.empty());
    CHECK(!preset.setActiveInBiome("missing", true).ok());
}

TEST_CASE("procgen.biomePreset.strictSnapshotMigrationAndAtomicFailure") {
    auto             map       = fitness();
    auto             operation = bounds();
    TerrainSpawnPlan plan;
    REQUIRE(plan.addTrees("tree", map, trees(), operation, TerrainTreeOperationMode::Add).ok());
    TerrainBiomePreset source;
    REQUIRE(source.addSpawner("forest", plan, true, false, false).ok());
    auto json = source.snapshotJson();
    REQUIRE(json.ok());
    TerrainBiomePreset restored;
    REQUIRE(restored.restoreJson(json.value()).ok());
    CHECK(restored.snapshotJson().value() == json.value());
    const auto before = restored.snapshotJson().value();
    CHECK(
        !restored
             .restoreJson("{\"schema\":\"eve.procgen.terrain-biome-preset\",\"version\":1,\"entries\":[],\"extra\":1}")
             .ok());
    CHECK(restored.snapshotJson().value() == before);
    CHECK(!restored.restoreJson("{broken").ok());
    CHECK(restored.snapshotJson().value() == before);

    auto legacy = eve::Value::fromJson(json.value());
    REQUIRE(legacy.ok());
    legacy.value().set("version", 0);
    auto* entriesValue = legacy.value().find("entries");
    REQUIRE(entriesValue != nullptr);
    auto* entries = entriesValue->getIf<eve::Value::Array>();
    REQUIRE(entries != nullptr);
    auto* first = entries->at(0).getIf<eve::Value::Object>();
    REQUIRE(first != nullptr);
    first->erase("autoAssignResources");
    auto legacyJson = legacy.value().toJson();
    REQUIRE(legacyJson.ok());
    TerrainBiomePreset migrated;
    REQUIRE(migrated.restoreJson(legacyJson.value()).ok());
    CHECK(migrated.getAutoAssignResources("forest").value());
}

TEST_CASE("procgen.spawnPlan.strictSnapshotRoundTripMigrationAndAtomicFailure") {
    auto                  map       = fitness();
    auto                  operation = bounds();
    TerrainDetailSettings detail;
    detail.minimumFitness = detail.fadeStart = 0;
    detail.density                           = 6;
    detail.namespaceId                       = 4101;
    TerrainSpawnPlan source;
    REQUIRE(source.addDetail("stable-rule", map, detail, operation, TerrainDetailMode::Add, -17).ok());
    REQUIRE(source.setEnabled("stable-rule", false).ok());
    auto json = source.snapshotJson();
    REQUIRE(json.ok());
    TerrainSpawnPlan restored;
    REQUIRE(restored.restoreJson(json.value()).ok());
    REQUIRE(restored.snapshotJson().ok());
    CHECK(restored.snapshotJson().value() == json.value());

    const auto before = restored.snapshotJson().value();
    CHECK(!restored
               .restoreJson(
                   "{\"schema\":\"eve.procgen.terrain-spawn-plan\",\"version\":1,\"payload\":\"00\",\"extra\":1}")
               .ok());
    CHECK(restored.snapshotJson().value() == before);
    CHECK(!restored.restoreJson("{broken").ok());
    CHECK(restored.snapshotJson().value() == before);

    auto legacyValue = eve::Value::fromJson(json.value());
    REQUIRE(legacyValue.ok());
    legacyValue.value().set("version", 0);
    auto* payload = legacyValue.value().find("payload");
    REQUIRE(payload != nullptr);
    REQUIRE(payload->isString());
    std::string       legacyPayload     = payload->asString();
    const std::size_t enabledByteOffset = 4 + 1 + 4 + std::string("stable-rule").size();
    // Strip the two v4 stamp fields before converting this 5x3 fixture to v0.
    const std::size_t rasterBytes   = 8 + map.data().size() * sizeof(float);
    const std::size_t oldStampBytes = 9 * sizeof(double) + 3 * sizeof(float) + sizeof(int);
    legacyPayload.erase((enabledByteOffset + 1 + rasterBytes + oldStampBytes) * 2, 16);
    legacyPayload.erase(enabledByteOffset * 2, 2);
    legacyValue.value().set("payload", legacyPayload);
    auto legacyJson = legacyValue.value().toJson();
    REQUIRE(legacyJson.ok());
    TerrainSpawnPlan migrated;
    REQUIRE(migrated.restoreJson(legacyJson.value()).ok());
    CHECK(migrated.getRuleCount() == 1);
    CHECK(migrated.snapshotJson().value() != json.value());
}

// terrain_splatmap.cpp
using namespace eve::procgen;

TEST_CASE("procgen.terrainSplatmap.renormalizesTargetAndOtherLayers") {
    TerrainSplatmap splat;
    REQUIRE(splat.initialize(2, 1, 3, 0).ok());
    Heightmap paint(2, 1);
    paint.setHeight(0, 0, 0.25F);
    paint.setHeight(1, 0, 1);
    auto changed = paintTerrainSplatLayer(splat, paint, 1);
    REQUIRE(changed.ok());
    CHECK(changed.value() == 2);
    CHECK(splat.sample(0, 0, 0).value() == 0.375F);
    CHECK(splat.sample(1, 0, 0).value() == 0.25F);
    CHECK(splat.sample(2, 0, 0).value() == 0.375F);
    CHECK(splat.sample(0, 1, 0).value() == 0);
    CHECK(splat.sample(1, 1, 0).value() == 1);
    CHECK(splat.sample(2, 1, 0).value() == 0);
    CHECK(splat.getLastChangedSamples() == 2);
    Heightmap extracted(2, 1);
    REQUIRE(splat.copyLayer(1, extracted).ok());
    CHECK(extracted.data()[0] == 0.25F);
    CHECK(extracted.data()[1] == 1.0F);
    auto beforeExtract = extracted.data();
    CHECK(!splat.copyLayer(3, extracted).ok());
    CHECK(extracted.data() == beforeExtract);

    Heightmap repaint(2, 1);
    repaint.setHeight(0, 0, 0.5F);
    repaint.setHeight(1, 0, 0.5F);
    REQUIRE(paintTerrainSplatLayer(splat, repaint, 2).ok());
    CHECK(splat.sample(0, 0, 0).value() == 0.25F);
    CHECK(splat.sample(1, 0, 0).value() == 0.25F / 1.5F);
    CHECK(splat.sample(2, 0, 0).value() == 0.5F);
}

TEST_CASE("procgen.terrainSplatmap.failureIsAtomic") {
    TerrainSplatmap splat;
    REQUIRE(splat.initialize(2, 2, 2, 0).ok());
    Heightmap wrong(1, 2);
    CHECK(!paintTerrainSplatLayer(splat, wrong, 1).ok());
    CHECK(splat.sample(0, 1, 1).value() == 1);
    CHECK(splat.sample(1, 1, 1).value() == 0);
    Heightmap invalid(2, 2);
    invalid.setHeight(0, 0, 2);
    CHECK(!paintTerrainSplatLayer(splat, invalid, 1).ok());
    CHECK(splat.getLastChangedSamples() == 0);
}

TEST_CASE("procgen.terrainTextureAligner.blendsAdjacentEdgesAndIsAtomic") {
    TerrainSplatmap a, b;
    REQUIRE(a.initialize(4, 4, 2, 0).ok());
    REQUIRE(b.initialize(4, 4, 2, 1).ok());
    TerrainTextureAlignSettings settings;
    settings.terrainAOriginX = 0;
    settings.terrainAOriginZ = 0;
    settings.terrainAWidth   = 4;
    settings.terrainADepth   = 4;
    settings.terrainBOriginX = 0;
    settings.terrainBOriginZ = 4;
    settings.terrainBWidth   = 4;
    settings.terrainBDepth   = 4;
    settings.blendStrength   = 1;
    settings.blendWidth      = 2;
    auto aligned             = alignTerrainSplatTextures(a, b, settings);
    REQUIRE(aligned.ok());
    CHECK(aligned.value() == 16);
    int mixedA = 0, mixedB = 0;
    for (int x = 0; x < 4; ++x) {
        const float a0 = a.sample(0, x, 3).value(), a1 = a.sample(1, x, 3).value();
        const float b0 = b.sample(0, x, 0).value(), b1 = b.sample(1, x, 0).value();
        mixedA += a0 > 0 && a0 < 1;
        mixedB += b0 > 0 && b0 < 1;
        CHECK(std::abs(a0 + a1 - 1) < 1e-6F);
        CHECK(std::abs(b0 + b1 - 1) < 1e-6F);
    }
    CHECK(mixedA > 0);
    CHECK(mixedB > 0);
    const float preserved    = a.sample(0, 0, 3).value();
    settings.terrainBOriginZ = 20;
    CHECK(!alignTerrainSplatTextures(a, b, settings).ok());
    CHECK(a.sample(0, 0, 3).value() == preserved);
    CHECK(!alignTerrainSplatTextures(a, a, settings).ok());
}


TEST_CASE("procgen.terrainSplatmap.multiTileMapsAndPublishesTogether") {
    TerrainSplatmap west, east;
    REQUIRE(west.initialize(2, 2, 2, 0).ok());
    REQUIRE(east.initialize(2, 2, 2, 0).ok());
    Heightmap paint(4, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x) paint.setHeight(x, y, x < 2 ? 0.25F : 0.75F);
    TerrainStampSettings operation;
    operation.centerX = 2;
    operation.centerZ = 1;
    operation.width   = 4;
    operation.depth   = 2;
    std::vector<TerrainSplatTile> tiles{{"west", &west, 0, 0, 2, 2, false}, {"east", &east, 2, 0, 2, 2, false}};
    auto                          result = paintTerrainSplatLayerMultiTile(tiles, paint, 1, operation);
    REQUIRE(result.ok());
    CHECK(result.value().affectedTiles == 2);
    CHECK(result.value().changedSamples == 8);
    CHECK(west.sample(1, 1, 1).value() == 0.25F);
    CHECK(east.sample(1, 0, 0).value() == 0.75F);
    Heightmap wrong(3, 2);
    CHECK(!paintTerrainSplatLayerMultiTile(tiles, wrong, 1, operation).ok());
    CHECK(west.sample(1, 1, 1).value() == 0.25F);
    CHECK(east.sample(1, 0, 0).value() == 0.75F);
}


TEST_CASE("procgen.terrainSplatmap.bakesPaletteIntoAtomicAlbedo") {
    TerrainSplatmap splat;
    REQUIRE(splat.initialize(2, 1, 2, 0).ok());
    Heightmap paint(2, 1);
    paint.setHeight(0, 0, 0.25F);
    paint.setHeight(1, 0, 0.75F);
    REQUIRE(paintTerrainSplatLayer(splat, paint, 1).ok());
    TerrainSplatPalette palette;
    REQUIRE(palette.addColor(0.2F, 0.4F, 0.1F).ok());
    REQUIRE(palette.addColor(0.8F, 0.6F, 0.3F).ok());
    eve::image::ImageData image(2, 1, "RGBA32F");
    auto                  baked = bakeTerrainSplatAlbedo(image, splat, palette);
    REQUIRE(baked.ok());
    CHECK(baked.value() == 2);
    CHECK(std::abs(image.getPixel(0, 0).r - 0.35F) < 1e-6F);
    CHECK(std::abs(image.getPixel(1, 0).r - 0.65F) < 1e-6F);
    const auto          preserved = image.getPixel(0, 0);
    TerrainSplatPalette incomplete;
    REQUIRE(incomplete.addColor(1, 0, 0).ok());
    CHECK(!bakeTerrainSplatAlbedo(image, splat, incomplete).ok());
    CHECK(image.getPixel(0, 0).r == preserved.r);
}


TEST_CASE("procgen.gtsPackedLayers.matchesTriplanarAndStochasticSampling") {
    TerrainSplatmap splat;
    REQUIRE(splat.initialize(2, 2, 1, 0).ok());
    Heightmap heights(2, 2);
    heights.setHeight(0, 0, 0);
    heights.setHeight(1, 0, 2);
    heights.setHeight(0, 1, 0);
    heights.setHeight(1, 1, 2);
    eve::image::ImageData albedoTexture(2, 2, "RGBA32F"), normalTexture(1, 1, "RGBA32F");
    albedoTexture.setPixel(0, 0, {0, 0, 0, 0});
    albedoTexture.setPixel(1, 0, {1, 0, 0, 0.25F});
    albedoTexture.setPixel(0, 1, {0, 1, 0, 0.75F});
    albedoTexture.setPixel(1, 1, {0, 0, 1, 1});
    normalTexture.setPixel(0, 0, {0.5F, 0.5F, 1, 0.5F});
    auto bake = [&](GtsPackedLayerSettings settings) {
        GtsPackedLayerSet layers;
        REQUIRE(layers.addLayer(albedoTexture, normalTexture, settings).ok());
        eve::image::ImageData albedo(2, 2, "RGBA32F"), normal(2, 2, "RGBA32F");
        Heightmap             geo(2, 2), detail(2, 2);
        REQUIRE(bakeGtsPackedLayers(albedo, normal, geo, detail, heights, splat, layers, 3, 5, 7, 1, 1).ok());
        return albedo.getPixel(1, 1);
    };
    GtsPackedLayerSettings planar;
    const auto             plain = bake(planar);
    GtsPackedLayerSettings stochastic;
    stochastic.stochastic         = true;
    const auto             random = bake(stochastic);
    GtsPackedLayerSettings triplanar;
    triplanar.triPlanar      = true;
    triplanar.triPlanarSizeX = 0.4F;
    triplanar.triPlanarSizeZ = 0.7F;
    const auto projected     = bake(triplanar);
    CHECK(std::abs(plain.r - random.r) + std::abs(plain.g - random.g) + std::abs(plain.b - random.b) > 1e-5F);
    CHECK(std::abs(plain.r - projected.r) + std::abs(plain.g - projected.g) + std::abs(plain.b - projected.b) > 1e-5F);
}


TEST_CASE("procgen.gtsWeather.matchesSnowThenRainAlbedoBranches") {
    Heightmap heights(2, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) heights.setHeight(x, y, 100);
    eve::image::ImageData output(2, 2, "RGBA32F"), snowAlbedo(1, 1, "RGBA32F"), snowMask(1, 1, "RGBA32F");
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) output.setPixel(x, y, {0.2F, 0.4F, 0.1F, 1});
    snowAlbedo.setPixel(0, 0, {0.8F, 0.9F, 1, 1});
    snowMask.setPixel(0, 0, {0.1F, 0.2F, 0.3F, 1});
    GtsSnowSurfaceSettings snow;
    snow.enabled       = true;
    snow.minimumHeight = 100;
    snow.blendRange    = 20;
    GtsRainSurfaceSettings rain;
    rain.enabled       = true;
    rain.maximumHeight = 3000;
    auto baked         = bakeGtsWeatherAlbedo(output, heights, snowAlbedo, snowMask, snow, rain);
    REQUIRE(baked.ok());
    CHECK(baked.value() == 4);
    CHECK(std::abs(output.getPixel(0, 0).r - 0.41F) < 1e-6F);
    CHECK(std::abs(output.getPixel(0, 0).g - 0.533F) < 1e-6F);
    CHECK(std::abs(output.getPixel(0, 0).b - 0.451F) < 1e-6F);
    CHECK(output.getPixel(0, 0).a == 1);

    const auto preserved = output.getPixel(0, 0);
    snow.scale           = 0;
    CHECK(!bakeGtsWeatherAlbedo(output, heights, snowAlbedo, snowMask, snow, rain).ok());
    CHECK(output.getPixel(0, 0).r == preserved.r);
}

TEST_CASE("procgen.gtsColorVariation.matchesGtsShaderOrderAndMath") {
    eve::image::ImageData albedo(1, 1, "RGBA32F"), colorMap(1, 1, "RGBA32F"), variation(1, 1, "RGBA32F");
    Heightmap             blend(1, 1);
    albedo.setPixel(0, 0, {0.2F, 0.4F, 0.6F, 1});
    colorMap.setPixel(0, 0, {0.8F, 0.2F, 0.4F, 0.5F});
    variation.setPixel(0, 0, {0.25F, 0, 0, 1});
    blend.setHeight(0, 0, 0.5F);
    GtsColorMapSettings color;
    color.alphaIntensity = 2;
    color.colorIntensity = 0.5F;
    color.nearIntensity  = 0.25F;
    color.farIntensity   = 0.75F;
    REQUIRE(bakeGtsColorMapAlbedo(albedo, colorMap, blend, color).ok());
    CHECK(std::abs(albedo.getPixel(0, 0).r - 0.3F) < 1e-6F);
    CHECK(std::abs(albedo.getPixel(0, 0).g - 0.25F) < 1e-6F);
    GtsMacroVariationSettings macro;
    macro.intensity = 0.5F;
    REQUIRE(bakeGtsMacroVariationAlbedo(albedo, variation, macro).ok());
    const float multiplier = 0.5F + 0.5F * (0.75F * 0.75F * 0.75F);
    CHECK(std::abs(albedo.getPixel(0, 0).r - 0.3F * multiplier) < 1e-6F);
    CHECK(albedo.getPixel(0, 0).a == 1);
}


TEST_CASE("procgen.gtsGeological.blendsHeightStripColorAndPackedNormal") {
    Heightmap heights(1, 1), strength(1, 1), distance(1, 1);
    heights.setHeight(0, 0, 0);
    strength.setHeight(0, 0, 0.5F);
    distance.setHeight(0, 0, 0.25F);
    eve::image::ImageData albedo(1, 1, "RGBA32F"), packed(1, 1, "RGBA32F");
    eve::image::ImageData geoAlbedo(1, 1, "RGBA32F"), geoNormal(1, 1, "RGBA32F");
    albedo.setPixel(0, 0, {0.2F, 0.3F, 0.4F, 1});
    packed.setPixel(0, 0, {0.5F, 0.5F, 0.7F, 0.8F});
    geoAlbedo.setPixel(0, 0, {0.5F, 0.25F, 0.75F, 1});
    geoNormal.setPixel(0, 0, {0, 0.75F, 0, 0.625F});
    GtsGeologicalSettings geo;
    geo.enabled            = true;
    geo.nearStrength       = 0.2F;
    geo.farStrength        = 0.4F;
    geo.nearNormalStrength = 0.4F;
    geo.farNormalStrength  = 0.8F;
    auto result = bakeGtsGeologicalSurface(albedo, packed, heights, strength, distance, geoAlbedo, geoNormal, geo);
    REQUIRE(result.ok());
    CHECK(result.value() == 2);
    CHECK(std::abs(albedo.getPixel(0, 0).r - 0.3225F) < 1e-6F);
    CHECK(std::abs(albedo.getPixel(0, 0).g - 0.335F) < 1e-6F);
    CHECK(packed.getPixel(0, 0).r > 0.53F);
    CHECK(packed.getPixel(0, 0).g > 0.57F);
    CHECK(packed.getPixel(0, 0).b == 0.7F);
    CHECK(packed.getPixel(0, 0).a == 0.8F);
}


TEST_CASE("procgen.gtsDetail.matchesNearFarNormalAndMicroShadow") {
    Heightmap strength(1, 1), distance(1, 1), greyscale(1, 1);
    strength.setHeight(0, 0, 0.5F);
    distance.setHeight(0, 0, 0.25F);
    eve::image::ImageData albedo(1, 1, "RGBA32F"), packed(1, 1, "RGBA32F"), detail(1, 1, "RGBA32F");
    albedo.setPixel(0, 0, {0.8F, 0.4F, 0.2F, 1});
    packed.setPixel(0, 0, {0.5F, 0.5F, 0.7F, 0.8F});
    detail.setPixel(0, 0, {0.75F, 0.25F, 0, 1});
    GtsDetailNormalSettings settings;
    settings.nearStrength = 0.4F;
    settings.farStrength  = 0.8F;
    auto result           = bakeGtsDetailSurface(albedo, packed, greyscale, strength, distance, detail, settings);
    REQUIRE(result.ok());
    CHECK(result.value() == 3);
    CHECK(std::abs(greyscale.height(0, 0) - 0.125F) < 1e-6F);
    CHECK(std::abs(albedo.getPixel(0, 0).r - 0.775F) < 1e-6F);
    CHECK(std::abs(packed.getPixel(0, 0).r - 0.5625F) < 1e-6F);
    CHECK(std::abs(packed.getPixel(0, 0).g - 0.4375F) < 1e-6F);
    CHECK(packed.getPixel(0, 0).b == 0.7F);
    CHECK(packed.getPixel(0, 0).a == 0.8F);
}


TEST_CASE("procgen.gtsWeather.bakesPackedPbrAndDisplacementAtomically") {
    Heightmap heights(1, 1), displacement(1, 1), tessellation(1, 1);
    heights.setHeight(0, 0, 100);
    eve::image::ImageData normal(1, 1, "RGBA32F"), mask(1, 1, "RGBA32F");
    eve::image::ImageData snowNormal(1, 1, "RGBA32F"), snowMask(1, 1, "RGBA32F"), rainData(1, 1, "RGBA32F");
    normal.setPixel(0, 0, {0.5F, 0.5F, 1, 1});
    mask.setPixel(0, 0, {0, 0, 0, 0});
    snowNormal.setPixel(0, 0, {0.5F, 0.5F, 1, 1});
    snowMask.setPixel(0, 0, {0.2F, 0.4F, 0.5F, 0.6F});
    rainData.setPixel(0, 0, {0, 0, 0, 0});
    GtsSnowSurfaceSettings snow;
    snow.enabled       = true;
    snow.minimumHeight = 100;
    snow.blendRange    = 20;
    GtsRainSurfaceSettings rain;
    rain.enabled       = true;
    rain.maximumHeight = 3000;
    auto baked = bakeGtsWeatherPbr(normal, mask, displacement, tessellation, heights, snowNormal, snowMask, rainData,
                                   snow, rain, 0);
    REQUIRE(baked.ok());
    CHECK(baked.value() == 4);
    CHECK(std::abs(mask.getPixel(0, 0).r - 0.1F) < 1e-6F);
    CHECK(std::abs(mask.getPixel(0, 0).b - 0.75F) < 1e-6F);
    CHECK(std::abs(mask.getPixel(0, 0).a - 0.75F) < 1e-6F);
    CHECK(std::abs(displacement.height(0, 0) - 0.35F) < 1e-6F);
    CHECK(std::abs(tessellation.height(0, 0) - 12.5F) < 1e-6F);
    const auto before = mask.getPixel(0, 0);
    CHECK(!bakeGtsWeatherPbr(normal, mask, displacement, tessellation, heights, snowNormal, snowMask, rainData, snow,
                             rain, std::numeric_limits<double>::infinity())
               .ok());
    CHECK(mask.getPixel(0, 0).a == before.a);
    CHECK(displacement.height(0, 0) == 0.35F);
}


TEST_CASE("procgen.gtsWeather.rainNormalsUseExplicitDeterministicTime") {
    Heightmap heights(2, 2), displacementA(2, 2), tessellationA(2, 2), displacementB(2, 2), tessellationB(2, 2);
    eve::image::ImageData normalA(2, 2, "RGBA32F"), normalB(2, 2, "RGBA32F");
    eve::image::ImageData maskA(2, 2, "RGBA32F"), maskB(2, 2, "RGBA32F");
    eve::image::ImageData snowNormal(1, 1, "RGBA32F"), snowMask(1, 1, "RGBA32F"), rainData(2, 2, "RGBA32F");
    snowNormal.setPixel(0, 0, {0.5F, 0.5F, 1, 1});
    snowMask.setPixel(0, 0, {0, 0, 0, 0});
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) {
            heights.setHeight(x, y, 100);
            normalA.setPixel(x, y, {0.5F, 0.5F, 1, 1});
            normalB.setPixel(x, y, {0.5F, 0.5F, 1, 1});
            maskA.setPixel(x, y, {0, 0, 0, 0.2F});
            maskB.setPixel(x, y, {0, 0, 0, 0.2F});
            const float phase = 0.1F + 0.2F * x + 0.3F * y;
            rainData.setPixel(x, y, {phase, phase * 0.7F, phase * 0.4F, phase * 0.2F});
        }
    GtsSnowSurfaceSettings snow;
    GtsRainSurfaceSettings rain;
    rain.enabled       = true;
    rain.maximumHeight = 3000;
    rain.scale         = 3;
    REQUIRE(bakeGtsWeatherPbr(normalA, maskA, displacementA, tessellationA, heights, snowNormal, snowMask, rainData,
                              snow, rain, 0.125)
                .ok());
    REQUIRE(bakeGtsWeatherPbr(normalB, maskB, displacementB, tessellationB, heights, snowNormal, snowMask, rainData,
                              snow, rain, 0.625)
                .ok());
    const auto a = normalA.getPixel(0, 0), b = normalB.getPixel(0, 0);
    CHECK(std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b) > 1e-5F);
}


// terrain_terrace_remover.cpp
using namespace eve::procgen;

TEST_CASE("procgen.terraceRemoval.classificationPriorityAndEdges") {
    Heightmap source(5, 5), classes(5, 5);
    source.data().assign(25, 0.1F);
    source.setHeight(2, 1, 0.F);
    source.setHeight(2, 3, 0.3F);
    TerrainTerraceRemovalSettings settings;
    settings.minimumTerraceThreshold = 0.2F;
    settings.maximumTerraceThreshold = 0.4F;
    REQUIRE(analyzeTerrainTerraces(classes, source, settings).ok());
    CHECK(classes.height(2, 2) == float(TerrainTerraceClass::Green));
    CHECK(classes.height(0, 0) == float(TerrainTerraceClass::Black));

    const auto before                = classes.data();
    settings.minimumTerraceThreshold = 0.5F;
    settings.maximumTerraceThreshold = 0.4F;
    CHECK(!analyzeTerrainTerraces(classes, source, settings).ok());
    CHECK(classes.data() == before);
    CHECK(!analyzeTerrainTerraces(source, source, TerrainTerraceRemovalSettings{}).ok());
}

TEST_CASE("procgen.terraceRemoval.pipelineAndAtomicFailure") {
    Heightmap source(7, 7), classes(7, 7), output(7, 7);
    for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 7; ++x) source.setHeight(x, y, 0.005F * float(y) + (x == 3 ? 0.003F : 0.F));
    classes.data().assign(49, float(TerrainTerraceClass::Green));
    output.data().assign(49, -1.F);
    TerrainTerraceRemovalSettings settings;
    settings.perlinStrength = 0.F;
    REQUIRE(removeTerrainTerraces(output, source, classes, settings).ok());
    CHECK(output.height(0, 0) == source.height(0, 0));
    CHECK(output.height(6, 6) == source.height(6, 6));
    CHECK(output.height(3, 3) < source.height(3, 3));
    for (float value : output.data()) CHECK(std::isfinite(value));

    const auto before    = output.data();
    settings.perlinScale = -1.F;
    CHECK(!removeTerrainTerraces(output, source, classes, settings).ok());
    CHECK(output.data() == before);
    CHECK(!removeTerrainTerraces(classes, source, classes, TerrainTerraceRemovalSettings{}).ok());
}


// terrain_world_workspace.cpp
using namespace eve::procgen;

namespace {
[[maybe_unused]] Heightmap filled(int width, int height, float value) {
    Heightmap result(width, height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) result.setHeight(x, y, value);
    return result;
}
[[maybe_unused]] TerrainWorldCreationSettings smallWorld() {
    TerrainWorldCreationSettings s;
    s.tilesX                   = 2;
    s.tilesZ                   = 1;
    s.tileSize                 = 2;
    s.tileHeight               = 8;
    s.heightmapResolution      = 3;
    s.controlTextureResolution = 2;
    s.detailResolution         = 2;
    s.treeResolution           = 2;
    s.objectResolution         = 2;
    s.splatLayers              = 2;
    s.nameSuffix               = "stable";
    return s;
}
[[maybe_unused]] TerrainStampSettings wholeWorld() {
    TerrainStampSettings s;
    s.centerX = 0;
    s.centerZ = 0;
    s.width   = 4;
    s.depth   = 2;
    return s;
}
}  // namespace

TEST_CASE("procgen.terrainWorld.createsCenteredPcgTopologyAtomically") {
    TerrainWorldWorkspace world;
    auto                  settings = smallWorld();
    REQUIRE(world.create(settings).ok());
    CHECK(world.getTileCount() == 2);
    CHECK(world.getTileName(0).value() == "Terrain_0_0-stable");
    CHECK(world.getTileName(1).value() == "Terrain_1_0-stable");
    CHECK(world.getTileOriginX(0).value() == -2);
    CHECK(world.getTileOriginX(1).value() == 0);
    CHECK(world.getTileOriginZ(0).value() == -1);
    Heightmap height;
    REQUIRE(world.copyHeightmap(0, height).ok());
    CHECK(height.getWidth() == 3);
    CHECK(height.getHeight() == 3);
    settings.heightmapResolution = 4;
    CHECK(!world.create(settings).ok());
    CHECK(world.getTileCount() == 2);
    CHECK(world.getTileName(0).value() == "Terrain_0_0-stable");

    settings          = smallWorld();
    settings.worldMap = true;
    REQUIRE(world.create(settings).ok());
    CHECK(world.getTileName(0).value() == "World Map_0_0-stable");
}

TEST_CASE("procgen.terrainWorld.ownsCrossDomainHistory") {
    TerrainWorldWorkspace world;
    REQUIRE(world.create(smallWorld()).ok());
    auto operation = wholeWorld();
    auto paint     = filled(4, 2, 0.75F);
    REQUIRE(world.paintSplat(paint, 1, operation).ok());
    TerrainSplatmap splat;
    REQUIRE(world.copySplatmap(0, splat).ok());
    CHECK(splat.sample(1, 0, 0).value() == 0.75F);

    TerrainObjectPlacementSettings objects;
    objects.spacing = objects.spawnDensity = 1;
    objects.jitterPercent                  = 0;
    objects.failureRate = objects.minimumFitness = objects.minimumInstanceFitness = 0;
    objects.boundsRadius                                                          = 0.2F;
    objects.prototype                                                             = "rocks";
    objects.namespaceId                                                           = 501;
    objects.maxPoints                                                             = 100;
    TerrainObjectInstanceSettings child;
    child.asset = "rock-a";
    REQUIRE(objects.addInstance(child).ok());
    REQUIRE(world.applyObjects(paint, objects, operation, TerrainObjectOperationMode::Add).ok());
    PointSet points;
    REQUIRE(world.copyObjects(0, points).ok());
    CHECK(!points.empty());
    REQUIRE(world.undo().ok());
    REQUIRE(world.copyObjects(0, points).ok());
    CHECK(points.empty());
    REQUIRE(world.copySplatmap(0, splat).ok());
    CHECK(splat.sample(1, 0, 0).value() == 0.75F);
    REQUIRE(world.undo().ok());
    REQUIRE(world.copySplatmap(0, splat).ok());
    CHECK(splat.sample(1, 0, 0).value() == 0);
    REQUIRE(world.redo().ok());
    REQUIRE(world.redo().ok());
    CHECK(world.getOperationCount() == 2);
    CHECK(world.getAppliedCount() == 2);

    auto invalidPaint = filled(3, 3, 1);
    CHECK(!world.paintSplat(invalidPaint, 1, operation).ok());
    CHECK(world.getOperationCount() == 2);
    CHECK(world.getAppliedCount() == 2);
    REQUIRE(world.copyObjects(0, points).ok());
    CHECK(!points.empty());
    REQUIRE(world.copySplatmap(0, splat).ok());
    CHECK(splat.sample(1, 0, 0).value() == 0.75F);
}


TEST_CASE("procgen.terrainWorld.routesHeightDetailAndTreeDomains") {
    TerrainWorldWorkspace world;
    REQUIRE(world.create(smallWorld()).ok());
    auto operation = wholeWorld();
    auto one = filled(1, 1, 1), stamp = filled(1, 1, 0.5F);
    REQUIRE(world.stamp(stamp, operation, one, one).ok());
    Heightmap heights;
    REQUIRE(world.copyHeightmap(0, heights).ok());
    CHECK(heights.height(1, 1) > 0);

    auto                  fitness = filled(4, 2, 1);
    TerrainDetailSettings detail;
    detail.minimumFitness = detail.fadeStart = 0;
    detail.density                           = 4;
    detail.namespaceId                       = 701;
    REQUIRE(world.applyDetail(fitness, detail, operation, TerrainDetailMode::Replace, 17).ok());
    TerrainDetailLayer details;
    REQUIRE(world.copyDetail(1, details).ok());
    CHECK(details.sample(0, 0).value() == 4);
    auto flowers        = detail;
    flowers.density     = 2;
    flowers.namespaceId = 702;
    REQUIRE(world.applyDetail(fitness, flowers, operation, TerrainDetailMode::Add, 19).ok());
    REQUIRE(world.copyDetail(1, details).ok());
    CHECK(details.sample(0, 0).value() == 6);
    TerrainWorldClearSettings detailSourceClear;
    detailSourceClear.trees = detailSourceClear.objects = false;
    detailSourceClear.sourceNamespace                   = 701;
    REQUIRE(world.clearSpawns(detailSourceClear).ok());
    REQUIRE(world.copyDetail(1, details).ok());
    CHECK(details.sample(0, 0).value() == 2);
    REQUIRE(world.undo().ok());

    TerrainTreePlacementSettings trees;
    trees.spacing = trees.spawnDensity = 1;
    trees.jitterPercent = trees.failureRate = trees.minimumFitness = 0;
    trees.minimumWidth = trees.maximumWidth = trees.minimumHeight = trees.maximumHeight = 1;
    trees.asset                                                                         = "oak";
    trees.namespaceId                                                                   = 601;
    trees.maxPoints                                                                     = 100;
    REQUIRE(world.applyTrees(fitness, trees, operation, TerrainTreeOperationMode::Add).ok());
    PointSet points;
    REQUIRE(world.copyTrees(0, points).ok());
    CHECK(!points.empty());
    CHECK(world.getOperationCount() == 4);

    auto pines        = trees;
    pines.asset       = "pine";
    pines.namespaceId = 602;
    REQUIRE(world.applyTrees(fitness, pines, operation, TerrainTreeOperationMode::Add).ok());
    TerrainWorldClearSettings sourceClear;
    sourceClear.details = sourceClear.objects = false;
    sourceClear.sourceNamespace               = 601;
    REQUIRE(world.clearSpawns(sourceClear).ok());
    REQUIRE(world.copyTrees(0, points).ok());
    REQUIRE(!points.empty());
    for (int i = 0; i < points.getCount(); ++i) CHECK(points.getStringAttribute(i, "spawnNamespace", "") == "602");
    REQUIRE(world.undo().ok());

    TerrainWorldClearSettings clear;
    clear.objects = false;
    REQUIRE(world.clearSpawns(clear).ok());
    REQUIRE(world.copyDetail(1, details).ok());
    CHECK(details.sample(0, 0).value() == 0);
    REQUIRE(world.copyTrees(0, points).ok());
    CHECK(points.empty());
    REQUIRE(world.undo().ok());
    REQUIRE(world.copyTrees(0, points).ok());
    CHECK(!points.empty());

    REQUIRE(world.flatten().ok());
    REQUIRE(world.copyHeightmap(0, heights).ok());
    for (float value : heights.data()) CHECK(value == 0);
    REQUIRE(world.undo().ok());
    REQUIRE(world.copyHeightmap(0, heights).ok());
    CHECK(heights.height(1, 1) > 0);
}
}  // namespace test_terrain_probe_unit
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_sediment)
#include <cmath>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "PrivateTestScope.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainErosion.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "procgen/heightmap/TerrainWaterField.h"
#include "zeroerr/unittest.h"
// terrain_sediment.cpp
namespace test_terrain_sediment_unit {
using namespace eve::procgen;


TEST_CASE("procgen.sediment.sourceStationaryAccumulation") {
    Heightmap heights(2, 1), sediment(2, 1), vx(2, 1), vz(2, 1);
    heights.data()  = {2, 2};
    sediment.data() = {3, 4};
    TerrainWaterSettings water;
    water.dt = 0;
    TerrainSedimentSettings reaction;
    REQUIRE(applyTerrainSediment(heights, sediment, vx, vz, water, reaction).ok());
    CHECK(heights.data() == std::vector<float>({2, 2}));
    CHECK(sediment.data() == std::vector<float>({6, 8}));
}

TEST_CASE("procgen.sediment.sourceBacktraceWeights") {
    Heightmap heights(3, 3), sediment(3, 3), vx(3, 3), vz(3, 3);
    sediment.data() = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    vx.data()[8]    = 1.5F;
    vz.data()[8]    = 0.5F;
    TerrainWaterSettings water;
    water.dt = 1;
    TerrainSedimentSettings reaction;
    reaction.depositRate = 0;
    REQUIRE(applyTerrainSediment(heights, sediment, vx, vz, water, reaction).ok());
    CHECK(std::abs(sediment.data()[8] - (9.0F + 145.0F / 36.0F)) < 0.00001F);
}

TEST_CASE("procgen.sediment.atomicMultiIterationFailure") {
    Heightmap heights(1, 1), sediment(1, 1);
    heights.data()[0]          = 1;
    sediment.data()[0]         = std::numeric_limits<float>::max() * 0.4F;
    const auto        original = sediment.data();
    TerrainWaterField field;
    REQUIRE(field.reset(1, 1, 1).ok());
    TerrainWaterSettings water;
    water.dt               = 1;
    water.precipitation    = 0.1F;
    water.evaporation      = 0;
    water.flowAcceleration = 0;
    TerrainSedimentSettings reaction;
    reaction.depositRate = 0;
    TerrainThermalSettings thermal;
    thermal.iterations = 0;
    CHECK(!field.advanceHydraulic(heights, sediment, water, reaction, thermal, 2).ok());
    CHECK(heights.data()[0] == 1);
    CHECK(sediment.data() == original);
    auto sample = field.sample(0, 0);
    REQUIRE(sample.ok());
    CHECK(sample.value().depth == 1);
    REQUIRE(field.advanceHydraulic(heights, sediment, water, reaction, thermal, 1).ok());
    CHECK(sediment.data()[0] == original[0] * 2);
    sample = field.sample(0, 0);
    REQUIRE(sample.ok());
    CHECK(sample.value().depth == 1.1F);
}

TEST_CASE("procgen.sediment.velocityAliasesUseSnapshot") {
    Heightmap heights(2, 2), sediment(2, 2);
    heights.data()                          = {1, 2, 3, 4};
    sediment.data()                         = {4, 3, 2, 1};
    Heightmap               expectedHeights = heights, expectedSediment = sediment;
    const Heightmap         vx = heights, vz = sediment;
    TerrainWaterSettings    water;
    TerrainSedimentSettings reaction;
    REQUIRE(applyTerrainSediment(expectedHeights, expectedSediment, vx, vz, water, reaction).ok());
    REQUIRE(applyTerrainSediment(heights, sediment, heights, sediment, water, reaction).ok());
    CHECK(heights.data() == expectedHeights.data());
    CHECK(sediment.data() == expectedSediment.data());
    const auto previous = heights.data();
    CHECK(!applyTerrainSediment(heights, heights, vx, vz, water, reaction).ok());
    CHECK(heights.data() == previous);
}

TEST_CASE("procgen.sediment.combinedMatchesExplicitStages") {
    Heightmap heights(3, 2), sediment(3, 2);
    heights.data()                    = {2, 3, 1, 4, 2, 5};
    sediment.data()                   = {1, 2, 3, 4, 5, 6};
    Heightmap         expectedHeights = heights, expectedSediment = sediment, vx(3, 2), vz(3, 2);
    TerrainWaterField combined, separate;
    REQUIRE(combined.reset(3, 2, 1).ok());
    REQUIRE(separate.reset(3, 2, 1).ok());
    TerrainWaterSettings    water;
    TerrainSedimentSettings reaction;
    TerrainThermalSettings  thermal;
    thermal.reposeSlope = 0.1F;
    thermal.dt          = 0.01F;
    for (int iteration = 0; iteration < 3; ++iteration) {
        REQUIRE(separate.advance(expectedHeights, water).ok());
        for (int z = 0; z < 2; ++z)
            for (int x = 0; x < 3; ++x) {
                auto cell = separate.sample(x, z);
                REQUIRE(cell.ok());
                vx.data()[z * 3 + x] = cell.value().velocityX;
                vz.data()[z * 3 + x] = cell.value().velocityZ;
            }
        REQUIRE(applyTerrainSediment(expectedHeights, expectedSediment, vx, vz, water, reaction).ok());
        REQUIRE(applyTerrainThermal(expectedHeights, expectedSediment, thermal).ok());
    }
    REQUIRE(combined.advanceHydraulic(heights, sediment, water, reaction, thermal, 3).ok());
    CHECK(heights.data() == expectedHeights.data());
    CHECK(sediment.data() == expectedSediment.data());
    for (int z = 0; z < 2; ++z)
        for (int x = 0; x < 3; ++x) {
            auto a = combined.sample(x, z), b = separate.sample(x, z);
            REQUIRE(a.ok());
            REQUIRE(b.ok());
            CHECK(a.value().depth == b.value().depth);
            CHECK(a.value().velocityX == b.value().velocityX);
            CHECK(a.value().velocityZ == b.value().velocityZ);
            CHECK(a.value().fluxRight == b.value().fluxRight);
            CHECK(a.value().fluxLeft == b.value().fluxLeft);
            CHECK(a.value().fluxTop == b.value().fluxTop);
            CHECK(a.value().fluxBottom == b.value().fluxBottom);
        }
}
}  // namespace test_terrain_sediment_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_smooth_stamp)
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include "PrivateTestScope.h"
#include "procgen/MeshBuild.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainMeshStamp.h"
#include "procgen/heightmap/TerrainMultiTile.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "zeroerr/unittest.h"
// terrain_smooth_stamp.cpp
namespace test_terrain_smooth_stamp {
using namespace eve::procgen;
namespace {
Heightmap filled(int w, int h, float value) {
    Heightmap result(w, h);
    std::fill(result.data().begin(), result.data().end(), value);
    return result;
}
TerrainStampSettings settings() {
    TerrainStampSettings s;
    s.centerX = s.centerZ = 2;
    s.width = s.depth = 4;
    s.operation       = TerrainStampOperation::SmoothRaise;
    s.smoothWidth     = 4;
    return s;
}
MeshBuild roof(float height) {
    MeshBuild mesh;
    mesh.positions() = {0, height, 0, 4, height, 0, 0, height, 4, 4, height, 4};
    mesh.indices()   = {0, 1, 2, 1, 3, 2};
    return mesh;
}
}  // namespace

TEST_CASE("procgen.smoothStamp.heightBandMaskAndEdgeFade") {
    auto terrain = filled(5, 5, 10), stamp = filled(1, 1, 10), one = filled(1, 1, 1);
    auto s      = settings();
    s.edgeFade  = 2;
    auto result = applyTerrainStamp(terrain, stamp, s, one, one);
    REQUIRE(result.ok());
    CHECK_EQ(result.value(), 9);
    CHECK_EQ(terrain.height(2, 2), 11.f);
    CHECK_EQ(terrain.height(1, 2), 10.5f);
    CHECK_EQ(terrain.height(1, 1), 10.25f);
    CHECK_EQ(terrain.height(0, 2), 10.f);
    auto half = filled(1, 1, 0.5f);
    terrain   = filled(5, 5, 10);
    REQUIRE(applyTerrainStamp(terrain, stamp, s, one, half).ok());
    CHECK_EQ(terrain.height(2, 2), 10.5f);
    // Local mask alters stamp height; it must not be interpreted as coverage.
    terrain = filled(5, 5, 10);
    REQUIRE(applyTerrainStamp(terrain, stamp, s, half, one).ok());
    CHECK_EQ(terrain.height(2, 2), 10.f);
}

TEST_CASE("procgen.smoothStamp.zeroWidthAndFailureAreCompatible") {
    auto a = filled(5, 5, 2), b = a, stamp = filled(1, 1, 3.1f), half = filled(1, 1, 0.35f);
    auto s        = settings();
    s.smoothWidth = 0;
    s.amplitude   = 1.7f;
    s.baseHeight  = 4.f;
    REQUIRE(applyTerrainStamp(a, stamp, s, half, half).ok());
    s.operation = TerrainStampOperation::Raise;
    REQUIRE(applyTerrainStamp(b, stamp, s, half, half).ok());
    CHECK(a.data() == b.data());
    s.operation   = TerrainStampOperation::SmoothRaise;
    a             = filled(5, 5, 2);
    s.smoothWidth = 0.01f;
    REQUIRE(applyTerrainStamp(a, stamp, s, half, half).ok());
    CHECK(a.data() == b.data());  // Outside the band preserves double-intermediate Raise exactly.
    for (float invalid : {-1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        s.smoothWidth = invalid;
        CHECK(!applyTerrainStamp(a, stamp, s, half, half).ok());
        CHECK(a.data() == b.data());
        s.smoothWidth = 4;
        s.edgeFade    = invalid;
        CHECK(!applyTerrainStamp(a, stamp, s, half, half).ok());
        CHECK(a.data() == b.data());
        s.edgeFade = 0;
    }
    auto       maximum = filled(5, 5, std::numeric_limits<float>::max());
    const auto before  = maximum.data();
    s.smoothWidth      = std::numeric_limits<float>::max();
    s.amplitude        = 1;
    auto one           = filled(1, 1, 1);
    CHECK(!applyTerrainStamp(maximum, maximum, s, one, one).ok());
    CHECK(maximum.data() == before);
}

TEST_CASE("procgen.smoothStamp.rotatedTilesAndAliasing") {
    auto whole = filled(9, 5, 2), left = filled(5, 5, 2), right = left, stamp = filled(2, 2, 0);
    stamp.data() = {0, 4, 5, 2};
    auto one     = filled(1, 1, 1);
    auto s       = settings();
    s.centerX    = 4;
    s.width      = 8;
    s.rotation   = 0.3;
    s.edgeFade   = 1;
    REQUIRE(applyTerrainStamp(whole, stamp, s, one, one).ok());
    REQUIRE(applyTerrainStamp(left, stamp, s, one, one).ok());
    s.originX = 4;
    REQUIRE(applyTerrainStamp(right, stamp, s, one, one).ok());
    for (int z = 0; z < 5; ++z)
        for (int x = 0; x < 5; ++x) {
            CHECK_EQ(left.height(x, z), whole.height(x, z));
            CHECK_EQ(right.height(x, z), whole.height(x + 4, z));
        }
    auto expected = left, source = left;
    s          = settings();
    s.rotation = std::numbers::pi / 2;
    REQUIRE(applyTerrainStamp(expected, source, s, one, one).ok());
    REQUIRE(applyTerrainStamp(left, left, s, one, one).ok());
    CHECK(left.data() == expected.data());
}

TEST_CASE("procgen.meshStamp.highestSurfaceWindingAndOwnedSource") {
    auto mesh = roof(-3);
    // Overlapping reversed-winding face above the roof.
    mesh.positions().insert(mesh.positions().end(), {0, 7, 0, 4, 7, 0, 0, 7, 4});
    mesh.indices().insert(mesh.indices().end(), {6, 5, 4});
    TerrainMeshStampBuilder builder;
    REQUIRE(builder.setSource(mesh).ok());
    mesh.clear();
    Heightmap heights(5, 5), mask(5, 5);
    auto      result = builder.bake(heights, mask, 0, 0, 4, 4, 0);
    REQUIRE(result.ok());
    CHECK_EQ(result.value(), 25);
    CHECK_EQ(heights.height(1, 1), 7.f);
    CHECK_EQ(heights.height(3, 3), -3.f);
    CHECK_EQ(heights.height(2, 2), 7.f);
    CHECK_EQ(mask.height(4, 4), 1.f);
    REQUIRE(builder.bake(heights, mask, 0, 0, 4, 4, 2).ok());
    CHECK_EQ(mask.height(0, 2), 0.f);
    CHECK_EQ(mask.height(1, 2), 0.5f);
    CHECK_EQ(mask.height(2, 2), 1.f);
}

TEST_CASE("procgen.meshStamp.missingAndDegenerateTriangles") {
    auto mesh      = roof(8);
    mesh.indices() = {0, 1, 2, 0, 0, 1};
    TerrainMeshStampBuilder builder;
    REQUIRE(builder.setSource(mesh).ok());
    Heightmap heights(5, 5), mask(5, 5);
    REQUIRE(builder.bake(heights, mask, 0, 0, 4, 4, 0).ok());
    CHECK_EQ(mask.height(4, 4), 0.f);
    CHECK_EQ(heights.height(4, 4), 0.f);
    CHECK_EQ(mask.height(0, 0), 1.f);
    REQUIRE(builder.bake(heights, mask, 10, 10, 4, 4, 0).ok());
    for (float value : mask.data()) CHECK_EQ(value, 0.f);
}

TEST_CASE("procgen.meshStamp.failuresPreserveBothOutputsAndSource") {
    auto                    mesh = roof(8);
    TerrainMeshStampBuilder builder;
    REQUIRE(builder.setSource(mesh).ok());
    mesh.indices()[0] = 99;
    CHECK(!builder.setSource(mesh).ok());
    auto       heights = filled(5, 5, 3), mask = filled(5, 5, 0.75f);
    const auto beforeH = heights.data(), beforeM = mask.data();
    CHECK(!builder.bake(heights, mask, 0, 0, 0, 4, 0).ok());
    CHECK(!builder.bake(heights, heights, 0, 0, 4, 4, 0).ok());
    CHECK(!builder.bake(heights, mask, 0, 0, 4, 4, -1).ok());
    CHECK(heights.data() == beforeH);
    CHECK(mask.data() == beforeM);
    REQUIRE(builder.bake(heights, mask, 0, 0, 4, 4, 0).ok());
    CHECK_EQ(heights.height(2, 2), 8.f);
    // Oversized projected overlap must fail without publishing already computed samples.
    mesh = roof(8);
    mesh.indices().clear();
    for (int i = 0; i < 150; ++i) mesh.indices().insert(mesh.indices().end(), {0, 1, 2});
    REQUIRE(builder.setSource(mesh).ok());
    heights = filled(1024, 1024, 3);
    mask    = filled(1024, 1024, 0.75f);
    CHECK(!builder.bake(heights, mask, 0, 0, 4, 4, 0).ok());
    CHECK_EQ(heights.height(0, 0), 3.f);
    CHECK_EQ(mask.height(0, 0), 0.75f);
    CHECK_EQ(heights.height(1023, 1023), 3.f);
}

TEST_CASE("procgen.meshStamp.bakedMountainComposesWithSmoothRaise") {
    MeshBuild mountain;
    mountain.positions() = {0, -4, 0, 4, -4, 0, 4, -4, 4, 0, -4, 4, 2, 10, 2};
    mountain.indices()   = {0, 1, 4, 1, 2, 4, 2, 3, 4, 3, 0, 4};
    TerrainMeshStampBuilder builder;
    REQUIRE(builder.setSource(mountain).ok());
    Heightmap heights(17, 17), coverage(17, 17);
    REQUIRE(builder.bake(heights, coverage, 0, 0, 4, 4, 0.5).ok());
    auto terrain = filled(17, 17, 0), one = filled(1, 1, 1);
    auto s     = settings();
    s.spacingX = s.spacingZ = 0.25;
    REQUIRE(applyTerrainStamp(terrain, heights, s, one, coverage).ok());
    CHECK_EQ(terrain.height(8, 8), 10.f);
    for (int i = 0; i < 17; ++i) {
        CHECK_EQ(terrain.height(0, i), 0.f);
        CHECK_EQ(terrain.height(16, i), 0.f);
    }
    for (float h : terrain.data()) CHECK(h >= 0);
}

TEST_CASE("procgen.smoothStamp.multiTilePublicationIsAtomic") {
    auto       left = filled(5, 5, 10), right = left, whole = filled(9, 5, 10);
    const auto stamp = filled(1, 1, 10), one = filled(1, 1, 1);
    auto       s = settings();
    s.centerX    = 4;
    s.width      = 8;
    s.edgeFade   = 1;
    const std::vector<TerrainHeightTile> tiles{{"left", &left, 0, 0, 4, 4, false},
                                               {"right", &right, 4, 0, 4, 4, false}};
    REQUIRE(applyTerrainStamp(whole, stamp, s, one, one).ok());
    auto report = applyTerrainStampMultiTile(tiles, stamp, s, one, one);
    REQUIRE(report.ok());
    CHECK_EQ(report.value().affectedTiles, 2);
    for (int z = 0; z < 5; ++z)
        for (int x = 0; x < 5; ++x) {
            CHECK_EQ(left.height(x, z), whole.height(x, z));
            CHECK_EQ(right.height(x, z), whole.height(x + 4, z));
        }
    // The second tile overflows after the first tile has computed its candidate.
    right                 = filled(5, 5, std::numeric_limits<float>::max());
    const auto beforeLeft = left.data(), beforeRight = right.data();
    const auto highStamp = filled(1, 1, std::numeric_limits<float>::max());
    s.smoothWidth        = std::numeric_limits<float>::max();
    CHECK(!applyTerrainStampMultiTile(tiles, highStamp, s, one, one).ok());
    CHECK(left.data() == beforeLeft);
    CHECK(right.data() == beforeRight);
}
}  // namespace test_terrain_smooth_stamp
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_smooth_stamp_persistence)
#include <algorithm>
#include "PrivateTestScope.h"
#include "common/Value.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainGenerationSession.h"
#include "procgen/heightmap/TerrainSpawnPlan.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "zeroerr/unittest.h"
// terrain_smooth_stamp_persistence.cpp
namespace test_terrain_smooth_stamp_persistence {
using namespace eve::procgen;
namespace {
Heightmap filled(int size, float value) {
    Heightmap map(size, size);
    std::fill(map.data().begin(), map.data().end(), value);
    return map;
}
TerrainStampSettings settings(TerrainStampOperation operation) {
    TerrainStampSettings s;
    s.centerX = s.centerZ = 2;
    s.width = s.depth = 4;
    s.operation       = operation;
    s.smoothWidth     = 4;
    s.edgeFade        = 2;
    return s;
}
// A single-stamp session/plan ends with its stamp settings; remove v2/v4's two floats.
std::string oldStampSnapshot(const std::string& snapshot, int version) {
    auto value = eve::Value::fromJson(snapshot);
    if (!value.ok()) return {};
    auto payload = value.value().find("payload")->asString();
    payload.resize(payload.size() - 16);
    value.value().set("payload", payload);
    value.value().set("version", version);
    auto text = value.value().toJson();
    if (!text.ok()) return {};
    return text.value();
}
}  // namespace

TEST_CASE("procgen.smoothStamp.sessionUndoReplaySnapshotAndLegacy") {
    const auto               baseline = filled(5, 10), stamp = filled(1, 10), one = filled(1, 1);
    auto                     s = settings(TerrainStampOperation::SmoothRaise);
    TerrainGenerationSession session;
    REQUIRE(session.reset(baseline).ok());
    REQUIRE(session.stamp(stamp, s, one, one).ok());
    auto output = baseline;
    REQUIRE(session.copyTerrain(output).ok());
    CHECK_EQ(output.height(2, 2), 11.f);
    CHECK_EQ(output.height(0, 2), 10.f);
    const auto expected = output.data();
    REQUIRE(session.undo().ok());
    REQUIRE(session.copyTerrain(output).ok());
    CHECK(output.data() == baseline.data());
    REQUIRE(session.redo().ok());
    REQUIRE(session.replay().ok());
    REQUIRE(session.copyTerrain(output).ok());
    CHECK(output.data() == expected);
    auto snapshot = session.snapshotJson();
    REQUIRE(snapshot.ok());
    TerrainGenerationSession restored;
    REQUIRE(restored.restoreJson(snapshot.value()).ok());
    REQUIRE(restored.copyTerrain(output).ok());
    CHECK(output.data() == expected);
    CHECK_EQ(restored.snapshotJson().value(), snapshot.value());
    // An old reader version must not silently accept the new enum.
    CHECK(!restored.restoreJson(oldStampSnapshot(snapshot.value(), 1)).ok());
    CHECK_EQ(restored.snapshotJson().value(), snapshot.value());
    auto truncated = snapshot.value();
    truncated.resize(truncated.size() / 2);
    CHECK(!restored.restoreJson(truncated).ok());
    CHECK_EQ(restored.snapshotJson().value(), snapshot.value());

    REQUIRE(session.reset(baseline).ok());
    s.operation = TerrainStampOperation::Set;
    REQUIRE(session.stamp(stamp, s, one, one).ok());
    auto legacy = oldStampSnapshot(session.snapshotJson().value(), 1);
    REQUIRE(restored.restoreJson(legacy).ok());
    REQUIRE(restored.copyTerrain(output).ok());
    CHECK(output.data() == baseline.data());
    REQUIRE(restored.undo().ok());
    REQUIRE(restored.redo().ok());
}

TEST_CASE("procgen.smoothStamp.spawnPlanSnapshotAndLegacy") {
    const auto       stamp = filled(1, 10), one = filled(1, 1);
    auto             s = settings(TerrainStampOperation::SmoothRaise);
    TerrainSpawnPlan plan;
    REQUIRE(plan.addModifierStamp("mountain", stamp, s, one, one).ok());
    auto snapshot = plan.snapshotJson();
    REQUIRE(snapshot.ok());
    TerrainSpawnPlan restored;
    REQUIRE(restored.restoreJson(snapshot.value()).ok());
    CHECK_EQ(restored.snapshotJson().value(), snapshot.value());
    CHECK(!restored.restoreJson(oldStampSnapshot(snapshot.value(), 3)).ok());
    CHECK_EQ(restored.snapshotJson().value(), snapshot.value());
    TerrainSpawnPlan old;
    s.operation = TerrainStampOperation::Raise;
    REQUIRE(old.addModifierStamp("mountain", stamp, s, one, one).ok());
    REQUIRE(restored.restoreJson(oldStampSnapshot(old.snapshotJson().value(), 3)).ok());
    const auto       migrated = restored.snapshotJson().value();
    TerrainSpawnPlan defaults;
    s.smoothWidth = 0;
    s.edgeFade    = 0;
    REQUIRE(defaults.addModifierStamp("mountain", stamp, s, one, one).ok());
    CHECK_EQ(migrated, defaults.snapshotJson().value());
}

TEST_CASE("procgen.smoothStamp.spawnPlanRejectsInvalidOperation") {
    const auto       paint = filled(2, 1);
    auto             s     = settings(TerrainStampOperation::Raise);
    TerrainSpawnPlan plan;
    REQUIRE(plan.addSplat("splat", paint, 0, s).ok());
    auto snapshot = plan.snapshotJson();
    REQUIRE(snapshot.ok());
    const auto baseline = snapshot.value();

    auto value = eve::Value::fromJson(baseline);
    REQUIRE(value.ok());
    auto payload = value.value().find("payload")->asString();
    // count(4) + kind(1) + idLen(4) + "splat"(5) + enabled(1) + heightmap + stamp prefix.
    const std::size_t stampStart      = 4 + 1 + 4 + 5 + 1 + 8 + paint.data().size() * sizeof(float);
    const std::size_t operationOffset = stampStart + 9 * sizeof(double) + 3 * sizeof(float);
    const std::size_t hexOffset       = operationOffset * 2;
    REQUIRE(payload.size() > hexOffset + 8);
    // Corrupt the stamp operation to an unsupported enum (99).
    payload.replace(hexOffset, 8, "63000000");
    value.value().set("payload", payload);
    auto corrupted = value.value().toJson();
    REQUIRE(corrupted.ok());

    TerrainSpawnPlan restored;
    REQUIRE(restored.restoreJson(baseline).ok());
    CHECK(!restored.restoreJson(corrupted.value()).ok());
    CHECK_EQ(restored.snapshotJson().value(), baseline);
}

TEST_CASE("procgen.smoothStamp.spawnPlanV0DetailMigration") {
    const auto raster = filled(2, 1);
    auto       s      = settings(TerrainStampOperation::Raise);
    s.smoothWidth     = 0;
    s.edgeFade        = 0;
    TerrainSpawnPlan      plan;
    TerrainDetailSettings detail;
    REQUIRE(plan.addDetail("detail", raster, detail, s, TerrainDetailMode::Add, 17).ok());
    auto value = eve::Value::fromJson(plan.snapshotJson().value());
    REQUIRE(value.ok());
    auto              payload       = value.value().find("payload")->asString();
    const std::size_t enabledOffset = 4 + 1 + 4 + std::string("detail").size();
    const std::size_t rasterBytes   = 8 + raster.data().size() * sizeof(float);
    const std::size_t oldStampBytes = 9 * sizeof(double) + 3 * sizeof(float) + sizeof(int);
    payload.erase((enabledOffset + 1 + rasterBytes + oldStampBytes) * 2, 16);
    payload.erase(enabledOffset * 2, 2);
    value.value().set("payload", payload);
    value.value().set("version", 0);
    auto legacy = value.value().toJson();
    REQUIRE(legacy.ok());
    TerrainSpawnPlan restored;
    REQUIRE(restored.restoreJson(legacy.value()).ok());
    CHECK_EQ(restored.snapshotJson().value(), plan.snapshotJson().value());
}
}  // namespace test_terrain_smooth_stamp_persistence
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_stamp)
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include "PrivateTestScope.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "zeroerr/unittest.h"
// terrain_stamp.cpp
namespace test_terrain_stamp {
using namespace eve::procgen;

namespace {
Heightmap filled(int w, int h, float value) {
    Heightmap map(w, h);
    std::fill(map.data().begin(), map.data().end(), value);
    return map;
}
TerrainStampSettings centered() {
    TerrainStampSettings s;
    s.centerX = s.centerZ = 1;
    s.width = s.depth = 2;
    return s;
}
}  // namespace

TEST_CASE("procgen.stamp.sixOperations") {
    const auto                 stamp = filled(1, 1, 3), one = filled(1, 1, 1);
    auto                       s = centered();
    const std::array<float, 6> expected{3, 2, 2.5F, 3, 5, -1};
    for (int op = 0; op < 6; ++op) {
        auto target = filled(3, 3, 2);
        s.operation = static_cast<TerrainStampOperation>(op);
        auto r      = applyTerrainStamp(target, stamp, s, one, one);
        REQUIRE(r.ok());
        CHECK(r.value() == (op == 1 ? 0 : 9));
        for (float value : target.data()) CHECK(value == expected[size_t(op)]);
    }
}

TEST_CASE("procgen.stamp.localAndGlobalHaveDistinctMeaning") {
    auto       target = filled(3, 3, 2);
    const auto stamp = filled(1, 1, 4), half = filled(1, 1, 0.5F);
    auto       s = centered();
    s.baseHeight = 1;
    s.operation  = TerrainStampOperation::Set;
    auto r       = applyTerrainStamp(target, stamp, s, half, half);
    REQUIRE(r.ok());
    CHECK(target.height(1, 1) == 2.5F);  // lerp(2, 1 + 4 * .5, .5)
    auto zero   = filled(1, 1, 0);
    s.operation = TerrainStampOperation::Add;
    r           = applyTerrainStamp(target, stamp, s, half, zero);
    REQUIRE(r.ok());
    CHECK(r.value() == 0);
    CHECK(target.height(1, 1) == 2.5F);
}

TEST_CASE("procgen.stamp.rotatedFootprintAndBilinearSampling") {
    auto target = filled(5, 5, -2);
    auto stamp  = filled(2, 2, 0);
    stamp.setHeight(1, 0, 2);
    stamp.setHeight(0, 1, 4);
    stamp.setHeight(1, 1, 6);
    const auto one = filled(1, 1, 1);
    auto       s   = centered();
    s.centerX = s.centerZ = 2;
    s.width               = 2;
    s.depth               = 4;
    s.rotation            = std::numbers::pi / 2;
    s.operation           = TerrainStampOperation::Set;
    auto r                = applyTerrainStamp(target, stamp, s, one, one);
    REQUIRE(r.ok());
    CHECK(r.value() == 15);
    CHECK(std::abs(target.height(2, 2) - 3) < 1e-6F);
    CHECK(std::abs(target.height(1, 2) - 4) < 1e-6F);
    CHECK(target.height(2, 0) == -2);
    CHECK(target.height(0, 0) == -2);
}

TEST_CASE("procgen.stamp.tilesMatchOneWorldRaster") {
    auto whole = filled(5, 3, 0), left = filled(3, 3, 0), right = filled(3, 3, 0);
    auto stamp     = filled(2, 2, 0);
    stamp.data()   = {0, 1, 2, 4};
    const auto one = filled(1, 1, 1);
    auto       s   = centered();
    s.centerX      = 2;
    s.width        = 4;
    s.operation    = TerrainStampOperation::Set;
    auto r         = applyTerrainStamp(whole, stamp, s, one, one);
    REQUIRE(r.ok());
    r = applyTerrainStamp(left, stamp, s, one, one);
    REQUIRE(r.ok());
    s.originX = 2;
    r         = applyTerrainStamp(right, stamp, s, one, one);
    REQUIRE(r.ok());
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) {
            CHECK(left.height(x, y) == whole.height(x, y));
            CHECK(right.height(x, y) == whole.height(x + 2, y));
        }
}

TEST_CASE("procgen.stamp.aliasUsesOriginalInput") {
    auto target       = filled(3, 3, 0);
    target.data()     = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    const auto source = target, one = filled(1, 1, 1);
    auto       expected = target;
    auto       s        = centered();
    s.rotation          = std::numbers::pi;
    s.operation         = TerrainStampOperation::Set;
    auto r              = applyTerrainStamp(expected, source, s, one, one);
    REQUIRE(r.ok());
    r = applyTerrainStamp(target, target, s, one, one);
    REQUIRE(r.ok());
    CHECK(target.data() == expected.data());
}

TEST_CASE("procgen.stamp.invalidAndOverflowAreAtomic") {
    auto       target = filled(3, 3, 7);
    auto       stamp  = filled(3, 3, 1);
    const auto one = filled(1, 1, 1), before = target;
    auto       s = centered();
    s.operation  = TerrainStampOperation::Set;
    s.width      = 0;
    auto r       = applyTerrainStamp(target, stamp, s, one, one);
    CHECK(!r.ok());
    CHECK(target.data() == before.data());
    s.width             = 2;
    stamp.data().back() = std::numeric_limits<float>::infinity();
    r                   = applyTerrainStamp(target, stamp, s, one, one);
    CHECK(!r.ok());
    CHECK(target.data() == before.data());
    stamp.data().back() = std::numeric_limits<float>::max();
    s.amplitude         = 2;
    r                   = applyTerrainStamp(target, stamp, s, one, one);
    CHECK(!r.ok());
    CHECK(target.data() == before.data());
    stamp.data().pop_back();  // Legacy Heightmap mutable data can violate its shape.
    r = applyTerrainStamp(target, stamp, s, one, one);
    CHECK(!r.ok());
    CHECK(target.data() == before.data());
}

TEST_CASE("procgen.stamp.maskBlendGoldenValues") {
    const auto                 source = filled(1, 1, 0.8F);
    const std::array<float, 5> expected{0.16F, 0.8F, 0.2F, 1.0F, -0.6F};
    for (int op = 0; op < 5; ++op) {
        auto target = filled(1, 1, 0.2F);
        auto r      = blendTerrainMask(target, source, static_cast<TerrainMaskBlend>(op));
        REQUIRE(r.ok());
        CHECK(std::abs(target.height(0, 0) - expected[size_t(op)]) < 1e-6F);
    }
}

TEST_CASE("procgen.stamp.maskOrderStrengthInvertAndAlias") {
    auto       target = filled(1, 1, 0.2F);
    const auto source = filled(1, 1, 0.8F), half = filled(1, 1, 0.5F);
    auto       r = blendTerrainMask(target, source, TerrainMaskBlend::Add);
    REQUIRE(r.ok());
    r = blendTerrainMask(target, half, TerrainMaskBlend::Multiply);
    REQUIRE(r.ok());
    CHECK(std::abs(target.height(0, 0) - 0.5F) < 1e-6F);
    r = blendTerrainMask(target, target, TerrainMaskBlend::Multiply, 0.5F, true);
    REQUIRE(r.ok());
    CHECK(std::abs(target.height(0, 0) - 0.375F) < 1e-6F);
}

TEST_CASE("procgen.stamp.maskFailuresPreserveTarget") {
    auto       target = filled(2, 1, 1), badSize = filled(1, 1, 1);
    const auto before = target;
    auto       r      = blendTerrainMask(target, badSize, TerrainMaskBlend::Multiply);
    CHECK(!r.ok());
    auto large = filled(2, 1, std::numeric_limits<float>::max());
    r          = blendTerrainMask(target, large, TerrainMaskBlend::Add, -1);
    CHECK(!r.ok());
    r = blendTerrainMask(target, large, static_cast<TerrainMaskBlend>(999));
    CHECK(!r.ok());
    CHECK(target.data() == before.data());
    target.data().back()   = std::numeric_limits<float>::max();
    const auto largeBefore = target;
    r                      = blendTerrainMask(target, large, TerrainMaskBlend::Add);
    CHECK(!r.ok());
    CHECK(target.data() == largeBefore.data());
}
}  // namespace test_terrain_stamp
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_tree)
#include <cmath>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "PrivateTestScope.h"
#include "procgen/PointSet.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "procgen/heightmap/TerrainTreePlacement.h"
#include "zeroerr/unittest.h"
// terrain_tree.cpp
namespace test_terrain_tree_unit {
using namespace eve::procgen;

namespace {
[[maybe_unused]] TerrainTreePlacementSettings baseSettings() {
    TerrainTreePlacementSettings settings;
    settings.asset          = "tree/oak";
    settings.width          = 20;
    settings.depth          = 20;
    settings.spacing        = 10;
    settings.spawnDensity   = 1;
    settings.jitterPercent  = 0;
    settings.minimumFitness = 0;
    settings.minimumWidth   = 2;
    settings.maximumWidth   = 4;
    settings.minimumHeight  = 3;
    settings.maximumHeight  = 7;
    settings.healthyR       = 0.8F;
    settings.dryR           = 0.2F;
    settings.bendFactor     = 0.35F;
    settings.boundsRadius   = 1.5F;
    settings.seed           = 17;
    settings.namespaceId    = 91;
    return settings;
}
}  // namespace

TEST_CASE("procgen.tree.fixedFitnessTransformsAndMetadata") {
    Heightmap fitness(2, 2), heights(2, 2);
    fitness.data()       = {1, 1, 1, 1};
    heights.data()       = {0, 1, 0, 1};
    auto settings        = baseSettings();
    settings.scaleMode   = TerrainTreeScaleMode::Fixed;
    settings.heightScale = 10;
    PointSet points;
    auto     result = exportTerrainTreePoints(points, fitness, heights, settings);
    REQUIRE(result.ok());
    REQUIRE(result.value() == 9);
    for (int i = 0; i < points.getCount(); ++i) {
        CHECK(points.getPointId(i) != 0);
        CHECK(points.getScaleX(i) == 2);
        CHECK(points.getScaleY(i) == 3);
        CHECK(points.getScaleZ(i) == 2);
        CHECK(std::abs(points.getY(i) - points.getX(i) * 0.5F) < 0.00001F);
        CHECK(points.getColorR(i) == settings.healthyR);
        CHECK(points.getStringAttribute(i, "asset", "") == settings.asset);
        CHECK(points.getIntAttribute(i, "treeCandidate", -1) == i);
        CHECK(points.getFloatAttribute(i, "bendFactor", -1) == settings.bendFactor);
        CHECK(points.getBoundsMaxX(i) == settings.boundsRadius * settings.minimumWidth);
    }
}

TEST_CASE("procgen.tree.scaleOffsetColorAndDeterministicReplay") {
    Heightmap fitness(1, 1), heights(1, 1);
    fitness.data()[0]       = 0.75F;
    heights.data()[0]       = 0.4F;
    auto settings           = baseSettings();
    settings.jitterPercent  = 0.4F;
    settings.scaleMode      = TerrainTreeScaleMode::FitnessRandomized;
    settings.snapToTerrain  = false;
    settings.yOffsetMode    = TerrainTreeYOffsetMode::Custom;
    settings.customOffset   = 12;
    settings.minimumYOffset = settings.maximumYOffset = -2;
    PointSet first, repeat;
    REQUIRE(exportTerrainTreePoints(first, fitness, heights, settings).ok());
    REQUIRE(exportTerrainTreePoints(repeat, fitness, heights, settings).ok());
    REQUIRE(first.getCount() > 0);
    REQUIRE(first.getCount() == repeat.getCount());
    for (int i = 0; i < first.getCount(); ++i) {
        CHECK(first.getPointId(i) == repeat.getPointId(i));
        CHECK(first.getX(i) == repeat.getX(i));
        CHECK(first.getZ(i) == repeat.getZ(i));
        CHECK(first.getYaw(i) == repeat.getYaw(i));
        CHECK(first.getScaleX(i) == repeat.getScaleX(i));
        CHECK(first.getY(i) == 10);
        CHECK(std::abs(first.getColorR(i) - 0.65F) < 0.00001F);
    }
}

TEST_CASE("procgen.tree.identityIgnoresPrototypeAppearanceAndFailuresAreAtomic") {
    Heightmap fitness(1, 1), heights(1, 1);
    fitness.data()[0] = 1;
    auto     settings = baseSettings();
    PointSet original, changed;
    REQUIRE(exportTerrainTreePoints(original, fitness, heights, settings).ok());
    settings.healthyR     = 0.4F;
    settings.minimumWidth = settings.maximumWidth = 6;
    REQUIRE(exportTerrainTreePoints(changed, fitness, heights, settings).ok());
    REQUIRE(original.getCount() == changed.getCount());
    for (int i = 0; i < original.getCount(); ++i) {
        CHECK(original.getPointId(i) == changed.getPointId(i));
        CHECK(original.getX(i) == changed.getX(i));
        CHECK(original.getZ(i) == changed.getZ(i));
    }
    const auto beforeId   = changed.getPointId(0);
    settings.maximumWidth = 5;
    CHECK(!exportTerrainTreePoints(changed, fitness, heights, settings).ok());
    CHECK(changed.getPointId(0) == beforeId);
    settings.minimumWidth = settings.maximumWidth = 6;
    fitness.data()[0]                             = 1.1F;
    CHECK(!exportTerrainTreePoints(changed, fitness, heights, settings).ok());
    CHECK(changed.getPointId(0) == beforeId);
    fitness.data()[0]  = 1;
    settings.maxPoints = 1;
    CHECK(!exportTerrainTreePoints(changed, fitness, heights, settings).ok());
    CHECK(changed.getPointId(0) == beforeId);
}


TEST_CASE("procgen.tree.removeIsAssetScopedStrictAndAliasSafe") {
    Heightmap fitness(1, 1), heights(1, 1);
    fitness.data()[0]  = 1;
    auto settings      = baseSettings();
    settings.scaleMode = TerrainTreeScaleMode::Fixed;
    PointSet points;
    REQUIRE(exportTerrainTreePoints(points, fitness, heights, settings).ok());
    const int foreign = points.add(5, 0, 5);
    points.setStringAttribute(foreign, "asset", "tree/pine");
    const auto foreignId = points.getPointId(foreign);
    REQUIRE(removeTerrainTreePoints(points, points, fitness, settings).ok());
    REQUIRE(points.getCount() == 1);
    CHECK(points.getStringAttribute(0, "asset", "") == "tree/pine");
    CHECK(points.getPointId(0) == foreignId);

    PointSet unchanged;
    settings.minimumFitness = 1;
    REQUIRE(removeTerrainTreePoints(unchanged, points, fitness, settings).ok());
    CHECK(unchanged.getCount() == 1);
    fitness.data()[0] = -0.1F;
    CHECK(!removeTerrainTreePoints(unchanged, points, fitness, settings).ok());
    CHECK(unchanged.getCount() == 1);
}

TEST_CASE("procgen.tree.prototypeRefreshPreservesIdentityAndMapsScale") {
    Heightmap fitness(1, 1), heights(1, 1);
    fitness.data()[0]   = 1;
    auto placement      = baseSettings();
    placement.scaleMode = TerrainTreeScaleMode::Fitness;
    PointSet points;
    REQUIRE(exportTerrainTreePoints(points, fitness, heights, placement).ok());
    REQUIRE(points.getCount() > 0);
    const auto                 id = points.getPointId(0);
    const auto                 x = points.getX(0), yaw = points.getYaw(0), color = points.getColorR(0);
    TerrainTreeRescaleSettings refresh;
    refresh.asset                 = placement.asset;
    refresh.previousMinimumWidth  = placement.minimumWidth;
    refresh.previousMaximumWidth  = placement.maximumWidth;
    refresh.previousMinimumHeight = placement.minimumHeight;
    refresh.previousMaximumHeight = placement.maximumHeight;
    refresh.minimumWidth          = 4;
    refresh.maximumWidth          = 8;
    refresh.minimumHeight         = 6;
    refresh.maximumHeight         = 14;
    refresh.boundsRadius          = 2;
    refresh.bendFactor            = 0.8F;
    auto result                   = rescaleTerrainTreePoints(points, points, refresh);
    REQUIRE(result.ok());
    CHECK(result.value() == points.getCount());
    CHECK(points.getPointId(0) == id);
    CHECK(points.getX(0) == x);
    CHECK(points.getYaw(0) == yaw);
    CHECK(points.getColorR(0) == color);
    CHECK(points.getScaleX(0) == 8);
    CHECK(points.getScaleY(0) == 14);
    CHECK(points.getBoundsMaxX(0) == 16);
    CHECK(points.getFloatAttribute(0, "bendFactor", 0) == 0.8F);
    refresh.previousMaximumWidth = 1;
    CHECK(!rescaleTerrainTreePoints(points, points, refresh).ok());
    CHECK(points.getPointId(0) == id);
    CHECK(points.getScaleX(0) == 8);
}
}  // namespace test_terrain_tree_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_water)
#include <algorithm>
#include <cmath>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "PrivateTestScope.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainMask.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "procgen/heightmap/TerrainWaterField.h"
#include "zeroerr/unittest.h"
// terrain_water.cpp
namespace test_terrain_water_unit {
using namespace eve::procgen;

TEST_CASE("procgen.waterFlow.tracesStrictLowestAndTransposes") {
    Heightmap source(3, 3), output(3, 3);
    source.data() = {0, 0, 0, 0, 1, 0, 0, 0, 0};
    output.data().assign(9, 0.7F);
    TerrainWaterFlowMapSettings settings;
    settings.dropletVolume    = 0.11F;
    settings.absorptionRate   = 0.05F;
    settings.smoothIterations = 0;
    REQUIRE(generateTerrainWaterFlowMap(output, source, settings).ok());
    // Three deposits: the peak once, then its first lowest corner twice while
    // pooling raises the private working terrain. The last descent has no volume.
    REQUIRE(output.data() == std::vector<float>({0.1F, 0, 0, 0, 0.05F, 0, 0, 0, 0}));
    CHECK(source.data() == std::vector<float>({0, 0, 0, 0, 1, 0, 0, 0, 0}));
}

TEST_CASE("procgen.waterFlow.rectangularFlipSmoothAndAtomicFailure") {
    Heightmap source(4, 3), output(3, 4);
    source.data() = {0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0};
    TerrainWaterFlowMapSettings settings;
    settings.dropletVolume = settings.absorptionRate = 0.2F;
    settings.smoothIterations                        = 1;
    REQUIRE(generateTerrainWaterFlowMap(output, source, settings).ok());
    CHECK(output.getWidth() == 3);
    CHECK(output.getHeight() == 4);
    for (float value : output.data()) {
        CHECK(value >= 0.f);
        CHECK(value <= 1.f);
    }
    const auto original     = output.data();
    settings.absorptionRate = 0.f;
    CHECK(!generateTerrainWaterFlowMap(output, source, settings).ok());
    CHECK(output.data() == original);
    Heightmap wrong(4, 3);
    settings.absorptionRate = 0.2F;
    CHECK(!generateTerrainWaterFlowMap(wrong, source, settings).ok());
    CHECK(!generateTerrainWaterFlowMap(source, source, settings).ok());
}


TEST_CASE("procgen.velocityFlow.normalizesAndIsDeterministic") {
    Heightmap source(5, 5), first(5, 5), second(5, 5);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x) source.setHeight(x, y, 0.1F * float(x) + 0.03F * float(y * y));
    REQUIRE(generateTerrainVelocityFlowMap(first, source, 4).ok());
    REQUIRE(generateTerrainVelocityFlowMap(second, source, 4).ok());
    CHECK(first.data() == second.data());
    CHECK(*std::min_element(first.data().begin(), first.data().end()) == 0.F);
    CHECK(*std::max_element(first.data().begin(), first.data().end()) == 1.F);
    for (float value : first.data()) {
        CHECK(value >= 0.F);
        CHECK(value <= 1.F);
    }
}

TEST_CASE("procgen.velocityFlow.flatZeroAliasAndFailure") {
    Heightmap flat(4, 3), output(4, 3);
    flat.data().assign(12, 0.25F);
    REQUIRE(generateTerrainVelocityFlowMap(output, flat, 8).ok());
    CHECK(output.data() == std::vector<float>(12, 0.F));
    Heightmap aliased(3, 3);
    for (int i = 0; i < 9; ++i) aliased.data()[size_t(i)] = float(i) * 0.1F;
    REQUIRE(generateTerrainVelocityFlowMap(aliased, aliased, 2).ok());
    const auto before = aliased.data();
    CHECK(!generateTerrainVelocityFlowMap(aliased, aliased, -1).ok());
    CHECK(aliased.data() == before);
    Heightmap wrong(2, 2);
    CHECK(!generateTerrainVelocityFlowMap(wrong, flat, 1).ok());
}


TEST_CASE("procgen.water.exportCurveMaskComposition") {
    Heightmap terrain(2, 1), channel(2, 1), curve(2, 1), mask(2, 1);
    terrain.data() = {1, 0};
    curve.data()   = {0.2F, 0.8F};
    mask.data()    = {0.5F, 0.5F};
    TerrainWaterField field;
    REQUIRE(field.reset(2, 1, 1).ok());
    TerrainWaterSettings settings;
    settings.dt               = 1;
    settings.flowAcceleration = 1;
    REQUIRE(field.advance(terrain, settings).ok());
    REQUIRE(field.exportChannel(channel, TerrainWaterChannel::FluxRight).ok());
    REQUIRE(transformTerrainMask(channel, channel, curve).ok());
    REQUIRE(blendTerrainMask(mask, channel, TerrainMaskBlend::Multiply, 1, false).ok());
    CHECK(std::abs(mask.data()[0] - 0.4F) < 1e-6F);
    CHECK(std::abs(mask.data()[1] - 0.1F) < 1e-6F);
    auto source = field.sample(0, 0);
    REQUIRE(source.ok());
    CHECK(source.value().fluxRight == 1);
    CHECK(terrain.data() == std::vector<float>({1, 0}));
}

TEST_CASE("procgen.water.sourceFluxVelocityAndDepthOverwrite") {
    Heightmap terrain(2, 1);
    terrain.data() = {1, 0};
    TerrainWaterField field;
    REQUIRE(field.reset(2, 1, 1).ok());
    TerrainWaterSettings s;
    s.dt               = 1;
    s.flowAcceleration = 1;
    s.precipitation    = 0.1F;
    s.evaporation      = 0;
    REQUIRE(field.advance(terrain, s).ok());
    auto left = field.sample(0, 0), right = field.sample(1, 0);
    REQUIRE(left.ok());
    REQUIRE(right.ok());
    CHECK(left.value().fluxRight == 1);
    CHECK(left.value().fluxLeft == 0);
    CHECK(left.value().velocityX == -0.5F);
    CHECK(right.value().velocityX == 0);
    CHECK(left.value().depth == 1.1F);
    CHECK(right.value().depth == 1.1F);
    REQUIRE(field.advance(terrain, s).ok());
    left = field.sample(0, 0);
    REQUIRE(left.ok());
    CHECK(left.value().fluxRight == 2);
    CHECK(terrain.data() == std::vector<float>({1, 0}));
}

TEST_CASE("procgen.water.dryFlatFieldAndScaledRainEvaporation") {
    Heightmap         terrain(1, 1);
    TerrainWaterField field;
    REQUIRE(field.reset(1, 1, 0).ok());
    TerrainWaterSettings s;
    REQUIRE(field.advance(terrain, s).ok());
    auto sample = field.sample(0, 0);
    REQUIRE(sample.ok());
    CHECK(sample.value().velocityX == 0);
    CHECK(sample.value().velocityZ == 0);
    CHECK(sample.value().depth == 0);
    s.waterScale    = 3;
    s.dt            = 1;
    s.precipitation = 0.5F;
    s.evaporation   = 0;
    REQUIRE(field.advance(terrain, s).ok());
    sample = field.sample(0, 0);
    REQUIRE(sample.ok());
    CHECK(sample.value().depth == 1.5F);
    s.precipitation = 0;
    s.evaporation   = 2;
    REQUIRE(field.advance(terrain, s).ok());
    sample = field.sample(0, 0);
    REQUIRE(sample.ok());
    CHECK(sample.value().depth == 0);
}

TEST_CASE("procgen.water.rejectsInvalidAndOverflowWithoutPublishing") {
    Heightmap terrain(2, 1);
    terrain.data() = {1, 0};
    TerrainWaterField    field;
    TerrainWaterSettings s;
    CHECK(!field.advance(terrain, s).ok());
    CHECK(!field.sample(0, 0).ok());
    REQUIRE(field.reset(2, 1, 1).ok());
    CHECK(!field.reset(-1, 1, 0).ok());
    CHECK(!field.reset(std::numeric_limits<int>::max(), 2, 0).ok());
    CHECK(field.getWidth() == 2);
    s.dt               = std::numeric_limits<float>::max();
    s.flowAcceleration = std::numeric_limits<float>::max();
    CHECK(!field.advance(terrain, s).ok());
    auto sample = field.sample(0, 0);
    REQUIRE(sample.ok());
    CHECK(sample.value().depth == 1);
    CHECK(sample.value().fluxRight == 0);
    s                  = TerrainWaterSettings();
    s.dt               = 1;
    s.flowAcceleration = 1;
    s.waterScale       = std::numeric_limits<float>::max();
    s.precipitation    = std::numeric_limits<float>::max();
    CHECK(!field.advance(terrain, s).ok());
    sample = field.sample(0, 0);
    REQUIRE(sample.ok());
    CHECK(sample.value().fluxRight == 0);
    CHECK(sample.value().depth == 1);
    CHECK(!field.sample(2, 0).ok());
    TerrainWaterField moved = std::move(field);
    CHECK(field.getWidth() == 0);
    CHECK(!field.sample(0, 0).ok());
    sample = moved.sample(0, 0);
    REQUIRE(sample.ok());
    CHECK(sample.value().depth == 1);
    REQUIRE(field.reset(1, 1, 0).ok());
}

TEST_CASE("procgen.water.signedSourceAccelerationAndZeroDt") {
    Heightmap terrain(2, 1);
    terrain.data() = {1, 0};
    TerrainWaterField field;
    REQUIRE(field.reset(2, 1, 1).ok());
    TerrainWaterSettings s;
    s.dt               = 1;
    s.flowAcceleration = -1;
    s.precipitation    = 0;
    s.evaporation      = 0;
    REQUIRE(field.advance(terrain, s).ok());
    auto right = field.sample(1, 0);
    REQUIRE(right.ok());
    CHECK(right.value().fluxLeft == 1);
    CHECK(right.value().velocityX == 0.5F);
    s.dt = 0;
    REQUIRE(field.advance(terrain, s).ok());
    auto unchanged = field.sample(1, 0);
    REQUIRE(unchanged.ok());
    CHECK(unchanged.value().fluxLeft == right.value().fluxLeft);
    CHECK(unchanged.value().velocityX == right.value().velocityX);
}


TEST_CASE("procgen.water.rectangularZFlowPreservesSourceSignsAndSpacing") {
    Heightmap terrain(1, 2);
    terrain.data() = {1, 0};
    TerrainWaterField field;
    REQUIRE(field.reset(1, 2, 1).ok());
    TerrainWaterSettings s;
    s.dt               = 1;
    s.flowAcceleration = 1;
    s.spacingZ         = 2;
    s.precipitation    = 0;
    s.evaporation      = 0;
    REQUIRE(field.advance(terrain, s).ok());
    auto top = field.sample(0, 0), bottom = field.sample(0, 1);
    REQUIRE(top.ok());
    REQUIRE(bottom.ok());
    CHECK(top.value().fluxBottom == 0.5F);
    CHECK(top.value().fluxTop == 0);
    CHECK(top.value().velocityX == 0);
    CHECK(top.value().velocityZ == 0.25F);
    CHECK(std::abs(bottom.value().velocityZ - 1.0F / 9) < 1e-6F);
    CHECK(bottom.value().depth == 1);
}
}  // namespace test_terrain_water_unit
#endif
