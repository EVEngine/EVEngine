#include <cstdint>
#include "Fixtures.h"
#include "hexmap/HexMap.h"
#include "hexmap/HexMapGenerator.h"
#include "hexmap/HexMapModule.h"
#include "hexmap/HexSphereGenerator.h"
#include "hexmap/HexSphereMap.h"
#include "hexmap/HexTerrainBake.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// hexmap_module.cpp
using namespace eve::hexmap;

namespace {

/** @brief Two chunks per side: enough that a rebuild produces several surfaces. */
constexpr std::int32_t kGridCells = 10;

}  // namespace

TEST_CASE("hexmap.module.rejectedGridSizeKeepsTheLiveGridAndItsMeshes") {
    GfxFixture   gfx(320, 240, /*useHeadless=*/true);
    HexMapModule module;

    REQUIRE(module.newGrid(gfx.gfx, kGridCells, kGridCells, 7u).ok());
    REQUIRE_EQ(module.map().cellCountX(), kGridCells);
    REQUIRE(module.rebuildDirtyChunks(gfx.gfx) > 0);

    eve::graphics::Mesh* live = module.chunkMeshAt(0, HexSurface::Terrain);
    REQUIRE(live != nullptr);

    // Every rejected size must leave the live grid *and* its meshes untouched.
    // The previous order released all GPU meshes before `HexMap::reset` had validated
    // anything, so a rejected size destroyed the meshes while the old grid stayed alive:
    // the renderables scripts already held kept pointing at destroyed meshes, and the
    // next draw threw on a null mesh.
    //
    // These are REQUIRE, not CHECK: in this suite a failed CHECK is only a warning that
    // still exits zero, so it would not fail the test.
    CHECK(!module.newGrid(gfx.gfx, 7, 7, 7u).ok());
    REQUIRE_EQ(module.map().cellCountX(), kGridCells);
    REQUIRE_EQ(module.map().cellCountZ(), kGridCells);
    REQUIRE(module.chunkMeshAt(0, HexSurface::Terrain) == live);

    CHECK(!module.newGrid(gfx.gfx, 0, kGridCells, 7u).ok());
    REQUIRE_EQ(module.map().cellCountX(), kGridCells);
    REQUIRE(module.chunkMeshAt(0, HexSurface::Terrain) == live);

    // The size is bounded here, exactly as the save format bounds it, instead of
    // reaching a raw cell allocation that would throw out of a native binding.
    CHECK(!module.newGrid(gfx.gfx, kMaxHexGridDimension + 5, kGridCells, 7u).ok());
    REQUIRE_EQ(module.map().cellCountX(), kGridCells);
    REQUIRE(module.chunkMeshAt(0, HexSurface::Terrain) == live);

    module.releaseMeshes(gfx.gfx);
}

TEST_CASE("hexmap.module.rejectedSphereShapeKeepsTheLiveMeshes") {
    GfxFixture   gfx(320, 240, /*useHeadless=*/true);
    HexMapModule module;

    REQUIRE(module.newSphere(gfx.gfx, 1, 100.f, 3u).ok());
    REQUIRE(module.rebuildSphere(gfx.gfx).ok());

    eve::graphics::Mesh* live = module.sphereTerrainMesh();
    REQUIRE(live != nullptr);

    // Same ordering rule as the grid path: `HexSphereMap::reset` rejects its arguments
    // before touching the map, so a rejected shape leaves both the map and its meshes
    // exactly as they were.
    CHECK(!module.newSphere(gfx.gfx, -1, 100.f, 3u).ok());
    REQUIRE(module.sphereTerrainMesh() == live);
    REQUIRE(!module.sphere().empty());

    module.releaseSphereMeshes(gfx.gfx);
}

