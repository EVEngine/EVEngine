#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#include "Fixtures.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include "filesystem/FileData.h"
#include "graphics/Graphics.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "hd2d/Hd2d.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "map/Map.h"
#include "map/TileLayer.h"

using eve::graphics::Color;
using eve::graphics::Graphics;
using eve::graphics::Mesh;
using eve::graphics::Texture;
using eve::hd2d::Hd2D;
using eve::hd2d::Sprite3D;
using eve::hd2d::TileMap3D;
using eve::map::Map;
using eve::map::TileLayer;

namespace {

std::string testOutDir() { return std::string(EVENGINE_TEST_BINARY_DIR) + "/out"; }

void savePng(eve::image::ImageData *frame, const std::string &name) {
    REQUIRE(frame != nullptr);
    [[maybe_unused]] auto *const               imageModule = eve::image::Image::create();
    std::unique_ptr<eve::filesystem::FileData> png(
        frame->encode(eve::image::ImageData::FormatHandler::ENCODED_PNG, name.c_str(), false));
    REQUIRE(png.get() != nullptr);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(testOutDir()), ec);
    std::ofstream out(std::filesystem::path(testOutDir()) / name, std::ios::binary);
    out.write(static_cast<const char *>(png->getData()), std::streamsize(png->getSize()));
    REQUIRE(out.good());
}

void resetScene3D() {
    if (ecs::current()->getManager<eve::graphics::Renderable3D>() != nullptr) {
        auto view = ecs::View<eve::graphics::Renderable3D, eve::graphics::Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [mr] = *it;
            mr->visible = false;
        }
    }
    if (ecs::current()->getManager<eve::graphics::Camera3D>() != nullptr) {
        auto camView = ecs::View<eve::graphics::Camera3D, eve::graphics::Camera3D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data] = *it;
            data->active = false;
        }
    }
}

Texture *makeChecker(Graphics *gfx, int size, int cell) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            uint8_t c = ((x / cell) ^ (y / cell)) & 1 ? 255 : 0;
            size_t i = size_t(y * size + x) * 4;
            px[i] = c;
            px[i + 1] = c;
            px[i + 2] = c;
            px[i + 3] = 255;
        }
    }
    return gfx->newTexture(size, size, px.data(), eve::graphics::TextureCreateInfo::withMipmaps(true));
}

/** @brief Fills a 4x3 layer with two GIDs from a 4-column checker tileset. */
TileLayer *makeTilesetLayer(Graphics *gfx) {
    TileLayer *layer = Map::create()->newLayer(4, 3, 32.f, 32.f);
    Texture *atlas = makeChecker(gfx, 128, 8);
    layer->setTileset(atlas, 1, 4, 0, 0);
    layer->setTilesetTileSize(32, 32);
    for (int ty = 0; ty < 3; ++ty)
        for (int tx = 0; tx < 4; ++tx) layer->setTile(tx, ty, 1 + ((tx + ty) % 4));
    return layer;
}

}  // namespace

TEST_CASE("hd2d.tilemap.buildMeshBoxCounts") {
    GfxFixture fx;
    TileLayer *layer = makeTilesetLayer(fx.gfx);
    TileMap3D t;
    t.setSideDepth(4.f);
    Mesh *mesh = t.buildMesh(fx.gfx, layer);
    REQUIRE(mesh != nullptr);
    // 12 filled tiles, 24 verts + 36 indices per box.
    CHECK_EQ(t.getTileCount(), 12);
    CHECK_EQ(mesh->indexCount, 12 * 36);
    CHECK_EQ(mesh->gpuVertexCount, 12 * 24);
    CHECK(mesh->hasBounds());
}

TEST_CASE("hd2d.tilemap.heightRaisesBoxes") {
    GfxFixture fx;
    TileLayer *layer = makeTilesetLayer(fx.gfx);
    layer->setTileDataNumber(1, "height", 3.f);
    TileMap3D t;
    t.setHeightScale(2.f);
    Mesh *low = t.buildMesh(fx.gfx, layer);
    REQUIRE(low != nullptr);
    CHECK_GT(low->boundsCy, 0.f);
    TileMap3D t2;
    t2.setHeightScale(0.f);
    Mesh *flat = t2.buildMesh(fx.gfx, layer);
    REQUIRE(flat != nullptr);
    CHECK_LT(flat->boundsCy, low->boundsCy);
}

TEST_CASE("hd2d.tilemap.emptyLayerYieldsNullMesh") {
    GfxFixture fx;
    TileLayer *layer = Map::create()->newLayer(3, 3, 32.f, 32.f);
    TileMap3D t;
    CHECK(t.buildMesh(fx.gfx, layer) == nullptr);
    CHECK_EQ(t.getTileCount(), 0);
}

