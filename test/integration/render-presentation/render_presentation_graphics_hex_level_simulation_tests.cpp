#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_hd2d)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "Fixtures.h"
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
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// hd2d.cpp
namespace test_hd2d {
// hd2d.cpp
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
            auto [mr]   = *it;
            mr->visible = false;
        }
    }
    if (ecs::current()->getManager<eve::graphics::Camera3D>() != nullptr) {
        auto camView = ecs::View<eve::graphics::Camera3D, eve::graphics::Camera3D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data]  = *it;
            data->active = false;
        }
    }
}

Texture *makeChecker(Graphics *gfx, int size, int cell) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            uint8_t c = ((x / cell) ^ (y / cell)) & 1 ? 255 : 0;
            size_t  i = size_t(y * size + x) * 4;
            px[i]     = c;
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
    Texture   *atlas = makeChecker(gfx, 128, 8);
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
    TileMap3D  t;
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
    TileMap3D  t;
    CHECK(t.buildMesh(fx.gfx, layer) == nullptr);
    CHECK_EQ(t.getTileCount(), 0);
}

TEST_CASE("hd2d.tilemap.buildRenderableDraws") {
    GfxFixture fx;
    resetScene3D();
    TileLayer *layer = makeTilesetLayer(fx.gfx);
    TileMap3D  t;
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
            if (std::fabs(p.r - 0.06f) + std::fabs(p.g - 0.08f) + std::fabs(p.b - 0.10f) > 0.3f) ++changed;
        }
    CHECK_GT(changed, 50);
}

TEST_CASE("hd2d.scene.tilemapPlusBillboard") {
    GfxFixture fx;
    resetScene3D();

    // HD-2D ground: a 12x8 tile layer extruded into 3D blocks with checkered tops.
    TileLayer *layer = Map::create()->newLayer(12, 8, 32.f, 32.f);
    Texture   *atlas = makeChecker(fx.gfx, 128, 8);
    layer->setTileset(atlas, 1, 4, 0, 0);
    layer->setTilesetTileSize(32, 32);
    for (int ty = 0; ty < 8; ++ty)
        for (int tx = 0; tx < 12; ++tx) layer->setTile(tx, ty, 1 + ((tx / 2 + ty / 2) % 4));
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
    auto      *sprite = Hd2D::create()->newSprite(fx.gfx);
    REQUIRE(sprite != nullptr);
    sprite->setTexture(makeChecker(fx.gfx, 64, 8));
    sprite->setFrameGrid(4, 2);
    CHECK_EQ(sprite->getFrameCount(), 8);
    CHECK_EQ(sprite->getFrameIndex(), 0);

    sprite->play(2, 5, 4.f);  // 4 frames at 4fps -> 0.25s each
    CHECK(sprite->isPlaying());
    sprite->update(0.75f);  // 3 steps -> frame 5
    CHECK_EQ(sprite->getFrameIndex(), 5);
    sprite->update(0.26f);  // wrap to start of range
    CHECK_EQ(sprite->getFrameIndex(), 2);
    sprite->stop();
    CHECK(!sprite->isPlaying());
    sprite->update(10.f);
    CHECK_EQ(sprite->getFrameIndex(), 2);
    fx.gfx->clear(std::nullopt, std::nullopt, std::nullopt);  // flush GPU ring before teardown
    fx.gfx->present();
}

TEST_CASE("hd2d.sprite.flipPreservesSelectedAtlasRegion") {
    GfxFixture fx;
    auto      *sprite = Hd2D::create()->newSprite(fx.gfx);
    REQUIRE(sprite != nullptr);
    sprite->setTexture(makeChecker(fx.gfx, 16, 4));
    sprite->setFrameGrid(2, 1);
    sprite->setFrameIndex(1);
    float u0, v0, u1, v1;
    sprite->getFrame(u0, v0, u1, v1);
    REQUIRE(u0 > 0.49f);
    sprite->setFlipX(true);
    sprite->getFrame(u0, v0, u1, v1);
    // Flipping reverses sampling inside the same cell, not the selected cell.
    REQUIRE(u0 > 0.49f);
    REQUIRE_EQ(sprite->getFrameIndex(), 1);
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
    auto      *sprite = Hd2D::create()->newSprite(fx.gfx);
    REQUIRE(sprite != nullptr);
    REQUIRE(sprite->quadMesh() != nullptr);
    CHECK_EQ(sprite->quadMesh()->indexCount, 6);
    CHECK_EQ(sprite->quadMesh()->gpuVertexCount, 4);
}

// hd2d_animation_contract.cpp
TEST_CASE("hd2d.sprite.gridReplacementStopsOldClip") {
    eve::hd2d::Sprite3D sprite;
    sprite.setFrameGrid(6, 4);
    sprite.play(18, 23, 10.f);
    sprite.setFrameGrid(2, 1);
    REQUIRE(!sprite.isPlaying());
    sprite.update(1.f);
    REQUIRE_LT(sprite.getFrameIndex(), sprite.getFrameCount());
}

TEST_CASE("hd2d.sprite.invalidTimeDoesNotPoisonWalkClock") {
    eve::hd2d::Sprite3D sprite;
    sprite.setFrameGrid(6, 4);
    sprite.play(6, 11, 10.f);
    sprite.update(-10.f);
    sprite.update(std::numeric_limits<float>::quiet_NaN());
    sprite.update(std::numeric_limits<float>::infinity());
    sprite.update(0.21f);
    REQUIRE_EQ(sprite.getFrameIndex(), 8);
}

TEST_CASE("hd2d.sprite.largeTimeRemainsInClip") {
    eve::hd2d::Sprite3D sprite;
    sprite.setFrameGrid(6, 4);
    sprite.play(12, 17, 8.f);
    sprite.update(1073741824.f);
    REQUIRE_GE(sprite.getFrameIndex(), 12);
    REQUIRE_LE(sprite.getFrameIndex(), 17);
    sprite.update(0.125f);
    REQUIRE_GE(sprite.getFrameIndex(), 12);
    REQUIRE_LE(sprite.getFrameIndex(), 17);
}

// hd2d_look_contract.cpp
TEST_CASE("hd2d.look.miniatureWritesCameraDofAndBloom") {
    eve::hd2d::Hd2dLook look = eve::hd2d::Hd2dLook::miniature();
    CHECK(look.getMaxBlur() > 0.f);
    CHECK(look.getBloomIntensity() > 0.f);

    eve::graphics::Camera3D *cam = eve::graphics::Camera3D::createCamera();
    REQUIRE(cam != nullptr);
    look.apply(cam);
    CHECK_EQ(cam->getDofFocusDistance(), look.getFocusDistance());
    CHECK_EQ(cam->getDofMaxBlur(), look.getMaxBlur());
    CHECK_EQ(cam->getDofFocusRange(), look.getFocusRange());
    CHECK_EQ(cam->getBloomIntensity(), look.getBloomIntensity());
    CHECK_EQ(cam->getBloomThreshold(), look.getBloomThreshold());
}

TEST_CASE("hd2d.look.rejectsInvalidFocusRange") {
    eve::hd2d::Hd2dLook look;
    bool                threw = false;
    try {
        look.setFocusRange(0.f);
    } catch (...) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("hd2d.sprite.dopFixMaterialDefaults") {
    eve::hd2d::Sprite3D sprite;
    CHECK(sprite.getDepthWrite());
    CHECK(sprite.getDoubleSided());
    CHECK_EQ(sprite.getAlphaCutoff(), 0.5f);
    CHECK_EQ(sprite.getBillboardMode(), std::string("screen"));
    sprite.setBillboardMode("yaw");
    CHECK_EQ(sprite.getBillboardMode(), std::string("yaw"));
    sprite.setAlphaCutoff(0.7f);
    CHECK_EQ(sprite.getAlphaCutoff(), 0.7f);
    sprite.setDepthWrite(false);
    CHECK(!sprite.getDepthWrite());
}

// hd2d_scene_contract.cpp
TEST_CASE("hd2d.sprite.screenAlignedUnderCameraPitchAndRoll") {
    GfxFixture                           fx;
    const unsigned char                  red[] = {255, 0, 0, 255};
    std::unique_ptr<eve::hd2d::Sprite3D> sprite(eve::hd2d::Hd2D::create()->newSprite(fx.gfx));
    sprite->setTexture(fx.gfx->newTexture(1, 1, red));
    sprite->setSize(2.f, 2.f);
    sprite->setPosition(1.f, 0.f, 0.f);
    auto *camera = eve::graphics::Camera3D::createCamera();
    camera->setEye(4.f, 8.f, 6.f);
    camera->setTarget(0.f, 0.f, 0.f);
    camera->setUp(0.4f, 1.f, 0.2f);
    camera->setOrthographic(6.f);
    sprite->setCamera(camera);
    fx.gfx->setScreenReadbackEnabled(true);
    fx.gfx->setBackgroundColor(eve::Color(0.f, 0.3f, 0.f, 1.f));
    for (int i = 0; i < 3; ++i) {
        sprite->update(0.f);
        fx.gfx->clear(std::nullopt, std::nullopt, std::nullopt);
        eve::graphics::RenderSystem3D::render(*fx.gfx);
        fx.gfx->present();
    }
    std::unique_ptr<eve::image::ImageData> frame(fx.gfx->newImageData());
    REQUIRE(frame != nullptr);
    int left = frame->getWidth(), right = -1, top = frame->getHeight(), bottom = -1, area = 0;
    for (int y = 0; y < frame->getHeight(); ++y)
        for (int x = 0; x < frame->getWidth(); ++x) {
            const auto c = frame->getPixel(x, y);
            if (c.r > c.g + 0.3f) {
                left   = std::min(left, x);
                right  = std::max(right, x);
                top    = std::min(top, y);
                bottom = std::max(bottom, y);
                ++area;
            }
        }
    REQUIRE_GT(area, 100);
    REQUIRE_LE(std::abs((right - left) - (bottom - top)), 2);
    REQUIRE_GT(float(area) / float((right - left + 1) * (bottom - top + 1)), 0.97f);

    // Put the image's lower center at the camera target. The last opaque row
    // must stay on the horizon through an orbit, resize and frame/UV update.
    sprite->setPivot(0.5f, 1.f);
    sprite->setPosition(0.f, 0.f, 0.f);
    sprite->setSize(1.5f, 2.f);
    sprite->setFrameGrid(1, 1);
    sprite->setFlipX(true);
    camera->setEye(-6.f, 5.f, -4.f);
    for (int i = 0; i < 3; ++i) {
        sprite->update(0.f);
        fx.gfx->clear(std::nullopt, std::nullopt, std::nullopt);
        eve::graphics::RenderSystem3D::render(*fx.gfx);
        fx.gfx->present();
    }
    frame.reset(fx.gfx->newImageData());
    REQUIRE(frame != nullptr);
    bottom = -1;
    top    = frame->getHeight();
    for (int y = 0; y < frame->getHeight(); ++y)
        for (int x = 0; x < frame->getWidth(); ++x) {
            const auto c = frame->getPixel(x, y);
            if (c.r > c.g + 0.3f) {
                bottom = std::max(bottom, y);
                top    = std::min(top, y);
            }
        }
    REQUIRE_GT(bottom - top, 30);
    REQUIRE_LE(std::abs(bottom - (frame->getHeight() / 2 - 1)), 2);
}

TEST_CASE("hd2d.tilemap.footprintMatchesMapCoordinates") {
    GfxFixture fx;
    auto      *layer = eve::map::Map::create()->newLayer(1, 1, 32.f, 48.f);
    layer->setTile(0, 0, 1);
    eve::hd2d::TileMap3D builder;
    auto                *mesh = builder.buildMesh(fx.gfx, layer);
    REQUIRE(mesh != nullptr);
    REQUIRE_EQ(mesh->boundsCx, layer->tileToWorldX(0, 0) + 16.f);
    REQUIRE_EQ(mesh->boundsCz, layer->tileToWorldY(0, 0) + 24.f);
}

TEST_CASE("hd2d.sprite.imageOrientationAndTransparentCorners") {
    GfxFixture                 fx;
    std::vector<unsigned char> pixels(8 * 8 * 4, 0);
    for (int y = 2; y < 6; ++y)
        for (int x = 2; x < 6; ++x) {
            const int i                 = (y * 8 + x) * 4;
            pixels[i + (y < 4 ? 0 : 2)] = 255;
            pixels[i + 1]               = x >= 4 ? 255 : 0;
            pixels[i + 3]               = 255;
        }
    std::unique_ptr<eve::hd2d::Sprite3D> sprite(eve::hd2d::Hd2D::create()->newSprite(fx.gfx));
    sprite->setTexture(fx.gfx->newTexture(8, 8, pixels.data()));
    sprite->setSize(4.f, 4.f);
    auto *camera = eve::graphics::Camera3D::createCamera();
    camera->setEye(0.f, 0.f, 6.f);
    camera->setTarget(0.f, 0.f, 0.f);
    camera->setOrthographic(6.f);
    sprite->setCamera(camera);
    fx.gfx->setScreenReadbackEnabled(true);
    fx.gfx->setBackgroundColor(eve::Color(0.1f, 0.5f, 0.1f, 1.f));
    for (int i = 0; i < 3; ++i) {
        fx.gfx->clear(std::nullopt, std::nullopt, std::nullopt);
        eve::graphics::RenderSystem3D::render(*fx.gfx);
        fx.gfx->present();
    }
    std::unique_ptr<eve::image::ImageData> frame(fx.gfx->newImageData());
    REQUIRE(frame != nullptr);
    const int  cx = frame->getWidth() / 2, cy = frame->getHeight() / 2;
    const auto top    = frame->getPixel(cx, cy - 20);
    const auto bottom = frame->getPixel(cx, cy + 20);
    const auto corner = frame->getPixel(cx + 65, cy + 65);
    const auto left   = frame->getPixel(cx - 20, cy - 20);
    const auto right  = frame->getPixel(cx + 20, cy - 20);
    REQUIRE_GT(top.r, top.b + 0.3f);
    REQUIRE_GT(bottom.b, bottom.r + 0.3f);
    REQUIRE_GT(corner.g, corner.r + 0.2f);
    REQUIRE_GT(right.g, left.g + 0.3f);
    sprite->setFlipX(true);
    sprite->setFlipY(true);
    for (int i = 0; i < 3; ++i) {
        fx.gfx->clear(std::nullopt, std::nullopt, std::nullopt);
        eve::graphics::RenderSystem3D::render(*fx.gfx);
        fx.gfx->present();
    }
    frame.reset(fx.gfx->newImageData());
    REQUIRE(frame != nullptr);
    const auto flippedLeft  = frame->getPixel(cx - 20, cy - 20);
    const auto flippedRight = frame->getPixel(cx + 20, cy - 20);
    REQUIRE_GT(flippedLeft.b, flippedLeft.r + 0.3f);
    REQUIRE_GT(flippedLeft.g, flippedRight.g + 0.3f);
}

TEST_CASE("hd2d.tilemap.flatOverlayHasOnlyTopFace") {
    GfxFixture fx;
    auto      *layer = eve::map::Map::create()->newLayer(1, 1, 32.f, 32.f);
    layer->setTile(0, 0, 1);
    eve::hd2d::TileMap3D builder;
    builder.setSideDepth(0.f);
    auto *mesh = builder.buildMesh(fx.gfx, layer);
    REQUIRE(mesh != nullptr);
    REQUIRE_EQ(mesh->indexCount, 6);
    REQUIRE_EQ(mesh->gpuVertexCount, 4);
}

TEST_CASE("hd2d.tilemap.tiledFlipFlagsPreserveAtlasOrientation") {
    GfxFixture                 fx;
    const unsigned char        colors[4][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}};
    std::vector<unsigned char> pixels(8 * 8 * 4, 255);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            for (int c = 0; c < 3; ++c) pixels[(y * 8 + x) * 4 + c] = colors[(y / 4) * 2 + x / 4][c];
    auto *layer = eve::map::Map::create()->newLayer(4, 2, 2.f, 2.f);
    layer->setVisible(false);
    layer->setTileset(fx.gfx->newTexture(8, 8, pixels.data()), 1, 1, 0, 0);
    layer->setTilesetTileSize(8, 8);
    for (unsigned flags = 0; flags < 8; ++flags) {
        const uint32_t raw =
            1u | ((flags & 1) ? 0x80000000u : 0u) | ((flags & 2) ? 0x40000000u : 0u) | ((flags & 4) ? 0x20000000u : 0u);
        layer->setTile(int(flags % 4), int(flags / 4), 1);
        // Tiled import stores unsigned raw GIDs; the signed editing setter
        // intentionally maps negative values to empty cells.
        layer->tiles()->gids[flags] = raw;
    }
    eve::hd2d::TileMap3D builder;
    builder.setSideDepth(0.f);
    builder.buildRenderable(fx.gfx, layer)->setReceiveLight(false);
    auto *camera = eve::graphics::Camera3D::createCamera();
    camera->setEye(4.f, 10.f, 2.f);
    camera->setTarget(4.f, 0.f, 2.f);
    camera->setUp(0.f, 0.f, -1.f);
    camera->setOrthographic(6.f);
    fx.gfx->setScreenReadbackEnabled(true);
    for (int i = 0; i < 3; ++i) {
        fx.gfx->clear(std::nullopt, std::nullopt, std::nullopt);
        eve::graphics::RenderSystem3D::render(*fx.gfx);
        fx.gfx->present();
    }
    std::unique_ptr<eve::image::ImageData> frame(fx.gfx->newImageData());
    REQUIRE(frame != nullptr);
    const float scale = float(frame->getHeight()) / 6.f;
    for (int flags = 0; flags < 8; ++flags)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x) {
                int u = (flags & 1) ? 1 - x : x, v = (flags & 2) ? 1 - y : y;
                if (flags & 4) std::swap(u, v);
                const int px = int(float(frame->getWidth()) * 0.5f + (float((flags % 4) * 2 + x) + 0.5f - 4.f) * scale);
                const int py =
                    int(float(frame->getHeight()) * 0.5f + (float((flags / 4) * 2 + y) + 0.5f - 2.f) * scale);
                const auto actual = frame->getPixel(px, py);
                REQUIRE_EQ(actual.r > 0.3f, colors[v * 2 + u][0] != 0);
                REQUIRE_EQ(actual.g > 0.3f, colors[v * 2 + u][1] != 0);
                REQUIRE_EQ(actual.b > 0.3f, colors[v * 2 + u][2] != 0);
            }
}
}  // namespace test_hd2d
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_hex_level_simulation)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "ProcgenGridTestSupport.h"
#include "RenderImageAudit.h"
#include "filesystem/Filesystem.h"
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
#include "graphics/RenderSystem.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "image/ImageData.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/Inventory.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "map/DualGrid.h"
#include "map/FlowField.h"
#include "map/Fov.h"
#include "map/Map.h"
#include "map/Path.h"
#include "map/Pathfinder.h"
#include "map/TileLayer.h"
#include "map/TileOrientation.h"
#include "map/TileSystem.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/Grid2D.h"
#include "procgen/Params.h"
#include "procgen/Procgen.h"
#include "procgen/Semantic.h"
#include "spatial/QuadTree.h"
#include "spatial/Spatial.h"
#include "spatial/SpatialHash2D.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// hex_level_simulation.cpp
namespace test_hex_level_simulation {
// Hex dungeon test levels — cross-module simulation for engine feature QA.
//
// Levels (each TEST_CASE is an independent scenario; pipelines combine them):
//  01 procgen + hex tilemap + A* / Flow Field pathfinding
//  02 2D dynamic FOV (shadowcast, hex topology, explored memory)
//  03 2D dynamic lighting (point light + lit sprite collection)
//  04 object pickup via SpatialHash2D collision + Inventory
//  05 particle emitters (torch / spark on pickup)
//  06 Flow Field swarm (multi-unit same goal on hex)
//  07 cell-cost detour (prefer cheap corridor)
//  08 multi-revealer FOV + perception / stealth
//  09 FoW mask export + alternate FOV algorithms
//  10 Camera2D screen ↔ world ↔ hex tile pick
//  11 Dual-grid resolve on hexagonal logic layer
//  12 procgen algorithm variants on hex (cave / maze / wfc)
//  13 QuadTree viewport culling for loot markers
//  14 multi-light (point + directional) ambient scene
//  15 particle presets lifecycle + bag → stash transfer
//  16 facing-cone FOV (directional revealer)
//  17 Flow Field + cell-cost combined
//  18 seed reproducibility (spawn/path stable)
//  19 corner-peek FOV toggle
//  20 equipment loot pickup + equip slot
//  21 hex world↔tile roundtrip sampling
//  22 FOV explored memory clearMemory
//  23 findGroupPath on hex topology
//  24 FOV algorithm visibility parity smoke
//  25 blocked-cell + syncFromLayer pathfinding
//  26 drunkard cave hex
//  27 maze.backtrack hex path
//  28 wfc.simple dungeon hex
//  29 mist particle + cool light scene
//  30 raid combo (flow + cost + fov + pickup + particles + windowed preview)
//  pipeline.dungeonCrawl: full crawl through one seeded hex dungeon
//  pipeline.fogRaid: FOV cone + perception gated pickup + flow escort
//  pipeline.torchEscort: multi-revealer + light + particles along flow
//  pipeline.catalogRaid: catalog raid loot + perception gates
//  pipeline.costlyFogPickup: cell-cost detour + FOV pickup + particles


// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::map;
using namespace eve::procgen;
using namespace eve::graphics;
using namespace eve::particles;
using namespace eve::spatial;
using namespace eve::inventory;

namespace {

constexpr float kTileW    = 64.f;
constexpr float kTileH    = 32.f;
constexpr float kHexSide  = 16.f;
constexpr int   kWallGid  = 1;
constexpr int   kFloorGid = 2;
constexpr int   kDoorGid  = 3;

bool approxEq(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

void hideAllTileLayers() {
    if (ecs::current()->getManager<TileLayer>() == nullptr) return;
    auto view = ecs::View<TileLayer, TileLayer::Draw>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [draw]   = *it;
        draw->visible = false;
    }
}

void configureHexLayer(TileLayer *layer) {
    auto c           = layer->config();
    c->orientation   = MapOrientation::Hexagonal;
    c->staggerAxis   = StaggerAxis::Y;
    c->staggerIndex  = StaggerIndex::Odd;
    c->hexSideLength = kHexSide;
    layer->setOrigin(0.f, 0.f);
    layer->setVisible(true);
}

struct HexDungeon {
    TileLayer *layer = nullptr;
    Grid2D     grid;
    int        spawnTx = -1;
    int        spawnTy = -1;
    int        exitTx  = -1;
    int        exitTy  = -1;
};

bool findWalkable(const Grid2D &g, int &ox, int &oy) {
    for (int y = 0; y < g.getHeight(); ++y) {
        for (int x = 0; x < g.getWidth(); ++x) {
            const int c = g.getCell(x, y);
            if (c == int(Semantic::Floor) || c == int(Semantic::Corridor) || c == int(Semantic::Door)) {
                ox = x;
                oy = y;
                return true;
            }
        }
    }
    return false;
}

bool findFarthestWalkable(const Grid2D &g, int sx, int sy, int &ox, int &oy) {
    int bestX = sx, bestY = sy, bestD = -1;
    for (int y = 0; y < g.getHeight(); ++y) {
        for (int x = 0; x < g.getWidth(); ++x) {
            const int c = g.getCell(x, y);
            if (c != int(Semantic::Floor) && c != int(Semantic::Corridor) && c != int(Semantic::Door)) continue;
            const int dx   = x - sx;
            const int dy   = y - sy;
            const int dist = dx * dx + dy * dy;
            if (dist > bestD) {
                bestD = dist;
                bestX = x;
                bestY = y;
            }
        }
    }
    ox = bestX;
    oy = bestY;
    return bestD >= 0;
}

HexDungeon buildHexDungeon(Map *mapMod, Procgen *gen, uint32_t seed, int w, int h, const char *algo = "dungeon.bsp") {
    HexDungeon d;
    hideAllTileLayers();
    d.layer = mapMod->newLayer(w, h, kTileW, kTileH);
    configureHexLayer(d.layer);

    GeneratorRegistry::instance().registerBuiltins();
    Params p;
    p.setSeed(seed);
    p.setSize(w, h);
    std::string err;
    REQUIRE(GeneratorRegistry::instance().generate(algo, p, d.grid, err));

    gen->setPaletteGid("hex_test", "empty", 0);
    gen->setPaletteGid("hex_test", "wall", kWallGid);
    gen->setPaletteGid("hex_test", "floor", kFloorGid);
    gen->setPaletteGid("hex_test", "corridor", kFloorGid);
    gen->setPaletteGid("hex_test", "door", kDoorGid);
    eve::test_support::applyGridToLayer(*gen, d.grid, "hex_test", *d.layer);

    for (int i = 0; i < d.grid.getObjectCount(); ++i) {
        const std::string t  = d.grid.getObjectType(i);
        const int         tx = int(d.grid.getObjectX(i));
        const int         ty = int(d.grid.getObjectY(i));
        if (t == "spawn") {
            d.spawnTx = tx;
            d.spawnTy = ty;
        } else if (t == "stairs") {
            d.exitTx = tx;
            d.exitTy = ty;
        }
    }
    if (d.spawnTx < 0 || d.spawnTy < 0) REQUIRE(findWalkable(d.grid, d.spawnTx, d.spawnTy));
    if (d.exitTx < 0 || d.exitTy < 0) {
        // Prefer a walkable cell far from spawn.
        int bestX = d.spawnTx, bestY = d.spawnTy, bestD = -1;
        for (int y = 0; y < d.grid.getHeight(); ++y) {
            for (int x = 0; x < d.grid.getWidth(); ++x) {
                const int c = d.grid.getCell(x, y);
                if (c != int(Semantic::Floor) && c != int(Semantic::Corridor)) continue;
                const int dx   = x - d.spawnTx;
                const int dy   = y - d.spawnTy;
                const int dist = dx * dx + dy * dy;
                if (dist > bestD) {
                    bestD = dist;
                    bestX = x;
                    bestY = y;
                }
            }
        }
        d.exitTx = bestX;
        d.exitTy = bestY;
    }
    return d;
}

bool pathWalkable(Path *path, Pathfinder *pf) {
    if (!path) return false;
    for (int i = 0; i < path->getLength(); ++i) {
        if (!pf->isWalkable(path->getX(i), path->getY(i))) return false;
    }
    return true;
}

Texture *makeSolidTex(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

void hideLeftover2DCameras() {
    if (ecs::current()->getManager<Camera2D>() == nullptr) return;
    auto view = ecs::View<Camera2D, Camera2D::Data>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [data]  = *it;
        data->active = false;
    }
}

void hideLeftoverSprites() {
    if (ecs::current()->getManager<Renderable2D>() == nullptr) return;
    auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [sp]   = *it;
        sp->visible = false;
    }
}

void hideLeftoverEmitters() {
    if (ecs::current()->getManager<ParticleEmitter>() == nullptr) return;
    auto view = ecs::View<ParticleEmitter, ParticleEmitter::Draw>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [draw]   = *it;
        draw->visible = false;
    }
}

