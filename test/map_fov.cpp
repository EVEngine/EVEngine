#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "map/Map.h"
#include "map/TileLayer.h"
#include "map/Fov.h"
#include "graphics/Graphics.h"
#include "graphics/Texture.h"
#include "window/Window.h"

#include <cstdint>
#include <string>
#include <vector>

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
    CHECK_EQ(fov->getAlgorithm(), std::string("raycast"));
    fov->setAlgorithm("permissive");
    CHECK_EQ(fov->getAlgorithm(), std::string("permissive"));
    fov->setAlgorithm("rectangle");
    CHECK_EQ(fov->getAlgorithm(), std::string("rectangle"));
    fov->setAlgorithm("nope");
    CHECK_EQ(fov->getAlgorithm(), std::string("rectangle"));
    CHECK_EQ(fov->getMode(), std::string("grid2d"));
    CHECK_EQ(fov->getTopology(), std::string("ortho"));
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

TEST_CASE("map.fov.raycast.wallShadow") {
    auto *mod = Map::create();
    Fov *fov = mod->newFovSize(9, 5);
    fov->setAlgorithm("raycast");
    fov->setRadiusMetric("chebyshev");
    fov->setBlockEmpty(false);
    for (int y = 0; y < 5; ++y) fov->setOpaque(4, y, true);
    fov->addRevealer(1, 2, 6);
    fov->compute();
    CHECK(fov->isVisible(1, 2));
    CHECK(fov->isVisible(4, 2));
    CHECK(!fov->isVisible(6, 2));
    delete fov;
}

TEST_CASE("map.fov.permissive.openRadius") {
    auto *mod = Map::create();
    Fov *fov = mod->newFovSize(7, 7);
    fov->setAlgorithm("permissive");
    fov->setRadiusMetric("chebyshev");
    fov->setBlockEmpty(false);
    fov->addRevealer(3, 3, 2);
    fov->compute();
    CHECK(fov->isVisible(3, 3));
    CHECK(fov->isVisible(5, 5));
    CHECK(!fov->isVisible(6, 3));
    delete fov;
}

TEST_CASE("map.fov.heightmap.cliffBlocks") {
    auto *mod = Map::create();
    Fov *fov = mod->newFovSize(9, 3);
    fov->setMode("heightmap");
    fov->setRadiusMetric("chebyshev");
    fov->setBlockEmpty(false);
    fov->setCliffBlock(1.f);
    fov->setEyeOffset(0.f);
    // Flat ground elev 0; cliff column at x=4 with elev 2
    for (int y = 0; y < 3; ++y) fov->setElevation(4, y, 2.f);
    fov->addRevealer(1, 1, 6);
    fov->compute();
    CHECK(fov->isVisible(1, 1));
    CHECK(!fov->isVisible(6, 1));  // behind high cliff
    // Lower cliff should not block with cliffBlock=1 if elev=0.5
    fov->setElevation(4, 1, 0.5f);
    fov->compute();
    CHECK(fov->isVisible(6, 1));
    delete fov;
}

TEST_CASE("map.fov.volume.bridgeLayers") {
    auto *mod = Map::create();
    Fov *fov = mod->newFovVolume(7, 3, 4);
    REQUIRE(fov != nullptr);
    CHECK_EQ(fov->getDepth(), 4);
    CHECK_EQ(fov->getMode(), std::string("volume"));
    fov->setRadiusMetric("chebyshev");
    fov->setVerticalRange(3);
    // Solid slab at z=1 separates upper (z=2) and lower (z=0) spaces.
    for (int x = 0; x < 7; ++x) fov->setOpaque3(x, 1, 1, true);

    fov->addRevealer3(1, 1, 2, 5);  // above slab
    fov->compute();
    CHECK(fov->isVisible3(1, 1, 2));
    CHECK(fov->isVisible3(3, 1, 2));
    CHECK(fov->isVisible3(3, 1, 1));   // slab surface lit
    CHECK(!fov->isVisible3(3, 1, 0));  // below slab blocked

    fov->clearRevealers();
    fov->addRevealer3(1, 1, 0, 5);  // below slab
    fov->compute();
    CHECK(fov->isVisible3(1, 1, 0));
    CHECK(fov->isVisible3(3, 1, 1));
    CHECK(!fov->isVisible3(3, 1, 2));
    delete fov;
}

TEST_CASE("map.fov.volume.verticalExtend") {
    auto *mod = Map::create();
    Fov *fov = mod->newFovVolume(5, 5, 5);
    fov->setRadiusMetric("chebyshev");
    fov->setVerticalRange(2);
    fov->addRevealer3(2, 2, 2, 1);
    fov->compute();
    CHECK(fov->isVisible3(2, 2, 2));
    CHECK(fov->isVisible3(2, 2, 3));
    CHECK(fov->isVisible3(2, 2, 4));
    CHECK(fov->isVisible3(2, 2, 1));
    CHECK(fov->isVisible3(2, 2, 0));
    // Block upward
    fov->setOpaque3(2, 2, 3, true);
    fov->compute();
    CHECK(fov->isVisible3(2, 2, 3));  // opaque cell lit
    CHECK(!fov->isVisible3(2, 2, 4));
    delete fov;
}

