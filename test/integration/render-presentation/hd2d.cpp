#include "hd2d/Hd2d.h"
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
#include "image/Image.h"
#include "image/ImageData.h"
#include "map/Map.h"
#include "map/TileLayer.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