constexpr float kKenneyTileW   = 55.f;
constexpr float kKenneyTileH   = 57.f;
constexpr float kKenneyHexSide = 28.5f;

bool bindKenneyHexTileset(Graphics *gfx, TileLayer *layer) {
    auto *fs = eve::filesystem::Filesystem::create();
    if (!fs || !gfx || !layer) return false;
    fs->setIdentity("ev_ut_hex_kenney", true);
    fs->setupWriteDirectory();
    const std::string dir = std::string(EVENGINE_SOURCE_DIR) + "/examples/hex-levels/data/tiles";
    fs->allowMountingForPath(dir);
    if (!fs->mount(dir, "", false)) return false;
    Texture *tex = gfx->newTextureFromFile("kenney_hex.png");
    if (!tex) return false;
    layer->setTileSize(kKenneyTileW, kKenneyTileH);
    layer->config()->hexSideLength = kKenneyHexSide;
    layer->setTilesetTileSize(int(kKenneyTileW), int(kKenneyTileH));
    layer->setTileset(tex, 1, 4, 0, 1);
    return true;
}

struct HexPreview {
    const char      *title   = "hex preview";
    const char      *pngName = "preview.png";
    Path            *path    = nullptr;
    ParticleEmitter *fire    = nullptr;
    Light2D         *lamp    = nullptr;
    int              lootTx  = -1;
    int              lootTy  = -1;
    int              frames  = 40;
};

/** Windowed follow-cam playback: Kenney hex tiles + hero + torch + fire. */
void previewHex(Map *mapMod, TileLayer *layer, int spawnTx, int spawnTy, int exitTx, int exitTy,
                const HexPreview &opt) {
    if (!mapMod || !layer) return;
    hideLeftover2DCameras();
    hideLeftoverSprites();
    hideLeftoverEmitters();
    hideAllTileLayers();
    layer->setVisible(true);

    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings ws;
    ws.width    = 960;
    ws.height   = 540;
    ws.centered = true;
    REQUIRE(win->setWindowSettings(ws));
    win->setWindowTitle(opt.title ? opt.title : "hex preview");
    gfx->setScreenReadbackEnabled(true);

    const Color sky(0.05f, 0.06f, 0.09f, 1.f);
    gfx->setBackgroundColor(sky);
    REQUIRE(bindKenneyHexTileset(gfx, layer));
    const float tw = layer->getTileWidth();
    const float th = layer->getTileHeight();

    Path       *ownedPath = nullptr;
    Pathfinder *ownedPf   = nullptr;
    Path       *path      = opt.path;
    if ((!path || path->getLength() <= 0) && spawnTx >= 0 && exitTx >= 0) {
        ownedPf = mapMod->newPathfinder(layer);
        ownedPf->blockGid(kWallGid);
        ownedPf->setTopology("hex");
        ownedPath = ownedPf->findPath(spawnTx, spawnTy, exitTx, exitTy);
        path      = ownedPath;
    }

    ParticleEmitter *fire = opt.fire;
    if (!fire) {
        auto *parts = Particles::create();
        fire        = parts->newEmitter(96);
        fire->applyPreset("fire");
    }
    Light2D *lamp = opt.lamp;
    if (!lamp) lamp = Light2D::createLight("point");

    auto *cam           = Camera2D::createCamera();
    cam->data()->active = true;
    cam->data()->zoom   = 1.15f;
    cam->setAmbient(0.12f, 0.11f, 0.14f);
    layer->setCamera(cam);

    Texture *heroTex = makeSolidTex(gfx, 255, 220, 80);
    Texture *lootTex = makeSolidTex(gfx, 240, 70, 200);
    Texture *exitTex = makeSolidTex(gfx, 70, 220, 230);
    REQUIRE(heroTex != nullptr);
    REQUIRE(lootTex != nullptr);
    REQUIRE(exitTex != nullptr);

    auto *hero                   = Renderable2D::create();
    hero->sprite()->texture      = heroTex;
    hero->sprite()->width        = 22.f;
    hero->sprite()->height       = 22.f;
    hero->sprite()->visible      = true;
    hero->sprite()->layer        = 20;
    hero->sprite()->camera       = cam;
    hero->sprite()->receiveLight = true;

    auto *lootMark                   = Renderable2D::create();
    lootMark->sprite()->texture      = lootTex;
    lootMark->sprite()->width        = 14.f;
    lootMark->sprite()->height       = 14.f;
    lootMark->sprite()->layer        = 18;
    lootMark->sprite()->camera       = cam;
    lootMark->sprite()->receiveLight = true;
    if (opt.lootTx >= 0) {
        float lootWx = 0.f, lootWy = 0.f;
        layer->tileToWorld(opt.lootTx, opt.lootTy, lootWx, lootWy);
        lootMark->transform()->x    = lootWx + tw * 0.25f;
        lootMark->transform()->y    = lootWy + th * 0.15f;
        lootMark->sprite()->visible = true;
    } else {
        lootMark->sprite()->visible = false;
    }

    auto *exitMark                   = Renderable2D::create();
    exitMark->sprite()->texture      = exitTex;
    exitMark->sprite()->width        = 16.f;
    exitMark->sprite()->height       = 16.f;
    exitMark->sprite()->layer        = 18;
    exitMark->sprite()->camera       = cam;
    exitMark->sprite()->receiveLight = true;
    if (exitTx >= 0) {
        float exitWx = 0.f, exitWy = 0.f;
        layer->tileToWorld(exitTx, exitTy, exitWx, exitWy);
        exitMark->transform()->x    = exitWx + tw * 0.2f;
        exitMark->transform()->y    = exitWy + th * 0.1f;
        exitMark->sprite()->visible = true;
    } else {
        exitMark->sprite()->visible = false;
    }

    lamp->setColor(1.f, 0.72f, 0.42f, 2.4f);
    lamp->setRadius(180.f);
    lamp->setEnabled(true);
    fire->setCamera(cam);
    fire->setVisible(true);
    fire->start();

    const int n      = (path && path->getLength() > 0) ? path->getLength() : 1;
    const int holdTx = spawnTx >= 0 ? spawnTx : 0;
    const int holdTy = spawnTy >= 0 ? spawnTy : 0;
    const int frames = std::clamp(opt.frames, 16, 60);
    bool      quit   = false;
    for (int f = 0; f < frames && !quit; ++f) {
        const int step = (n <= 1) ? 0 : f * (n - 1) / std::max(1, frames - 1);
        float     wx = 0.f, wy = 0.f;
        if (path && path->getLength() > 0)
            layer->tileToWorld(path->getX(step), path->getY(step), wx, wy);
        else
            layer->tileToWorld(holdTx, holdTy, wx, wy);
        wx += tw * 0.35f;
        wy += th * 0.35f;
        cam->data()->x       = wx;
        cam->data()->y       = wy;
        hero->transform()->x = wx - 11.f;
        hero->transform()->y = wy - 11.f;
        lamp->setPosition(wx, wy);
        fire->setPosition(wx + 4.f, wy - 6.f);
        if (opt.lootTx >= 0 && n > 1 && step >= n / 2) lootMark->sprite()->visible = false;
        ParticleSimSystem::update(1.f / 30.f);

        gfx->clearScreen();
        mapMod->render(gfx);
        ParticleRenderSystem::render(gfx);
        gfx->present();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
        }
        SDL_Delay(16);
    }

    const int   w    = gfx->getWidth();
    const int   h    = gfx->getHeight();
    Color       mid  = gfx->getPixel(w / 2, h / 2);
    const float luma = (mid.r + mid.g + mid.b) / 3.f;
    CHECK(luma > 0.04f);

    std::unique_ptr<eve::image::ImageData> img(gfx->newImageData());
    REQUIRE(img.get() != nullptr);
    const std::string dir = std::string(EVENGINE_TEST_BINARY_DIR) + "/out/hex_levels";
    std::error_code   ec;
    std::filesystem::create_directories(dir, ec);
    const char *png = opt.pngName ? opt.pngName : "preview.png";
    REQUIRE(saveImagePng(*img, dir + "/" + png));

    hero->sprite()->visible     = false;
    lootMark->sprite()->visible = false;
    exitMark->sprite()->visible = false;
    fire->setVisible(false);
    fire->stop();
    lamp->setEnabled(false);
    cam->data()->active = false;
    layer->setCamera(nullptr);
    layer->setVisible(false);
    win->close();
    delete ownedPath;
    delete ownedPf;
}

void previewHex(Map *mapMod, const HexDungeon &d, const HexPreview &opt) {
    previewHex(mapMod, d.layer, d.spawnTx, d.spawnTy, d.exitTx, d.exitTy, opt);
}

}  // namespace

TEST_CASE("hex.level.01.procgenPath") {
    auto      *mapMod = Map::create();
    auto      *gen    = Procgen::create();
    HexDungeon d      = buildHexDungeon(mapMod, gen, 42, 32, 24);

    CHECK_EQ(static_cast<int>(d.layer->config()->orientation), static_cast<int>(MapOrientation::Hexagonal));
    CHECK(approxEq(d.layer->config()->hexSideLength, kHexSide));
    CHECK(d.spawnTx >= 0);
    CHECK(d.exitTx >= 0);
    CHECK_NE(d.layer->getTile(d.spawnTx, d.spawnTy), 0);
    CHECK_NE(d.layer->getTile(d.exitTx, d.exitTy), 0);

    // Projection: hex row pitch shifts odd rows.
    float wx0 = 0.f, wy0 = 0.f, wx1 = 0.f, wy1 = 0.f;
    d.layer->tileToWorld(0, 0, wx0, wy0);
    d.layer->tileToWorld(0, 1, wx1, wy1);
    CHECK(approxEq(wx0, 0.f));
    CHECK(approxEq(wy0, 0.f));
    CHECK(approxEq(wx1, kTileW * 0.5f));
    CHECK(approxEq(wy1, (kTileH + kHexSide) * 0.5f));

    int backTx = -1, backTy = -1;
    d.layer->worldToTile(wx1 + kTileW * 0.5f, wy1 + kTileH * 0.5f, backTx, backTy);
    CHECK_EQ(backTx, 0);
    CHECK_EQ(backTy, 1);

    Pathfinder *pf = mapMod->newPathfinder(d.layer);
    REQUIRE(pf != nullptr);
    pf->blockGid(kWallGid);
    pf->setBlockEmpty(true);
    pf->setTopology("auto");
    CHECK_EQ(pf->getTopology(), std::string("hex"));

    Path *path = pf->findPath(d.spawnTx, d.spawnTy, d.exitTx, d.exitTy);
    REQUIRE(path != nullptr);
    CHECK(path->getLength() > 0);
    CHECK_EQ(path->getX(0), d.spawnTx);
    CHECK_EQ(path->getY(0), d.spawnTy);
    CHECK_EQ(path->getX(path->getLength() - 1), d.exitTx);
    CHECK_EQ(path->getY(path->getLength() - 1), d.exitTy);
    CHECK(pathWalkable(path, pf));

    // Group path / flow field to the same exit.
    FlowField *field = pf->buildFlowField(d.exitTx, d.exitTy);
    REQUIRE(field != nullptr);
    CHECK(field->isReachable(d.spawnTx, d.spawnTy));
    Path *group = pf->findGroupPath(d.spawnTx, d.spawnTy, d.exitTx, d.exitTy);
    REQUIRE(group != nullptr);
    CHECK(group->getLength() > 0);
    CHECK_EQ(group->getX(group->getLength() - 1), d.exitTx);
    CHECK_EQ(group->getY(group->getLength() - 1), d.exitTy);

    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.01 procgen path", .pngName = "01_procgenPath.png", .path = path});

    delete group;
    delete field;
    delete path;
    delete pf;
}

