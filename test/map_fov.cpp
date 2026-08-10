#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "map/Map.h"
#include "map/TileLayer.h"
#include "map/Fov.h"

#include <string>

using namespace eve::map;

namespace {

int countVisible(Fov *fov) {
    int n = 0;
    for (int y = 0; y < fov->getHeight(); ++y)
        for (int x = 0; x < fov->getWidth(); ++x)
            if (fov->isVisible(x, y)) ++n;
    return n;
}

}  // namespace

TEST_CASE("map.fov.algorithm.defaults") {
    auto *mod = Map::create();
    Fov *fov = mod->newFovSize(4, 4);
    REQUIRE(fov != nullptr);
    CHECK_EQ(fov->getWidth(), 4);
    CHECK_EQ(fov->getHeight(), 4);
    CHECK_EQ(fov->getAlgorithm(), std::string("shadowcast"));
    CHECK_EQ(fov->getRadiusMetric(), std::string("euclidean"));
    fov->setRadiusMetric("chebyshev");
    CHECK_EQ(fov->getRadiusMetric(), std::string("chebyshev"));
    fov->setRadiusMetric("nope");
    CHECK_EQ(fov->getRadiusMetric(), std::string("chebyshev"));
    fov->setAlgorithm("raycast");
    CHECK_EQ(fov->getAlgorithm(), std::string("shadowcast"));
    delete fov;
}

TEST_CASE("map.fov.openRadius.chebyshev") {
    auto *mod = Map::create();
    Fov *fov = mod->newFovSize(11, 11);
    REQUIRE(fov != nullptr);
    fov->setRadiusMetric("chebyshev");
    fov->setBlockEmpty(false);
    const int id = fov->addRevealer(5, 5, 2);
    CHECK(id >= 1);
    fov->compute();
    CHECK(fov->isVisible(5, 5));
    CHECK(fov->isVisible(5 + 2, 5));
    CHECK(fov->isVisible(5, 5 + 2));
    CHECK(fov->isVisible(5 + 2, 5 + 2));
    CHECK(!fov->isVisible(5 + 3, 5));
    // Chebyshev radius 2 => 5x5 = 25 cells
    CHECK_EQ(countVisible(fov), 25);
    delete fov;
}

TEST_CASE("map.fov.wallCastsShadow") {
    auto *mod = Map::create();
    Fov *fov = mod->newFovSize(9, 5);
    REQUIRE(fov != nullptr);
    fov->setRadiusMetric("chebyshev");
    fov->setBlockEmpty(false);
    // Vertical wall at x=4, gap none — full column opaque
    for (int y = 0; y < 5; ++y) fov->setOpaque(4, y, true);
    fov->addRevealer(1, 2, 6);
    fov->compute();
    CHECK(fov->isVisible(1, 2));
    CHECK(fov->isVisible(4, 2));  // blocking cell itself is visible
    CHECK(!fov->isVisible(6, 2)); // behind wall
    CHECK(!fov->isVisible(7, 2));
    delete fov;
}

TEST_CASE("map.fov.corridorAndRoom") {
    auto *mod = Map::create();
    Fov *fov = mod->newFovSize(7, 5);
    fov->setBlockEmpty(false);
    fov->setRadiusMetric("chebyshev");
    // Room walls with a corridor opening east from (1,2)
    for (int x = 0; x < 7; ++x) {
        fov->setOpaque(x, 0, true);
        fov->setOpaque(x, 4, true);
    }
    for (int y = 0; y < 5; ++y) {
        fov->setOpaque(0, y, true);
        fov->setOpaque(6, y, true);
    }
    // Interior divider with door at (3,2)
    fov->setOpaque(3, 1, true);
    fov->setOpaque(3, 3, true);
    fov->addRevealer(1, 2, 8);
    fov->compute();
    CHECK(fov->isVisible(1, 2));
    CHECK(fov->isVisible(2, 2));
    CHECK(fov->isVisible(3, 2));  // open gap between walls
    CHECK(fov->isVisible(4, 2));  // room beyond door
    CHECK(fov->isVisible(3, 1));  // opaque wall cell is lit when hit
    delete fov;
}

