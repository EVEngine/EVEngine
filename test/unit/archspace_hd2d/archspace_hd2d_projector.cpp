#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "archspace/ArchSpaceDocument.h"
#include "archspace/hd2d/ArchSpaceHd2dProjector.h"
#include "map/TileLayer.h"

using namespace eve::archspace;
using eve::archspace_hd2d::Hd2dSpaceProjector;

TEST_CASE("archspace.hd2d.sync_builds_tile_layer_without_graphics") {
    Document document;
    REQUIRE(document.bootstrap("site", "cave", "level0").ok());
    REQUIRE(document.setTopology(TopologyKind::Cave).ok());
    REQUIRE(document.createRegion("level0", "chamber", "Chamber", {{0, 0}, {5, 0}, {5, 5}, {0, 5}}).ok());

    Hd2dSpaceProjector projector;
    projector.setLevelId("level0");
    projector.setCellSize(1.f);
    projector.setSideDepth(2.f);
    CHECK_EQ(std::string(projector.kind()), "hd2d");

    REQUIRE(projector.sync(document, 9).ok());
    CHECK_EQ(projector.revision(), 9);
    REQUIRE(projector.tiles().layer() != nullptr);
    CHECK(projector.tiles().grid().width > 0);
    CHECK_EQ(projector.tiles().orientation(), "orthogonal");

    // Mesh upload is host-driven; without Graphics the Result must fail cleanly.
    CHECK(!projector.buildMesh(nullptr).ok());

    projector.clear();
    CHECK_EQ(projector.revision(), 0);
    CHECK(projector.tiles().layer() == nullptr);
}