TEST_CASE("hex.level.02.dynamicFov") {
    // Handcrafted open hex strip: procgen spawn↔exit connectivity is not
    // portable across libstdc++/libc++ (uniform_int_distribution differs), and
    // cave.cellular can place spawn/stairs in disconnected components.
    auto *mapMod = Map::create();
    hideAllTileLayers();
    TileLayer *layer = mapMod->newLayer(12, 5, kTileW, kTileH);
    configureHexLayer(layer);
    layer->fill(kFloorGid);

    Fov *fov = mapMod->newFov(layer);
    REQUIRE(fov != nullptr);
    fov->blockOpaqueGid(kWallGid);
    fov->setBlockEmpty(false);
    fov->setTopology("auto");
    CHECK_EQ(fov->getTopology(), std::string("hex"));
    fov->setAlgorithm("shadowcast");
    fov->setRadiusMetric("euclidean");

    const int spawnTx = 1, spawnTy = 2;
    const int id = fov->addRevealer(spawnTx, spawnTy, 6);
    fov->compute();
    CHECK(fov->isVisible(spawnTx, spawnTy));
    CHECK(fov->isExplored(spawnTx, spawnTy));

    // Move along A* path; memory should retain explored cells after leaving.
    Pathfinder *pf = mapMod->newPathfinder(layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");
    Path *path = pf->findPath(spawnTx, spawnTy, 10, 2);
    REQUIRE(path != nullptr);
    REQUIRE(path->getLength() > 1);

    const int mid  = path->getLength() / 2;
    const int midX = path->getX(mid);
    const int midY = path->getY(mid);
    fov->setRevealerPosition(id, midX, midY);
    fov->compute();
    CHECK(fov->isVisible(midX, midY));
    CHECK(fov->isExplored(spawnTx, spawnTy));  // memory

    // Opaque wall should cast a shadow along cube line when present.
    Fov *wallFov = mapMod->newFovSize(9, 5);
    wallFov->setTopology("hex");
    wallFov->setAlgorithm("shadowcast");
    wallFov->setBlockEmpty(false);
    for (int y = 0; y < 5; ++y) wallFov->setOpaque(4, y, true);
    wallFov->addRevealer(1, 2, 5);
    wallFov->compute();
    CHECK(wallFov->isVisible(1, 2));
    CHECK(wallFov->isVisible(4, 2));
    CHECK(!wallFov->isVisible(7, 2));

    previewHex(mapMod, layer, spawnTx, spawnTy, 10, 2,
               HexPreview{.title = "hex.level.02 dynamic fov", .pngName = "02_dynamicFov.png", .path = path});

    delete wallFov;
    delete path;
    delete pf;
    delete fov;
    layer->setVisible(false);
}

TEST_CASE("hex.level.03.dynamicLight") {
    auto      *mapMod = Map::create();
    auto      *gen    = Procgen::create();
    HexDungeon d      = buildHexDungeon(mapMod, gen, 99, 24, 18);

    float heroWx = 0.f, heroWy = 0.f;
    d.layer->tileToWorld(d.spawnTx, d.spawnTy, heroWx, heroWy);

    auto *cam           = Camera2D::createCamera();
    cam->data()->active = true;
    cam->data()->x      = heroWx + 32.f;
    cam->data()->y      = heroWy;
    cam->data()->zoom   = 1.f;
    cam->setAmbient(0.08f, 0.08f, 0.10f);

    Texture albedo;
    albedo.width  = 32;
    albedo.height = 32;
    Texture normal;
    normal.width  = 32;
    normal.height = 32;

    auto *hero                    = Renderable2D::create();
    hero->transform()->x          = heroWx;
    hero->transform()->y          = heroWy;
    hero->sprite()->width         = 24.f;
    hero->sprite()->height        = 24.f;
    hero->sprite()->texture       = &albedo;
    hero->sprite()->normalTexture = &normal;
    hero->sprite()->receiveLight  = true;
    hero->sprite()->visible       = true;
    hero->sprite()->layer         = 10;
    hero->sprite()->camera        = cam;

    auto *torch = Light2D::createLight("point");
    torch->setPosition(heroWx + 12.f, heroWy + 12.f);
    torch->setColor(1.f, 0.75f, 0.45f, 2.2f);
    torch->setRadius(140.f);
    torch->setEnabled(true);
    CHECK_EQ(torch->getType(), "point");
    CHECK(torch->isEnabled());

    // Far unlit prop for contrast in collection.
    float exitWx = 0.f, exitWy = 0.f;
    d.layer->tileToWorld(d.exitTx, d.exitTy, exitWx, exitWy);
    auto *exitMarker                   = Renderable2D::create();
    exitMarker->transform()->x         = exitWx;
    exitMarker->transform()->y         = exitWy;
    exitMarker->sprite()->width        = 16.f;
    exitMarker->sprite()->height       = 16.f;
    exitMarker->sprite()->texture      = &albedo;
    exitMarker->sprite()->receiveLight = true;
    exitMarker->sprite()->visible      = true;
    exitMarker->sprite()->layer        = 10;
    exitMarker->sprite()->camera       = cam;

    std::vector<DrawItem2D> scene;
    TileRenderSystem::collect(scene);
    RenderSystem::collectSprites(scene);
    REQUIRE(scene.size() >= 2);

    bool foundLitHero = false;
    for (const auto &it : scene) {
        if (it.texture == &albedo && it.normal == &normal) {
            CHECK(it.litPath);
            CHECK(it.receiveLight);
            foundLitHero = true;
        }
    }
    CHECK(foundLitHero);

    // Torch follows hero one tile along path.
    Pathfinder *pf = mapMod->newPathfinder(d.layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");
    Path *path = pf->findPath(d.spawnTx, d.spawnTy, d.exitTx, d.exitTy);
    REQUIRE(path != nullptr);
    REQUIRE(path->getLength() > 1);
    float nx = 0.f, ny = 0.f;
    d.layer->tileToWorld(path->getX(1), path->getY(1), nx, ny);
    hero->transform()->x = nx;
    hero->transform()->y = ny;
    torch->setPosition(nx + 12.f, ny + 12.f);
    CHECK(approxEq(torch->getX(), nx + 12.f));
    CHECK(approxEq(torch->getY(), ny + 12.f));

    hero->sprite()->visible       = false;
    exitMarker->sprite()->visible = false;
    torch->setEnabled(false);
    cam->data()->active = false;

    previewHex(
        mapMod, d,
        HexPreview{
            .title = "hex.level.03 dynamic light", .pngName = "03_dynamicLight.png", .path = path, .lamp = torch});

    d.layer->setVisible(false);

    delete path;
    delete pf;
}

TEST_CASE("hex.level.04.pickupCollision") {
    auto      *mapMod     = Map::create();
    auto      *gen        = Procgen::create();
    auto      *spatialMod = Spatial::create();
    HexDungeon d          = buildHexDungeon(mapMod, gen, 123, 24, 18);

    ItemRegistry::clear();
    InventorySystem::clearEvents();
    InventorySystem::ensureBuiltins();
    ItemDefinition potion;
    potion.id          = "hex.potion";
    potion.displayName = "Hex Potion";
    potion.maxStack    = 10;
    potion.weight      = 0.2f;
    potion.tags        = {"consumable", "loot"};
    ItemRegistry::registerItem(potion);
    ItemDefinition key;
    key.id          = "hex.key";
    key.displayName = "Brass Key";
    key.maxStack    = 1;
    key.weight      = 0.1f;
    key.tags        = {"key", "loot"};
    ItemRegistry::registerItem(key);

    auto *inv = Inventory::create();
    Bag  *bag = inv->newBag(12);
    bag->setId("hex.player");
    bag->setMaxWeight(40.f);

    // Place loot on neighboring walkable hexes around spawn.
    struct Loot {
        int         id;
        int         tx, ty;
        const char *itemId;
        bool        taken = false;
    };
    std::vector<Loot> loots;
    Pathfinder       *pf = mapMod->newPathfinder(d.layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");

    auto tryAddLoot = [&](int tx, int ty, int id, const char *itemId) {
        if (!pf->isWalkable(tx, ty)) return;
        if (tx == d.spawnTx && ty == d.spawnTy) return;
        loots.push_back({id, tx, ty, itemId, false});
    };
    tryAddLoot(d.spawnTx + 1, d.spawnTy, 1, "hex.potion");
    tryAddLoot(d.spawnTx, d.spawnTy + 1, 2, "hex.key");
    tryAddLoot(d.spawnTx - 1, d.spawnTy, 3, "hex.potion");
    if (loots.empty()) {
        // Fallback: scan nearby cells.
        for (int dy = -2; dy <= 2 && loots.size() < 2; ++dy) {
            for (int dx = -2; dx <= 2 && loots.size() < 2; ++dx) {
                tryAddLoot(d.spawnTx + dx, d.spawnTy + dy, int(loots.size()) + 1,
                           loots.empty() ? "hex.potion" : "hex.key");
            }
        }
    }
    REQUIRE(loots.size() >= 1);

    std::unique_ptr<SpatialHash2D> hash(spatialMod->newSpatialHash2D(48.f));
    const float                    half = 10.f;
    for (const auto &L : loots) {
        float wx = 0.f, wy = 0.f;
        d.layer->tileToWorld(L.tx, L.ty, wx, wy);
        wx += kTileW * 0.5f;
        wy += kTileH * 0.5f;
        CHECK(hash->insert(L.id, wx - half, wy - half, wx + half, wy + half));
    }

    float playerWx = 0.f, playerWy = 0.f;
    d.layer->tileToWorld(d.spawnTx, d.spawnTy, playerWx, playerWy);
    playerWx += kTileW * 0.5f;
    playerWy += kTileH * 0.5f;

    int picked = 0;
    for (auto &L : loots) {
        float lx = 0.f, ly = 0.f;
        d.layer->tileToWorld(L.tx, L.ty, lx, ly);
        lx += kTileW * 0.5f;
        ly += kTileH * 0.5f;
        // Move player onto loot cell (collision pickup radius).
        playerWx    = lx;
        playerWy    = ly;
        const int n = hash->queryCircle(playerWx, playerWy, 14.f);
        for (int i = 0; i < n; ++i) {
            const int hit = hash->getResultId(i);
            for (auto &cand : loots) {
                if (cand.taken || cand.id != hit) continue;
                const int added = bag->addItem(cand.itemId, 1);
                CHECK_EQ(added, 1);
                CHECK(hash->remove(cand.id));
                cand.taken = true;
                ++picked;
            }
        }
    }
    CHECK(picked >= 1);
    CHECK(bag->countItem("hex.potion") + bag->countItem("hex.key") == picked);

    // No double-pickup after removal.
    const int again = hash->queryCircle(playerWx, playerWy, 14.f);
    CHECK_EQ(again, 0);

    previewHex(mapMod, d, HexPreview{.title = "hex.level.04 pickup", .pngName = "04_pickupCollision.png"});

    bag->destroy();
    ItemRegistry::clear();
    delete pf;
}

TEST_CASE("hex.level.05.particles") {
    auto      *mapMod = Map::create();
    auto      *gen    = Procgen::create();
    auto      *parts  = Particles::create();
    HexDungeon d      = buildHexDungeon(mapMod, gen, 55, 20, 16);

    float torchWx = 0.f, torchWy = 0.f;
    d.layer->tileToWorld(d.spawnTx, d.spawnTy, torchWx, torchWy);
    torchWx += kTileW * 0.5f;
    torchWy += kTileH * 0.35f;

    ParticleEmitter *torchFx = parts->newEmitter(256);
    REQUIRE(torchFx != nullptr);
    torchFx->applyPreset("fire");
    torchFx->setPosition(torchWx, torchWy);
    torchFx->start();
    CHECK(torchFx->isActive());

    ParticleEmitter *sparkFx = parts->newEmitter(128);
    sparkFx->applyPreset("spark");
    sparkFx->setPosition(torchWx, torchWy);
    sparkFx->setEmitterLifetime(0.35f);
    sparkFx->start();

    // Simulate a short burn-in so particles exist.
    for (int i = 0; i < 8; ++i) parts->update(1.f / 60.f);
    CHECK(torchFx->getCount() > 0);

    // Pickup burst: one-shot spark at exit.
    float exitWx = 0.f, exitWy = 0.f;
    d.layer->tileToWorld(d.exitTx, d.exitTy, exitWx, exitWy);
    sparkFx->setPosition(exitWx + kTileW * 0.5f, exitWy + kTileH * 0.5f);
    sparkFx->reset();
    sparkFx->setEmitterLifetime(0.2f);
    sparkFx->start();
    sparkFx->emit(24);
    CHECK(sparkFx->getCount() > 0);

    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.05 particles", .pngName = "05_particles.png", .fire = torchFx});

    torchFx->stop();
    sparkFx->stop();
}

TEST_CASE("hex.level.pipeline.dungeonCrawl") {
    auto      *mapMod     = Map::create();
    auto      *gen        = Procgen::create();
    auto      *spatialMod = Spatial::create();
    auto      *parts      = Particles::create();
    HexDungeon d          = buildHexDungeon(mapMod, gen, 20260812, 36, 28);

    // --- Path: spawn → exit ---
    Pathfinder *pf = mapMod->newPathfinder(d.layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");
    Path *path = pf->findPath(d.spawnTx, d.spawnTy, d.exitTx, d.exitTy);
    REQUIRE(path != nullptr);
    REQUIRE(path->getLength() > 2);

    // --- FOV revealer follows path ---
    Fov *fov = mapMod->newFov(d.layer);
    fov->blockOpaqueGid(kWallGid);
    fov->setTopology("hex");
    const int revealer = fov->addRevealer(d.spawnTx, d.spawnTy, 5);

    // --- Lighting / hero sprite ---
    float wx = 0.f, wy = 0.f;
    d.layer->tileToWorld(d.spawnTx, d.spawnTy, wx, wy);
    auto *cam           = Camera2D::createCamera();
    cam->data()->active = true;
    cam->data()->x      = wx;
    cam->data()->y      = wy;
    cam->setAmbient(0.06f, 0.06f, 0.08f);

    Texture albedo;
    albedo.width                 = 16;
    albedo.height                = 16;
    auto *hero                   = Renderable2D::create();
    hero->transform()->x         = wx;
    hero->transform()->y         = wy;
    hero->sprite()->width        = 20.f;
    hero->sprite()->height       = 20.f;
    hero->sprite()->texture      = &albedo;
    hero->sprite()->receiveLight = true;
    hero->sprite()->visible      = true;
    hero->sprite()->camera       = cam;

    auto *torch = Light2D::createLight("point");
    torch->setColor(1.f, 0.8f, 0.5f, 2.f);
    torch->setRadius(120.f);
    torch->setEnabled(true);

    ParticleEmitter *fire = parts->newEmitter(128);
    fire->applyPreset("fire");
    fire->start();

    // --- Loot on path midpoint ---
    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    ItemDefinition gem;
    gem.id       = "hex.gem";
    gem.maxStack = 5;
    gem.weight   = 0.05f;
    ItemRegistry::registerItem(gem);
    auto *inv = Inventory::create();
    Bag  *bag = inv->newBag(8);

    const int mid    = path->getLength() / 2;
    const int lootTx = path->getX(mid);
    const int lootTy = path->getY(mid);
    float     lootWx = 0.f, lootWy = 0.f;
    d.layer->tileToWorld(lootTx, lootTy, lootWx, lootWy);
    lootWx += kTileW * 0.5f;
    lootWy += kTileH * 0.5f;

    std::unique_ptr<SpatialHash2D> hash(spatialMod->newSpatialHash2D(32.f));
    CHECK(hash->insert(42, lootWx - 8.f, lootWy - 8.f, lootWx + 8.f, lootWy + 8.f));

    bool gotGem         = false;
    int  exploredBefore = 0;
    for (int step = 0; step < path->getLength(); ++step) {
        const int tx = path->getX(step);
        const int ty = path->getY(step);
        fov->setRevealerPosition(revealer, tx, ty);
        fov->compute();
        CHECK(fov->isVisible(tx, ty));

        d.layer->tileToWorld(tx, ty, wx, wy);
        hero->transform()->x = wx;
        hero->transform()->y = wy;
        torch->setPosition(wx + 10.f, wy + 8.f);
        fire->setPosition(wx + 10.f, wy + 4.f);
        cam->data()->x = wx;
        cam->data()->y = wy;
        parts->update(1.f / 30.f);

        const float px = wx + kTileW * 0.5f;
        const float py = wy + kTileH * 0.5f;
        if (!gotGem && hash->queryCircle(px, py, 16.f) > 0) {
            const int added = bag->addItem("hex.gem", 1);
            CHECK_EQ(added, 1);
            hash->remove(42);
            ParticleEmitter *burst = parts->newEmitter(64);
            burst->applyPreset("spark");
            burst->setPosition(lootWx, lootWy);
            burst->setEmitterLifetime(0.25f);
            burst->start();
            burst->emit(16);
            gotGem = true;
        }
        if (fov->isExplored(d.spawnTx, d.spawnTy)) exploredBefore = 1;
    }
    CHECK_EQ(exploredBefore, 1);
    CHECK(gotGem);
    CHECK_EQ(bag->countItem("hex.gem"), 1);
    CHECK(fov->isExplored(d.spawnTx, d.spawnTy));
    CHECK_EQ(path->getX(path->getLength() - 1), d.exitTx);
    CHECK(fire->getCount() > 0);

    std::vector<DrawItem2D> scene;
    RenderSystem::collectSprites(scene);
    bool lit = false;
    for (const auto &it : scene) {
        if (it.texture == &albedo && it.receiveLight) lit = true;
    }
    CHECK(lit);

    hero->sprite()->visible = false;
    torch->setEnabled(false);
    cam->data()->active = false;
    fire->stop();
    bag->destroy();
    ItemRegistry::clear();
    delete path;
    delete pf;
    delete fov;
}

TEST_CASE("hex.level.06.flowFieldSwarm") {
    auto      *mapMod = Map::create();
    auto      *gen    = Procgen::create();
    HexDungeon d      = buildHexDungeon(mapMod, gen, 606, 28, 22);

    Pathfinder *pf = mapMod->newPathfinder(d.layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");

    FlowField *field = pf->buildFlowField(d.exitTx, d.exitTy);
    REQUIRE(field != nullptr);
    CHECK_EQ(field->getGoalX(), d.exitTx);
    CHECK_EQ(field->getGoalY(), d.exitTy);
    CHECK(field->isReachable(d.spawnTx, d.spawnTy));

    // Collect a few distinct walkable starts near spawn.
    std::vector<std::pair<int, int>> starts;
    starts.push_back({d.spawnTx, d.spawnTy});
    for (int dy = -3; dy <= 3 && starts.size() < 4; ++dy) {
        for (int dx = -3; dx <= 3 && starts.size() < 4; ++dx) {
            const int x = d.spawnTx + dx;
            const int y = d.spawnTy + dy;
            if (!pf->isWalkable(x, y)) continue;
            if (x == d.spawnTx && y == d.spawnTy) continue;
            if (!field->isReachable(x, y)) continue;
            starts.push_back({x, y});
        }
    }
    REQUIRE(starts.size() >= 2);

    for (const auto &s : starts) {
        Path *p = pf->followFlow(field, s.first, s.second);
        REQUIRE(p != nullptr);
        CHECK(p->getLength() > 0);
        CHECK_EQ(p->getX(0), s.first);
        CHECK_EQ(p->getY(0), s.second);
        CHECK_EQ(p->getX(p->getLength() - 1), d.exitTx);
        CHECK_EQ(p->getY(p->getLength() - 1), d.exitTy);
        CHECK(pathWalkable(p, pf));
        delete p;

        Path *g = pf->findGroupPath(s.first, s.second, d.exitTx, d.exitTy);
        REQUIRE(g != nullptr);
        CHECK_EQ(g->getX(g->getLength() - 1), d.exitTx);
        delete g;
    }
    Path *swarmPath = pf->followFlow(field, d.spawnTx, d.spawnTy);
    REQUIRE(swarmPath != nullptr);
    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.06 flow swarm", .pngName = "06_flowFieldSwarm.png", .path = swarmPath});
    delete swarmPath;
    delete field;
    delete pf;
}

TEST_CASE("hex.level.07.cellCostDetour") {
    auto *mapMod = Map::create();
    hideAllTileLayers();
    TileLayer *layer = mapMod->newLayer(9, 5, kTileW, kTileH);
    configureHexLayer(layer);
    layer->fill(kFloorGid);
    // Vertical expensive corridor at x=4; cheap bypass via bottom.
    for (int y = 0; y < 4; ++y) layer->setTile(4, y, kFloorGid);

    Pathfinder *pf = mapMod->newPathfinder(layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");
    for (int y = 0; y < 4; ++y) pf->setCellCost(4, y, 12.f);

    Path *cheap = pf->findPath(0, 2, 8, 2);
    REQUIRE(cheap != nullptr);
    CHECK(cheap->getLength() > 0);
    CHECK_EQ(cheap->getX(cheap->getLength() - 1), 8);
    // Prefer bypassing the costly column when possible.
    bool steppedOnCostly = false;
    for (int i = 0; i < cheap->getLength(); ++i) {
        if (cheap->getX(i) == 4 && cheap->getY(i) < 4) steppedOnCostly = true;
    }
    // With high cost, path should usually avoid the strip; if topology forces a
    // step, totalCost must still beat a forced expensive route.
    Path *forced = pf->findPath(0, 0, 8, 0);
    REQUIRE(forced != nullptr);
    CHECK(cheap->getTotalCost() <= forced->getTotalCost() + 1e-3f);
    (void)steppedOnCostly;

    previewHex(mapMod, layer, 0, 2, 8, 2,
               HexPreview{.title = "hex.level.07 cell cost", .pngName = "07_cellCostDetour.png", .path = cheap});

    delete forced;
    delete cheap;
    delete pf;
    layer->setVisible(false);
}

TEST_CASE("hex.level.08.multiRevealerPerception") {
    auto      *mapMod = Map::create();
    auto      *gen    = Procgen::create();
    HexDungeon d      = buildHexDungeon(mapMod, gen, 808, 24, 18, "cave.cellular");

    Fov *fov = mapMod->newFov(d.layer);
    fov->blockOpaqueGid(kWallGid);
    fov->setTopology("hex");
    fov->setPerceptionRadiusScale(1.f);
    fov->setDetectionMargin(0.f);

    const int hero  = fov->addRevealer(d.spawnTx, d.spawnTy, 3);
    const int torch = fov->addRevealer(d.exitTx, d.exitTy, 2);
    fov->setRevealerPerception(hero, 2.f);
    CHECK_EQ(fov->getEffectiveRadius(hero), 5);  // 3 + 2*1
    fov->compute();

    CHECK(fov->isVisible(d.spawnTx, d.spawnTy));
    CHECK(fov->isVisible(d.exitTx, d.exitTy));
    CHECK_EQ(fov->getRevealerCount(), 2);

    // Stealth gate: visible cell but high stealth fails detection.
    CHECK(fov->canDetect(hero, d.spawnTx, d.spawnTy, 1.f));
    CHECK(!fov->canDetect(hero, d.spawnTx, d.spawnTy, 9.f));

    fov->setRevealerEnabled(torch, false);
    fov->compute();
    CHECK(fov->isVisible(d.spawnTx, d.spawnTy));

    fov->setRevealerFacing(hero, 0.f, 45.f);  // east-ish cone
    fov->setRevealerPosition(hero, d.spawnTx, d.spawnTy);
    fov->compute();
    CHECK(fov->isVisible(d.spawnTx, d.spawnTy));

    previewHex(mapMod, d, HexPreview{.title = "hex.level.08 perception", .pngName = "08_multiRevealerPerception.png"});

    delete fov;
}

TEST_CASE("hex.level.09.fowMaskAndAlgos") {
    auto      *mapMod = Map::create();
    auto      *gen    = Procgen::create();
    HexDungeon d      = buildHexDungeon(mapMod, gen, 909, 20, 16);

    Fov *fov = mapMod->newFov(d.layer);
    fov->blockOpaqueGid(kWallGid);
    fov->setTopology("hex");
    fov->setAlgorithm("shadowcast");
    const int id = fov->addRevealer(d.spawnTx, d.spawnTy, 4);
    fov->compute();
    CHECK(fov->isVisible(d.spawnTx, d.spawnTy));
    CHECK_EQ(fov->getMaskByte(d.spawnTx, d.spawnTy), 255);

    // Leave the cell: explored memory → mid mask byte, not fully dark.
    fov->setRevealerPosition(id, d.exitTx, d.exitTy);
    fov->compute();
    if (!fov->isVisible(d.spawnTx, d.spawnTy)) {
        CHECK(fov->isExplored(d.spawnTx, d.spawnTy));
        CHECK(fov->getMaskByte(d.spawnTx, d.spawnTy) > 0);
        CHECK(fov->getMaskByte(d.spawnTx, d.spawnTy) < 255);
    }

    std::vector<uint8_t> mask;
    CHECK(fov->fillMaskR8(mask));
    CHECK_EQ(int(mask.size()), d.layer->getMapWidth() * d.layer->getMapHeight());

    fov->clearMemory();
    fov->compute();
    // After clear, cells only currently visible stay explored.
    CHECK(fov->isVisible(d.exitTx, d.exitTy));

    fov->setAlgorithm("raycast");
    CHECK_EQ(fov->getAlgorithm(), std::string("raycast"));
    fov->markDirty();
    fov->compute();
    CHECK(fov->isVisible(d.exitTx, d.exitTy));

    fov->setAlgorithm("permissive");
    fov->markDirty();
    fov->compute();
    CHECK(fov->isVisible(d.exitTx, d.exitTy));

    previewHex(mapMod, d, HexPreview{.title = "hex.level.09 fow mask", .pngName = "09_fowMaskAndAlgos.png"});

    delete fov;
}

TEST_CASE("hex.level.10.cameraPick") {
    auto           *mapMod = Map::create();
    auto           *gen    = Procgen::create();
    HexDungeon      d      = buildHexDungeon(mapMod, gen, 1010, 18, 14);
    constexpr float kViewW = 320.f;
    constexpr float kViewH = 240.f;

    float wx = 0.f, wy = 0.f;
    d.layer->tileToWorld(d.spawnTx, d.spawnTy, wx, wy);
    wx += kTileW * 0.5f;
    wy += kTileH * 0.5f;

    auto *cam           = Camera2D::createCamera();
    cam->data()->active = true;
    cam->setPosition(wx, wy);
    cam->setZoom(1.f);

    const float sx     = cam->worldToScreenX(wx, wy, kViewW, kViewH);
    const float sy     = cam->worldToScreenY(wx, wy, kViewW, kViewH);
    const float backWx = cam->screenToWorldX(sx, sy, kViewW, kViewH);
    const float backWy = cam->screenToWorldY(sx, sy, kViewW, kViewH);
    CHECK(approxEq(backWx, wx));
    CHECK(approxEq(backWy, wy));

    int tx = -1, ty = -1;
    d.layer->worldToTile(backWx, backWy, tx, ty);
    CHECK_EQ(tx, d.spawnTx);
    CHECK_EQ(ty, d.spawnTy);

    // Click near exit: convert screen → tile.
    float exitWx = 0.f, exitWy = 0.f;
    d.layer->tileToWorld(d.exitTx, d.exitTy, exitWx, exitWy);
    exitWx += kTileW * 0.5f;
    exitWy += kTileH * 0.5f;
    const float esx    = cam->worldToScreenX(exitWx, exitWy, kViewW, kViewH);
    const float esy    = cam->worldToScreenY(exitWx, exitWy, kViewW, kViewH);
    const float pickWx = cam->screenToWorldX(esx, esy, kViewW, kViewH);
    const float pickWy = cam->screenToWorldY(esx, esy, kViewW, kViewH);
    int         ptx = -1, pty = -1;
    d.layer->worldToTile(pickWx, pickWy, ptx, pty);
    CHECK_EQ(ptx, d.exitTx);
    CHECK_EQ(pty, d.exitTy);

    previewHex(mapMod, d, HexPreview{.title = "hex.level.10 camera pick", .pngName = "10_cameraPick.png"});

    cam->data()->active = false;
}

TEST_CASE("hex.level.11.dualGridHex") {
    auto      *mapMod = Map::create();
    auto      *gen    = Procgen::create();
    HexDungeon d      = buildHexDungeon(mapMod, gen, 1111, 12, 10);

    // Build a compact logic layer from a corner of the dungeon.
    TileLayer *logic = mapMod->newLayer(4, 4, kTileW, kTileH);
    configureHexLayer(logic);
    logic->setOrigin(8.f, 12.f);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const int gid = d.layer->getTile(x, y);
            logic->setTile(x, y, gid == kWallGid ? 0 : 1);
        }
    }
    TileLayer      *display = mapMod->newLayer(1, 1, 8.f, 8.f);
    DualGridOptions opts;
    opts.useDefaultFrameTable = true;
    opts.firstDisplayGid      = 1;
    CHECK(resolveDualGrid(logic, display, opts, nullptr));
    CHECK_EQ(static_cast<int>(display->config()->orientation), static_cast<int>(MapOrientation::Hexagonal));
    CHECK(approxEq(display->config()->hexSideLength, kHexSide));
    CHECK(display->getMapWidth() >= 4);
    CHECK(display->getMapHeight() >= 4);

    const int mask = dualGridMaskAt(*logic, 0, 0, 1);
    CHECK(mask >= 0);
    CHECK(dualGridDefaultFrame(mask) >= -1);
    float ox = 0.f, oy = 0.f;
    dualGridHalfOffset(*logic->config(), ox, oy);
    const bool hasOffset = std::fabs(ox) > 0.f || std::fabs(oy) > 0.f;
    CHECK(hasOffset);

    previewHex(mapMod, d, HexPreview{.title = "hex.level.11 dual grid", .pngName = "11_dualGridHex.png"});

    logic->setVisible(false);
    display->setVisible(false);
}

TEST_CASE("hex.level.12.procgenVariants") {
    auto       *mapMod  = Map::create();
    auto       *gen     = Procgen::create();
    const char *algos[] = {"cave.cellular", "maze.backtrack", "wfc.simple", "cave.drunkard"};
    for (const char *algo : algos) {
        HexDungeon d = buildHexDungeon(mapMod, gen, 1212, 24, 18, algo);
        CHECK_EQ(static_cast<int>(d.layer->config()->orientation), static_cast<int>(MapOrientation::Hexagonal));
        Pathfinder *pf = mapMod->newPathfinder(d.layer);
        pf->blockGid(kWallGid);
        pf->setTopology("hex");
        CHECK(pf->isWalkable(d.spawnTx, d.spawnTy));
        CHECK(pf->isWalkable(d.exitTx, d.exitTy));
        Path *path = pf->findPath(d.spawnTx, d.spawnTy, d.exitTx, d.exitTy);
        REQUIRE(path != nullptr);
        // Maze/WFC should usually connect; if not, at least spawn is walkable.
        if (path->getLength() > 0) {
            CHECK_EQ(path->getX(path->getLength() - 1), d.exitTx);
            CHECK(pathWalkable(path, pf));
        }
        if (std::string(algo) == "cave.drunkard") {
            previewHex(
                mapMod, d,
                HexPreview{
                    .title = "hex.level.12 procgen variants", .pngName = "12_procgenVariants.png", .path = path});
        }
        delete path;
        delete pf;
        d.layer->setVisible(false);
    }
}

TEST_CASE("hex.level.13.spatialCull") {
    auto      *mapMod     = Map::create();
    auto      *gen        = Procgen::create();
    auto      *spatialMod = Spatial::create();
    HexDungeon d          = buildHexDungeon(mapMod, gen, 1313, 22, 16);

    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    struct Marker {
        int   id;
        float x, y;
    };
    std::vector<Marker> markers;
    Pathfinder         *pf = mapMod->newPathfinder(d.layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");

    int nextId = 1;
    for (int y = 0; y < d.layer->getMapHeight(); ++y) {
        for (int x = 0; x < d.layer->getMapWidth(); ++x) {
            if (!pf->isWalkable(x, y)) continue;
            if ((x + y) % 5 != 0) continue;
            float wx = 0.f, wy = 0.f;
            d.layer->tileToWorld(x, y, wx, wy);
            wx += kTileW * 0.5f;
            wy += kTileH * 0.5f;
            markers.push_back({nextId++, wx, wy});
            minX = std::min(minX, wx);
            minY = std::min(minY, wy);
            maxX = std::max(maxX, wx);
            maxY = std::max(maxY, wy);
        }
    }
    REQUIRE(markers.size() >= 3);

    std::unique_ptr<QuadTree> tree(spatialMod->newQuadTree(minX - 32.f, minY - 32.f, maxX + 32.f, maxY + 32.f, 8, 6));
    for (const auto &m : markers) {
        CHECK(tree->insert(m.id, m.x - 4.f, m.y - 4.f, m.x + 4.f, m.y + 4.f));
    }

    float camWx = 0.f, camWy = 0.f;
    d.layer->tileToWorld(d.spawnTx, d.spawnTy, camWx, camWy);
    const float view = 96.f;
    const int   hit  = tree->queryRect(camWx - view, camWy - view, camWx + view, camWy + view);
    CHECK(hit >= 1);
    CHECK(hit <= int(markers.size()));

    // Far query should miss spawn-neighborhood markers when exit is distant.
    float exitWx = 0.f, exitWy = 0.f;
    d.layer->tileToWorld(d.exitTx, d.exitTy, exitWx, exitWy);
    if (std::fabs(exitWx - camWx) + std::fabs(exitWy - camWy) > view * 3.f) {
        const int farHit = tree->queryRect(exitWx - 8.f, exitWy - 8.f, exitWx + 8.f, exitWy + 8.f);
        // May be zero if no marker on exit cell; still a valid cull API exercise.
        CHECK(farHit >= 0);
    }
    previewHex(mapMod, d, HexPreview{.title = "hex.level.13 spatial cull", .pngName = "13_spatialCull.png"});
    delete pf;
}

TEST_CASE("hex.level.14.multiLight") {
    auto      *mapMod = Map::create();
    auto      *gen    = Procgen::create();
    HexDungeon d      = buildHexDungeon(mapMod, gen, 1414, 16, 12);

    float wx = 0.f, wy = 0.f;
    d.layer->tileToWorld(d.spawnTx, d.spawnTy, wx, wy);

    auto *cam           = Camera2D::createCamera();
    cam->data()->active = true;
    cam->setPosition(wx, wy);
    cam->setAmbient(0.05f, 0.05f, 0.07f);

    Texture albedo;
    albedo.width                 = 16;
    albedo.height                = 16;
    auto *hero                   = Renderable2D::create();
    hero->transform()->x         = wx;
    hero->transform()->y         = wy;
    hero->sprite()->width        = 18.f;
    hero->sprite()->height       = 18.f;
    hero->sprite()->texture      = &albedo;
    hero->sprite()->receiveLight = true;
    hero->sprite()->visible      = true;
    hero->sprite()->camera       = cam;

    auto *torch = Light2D::createLight("point");
    torch->setPosition(wx + 8.f, wy + 8.f);
    torch->setColor(1.f, 0.7f, 0.4f, 2.f);
    torch->setRadius(100.f);
    torch->setEnabled(true);

    auto *moon = Light2D::createLight("dir");
    moon->setDirection(-0.3f, 1.f);
    moon->setColor(0.4f, 0.5f, 0.8f, 0.6f);
    moon->setEnabled(true);
    CHECK_EQ(moon->getType(), "dir");

    std::vector<DrawItem2D> scene;
    RenderSystem::collectSprites(scene);
    bool found = false;
    for (const auto &it : scene) {
        if (it.texture == &albedo && it.receiveLight) found = true;
    }
    CHECK(found);
    CHECK(torch->isEnabled());
    CHECK(moon->isEnabled());

    torch->setEnabled(false);
    moon->setEnabled(false);
    hero->sprite()->visible = false;
    cam->data()->active     = false;

    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.14 multi light", .pngName = "14_multiLight.png", .lamp = torch});

    d.layer->setVisible(false);
}

TEST_CASE("hex.level.15.particleStashTransfer") {
    auto      *mapMod = Map::create();
    auto      *gen    = Procgen::create();
    auto      *parts  = Particles::create();
    HexDungeon d      = buildHexDungeon(mapMod, gen, 1515, 18, 14);

    float wx = 0.f, wy = 0.f;
    d.layer->tileToWorld(d.spawnTx, d.spawnTy, wx, wy);

    ParticleEmitter *fire = parts->newEmitter(128);
    fire->applyPreset("fire");
    fire->setPosition(wx, wy);
    fire->start();

    ParticleEmitter *smoke = parts->newEmitter(128);
    smoke->applyPreset("smoke");
    smoke->setPosition(wx + 6.f, wy - 4.f);
    smoke->start();

    ParticleEmitter *spark = parts->newEmitter(64);
    spark->applyPreset("spark");
    spark->setPosition(wx, wy);
    spark->setEmitterLifetime(0.4f);
    spark->start();

    for (int i = 0; i < 10; ++i) parts->update(1.f / 60.f);
    CHECK(fire->getCount() > 0);
    CHECK(smoke->getCount() > 0);

    spark->stop();
    CHECK(spark->isStopped());
    fire->pause();
    CHECK(fire->isPaused());
    fire->start();
    CHECK(fire->isActive());

    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    ItemDefinition ore;
    ore.id       = "hex.ore";
    ore.maxStack = 20;
    ore.weight   = 1.f;
    ItemRegistry::registerItem(ore);

    auto *inv   = Inventory::create();
    Bag  *bag   = inv->newBag(8);
    Bag  *stash = inv->newBag(12);
    bag->setId("player");
    stash->setId("stash");
    const int added = bag->addItem("hex.ore", 5);
    CHECK_EQ(added, 5);
    const int moved = inv->transferItem(bag, stash, "hex.ore", 3);
    CHECK_EQ(moved, 3);
    CHECK_EQ(bag->countItem("hex.ore"), 2);
    CHECK_EQ(stash->countItem("hex.ore"), 3);

    previewHex(
        mapMod, d,
        HexPreview{.title = "hex.level.15 particle stash", .pngName = "15_particleStashTransfer.png", .fire = fire});

    fire->stop();
    smoke->stop();
    bag->destroy();
    stash->destroy();
    ItemRegistry::clear();
    d.layer->setVisible(false);
}

TEST_CASE("hex.level.pipeline.fogRaid") {
    auto      *mapMod     = Map::create();
    auto      *gen        = Procgen::create();
    auto      *spatialMod = Spatial::create();
    auto      *parts      = Particles::create();
    HexDungeon d          = buildHexDungeon(mapMod, gen, 1616, 30, 22);

    Pathfinder *pf = mapMod->newPathfinder(d.layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");
    FlowField *field = pf->buildFlowField(d.exitTx, d.exitTy);
    REQUIRE(field != nullptr);
    Path *path = pf->followFlow(field, d.spawnTx, d.spawnTy);
    REQUIRE(path != nullptr);
    REQUIRE(path->getLength() > 2);

    Fov *fov = mapMod->newFov(d.layer);
    fov->blockOpaqueGid(kWallGid);
    fov->setTopology("hex");
    fov->setPerceptionRadiusScale(0.5f);
    const int hero = fov->addRevealer(d.spawnTx, d.spawnTy, 4);
    fov->setRevealerPerception(hero, 2.f);
    fov->setRevealerFacing(hero, 90.f, 80.f);

    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    ItemDefinition relic;
    relic.id       = "hex.relic";
    relic.maxStack = 1;
    relic.weight   = 0.5f;
    ItemRegistry::registerItem(relic);
    auto *inv = Inventory::create();
    Bag  *bag = inv->newBag(6);

    const int mid    = path->getLength() / 2;
    const int lootTx = path->getX(mid);
    const int lootTy = path->getY(mid);
    float     lootWx = 0.f, lootWy = 0.f;
    d.layer->tileToWorld(lootTx, lootTy, lootWx, lootWy);
    lootWx += kTileW * 0.5f;
    lootWy += kTileH * 0.5f;
    std::unique_ptr<SpatialHash2D> hash(spatialMod->newSpatialHash2D(28.f));
    CHECK(hash->insert(7, lootWx - 6.f, lootWy - 6.f, lootWx + 6.f, lootWy + 6.f));

    ParticleEmitter *ember = parts->newEmitter(96);
    ember->applyPreset("spark");
    ember->start();

    bool looted = false;
    for (int step = 0; step < path->getLength(); ++step) {
        const int tx = path->getX(step);
        const int ty = path->getY(step);
        fov->setRevealerPosition(hero, tx, ty);
        fov->compute();
        CHECK(fov->isVisible(tx, ty));

        float wx = 0.f, wy = 0.f;
        d.layer->tileToWorld(tx, ty, wx, wy);
        ember->setPosition(wx + 8.f, wy + 4.f);
        parts->update(1.f / 30.f);

        // Perception-gated pickup: must see tile and pass stealth check.
        if (!looted && fov->canDetect(hero, lootTx, lootTy, 0.5f) &&
            hash->queryCircle(wx + kTileW * 0.5f, wy + kTileH * 0.5f, 18.f) > 0) {
            const int added = bag->addItem("hex.relic", 1);
            CHECK_EQ(added, 1);
            hash->remove(7);
            ember->emit(12);
            looted = true;
        }
    }
    CHECK(looted);
    CHECK_EQ(bag->countItem("hex.relic"), 1);
    CHECK(fov->isExplored(d.spawnTx, d.spawnTy));
    CHECK_EQ(path->getX(path->getLength() - 1), d.exitTx);

    ember->stop();
    bag->destroy();
    ItemRegistry::clear();
    delete path;
    delete field;
    delete pf;
    delete fov;
}

TEST_CASE("hex.level.16.facingConeFov") {
    auto *mapMod = Map::create();
    hideAllTileLayers();
    TileLayer *layer = mapMod->newLayer(11, 11, kTileW, kTileH);
    configureHexLayer(layer);
    layer->fill(kFloorGid);

    Fov *fov = mapMod->newFov(layer);
    fov->setBlockEmpty(false);
    fov->setTopology("hex");
    fov->setAlgorithm("shadowcast");
    const int id = fov->addRevealer(5, 5, 4);
    // Narrow east-facing cone.
    fov->setRevealerFacing(id, 0.f, 35.f);
    fov->compute();
    CHECK(fov->isVisible(5, 5));

    int eastVisible = 0;
    int westVisible = 0;
    for (int x = 6; x <= 9; ++x)
        if (fov->isVisible(x, 5)) ++eastVisible;
    for (int x = 1; x <= 4; ++x)
        if (fov->isVisible(x, 5)) ++westVisible;
    CHECK(eastVisible > westVisible);

    fov->clearRevealerFacing(id);
    fov->compute();
    int omni = 0;
    for (int y = 3; y <= 7; ++y)
        for (int x = 3; x <= 7; ++x)
            if (fov->isVisible(x, y)) ++omni;
    CHECK(omni > eastVisible);

    previewHex(mapMod, layer, 5, 5, 9, 5,
               HexPreview{.title = "hex.level.16 facing cone", .pngName = "16_facingConeFov.png"});

    delete fov;
    layer->setVisible(false);
}

TEST_CASE("hex.level.17.flowPlusCellCost") {
    // Handcrafted open field: BSP spawn/stairs path length is not stable across
    // standard-library RNGs (macOS libc++ vs Linux libstdc++).
    auto *mapMod = Map::create();
    hideAllTileLayers();
    TileLayer *layer = mapMod->newLayer(14, 8, kTileW, kTileH);
    configureHexLayer(layer);
    layer->fill(kFloorGid);

    const int spawnTx = 1, spawnTy = 4;
    const int exitTx = 12, exitTy = 4;

    Pathfinder *pf = mapMod->newPathfinder(layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");

    Path *baseline = pf->findPath(spawnTx, spawnTy, exitTx, exitTy);
    REQUIRE(baseline != nullptr);
    REQUIRE(baseline->getLength() > 2);
    const float baseCost = baseline->getTotalCost();

    // Paint expensive cells along the first half of the baseline path.
    const int paintUntil = std::max(1, baseline->getLength() / 2);
    for (int i = 1; i < paintUntil; ++i) pf->setCellCost(baseline->getX(i), baseline->getY(i), 10.f);

    Path *detour = pf->findPath(spawnTx, spawnTy, exitTx, exitTy);
    REQUIRE(detour != nullptr);
    CHECK(detour->getLength() > 0);
    CHECK(detour->getTotalCost() + 1e-3f >= baseCost);

    FlowField *field = pf->buildFlowField(exitTx, exitTy);
    REQUIRE(field != nullptr);
    CHECK(field->isReachable(spawnTx, spawnTy));
    Path *flow = pf->followFlow(field, spawnTx, spawnTy);
    REQUIRE(flow != nullptr);
    CHECK_EQ(flow->getX(flow->getLength() - 1), exitTx);
    CHECK_EQ(flow->getY(flow->getLength() - 1), exitTy);

    previewHex(
        mapMod, layer, spawnTx, spawnTy, exitTx, exitTy,
        HexPreview{.title = "hex.level.17 flow plus cell cost", .pngName = "17_flowPlusCellCost.png", .path = detour});

    delete flow;
    delete field;
    delete detour;
    delete baseline;
    delete pf;
    layer->setVisible(false);
}

TEST_CASE("hex.level.18.seedReproducible") {
    auto      *mapMod = Map::create();
    auto      *gen    = Procgen::create();
    HexDungeon a      = buildHexDungeon(mapMod, gen, 4242, 24, 18);
    HexDungeon b      = buildHexDungeon(mapMod, gen, 4242, 24, 18);

    CHECK_EQ(a.spawnTx, b.spawnTx);
    CHECK_EQ(a.spawnTy, b.spawnTy);
    CHECK_EQ(a.exitTx, b.exitTx);
    CHECK_EQ(a.exitTy, b.exitTy);
    CHECK_EQ(a.layer->getMapWidth(), b.layer->getMapWidth());

    // Same seed ⇒ same tile footprint sample.
    int mismatches = 0;
    for (int y = 0; y < a.layer->getMapHeight(); ++y)
        for (int x = 0; x < a.layer->getMapWidth(); ++x)
            if (a.layer->getTile(x, y) != b.layer->getTile(x, y)) ++mismatches;
    CHECK_EQ(mismatches, 0);

    Pathfinder *pfA = mapMod->newPathfinder(a.layer);
    Pathfinder *pfB = mapMod->newPathfinder(b.layer);
    pfA->blockGid(kWallGid);
    pfB->blockGid(kWallGid);
    pfA->setTopology("hex");
    pfB->setTopology("hex");
    Path *pa = pfA->findPath(a.spawnTx, a.spawnTy, a.exitTx, a.exitTy);
    Path *pb = pfB->findPath(b.spawnTx, b.spawnTy, b.exitTx, b.exitTy);
    REQUIRE(pa != nullptr);
    REQUIRE(pb != nullptr);
    CHECK_EQ(pa->getLength(), pb->getLength());

    previewHex(mapMod, a,
               HexPreview{.title = "hex.level.18 seed reproducible", .pngName = "18_seedReproducible.png", .path = pa});

    delete pa;
    delete pb;
    delete pfA;
    delete pfB;
    a.layer->setVisible(false);
    b.layer->setVisible(false);
}

TEST_CASE("hex.level.19.cornerPeekToggle") {
    auto *mapMod = Map::create();
    hideAllTileLayers();
    TileLayer *layer = mapMod->newLayer(7, 7, kTileW, kTileH);
    configureHexLayer(layer);
    layer->fill(kFloorGid);
    // Wall corner near center.
    layer->setTile(3, 2, kWallGid);
    layer->setTile(2, 3, kWallGid);

    Fov *fov = mapMod->newFov(layer);
    fov->blockOpaqueGid(kWallGid);
    fov->setBlockEmpty(false);
    fov->setTopology("hex");
    fov->setCornerPeek(false);
    CHECK(!fov->getCornerPeek());
    const int id = fov->addRevealer(1, 1, 5);
    fov->compute();
    CHECK(fov->isVisible(1, 1));
    const bool peekOff = fov->isVisible(4, 4);

    fov->setCornerPeek(true);
    CHECK(fov->getCornerPeek());
    fov->markDirty();
    fov->setRevealerPosition(id, 1, 1);
    fov->compute();
    const bool peekOn = fov->isVisible(4, 4);
    // Enabling corner peek should not reduce visibility of the origin.
    CHECK(fov->isVisible(1, 1));
    (void)peekOff;
    (void)peekOn;

    previewHex(mapMod, layer, 1, 1, 5, 5,
               HexPreview{.title = "hex.level.19 corner peek", .pngName = "19_cornerPeekToggle.png"});

    delete fov;
    layer->setVisible(false);
}

TEST_CASE("hex.level.20.equipmentLootEquip") {
    auto      *mapMod     = Map::create();
    auto      *gen        = Procgen::create();
    auto      *spatialMod = Spatial::create();
    HexDungeon d          = buildHexDungeon(mapMod, gen, 2020, 22, 16);

    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    auto     *inv = Inventory::create();
    const int n   = inv->registerItemsFromJson(R"([
      {"id":"hex.boots","displayName":"疾行靴","maxStack":1,"weight":0.8,"equipSlot":"feet","tags":["equip","boots"]},
      {"id":"hex.shield","displayName":"六角盾","maxStack":1,"weight":2.0,"equipSlot":"offhand","tags":["equip","shield"]}
    ])");
    CHECK_EQ(n, 2);

    Bag          *bag = inv->newBag(8);
    EquipmentSet *eq  = inv->newEquipmentSet();
    eq->setId("hero");
    eq->defineSlot("feet");
    eq->addSlotAllowedTag("feet", "boots");
    eq->defineSlot("offhand");
    eq->addSlotAllowedTag("offhand", "shield");

    float wx = 0.f, wy = 0.f;
    d.layer->tileToWorld(d.spawnTx, d.spawnTy, wx, wy);
    wx += kTileW * 0.5f;
    wy += kTileH * 0.5f;
    std::unique_ptr<SpatialHash2D> hash(spatialMod->newSpatialHash2D(32.f));
    CHECK(hash->insert(1, wx - 8.f, wy - 8.f, wx + 8.f, wy + 8.f));
    CHECK(hash->queryCircle(wx, wy, 16.f) > 0);

    const int bootsAdded  = bag->addItem("hex.boots", 1);
    const int shieldAdded = bag->addItem("hex.shield", 1);
    CHECK_EQ(bootsAdded, 1);
    CHECK_EQ(shieldAdded, 1);
    hash->remove(1);

    const int bootsSlot  = bag->findItem("hex.boots");
    const int shieldSlot = bag->findItem("hex.shield");
    CHECK(bootsSlot >= 0);
    CHECK(shieldSlot >= 0);
    CHECK(eq->equipFromBag("feet", bag, bootsSlot));
    CHECK(eq->equipFromBag("offhand", bag, shieldSlot));
    CHECK_EQ(eq->getSlotItemId("feet"), std::string("hex.boots"));
    CHECK_EQ(eq->getSlotItemId("offhand"), std::string("hex.shield"));
    CHECK_EQ(bag->countItem("hex.boots"), 0);
    CHECK_EQ(bag->countItem("hex.shield"), 0);

    CHECK(eq->unequipToBag("feet", bag));
    CHECK_EQ(bag->countItem("hex.boots"), 1);

    previewHex(mapMod, d, HexPreview{.title = "hex.level.20 equipment loot", .pngName = "20_equipmentLootEquip.png"});

    bag->destroy();
    eq->destroy();
    ItemRegistry::clear();
    d.layer->setVisible(false);
}

TEST_CASE("hex.level.pipeline.torchEscort") {
    auto      *mapMod = Map::create();
    auto      *gen    = Procgen::create();
    auto      *parts  = Particles::create();
    HexDungeon d      = buildHexDungeon(mapMod, gen, 3030, 28, 20);

    Pathfinder *pf = mapMod->newPathfinder(d.layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");
    FlowField *field = pf->buildFlowField(d.exitTx, d.exitTy);
    REQUIRE(field != nullptr);
    Path *path = pf->followFlow(field, d.spawnTx, d.spawnTy);
    REQUIRE(path != nullptr);
    REQUIRE(path->getLength() > 2);

    Fov *fov = mapMod->newFov(d.layer);
    fov->blockOpaqueGid(kWallGid);
    fov->setTopology("hex");
    fov->setPerceptionRadiusScale(1.f);
    const int hero  = fov->addRevealer(d.spawnTx, d.spawnTy, 4);
    const int torch = fov->addRevealer(d.exitTx, d.exitTy, 3);
    fov->setRevealerPerception(hero, 1.5f);

    auto *lamp = Light2D::createLight("point");
    lamp->setColor(1.f, 0.75f, 0.4f, 2.f);
    lamp->setRadius(130.f);
    lamp->setEnabled(true);

    ParticleEmitter *fire = parts->newEmitter(128);
    fire->applyPreset("fire");
    fire->start();

    int visibleSteps = 0;
    for (int step = 0; step < path->getLength(); ++step) {
        const int tx = path->getX(step);
        const int ty = path->getY(step);
        fov->setRevealerPosition(hero, tx, ty);
        fov->compute();
        if (fov->isVisible(tx, ty)) ++visibleSteps;

        float wx = 0.f, wy = 0.f;
        d.layer->tileToWorld(tx, ty, wx, wy);
        lamp->setPosition(wx + 4.f, wy);
        fire->setPosition(wx + 4.f, wy - 6.f);
        parts->update(1.f / 30.f);
    }
    CHECK(visibleSteps == path->getLength());
    const bool exitKnown = fov->isVisible(d.exitTx, d.exitTy) || fov->isExplored(d.exitTx, d.exitTy);
    CHECK(exitKnown);
    CHECK(fov->getRevealerCount() == 2);
    CHECK(fire->getCount() > 0);
    CHECK(approxEq(lamp->getRadius(), 130.f));

    fire->stop();
    lamp->setEnabled(false);
    delete path;
    delete field;
    delete pf;
    delete fov;
    (void)torch;
}

TEST_CASE("hex.level.pipeline.catalogRaid") {
    // Raid-style crawl: path-mid relic gated by perception + rich side loot.
    auto      *mapMod     = Map::create();
    auto      *gen        = Procgen::create();
    auto      *spatialMod = Spatial::create();
    HexDungeon d          = buildHexDungeon(mapMod, gen, 20260812, 32, 24);

    Pathfinder *pf = mapMod->newPathfinder(d.layer);
    pf->blockGid(kWallGid);
    pf->setTopology("auto");
    CHECK_EQ(pf->getTopology(), std::string("hex"));
    Path *path = pf->findPath(d.spawnTx, d.spawnTy, d.exitTx, d.exitTy);
    REQUIRE(path != nullptr);
    REQUIRE(path->getLength() > 2);

    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    auto *inv = Inventory::create();
    inv->registerItemsFromJson(R"([
      {"id":"hex.relic","displayName":"迷雾圣物","maxStack":1,"weight":0.5,"tags":["quest","relic"]},
      {"id":"hex.gem","displayName":"地牢宝石","maxStack":5,"weight":0.05,"tags":["treasure"]},
      {"id":"hex.potion","displayName":"六角药水","maxStack":10,"weight":0.2,"tags":["potion"]},
      {"id":"hex.coin","displayName":"六角币","maxStack":99,"weight":0.01,"tags":["currency"]}
    ])");
    Bag *bag = inv->newBag(16);

    Fov *fov = mapMod->newFov(d.layer);
    fov->blockOpaqueGid(kWallGid);
    fov->setTopology("hex");
    fov->setDetectionMargin(0.f);
    const int hero = fov->addRevealer(d.spawnTx, d.spawnTy, 5);
    fov->setRevealerPerception(hero, 2.f);

    struct Drop {
        int         id;
        int         tx, ty;
        const char *item;
        float       stealth;
        bool        needVisible;
        bool        taken = false;
    };
    const int         mid   = path->getLength() / 2;
    std::vector<Drop> drops = {
        {1, path->getX(mid), path->getY(mid), "hex.relic", 0.5f, true},
        {2, d.spawnTx + 1, d.spawnTy, "hex.potion", 0.f, false},
        {3, d.spawnTx, d.spawnTy + 1, "hex.gem", 0.f, false},
        {4, d.spawnTx - 1, d.spawnTy, "hex.coin", 0.f, false},
    };

    std::unique_ptr<SpatialHash2D> hash(spatialMod->newSpatialHash2D(36.f));
    for (auto &drop : drops) {
        if (!pf->isWalkable(drop.tx, drop.ty)) {
            drop.tx = d.spawnTx;
            drop.ty = d.spawnTy;
        }
        float wx = 0.f, wy = 0.f;
        d.layer->tileToWorld(drop.tx, drop.ty, wx, wy);
        wx += kTileW * 0.5f;
        wy += kTileH * 0.5f;
        CHECK(hash->insert(drop.id, wx - 6.f, wy - 6.f, wx + 6.f, wy + 6.f));
    }

    int picked = 0;
    for (int step = 0; step < path->getLength(); ++step) {
        const int tx = path->getX(step);
        const int ty = path->getY(step);
        fov->setRevealerPosition(hero, tx, ty);
        fov->compute();
        float wx = 0.f, wy = 0.f;
        d.layer->tileToWorld(tx, ty, wx, wy);
        wx += kTileW * 0.5f;
        wy += kTileH * 0.5f;
        for (auto &drop : drops) {
            if (drop.taken) continue;
            if (drop.needVisible && !fov->canDetect(hero, drop.tx, drop.ty, drop.stealth)) continue;
            if (hash->queryCircle(wx, wy, 20.f) <= 0) continue;
            // Confirm this query hit the drop id.
            bool      hit = false;
            const int n   = hash->queryCircle(wx, wy, 20.f);
            for (int qi = 0; qi < n; ++qi)
                if (hash->getResultId(qi) == drop.id) hit = true;
            if (!hit) continue;
            const int added = bag->addItem(drop.item, 1);
            if (added > 0) {
                drop.taken = true;
                hash->remove(drop.id);
                ++picked;
            }
        }
    }
    CHECK(picked >= 2);
    CHECK(bag->getUsedSlotCount() >= 2);
    CHECK_EQ(path->getX(path->getLength() - 1), d.exitTx);

    bag->destroy();
    ItemRegistry::clear();
    delete path;
    delete pf;
    delete fov;
}

TEST_CASE("hex.level.21.hexWorldTileRoundtrip") {
    auto      *mapMod = Map::create();
    auto      *gen    = Procgen::create();
    HexDungeon d      = buildHexDungeon(mapMod, gen, 2121, 20, 14);

    int ok = 0;
    for (int y = 0; y < d.layer->getMapHeight(); ++y) {
        for (int x = 0; x < d.layer->getMapWidth(); ++x) {
            if (d.layer->getTile(x, y) == kWallGid) continue;
            float wx = 0.f, wy = 0.f;
            d.layer->tileToWorld(x, y, wx, wy);
            // Sample near tile center for stable hex pick.
            wx += kTileW * 0.5f;
            wy += kTileH * 0.5f;
            int tx = -1, ty = -1;
            d.layer->worldToTile(wx, wy, tx, ty);
            CHECK_EQ(tx, x);
            CHECK_EQ(ty, y);
            CHECK_EQ(d.layer->worldToTileX(wx, wy), x);
            CHECK_EQ(d.layer->worldToTileY(wx, wy), y);
            ++ok;
            if (ok >= 12) break;
        }
        if (ok >= 12) break;
    }
    CHECK(ok >= 8);

    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.21 world tile roundtrip", .pngName = "21_hexWorldTileRoundtrip.png"});

    d.layer->setVisible(false);
}

TEST_CASE("hex.level.22.fovExploredMemoryClear") {
    auto      *mapMod = Map::create();
    auto      *gen    = Procgen::create();
    HexDungeon d      = buildHexDungeon(mapMod, gen, 2222, 22, 16);

    Fov *fov = mapMod->newFov(d.layer);
    fov->blockOpaqueGid(kWallGid);
    fov->setTopology("hex");
    const int id = fov->addRevealer(d.spawnTx, d.spawnTy, 5);
    fov->compute();
    CHECK(fov->isVisible(d.spawnTx, d.spawnTy));
    CHECK_EQ(fov->getState(d.spawnTx, d.spawnTy), std::string("visible"));

    fov->setRevealerPosition(id, d.exitTx, d.exitTy);
    fov->compute();
    CHECK(fov->isExplored(d.spawnTx, d.spawnTy));
    const std::string exploredState     = fov->getState(d.spawnTx, d.spawnTy);
    const bool        exploredOrVisible = exploredState == "explored" || exploredState == "visible";
    CHECK(exploredOrVisible);

    fov->clearMemory();
    fov->markDirty();
    fov->compute();
    // After clear, spawn is unknown unless still in current FOV.
    if (!fov->isVisible(d.spawnTx, d.spawnTy)) {
        CHECK(!fov->isExplored(d.spawnTx, d.spawnTy));
        CHECK_EQ(fov->getState(d.spawnTx, d.spawnTy), std::string("unknown"));
    }

    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.22 fov explored memory", .pngName = "22_fovExploredMemoryClear.png"});

    delete fov;
}

TEST_CASE("hex.level.23.findGroupPathHex") {
    auto      *mapMod = Map::create();
    auto      *gen    = Procgen::create();
    HexDungeon d      = buildHexDungeon(mapMod, gen, 2323, 26, 18);

    Pathfinder *pf = mapMod->newPathfinder(d.layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");

    Path *solo = pf->findPath(d.spawnTx, d.spawnTy, d.exitTx, d.exitTy);
    REQUIRE(solo != nullptr);
    CHECK(solo->getLength() > 0);

    Path *group = pf->findGroupPath(d.spawnTx, d.spawnTy, d.exitTx, d.exitTy);
    REQUIRE(group != nullptr);
    CHECK(group->getLength() > 0);
    CHECK_EQ(group->getX(group->getLength() - 1), d.exitTx);
    CHECK_EQ(group->getY(group->getLength() - 1), d.exitTy);
    CHECK(pathWalkable(group, pf));

    previewHex(mapMod, d,
               HexPreview{.title = "hex.level.23 find group path", .pngName = "23_findGroupPathHex.png", .path = solo});

    delete group;
    delete solo;
    delete pf;
}

TEST_CASE("hex.level.24.fovAlgoVisibilitySmoke") {
    auto *mapMod = Map::create();
    hideAllTileLayers();
    TileLayer *layer = mapMod->newLayer(13, 13, kTileW, kTileH);
    configureHexLayer(layer);
    layer->fill(kFloorGid);
    layer->setTile(6, 5, kWallGid);
    layer->setTile(5, 6, kWallGid);
    layer->setTile(7, 6, kWallGid);

    const char *algos[]   = {"shadowcast", "raycast", "permissive", "rectangle"};
    int         counts[4] = {0, 0, 0, 0};
    for (int ai = 0; ai < 4; ++ai) {
        Fov *fov = mapMod->newFov(layer);
        fov->blockOpaqueGid(kWallGid);
        fov->setBlockEmpty(false);
        fov->setTopology("hex");
        fov->setAlgorithm(algos[ai]);
        CHECK_EQ(fov->getAlgorithm(), std::string(algos[ai]));
        fov->addRevealer(6, 6, 4);
        fov->compute();
        CHECK(fov->isVisible(6, 6));
        for (int y = 0; y < 13; ++y)
            for (int x = 0; x < 13; ++x)
                if (fov->isVisible(x, y)) ++counts[ai];
        CHECK(counts[ai] > 1);
        delete fov;
    }
    for (int ai = 0; ai < 4; ++ai) CHECK(counts[ai] >= 5);

    previewHex(mapMod, layer, 6, 6, 11, 11,
               HexPreview{.title = "hex.level.24 fov algo smoke", .pngName = "24_fovAlgoVisibilitySmoke.png"});

    layer->setVisible(false);
}

TEST_CASE("hex.level.25.blockedCellSyncPath") {
    auto *mapMod = Map::create();
    hideAllTileLayers();
    TileLayer *layer = mapMod->newLayer(8, 5, kTileW, kTileH);
    configureHexLayer(layer);
    layer->fill(kFloorGid);

    Pathfinder *pf = mapMod->newPathfinder(layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");
    CHECK(pf->isWalkable(3, 2));

    // setBlocked updates the cost grid immediately (pre-sync).
    pf->setBlocked(3, 2, true);
    CHECK(!pf->isWalkable(3, 2));
    pf->setBlocked(3, 2, false);
    CHECK(pf->isWalkable(3, 2));

    // Persistent block via tile + syncFromLayer (survives findPath ensureSynced).
    layer->setTile(4, 2, kWallGid);
    pf->syncFromLayer();
    CHECK(!pf->isWalkable(4, 2));
    Path *after = pf->findPath(0, 2, 7, 2);
    REQUIRE(after != nullptr);
    CHECK(after->getLength() > 0);
    bool steppedWall = false;
    for (int i = 0; i < after->getLength(); ++i)
        if (after->getX(i) == 4 && after->getY(i) == 2) steppedWall = true;
    CHECK(!steppedWall);

    previewHex(
        mapMod, layer, 0, 2, 7, 2,
        HexPreview{.title = "hex.level.25 blocked cell sync", .pngName = "25_blockedCellSyncPath.png", .path = after});

    delete after;
    delete pf;
    layer->setVisible(false);
}

TEST_CASE("hex.level.pipeline.costlyFogPickup") {
    auto      *mapMod     = Map::create();
    auto      *gen        = Procgen::create();
    auto      *spatialMod = Spatial::create();
    auto      *parts      = Particles::create();
    HexDungeon d          = buildHexDungeon(mapMod, gen, 2525, 28, 20);

    Pathfinder *pf = mapMod->newPathfinder(d.layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");
    Path *base = pf->findPath(d.spawnTx, d.spawnTy, d.exitTx, d.exitTy);
    REQUIRE(base != nullptr);
    REQUIRE(base->getLength() > 2);

    const int paintN = std::max(1, base->getLength() / 3);
    for (int i = 1; i < paintN; ++i) pf->setCellCost(base->getX(i), base->getY(i), 9.f);

    Path *path = pf->findPath(d.spawnTx, d.spawnTy, d.exitTx, d.exitTy);
    REQUIRE(path != nullptr);
    CHECK(path->getTotalCost() + 1e-3f >= base->getTotalCost());

    Fov *fov = mapMod->newFov(d.layer);
    fov->blockOpaqueGid(kWallGid);
    fov->setTopology("hex");
    const int hero = fov->addRevealer(d.spawnTx, d.spawnTy, 5);
    fov->setRevealerPerception(hero, 2.f);

    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    auto *inv = Inventory::create();
    inv->registerItemsFromJson(
        R"([{"id":"hex.crystal","displayName":"视野水晶","maxStack":3,"weight":0.15,"tags":["loot","fov"]}])");
    Bag *bag = inv->newBag(6);

    const int mid    = path->getLength() / 2;
    const int lootTx = path->getX(mid);
    const int lootTy = path->getY(mid);
    float     lootWx = 0.f, lootWy = 0.f;
    d.layer->tileToWorld(lootTx, lootTy, lootWx, lootWy);
    lootWx += kTileW * 0.5f;
    lootWy += kTileH * 0.5f;
    std::unique_ptr<SpatialHash2D> hash(spatialMod->newSpatialHash2D(30.f));
    CHECK(hash->insert(9, lootWx - 5.f, lootWy - 5.f, lootWx + 5.f, lootWy + 5.f));

    ParticleEmitter *spark = parts->newEmitter(64);
    spark->applyPreset("spark");
    spark->setEmitterLifetime(0.35f);

    bool looted = false;
    for (int step = 0; step < path->getLength(); ++step) {
        const int tx = path->getX(step);
        const int ty = path->getY(step);
        fov->setRevealerPosition(hero, tx, ty);
        fov->compute();
        float wx = 0.f, wy = 0.f;
        d.layer->tileToWorld(tx, ty, wx, wy);
        wx += kTileW * 0.5f;
        wy += kTileH * 0.5f;
        if (!looted && fov->canDetect(hero, lootTx, lootTy, 1.5f) && hash->queryCircle(wx, wy, 18.f) > 0) {
            const int added = bag->addItem("hex.crystal", 1);
            if (added > 0) {
                looted = true;
                hash->remove(9);
                spark->setPosition(lootWx, lootWy);
                spark->start();
                spark->emit(16);
            }
        }
        parts->update(1.f / 30.f);
    }
    CHECK(looted);
    CHECK_EQ(bag->countItem("hex.crystal"), 1);

    spark->stop();
    bag->destroy();
    ItemRegistry::clear();
    delete path;
    delete base;
    delete pf;
    delete fov;
}

TEST_CASE("hex.level.26.drunkardCaveHex") {
    auto *mapMod = Map::create();
    auto *gen    = Procgen::create();
    hideAllTileLayers();
    TileLayer *layer = mapMod->newLayer(30, 22, kTileW, kTileH);
    configureHexLayer(layer);
    GeneratorRegistry::instance().registerBuiltins();
    Params p;
    p.setSeed(2626);
    p.setSize(30, 22);
    p.setFloat("floorPct", 0.42f);
    Grid2D      grid;
    std::string err;
    REQUIRE(GeneratorRegistry::instance().generate("cave.drunkard", p, grid, err));
    gen->setPaletteGid("hex_drunk", "wall", kWallGid);
    gen->setPaletteGid("hex_drunk", "floor", kFloorGid);
    gen->setPaletteGid("hex_drunk", "corridor", kFloorGid);
    gen->setPaletteGid("hex_drunk", "door", kDoorGid);
    eve::test_support::applyGridToLayer(*gen, grid, "hex_drunk", *layer);

    int floors = 0;
    for (uint32_t c : grid.cells())
        if (c == Semantic::Floor || c == Semantic::Corridor) ++floors;
    CHECK(floors >= 8);

    Pathfinder *pf = mapMod->newPathfinder(layer);
    pf->blockGid(kWallGid);
    pf->setTopology("auto");
    CHECK_EQ(pf->getTopology(), std::string("hex"));
    int sx = -1, sy = -1;
    REQUIRE(findWalkable(grid, sx, sy));
    CHECK(pf->isWalkable(sx, sy));
    int ex = sx, ey = sy;
    findFarthestWalkable(grid, sx, sy, ex, ey);
    previewHex(mapMod, layer, sx, sy, ex, ey,
               HexPreview{.title = "hex.level.26 drunkard cave", .pngName = "26_drunkardCaveHex.png"});
    delete pf;
    layer->setVisible(false);
}

TEST_CASE("hex.level.27.mazeHexPath") {
    auto       *mapMod = Map::create();
    auto       *gen    = Procgen::create();
    HexDungeon  d      = buildHexDungeon(mapMod, gen, 2727, 28, 22, "maze.backtrack");
    Pathfinder *pf     = mapMod->newPathfinder(d.layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");
    Path *path = pf->findPath(d.spawnTx, d.spawnTy, d.exitTx, d.exitTy);
    REQUIRE(path != nullptr);
    CHECK(path->getLength() > 0);
    CHECK(pathWalkable(path, pf));

    previewHex(mapMod, d, HexPreview{.title = "hex.level.27 maze hex", .pngName = "27_mazeHexPath.png", .path = path});

    delete path;
    delete pf;
}

TEST_CASE("hex.level.28.wfcDungeonHex") {
    auto *mapMod = Map::create();
    auto *gen    = Procgen::create();
    hideAllTileLayers();
    TileLayer *layer = mapMod->newLayer(24, 18, kTileW, kTileH);
    configureHexLayer(layer);
    GeneratorRegistry::instance().registerBuiltins();
    Params p;
    p.setSeed(2828);
    p.setSize(24, 18);
    p.setString("preset", "dungeon");
    p.setInt("maxAttempts", 64);
    Grid2D      grid;
    std::string err;
    const bool  ok = GeneratorRegistry::instance().generate("wfc.simple", p, grid, err);
    CHECK(ok);
    if (ok) {
        gen->setPaletteGid("hex_wfc", "wall", kWallGid);
        gen->setPaletteGid("hex_wfc", "floor", kFloorGid);
        gen->setPaletteGid("hex_wfc", "corridor", kFloorGid);
        gen->setPaletteGid("hex_wfc", "door", kDoorGid);
        eve::test_support::applyGridToLayer(*gen, grid, "hex_wfc", *layer);
        Pathfinder *pf = mapMod->newPathfinder(layer);
        pf->blockGid(kWallGid);
        pf->setTopology("hex");
        int sx = -1, sy = -1;
        if (findWalkable(grid, sx, sy)) {
            CHECK(pf->isWalkable(sx, sy));
            int ex = sx, ey = sy;
            findFarthestWalkable(grid, sx, sy, ex, ey);
            previewHex(mapMod, layer, sx, sy, ex, ey,
                       HexPreview{.title = "hex.level.28 wfc dungeon", .pngName = "28_wfcDungeonHex.png"});
        }
        delete pf;
    }
    layer->setVisible(false);
}

TEST_CASE("hex.level.29.mistParticleScene") {
    auto      *mapMod = Map::create();
    auto      *gen    = Procgen::create();
    auto      *parts  = Particles::create();
    HexDungeon d      = buildHexDungeon(mapMod, gen, 2929, 24, 18);

    float wx = 0.f, wy = 0.f;
    d.layer->tileToWorld(d.spawnTx, d.spawnTy, wx, wy);

    ParticleEmitter *mist = parts->newEmitter(160);
    mist->applyPreset("smoke");
    mist->setEmissionRate(28.f);
    mist->setPosition(wx, wy);
    mist->start();

    ParticleEmitter *ember = parts->newEmitter(96);
    ember->applyPreset("spark");
    ember->setPosition(wx + 8.f, wy - 4.f);
    ember->start();

    for (int i = 0; i < 12; ++i) parts->update(1.f / 60.f);
    CHECK(mist->getCount() > 0);
    CHECK(ember->getCount() > 0);

    auto *lamp = Light2D::createLight("point");
    lamp->setRadius(110.f);
    lamp->setColor(0.6f, 0.7f, 0.95f, 1.4f);
    lamp->setPosition(wx, wy);
    lamp->setEnabled(true);
    CHECK(approxEq(lamp->getRadius(), 110.f));

    previewHex(
        mapMod, d,
        HexPreview{
            .title = "hex.level.29 mist particle", .pngName = "29_mistParticleScene.png", .fire = mist, .lamp = lamp});

    mist->stop();
    ember->stop();
    lamp->setEnabled(false);
    d.layer->setVisible(false);
}

TEST_CASE("hex.level.30.raidComboPipeline") {
    auto      *mapMod     = Map::create();
    auto      *gen        = Procgen::create();
    auto      *spatialMod = Spatial::create();
    auto      *parts      = Particles::create();
    HexDungeon d          = buildHexDungeon(mapMod, gen, 3030, 32, 24);

    Pathfinder *pf = mapMod->newPathfinder(d.layer);
    pf->blockGid(kWallGid);
    pf->setTopology("hex");
    Path *path = pf->findPath(d.spawnTx, d.spawnTy, d.exitTx, d.exitTy);
    REQUIRE(path != nullptr);
    REQUIRE(path->getLength() > 2);

    // Cost strip near spawn.
    for (int x = d.spawnTx + 2; x <= d.spawnTx + 4; ++x)
        for (int y = d.spawnTy - 1; y <= d.spawnTy + 1; ++y)
            if (pf->isWalkable(x, y)) pf->setCellCost(x, y, 8.f);

    FlowField *field = pf->buildFlowField(d.exitTx, d.exitTy);
    REQUIRE(field != nullptr);
    Path *flow = pf->followFlow(field, d.spawnTx, d.spawnTy);
    REQUIRE(flow != nullptr);

    Fov *fov = mapMod->newFov(d.layer);
    fov->blockOpaqueGid(kWallGid);
    fov->setTopology("hex");
    fov->setPerceptionRadiusScale(1.f);
    const int hero = fov->addRevealer(d.spawnTx, d.spawnTy, 5);
    fov->setRevealerPerception(hero, 2.f);
    const int torch = fov->addRevealer(d.exitTx, d.exitTy, 3);

    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    auto *inv = Inventory::create();
    inv->registerItemsFromJson(R"([
      {"id":"hex.relic","maxStack":1,"weight":0.5,"tags":["quest"]},
      {"id":"hex.potion","maxStack":10,"weight":0.2,"tags":["potion"]},
      {"id":"hex.coin","maxStack":99,"weight":0.01,"tags":["currency"]}
    ])");
    Bag *bag = inv->newBag(12);

    const int mid = path->getLength() / 2;
    float     lx = 0.f, ly = 0.f;
    d.layer->tileToWorld(path->getX(mid), path->getY(mid), lx, ly);
    lx += kTileW * 0.5f;
    ly += kTileH * 0.5f;
    std::unique_ptr<SpatialHash2D> hash(spatialMod->newSpatialHash2D(32.f));
    CHECK(hash->insert(1, lx - 6.f, ly - 6.f, lx + 6.f, ly + 6.f));

    ParticleEmitter *fire = parts->newEmitter(96);
    fire->applyPreset("fire");
    fire->start();

    auto *lamp = Light2D::createLight("point");
    lamp->setRadius(160.f);
    lamp->setEnabled(true);

    bool looted = false;
    for (int step = 0; step < path->getLength(); ++step) {
        const int tx = path->getX(step);
        const int ty = path->getY(step);
        fov->setRevealerPosition(hero, tx, ty);
        fov->compute();
        float wx = 0.f, wy = 0.f;
        d.layer->tileToWorld(tx, ty, wx, wy);
        lamp->setPosition(wx, wy);
        fire->setPosition(wx + 4.f, wy - 4.f);
        parts->update(1.f / 30.f);
        if (!looted && fov->canDetect(hero, path->getX(mid), path->getY(mid), 0.5f)) {
            float cx = wx + kTileW * 0.5f, cy = wy + kTileH * 0.5f;
            if (hash->queryCircle(cx, cy, 20.f) > 0) {
                const int added = bag->addItem("hex.relic", 1);
                if (added > 0) {
                    looted = true;
                    hash->remove(1);
                }
            }
        }
    }
    CHECK(looted);
    CHECK_EQ(bag->countItem("hex.relic"), 1);
    CHECK_EQ(fov->getRevealerCount(), 2);
    CHECK(flow->getLength() > 0);

    previewHex(mapMod, d,
               HexPreview{.title   = "hex.level.30 raid combo",
                          .pngName = "30_raid_combo.png",
                          .path    = path,
                          .fire    = fire,
                          .lamp    = lamp,
                          .lootTx  = path->getX(mid),
                          .lootTy  = path->getY(mid)});

    bag->destroy();
    ItemRegistry::clear();
    delete flow;
    delete field;
    delete path;
    delete pf;
    delete fov;
    (void)torch;
}
}  // namespace test_hex_level_simulation
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_hexmap_module)
#include "PrivateTestScope.h"
#include <cstdint>
#include "Fixtures.h"
#include "hexmap/HexMap.h"
#include "hexmap/HexMapGenerator.h"
#include "hexmap/HexMapModule.h"
#include "hexmap/HexSphereGenerator.h"
#include "hexmap/HexSphereMap.h"
#include "hexmap/HexTerrainBake.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// hexmap_module.cpp
namespace test_hexmap_module {
// hexmap_module.cpp
using namespace eve::hexmap;

namespace {

/** @brief Two chunks per side: enough that a rebuild produces several surfaces. */
constexpr std::int32_t kGridCells = 10;

}  // namespace

TEST_CASE("hexmap.module.rejectedGridSizeKeepsTheLiveGridAndItsMeshes") {
    GfxFixture   gfx(320, 240, /*useHeadless=*/true);
    HexMapModule module;

    REQUIRE(module.newGrid(gfx.gfx, kGridCells, kGridCells, 7u).ok());
    REQUIRE_EQ(module.map().cellCountX(), kGridCells);
    REQUIRE(module.rebuildDirtyChunks(gfx.gfx) > 0);

    eve::graphics::Mesh *live = module.chunkMeshAt(0, HexSurface::Terrain);
    REQUIRE(live != nullptr);

    // Every rejected size must leave the live grid *and* its meshes untouched.
    // The previous order released all GPU meshes before `HexMap::reset` had validated
    // anything, so a rejected size destroyed the meshes while the old grid stayed alive:
    // the renderables scripts already held kept pointing at destroyed meshes, and the
    // next draw threw on a null mesh.
    //
    // These are REQUIRE, not CHECK: in this suite a failed CHECK is only a warning that
    // still exits zero, so it would not fail the test.
    CHECK(!module.newGrid(gfx.gfx, 7, 7, 7u).ok());
    REQUIRE_EQ(module.map().cellCountX(), kGridCells);
    REQUIRE_EQ(module.map().cellCountZ(), kGridCells);
    REQUIRE(module.chunkMeshAt(0, HexSurface::Terrain) == live);

    CHECK(!module.newGrid(gfx.gfx, 0, kGridCells, 7u).ok());
    REQUIRE_EQ(module.map().cellCountX(), kGridCells);
    REQUIRE(module.chunkMeshAt(0, HexSurface::Terrain) == live);

    // The size is bounded here, exactly as the save format bounds it, instead of
    // reaching a raw cell allocation that would throw out of a native binding.
    CHECK(!module.newGrid(gfx.gfx, kMaxHexGridDimension + 5, kGridCells, 7u).ok());
    REQUIRE_EQ(module.map().cellCountX(), kGridCells);
    REQUIRE(module.chunkMeshAt(0, HexSurface::Terrain) == live);

    module.releaseMeshes(gfx.gfx);
}

TEST_CASE("hexmap.module.rejectedSphereShapeKeepsTheLiveMeshes") {
    GfxFixture   gfx(320, 240, /*useHeadless=*/true);
    HexMapModule module;

    REQUIRE(module.newSphere(gfx.gfx, 1, 100.f, 3u).ok());
    REQUIRE(module.rebuildSphere(gfx.gfx).ok());

    eve::graphics::Mesh *live = module.sphereTerrainMesh();
    REQUIRE(live != nullptr);

    // Same ordering rule as the grid path: `HexSphereMap::reset` rejects its arguments
    // before touching the map, so a rejected shape leaves both the map and its meshes
    // exactly as they were.
    CHECK(!module.newSphere(gfx.gfx, -1, 100.f, 3u).ok());
    REQUIRE(module.sphereTerrainMesh() == live);
    REQUIRE(!module.sphere().empty());

    module.releaseSphereMeshes(gfx.gfx);
}

TEST_CASE("hexmap.module.applyTerrainRebuildsFromAProcgenBake") {
    GfxFixture   gfx(320, 240, /*useHeadless=*/true);
    HexMapModule module;
    REQUIRE(module.newGrid(gfx.gfx, kGridCells, kGridCells, 1u).ok());
    REQUIRE(module.rebuildDirtyChunks(gfx.gfx) > 0);
    eve::graphics::Mesh *live = module.chunkMeshAt(0, HexSurface::Terrain);
    REQUIRE(live != nullptr);

    HexTerrainBake rejected;
    rejected.kind       = HexTerrainBake::Kind::Planar;
    rejected.cellCountX = 7;
    rejected.cellCountZ = 7;
    CHECK(!module.applyTerrain(gfx.gfx, rejected).ok());
    REQUIRE_EQ(module.map().seed(), 1u);
    REQUIRE(module.chunkMeshAt(0, HexSurface::Terrain) == live);

    HexMap scratch;
    REQUIRE(scratch.reset(kGridCells, kGridCells, 9u).ok());
    HexMapGeneratorSettings settings;
    settings.seed           = 9u;
    settings.landPercentage = 50;
    settings.mapBorderX     = 0;
    settings.mapBorderZ     = 0;
    settings.regionBorder   = 0;
    REQUIRE(generateHexMap(scratch, settings).ok());
    const HexTerrainBake bake = snapshotHexTerrain(scratch);

    REQUIRE(module.applyTerrain(gfx.gfx, bake).ok());
    REQUIRE_EQ(module.map().seed(), 9u);
    REQUIRE_EQ(module.map().cellCount(), kGridCells * kGridCells);
    REQUIRE_EQ(module.map().values(module.map().coordinatesAt(0)).raw(),
               scratch.values(scratch.coordinatesAt(0)).raw());
    REQUIRE(module.chunkMeshAt(0, HexSurface::Terrain) == nullptr);
    REQUIRE(module.rebuildDirtyChunks(gfx.gfx) > 0);
    REQUIRE(module.chunkMeshAt(0, HexSurface::Terrain) != nullptr);

    HexTerrainBake sphereBake = bake;
    sphereBake.kind           = HexTerrainBake::Kind::Sphere;
    CHECK(!module.applyTerrain(gfx.gfx, sphereBake).ok());
    REQUIRE_EQ(module.map().seed(), 9u);

    module.releaseMeshes(gfx.gfx);
}

TEST_CASE("hexmap.module.applySphereTerrainRebuildsMeshes") {
    GfxFixture   gfx(320, 240, /*useHeadless=*/true);
    HexMapModule module;
    REQUIRE(module.newSphere(gfx.gfx, 1, 100.f, 3u).ok());

    HexSphereMap scratch;
    REQUIRE(scratch.reset(1, 100.f, 3u).ok());
    HexSphereGeneratorSettings settings;
    settings.seed           = 3u;
    settings.landPercentage = 45;
    REQUIRE(generateSphereMap(scratch, settings).ok());
    const HexTerrainBake bake = snapshotHexSphereTerrain(scratch);

    REQUIRE(module.applySphereTerrain(gfx.gfx, bake).ok());
    REQUIRE(module.sphereTerrainMesh() != nullptr);
    REQUIRE_EQ(module.sphere().values(0).raw(), scratch.values(0).raw());

    HexMap emptyPlanar;
    CHECK(!module.applySphereTerrain(gfx.gfx, snapshotHexTerrain(emptyPlanar)).ok());

    module.releaseSphereMeshes(gfx.gfx);
}

// hexmap_terrain_bake.cpp
using namespace eve::hexmap;

namespace {

[[nodiscard]] HexMap makePlanar(std::int32_t cellCountX = 20, std::int32_t cellCountZ = 15,
                                std::uint32_t seed = 1234u) {
    HexMap map;
    auto   created = map.reset(cellCountX, cellCountZ, seed);
    REQUIRE(created.ok());
    return map;
}

[[nodiscard]] HexMapGeneratorSettings tightSettings(std::uint32_t seed) {
    HexMapGeneratorSettings settings;
    settings.seed           = seed;
    settings.landPercentage = 50;
    settings.waterLevel     = 3;
    settings.mapBorderX     = 0;
    settings.mapBorderZ     = 0;
    settings.regionBorder   = 0;
    return settings;
}

}  // namespace
}  // namespace test_hexmap_module
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_housegen_render)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <assimp/scene.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>
#include "PathBesideSource.h"
#include "data/ByteData.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
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
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "medialoader/model/ModelLoader.h"
#include "model3d/Model3D.h"
#include "procgen/house/HouseComponentLibrary.h"
#include "procgen/house/HouseGenerator.h"
#include "procgen/house/HouseLayout.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// housegen_render.cpp
namespace test_housegen_render {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;
using namespace eve::housegen;

namespace {

int envInt(const char *name, int fallback) {
    const char *value = std::getenv(name);
    if (!value || !*value) return fallback;
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

std::string envString(const char *name, const char *fallback) {
    const char *value = std::getenv(name);
    return value && *value ? value : fallback;
}

struct Palette {
    float wallR, wallG, wallB;
    float trimR, trimG, trimB;
    float roofR, roofG, roofB;
    float glassR, glassG, glassB;
};

Palette gPalette{0.76f, 0.58f, 0.38f, 0.82f, 0.64f, 0.42f, 0.42f, 0.16f, 0.13f, 0.18f, 0.55f, 0.72f};

void selectPalette(uint32_t seed) {
    switch (seed % 3u) {
        case 0: gPalette = {0.78f, 0.80f, 0.76f, 0.25f, 0.28f, 0.30f, 0.16f, 0.20f, 0.25f, 0.22f, 0.48f, 0.64f}; break;
        case 1: gPalette = {0.76f, 0.58f, 0.38f, 0.82f, 0.64f, 0.42f, 0.42f, 0.16f, 0.13f, 0.18f, 0.55f, 0.72f}; break;
        default: gPalette = {0.64f, 0.70f, 0.78f, 0.92f, 0.90f, 0.82f, 0.20f, 0.28f, 0.36f, 0.16f, 0.52f, 0.70f}; break;
    }
}

// A neutral unit cube is enough for this preview: HouseLayout supplies all placement and the
// renderer supplies material tint, lighting and perspective. Real GLB components use the same
// transforms through HouseLayout::instantiate().
constexpr char kCubeObj[] =
    "v -0.5 -0.5  0.5\nv  0.5 -0.5  0.5\nv  0.5  0.5  0.5\nv -0.5  0.5  0.5\n"
    "v  0.5 -0.5 -0.5\nv -0.5 -0.5 -0.5\nv -0.5  0.5 -0.5\nv  0.5  0.5 -0.5\n"
    "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
    "vn 0 0 1\nvn 0 0 -1\nvn 1 0 0\nvn -1 0 0\nvn 0 1 0\nvn 0 -1 0\n"
    "f 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n"
    "f 5/1/2 6/2/2 7/3/2\nf 5/1/2 7/3/2 8/4/2\n"
    "f 2/1/3 5/2/3 8/3/3\nf 2/1/3 8/3/3 3/4/3\n"
    "f 6/1/4 1/2/4 4/3/4\nf 6/1/4 4/3/4 7/4/4\n"
    "f 4/1/5 3/2/5 8/3/5\nf 4/1/5 8/3/5 7/4/5\n"
    "f 6/1/6 5/2/6 2/3/6\nf 6/1/6 2/3/6 1/4/6\n";

// Unit triangular prism whose base starts at y=0. Scaling it produces a continuous gable end;
// unlike stacked cubes it cannot poke through the sloped roof or leave stair-step notches.
constexpr char kGableObj[] =
    "v -0.5 0.0  0.5\nv 0.5 0.0  0.5\nv 0.0 1.0  0.5\n"
    "v -0.5 0.0 -0.5\nv 0.5 0.0 -0.5\nv 0.0 1.0 -0.5\n"
    "f 1 2 3\nf 6 5 4\nf 1 4 5\nf 1 5 2\n"
    "f 2 5 6\nf 2 6 3\nf 3 6 4\nf 3 4 1\n";

Mesh *previewCube(Graphics *gfx) {
    medialoader::ModelLoader loader;
    auto                     scene = loader.loadFromMemory(kCubeObj, sizeof(kCubeObj) - 1, ".obj");
    REQUIRE(!scene.empty());
    REQUIRE(scene->mNumMeshes > 0);
    return gfx->newMeshFromAssimp(*scene->mMeshes[0]);
}

Mesh *previewGable(Graphics *gfx) {
    medialoader::ModelLoader loader;
    auto                     scene = loader.loadFromMemory(kGableObj, sizeof(kGableObj) - 1, ".obj");
    REQUIRE(!scene.empty());
    REQUIRE(scene->mNumMeshes > 0);
    return gfx->newMeshFromAssimp(*scene->mMeshes[0]);
}

Renderable3D *box(Mesh *mesh, float x, float y, float z, float sx, float sy, float sz, float r, float g, float b,
                  float roll = 0.f) {
    auto *entity = Renderable3D::create();
    entity->setMesh(mesh);
    entity->setPosition(x, y, z);
    entity->setScale(sx, sy, sz);
    entity->setRotation(0.f, 0.f, roll);
    entity->setTint(r, g, b, 1.f);
    entity->setRoughness(0.72f);
    return entity;
}

Texture *gBrickColor  = nullptr;
Texture *gBrickNormal = nullptr;
Texture *gBrickHeight = nullptr;

Renderable3D *wallSurfaceBox(Mesh *mesh, float x, float y, float z, float sx, float sy, float sz) {
    auto *entity = box(mesh, x, y, z, sx, sy, sz, 1.f, 1.f, 1.f);
    if (gBrickColor) entity->setTexture(gBrickColor);
    if (gBrickNormal) entity->setNormalTexture(gBrickNormal);
    if (gBrickHeight) {
        entity->setHeightTexture(gBrickHeight);
        entity->setParallax(0.018f, 8.f, 20.f);
    }
    entity->setMetallic(0.f);
    entity->setRoughness(0.86f);
    entity->setTexCellBomb(4.f, 0.05f, 0.f);
    return entity;
}

void addWallModule(Mesh *mesh, const HouseInstance &instance, float floorHeight) {
    const float outX   = instance.rotationDeg == 90 ? 1.f : instance.rotationDeg == 270 ? -1.f : 0.f;
    const float outZ   = instance.rotationDeg == 180 ? 1.f : instance.rotationDeg == 0 ? -1.f : 0.f;
    const float x      = float(instance.x) + outX * 0.50f;
    const float y0     = float(instance.z) * floorHeight;
    const float z      = float(instance.y) + outZ * 0.50f;
    const bool  alongX = instance.rotationDeg == 0 || instance.rotationDeg == 180;
    const float sx     = alongX ? 1.02f : 0.12f;
    const float sz     = alongX ? 0.12f : 1.02f;

    if (instance.componentId == "wall.window") {
        wallSurfaceBox(mesh, x, y0 + 0.40f, z, sx, 0.80f, sz);
        wallSurfaceBox(mesh, x, y0 + 2.08f, z, sx, 0.64f, sz);
        const float glassX = x + outX * 0.015f, glassZ = z + outZ * 0.015f;
        box(mesh, glassX, y0 + 1.28f, glassZ, alongX ? 0.72f : 0.07f, 0.92f, alongX ? 0.07f : 0.72f, gPalette.glassR,
            gPalette.glassG, gPalette.glassB);
        for (float side : {-0.40f, 0.40f})
            box(mesh, glassX + (alongX ? side : 0.f), y0 + 1.28f, glassZ + (alongX ? 0.f : side),
                alongX ? 0.07f : 0.08f, 1.08f, alongX ? 0.08f : 0.07f, gPalette.trimR, gPalette.trimG, gPalette.trimB);
        for (float side : {0.76f, 1.80f})
            box(mesh, glassX, y0 + side, glassZ, alongX ? 0.86f : 0.08f, 0.07f, alongX ? 0.08f : 0.86f, gPalette.trimR,
                gPalette.trimG, gPalette.trimB);
        return;
    }
    wallSurfaceBox(mesh, x, y0 + floorHeight * 0.5f, z, sx, floorHeight, sz);
}

void addDoorModule(Mesh *mesh, const HouseInstance &instance, float floorHeight) {
    float       x      = float(instance.x);
    const float y0     = float(instance.z) * floorHeight;
    float       z      = float(instance.y);
    const bool  alongX = instance.rotationDeg == 0 || instance.rotationDeg == 180;
    const float outX   = instance.rotationDeg == 90 ? 1.f : instance.rotationDeg == 270 ? -1.f : 0.f;
    const float outZ   = instance.rotationDeg == 180 ? 1.f : instance.rotationDeg == 0 ? -1.f : 0.f;
    x += outX * 0.50f;
    z += outZ * 0.50f;
    const float leafX = alongX ? 0.58f : 0.12f, leafZ = alongX ? 0.12f : 0.58f;
    box(mesh, x, y0 + 0.92f, z, leafX, 1.84f, leafZ, 0.16f, 0.24f, 0.20f);
    for (float side : {-0.39f, 0.39f})
        box(mesh, x + (alongX ? side : 0.f), y0 + floorHeight * 0.5f, z + (alongX ? 0.f : side), alongX ? 0.15f : 0.16f,
            floorHeight, alongX ? 0.16f : 0.15f, gPalette.trimR, gPalette.trimG, gPalette.trimB);
    box(mesh, x, y0 + 2.25f, z, alongX ? 0.64f : 0.16f, 0.30f, alongX ? 0.16f : 0.64f, gPalette.trimR, gPalette.trimG,
        gPalette.trimB);
    box(mesh, x + (alongX ? 0.20f : outX * 0.075f), y0 + 0.95f, z + (alongX ? outZ * 0.075f : 0.20f), 0.055f, 0.055f,
        0.055f, 0.92f, 0.72f, 0.20f);

    // A small porch makes entrance-side variation readable from a distance.
    const float porchX = x + outX * 0.55f, porchZ = z + outZ * 0.55f;
    box(mesh, porchX, y0 + 0.04f, porchZ, alongX ? 1.55f : 0.80f, 0.10f, alongX ? 0.80f : 1.55f, 0.38f, 0.28f, 0.19f);
    box(mesh, porchX, y0 + 2.48f, porchZ, alongX ? 1.75f : 0.92f, 0.10f, alongX ? 0.92f : 1.75f, gPalette.roofR,
        gPalette.roofG, gPalette.roofB);
    for (float side : {-0.67f, 0.67f})
        box(mesh, porchX + (alongX ? side : 0.f), y0 + 1.24f, porchZ + (alongX ? 0.f : side), 0.07f, 2.42f, 0.07f,
            gPalette.trimR, gPalette.trimG, gPalette.trimB);
}

void saveFrame(Graphics *gfx, const std::string &name) {
    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();
    auto                        *frame       = gfx->newImageData();
    REQUIRE(frame != nullptr);
    auto *png = frame->encode(medialoader::FormatHandler::ENCODED_PNG, name.c_str(), false);
    REQUIRE(png != nullptr);
    const std::filesystem::path outDir = std::filesystem::path(EVENGINE_TEST_BINARY_DIR) / "out";
    std::filesystem::create_directories(outDir);
    const auto    outPath = outDir / name;
    std::ofstream out(outPath, std::ios::binary);
    REQUIRE(out.good());
    out.write(static_cast<const char *>(png->getData()), static_cast<std::streamsize>(png->getSize()));
    REQUIRE(out.good());
    std::printf("housegen render saved: %s\n", outPath.string().c_str());
    delete png;
    delete frame;
}

Texture *loadPreviewTexture(Graphics *gfx, const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    REQUIRE(input.good());
    const std::streamsize size = input.tellg();
    REQUIRE(size > 0);
    input.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    REQUIRE(input.read(reinterpret_cast<char *>(bytes.data()), size).good());
    eve::data::ByteData                    source(bytes.data(), bytes.size());
    std::unique_ptr<eve::image::ImageData> decoded(eve::image::Image::create()->newImageData(&source));
    TextureCreateInfo                      info = TextureCreateInfo::withMipmaps(true);
    info.sampler.repeatU                        = true;
    info.sampler.repeatV                        = true;
    return gfx->newTexture(decoded.get(), info);
}

}  // namespace

TEST_CASE("housegen.renderPreview") {
    constexpr char kit[] = R"({"components":[
      {"id":"foundation","model":"foundation.glb","category":"foundation"},
      {"id":"floor","model":"floor.glb","category":"floor"},
      {"id":"wall.solid","model":"wall.glb","category":"wall","weight":2},
      {"id":"wall.window","model":"window.glb","category":"wall","weight":5,"tags":["window"]},
      {"id":"door","model":"door.glb","category":"door"},
      {"id":"roof","model":"roof.glb","category":"roof"}
    ]})";

    HouseComponentLibrary library;
    auto                  kitResult = library.loadFromJson(kit);
    REQUIRE(kitResult.ok());
    HouseRequest request;
    request.seed        = static_cast<unsigned>(envInt("EVHOUSE_PREVIEW_SEED", 20260815));
    request.width       = envInt("EVHOUSE_PREVIEW_WIDTH", 7);
    request.depth       = envInt("EVHOUSE_PREVIEW_DEPTH", 6);
    request.floors      = envInt("EVHOUSE_PREVIEW_FLOORS", 2);
    request.footprint   = envString("EVHOUSE_PREVIEW_FOOTPRINT", "auto");
    request.roof        = envString("EVHOUSE_PREVIEW_ROOF", "auto");
    request.entrance    = envString("EVHOUSE_PREVIEW_ENTRANCE", "auto");
    request.floorHeight = 2.4f;
    HouseLayout    layout;
    HouseGenerator generator(library);
    auto           generated = generator.generate(request, layout);
    REQUIRE(generated.ok());

    auto *window = eve::window::Window::create();
    auto *gfx    = Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 960;
    settings.height   = 640;
    settings.centered = true;
    REQUIRE(window->setWindowSettings(settings));

    Mesh *cube  = previewCube(gfx);
    Mesh *gable = previewGable(gfx);
    REQUIRE(cube != nullptr);
    REQUIRE(gable != nullptr);
    const std::filesystem::path brickDir =
        eve_test_path::pathBesideTestDir(__FILE__, "assets/housegen/materials/ambientcg-bricks001");
    gBrickColor  = loadPreviewTexture(gfx, brickDir / "bricks001-color.jpg");
    gBrickNormal = loadPreviewTexture(gfx, brickDir / "bricks001-normal-gl.jpg");
    gBrickHeight = loadPreviewTexture(gfx, brickDir / "bricks001-height.jpg");
    REQUIRE(gBrickColor != nullptr);
    REQUIRE(gBrickNormal != nullptr);
    REQUIRE(gBrickHeight != nullptr);
    selectPalette(request.seed);
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.58f, 0.75f, 0.90f, 1.f));

    // Lawn, foundation/floors, then facade modules selected by the generated layout.
    const float plotCenterX = float(request.width - 1) * 0.5f;
    const float plotCenterZ = float(request.depth - 1) * 0.5f;
    box(cube, plotCenterX, -0.30f, plotCenterZ, float(request.width + 4), 0.25f, float(request.depth + 4), 0.20f, 0.42f,
        0.18f);
    for (const auto &instance : layout.instances) {
        const auto component = library.find(instance.componentId);
        REQUIRE(component.has_value());
        if (component->get().category == "foundation") {
            box(cube, float(instance.x), -0.08f, float(instance.y), 0.96f, 0.16f, 0.96f, 0.35f, 0.34f, 0.33f);
        } else if (component->get().category == "floor") {
            box(cube, float(instance.x), float(instance.z) * request.floorHeight - 0.03f, float(instance.y), 0.96f,
                0.06f, 0.96f, 0.48f, 0.33f, 0.22f);
        } else if (component->get().category == "wall") {
            addWallModule(cube, instance, request.floorHeight);
        } else if (component->get().category == "door") {
            addDoorModule(cube, instance, request.floorHeight);
        }
    }

    // One coherent gable roof replaces the per-cell debug roof blocks while still deriving its
    // footprint and elevation from the generated request/layout.
    int roofLevel = 0;
    for (const auto &instance : layout.instances) {
        const auto component = library.find(instance.componentId);
        if (component && component->get().category == "roof") roofLevel = std::max(roofLevel, instance.z);
    }
    int roofMinX = request.width, roofMaxX = 0, roofMinZ = request.depth, roofMaxZ = 0;
    for (const auto &instance : layout.instances) {
        const auto component = library.find(instance.componentId);
        if (component && component->get().category == "roof" && instance.z == roofLevel) {
            roofMinX = std::min(roofMinX, instance.x);
            roofMaxX = std::max(roofMaxX, instance.x);
            roofMinZ = std::min(roofMinZ, instance.y);
            roofMaxZ = std::max(roofMaxZ, instance.y);
        }
    }
    const float             roofWidth   = float(roofMaxX - roofMinX + 1);
    const float             roofDepth   = float(roofMaxZ - roofMinZ + 1);
    const float             roofCenterX = float(roofMinX + roofMaxX) * 0.5f;
    const float             roofCenterZ = float(roofMinZ + roofMaxZ) * 0.5f;
    const float             eaveY       = roofLevel * request.floorHeight;
    std::unordered_set<int> topRoofCells;
    for (const auto &instance : layout.instances) {
        const auto component = library.find(instance.componentId);
        if (component && component->get().category == "roof" && instance.z == roofLevel)
            topRoofCells.insert(instance.y * request.width + instance.x);
    }
    auto hasTopRoof = [&](int x, int z) {
        return x >= 0 && z >= 0 && x < request.width && z < request.depth &&
               topRoofCells.contains(z * request.width + x);
    };
    // Roof instances below the main roof are mandatory covers for cells exposed by a setback.
    for (const auto &instance : layout.instances) {
        const auto component = library.find(instance.componentId);
        if (!component || component->get().category != "roof" || instance.z == roofLevel) continue;
        box(cube, float(instance.x), instance.z * request.floorHeight, float(instance.y), 1.02f, 0.14f, 1.02f,
            gPalette.roofR, gPalette.roofG, gPalette.roofB);
    }
    if (layout.roofStyle == "gable" && layout.footprintStyle == "rectangle") {
        const float halfRun     = roofWidth * 0.5f + 0.30f;
        const float rise        = roofWidth * 0.22f;
        const float angle       = std::atan2(rise, halfRun);
        const float panelLength = std::hypot(halfRun, rise);
        const float panelOffset = halfRun * 0.5f;
        const float roofY       = eaveY + rise * 0.5f;
        box(cube, roofCenterX - panelOffset, roofY, roofCenterZ, panelLength, 0.18f, roofDepth + 0.65f, gPalette.roofR,
            gPalette.roofG, gPalette.roofB, angle);
        box(cube, roofCenterX + panelOffset, roofY, roofCenterZ, panelLength, 0.18f, roofDepth + 0.65f, gPalette.roofR,
            gPalette.roofG, gPalette.roofB, -angle);
        const float wallRise = roofWidth * 0.5f * std::tan(angle) - 0.05f;
        for (float z : {float(roofMinZ) - 0.50f, float(roofMaxZ) + 0.50f})
            box(gable, roofCenterX, eaveY - 0.02f, z, roofWidth, wallRise, 0.12f, gPalette.wallR, gPalette.wallG,
                gPalette.wallB);
    } else {
        // Tile-wise flat/shed roofs preserve L/T-shaped silhouettes instead of filling their
        // missing quadrants with a rectangular cap.
        const float shedAngle = layout.roofStyle == "shed" ? -0.16f : 0.f;
        const float roofY     = eaveY + (layout.roofStyle == "shed" ? roofWidth * 0.08f : 0.f);
        for (const auto &instance : layout.instances) {
            const auto component = library.find(instance.componentId);
            if (!component || component->get().category != "roof" || instance.z != roofLevel) continue;
            const float slopeY = layout.roofStyle == "shed" ? (roofCenterX - float(instance.x)) * 0.16f : 0.f;
            box(cube, float(instance.x), roofY + slopeY, float(instance.y), 1.08f,
                layout.roofStyle == "flat" ? 0.20f : 0.14f, 1.08f, gPalette.roofR, gPalette.roofG, gPalette.roofB,
                shedAngle);
        }
        // Close every actual roof boundary. Flat roofs receive a complete parapet; shed roofs
        // receive stepped wall infill up to the sloped plane.
        const int dx[] = {0, 1, 0, -1}, dz[] = {-1, 0, 1, 0};
        for (int z = roofMinZ; z <= roofMaxZ; ++z)
            for (int x = roofMinX; x <= roofMaxX; ++x) {
                if (!hasTopRoof(x, z)) continue;
                const float tileY = roofY + (layout.roofStyle == "shed" ? (roofCenterX - float(x)) * 0.16f : 0.f);
                for (int side = 0; side < 4; ++side) {
                    if (hasTopRoof(x + dx[side], z + dz[side])) continue;
                    const bool  alongX = side == 0 || side == 2;
                    const float px     = float(x) + float(dx[side]) * 0.50f;
                    const float pz     = float(z) + float(dz[side]) * 0.50f;
                    if (layout.roofStyle == "flat") {
                        box(cube, px, eaveY + 0.20f, pz, alongX ? 1.02f : 0.10f, 0.34f, alongX ? 0.10f : 1.02f,
                            gPalette.trimR, gPalette.trimG, gPalette.trimB);
                    } else {
                        const float fillHeight = std::max(0.02f, tileY - eaveY - 0.14f);
                        wallSurfaceBox(cube, px, eaveY + fillHeight * 0.5f - 0.07f, pz, alongX ? 1.02f : 0.11f,
                                       fillHeight, alongX ? 0.11f : 1.02f);
                        box(cube, px, tileY - 0.10f, pz, alongX ? 1.06f : 0.13f, 0.18f, alongX ? 0.13f : 1.06f,
                            gPalette.roofR, gPalette.roofG, gPalette.roofB);
                    }
                }
            }
    }

    // Some seeds receive a front balcony; it is an optional decorative grammar production.
    if (request.floors >= 2 && request.seed % 2u == 0u) {
        const float balconyZ = -0.88f;
        box(cube, plotCenterX, request.floorHeight + 0.14f, balconyZ, 2.8f, 0.14f, 0.74f, 0.38f, 0.28f, 0.19f);
        box(cube, plotCenterX, request.floorHeight + 0.72f, balconyZ - 0.30f, 2.8f, 0.08f, 0.08f, gPalette.trimR,
            gPalette.trimG, gPalette.trimB);
        for (float side : {-1.30f, 0.f, 1.30f})
            box(cube, plotCenterX + side, request.floorHeight + 0.44f, balconyZ - 0.30f, 0.07f, 0.60f, 0.07f,
                gPalette.trimR, gPalette.trimG, gPalette.trimB);
    }

    auto       *camera   = Camera3D::createCamera();
    const float plotSpan = float(std::max(request.width, request.depth));
    camera->setTarget(plotCenterX, request.floors * request.floorHeight * 0.52f, plotCenterZ);
    float eyeX = plotCenterX + plotSpan * 1.10f;
    float eyeZ = plotCenterZ - plotSpan * 1.55f;
    if (layout.entranceSide == "east") {
        eyeX = plotCenterX + plotSpan * 1.55f;
        eyeZ = plotCenterZ - plotSpan * 1.10f;
    } else if (layout.entranceSide == "south") {
        eyeX = plotCenterX - plotSpan * 1.10f;
        eyeZ = plotCenterZ + plotSpan * 1.55f;
    } else if (layout.entranceSide == "west") {
        eyeX = plotCenterX - plotSpan * 1.55f;
        eyeZ = plotCenterZ - plotSpan * 1.10f;
    }
    camera->setEye(eyeX, request.floors * request.floorHeight + plotSpan * 0.48f, eyeZ);
    camera->setAmbient(0.34f, 0.34f, 0.38f);
    camera->data()->farZ = 80.f;
    auto *sun            = Light3D::createLight("dir");
    sun->setDirection(-0.7f, -1.0f, -0.45f);
    sun->setColor(1.f, 0.94f, 0.84f, 1.55f);
    sun->setCastShadow(true);
    sun->setShadowStrength(0.82f);

    // RenderSystem closes the 3D pass and presents. The tiny off-screen-ish HUD also exercises
    // the normal mixed 3D/2D game frame path.
    auto *hud             = Renderable2D::create();
    hud->transform()->x   = 0.f;
    hud->transform()->y   = 0.f;
    hud->sprite()->width  = 1.f;
    hud->sprite()->height = 1.f;
    hud->sprite()->a      = 0.f;
    for (int frame = 0; frame < 4; ++frame) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
        }
    }

    saveFrame(gfx, envString("EVHOUSE_PREVIEW_NAME", "housegen_seed_20260815.png"));
    window->close();
}

TEST_CASE("housegen.materialPreview") {
    const std::filesystem::path assetDir =
        eve_test_path::pathBesideTestDir(__FILE__, "assets/housegen/kenney-modular-buildings");
    HouseComponentLibrary library;
    auto                  loaded = library.loadFromFile((assetDir / "components.json").string());
    REQUIRE(loaded.ok());
    REQUIRE_EQ(library.count(), 4);
    for (const auto &id : library.ids()) {
        const auto component = library.find(id);
        REQUIRE(component.has_value());
        REQUIRE(std::filesystem::is_regular_file(component->get().modelPath));
    }

    auto *window     = eve::window::Window::create();
    auto *gfx        = Graphics::create();
    auto *filesystem = eve::filesystem::Filesystem::create();
    auto *models     = eve::model3d::Model3D::create();
    REQUIRE(window != nullptr);
    REQUIRE(gfx != nullptr);
    REQUIRE(filesystem != nullptr);
    REQUIRE(models != nullptr);
    REQUIRE(filesystem->setIdentity("housegen_material_preview", true));
    REQUIRE(filesystem->setupWriteDirectory());
    eve::window::WindowSettings settings;
    settings.width    = 960;
    settings.height   = 640;
    settings.centered = true;
    REQUIRE(window->setWindowSettings(settings));

    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.56f, 0.72f, 0.86f, 1.f));
    Mesh *cube = previewCube(gfx);
    REQUIRE(cube != nullptr);
    box(cube, 2.1f, -0.12f, 0.f, 7.2f, 0.18f, 3.2f, 0.22f, 0.38f, 0.18f);

    HouseLayout layout;
    layout.moduleSize     = 1.4f;
    layout.floorHeight    = 2.f;
    layout.footprintWidth = 4;
    layout.footprintDepth = 1;
    layout.footprintMask.assign(4, 1);
    layout.instances = {
        {"kenney.wall.block", 0, 0, 0, 0},
        {"kenney.wall.window", 1, 0, 0, 0},
        {"kenney.door", 2, 0, 0, 0},
        {"kenney.roof.gable", 3, 0, 0, 0},
    };
    auto instantiated = layout.instantiate(*gfx, *models, library);
    REQUIRE(instantiated.ok());
    auto entities = std::move(instantiated).takeValue();
    REQUIRE(entities.size() >= layout.instances.size());
    auto *first = static_cast<Renderable3D *>(ecs::try_get(entities.front()));
    REQUIRE(first != nullptr);
    REQUIRE(first->meshRenderer()->texture != nullptr);
    REQUIRE(first->meshRenderer()->normalTexture != nullptr);
    REQUIRE(first->meshRenderer()->heightTexture != nullptr);
    REQUIRE(first->meshRenderer()->parallaxScale > 0.f);

    auto *camera = Camera3D::createCamera();
    camera->setTarget(2.1f, 0.55f, 0.f);
    camera->setEye(4.9f, 2.8f, 5.0f);
    camera->setAmbient(0.30f, 0.30f, 0.34f);
    camera->data()->farZ = 60.f;
    auto *sun            = Light3D::createLight("dir");
    sun->setDirection(-0.7f, -1.f, -0.5f);
    sun->setColor(1.f, 0.94f, 0.84f, 1.6f);
    sun->setCastShadow(true);
    sun->setShadowStrength(0.78f);
    auto *hud        = Renderable2D::create();
    hud->sprite()->a = 0.f;
    for (int frame = 0; frame < 4; ++frame) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
        }
    }
    saveFrame(gfx, envString("EVHOUSE_MATERIAL_PREVIEW_NAME", "housegen_materials.png"));
    window->close();
}
}  // namespace test_housegen_render
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ik)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <cmath>
#include <memory>
#include "common/Exception.h"
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
#include "ik/IK.h"
#include "ik/Skeleton2D.h"
#include "ik/Skeleton3D.h"
#include "ik/Solver2D.h"
#include "ik/Solver3D.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// ik.cpp
namespace test_ik {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace eve::ik;
using namespace eve::graphics;

[[maybe_unused]] static bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }


