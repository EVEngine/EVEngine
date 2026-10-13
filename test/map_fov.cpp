#include <SDL2/SDL.h>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "map/FlowField.h"
#include "map/Fov.h"
#include "map/Map.h"
#include "map/Path.h"
#include "map/Pathfinder.h"
#include "map/TileLayer.h"
#include "map/TileOrientation.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// map_fov.cpp
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
    Fov  *fov = mod->newFovSize(4, 4);
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
    Fov  *fov = mod->newFovSize(11, 11);
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
    Fov  *fov = mod->newFovSize(9, 5);
    REQUIRE(fov != nullptr);
    fov->setRadiusMetric("chebyshev");
    fov->setBlockEmpty(false);
    // Vertical wall at x=4, gap none — full column opaque
    for (int y = 0; y < 5; ++y) fov->setOpaque(4, y, true);
    fov->addRevealer(1, 2, 6);
    fov->compute();
    CHECK(fov->isVisible(1, 2));
    CHECK(fov->isVisible(4, 2));   // blocking cell itself is visible
    CHECK(!fov->isVisible(6, 2));  // behind wall
    CHECK(!fov->isVisible(7, 2));
    delete fov;
}

TEST_CASE("map.fov.corridorAndRoom") {
    auto *mod = Map::create();
    Fov  *fov = mod->newFovSize(7, 5);
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
    Fov  *fov = mod->newFovSize(11, 11);
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
    Fov  *fov = mod->newFovSize(15, 5);
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
    Fov  *fov = mod->newFovSize(9, 9);
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

TEST_CASE("map.fov.snapshotRestoresExactVisibilityMemory") {
    Fov fov(7, 7);
    fov.setBlockEmpty(false);
    fov.setRadiusMetric("chebyshev");
    const int revealer = fov.addRevealer(2, 2, 1);
    fov.compute();
    fov.setRevealerPosition(revealer, 5, 5);
    fov.compute();

    const auto saved = fov.snapshot();
    CHECK(fov.isExplored(2, 2));
    CHECK(!fov.isVisible(2, 2));
    CHECK(fov.isVisible(5, 5));
    CHECK(!fov.isExplored(0, 6));

    fov.clearMemory();
    CHECK(!fov.isExplored(2, 2));
    REQUIRE(fov.restore(saved).ok());
    CHECK(fov.isExplored(2, 2));
    CHECK(!fov.isVisible(2, 2));
    CHECK(fov.isVisible(5, 5));
    CHECK(!fov.isExplored(0, 6));

    auto invalid = saved;
    invalid.width += 1;
    CHECK(!fov.restore(invalid).ok());
}

TEST_CASE("map.fov.layerOpaqueGid") {
    auto      *mod   = Map::create();
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

TEST_CASE("map.fov.boundLayerAutoSyncsRevision") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(7, 1, 8.f, 8.f);
    layer->fill(2);
    Fov *fov = mod->newFov(layer);
    fov->setBlockEmpty(false);
    fov->blockOpaqueGid(1);
    fov->addRevealer(0, 0, 7);
    fov->compute();
    CHECK(fov->isVisible(6, 0));
    layer->setTile(3, 0, 1);
    fov->compute();
    CHECK(!fov->isVisible(6, 0));
    delete fov;
}

TEST_CASE("map.fov.removeAndDisable") {
    auto *mod = Map::create();
    Fov  *fov = mod->newFovSize(5, 5);
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
    Fov  *fov = mod->newFovSize(9, 5);
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
    Fov  *fov = mod->newFovSize(7, 7);
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
    Fov  *fov = mod->newFovSize(9, 3);
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
    Fov  *fov = mod->newFovVolume(7, 3, 4);
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
    Fov  *fov = mod->newFovVolume(5, 5, 5);
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
    Fov  *fov = mod->newFovSize(5, 5);
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
    Fov  *fov = mod->newFovSize(5, 5);
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
    Fov  *fov = mod->newFovSize(11, 11);
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
    Fov  *fov = mod->newFovSize(9, 5);
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
    Fov  *fov = mod->newFovSize(11, 5);
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
    Fov  *fov = mod->newFovSize(7, 7);
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
    eve::window::WindowSettings settings;
    settings.width    = 4;
    settings.height   = 4;
    settings.centered = true;
    REQUIRE(win->setWindowSettings(settings));

    auto *mod = Map::create();
    Fov  *fov = mod->newFovSize(4, 4);
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

TEST_CASE("map.fov.accessorRoundTrips") {
    auto *mod = Map::create();
    Fov  *fov = mod->newFovSize(4, 4);
    REQUIRE(fov != nullptr);

    fov->setCornerPeek(true);
    CHECK(fov->getCornerPeek());
    fov->setCornerPeek(false);
    CHECK(!fov->getCornerPeek());

    fov->setEyeOffset(1.f);
    CHECK_EQ(fov->getEyeOffset(), 1.f);

    fov->setCliffBlock(0.5f);
    CHECK_EQ(fov->getCliffBlock(), 0.5f);

    fov->setVerticalRange(3);
    CHECK_EQ(fov->getVerticalRange(), 3);

    fov->setPerceptionRadiusScale(1.5f);
    CHECK_EQ(fov->getPerceptionRadiusScale(), 1.5f);

    const int id = fov->addRevealer(1, 1, 2);
    REQUIRE(id >= 0);
    fov->setRevealerPosition3(id, 2, 3, 1);
    fov->setRevealerRadius(id, 4);
    fov->setRevealerPerception(id, 2.f);
    CHECK_EQ(fov->getRevealerPerception(id), 2.f);
    CHECK_EQ(fov->getRevealerCount(), 1);
    fov->setRevealerEnabled(id, false);
    CHECK_EQ(fov->getRevealerCount(), 1);            // disabled revealers stay registered
    CHECK_EQ(fov->getRevealerPerception(999), 0.f);  // unknown id

    delete fov;
}

// map_path.cpp
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::map;
using namespace eve::graphics;

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
    auto       *mod = Map::create();
    Pathfinder *pf  = mod->newPathfinderSize(2, 2);
    pf->setTopology("ortho4");
    CHECK_EQ(pf->getTopology(), std::string("ortho4"));
    pf->setTopology("ortho8");
    CHECK_EQ(pf->getTopology(), std::string("ortho8"));
    pf->setTopology("hex");
    CHECK_EQ(pf->getTopology(), std::string("hex"));
    pf->setTopology("nope");  // fallback keeps last valid (hex)
    CHECK_EQ(pf->getTopology(), std::string("hex"));
    delete pf;
}

TEST_CASE("map.path.astar.orthoBypassWall") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(8, 5, 16.f, 16.f);
    fillOpen(layer);
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
    bool viaGap = false;
    for (int i = 0; i < path->getLength(); ++i) {
        if (path->getX(i) == 3 && path->getY(i) == 2) viaGap = true;
    }
    CHECK(viaGap);
    delete path;
    delete pf;
}

