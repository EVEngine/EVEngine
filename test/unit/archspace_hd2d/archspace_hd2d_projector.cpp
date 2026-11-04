#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "archspace/ArchSpaceDocument.h"
#include "archspace/hd2d/ArchSpaceHd2dProjector.h"
#include "map/TileLayer.h"

#include <limits>
#include <string>

namespace {
using eve::archspace::Document;
using eve::archspace::TopologyKind;
using eve::archspace_hd2d::Hd2dSpaceProjector;

// Input contract: level id and finite positive cell size are required before sync;
// rejection leaves revision and tile ownership at their default values.
TEST_CASE("archspace.hd2d.spec.sync_rejects_invalid_configuration") {
    Document document;
    REQUIRE(document.bootstrap("site", "cave", "level0").ok());
    REQUIRE(document.setTopology(TopologyKind::Cave).ok());
    REQUIRE(document.createRegion("level0", "chamber", "Chamber", {{0, 0}, {5, 0}, {5, 5}, {0, 5}}).ok());
    Hd2dSpaceProjector projector;
    CHECK_EQ(std::string(projector.kind()), "hd2d");
    CHECK(!projector.sync(document, 1).ok());
    CHECK_EQ(projector.revision(), 0);
    CHECK(projector.tiles().layer() == nullptr);
    projector.setLevelId("level0");
    projector.setCellSize(0.f);
    CHECK(!projector.sync(document, 2).ok());
    projector.setCellSize(std::numeric_limits<float>::infinity());
    CHECK(!projector.sync(document, 3).ok());
    CHECK_EQ(projector.revision(), 0);
    CHECK(projector.tiles().layer() == nullptr);
}

// Projection contract: sync bakes a positive grid into an orthogonal layer and
// records the caller's revision. Clear releases owned state and permits resync.
TEST_CASE("archspace.hd2d.spec.sync_clear_and_resync") {
    Document document;
    REQUIRE(document.bootstrap("site", "cave", "level0").ok());
    REQUIRE(document.setTopology(TopologyKind::Cave).ok());
    REQUIRE(document.createRegion("level0", "chamber", "Chamber", {{0, 0}, {5, 0}, {5, 5}, {0, 5}}).ok());
    Hd2dSpaceProjector projector;
    projector.setLevelId("level0");
    projector.setCellSize(1.f);
    projector.setSideDepth(2.f);
    projector.setHeightScale(0.5f);
    REQUIRE(projector.sync(document, 9).ok());
    CHECK_EQ(projector.revision(), 9);
    REQUIRE(projector.tiles().layer() != nullptr);
    CHECK(projector.tiles().grid().width > 0);
    CHECK(projector.tiles().grid().height > 0);
    CHECK_EQ(projector.tiles().orientation(), "orthogonal");
    CHECK_EQ(projector.lastTileCount(), 0);
    projector.clear();
    CHECK_EQ(projector.revision(), 0);
    CHECK(projector.tiles().layer() == nullptr);
    CHECK_EQ(projector.tiles().grid().width, 0);
    REQUIRE(projector.sync(document, 10).ok());
    CHECK_EQ(projector.revision(), 10);
    CHECK(projector.tiles().layer() != nullptr);
}

// Host contract: mesh and renderable baking require Graphics. Null host input
// fails before or after sync without changing the last successful tile count.
TEST_CASE("archspace.hd2d.spec.mesh_methods_require_graphics") {
    Hd2dSpaceProjector projector;
    CHECK(!projector.buildMesh(nullptr).ok());
    CHECK(!projector.buildRenderable(nullptr).ok());
    CHECK_EQ(projector.lastTileCount(), 0);
    Document document;
    REQUIRE(document.bootstrap("site", "cave", "level0").ok());
    REQUIRE(document.setTopology(TopologyKind::Cave).ok());
    REQUIRE(document.createRegion("level0", "chamber", "Chamber", {{0, 0}, {5, 0}, {5, 5}, {0, 5}}).ok());
    projector.setLevelId("level0");
    REQUIRE(projector.sync(document, 4).ok());
    CHECK(!projector.buildMesh(nullptr).ok());
    CHECK(!projector.buildRenderable(nullptr).ok());
    CHECK_EQ(projector.lastTileCount(), 0);
    CHECK_EQ(projector.revision(), 4);
}
}  // namespace
