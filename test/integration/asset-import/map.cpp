#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "data/DataModule.h"
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
#include "map/DualGrid.h"
#include "map/Fov.h"
#include "map/Map.h"
#include "map/MapObject.h"
#include "map/Pathfinder.h"
#include "map/TileCollision.h"
#include "map/TileConfig.h"
#include "map/TileLayer.h"
#include "map/TileProjection.h"
#include "map/TileSystem.h"
#include "window/Window.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace eve::map;

namespace {
class CollisionSinkMock final : public ITileCollisionSink {
public:
    void replaceTileCollision(const void *source, const TileCollisionRect *rects,
                              size_t count) override {
        layer = source;
        values.assign(rects, rects + count);
    }
    const void *layer = nullptr;
    std::vector<TileCollisionRect> values;
};
}  // namespace

namespace {

[[maybe_unused]] void hideAllTileLayers() {
    if (ecs::current()->getManager<TileLayer>() == nullptr) return;
    auto view = ecs::View<TileLayer, TileLayer::Draw>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [draw] = *it;
        draw->visible = false;
    }
}

}  // namespace


TEST_CASE("map.tileset.loadsExternalTsjRelativeToMap") {
    std::string       error;
    const std::string dataDir = std::string(EVENGINE_SOURCE_DIR) + "/test/data/";
    auto              layers  = loadMapFile(dataDir + "tiled_external_map.json", &error);
    CHECK_EQ(error, std::string());
    REQUIRE(layers.size() == 1);
    TileLayer *layer = layers.front();
    CHECK_EQ(layer->getTile(0, 0), 20);
    CHECK_EQ(layer->getTile(1, 0), 30);
    CHECK_EQ(layer->getTilesetFirstGid(), 20);
    REQUIRE(layer->tileset()->atlases.size() == 2);
    CHECK_EQ(layer->tileset()->atlases[1].firstGid, 30);
    CHECK_EQ(layer->getTileDataType(20, "wet"), std::string("bool"));
    CHECK_EQ(layer->getTileDataType(30, "walkable"), std::string("bool"));
    REQUIRE(layer->tileset()->terrainRules.size() == 1);
    CHECK_EQ(layer->tileset()->terrainRules[0].gid, 20);
    CHECK_EQ(layer->tileset()->terrainRules[0].neighborMask, 1 << 3);
    CHECK_EQ(layer->resource()->texturePath, dataDir + "shore-missing.png");
    REQUIRE(layer->resource()->dependencyPaths.size() == 2);
    CHECK_EQ(layer->resource()->dependencyPaths[0], dataDir + "shore.tsj");
    CHECK_EQ(layer->resource()->dependencyPaths[1], dataDir + "walls.tsj");
    Pathfinder pathfinder(layer);
    CHECK(pathfinder.isWalkable(0, 0));
    CHECK(!pathfinder.isWalkable(1, 0));
    layer->setVisible(false);
}

TEST_CASE("map.tileset.loadsExternalTsxWithMetadataAnimationAndWang") {
    std::string       error;
    const std::string path   = std::string(EVENGINE_SOURCE_DIR) + "/test/data/tiled_external_tsx_map.json";
    auto              layers = loadMapFile(path, &error);
    CHECK_EQ(error, std::string());
    REQUIRE(layers.size() == 1);
    TileLayer *layer = layers.front();
    CHECK_EQ(layer->getTile(0, 0), 40);
    CHECK_EQ(layer->getTilesetFirstGid(), 40);
    CHECK_EQ(layer->getTileAnimationFrameCount(40), 2);
    CHECK_EQ(layer->getTileDataString(40, "terrainTag"), std::string("3"));
    REQUIRE(layer->tileset()->terrainRules.size() == 1);
    CHECK_EQ(layer->tileset()->terrainRules[0].gid, 40);
    CHECK_EQ(layer->tileset()->terrainRules[0].neighborMask, 1 << 1);
    Pathfinder pathfinder(layer);
    CHECK(!pathfinder.isWalkable(0, 0));
    Map map;
    CHECK_EQ(map.publishCollision(layer), 1);
    CHECK_EQ(map.getCollisionRectWidth(0), 8.f);
    layer->setVisible(false);
}


TEST_CASE("map.render.orthoAndIsoPreview") {
    hideAllTileLayers();

    auto *win = eve::window::Window::create();
    auto *gfx = eve::graphics::Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width = 640;
    s.height = 400;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    auto *mod = Map::create();

    // Left: orthogonal concentric rings (solidForGid colors, no atlas needed).
    TileLayer *ortho = mod->newLayer(10, 8, 28.f, 28.f);
    ortho->config()->orientation = MapOrientation::Orthogonal;
    ortho->setOrigin(40.f, 40.f);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 10; ++x) {
            const int ring = std::min({x, y, 9 - x, 7 - y});
            ortho->setTile(x, y, uint32_t(1 + ring));
        }
    }
    ortho->setVisible(true);

    // Right: isometric diamond so projection is visibly different.
    TileLayer *iso = mod->newLayer(6, 6, 48.f, 24.f);
    iso->config()->orientation = MapOrientation::Isometric;
    iso->setOrigin(420.f, 80.f);
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 6; ++x) {
            iso->setTile(x, y, uint32_t(3 + ((x + y) % 4)));
        }
    }
    iso->setVisible(true);

    std::vector<eve::graphics::DrawItem2D> items;
    TileRenderSystem::collect(items);
    REQUIRE(items.size() == 10u * 8u + 6u * 6u);

    gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.12f, 1.f);
    for (int frame = 0; frame < 75; ++frame) {
        ortho->setOrigin(40.f + float(frame) * 0.15f, 40.f);
        iso->setOrigin(420.f, 80.f + float(std::sin(float(frame) * 0.08f)) * 6.f);

        gfx->clearScreen();
        mod->render(gfx);
        gfx->present();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    ortho->setVisible(false);
    iso->setVisible(false);
    win->close();
}