TEST_CASE("hd2d.tilemap.buildRenderableDraws") {
    GfxFixture fx;
    resetScene3D();
    TileLayer *layer = makeTilesetLayer(fx.gfx);
    TileMap3D t;
    t.setSideDepth(4.f);
    auto *renderable = t.buildRenderable(fx.gfx, layer);
    REQUIRE(renderable != nullptr);
    REQUIRE(renderable->getMesh() != nullptr);

    auto *cam = eve::graphics::Camera3D::createCamera();
    cam->setEye(64.f, 40.f, 110.f);
    cam->setTarget(64.f, 0.f, 48.f);
    eve::graphics::RenderSystem3D::setDirectionalLight(0.4f, 1.f, 0.3f, 1.f, 1.f, 1.f);
    fx.gfx->setScreenReadbackEnabled(true);
    fx.gfx->setBackgroundColor(Color(0.06f, 0.08f, 0.10f, 1.f));

    for (int i = 0; i < 3; ++i) {
        fx.gfx->clear(std::nullopt, std::nullopt, std::nullopt);
        eve::graphics::RenderSystem3D::render(*fx.gfx);
        fx.gfx->present();
    }
    auto frame = std::unique_ptr<eve::image::ImageData>(fx.gfx->newImageData());
    REQUIRE(frame.get() != nullptr);
    savePng(frame.get(), "hd2d_tilemap3d.png");
    int changed = 0;
    for (int y = 0; y < frame->getHeight(); y += 2)
        for (int x = 0; x < frame->getWidth(); x += 2) {
            const auto p = frame->getPixel(x, y);
            if (std::fabs(p.r - 0.06f) + std::fabs(p.g - 0.08f) + std::fabs(p.b - 0.10f) > 0.3f)
                ++changed;
        }
    CHECK_GT(changed, 50);
}

TEST_CASE("hd2d.scene.tilemapPlusBillboard") {
    GfxFixture fx;
    resetScene3D();

    // HD-2D ground: a 12x8 tile layer extruded into 3D blocks with checkered tops.
    TileLayer *layer = Map::create()->newLayer(12, 8, 32.f, 32.f);
    Texture *atlas = makeChecker(fx.gfx, 128, 8);
    layer->setTileset(atlas, 1, 4, 0, 0);
    layer->setTilesetTileSize(32, 32);
    for (int ty = 0; ty < 8; ++ty)
        for (int tx = 0; tx < 12; ++tx)
            layer->setTile(tx, ty, 1 + ((tx / 2 + ty / 2) % 4));
    layer->setTileDataNumber(1, "height", 1.f);
    layer->setTileDataNumber(2, "height", 2.f);
    TileMap3D ground;
    ground.setSideDepth(6.f);
    ground.setHeightScale(8.f);
    ground.buildRenderable(fx.gfx, layer);

    // A billboard "character" standing on the terrain.
    auto *hero = Hd2D::create()->newSprite(fx.gfx);
    REQUIRE(hero != nullptr);
    hero->setTexture(makeChecker(fx.gfx, 16, 4));
    hero->setFrameGrid(1, 1);
    hero->setPosition(32.f * 6.f, 16.f, 32.f * 4.f);
    hero->setSize(28.f, 56.f);

    auto *cam = eve::graphics::Camera3D::createCamera();
    cam->setEye(192.f, 40.f, 190.f);
    cam->setTarget(192.f, 0.f, 128.f);
    hero->setCamera(cam);
    eve::graphics::RenderSystem3D::setDirectionalLight(0.4f, 1.f, 0.3f, 1.f, 1.f, 1.f);
    fx.gfx->setScreenReadbackEnabled(true);
    fx.gfx->setBackgroundColor(Color(0.06f, 0.08f, 0.10f, 1.f));

    for (int i = 0; i < 4; ++i) {
        hero->update(0.05f);
        fx.gfx->clear(std::nullopt, std::nullopt, std::nullopt);
        eve::graphics::RenderSystem3D::render(*fx.gfx);
        fx.gfx->present();
    }
    auto frame = std::unique_ptr<eve::image::ImageData>(fx.gfx->newImageData());
    REQUIRE(frame.get() != nullptr);
    savePng(frame.get(), "hd2d_scene_tilemap_plus_billboard.png");
    // Count checker-white pixels (the tilemap tops and the hero billboard).
    int bright = 0;
    for (int y = 0; y < frame->getHeight(); y += 2)
        for (int x = 0; x < frame->getWidth(); x += 2) {
            const auto p = frame->getPixel(x, y);
            if (p.r + p.g + p.b > 1.2f) ++bright;
        }
    CHECK_GT(bright, 100);
}