TEST_CASE("hexmap.module.applyTerrainRebuildsFromAProcgenBake") {
    GfxFixture   gfx(320, 240, /*useHeadless=*/true);
    HexMapModule module;
    REQUIRE(module.newGrid(gfx.gfx, kGridCells, kGridCells, 1u).ok());
    REQUIRE(module.rebuildDirtyChunks(gfx.gfx) > 0);
    eve::graphics::Mesh* live = module.chunkMeshAt(0, HexSurface::Terrain);
    REQUIRE(live != nullptr);

    HexTerrainBake rejected;
    rejected.kind       = HexTerrainBake::Kind::Planar;
    rejected.cellCountX = 7;
    rejected.cellCountZ = 7;
    CHECK(!module.applyTerrain(gfx.gfx, rejected).ok());
    REQUIRE_EQ(module.map().seed(), 1u);
    REQUIRE(module.chunkMeshAt(0, HexSurface::Terrain) == live);

    HexMap scratch;
    REQUIRE(scratch.reset(kGridCells, kGridCells, 9u).ok());
    HexMapGeneratorSettings settings;
    settings.seed           = 9u;
    settings.landPercentage = 50;
    settings.mapBorderX     = 0;
    settings.mapBorderZ     = 0;
    settings.regionBorder   = 0;
    REQUIRE(generateHexMap(scratch, settings).ok());
    const HexTerrainBake bake = snapshotHexTerrain(scratch);

    REQUIRE(module.applyTerrain(gfx.gfx, bake).ok());
    REQUIRE_EQ(module.map().seed(), 9u);
    REQUIRE_EQ(module.map().cellCount(), kGridCells * kGridCells);
    REQUIRE_EQ(module.map().values(module.map().coordinatesAt(0)).raw(),
               scratch.values(scratch.coordinatesAt(0)).raw());
    REQUIRE(module.chunkMeshAt(0, HexSurface::Terrain) == nullptr);
    REQUIRE(module.rebuildDirtyChunks(gfx.gfx) > 0);
    REQUIRE(module.chunkMeshAt(0, HexSurface::Terrain) != nullptr);

    HexTerrainBake sphereBake = bake;
    sphereBake.kind           = HexTerrainBake::Kind::Sphere;
    CHECK(!module.applyTerrain(gfx.gfx, sphereBake).ok());
    REQUIRE_EQ(module.map().seed(), 9u);

    module.releaseMeshes(gfx.gfx);
}

TEST_CASE("hexmap.module.applySphereTerrainRebuildsMeshes") {
    GfxFixture   gfx(320, 240, /*useHeadless=*/true);
    HexMapModule module;
    REQUIRE(module.newSphere(gfx.gfx, 1, 100.f, 3u).ok());

    HexSphereMap scratch;
    REQUIRE(scratch.reset(1, 100.f, 3u).ok());
    HexSphereGeneratorSettings settings;
    settings.seed           = 3u;
    settings.landPercentage = 45;
    REQUIRE(generateSphereMap(scratch, settings).ok());
    const HexTerrainBake bake = snapshotHexSphereTerrain(scratch);

    REQUIRE(module.applySphereTerrain(gfx.gfx, bake).ok());
    REQUIRE(module.sphereTerrainMesh() != nullptr);
    REQUIRE_EQ(module.sphere().values(0).raw(), scratch.values(0).raw());

    HexMap emptyPlanar;
    CHECK(!module.applySphereTerrain(gfx.gfx, snapshotHexTerrain(emptyPlanar)).ok());

    module.releaseSphereMeshes(gfx.gfx);
}

// hexmap_terrain_bake.cpp
using namespace eve::hexmap;

namespace {

[[nodiscard]] HexMap makePlanar(std::int32_t cellCountX = 20, std::int32_t cellCountZ = 15,
                                std::uint32_t seed = 1234u) {
    HexMap map;
    auto   created = map.reset(cellCountX, cellCountZ, seed);
    REQUIRE(created.ok());
    return map;
}

[[nodiscard]] HexMapGeneratorSettings tightSettings(std::uint32_t seed) {
    HexMapGeneratorSettings settings;
    settings.seed           = seed;
    settings.landPercentage = 50;
    settings.waterLevel     = 3;
    settings.mapBorderX     = 0;
    settings.mapBorderZ     = 0;
    settings.regionBorder   = 0;
    return settings;
}

}  // namespace

