// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "pcg_terrain_watcher.cpp",
    "procgen_hex_terrain.cpp",
    "terrain_curve_texture.cpp",
    "terrain_detail.cpp",
    "terrain_effect.cpp",
    "terrain_erosion.cpp",
    "terrain_generation_session.cpp",
    "terrain_legacy_hydraulic.cpp",
    "terrain_mask.cpp",
    "terrain_multitile.cpp",
    "terrain_probe.cpp",
    "terrain_sediment.cpp",
    "terrain_smooth_stamp.cpp",
    "terrain_smooth_stamp_persistence.cpp",
    "terrain_stamp.cpp",
    "terrain_tree.cpp",
    "terrain_water.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_pcg_terrain_watcher)
#include "PrivateTestScope.h"
#include "procgen/PointSet.h"
#include "procgen/heightmap/PcgTerrainWatcher.h"
#include "procgen/heightmap/PcgTreeManager.h"

namespace test_terrain_watch_contract {
using namespace eve::procgen;

// A scan publishes an ordered delta. The first snapshot establishes the
// baseline; replacing west with north emits add then remove.
TEST_CASE("procgen.terrainWatch.initial_scan_change_order_and_cancel") {
    PcgTerrainWatcher watcher;
    CHECK(!watcher.addTerrain("orphan").ok());
    watcher.beginScan();
    REQUIRE(watcher.addTerrain("west").ok());
    REQUIRE(watcher.addTerrain("east").ok());
    auto initial = watcher.commitScan();
    REQUIRE(initial.ok());
    CHECK_EQ(initial.value(), 0);
    CHECK_EQ(watcher.getTerrainCount(), 2);
    watcher.beginScan();
    REQUIRE(watcher.addTerrain("east").ok());
    REQUIRE(watcher.addTerrain("north").ok());
    auto changed = watcher.commitScan();
    REQUIRE(changed.ok());
    CHECK_EQ(changed.value(), 2);
    CHECK_EQ(watcher.getChangeTerrainId(0).value(), "north");
    CHECK_EQ(watcher.getChangeType(0).value(), 0);
    CHECK_EQ(watcher.getChangeTerrainId(1).value(), "west");
    CHECK_EQ(watcher.getChangeType(1).value(), 1);
    watcher.beginScan();
    REQUIRE(watcher.addTerrain("only-temporary").ok());
    watcher.cancelScan();
    CHECK_EQ(watcher.getTerrainCount(), 2);
}

// Tree insertion is atomic; rectangular range is inclusive of its X/Z
// limits and rejects invalid radii.
TEST_CASE("procgen.treeManager.batch_bounds_and_range") {
    PcgTreeManager trees;
    CHECK(!trees.addTree(0.f, 0.f, 0).ok());
    // reset takes width/depth, yielding the half-open square [-10, 10).
    REQUIRE(trees.reset(-10.f, -10.f, 20.f, 20.f).ok());
    REQUIRE(trees.addTree(1.f, 1.f, 0).ok());
    PointSet batch;
    batch.add(4.f, 0.f, 4.f);
    REQUIRE(trees.addTrees(batch, 2).ok());
    CHECK_EQ(trees.countInRange(1.f, 1.f, 3.f).value(), 2);
    CHECK(!trees.addTree(10.f, 1.f, 0).ok());
    PointSet invalid;
    invalid.add(2.f, 0.f, 2.f);
    invalid.add(20.f, 0.f, 2.f);
    CHECK(!trees.addTrees(invalid, 2).ok());
    CHECK_EQ(trees.getCount(), 2);
    CHECK(!trees.countInRange(0.f, 0.f, -1.f).ok());
}
}  // namespace test_terrain_watch_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_hex_terrain)
#include "PrivateTestScope.h"
#include "hexmap/HexMap.h"
#include "hexmap/HexTerrainBake.h"
#include "procgen/Params.h"
#include "procgen/algorithms/HexTerrainBindings.h"

