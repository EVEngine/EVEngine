#include "archspace/ArchSpaceDocument.h"
#include "archspace/map/ArchSpaceTileMapProjector.h"
#include "map/TileLayer.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <limits>
#include <string>

using eve::archspace::Document;
using eve::archspace::GridCellKind;
using eve::archspace_map::ArchSpaceTilePalette;
using eve::archspace_map::TileMapProjector;

namespace {
Document roomDocument() {
    Document document;
    REQUIRE(document.bootstrap("site", "building", "level0").ok());
    REQUIRE(document.createRoom("level0", "room", "Room", {{0, 0}, {4, 0}, {4, 4}, {0, 4}}, 3.0, 0.2, 0.2).ok());
    return document;
}

void expectTilesMatchGrid(TileMapProjector& projector, const ArchSpaceTilePalette& palette) {
    auto* layer = projector.layer();
    REQUIRE(layer != nullptr);
    const auto& grid = projector.grid();
    REQUIRE_EQ(layer->getMapWidth(), grid.width);
    REQUIRE_EQ(layer->getMapHeight(), grid.height);
    bool sawFloor = false;
    for (int z = 0; z < grid.height; ++z) {
        for (int x = 0; x < grid.width; ++x) {
            const auto kind     = grid.cells[static_cast<std::size_t>(z * grid.width + x)];
            int        expected = 0;
            switch (kind) {
                case GridCellKind::Floor:
                    expected = palette.floorGid;
                    sawFloor = true;
                    break;
                case GridCellKind::Wall: expected = palette.wallGid; break;
                case GridCellKind::Portal: expected = palette.portalGid; break;
                case GridCellKind::Solid: expected = palette.solidGid; break;
                case GridCellKind::Empty: break;
            }
            REQUIRE_EQ(layer->getTile(x, z), expected);
        }
    }
    REQUIRE(sawFloor);
}
}  // namespace

TEST_CASE("Archspace map projection contract") {
    SUB_CASE("archspace.map contract: orientation accepts two wire names and rejects others without mutation") {
        TileMapProjector projector;
        REQUIRE_EQ(std::string(projector.kind()), "tile.ortho");
        REQUIRE_EQ(projector.orientation(), "orthogonal");
        REQUIRE(projector.setOrientation("isometric").ok());
        REQUIRE_EQ(std::string(projector.kind()), "tile.iso");
        REQUIRE(!projector.setOrientation("hexagonal").ok());
        REQUIRE_EQ(projector.orientation(), "isometric");
        REQUIRE(projector.setOrientation("orthogonal").ok());
        REQUIRE_EQ(std::string(projector.kind()), "tile.ortho");
    };

    SUB_CASE("archspace.map contract: invalid settings fail before a layer or revision is published") {
        auto             document = roomDocument();
        TileMapProjector projector;
        REQUIRE(!projector.sync(document, 9).ok());
        REQUIRE_EQ(projector.revision(), 0);
        REQUIRE(projector.layer() == nullptr);
        projector.setLevelId("level0");
        for (float size :
             {0.f, -1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            projector.setCellSize(size);
            REQUIRE(!projector.sync(document, 9).ok());
            REQUIRE_EQ(projector.revision(), 0);
            REQUIRE(projector.layer() == nullptr);
        }
        projector.setCellSize(1.f);
        projector.setLevelId("missing");
        REQUIRE(!projector.sync(document, 9).ok());
        REQUIRE_EQ(projector.revision(), 0);
    };

    SUB_CASE("archspace.map contract: sync maps every baked cell through the configured palette") {
        auto                 document = roomDocument();
        TileMapProjector     projector;
        ArchSpaceTilePalette palette{11, 12, 13, 14};
        projector.setLevelId("level0");
        projector.setCellSize(1.f);
        projector.setPalette(palette);
        REQUIRE(projector.sync(document, 3).ok());
        REQUIRE_EQ(projector.revision(), 3);
        REQUIRE_EQ(projector.grid().orientation, "orthogonal");
        expectTilesMatchGrid(projector, palette);
        REQUIRE(projector.setOrientation("isometric").ok());
        REQUIRE(projector.sync(document, 4).ok());
        REQUIRE_EQ(projector.revision(), 4);
        REQUIRE_EQ(projector.grid().orientation, "isometric");
        expectTilesMatchGrid(projector, palette);
    };

    SUB_CASE("archspace.map contract: repeated sync replaces tiles and clear discards owned projection") {
        auto             document = roomDocument();
        TileMapProjector projector;
        projector.setLevelId("level0");
        REQUIRE(projector.sync(document, 1).ok());
        auto* layer = projector.layer();
        REQUIRE(layer != nullptr);
        layer->setTile(0, 0, 99);
        REQUIRE(projector.sync(document, 2).ok());
        REQUIRE(projector.layer() == layer);
        expectTilesMatchGrid(projector, ArchSpaceTilePalette{});
        projector.clear();
        REQUIRE(projector.layer() == nullptr);
        REQUIRE_EQ(projector.revision(), 0);
        REQUIRE_EQ(projector.grid().width, 0);
        REQUIRE(projector.sync(document, 3).ok());
        REQUIRE_EQ(projector.revision(), 3);
    };
}