TEST_CASE("map.path.astar.unreachable") {
    auto      *mod   = Map::create();
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

TEST_CASE("map.path.layerMetadataIsNavigationAuthority") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(3, 2, 8.f, 8.f);
    layer->fill(2);
    layer->setTile(1, 0, 9);
    layer->setTileMetadata(9, 1, 1, false, 7.f);

    Pathfinder *pf = mod->newPathfinder(layer);
    REQUIRE(pf != nullptr);
    CHECK(!pf->isWalkable(1, 0));

    Path *detour = pf->findPath(0, 0, 2, 0);
    REQUIRE(detour != nullptr);
    CHECK(pathEndsAt(detour, 2, 0));
    CHECK(detour->getLength() == 5);
    delete detour;

    // Metadata mutation publishes a layer revision, so an already-bound
    // pathfinder observes the new canonical navigation state automatically.
    layer->setTileMetadata(9, 1, 1, true, 7.f);
    CHECK(pf->isWalkable(1, 0));
    CHECK_EQ(pf->getCellCost(1, 0), 7.f);
    delete pf;
}

TEST_CASE("map.path.directionalNavigationUsesTileProfiles") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(3, 1, 8.f, 8.f);
    REQUIRE(layer->applyConfig(R"({
      "width":3,"height":1,"data":[2,5,2],
      "tileset":{"firstgid":1,"columns":5,"tilewidth":8,"tileheight":8,
        "tiles":[{"id":4,"properties":[
          {"name":"exitMask","type":"int","value":2}
        ]}]}
    })"));

    Pathfinder pathfinder(layer);
    Path      *east = pathfinder.findPath(0, 0, 2, 0);
    REQUIRE(east != nullptr);
    CHECK(pathEndsAt(east, 2, 0));
    delete east;

    Path *west = pathfinder.findPath(2, 0, 0, 0);
    REQUIRE(west != nullptr);
    CHECK_EQ(west->getLength(), 0);
    delete west;
}