namespace test_hex_terrain_contract {
using namespace eve::procgen;

// The planar baker returns a typed, seed-stable hex snapshot which applies
// to a map of matching dimensions. Sizes outside the chunk multiple fail.
TEST_CASE("procgen.hexTerrain.planar_bake_round_trip_and_size_boundary") {
    Params params;
    params.setSeed(4242);
    params.setSize(20, 15);
    params.setInt("landPercentage", 50);
    auto baked = generateHexTerrain(params);
    REQUIRE(baked.ok());
    auto decoded = eve::hexmap::hexTerrainBakeFromValue(baked.value());
    REQUIRE(decoded.ok());
    CHECK(decoded.value().kind == eve::hexmap::HexTerrainBake::Kind::Planar);
    CHECK_EQ(decoded.value().cellCountX, 20);
    CHECK_EQ(decoded.value().cellCountZ, 15);
    CHECK_EQ(decoded.value().seed, 4242u);
    eve::hexmap::HexMap map;
    REQUIRE(eve::hexmap::applyHexTerrain(map, decoded.value()).ok());
    CHECK_EQ(map.cellCount(), 300);
    params.setSize(7, 7);
    CHECK(!generateHexTerrain(params).ok());
}

// Spherical generation uses the same seed for identical cell payloads and
// encodes its own bake kind rather than a planar map.
TEST_CASE("procgen.hexTerrain.sphere_seed_repeats_payload") {
    Params params;
    params.setSeed(123);
    params.setInt("subdivision", 1);
    params.setFloat("radius", 100.f);
    auto first = generateHexSphere(params);
    auto again = generateHexSphere(params);
    REQUIRE(first.ok());
    REQUIRE(again.ok());
    auto a = eve::hexmap::hexTerrainBakeFromValue(first.value());
    auto b = eve::hexmap::hexTerrainBakeFromValue(again.value());
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(a.value().kind == eve::hexmap::HexTerrainBake::Kind::Sphere);
    CHECK_EQ(a.value().cells.size(), b.value().cells.size());
    for (std::size_t i = 0; i < a.value().cells.size(); ++i)
        CHECK_EQ(a.value().cells[i].values.raw(), b.value().cells[i].values.raw());
}
}  // namespace test_hex_terrain_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_curve_texture)
#include "PrivateTestScope.h"
#include "image/ImageData.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainCurveTexture.h"
#include "procgen/heightmap/TerrainDerivedMap.h"
#include "procgen/heightmap/TerrainFile.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace test_terrain_raster_contract {
using namespace eve::procgen;

// Curve baking maps [0,1] output texel centers through the supplied height
// curve. A non-finite source fails without modifying a prior output pixel.
TEST_CASE("procgen.terrainCurveTexture.expected_pixels_and_atomic_failure") {
    Heightmap curve(3, 1);
    curve.setHeight(0, 0, .25f);
    curve.setHeight(1, 0, 1.f);
    curve.setHeight(2, 0, -.5f);
    eve::image::ImageData image(4, 1, "RGBA32F");
    auto                  result = bakeTerrainCurveTexture(image, curve);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().writtenPixels, 4);
    CHECK_EQ(image.getPixel(0, 0).r, 0.f);
    CHECK_EQ(image.getPixel(1, 0).r, .625f);
    CHECK_EQ(image.getPixel(2, 0).r, 1.f);
    CHECK_EQ(image.getPixel(3, 0).r, .25f);
    const float before = image.getPixel(1, 0).r;
    curve.setHeight(1, 0, std::numeric_limits<float>::quiet_NaN());
    CHECK(!bakeTerrainCurveTexture(image, curve).ok());
    CHECK_EQ(image.getPixel(1, 0).r, before);
}

// A height ramp has a reproducible aspect and slope; an invalid output shape
// or unsupported mode leaves the target raster unchanged.
TEST_CASE("procgen.derivedMap.aspect_slope_and_rejected_shape") {
    Heightmap plane(5, 5), aspect(5, 5), slope(5, 5);
    for (int z = 0; z < 5; ++z)
        for (int x = 0; x < 5; ++x) plane.setHeight(x, z, float(x) / 4.f);
    REQUIRE(generateTerrainHeightmapAspect(aspect, plane, TerrainHeightmapAspect::Aspect).ok());
    CHECK(std::abs(aspect.height(2, 2) - .75f) < 1e-6f);
    REQUIRE(generateTerrainHeightmapSlope(slope, plane).ok());
    CHECK(slope.height(2, 2) > slope.height(0, 2));
    const auto before = aspect.data();
    CHECK(!generateTerrainHeightmapAspect(aspect, plane, static_cast<TerrainHeightmapAspect>(99)).ok());
    CHECK(aspect.data() == before);
    Heightmap wrong(2, 2);
    CHECK(!generateTerrainHeightmapSlope(wrong, plane).ok());
}

// Scalar arithmetic emits exact unclamped/clamped values. Division by zero
// is rejected before any target element is written.
TEST_CASE("procgen.heightmapArithmetic.scalar_clamp_and_zero_division") {
    Heightmap source(2, 1), output(2, 1);
    source.data() = {.25f, .75f};
    REQUIRE(applyTerrainHeightmapScalarArithmetic(output, source, .5f, TerrainHeightmapArithmetic::Add, false, 0.f, 0.f)
                .ok());
    CHECK(output.data() == std::vector<float>({.75f, 1.25f}));
    REQUIRE(
        applyTerrainHeightmapScalarArithmetic(output, source, 2.f, TerrainHeightmapArithmetic::Multiply, true, .2f, 1.f)
            .ok());
    CHECK(output.data() == std::vector<float>({.5f, 1.f}));
    const auto before = output.data();
    CHECK(
        !applyTerrainHeightmapScalarArithmetic(output, source, 0.f, TerrainHeightmapArithmetic::Divide, false, 0.f, 1.f)
             .ok());
    CHECK(output.data() == before);
}

