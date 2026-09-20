#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "Fixtures.h"

#include "hexmap/HexMapModule.h"

#include <cstdint>

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