TEST_CASE("hd2d.sprite.frameGridAnimation") {
    GfxFixture fx;
    auto *sprite = Hd2D::create()->newSprite(fx.gfx);
    REQUIRE(sprite != nullptr);
    sprite->setTexture(makeChecker(fx.gfx, 64, 8));
    sprite->setFrameGrid(4, 2);
    CHECK_EQ(sprite->getFrameCount(), 8);
    CHECK_EQ(sprite->getFrameIndex(), 0);

    sprite->play(2, 5, 4.f);  // 4 frames at 4fps -> 0.25s each
    CHECK(sprite->isPlaying());
    sprite->update(0.75f);    // 3 steps -> frame 5
    CHECK_EQ(sprite->getFrameIndex(), 5);
    sprite->update(0.26f);    // wrap to start of range
    CHECK_EQ(sprite->getFrameIndex(), 2);
    sprite->stop();
    CHECK(!sprite->isPlaying());
    sprite->update(10.f);
    CHECK_EQ(sprite->getFrameIndex(), 2);
    fx.gfx->clear(std::nullopt, std::nullopt, std::nullopt);  // flush GPU ring before teardown
    fx.gfx->present();
}

TEST_CASE("hd2d.sprite.flipSwapsFrameUvs") {
    GfxFixture fx;
    auto *sprite = Hd2D::create()->newSprite(fx.gfx);
    REQUIRE(sprite != nullptr);
    sprite->setTexture(makeChecker(fx.gfx, 16, 4));
    sprite->setFrameGrid(2, 1);
    sprite->setFrameIndex(1);
    float u0, v0, u1, v1;
    sprite->getFrame(u0, v0, u1, v1);
    CHECK(u0 > 0.49f);
    sprite->setFlipX(true);
    sprite->getFrame(u0, v0, u1, v1);
    CHECK(u0 < 0.01f);
    fx.gfx->clear(std::nullopt, std::nullopt, std::nullopt);  // flush GPU ring before teardown
    fx.gfx->present();
}

TEST_CASE("hd2d.sprite.renderAsBillboard") {
    GfxFixture fx;
    resetScene3D();
    auto *sprite = Hd2D::create()->newSprite(fx.gfx);
    REQUIRE(sprite != nullptr);
    sprite->setTexture(makeChecker(fx.gfx, 16, 4));
    sprite->setFrameGrid(2, 2);
    sprite->setPosition(0.f, 0.f, 0.f);
    sprite->setSize(4.f, 4.f);

    auto *cam = eve::graphics::Camera3D::createCamera();
    cam->setEye(0.f, 2.f, 6.f);
    cam->setTarget(0.f, 0.f, 0.f);
    sprite->setCamera(cam);
    eve::graphics::RenderSystem3D::setDirectionalLight(0.4f, 1.f, 0.3f, 1.f, 1.f, 1.f);
    fx.gfx->setScreenReadbackEnabled(true);
    fx.gfx->setBackgroundColor(Color(0.05f, 0.05f, 0.05f, 1.f));

    for (int i = 0; i < 3; ++i) {
        sprite->update(0.05f);
        fx.gfx->clear(std::nullopt, std::nullopt, std::nullopt);
        eve::graphics::RenderSystem3D::render(*fx.gfx);
        fx.gfx->present();
    }
    auto frame = std::unique_ptr<eve::image::ImageData>(fx.gfx->newImageData());
    REQUIRE(frame.get() != nullptr);
    savePng(frame.get(), "hd2d_billboard.png");
    int bright = 0;
    for (int y = 0; y < frame->getHeight(); y += 1)
        for (int x = 0; x < frame->getWidth(); x += 1) {
            const auto p = frame->getPixel(x, y);
            if (p.r + p.g + p.b > 0.6f) ++bright;
        }
    // A camera-facing billboard must be clearly visible: plenty of bright sprite
    // pixels forming a roughly centred quad.
    REQUIRE_GT(bright, 30);
    int hBright = 0;
    for (int x = 0; x < frame->getWidth(); ++x) {
        const auto p = frame->getPixel(x, frame->getHeight() / 2);
        if (p.r + p.g + p.b > 0.6f) ++hBright;
    }
    // A camera-facing quad spans the mid-row as a horizontal band (not a sliver).
    REQUIRE_GT(hBright, 8);
}

TEST_CASE("hd2d.sprite.quadMeshSharedUpload") {
    GfxFixture fx;
    auto *sprite = Hd2D::create()->newSprite(fx.gfx);
    REQUIRE(sprite != nullptr);
    REQUIRE(sprite->quadMesh() != nullptr);
    CHECK_EQ(sprite->quadMesh()->indexCount, 6);
    CHECK_EQ(sprite->quadMesh()->gpuVertexCount, 4);
}