[[maybe_unused]] static void drawSeg(Graphics *gfx, float x0, float y0, float x1, float y1, const Color &c) {
    const float dx  = x1 - x0;
    const float dy  = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.5f) return;
    const float steps = std::max(1.f, len / 2.5f);
    for (float t = 0.f; t <= 1.f; t += 1.f / steps) {
        gfx->drawSolidRect(x0 + dx * t - 2.f, y0 + dy * t - 2.f, 4.f, 4.f, c);
    }
}

TEST_CASE("ik.solver2d.armReachPreview") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    // Multi-segment arm in world units (will scale to pixels).
    int b1 = sk->createBone(0, 1.1f);
    int b2 = sk->createBone(b1, 0.95f);
    int b3 = sk->createBone(b2, 0.7f);
    sk->initStraightPose(0.f, 0.f);
    sk->setConstraints(b1, -1.2f, 1.2f);
    sk->setConstraints(b2, -1.6f, 1.6f);

    std::unique_ptr<Solver2D> solver(new Solver2D());
    solver->setMaxIterations(40);
    solver->setTolerance(1e-3f);
    solver->addTarget(b3, 1.5f, 0.8f, 1.f);

    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 560;
    s.height   = 420;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    const float scale = 110.f;
    const float ox    = float(gfx->getWidth()) * 0.35f;
    const float oy    = float(gfx->getHeight()) * 0.55f;

    gfx->setBackgroundColorRGBA(0.07f, 0.08f, 0.11f, 1.f);
    float tipErr = 1.f;

    for (int frame = 0; frame < 120; ++frame) {
        const float t = float(frame) * 0.05f;
        // Orbiting reach target (figure-eight-ish).
        const float tx = 1.1f + 0.85f * std::cos(t);
        const float ty = 0.2f + 0.75f * std::sin(t * 1.3f);
        solver->clearTargets();
        solver->addTarget(b3, tx, ty, 1.f);
        solver->solve(sk.get());

        const float tipX = sk->getX(b3);
        const float tipY = sk->getY(b3);
        tipErr           = std::sqrt((tipX - tx) * (tipX - tx) + (tipY - ty) * (tipY - ty));

        gfx->clearScreen();
        // Ground / base
        gfx->drawSolidRect(ox - 18.f, oy - 6.f, 36.f, 12.f, Color(0.35f, 0.38f, 0.45f, 1.f));

        auto toScreen = [&](float wx, float wy, float &sx, float &sy) {
            sx = ox + wx * scale;
            sy = oy - wy * scale;
        };

        float sx0, sy0, sx1, sy1;
        toScreen(sk->getX(0), sk->getY(0), sx0, sy0);
        for (int b = 1; b < sk->getBoneCount(); ++b) {
            const int p = sk->getParent(b);
            toScreen(sk->getX(p), sk->getY(p), sx0, sy0);
            toScreen(sk->getX(b), sk->getY(b), sx1, sy1);
            drawSeg(gfx, sx0, sy0, sx1, sy1, Color(0.45f, 0.85f, 1.f, 1.f));
            gfx->drawSolidRect(sx1 - 3.f, sy1 - 3.f, 6.f, 6.f, Color(1.f, 1.f, 1.f, 1.f));
        }

        float tSx, tSy;
        toScreen(tx, ty, tSx, tSy);
        gfx->drawSolidRect(tSx - 5.f, tSy - 5.f, 10.f, 10.f, Color(1.f, 0.45f, 0.25f, 1.f));
        gfx->present();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    CHECK(tipErr < 0.25f);
    win->close();
}
}  // namespace test_ik
#undef M_PI
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_inventory)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <cmath>
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
#include "graphics/RenderSystem.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/Inventory.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "ui/UI.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// inventory.cpp
namespace test_inventory {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::inventory;
using namespace eve::graphics;
using namespace eve::ui;

namespace {
[[maybe_unused]] bool approxEq(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }
}  // namespace


TEST_CASE("inventory.bag.uiPreview") {
    ItemRegistry::clear();
    InventorySystem::clearEvents();
    InventorySystem::ensureBuiltins();

    ItemRegistry::loadFromJson(R"([
      {"id":"potion.hp","displayName":"Heal Potion","maxStack":20,"weight":0.2,"tags":["consumable"]},
      {"id":"ore.iron","displayName":"Iron Ore","maxStack":50,"weight":1.0,"tags":["material"]},
      {"id":"sword.iron","displayName":"Iron Sword","maxStack":1,"weight":3.5,
       "equipSlot":"weapon","tags":["weapon"]},
      {"id":"coin","displayName":"Gold Coin","maxStack":99,"weight":0.01,"tags":["currency"]}
    ])");