TEST_CASE("map.fov.coneFacing") {
    auto *mod = Map::create();
    Fov *fov = mod->newFovSize(11, 11);
    fov->setBlockEmpty(false);
    fov->setRadiusMetric("chebyshev");
    const int id = fov->addRevealer(5, 5, 4);
    // Face +X (east), half-angle 30°
    fov->setRevealerFacing(id, 0.f, 30.f);
    fov->compute();
    CHECK(fov->isVisible(5, 5));
    CHECK(fov->isVisible(8, 5));   // east
    CHECK(!fov->isVisible(2, 5));  // west outside cone
    CHECK(!fov->isVisible(5, 8));  // south outside cone
    fov->clearRevealerFacing(id);
    fov->compute();
    CHECK(fov->isVisible(2, 5));
    delete fov;
}

TEST_CASE("map.fov.multiRevealerOr") {
    auto *mod = Map::create();
    Fov *fov = mod->newFovSize(15, 5);
    fov->setBlockEmpty(false);
    fov->setRadiusMetric("chebyshev");
    fov->addRevealer(1, 2, 1);
    fov->addRevealer(13, 2, 1);
    fov->compute();
    CHECK(fov->isVisible(1, 2));
    CHECK(fov->isVisible(13, 2));
    CHECK(!fov->isVisible(7, 2));
    CHECK_EQ(fov->getRevealerCount(), 2);
    delete fov;
}

TEST_CASE("map.fov.exploredMemory") {
    auto *mod = Map::create();
    Fov *fov = mod->newFovSize(9, 9);
    fov->setBlockEmpty(false);
    fov->setRadiusMetric("chebyshev");
    const int id = fov->addRevealer(4, 4, 2);
    fov->compute();
    CHECK(fov->isVisible(4, 4));
    CHECK(fov->isExplored(4, 4));
    CHECK_EQ(fov->getState(4, 4), std::string("visible"));

    fov->setRevealerPosition(id, 0, 0);
    fov->setRevealerRadius(id, 1);
    fov->compute();
    CHECK(!fov->isVisible(4, 4));
    CHECK(fov->isExplored(4, 4));
    CHECK_EQ(fov->getState(4, 4), std::string("explored"));
    CHECK(fov->isVisible(0, 0));

    fov->clearMemory();
    CHECK(!fov->isExplored(4, 4));
    CHECK_EQ(fov->getState(4, 4), std::string("unknown"));
    // clearMemory does not recompute; current visibility cleared too
    CHECK(!fov->isVisible(0, 0));
    delete fov;
}

TEST_CASE("map.fov.layerOpaqueGid") {
    auto *mod = Map::create();
    TileLayer *layer = mod->newLayer(7, 3, 8.f, 8.f);
    layer->fill(2);
    for (int y = 0; y < 3; ++y) layer->setTile(3, y, 1);

    Fov *fov = mod->newFov(layer);
    REQUIRE(fov != nullptr);
    fov->setBlockEmpty(false);
    fov->blockOpaqueGid(1);
    fov->setRadiusMetric("chebyshev");
    fov->addRevealer(0, 1, 6);
    fov->compute();
    CHECK(fov->isOpaque(3, 1));
    CHECK(fov->isVisible(3, 1));
    CHECK(!fov->isVisible(5, 1));

    layer->setTile(3, 1, 2);
    fov->syncFromLayer();
    fov->compute();
    CHECK(!fov->isOpaque(3, 1));
    CHECK(fov->isVisible(5, 1));
    delete fov;
}

TEST_CASE("map.fov.removeAndDisable") {
    auto *mod = Map::create();
    Fov *fov = mod->newFovSize(5, 5);
    fov->setBlockEmpty(false);
    const int a = fov->addRevealer(2, 2, 1);
    const int b = fov->addRevealer(0, 0, 1);
    fov->setRevealerEnabled(b, false);
    fov->compute();
    CHECK(fov->isVisible(2, 2));
    CHECK(!fov->isVisible(0, 0));
    fov->removeRevealer(a);
    fov->setRevealerEnabled(b, true);
    fov->compute();
    CHECK(fov->isVisible(0, 0));
    CHECK(!fov->isVisible(2, 2));
    fov->clearRevealers();
    CHECK_EQ(fov->getRevealerCount(), 0);
    delete fov;
}