// Raw files require an explicit depth/endian and an exact square payload.
// Corresponding little and big endian bytes decode to identical heights.
TEST_CASE("procgen.terrainFile.raw_depth_endian_and_invalid_length") {
    const std::array<uint8_t, 4> raw8{0, 64, 128, 255};
    auto                         eight = decodeTerrainFile(raw8, TerrainFileFormat::Raw8);
    REQUIRE(eight.ok());
    CHECK_EQ(eight.value().heightmap.height(0, 0), 0.f);
    CHECK_EQ(eight.value().heightmap.height(1, 1), 1.f);
    SUB_CASE("16-bit little and big endian equivalence") {
        const std::array<uint8_t, 8> little{0, 0, 0, 64, 0, 128, 255, 255};
        const std::array<uint8_t, 8> big{0, 0, 64, 0, 128, 0, 255, 255};
        auto                         le = decodeTerrainFile(little, TerrainFileFormat::Raw16LE);
        auto                         be = decodeTerrainFile(big, TerrainFileFormat::Raw16BE);
        REQUIRE(le.ok());
        REQUIRE(be.ok());
        CHECK(le.value().heightmap.data() == be.value().heightmap.data());
    };
    SUB_CASE("automatic and non-square formats are rejected") {
        const std::array<uint8_t, 3> malformed{0, 1, 2};
        CHECK(!decodeTerrainFile(malformed, TerrainFileFormat::Raw8).ok());
        CHECK(!decodeTerrainFile(raw8, TerrainFileFormat::Auto).ok());
    };
}
}  // namespace test_terrain_raster_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_detail)
#include "PrivateTestScope.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainDetailLayer.h"

#include <limits>

namespace test_terrain_detail_contract {
using namespace eve::procgen;

// Replace/Add/Remove use the same fitness input but distinct count rules.
// Equal fade threshold is excluded and half values round to nearest even.
TEST_CASE("procgen.detail.fitness_threshold_modes_and_invalid_input") {
    Heightmap fitness(5, 1);
    fitness.data() = {0.f, .5f, .625f, .875f, 1.f};
    TerrainDetailLayer    detail;
    TerrainDetailSettings settings;
    settings.fadeStart = .5f;
    settings.density   = 2;
    REQUIRE(detail.reset(5, 1, 7).ok());
    REQUIRE(detail.apply(fitness, settings, 1).ok());
    const int expected[] = {0, 0, 0, 2, 2};
    for (int x = 0; x < 5; ++x) CHECK_EQ(detail.sample(x, 0).value(), expected[x]);
    REQUIRE(detail.reset(5, 1, 7).ok());
    settings.mode = TerrainDetailMode::Add;
    REQUIRE(detail.apply(fitness, settings, 1).ok());
    CHECK_EQ(detail.sample(0, 0).value(), 7);
    CHECK_EQ(detail.sample(3, 0).value(), 2);
    settings.mode = TerrainDetailMode::Remove;
    REQUIRE(detail.apply(fitness, settings, 1).ok());
    CHECK_EQ(detail.sample(0, 0).value(), 7);
    CHECK_EQ(detail.sample(3, 0).value(), 0);
    fitness.setHeight(4, 0, std::numeric_limits<float>::infinity());
    CHECK(!detail.apply(fitness, settings, 1).ok());
    CHECK_EQ(detail.sample(0, 0).value(), 7);
}
}  // namespace test_terrain_detail_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_effect)
#include "PrivateTestScope.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainEffect.h"

#include <cmath>
#include <limits>
#include <vector>

