// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "render-window",
  "module": "animation"
}
*/
// clang-format on
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/Animation.h"
#include "animation/SpriteAnim.h"
#include "animation/SpriteClip.h"
#include "animation/SpriteSheet.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Quad.h"
#include "graphics/RenderSystem.h"
#include "graphics/Texture.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "window/Window.h"

#include <cmath>
#include <memory>
#include <string>

TEST_CASE("animation.sprite.rotatedAtlasPixelsAndLayout") {
    auto                       *win = eve::window::Window::create();
    auto                       *gfx = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width  = 320;
    settings.height = 240;
    REQUIRE(win->setWindowSettings(settings));
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("evengine_rotated_atlas", true));
    REQUIRE(fs->setupWriteDirectory());
    [[maybe_unused]] auto *images = eve::image::Image::create();
    // Logical 3x2: R G B / Y M C. Stored clockwise in a 2x3 rect at (1,1).
    eve::image::ImageData               pixels(8, 8, "RGBA8");
    const eve::image::ImageData::Colorf colors[] = {{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1},
                                                    {1, 1, 0, 1}, {1, 0, 1, 1}, {0, 1, 1, 1}};
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 3; ++x) pixels.setPixel(2 - y, 1 + x, colors[y * 3 + x]);
    std::unique_ptr<eve::filesystem::FileData> png(
        pixels.encode(medialoader::FormatHandler::ENCODED_PNG, "atlas.png", true));
    REQUIRE(png != nullptr);
    const std::string json = R"({"frames":{
        "turn":{"rotated":true,"frame":{"x":1,"y":1,"w":3,"h":2},
        "spriteSourceSize":{"x":2,"y":3,"w":3,"h":2},"sourceSize":{"w":8,"h":9}},
        "plain":{"frame":{"x":1,"y":1,"w":2,"h":3}}
    }})";
    fs->write("atlas.json", json.data(), int64_t(json.size()));
    std::unique_ptr<eve::animation::SpriteSheet> sheet(
        eve::animation::Animation::create()->newSpriteSheetFromAtlasJson(gfx, "atlas.png", "atlas.json"));
    const int frame = sheet->findFrame("turn");
    REQUIRE(frame >= 0);
    CHECK_EQ(sheet->getFrameWidth(frame), 3);
    CHECK_EQ(sheet->getFrameHeight(frame), 2);
    CHECK_EQ(sheet->getFrameSourceWidth(frame), 8);
    CHECK_EQ(sheet->getFrameSourceHeight(frame), 9);
    CHECK_EQ(sheet->getFrameOffsetX(frame), 2);
    CHECK_EQ(sheet->getFrameOffsetY(frame), 3);
    // A second import must keep the ResourceManager-owned source image alive.
    std::unique_ptr<eve::animation::SpriteSheet> second(
        eve::animation::Animation::create()->newSpriteSheetFromAtlasJson(gfx, "atlas.png", "atlas.json"));
    CHECK_EQ(second->getFrameWidth(second->findFrame("turn")), 3);

    eve::graphics::Quad quad;
    sheet->applyToQuad(&quad, frame);
    auto *canvas           = gfx->newCanvas(8, 8);
    auto *camera           = eve::graphics::Camera2D::createCamera();
    camera->data()->canvas = canvas;
    camera->data()->active = true;
    camera->data()->x      = 4;
    camera->data()->y      = 4;
    auto *sprite           = eve::graphics::Renderable2D::create();
    sprite->setTexture(sheet->getTexture());
    sprite->setQuad(&quad);
    sprite->setPosition(2, 3);
    sprite->setSize(3, 2);
    sprite->setReceiveLight(false);
    sprite->sprite()->canvas = canvas;
    eve::graphics::RenderSystem::render(*gfx);
    // Check every texel through the Vulkan draw path, including orientation.
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 3; ++x) {
            const auto actual   = canvas->getPixel(2 + x, 3 + y);
            const auto expected = colors[y * 3 + x];
            CHECK(std::abs(actual.r - expected.r) < 0.05f);
            CHECK(std::abs(actual.g - expected.g) < 0.05f);
            CHECK(std::abs(actual.b - expected.b) < 0.05f);
        }
    }
    eve::animation::SpriteClip clip("mixed");
    clip.addFrame(frame, 0.1f);
    clip.addFrame(sheet->findFrame("plain"), 0.1f);
    eve::animation::SpriteAnim player;
    player.setSheet(sheet.get());
    player.bindQuad(&quad);
    sprite->setAnchor(0.25f, 0.75f);
    player.bindSprite(sprite);
    player.play(&clip);
    CHECK_EQ(sprite->sprite()->width, 8);
    CHECK_EQ(sprite->sprite()->height, 9);
    CHECK_EQ(sprite->sprite()->offsetX, 2);
    CHECK_EQ(sprite->sprite()->offsetY, 3);
    CHECK_EQ(sprite->getAnchorX(), 0.25f);
    CHECK_EQ(sprite->getAnchorY(), 0.75f);
    player.setFrame(1);
    CHECK_EQ(quad.getWidth(), 2);
    CHECK_EQ(quad.getHeight(), 3);
    CHECK_EQ(sprite->sprite()->offsetX, 0);
    CHECK_EQ(sprite->sprite()->offsetY, 0);
    eve::graphics::RenderSystem::render(*gfx);
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 2; ++x) {
            const auto actual   = canvas->getPixel(2 + x, 3 + y);
            const auto expected = pixels.getPixel(1 + x, 1 + y);
            CHECK(std::abs(actual.r - expected.r) < 0.05f);
            CHECK(std::abs(actual.g - expected.g) < 0.05f);
            CHECK(std::abs(actual.b - expected.b) < 0.05f);
        }
    }
    const std::string invalid = R"({"frames":{"bad":{"rotated":true,
        "frame":{"x":7,"y":7,"w":3,"h":2}}}})";
    fs->write("invalid.json", invalid.data(), int64_t(invalid.size()));
    CHECK_THROWS(
        (std::unique_ptr<eve::animation::SpriteSheet>(
             eve::animation::Animation::create()->newSpriteSheetFromAtlasJson(gfx, "atlas.png", "invalid.json")),
         false));
    player.bindSprite(nullptr);
    player.bindQuad(nullptr);
    sprite->setVisible(false);
    sprite->setQuad(nullptr);
    camera->data()->active = false;
    win->close();
}
