#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "map/Map.h"
#include "map/TileLayer.h"
#include "map/Pathfinder.h"
#include "map/Path.h"
#include "map/FlowField.h"
#include "map/PathTopology.h"
#include "map/TileOrientation.h"

#include <string>

using namespace eve::map;

namespace {

void fillOpen(TileLayer *layer, int floorGid = 2) { layer->fill(floorGid); }

void wallRect(TileLayer *layer, int x0, int y0, int x1, int y1, int wallGid = 1) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) layer->setTile(x, y, wallGid);
}

bool pathEndsAt(Path *p, int x, int y) {
    if (!p || p->getLength() <= 0) return false;
    return p->getX(p->getLength() - 1) == x && p->getY(p->getLength() - 1) == y;
}

bool pathStartsAt(Path *p, int x, int y) {
    if (!p || p->getLength() <= 0) return false;
    return p->getX(0) == x && p->getY(0) == y;
}

bool pathWalkable(Path *p, Pathfinder *pf) {
    if (!p) return false;
    for (int i = 0; i < p->getLength(); ++i) {
        if (!pf->isWalkable(p->getX(i), p->getY(i))) return false;
    }
    return true;
}

}  // namespace

TEST_CASE("map.path.topology.parse") {
    CHECK_EQ(static_cast<int>(parsePathTopology("ortho4")), static_cast<int>(PathTopology::Ortho4));
    CHECK_EQ(static_cast<int>(parsePathTopology("ortho8")), static_cast<int>(PathTopology::Ortho8));
    CHECK_EQ(static_cast<int>(parsePathTopology("hex")), static_cast<int>(PathTopology::Hex));
    CHECK_EQ(static_cast<int>(parsePathTopology("auto", PathTopology::Hex)),
             static_cast<int>(PathTopology::Hex));
    CHECK_EQ(static_cast<int>(parsePathTopology("nope", PathTopology::Ortho4)),
             static_cast<int>(PathTopology::Ortho4));
    CHECK_EQ(pathTopologyName(PathTopology::Hex), std::string("hex"));
}

TEST_CASE("map.path.astar.orthoBypassWall") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(8, 5, 16.f, 16.f);
    fillOpen(layer);
    // Vertical wall with a gap at y=2
    for (int y = 0; y < 5; ++y) layer->setTile(3, y, 1);
    layer->setTile(3, 2, 2);

    Pathfinder *pf = mod->newPathfinder(layer);
    REQUIRE(pf != nullptr);
    pf->blockGid(1);
    pf->setTopology("ortho4");

    Path *path = pf->findPath(0, 2, 7, 2);
    REQUIRE(path != nullptr);
    CHECK(path->getLength() > 0);
    CHECK(pathStartsAt(path, 0, 2));
    CHECK(pathEndsAt(path, 7, 2));
    CHECK(pathWalkable(path, pf));
    // Must pass through the gap (3,2)
    bool viaGap = false;
    for (int i = 0; i < path->getLength(); ++i) {
        if (path->getX(i) == 3 && path->getY(i) == 2) viaGap = true;
    }
    CHECK(viaGap);
    delete path;
    delete pf;
}

TEST_CASE("map.path.astar.unreachable") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(5, 3, 8.f, 8.f);
    fillOpen(layer);
    for (int y = 0; y < 3; ++y) layer->setTile(2, y, 1);
    Pathfinder *pf = mod->newPathfinder(layer);
    pf->blockGid(1);
    Path *path = pf->findPath(0, 1, 4, 1);
    REQUIRE(path != nullptr);
    CHECK_EQ(path->getLength(), 0);
    delete path;
    delete pf;
}