namespace test_terrain_effect_contract {
using namespace eve::procgen;

// Contrast applies the PCG curve even beyond [0,1]. Non-finite intermediate
// values fail without partially publishing a changed raster.
TEST_CASE("procgen.effect.contrast_extrapolation_and_atomic_failure") {
    Heightmap terrain(3, 1), mask(3, 1);
    terrain.data() = {0.f, 1.f, 0.f};
    mask.data()    = {1.f, 1.f, 1.f};
    REQUIRE(applyTerrainContrast(terrain, mask, 2.f, 1.f).ok());
    CHECK(terrain.data() == std::vector<float>({-.3125f, 1.625f, -.3125f}));
    terrain.data()    = {0.f, std::numeric_limits<float>::max(), 0.f};
    const auto before = terrain.data();
    CHECK(!applyTerrainContrast(terrain, mask, 2.f, 1.f).ok());
    CHECK(terrain.data() == before);
}

// Smooth with a full mask keeps finite constant fields constant. The same
// operation can use its own target as input without stale pass data.
TEST_CASE("procgen.effect.smooth_constant_and_invalid_settings") {
    Heightmap terrain(5, 5), mask(5, 5);
    terrain.data().assign(25, .4f);
    mask.data().assign(25, 1.f);
    TerrainSmoothSettings settings;
    settings.radius = 1.f;
    REQUIRE(applyTerrainSmooth(terrain, mask, settings).ok());
    for (float value : terrain.data()) CHECK(std::abs(value - .4f) < 1e-5f);
    const auto before = terrain.data();
    settings.radius   = std::numeric_limits<float>::quiet_NaN();
    CHECK(!applyTerrainSmooth(terrain, mask, settings).ok());
    CHECK(terrain.data() == before);
}
}  // namespace test_terrain_effect_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_erosion)
#include "PrivateTestScope.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainErosion.h"

#include <cmath>

namespace test_terrain_erosion_contract {
using namespace eve::procgen;

// One thermal step transfers material from a center peak to neighbors while
// conserving height plus sediment per cell. Invalid aliasing is atomic.
TEST_CASE("procgen.erosion.thermal_transfer_conservation_and_alias_failure") {
    Heightmap height(3, 3), sediment(3, 3);
    height.data().assign(9, 1.f);
    sediment.data().assign(9, 0.f);
    height.setHeight(1, 1, 2.f);
    const auto             original = height.data();
    TerrainThermalSettings settings;
    settings.iterations  = 1;
    settings.dt          = .1f;
    settings.reposeSlope = 0.f;
    REQUIRE(applyTerrainThermal(height, sediment, settings).ok());
    CHECK(std::abs(height.height(1, 1) - 1.957325f) < 1e-6f);
    for (std::size_t i = 0; i < original.size(); ++i)
        CHECK(std::abs(height.data()[i] + sediment.data()[i] - original[i]) < 1e-6f);
    const auto before = height.data();
    CHECK(!applyTerrainThermal(height, height, settings).ok());
    CHECK(height.data() == before);
}
}  // namespace test_terrain_erosion_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_generation_session)
#include "PrivateTestScope.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainGenerationSession.h"
#include "procgen/heightmap/TerrainStamp.h"

namespace test_terrain_session_contract {
using namespace eve::procgen;

// Session commands own source snapshots; undo/redo moves the cursor, and a
// new branch discards redo history without changing the prior baseline.
TEST_CASE("procgen.session.stamp_history_owns_inputs_and_branches") {
    Heightmap baseline(1, 1), stamp(1, 1), mask(1, 1), output(1, 1);
    baseline.data() = {1.f};
    stamp.data()    = {2.f};
    mask.data()     = {1.f};
    TerrainStampSettings settings;
    settings.operation = TerrainStampOperation::Add;
    TerrainGenerationSession session;
    REQUIRE(session.reset(baseline).ok());
    REQUIRE(session.stamp(stamp, settings, mask, mask).ok());
    stamp.data()[0] = 100.f;
    REQUIRE(session.copyTerrain(output).ok());
    CHECK_EQ(output.data()[0], 3.f);
    REQUIRE(session.undo().ok());
    REQUIRE(session.copyTerrain(output).ok());
    CHECK_EQ(output.data()[0], 1.f);
    REQUIRE(session.redo().ok());
    REQUIRE(session.copyTerrain(output).ok());
    CHECK_EQ(output.data()[0], 3.f);
    CHECK_EQ(session.getOperationCount(), 1);
    REQUIRE(session.undo().ok());
    stamp.data()[0] = 4.f;
    REQUIRE(session.stamp(stamp, settings, mask, mask).ok());
    CHECK(!session.redo().ok());
    REQUIRE(session.copyTerrain(output).ok());
    CHECK_EQ(output.data()[0], 5.f);
}
}  // namespace test_terrain_session_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_legacy_hydraulic)
#include "PrivateTestScope.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainErosion.h"

#include <vector>