TEST_CASE("hexmap.terrainBake.roundtripPreservesPlanarCells") {
    HexMap source = makePlanar();
    REQUIRE(generateHexMap(source, tightSettings(4242u)).ok());

    const HexTerrainBake bake = snapshotHexTerrain(source);
    REQUIRE_EQ(bake.kind, HexTerrainBake::Kind::Planar);
    REQUIRE_EQ(bake.cellCountX, source.cellCountX());
    REQUIRE_EQ(bake.cellCountZ, source.cellCountZ());
    REQUIRE_EQ(bake.seed, source.seed());

    const eve::Value encoded = hexTerrainBakeToValue(bake);
    auto             decoded = hexTerrainBakeFromValue(encoded);
    REQUIRE(decoded.ok());
    REQUIRE_EQ(decoded.value().kind, HexTerrainBake::Kind::Planar);
    REQUIRE_EQ(decoded.value().cells.size(), static_cast<std::size_t>(source.cellCount()));

    HexMap target;
    REQUIRE(applyHexTerrain(target, decoded.value()).ok());
    REQUIRE_EQ(target.cellCount(), source.cellCount());
    REQUIRE_EQ(target.seed(), source.seed());
    for (std::int32_t index = 0; index < source.cellCount(); ++index) {
        const HexCoordinates coordinates = source.coordinatesAt(index);
        REQUIRE_EQ(target.values(coordinates).raw(), source.values(coordinates).raw());
        REQUIRE_EQ(target.flags(coordinates).raw(), source.flags(coordinates).raw());
    }
    CHECK(target.dirtyChunkCount() > 0);
}

TEST_CASE("hexmap.terrainBake.roundtripPreservesSphereCells") {
    HexSphereMap source;
    REQUIRE(source.reset(1, 100.f, 7u).ok());
    HexSphereGeneratorSettings settings;
    settings.seed           = 7u;
    settings.landPercentage = 45;
    REQUIRE(generateSphereMap(source, settings).ok());

    const HexTerrainBake bake = snapshotHexSphereTerrain(source);
    REQUIRE_EQ(bake.kind, HexTerrainBake::Kind::Sphere);
    REQUIRE_EQ(bake.subdivision, source.subdivision());
    REQUIRE_EQ(bake.cellCount, source.cellCount());

    auto decoded = hexTerrainBakeFromValue(hexTerrainBakeToValue(bake));
    REQUIRE(decoded.ok());

    HexSphereMap target;
    REQUIRE(target.reset(1, 100.f, 99u).ok());
    REQUIRE(applyHexSphereTerrain(target, decoded.value()).ok());
    REQUIRE_EQ(target.cellCount(), source.cellCount());
    for (std::int32_t cell = 0; cell < source.cellCount(); ++cell) {
        REQUIRE_EQ(target.values(cell).raw(), source.values(cell).raw());
        REQUIRE_EQ(target.flags(cell).raw(), source.flags(cell).raw());
    }
}

TEST_CASE("hexmap.terrainBake.rejectsMalformedAndMismatchedPayloads") {
    CHECK(!hexTerrainBakeFromValue(eve::Value(1)).ok());
    eve::Value missingKind = eve::Value::object({});
    missingKind.set("seed", eve::Value(static_cast<std::int64_t>(1)));
    CHECK(!hexTerrainBakeFromValue(missingKind).ok());

    HexMap planar = makePlanar(10, 10, 3u);
    REQUIRE(generateHexMap(planar, tightSettings(3u)).ok());
    HexTerrainBake sphereBake = snapshotHexTerrain(planar);
    sphereBake.kind           = HexTerrainBake::Kind::Sphere;
    CHECK(!applyHexTerrain(planar, sphereBake).ok());

    HexSphereMap sphere;
    REQUIRE(sphere.reset(1, 50.f, 1u).ok());
    CHECK(!applyHexSphereTerrain(sphere, snapshotHexTerrain(planar)).ok());

    HexSphereMap other;
    REQUIRE(other.reset(2, 50.f, 1u).ok());
    CHECK(!applyHexSphereTerrain(other, snapshotHexSphereTerrain(sphere)).ok());

    HexMap         empty;
    HexTerrainBake badSize = snapshotHexTerrain(planar);
    badSize.cellCountX     = 7;
    badSize.cellCountZ     = 7;
    CHECK(!applyHexTerrain(empty, badSize).ok());
}