TEST_CASE("map.path.astar.ortho8NoCornerCut") {
    auto      *mod   = Map::create();
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
    CHECK(path->getLength() >= 5);
    delete path;
    delete pf;
}

TEST_CASE("map.path.customGrid") {
    auto       *mod = Map::create();
    Pathfinder *pf  = mod->newPathfinderSize(4, 4);
    REQUIRE(pf != nullptr);
    pf->setTopology("ortho4");
    pf->setBlocked(1, 0, true);
    pf->setBlocked(1, 1, true);
    pf->setBlocked(1, 2, true);
    Path *blocked = pf->findPath(0, 1, 3, 1);
    CHECK_EQ(blocked->getLength(), 0);
    delete blocked;

    pf->setBlocked(1, 1, false);
    Path *path = pf->findPath(0, 1, 3, 1);
    CHECK(pathStartsAt(path, 0, 1));
    CHECK(pathEndsAt(path, 3, 1));
    delete path;
    delete pf;
}

TEST_CASE("map.path.flowField.groupSameGoal") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(10, 8, 16.f, 16.f);
    fillOpen(layer);
    wallRect(layer, 4, 0, 4, 5);
    layer->setTile(4, 6, 2);

    Pathfinder *pf = mod->newPathfinder(layer);
    pf->blockGid(1);
    pf->setTopology("ortho4");

    const int  gx = 9, gy = 1;
    FlowField *field = pf->buildFlowField(gx, gy);
    REQUIRE(field != nullptr);
    CHECK_EQ(field->getGoalX(), gx);
    CHECK_EQ(field->getGoalY(), gy);
    CHECK(field->isReachable(0, 0));
    CHECK(field->isReachable(1, 7));

    int starts[][2] = {{0, 0}, {0, 7}, {2, 3}};
    for (auto &s : starts) {
        Path *p = pf->followFlow(field, s[0], s[1]);
        CHECK(pathStartsAt(p, s[0], s[1]));
        CHECK(pathEndsAt(p, gx, gy));
        CHECK(pathWalkable(p, pf));
        delete p;
    }

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
    auto       *mod = Map::create();
    Pathfinder *pf  = mod->newPathfinderSize(6, 6);
    pf->setTopology("ortho4");
    for (int y = 0; y < 5; ++y) pf->setBlocked(3, y, true);

    Path *a = pf->findPath(0, 0, 5, 0);
    Path *b = pf->findGroupPath(0, 0, 5, 0);
    CHECK_EQ(a->getLength(), 0);
    CHECK_EQ(b->getLength(), 0);
    delete a;
    delete b;

    pf->setBlocked(3, 5, false);
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
    auto      *mod                = Map::create();
    TileLayer *layer              = mod->newLayer(5, 5, 64.f, 32.f);
    layer->config()->orientation  = MapOrientation::Staggered;
    layer->config()->staggerAxis  = StaggerAxis::Y;
    layer->config()->staggerIndex = StaggerIndex::Odd;
    fillOpen(layer);

    Pathfinder *pf = mod->newPathfinder(layer);
    pf->setTopology("auto");
    CHECK_EQ(pf->getTopology(), std::string("hex"));

    Path *path = pf->findPath(0, 0, 4, 4);
    REQUIRE(path != nullptr);
    CHECK(path->getLength() > 0);
    CHECK(pathEndsAt(path, 4, 4));
    // Consecutive steps must be adjacent (chebyshev distance 1 in offset space is not
    // enough for hex; verify each step changes coords and stays walkable).
    for (int i = 1; i < path->getLength(); ++i) {
        const int x0 = path->getX(i - 1), y0 = path->getY(i - 1);
        const int x1 = path->getX(i), y1 = path->getY(i);
        const int adx = x1 > x0 ? x1 - x0 : x0 - x1;
        const int ady = y1 > y0 ? y1 - y0 : y0 - y1;
        CHECK(adx + ady > 0);
        CHECK(adx <= 1);
        CHECK(ady <= 1);
        CHECK(pf->isWalkable(x1, y1));
    }
    delete path;
    delete pf;
}

