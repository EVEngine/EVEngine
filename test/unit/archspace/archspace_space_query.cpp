#include "archspace/ArchSpaceDocument.h"
#include "archspace/ArchSpaceImport.h"
#include "archspace/ArchSpaceProjector.h"
#include "archspace/ArchSpaceQuery.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string>

using namespace eve::archspace;

namespace {
Document dungeon() {
    Document document;
    REQUIRE(document.bootstrap("site", "dungeon", "level0").ok());
    REQUIRE(document.setTopology(TopologyKind::Dungeon).ok());
    REQUIRE(document.createRegion("level0", "hall", "Hall", {{0, 0}, {8, 0}, {8, 6}, {0, 6}}).ok());
    REQUIRE(document.createRegion("level0", "cell", "Cell", {{10, 1}, {14, 1}, {14, 5}, {10, 5}}).ok());
    return document;
}

CellGridImport splitGrid() {
    CellGridImport grid;
    grid.width    = 5;
    grid.height   = 3;
    grid.cellSize = 1.0;
    grid.cells.assign(15, ImportCellKind::Empty);
    for (int z = 0; z < 2; ++z) {
        for (int x : {0, 1, 3, 4}) grid.cells[static_cast<std::size_t>(z * 5 + x)] = ImportCellKind::Floor;
        grid.cells[static_cast<std::size_t>(z * 5 + 2)] = ImportCellKind::Portal;
    }
    for (int x = 0; x < 5; ++x) grid.cells[static_cast<std::size_t>(10 + x)] = ImportCellKind::Wall;
    return grid;
}
}  // namespace

TEST_CASE("Archspace region and portal query contract") {
    SUB_CASE("archspace query contract: region containment is scoped to level and anchors") {
        Document document;
        REQUIRE(document.bootstrap("site", "building", "level0").ok());
        REQUIRE(document.createRoom("level0", "living", "Living", {{0, 0}, {6, 0}, {6, 4}, {0, 4}}, 3.0, .2, .2).ok());
        REQUIRE(document.placeItem("level0", "desk", "Desk", "furniture.desk", Vec3{2, 0, 2}, 0).ok());
        SpaceQuery query(document);
        REQUIRE_EQ(query.topology(), TopologyKind::Architectural);
        REQUIRE_EQ(query.strata().size(), 1u);
        REQUIRE_EQ(query.regions().size(), 1u);
        auto inside = query.regionAt(Vec2{3, 2}, "level0");
        REQUIRE(inside.has_value());
        REQUIRE_EQ(inside->id, "living.zone");
        REQUIRE(!query.regionAt(Vec2{3, 2}, "missing").has_value());
        REQUIRE(!query.regionAt(Vec2{100, 100}, "level0").has_value());
        auto anchors = query.anchorsIn("living.zone");
        REQUIRE_EQ(anchors.size(), 1u);
        REQUIRE_EQ(anchors.front().id, "desk");
        REQUIRE(query.anchorsIn("missing").empty());
    };

    SUB_CASE("archspace query contract: portal graph is undirected and unknown IDs fail") {
        auto       document = dungeon();
        SpaceQuery before(document);
        auto       disconnected = before.regionsConnected("hall", "cell");
        REQUIRE(disconnected.ok());
        REQUIRE(!disconnected.value());
        REQUIRE(!before.regionsConnected("hall", "missing").ok());
        REQUIRE(!before.regionsConnected("", "hall").ok());

        REQUIRE(document.createPortal("level0", "door", "Door", "hall", "cell", kPassageWalk, Vec3{9, 0, 3}).ok());
        SpaceQuery after(document);
        auto       forward = after.regionsConnected("hall", "cell");
        auto       reverse = after.regionsConnected("cell", "hall");
        REQUIRE(forward.ok());
        REQUIRE(reverse.ok());
        REQUIRE(forward.value());
        REQUIRE(reverse.value());
        auto portals = after.portalsFrom("hall");
        REQUIRE_EQ(portals.size(), 1u);
        REQUIRE_EQ(portals.front().id, "door");
        REQUIRE(!portals.front().wallHosted);
    };
}