TEST_CASE("map.path.astar.ortho8NoCornerCut") {
    auto *mod = Map::create();
    // Corridor with a wall at (1,1). Diagonal shortcuts that clip the wall corner are banned.
    //   . . .
    //   S # G
    //   . . .
    TileLayer *layer = mod->newLayer(3, 3, 8.f, 8.f);
    fillOpen(layer);
    layer->setTile(1, 1, 1);
    Pathfinder *pf = mod->newPathfinder(layer);
    pf->blockGid(1);
    pf->setTopology("ortho8");
    Path *path = pf->findPath(0, 1, 2, 1);
    REQUIRE(path != nullptr);
    CHECK(path->getLength() > 0);
    CHECK(pathEndsAt(path, 2, 1));
    CHECK(pathWalkable(path, pf));
    // Must not use diagonal into/out of the wall corner: never visit only via illegal cut.
    // Path length with no corner cut is at least 5 (around the wall).
    CHECK(path->getLength() >= 5);
    delete path;
    delete pf;
}

TEST_CASE("map.path.customGrid") {
    auto *mod = Map::create();
    Pathfinder *pf = mod->newPathfinderSize(4, 4);
    REQUIRE(pf != nullptr);
    pf->setTopology("ortho4");
    pf->setBlocked(1, 0, true);
    pf->setBlocked(1, 1, true);
    pf->setBlocked(1, 2, true);
    Path *blocked = pf->findPath(0, 1, 3, 1);
    CHECK_EQ(blocked->getLength(), 0);
    delete blocked;

    pf->setBlocked(1, 1, false);  // open a hole
    Path *path = pf->findPath(0, 1, 3, 1);
    CHECK(pathStartsAt(path, 0, 1));
    CHECK(pathEndsAt(path, 3, 1));
    delete path;
    delete pf;
}

TEST_CASE("map.path.flowField.groupSameGoal") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(10, 8, 16.f, 16.f);
    fillOpen(layer);
    wallRect(layer, 4, 0, 4, 5);
    layer->setTile(4, 6, 2);  // gap at bottom

    Pathfinder *pf = mod->newPathfinder(layer);
    pf->blockGid(1);
    pf->setTopology("ortho4");

    const int gx = 9, gy = 1;
    FlowField *field = pf->buildFlowField(gx, gy);
    REQUIRE(field != nullptr);
    CHECK_EQ(field->getGoalX(), gx);
    CHECK_EQ(field->getGoalY(), gy);
    CHECK(field->isReachable(0, 0));
    CHECK(field->isReachable(1, 7));

    // Multiple agents share one field
    int starts[][2] = {{0, 0}, {0, 7}, {2, 3}};
    for (auto &s : starts) {
        Path *p = pf->followFlow(field, s[0], s[1]);
        CHECK(pathStartsAt(p, s[0], s[1]));
        CHECK(pathEndsAt(p, gx, gy));
        CHECK(pathWalkable(p, pf));
        delete p;
    }

    // findGroupPath uses cache
    Path *g0 = pf->findGroupPath(0, 0, gx, gy);
    Path *g1 = pf->findGroupPath(2, 3, gx, gy);
    CHECK(pathEndsAt(g0, gx, gy));
    CHECK(pathEndsAt(g1, gx, gy));
    delete g0;
    delete g1;
    delete field;
    delete pf;
}

TEST_CASE("map.path.flowField.matchesAstarReachability") {
    auto *mod = Map::create();
    Pathfinder *pf = mod->newPathfinderSize(6, 6);
    pf->setTopology("ortho4");
    // Block a corridor
    for (int y = 0; y < 5; ++y) pf->setBlocked(3, y, true);

    Path *a = pf->findPath(0, 0, 5, 0);
    Path *b = pf->findGroupPath(0, 0, 5, 0);
    // Both unreachable (wall seals top)
    CHECK_EQ(a->getLength(), 0);
    CHECK_EQ(b->getLength(), 0);
    delete a;
    delete b;

    pf->setBlocked(3, 5, false);
    // open bottom — wait, wall is y=0..4 at x=3, y=5 already open
    Path *a2 = pf->findPath(0, 0, 5, 0);
    Path *b2 = pf->findGroupPath(0, 0, 5, 0);
    CHECK(a2->getLength() > 0);
    CHECK(b2->getLength() > 0);
    CHECK(pathEndsAt(a2, 5, 0));
    CHECK(pathEndsAt(b2, 5, 0));
    delete a2;
    delete b2;
    delete pf;
}

