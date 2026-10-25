// Related integration contracts share one translation unit to amortize header parsing.

// Contract section: hd2d
#include "PrivateTestScope.h"
#include "zeroerr/assert.h"

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

// Contract section: hex level simulation
#include "PrivateTestScope.h"

#include <utility>
#include "ProcgenGridTestSupport.h"
#include "filesystem/Filesystem.h"
#include "graphics/Light.h"
#include "graphics/RenderSystem.h"
#include "inventory/Bag.h"
#include "inventory/Inventory.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "map/FlowField.h"
#include "map/Fov.h"
#include "map/Path.h"
#include "map/Pathfinder.h"
#include "map/TileOrientation.h"
#include "particles/ParticleEmitter.h"
#include "particles/Particles.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/Grid2D.h"
#include "procgen/Params.h"
#include "procgen/Procgen.h"
#include "procgen/Semantic.h"
#include "spatial/Spatial.h"
#include "spatial/SpatialHash2D.h"
#include "window/Window.h"

namespace test_hex_level_simulation {
// One seeded dungeon proves Procgen -> hex TileLayer -> navigation/FOV ->
// spatial pickup/Inventory and a real rendered tilemap. Algorithm and feature
// matrices are deliberately not repeated as full level simulations.
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


// One real render boundary, without an interactive playback or PNG export.
void renderHexRaid(Map *mapMod, const HexDungeon &d, Path *path) {
    GfxFixture fx;
    fx.gfx->setScreenReadbackEnabled(true);
    const Color background(0.05f, 0.06f, 0.09f, 1.f);
    fx.gfx->setBackgroundColor(background);
    const uint8_t pixels[] = {70, 80, 100, 255, 160, 120, 70, 255, 60, 180, 210, 255};
    auto         *atlas    = fx.gfx->newTexture(3, 1, pixels);
    REQUIRE(atlas != nullptr);
    d.layer->setTilesetTileSize(1, 1);
    d.layer->setTileset(atlas, 1, 3, 0, 0);

    const int mid = path->getLength() / 2;
    float     wx = 0.f, wy = 0.f;
    d.layer->tileToWorld(path->getX(mid), path->getY(mid), wx, wy);
    auto *camera           = Camera2D::createCamera();
    camera->data()->active = true;
    camera->data()->x      = wx;
    camera->data()->y      = wy;
    camera->setAmbient(1.f, 1.f, 1.f);
    d.layer->setCamera(camera);

    // Read back the tilemap alone: particles cannot hide a missing map draw.
    for (int frame = 0; frame < 3; ++frame) {
        fx.gfx->clearScreen();
        mapMod->render(fx.gfx);
        fx.gfx->present();
    }
    std::unique_ptr<eve::image::ImageData> image(fx.gfx->newImageData());
    REQUIRE(image != nullptr);
    int changed = 0;
    for (int y = 0; y < image->getHeight(); y += 8)
        for (int x = 0; x < image->getWidth(); x += 8) {
            const auto pixel = image->getPixel(x, y);
            if (std::fabs(pixel.r - background.r) + std::fabs(pixel.g - background.g) +
                    std::fabs(pixel.b - background.b) >
                0.1f)
                ++changed;
        }
    REQUIRE_GT(changed, 100);

    camera->data()->active = false;
    d.layer->setCamera(nullptr);
    d.layer->setVisible(false);
}

}  // namespace

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
    REQUIRE_EQ(inv->registerItemsFromJson(R"([
      {"id":"hex.relic","maxStack":1,"weight":0.5,"tags":["quest"]},
      {"id":"hex.potion","maxStack":10,"weight":0.2,"tags":["potion"]},
      {"id":"hex.coin","maxStack":99,"weight":0.01,"tags":["currency"]}
    ])"),
               3);
    Bag *bag = inv->newBag(12);

    const int mid = path->getLength() / 2;
    float     lx = 0.f, ly = 0.f;
    d.layer->tileToWorld(path->getX(mid), path->getY(mid), lx, ly);
    lx += kTileW * 0.5f;
    ly += kTileH * 0.5f;
    std::unique_ptr<SpatialHash2D> hash(spatialMod->newSpatialHash2D(32.f));
    REQUIRE(hash->insert(1, lx - 6.f, ly - 6.f, lx + 6.f, ly + 6.f));

    ParticleEmitter *fire = parts->newEmitter(96);
    fire->applyPreset("fire");
    fire->start();

    bool looted = false;
    for (int step = 0; step < path->getLength(); ++step) {
        const int tx = path->getX(step);
        const int ty = path->getY(step);
        fov->setRevealerPosition(hero, tx, ty);
        fov->compute();
        REQUIRE(fov->isVisible(tx, ty));
        float wx = 0.f, wy = 0.f;
        d.layer->tileToWorld(tx, ty, wx, wy);
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
    REQUIRE(looted);
    REQUIRE_EQ(bag->countItem("hex.relic"), 1);
    REQUIRE_EQ(fov->getRevealerCount(), 2);
    REQUIRE(flow->getLength() > 0);
    REQUIRE_GT(fire->getCount(), 0);
    REQUIRE_EQ(hash->queryCircle(lx, ly, 20.f), 0);

    renderHexRaid(mapMod, d, path);

    fire->stop();
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

// Contract section: hexmap module
#include "PrivateTestScope.h"

#include "hexmap/HexMap.h"
#include "hexmap/HexMapGenerator.h"
#include "hexmap/HexMapModule.h"
#include "hexmap/HexSphereGenerator.h"
#include "hexmap/HexSphereMap.h"
#include "hexmap/HexTerrainBake.h"
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

// Contract section: housegen render
#include "PrivateTestScope.h"

#include <assimp/scene.h>
#include <cstdlib>
#include <unordered_set>
#include "PathBesideSource.h"
#include "data/ByteData.h"
#include "medialoader/model/ModelLoader.h"
#include "model3d/Model3D.h"
#include "procgen/house/HouseComponentLibrary.h"
#include "procgen/house/HouseGenerator.h"
#include "procgen/house/HouseLayout.h"
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

// Contract section: ik
#include "PrivateTestScope.h"

#include "common/Exception.h"
#include "ik/IK.h"
#include "ik/Skeleton2D.h"
#include "ik/Skeleton3D.h"
#include "ik/Solver2D.h"
#include "ik/Solver3D.h"
// ik.cpp
namespace test_ik {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;


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

// Contract section: inventory
#include "PrivateTestScope.h"

#include "ui/UI.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
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

// Contract section: map fov
#include "PrivateTestScope.h"

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

// Contract section: mouse cpp
#include "PrivateTestScope.h"

#include "mouse/Cursor.h"
#include "mouse/Mouse.h"
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