TEST_CASE("Archspace source import contract") {
    SUB_CASE("archspace import contract: portal cells separate flood fills then connect regions") {
        Document document;
        REQUIRE(document.bootstrap("site", "dungeon", "level0").ok());
        auto grid = splitGrid();
        REQUIRE(importCellGrid(document, "level0", grid, "dg").ok());
        SpaceQuery query(document);
        REQUIRE_EQ(query.topology(), TopologyKind::Dungeon);
        REQUIRE_EQ(query.regions().size(), 2u);
        auto connected = query.regionsConnected("dg.region.0", "dg.region.1");
        REQUIRE(connected.ok());
        REQUIRE(connected.value());
        REQUIRE(!query.portalsFrom("dg.region.0").empty());
    };

    SUB_CASE("archspace import contract: invalid dimensions and missing level preserve document") {
        Document document;
        REQUIRE(document.bootstrap("site", "dungeon", "level0").ok());
        const auto before    = document.nodeCount();
        auto       malformed = splitGrid();
        malformed.cells.pop_back();
        REQUIRE(!importCellGrid(document, "level0", malformed, "dg").ok());
        REQUIRE_EQ(document.nodeCount(), before);
        REQUIRE(!importCellGrid(document, "missing", splitGrid(), "dg").ok());
        REQUIRE_EQ(document.nodeCount(), before);
        REQUIRE(!importCellGrid(document, "level0", splitGrid(), "").ok());
        REQUIRE_EQ(document.nodeCount(), before);
    };
}

TEST_CASE("Archspace raster and mesh projection contract") {
    SUB_CASE("archspace projection contract: level rasterization validates inputs and preserves orientation hint") {
        auto document = dungeon();
        REQUIRE(!bakeGridProjection(document, "missing", 1.0).ok());
        REQUIRE(!bakeGridProjection(document, "level0", 0.0).ok());
        REQUIRE(!bakeGridProjection(document, "level0", 1.0, "hexagonal").ok());
        auto orthogonal = bakeGridProjection(document, "level0", 1.0, "orthogonal");
        auto isometric  = bakeGridProjection(document, "level0", 1.0, "isometric");
        REQUIRE(orthogonal.ok());
        REQUIRE(isometric.ok());
        REQUIRE_EQ(orthogonal.value().orientation, "orthogonal");
        REQUIRE_EQ(isometric.value().orientation, "isometric");
        REQUIRE_EQ(orthogonal.value().width, isometric.value().width);
        REQUIRE_EQ(orthogonal.value().height, isometric.value().height);
        REQUIRE_EQ(orthogonal.value().cells.size(),
                   static_cast<std::size_t>(orthogonal.value().width * orthogonal.value().height));
        bool sawFloor = false;
        for (auto cell : orthogonal.value().cells) sawFloor |= cell == GridCellKind::Floor;
        REQUIRE(sawFloor);
    };

    SUB_CASE("archspace projection contract: mesh sync owns revision and clear removes derived data") {
        Document document;
        REQUIRE(document.bootstrap("site", "building", "level0").ok());
        REQUIRE(document.createRoom("level0", "room", "Room", {{0, 0}, {4, 0}, {4, 4}, {0, 4}}, 3.0, .2, .2).ok());
        MeshSceneProjector projector;
        REQUIRE_EQ(std::string(projector.kind()), "scene3d");
        REQUIRE_EQ(projector.revision(), 0u);
        REQUIRE(projector.mesh().indices.empty());
        REQUIRE(projector.sync(document, 7).ok());
        REQUIRE_EQ(projector.revision(), 7u);
        REQUIRE(!projector.mesh().indices.empty());
        projector.clear();
        REQUIRE_EQ(projector.revision(), 0u);
        REQUIRE(projector.mesh().indices.empty());
    };
}
