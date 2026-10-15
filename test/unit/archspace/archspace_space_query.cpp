#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "archspace/ArchSpaceDocument.h"
#include "archspace/ArchSpaceImport.h"
#include "archspace/ArchSpaceProjector.h"
#include "archspace/ArchSpaceQuery.h"

using namespace eve::archspace;

TEST_CASE("archspace.query.architectural_region_and_door_portal") {
    Document document;
    REQUIRE(document.bootstrap("site", "building", "level0").ok());
    REQUIRE(document.setTopology(TopologyKind::Architectural).ok());
    REQUIRE(document.createRoom("level0", "living", "Living", {{0, 0}, {6, 0}, {6, 4}, {0, 4}}, 3.0, 0.2, 0.2).ok());
    REQUIRE(document.createOpening("living.wall.0", "living.door", "Door", OpeningKind::Door, 0.5, 1.0, 2.1, 0.0).ok());
    REQUIRE(document.placeItem("level0", "desk", "Desk", "furniture.desk", Vec3{2, 0, 2}, 0).ok());

    SpaceQuery query(document);
    CHECK_EQ(query.topology(), TopologyKind::Architectural);
    CHECK_EQ(query.strata().size(), 1);
    CHECK_EQ(query.regions().size(), 1);

    const auto hit = query.regionAt(Vec2{3, 2}, "level0");
    REQUIRE(hit.has_value());
    CHECK_EQ(hit->id, "living.zone");
    CHECK(!query.regionAt(Vec2{100, 100}, "level0").has_value());

    const auto anchors = query.anchorsIn("living.zone");
    REQUIRE_EQ(anchors.size(), 1);
    CHECK_EQ(anchors.front().id, "desk");

    const auto from = query.portalsFrom("living.zone");
    CHECK(!from.empty());
}

TEST_CASE("archspace.query.dungeon_regions_connected_through_portal") {
    Document document;
    REQUIRE(document.bootstrap("site", "dungeon", "level0").ok());
    REQUIRE(document.setTopology(TopologyKind::Dungeon).ok());
    REQUIRE(document.createRegion("level0", "hall", "Hall", {{0, 0}, {8, 0}, {8, 6}, {0, 6}}).ok());
    REQUIRE(document.createRegion("level0", "cell", "Cell", {{10, 1}, {14, 1}, {14, 5}, {10, 5}}).ok());
    REQUIRE(document.createPortal("level0", "door", "Door", "hall", "cell", kPassageWalk, Vec3{9, 0, 3}).ok());

    SpaceQuery query(document);
    CHECK_EQ(query.topology(), TopologyKind::Dungeon);
    CHECK_EQ(query.regions().size(), 2);
    auto connected = query.regionsConnected("hall", "cell");
    REQUIRE(connected.ok());
    CHECK(connected.value());
    connected = query.regionsConnected("cell", "hall");
    REQUIRE(connected.ok());
    CHECK(connected.value());
    CHECK(!query.regionsConnected("hall", "missing").ok());

    const auto portals = query.portalsFrom("hall");
    REQUIRE_EQ(portals.size(), 1);
    CHECK_EQ(portals.front().id, "door");
    CHECK(!portals.front().wallHosted);
}