namespace test_legacy_erosion_contract {
using namespace eve::procgen;

// One hydraulic step erodes the center unless hardness protects it. Legacy
// distributed thermal erosion conserves the one-cell source across neighbors.
TEST_CASE("procgen.legacyErosion.hydraulic_hardness_and_distributed_step") {
    Heightmap height(3, 1), sediment(3, 1), hardness(3, 1), rain(3, 1);
    height.data()  = {0.f, 1.f, 0.f};
    rain.data()[1] = 1.f;
    TerrainLegacyHydraulicSettings settings;
    settings.sedimentDissolveRate = 1.f;
    REQUIRE(applyTerrainLegacyHydraulic(height, sediment, hardness, rain, settings).ok());
    CHECK(height.data() == std::vector<float>({.2f, .6f, .2f}));
    Heightmap protectedHeight(3, 1), protectedSediment(3, 1);
    protectedHeight.data() = {0.f, 1.f, 0.f};
    hardness.data()[1]     = 1.f;
    REQUIRE(applyTerrainLegacyHydraulic(protectedHeight, protectedSediment, hardness, rain, settings).ok());
    CHECK(protectedHeight.data() == std::vector<float>({0.f, 1.f, 0.f}));
    Heightmap thermal(3, 3);
    thermal.setHeight(1, 1, 1.f);
    TerrainLegacyDistributedErosionSettings distributed;
    distributed.minimumThreshold = .1f;
    REQUIRE(applyTerrainLegacyDistributedErosion(thermal, distributed).ok());
    CHECK_EQ(thermal.height(1, 1), .5f);
    CHECK_EQ(thermal.height(1, 0), .125f);
}
}  // namespace test_legacy_erosion_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_mask)
#include "PrivateTestScope.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainMask.h"
#include "procgen/heightmap/TerrainPolygonMask.h"

#include <algorithm>

namespace test_terrain_mask_contract {
using namespace eve::procgen;

// Open polygons exclude their interior; closed polygons fill it. Invalid
// world spacing fails before changing a previously rasterized mask.
TEST_CASE("procgen.mask.polygon_open_closed_and_invalid_grid") {
    TerrainPolygonMask polygon;
    REQUIRE(polygon.addNode(1.f, 1.f, .5f, .25f).ok());
    REQUIRE(polygon.addNode(3.f, 1.f, .5f, .5f).ok());
    REQUIRE(polygon.addNode(3.f, 3.f, .5f, .75f).ok());
    REQUIRE(polygon.addNode(1.f, 3.f, .5f, 1.f).ok());
    Heightmap         mask(5, 5), brush(1, 1);
    TerrainSampleGrid grid;
    grid.width = grid.height = 5;
    REQUIRE(polygon.rasterize(mask, brush, grid, TerrainPolygonMaskType::Open).ok());
    CHECK_EQ(mask.height(2, 2), 0.f);
    REQUIRE(polygon.rasterize(mask, brush, grid, TerrainPolygonMaskType::Closed).ok());
    CHECK_EQ(mask.height(2, 2), 1.f);
    const auto before = mask.data();
    grid.spacingX     = 0.f;
    CHECK(!polygon.rasterize(mask, brush, grid, TerrainPolygonMaskType::Closed).ok());
    CHECK(mask.data() == before);
}
}  // namespace test_terrain_mask_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_multitile)
#include "PrivateTestScope.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainMultiTile.h"
#include "procgen/heightmap/TerrainStamp.h"

#include <vector>

namespace test_terrain_multitile_contract {
using namespace eve::procgen;

// Two 3x3 rasters share one seam column. A world-space stamp writes the same
// value to both copies and reports the operation-to-tile mapping.
TEST_CASE("procgen.multitile.height_seam_mapping_and_shared_commit") {
    Heightmap west(3, 3), east(3, 3), stamp(1, 1), mask(1, 1);
    stamp.data()[0] = 7.f;
    mask.data()[0]  = 1.f;
    std::vector<TerrainHeightTile> tiles{{"west", &west, 0, 0, 2, 2, false}, {"east", &east, 2, 0, 2, 2, false}};
    TerrainStampSettings settings;
    settings.centerX   = 2;
    settings.centerZ   = 1;
    settings.width     = 2;
    settings.depth     = 2;
    settings.operation = TerrainStampOperation::Set;
    auto report        = applyTerrainStampMultiTile(tiles, stamp, settings, mask, mask);
    REQUIRE(report.ok());
    CHECK_EQ(report.value().affectedTiles, 2);
    REQUIRE_EQ(report.value().mappings.size(), std::size_t{2});
    CHECK_EQ(report.value().mappings[0].operationX, 0);
    CHECK_EQ(report.value().mappings[1].operationX, 1);
    CHECK_EQ(west.height(2, 1), 7.f);
    CHECK_EQ(east.height(0, 1), 7.f);
}
}  // namespace test_terrain_multitile_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_probe)
#include "PrivateTestScope.h"
#include "procgen/PointSet.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainProbePlacement.h"
#include "procgen/heightmap/TerrainSplatmap.h"