TEST_CASE("map.path.isometric.autoUsesOrtho") {
    auto      *mod               = Map::create();
    TileLayer *layer             = mod->newLayer(4, 4, 64.f, 32.f);
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
    auto      *mod   = Map::create();
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
    auto       *mod = Map::create();
    Pathfinder *pf  = mod->newPathfinderSize(3, 3);
    pf->setTopology("ortho4");
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

TEST_CASE("map.path.astar.routePreview") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 640;
    s.height   = 400;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    const int  mw    = 16;
    const int  mh    = 10;
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(mw, mh, 16.f, 16.f);
    fillOpen(layer, 2);
    // Vertical wall with a gap, plus a few scattered blocks.
    for (int y = 0; y < mh; ++y) layer->setTile(6, y, 1);
    layer->setTile(6, 4, 2);
    layer->setTile(6, 5, 2);
    wallRect(layer, 10, 1, 10, 7, 1);
    layer->setTile(10, 3, 2);
    layer->setTile(3, 2, 1);
    layer->setTile(3, 7, 1);
    layer->setTile(12, 8, 1);

    Pathfinder *pf = mod->newPathfinder(layer);
    REQUIRE(pf != nullptr);
    pf->blockGid(1);
    pf->setTopology("ortho4");

    Path *path = pf->findPath(1, 4, 14, 4);
    REQUIRE(path != nullptr);
    REQUIRE(path->getLength() > 0);
    CHECK(pathWalkable(path, pf));

    const float cell = 28.f;
    const float ox   = (float(gfx->getWidth()) - float(mw) * cell) * 0.5f;
    const float oy   = (float(gfx->getHeight()) - float(mh) * cell) * 0.5f;

    gfx->setBackgroundColorRGBA(0.07f, 0.08f, 0.11f, 1.f);
    int markerAt = 0;

    for (int frame = 0; frame < 100; ++frame) {
        gfx->clearScreen();
        for (int y = 0; y < mh; ++y) {
            for (int x = 0; x < mw; ++x) {
                const int gid = layer->getTile(x, y);
                Color     c   = (gid == 1) ? Color(0.25f, 0.28f, 0.35f, 1.f) : Color(0.55f, 0.58f, 0.48f, 1.f);
                gfx->drawSolidRect(ox + float(x) * cell, oy + float(y) * cell, cell - 1.f, cell - 1.f, c);
            }
        }

        // Draw full path corridor.
        for (int i = 0; i < path->getLength(); ++i) {
            const float px = ox + float(path->getX(i)) * cell + 6.f;
            const float py = oy + float(path->getY(i)) * cell + 6.f;
            gfx->drawSolidRect(px, py, cell - 12.f, cell - 12.f, Color(0.35f, 0.75f, 1.f, 1.f));
        }

        // Animate a runner along the path.
        markerAt       = (frame / 2) % path->getLength();
        const float mx = ox + float(path->getX(markerAt)) * cell + 4.f;
        const float my = oy + float(path->getY(markerAt)) * cell + 4.f;
        gfx->drawSolidRect(mx, my, cell - 8.f, cell - 8.f, Color(1.f, 0.45f, 0.25f, 1.f));

        // Start / goal markers.
        gfx->drawSolidRect(ox + 1.f * cell + 2.f, oy + 4.f * cell + 2.f, cell - 4.f, cell - 4.f,
                           Color(0.4f, 0.95f, 0.45f, 1.f));
        gfx->drawSolidRect(ox + 14.f * cell + 2.f, oy + 4.f * cell + 2.f, cell - 4.f, cell - 4.f,
                           Color(0.95f, 0.85f, 0.3f, 1.f));

        gfx->present();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    CHECK_GT(path->getLength(), 8);
    delete path;
    delete pf;
    win->close();
}

TEST_CASE("map.path.accessorRoundTrips") {
    auto       *mod = Map::create();
    Pathfinder *pf  = mod->newPathfinderSize(4, 4);
    REQUIRE(pf != nullptr);

    pf->setDiagonal(true);
    CHECK(pf->getDiagonal());
    pf->setDiagonal(false);
    CHECK(!pf->getDiagonal());

    pf->setBlockEmpty(true);
    CHECK(pf->getBlockEmpty());
    pf->setBlockEmpty(false);
    CHECK(!pf->getBlockEmpty());

    pf->setCellCost(1, 1, 2.5f);
    CHECK_EQ(pf->getCellCost(1, 1), 2.5f);
    CHECK_EQ(pf->getCellCost(9, 9), 0.f);  // out of bounds

    pf->blockGid(7);
    pf->setBlocked(0, 0, true);
    pf->unblockGid(7);
    pf->clearBlockedGids();
    pf->invalidateCache();
    // setBlocked cells stay blocked until explicitly cleared; gid blocking is gone.
    CHECK(!pf->isWalkable(0, 0));
    pf->setBlocked(0, 0, false);
    CHECK(pf->isWalkable(0, 0));

    delete pf;
}

TEST_CASE("map.path.boundLayerAutoSyncsRevision") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(5, 1, 8.f, 8.f);
    layer->fill(2);
    Pathfinder *pf = mod->newPathfinder(layer);
    pf->blockGid(1);
    Path *initial = pf->findPath(0, 0, 4, 0);
    CHECK_GT(initial->getLength(), 0);
    delete initial;
    layer->setTile(2, 0, 1);
    Path *blocked = pf->findPath(0, 0, 4, 0);
    CHECK_EQ(blocked->getLength(), 0);
    delete blocked;
    layer->setTile(2, 0, 2);
    Path *open = pf->findPath(0, 0, 4, 0);
    CHECK_GT(open->getLength(), 0);
    delete open;
    delete pf;
}

TEST_CASE("map.path.dynamicEntryPenaltyDetoursWithoutMutatingGrid") {
    Pathfinder            pathfinder(5, 3);
    std::unique_ptr<Path> direct(pathfinder.findPath(0, 1, 4, 1));
    REQUIRE(direct.get() != nullptr);
    CHECK_EQ(direct->getLength(), 5);

    std::unique_ptr<Path> detour(
        pathfinder.findPath(0, 1, 4, 1, [](int x, int y) { return x == 2 && y == 1 ? 20.0f : 0.0f; }));
    REQUIRE(detour.get() != nullptr);
    CHECK_GT(detour->getLength(), direct->getLength());
    for (int index = 0; index < detour->getLength(); ++index)
        CHECK(!(detour->getX(index) == 2 && detour->getY(index) == 1));
    CHECK_EQ(pathfinder.getCellCost(2, 1), 1.0f);
}