TEST_CASE("archspace.query.cave_chamber_topology_and_grid_bake") {
    Document document;
    REQUIRE(document.bootstrap("site", "cave", "level0").ok());
    REQUIRE(document.setTopology(TopologyKind::Cave).ok());
    REQUIRE(document.createRegion("level0", "chamber.a", "A", {{0, 0}, {5, 0}, {5, 5}, {0, 5}}).ok());
    REQUIRE(document.createRegion("level0", "chamber.b", "B", {{7, 0}, {12, 0}, {12, 5}, {7, 5}}).ok());
    REQUIRE(document.createPortal("level0", "tunnel", "Tunnel", "chamber.a", "chamber.b", kPassageWalk, Vec3{6, 0, 2.5})
                .ok());

    SpaceQuery query(document);
    CHECK_EQ(query.topology(), TopologyKind::Cave);
    auto connected = query.regionsConnected("chamber.a", "chamber.b");
    REQUIRE(connected.ok());
    CHECK(connected.value());

    auto grid = bakeGridProjection(document, "level0", 1.0, "orthogonal");
    REQUIRE(grid.ok());
    CHECK(grid.value().width > 0);
    CHECK(grid.value().height > 0);
    bool hasFloor  = false;
    bool hasPortal = false;
    for (const auto kind : grid.value().cells) {
        if (kind == GridCellKind::Floor) hasFloor = true;
        if (kind == GridCellKind::Portal) hasPortal = true;
    }
    CHECK(hasFloor);
    CHECK(hasPortal);

    auto iso = bakeGridProjection(document, "level0", 1.0, "isometric");
    REQUIRE(iso.ok());
    CHECK_EQ(iso.value().orientation, "isometric");
}

TEST_CASE("archspace.projector.mesh_scene_sync_and_clear") {
    Document document;
    REQUIRE(document.bootstrap("site", "building", "level0").ok());
    REQUIRE(document.createRoom("level0", "room", "Room", {{0, 0}, {4, 0}, {4, 4}, {0, 4}}, 3.0, 0.2, 0.2).ok());

    MeshSceneProjector projector;
    CHECK_EQ(std::string(projector.kind()), "scene3d");
    REQUIRE(projector.sync(document, 7).ok());
    CHECK_EQ(projector.revision(), 7);
    CHECK(!projector.mesh().indices.empty());
    projector.clear();
    CHECK_EQ(projector.revision(), 0);
    CHECK(projector.mesh().indices.empty());
}

TEST_CASE("archspace.import.cell_grid_splits_regions_at_portal") {
    Document document;
    REQUIRE(document.bootstrap("site", "dungeon", "level0").ok());

    // Two 2x2 floor rooms separated by a portal column (and walls).
    // Layout (row z=0..2, col x=0..4):
    //   F F P F F
    //   F F P F F
    //   W W W W W
    CellGridImport grid;
    grid.width    = 5;
    grid.height   = 3;
    grid.cellSize = 1.0;
    grid.cells.assign(15, ImportCellKind::Empty);
    auto at = [&](int x, int z) -> ImportCellKind& { return grid.cells[static_cast<std::size_t>(z * grid.width + x)]; };
    for (int z = 0; z < 2; ++z) {
        at(0, z) = ImportCellKind::Floor;
        at(1, z) = ImportCellKind::Floor;
        at(2, z) = ImportCellKind::Portal;
        at(3, z) = ImportCellKind::Floor;
        at(4, z) = ImportCellKind::Floor;
    }
    for (int x = 0; x < 5; ++x) at(x, 2) = ImportCellKind::Wall;

    REQUIRE(importCellGrid(document, "level0", grid, "dg").ok());
    CHECK_EQ(document.topology(), TopologyKind::Dungeon);

    SpaceQuery query(document);
    CHECK_EQ(query.regions().size(), 2);
    auto connected = query.regionsConnected("dg.region.0", "dg.region.1");
    REQUIRE(connected.ok());
    CHECK(connected.value());
    CHECK(!query.portalsFrom("dg.region.0").empty());
}

TEST_CASE("archspace.import.rejects_bad_grid_and_missing_level") {
    Document document;
    REQUIRE(document.bootstrap("site", "dungeon", "level0").ok());

    CellGridImport bad;
    bad.width  = 2;
    bad.height = 2;
    bad.cells  = {ImportCellKind::Floor};
    CHECK(!importCellGrid(document, "level0", bad, "x").ok());
    CHECK(!importCellGrid(document, "missing", CellGridImport{1, 1, 1.0, 0, 0, {ImportCellKind::Floor}}, "x").ok());
}