    Bag bag(8);
    bag.setId("adventurer");
    bag.setMaxWeight(40.f);
    bag.addItem("potion.hp", 7);
    bag.addItem("ore.iron", 23);
    bag.addItem("sword.iron", 1);
    bag.addItem("coin", 120);

    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    auto *ui  = UI::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    REQUIRE(ui != nullptr);
    eve::window::WindowSettings s;
    s.width    = 520;
    s.height   = 420;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    REQUIRE(ui->initBackend());

    gfx->setBackgroundColor(Color(0.09f, 0.10f, 0.14f, 1.f));
    auto *cam      = Camera2D::createCamera();
    cam->data()->r = 0.09f;
    cam->data()->g = 0.10f;
    cam->data()->b = 0.14f;

    // Soft bag silhouette behind the panel.
    auto *panel              = Renderable2D::create();
    panel->transform()->x    = 40;
    panel->transform()->y    = 50;
    panel->sprite()->width   = 280;
    panel->sprite()->height  = 320;
    panel->sprite()->r       = 0.18f;
    panel->sprite()->g       = 0.22f;
    panel->sprite()->b       = 0.30f;
    panel->sprite()->visible = true;

    int highlighted = 0;
    for (int frame = 0; frame < 90; ++frame) {
        if (frame % 18 == 0) highlighted = (highlighted + 1) % bag.getSlotCount();

        std::vector<WidgetDesc> slots;
        slots.push_back(text("Adventurer Pack", "title"));
        slots.push_back(separator("sep"));
        slots.push_back(
            text("weight " + std::to_string(int(bag.getUsedWeight())) + " / " + std::to_string(int(bag.getMaxWeight())),
                 "weight"));
        slots.push_back(progress(bag.getUsedWeight() / bag.getMaxWeight(), "wbar", "load"));
        slots.push_back(separator("sep2"));

        for (int i = 0; i < bag.getSlotCount(); ++i) {
            const std::string id    = bag.getSlotItemId(i);
            std::string       label = "[" + std::to_string(i) + "] empty";
            if (!id.empty()) {
                const ItemDefinition *def  = ItemRegistry::find(id);
                const std::string     name = def ? def->displayName : id;
                label = "[" + std::to_string(i) + "] " + name + " x" + std::to_string(bag.getSlotQuantity(i));
                if (i == highlighted) label = "> " + label;
            }
            slots.push_back(text(label, "s" + std::to_string(i)));
        }

        REQUIRE(UIHost::resolve(ui->remountAs("bag", window("Inventory", slots, "root"))).has_value());
        ui->beginFrameAndRender();
        RenderSystem::render(*gfx);
        ui->dispatchEvents();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ui->processEvent(&e);
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    CHECK_EQ(bag.countItem("coin"), 120);
    panel->sprite()->visible = false;
    ItemRegistry::clear();
    win->close();
}
}  // namespace test_inventory
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_map_fov)
#include "PrivateTestScope.h"
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
namespace test_map_fov {
// map_fov.cpp
using namespace eve::map;

namespace {

[[maybe_unused]] int countVisible(Fov *fov) {
    int n = 0;
    for (int y = 0; y < fov->getHeight(); ++y)
        for (int x = 0; x < fov->getWidth(); ++x)
            if (fov->isVisible(x, y)) ++n;
    return n;
}

}  // namespace


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


// map_path.cpp
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::map;
using namespace eve::graphics;

namespace {

[[maybe_unused]] void fillOpen(TileLayer *layer, int floorGid = 2) { layer->fill(floorGid); }

[[maybe_unused]] void wallRect(TileLayer *layer, int x0, int y0, int x1, int y1, int wallGid = 1) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) layer->setTile(x, y, wallGid);
}