#include <algorithm>
#include <cstdint>

namespace test_terrain_probe_contract {
using namespace eve::procgen;

// Probe export keeps stable point identity and projects reflection/light
// height from the same source. Invalid settings leave prior output intact.
TEST_CASE("procgen.probe.deterministic_reflection_light_and_failure") {
    Heightmap fitness(2, 2), height(2, 2);
    fitness.data().assign(4, 1.f);
    height.data().assign(4, .5f);
    TerrainProbePlacementSettings settings;
    settings.name  = "forest-probe";
    settings.width = settings.depth = 2;
    settings.spacing                = 1.f;
    settings.jitterPercent          = 0.f;
    settings.heightScale            = 10.f;
    settings.seaLevelActive         = true;
    settings.seaLevel               = 4.f;
    settings.reflectionOffset       = 2.f;
    settings.namespaceId            = 901;
    PointSet first, repeated;
    REQUIRE(exportTerrainProbePoints(first, fitness, height, settings).ok());
    REQUIRE(exportTerrainProbePoints(repeated, fitness, height, settings).ok());
    REQUIRE_EQ(first.getCount(), 9);
    CHECK_EQ(first.points()[0].id, repeated.points()[0].id);
    CHECK_EQ(first.points()[0].y, 7.f);
    CHECK_EQ(first.getStringAttribute(0, "probeResource", ""), "forest-probe");
    settings.type = TerrainProbeType::Light;
    settings.lightOffset = 2.5f;
    REQUIRE(exportTerrainProbePoints(repeated, fitness, height, settings).ok());
    CHECK_EQ(repeated.points()[0].y, 7.5f);
    const auto id                 = repeated.points()[0].id;
    settings.reflectionResolution = 24;
    CHECK(!exportTerrainProbePoints(repeated, fitness, height, settings).ok());
    CHECK_EQ(repeated.points()[0].id, id);
}

// Painting one splat layer renormalizes all competing weights per texel;
// wrong shapes and out-of-range paints are rejected before any weight changes.
TEST_CASE("procgen.splatmap.paint_normalization_and_atomic_rejection") {
    TerrainSplatmap splat;
    REQUIRE(splat.initialize(2, 1, 3, 0).ok());
    Heightmap paint(2, 1);
    paint.data() = {.25f, 1.f};
    auto changed = paintTerrainSplatLayer(splat, paint, 1);
    REQUIRE(changed.ok());
    CHECK_EQ(changed.value(), 2);
    CHECK_EQ(splat.sample(0, 0, 0).value(), .375f);
    CHECK_EQ(splat.sample(1, 0, 0).value(), .25f);
    CHECK_EQ(splat.sample(2, 0, 0).value(), .375f);
    CHECK_EQ(splat.sample(1, 1, 0).value(), 1.f);
    Heightmap wrong(1, 1);
    CHECK(!paintTerrainSplatLayer(splat, wrong, 1).ok());
    CHECK_EQ(splat.sample(1, 1, 0).value(), 1.f);
    paint.data()[0] = 2.f;
    CHECK(!paintTerrainSplatLayer(splat, paint, 1).ok());
    CHECK_EQ(splat.sample(1, 0, 0).value(), .25f);
}
}  // namespace test_terrain_probe_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_sediment)
#include "PrivateTestScope.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainErosion.h"
#include "procgen/heightmap/TerrainWaterField.h"

#include <vector>

namespace test_terrain_sediment_contract {
using namespace eve::procgen;

// Zero dt leaves height in place but the legacy sediment accumulation still
// adds the source once. Aliasing the two outputs is rejected atomically.
TEST_CASE("procgen.sediment.stationary_accumulation_and_output_alias") {
    Heightmap height(2, 1), sediment(2, 1), vx(2, 1), vz(2, 1);
    height.data()   = {2.f, 2.f};
    sediment.data() = {3.f, 4.f};
    TerrainWaterSettings water;
    water.dt = 0.f;
    TerrainSedimentSettings reaction;
    REQUIRE(applyTerrainSediment(height, sediment, vx, vz, water, reaction).ok());
    CHECK(height.data() == std::vector<float>({2.f, 2.f}));
    CHECK(sediment.data() == std::vector<float>({6.f, 8.f}));
    const auto before = height.data();
    CHECK(!applyTerrainSediment(height, height, vx, vz, water, reaction).ok());
    CHECK(height.data() == before);
}
}  // namespace test_terrain_sediment_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_smooth_stamp)
#include "PrivateTestScope.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainStamp.h"

#include <limits>

