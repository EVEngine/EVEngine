#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "archspace/ArchSpaceDocument.h"
#include "archspace/map/ArchSpaceTileMapProjector.h"
#include "map/TileLayer.h"

using namespace eve::archspace;
using eve::archspace_map::TileMapProjector;

TEST_CASE("archspace.map.tile_ortho_sync_writes_gids") {
    Document document;
    REQUIRE(document.bootstrap("site", "building", "level0").ok());
    REQUIRE(document.createRoom("level0", "room", "Room", {{0, 0}, {4, 0}, {4, 4}, {0, 4}}, 3.0, 0.2, 0.2).ok());

    TileMapProjector projector;
    projector.setLevelId("level0");
    projector.setCellSize(1.f);
    REQUIRE(projector.setOrientation("orthogonal").ok());
    CHECK_EQ(std::string(projector.kind()), "tile.ortho");

    REQUIRE(projector.sync(document, 3).ok());
    CHECK_EQ(projector.revision(), 3);
    REQUIRE(projector.layer() != nullptr);
    CHECK(projector.layer()->getMapWidth() > 0);
    CHECK(projector.layer()->getMapHeight() > 0);
    CHECK(projector.grid().width > 0);

    bool hasFloorGid = false;
    for (int z = 0; z < projector.layer()->getMapHeight(); ++z) {
        for (int x = 0; x < projector.layer()->getMapWidth(); ++x) {
            if (projector.layer()->getTile(x, z) == 1) hasFloorGid = true;
        }
    }
    CHECK(hasFloorGid);

    projector.clear();
    CHECK_EQ(projector.revision(), 0);
}

TEST_CASE("archspace.map.tile_iso_orientation_and_reject") {
    Document document;
    REQUIRE(document.bootstrap("site", "dungeon", "level0").ok());
    REQUIRE(document.setTopology(TopologyKind::Dungeon).ok());
    REQUIRE(document.createRegion("level0", "hall", "Hall", {{0, 0}, {3, 0}, {3, 3}, {0, 3}}).ok());

    TileMapProjector projector;
    projector.setLevelId("level0");
    projector.setCellSize(1.f);
    CHECK(!projector.setOrientation("hexagonal").ok());
    REQUIRE(projector.setOrientation("isometric").ok());
    CHECK_EQ(std::string(projector.kind()), "tile.iso");
    REQUIRE(projector.sync(document, 1).ok());
    CHECK_EQ(projector.grid().orientation, "isometric");
}

TEST_CASE("archspace.map.tile_requires_level_id") {
    Document document;
    REQUIRE(document.bootstrap("site", "building", "level0").ok());
    TileMapProjector projector;
    projector.setCellSize(1.f);
    CHECK(!projector.sync(document, 1).ok());
}
