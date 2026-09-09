#include <memory>
#include <vector>
#include "Fixtures.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "hd2d/Hd2d.h"
#include "image/ImageData.h"
#include "map/Map.h"
#include "map/TileLayer.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("hd2d.sprite.screenAlignedUnderCameraPitchAndRoll") {
    GfxFixture                           fx;
    const unsigned char                  red[] = {255, 0, 0, 255};
    std::unique_ptr<eve::hd2d::Sprite3D> sprite(eve::hd2d::Hd2D::create()->newSprite(fx.gfx));
    sprite->setTexture(fx.gfx->newTexture(1, 1, red));
    sprite->setSize(2.f, 2.f);
    sprite->setPosition(1.f, 0.f, 0.f);
    auto* camera = eve::graphics::Camera3D::createCamera();
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
    auto*      layer = eve::map::Map::create()->newLayer(1, 1, 32.f, 48.f);
    layer->setTile(0, 0, 1);
    eve::hd2d::TileMap3D builder;
    auto*                mesh = builder.buildMesh(fx.gfx, layer);
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
    auto* camera = eve::graphics::Camera3D::createCamera();
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
    auto*      layer = eve::map::Map::create()->newLayer(1, 1, 32.f, 32.f);
    layer->setTile(0, 0, 1);
    eve::hd2d::TileMap3D builder;
    builder.setSideDepth(0.f);
    auto* mesh = builder.buildMesh(fx.gfx, layer);
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
    auto* layer = eve::map::Map::create()->newLayer(4, 2, 2.f, 2.f);
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
    auto* camera = eve::graphics::Camera3D::createCamera();
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