namespace test_terrain_smooth_stamp_contract {
using namespace eve::procgen;

// SmoothRaise fades toward the footprint edge. Local and global masks have
// different roles; non-finite edge fade cannot publish partial output.
TEST_CASE("procgen.smoothStamp.center_edge_mask_and_invalid_fade") {
    Heightmap terrain(5, 5), stamp(1, 1), one(1, 1), half(1, 1);
    terrain.data().assign(25, 10.f);
    stamp.data()[0] = 10.f;
    one.data()[0]   = 1.f;
    half.data()[0]  = .5f;
    TerrainStampSettings settings;
    settings.operation = TerrainStampOperation::SmoothRaise;
    settings.centerX = settings.centerZ = 2;
    settings.width = settings.depth = 4;
    settings.smoothWidth            = 4.f;
    settings.edgeFade               = 2.f;
    REQUIRE(applyTerrainStamp(terrain, stamp, settings, one, one).ok());
    CHECK_EQ(terrain.height(2, 2), 11.f);
    CHECK_EQ(terrain.height(1, 2), 10.5f);
    CHECK_EQ(terrain.height(1, 1), 10.25f);
    const auto before = terrain.data();
    settings.edgeFade = std::numeric_limits<float>::quiet_NaN();
    CHECK(!applyTerrainStamp(terrain, stamp, settings, one, half).ok());
    CHECK(terrain.data() == before);
}
}  // namespace test_terrain_smooth_stamp_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_smooth_stamp_persistence)
#include "PrivateTestScope.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainGenerationSession.h"
#include "procgen/heightmap/TerrainStamp.h"

namespace test_terrain_snapshot_contract {
using namespace eve::procgen;

// A session snapshot preserves SmoothRaise output and cursor. Truncated JSON
// cannot change a previously restored session's committed terrain.
TEST_CASE("procgen.session.smooth_stamp_snapshot_and_rejected_restore") {
    Heightmap baseline(5, 5), stamp(1, 1), mask(1, 1), out(5, 5);
    baseline.data().assign(25, 10.f);
    stamp.data()[0] = 10.f;
    mask.data()[0]  = 1.f;
    TerrainStampSettings settings;
    settings.centerX = settings.centerZ = 2;
    settings.width = settings.depth = 4;
    settings.operation              = TerrainStampOperation::SmoothRaise;
    settings.smoothWidth            = 4.f;
    settings.edgeFade               = 2.f;
    TerrainGenerationSession original;
    REQUIRE(original.reset(baseline).ok());
    REQUIRE(original.stamp(stamp, settings, mask, mask).ok());
    REQUIRE(original.copyTerrain(out).ok());
    CHECK_EQ(out.height(2, 2), 11.f);
    auto snapshot = original.snapshotJson();
    REQUIRE(snapshot.ok());
    TerrainGenerationSession restored;
    REQUIRE(restored.restoreJson(snapshot.value()).ok());
    REQUIRE(restored.copyTerrain(out).ok());
    CHECK_EQ(out.height(2, 2), 11.f);
    auto truncated = snapshot.value();
    truncated.resize(truncated.size() / 2);
    CHECK(!restored.restoreJson(truncated).ok());
    REQUIRE(restored.copyTerrain(out).ok());
    CHECK_EQ(out.height(2, 2), 11.f);
}
}  // namespace test_terrain_snapshot_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_stamp)
#include "PrivateTestScope.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainStamp.h"

#include <array>
#include <limits>

namespace test_terrain_stamp_contract {
using namespace eve::procgen;

// The six scalar stamp operations map source 2 and stamp 3 to their documented
// outputs. A bad footprint is rejected without editing any target sample.
TEST_CASE("procgen.stamp.operation_matrix_and_invalid_footprint") {
    Heightmap stamp(1, 1), mask(1, 1);
    stamp.data()[0] = 3.f;
    mask.data()[0]  = 1.f;
    const std::array<float, 6> expected{3.f, 2.f, 2.5f, 3.f, 5.f, -1.f};
    for (int op = 0; op < 6; ++op) {
        Heightmap target(3, 3);
        target.data().assign(9, 2.f);
        TerrainStampSettings settings;
        settings.centerX = settings.centerZ = 1;
        settings.width = settings.depth = 2;
        settings.operation              = static_cast<TerrainStampOperation>(op);
        auto result                     = applyTerrainStamp(target, stamp, settings, mask, mask);
        REQUIRE(result.ok());
        for (float value : target.data()) CHECK_EQ(value, expected[std::size_t(op)]);
    }
    Heightmap target(3, 3);
    target.data().assign(9, 2.f);
    TerrainStampSettings invalid;
    invalid.width = std::numeric_limits<float>::infinity();
    CHECK(!applyTerrainStamp(target, stamp, invalid, mask, mask).ok());
    for (float value : target.data()) CHECK_EQ(value, 2.f);
}
}  // namespace test_terrain_stamp_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_tree)
#include "PrivateTestScope.h"
#include "procgen/PointSet.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainTreePlacement.h"