TEST_CASE("map.fov.dirty.skipsRecompute") {
    auto *mod = Map::create();
    Fov *fov = mod->newFovSize(5, 5);
    fov->setBlockEmpty(false);
    fov->setRadiusMetric("chebyshev");
    fov->addRevealer(2, 2, 1);
    CHECK(fov->isDirty());
    fov->compute();
    CHECK(!fov->isDirty());
    CHECK(fov->isVisible(2, 2));
    // Second compute without changes is a no-op (still visible).
    fov->compute();
    CHECK(fov->isVisible(2, 2));
    CHECK(!fov->isDirty());
    fov->setOpaque(1, 2, true);
    CHECK(fov->isDirty());
    fov->markDirty();
    CHECK(fov->isDirty());
    delete fov;
}

TEST_CASE("map.fov.maskExport") {
    auto *mod = Map::create();
    Fov *fov = mod->newFovSize(5, 5);
    fov->setBlockEmpty(false);
    fov->setRadiusMetric("chebyshev");
    const int id = fov->addRevealer(2, 2, 1);
    fov->compute();
    CHECK_EQ(fov->getMaskByte(2, 2), 255);
    CHECK(fov->getMaskValue(2, 2) > 0.9f);

    fov->setRevealerPosition(id, 0, 0);
    fov->setRevealerRadius(id, 0);
    fov->compute();
    CHECK_EQ(fov->getMaskByte(2, 2), 89);  // explored ≈ 0.35*255
    CHECK(!fov->isVisible(2, 2));
    CHECK(fov->isExplored(2, 2));

    std::vector<uint8_t> mask;
    CHECK(fov->fillMaskR8(mask));
    CHECK_EQ(int(mask.size()), 25);
    CHECK_EQ(int(mask[size_t(2 + 2 * 5)]), 89);
    delete fov;
}

TEST_CASE("map.fov.hex.topologyRadius") {
    auto *mod = Map::create();
    Fov *fov = mod->newFovSize(11, 11);
    fov->setTopology("hex");
    fov->setBlockEmpty(false);
    CHECK_EQ(fov->getTopology(), std::string("hex"));
    fov->addRevealer(5, 5, 2);
    fov->compute();
    CHECK(fov->isVisible(5, 5));
    // Hex distance 2 neighbors should be visible; far chebyshev corner may not.
    CHECK(fov->isVisible(6, 5));
    const bool nearRing = fov->isVisible(5, 7) || fov->isVisible(6, 6);
    CHECK(nearRing);
    CHECK(!fov->isVisible(0, 0));
    delete fov;
}

TEST_CASE("map.fov.hex.wallBlocksCubeLine") {
    auto *mod = Map::create();
    Fov *fov = mod->newFovSize(9, 5);
    fov->setTopology("hex");
    fov->setAlgorithm("shadowcast");
    fov->setBlockEmpty(false);
    for (int y = 0; y < 5; ++y) fov->setOpaque(4, y, true);
    fov->addRevealer(1, 2, 5);
    fov->compute();
    CHECK(fov->isVisible(1, 2));
    CHECK(fov->isVisible(4, 2));
    CHECK(!fov->isVisible(7, 2));
    delete fov;
}

TEST_CASE("map.fov.rectangle.wallShadow") {
    auto *mod = Map::create();
    Fov *fov = mod->newFovSize(11, 5);
    fov->setAlgorithm("rectangle");
    fov->setRadiusMetric("chebyshev");
    fov->setBlockEmpty(false);
    for (int y = 0; y < 5; ++y) {
        fov->setOpaque(5, y, true);
        fov->setOpaque(6, y, true);  // thick wall merges to a rect
    }
    fov->addRevealer(1, 2, 8);
    fov->compute();
    CHECK(fov->isVisible(1, 2));
    CHECK(fov->isVisible(5, 2));
    CHECK(!fov->isVisible(9, 2));
    delete fov;
}

TEST_CASE("map.fov.perception.canDetect") {
    auto *mod = Map::create();
    Fov *fov = mod->newFovSize(7, 7);
    fov->setBlockEmpty(false);
    fov->setRadiusMetric("chebyshev");
    fov->setPerceptionRadiusScale(1.f);
    const int id = fov->addRevealer(3, 3, 1);
    fov->setRevealerPerception(id, 2.f);  // effective radius 3
    CHECK_EQ(fov->getEffectiveRadius(id), 3);
    fov->compute();
    CHECK(fov->isVisible(3, 6));
    fov->setDetectionMargin(0.f);
    CHECK(fov->canDetect(id, 3, 3, 1.f));   // perception 2 >= stealth 1
    CHECK(!fov->canDetect(id, 3, 3, 3.f));  // stealth too high
    CHECK(!fov->canDetect(id, 0, 0, 0.f));  // not visible
    delete fov;
}

TEST_CASE("map.fov.gpuMaskTexture") {
    auto *win = eve::window::Window::create();
    auto *gfx = eve::graphics::Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings settings;
    settings.width = 4;
    settings.height = 4;
    settings.centered = true;
    REQUIRE(win->setWindowSettings(settings));

    auto *mod = Map::create();
    Fov *fov = mod->newFovSize(4, 4);
    fov->setBlockEmpty(false);
    fov->addRevealer(1, 1, 1);
    fov->compute();
    CHECK(fov->buildMaskTexture(nullptr) == nullptr);

    auto *tex = fov->buildMaskTexture(gfx);
    REQUIRE(tex != nullptr);
    CHECK_EQ(tex->getWidth(), 4);
    CHECK_EQ(tex->getHeight(), 4);
    delete tex;
    delete fov;
    win->close();
}