TEST_CASE("map.path.hex.staggeredNeighborsReach") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(5, 5, 64.f, 32.f);
    layer->config()->orientation = MapOrientation::Staggered;
    layer->config()->staggerAxis = StaggerAxis::Y;
    layer->config()->staggerIndex = StaggerIndex::Odd;
    fillOpen(layer);

    Pathfinder *pf = mod->newPathfinder(layer);
    pf->setTopology("auto");
    CHECK_EQ(pf->getTopology(), std::string("hex"));

    Path *path = pf->findPath(0, 0, 4, 4);
    REQUIRE(path != nullptr);
    CHECK(path->getLength() > 0);
    CHECK(pathEndsAt(path, 4, 4));
    // Consecutive steps must be hex-neighbors
    for (int i = 1; i < path->getLength(); ++i) {
        const int x0 = path->getX(i - 1), y0 = path->getY(i - 1);
        const int x1 = path->getX(i), y1 = path->getY(i);
        bool ok = false;
        forEachNeighbor(PathTopology::Hex, x0, y0, true, true, [&](int nx, int ny, float) {
            if (nx == x1 && ny == y1) ok = true;
        });
        CHECK(ok);
    }
    delete path;
    delete pf;
}

TEST_CASE("map.path.isometric.autoUsesOrtho") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(4, 4, 64.f, 32.f);
    layer->config()->orientation = MapOrientation::Isometric;
    fillOpen(layer);
    Pathfinder *pf = mod->newPathfinder(layer);
    pf->setTopology("auto");
    CHECK_EQ(pf->getTopology(), std::string("ortho4"));
    Path *path = pf->findPath(0, 0, 3, 3);
    CHECK(pathEndsAt(path, 3, 3));
    delete path;

    pf->setDiagonal(true);
    pf->setTopology("auto");
    CHECK_EQ(pf->getTopology(), std::string("ortho8"));
    delete pf;
}

TEST_CASE("map.path.blockGid.syncFromLayer") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(3, 1, 8.f, 8.f);
    layer->setTile(0, 0, 2);
    layer->setTile(1, 0, 2);
    layer->setTile(2, 0, 2);
    Pathfinder *pf = mod->newPathfinder(layer);
    pf->blockGid(1);
    Path *ok = pf->findPath(0, 0, 2, 0);
    CHECK(ok->getLength() > 0);
    delete ok;

    layer->setTile(1, 0, 1);
    pf->syncFromLayer();
    Path *blocked = pf->findPath(0, 0, 2, 0);
    CHECK_EQ(blocked->getLength(), 0);
    delete blocked;
    delete pf;
}

TEST_CASE("map.path.cellCost.prefersCheaper") {
    auto *mod = Map::create();
    Pathfinder *pf = mod->newPathfinderSize(3, 3);
    pf->setTopology("ortho4");
    // Two routes from (0,1) to (2,1): through (1,1) expensive, or around via y=0
    pf->setCellCost(1, 1, 10.f);
    Path *path = pf->findPath(0, 1, 2, 1);
    REQUIRE(path->getLength() > 0);
    bool throughCenter = false;
    for (int i = 0; i < path->getLength(); ++i)
        if (path->getX(i) == 1 && path->getY(i) == 1) throughCenter = true;
    CHECK(!throughCenter);
    delete path;
    delete pf;
}

TEST_CASE("map.path.nullLayerReturnsNull") {
    auto *mod = Map::create();
    CHECK(mod->newPathfinder(nullptr) == nullptr);
    CHECK(mod->newPathfinderSize(0, 5) == nullptr);
}