namespace test_terrain_tree_contract {
using namespace eve::procgen;

// Fixed fitness emits stable tree identities with projected height, scale,
// resource name and deterministic ordering. Invalid scale ranges are atomic.
TEST_CASE("procgen.tree.fixed_placement_and_invalid_range") {
    Heightmap fitness(2, 2), height(2, 2);
    fitness.data().assign(4, 1.f);
    height.data() = {0.f, 1.f, 0.f, 1.f};
    TerrainTreePlacementSettings settings;
    settings.asset = "tree/oak";
    settings.width = settings.depth = 20;
    settings.spacing                = 10;
    settings.spawnDensity           = 1;
    settings.jitterPercent          = 0;
    settings.minimumFitness = 0;
    settings.minimumWidth           = 2;
    settings.maximumWidth           = 4;
    settings.minimumHeight          = 3;
    settings.maximumHeight          = 7;
    settings.heightScale            = 10;
    settings.scaleMode = TerrainTreeScaleMode::Fixed;
    settings.seed                   = 17;
    settings.namespaceId            = 91;
    PointSet first, repeated;
    auto     result = exportTerrainTreePoints(first, fitness, height, settings);
    REQUIRE(result.ok());
    REQUIRE_EQ(result.value(), 9);
    REQUIRE(exportTerrainTreePoints(repeated, fitness, height, settings).ok());
    CHECK_EQ(first.getPointId(0), repeated.getPointId(0));
    CHECK_EQ(first.getScaleX(0), 2.f);
    CHECK_EQ(first.getScaleY(0), 3.f);
    CHECK_EQ(first.getStringAttribute(0, "asset", ""), "tree/oak");
    const auto id         = first.getPointId(0);
    settings.maximumWidth = 1.f;
    CHECK(!exportTerrainTreePoints(first, fitness, height, settings).ok());
    CHECK_EQ(first.getPointId(0), id);
}
}  // namespace test_terrain_tree_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_terrain_water)
#include "PrivateTestScope.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainWaterField.h"

#include <algorithm>
#include <vector>

namespace test_terrain_water_contract {
using namespace eve::procgen;

// Flow-map output uses the transposed PCG raster convention and preserves
// source heights. Invalid absorption leaves a prior output untouched.
TEST_CASE("procgen.water.flow_map_peak_and_invalid_rate") {
    Heightmap height(3, 3), flow(3, 3);
    height.data() = {0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f};
    TerrainWaterFlowMapSettings settings;
    settings.dropletVolume    = .11f;
    settings.absorptionRate   = .05f;
    settings.smoothIterations = 0;
    REQUIRE(generateTerrainWaterFlowMap(flow, height, settings).ok());
    CHECK(flow.data() == std::vector<float>({.1f, 0.f, 0.f, 0.f, .05f, 0.f, 0.f, 0.f, 0.f}));
    CHECK_EQ(height.height(1, 1), 1.f);
    const auto before       = flow.data();
    settings.absorptionRate = 0.f;
    CHECK(!generateTerrainWaterFlowMap(flow, height, settings).ok());
    CHECK(flow.data() == before);
}

// Velocity normalization spans [0,1] for nonflat terrain; flat input emits
// zero, and invalid smoothing iterations cannot edit an aliased source.
TEST_CASE("procgen.water.velocity_map_normalization_flat_and_invalid") {
    Heightmap slope(5, 5), first(5, 5), repeated(5, 5);
    for (int z = 0; z < 5; ++z)
        for (int x = 0; x < 5; ++x) slope.setHeight(x, z, .1f * float(x) + .03f * float(z * z));
    REQUIRE(generateTerrainVelocityFlowMap(first, slope, 4).ok());
    REQUIRE(generateTerrainVelocityFlowMap(repeated, slope, 4).ok());
    CHECK(first.data() == repeated.data());
    CHECK_EQ(*std::min_element(first.data().begin(), first.data().end()), 0.f);
    CHECK_EQ(*std::max_element(first.data().begin(), first.data().end()), 1.f);
    Heightmap flat(4, 3), output(4, 3);
    flat.data().assign(12, .25f);
    REQUIRE(generateTerrainVelocityFlowMap(output, flat, 8).ok());
    CHECK(output.data() == std::vector<float>(12, 0.f));
    const auto before = output.data();
    CHECK(!generateTerrainVelocityFlowMap(output, flat, -1).ok());
    CHECK(output.data() == before);
}
}  // namespace test_terrain_water_contract
#endif