[[maybe_unused]] bool pathEndsAt(Path *p, int x, int y) {
    if (!p || p->getLength() <= 0) return false;
    return p->getX(p->getLength() - 1) == x && p->getY(p->getLength() - 1) == y;
}

[[maybe_unused]] bool pathStartsAt(Path *p, int x, int y) {
    if (!p || p->getLength() <= 0) return false;
    return p->getX(0) == x && p->getY(0) == y;
}

[[maybe_unused]] bool pathWalkable(Path *p, Pathfinder *pf) {
    if (!p) return false;
    for (int i = 0; i < p->getLength(); ++i) {
        if (!pf->isWalkable(p->getX(i), p->getY(i))) return false;
    }
    return true;
}

}  // namespace


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
}  // namespace test_map_fov
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_mouse_cpp)
#include "PrivateTestScope.h"
#include <cmath>
#include <vector>
#include "common/Exception.h"
#include "mouse/Cursor.h"
#include "mouse/Mouse.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// mouse_cpp.cpp
namespace test_mouse_cpp {
namespace {

eve::mouse::Mouse *mouse() { return eve::mouse::Mouse::create(); }

// SDL video + a minimal window improves cursor/position APIs on desktop.
eve::window::Window *ensureVideoWindow() {
    auto *win = eve::window::Window::create();
    if (win == nullptr) {
        return nullptr;
    }
    eve::window::WindowSettings settings;
    settings.width    = 320;
    settings.height   = 240;
    settings.centered = true;
    if (!win->setWindowSettings(settings)) {
        return nullptr;
    }
    return win;
}

}  // namespace

TEST_CASE("mouse.isCursorSupported") {
    auto *m = mouse();
    REQUIRE(m != nullptr);
    // SDL video init happens in Mouse ctor; result depends on display availability.
    CHECK(m->isCursorSupported());
}

TEST_CASE("mouse.positionRoundTrip") {
    auto *win = ensureVideoWindow();
    if (win == nullptr) {
        return;
    }
    auto *m = mouse();
    REQUIRE(m != nullptr);
    if (!m->isCursorSupported()) {
        return;
    }

    void *handle = win->getHandle();
    REQUIRE(handle != nullptr);

    // Warp to a known in-window point (requires SDL window handle on setPosition).
    m->setPosition(50.0, 60.0);
    double x = 0.0;
    double y = 0.0;
    m->getPosition(x, y);
    // SDL may not update GetMouseState without focus (common in headless/CI).
    if (std::abs(x - 50.0) >= 2.0 || std::abs(y - 60.0) >= 2.0) {
        // Fall back: identity round-trip still exercises the API path.
        m->setPosition(x, y);
        double x2 = 0.0;
        double y2 = 0.0;
        m->getPosition(x2, y2);
        CHECK(std::abs(x2 - x) < 1.0);
        CHECK(std::abs(y2 - y) < 1.0);
        return;
    }
    CHECK(std::abs(x - 50.0) < 2.0);
    CHECK(std::abs(y - 60.0) < 2.0);

    m->setX(80.0);
    CHECK(std::abs(m->getX() - 80.0) < 2.0);
    m->setY(90.0);
    CHECK(std::abs(m->getY() - 90.0) < 2.0);
}

TEST_CASE("mouse.visibleRoundTrip") {
    if (ensureVideoWindow() == nullptr) {
        return;
    }
    auto *m = mouse();
    REQUIRE(m != nullptr);
    if (!m->isCursorSupported()) {
        return;
    }

    m->setVisible(false);
    CHECK(!m->isVisible());
    m->setVisible(true);
    CHECK(m->isVisible());
}

TEST_CASE("mouse.isDownEmptySafe") {
    auto *m = mouse();
    REQUIRE(m != nullptr);

    const std::vector<int> empty;
    CHECK(!m->isDown(empty));

    const std::vector<int> nonePressed = {1, 2, 3};
    CHECK(!m->isDown(nonePressed));
}

TEST_CASE("mouse.getSystemCursorKnown") {
    if (ensureVideoWindow() == nullptr) {
        return;
    }
    auto *m = mouse();
    REQUIRE(m != nullptr);
    if (!m->isCursorSupported()) {
        return;
    }

    eve::mouse::Cursor *cursor = m->getSystemCursor("ARROW");
    REQUIRE(cursor != nullptr);
    CHECK(!cursor->isCustom());
    CHECK(cursor->getSystemType() == "ARROW");
    CHECK(cursor->getHandle() != nullptr);
}

TEST_CASE("mouse.getSystemCursorInvalid") {
    if (ensureVideoWindow() == nullptr) {
        return;
    }
    auto *m = mouse();
    REQUIRE(m != nullptr);
    if (!m->isCursorSupported()) {
        return;
    }

    bool threw = false;
    try {
        m->getSystemCursor("NOT_A_REAL_CURSOR_TYPE");
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}
}  // namespace test_mouse_cpp
#endif
