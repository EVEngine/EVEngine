#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Exception.h"
#include "filesystem/FileData.h"
#include "graphics/Color.h"
#include "graphics/Graphics.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "spritestack/SpriteStack.h"
#include "window/Window.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>

using eve::graphics::Color;
using eve::graphics::Graphics;
using eve::image::ImageData;
using eve::spritestack::SpriteStack;
using eve::spritestack::SpriteStack2D;
using eve::spritestack::SpriteStackBatch;
using Colorf = ImageData::Colorf;

namespace {
int countAlpha(const ImageData *img) {
    int count = 0;
    for (int y = 0; y < img->getHeight(); ++y)
        for (int x = 0; x < img->getWidth(); ++x)
            if (img->getPixel(x, y).a > 0.5f) ++count;
    return count;
}

std::string testOutDir() { return std::string(EVENGINE_TEST_BINARY_DIR) + "/out"; }

void savePng(ImageData *frame, const std::string &path) {
    REQUIRE(frame != nullptr);
    eve::image::Image::create();
    std::unique_ptr<eve::filesystem::FileData> png(
        frame->encode(ImageData::FormatHandler::ENCODED_PNG, "spritestack2d.png", false));
    REQUIRE(png.get() != nullptr);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    out.write(static_cast<const char *>(png->getData()), std::streamsize(png->getSize()));
    REQUIRE(out.good());
}

struct WindowFixture {
    eve::window::Window *win = eve::window::Window::create();
    Graphics *gfx = Graphics::create();
    WindowFixture() {
        REQUIRE(win != nullptr);
        REQUIRE(gfx != nullptr);
        eve::window::WindowSettings settings;
        settings.width = 320;
        settings.height = 240;
        settings.centered = true;
        REQUIRE(win->setWindowSettings(settings));
        gfx->setScreenReadbackEnabled(true);
        gfx->setBackgroundColor(Color(0.07f, 0.09f, 0.12f, 1.f));
    }
    ~WindowFixture() { win->close(); }
};

std::unique_ptr<ImageData> renderStack(Graphics *gfx, const SpriteStack2D &stack) {
    for (int i = 0; i < 3; ++i) {
        gfx->clear(std::nullopt, std::nullopt, std::nullopt);
        stack.render(gfx);
        gfx->present();
    }
    return std::unique_ptr<ImageData>(gfx->newImageData());
}
}

TEST_CASE("spritestack.slice.boxLayers") {
    auto layers = SpriteStack::create()->slicePrimitive("box", 6, 48, 48, "y", 0.f);
    REQUIRE_EQ(layers.size(), size_t(6));
    for (ImageData *layer : layers) {
        REQUIRE(layer != nullptr);
        CHECK_GT(countAlpha(layer), 100);
        delete layer;
    }
}

TEST_CASE("spritestack.slice.sphereVariesAcrossLayers") {
    auto layers = SpriteStack::create()->slicePrimitive("sphere", 9, 64, 64, "y", 0.f);
    REQUIRE_EQ(layers.size(), size_t(9));
    const int edge = countAlpha(layers.front());
    const int middle = countAlpha(layers[layers.size() / 2]);
    CHECK_GT(middle, edge);
    for (ImageData *layer : layers) delete layer;
}

TEST_CASE("spritestack.render.pure2DStack") {
    WindowFixture fx;
    auto layers = SpriteStack::create()->slicePrimitive("cone", 14, 64, 64, "y", 0.f);
    SpriteStack2D stack;
    stack.setLayerCount(int(layers.size()));
    for (int i = 0; i < int(layers.size()); ++i) stack.setLayerImage(fx.gfx, layers[size_t(i)], i);
    stack.setPosition(160.f, 155.f);
    stack.setSize(96.f, 96.f);
    stack.setThickness(3.f);
    stack.setRotation(25.f);
    stack.setTint(0.44f, 0.78f, 0.58f, 1.f);
    stack.setShadowEnabled(true);
    stack.setShadowOffset(8.f, 6.f);
    stack.setOutline(1.5f, 0.02f, 0.03f, 0.04f);

    auto frame = renderStack(fx.gfx, stack);
    REQUIRE(frame.get() != nullptr);
    savePng(frame.get(), testOutDir() + "/sprite_stack_2d.png");
    int changed = 0;
    for (int y = 30; y < 210; ++y) {
        for (int x = 60; x < 260; ++x) {
            const Colorf pixel = frame->getPixel(x, y);
            if (pixel.g > 0.20f && pixel.g > pixel.r * 1.15f) ++changed;
        }
    }
    CHECK_GT(changed, 500);
    for (ImageData *layer : layers) delete layer;
}

TEST_CASE("spritestack.render.rotationChangesPixels") {
    WindowFixture fx;
    auto layers = SpriteStack::create()->slicePrimitive("box", 8, 48, 48, "y", 0.f);
    SpriteStack2D stack;
    stack.setLayerCount(int(layers.size()));
    for (int i = 0; i < int(layers.size()); ++i) stack.setLayerImage(fx.gfx, layers[size_t(i)], i);
    stack.setPosition(160.f, 140.f);
    stack.setSize(100.f, 68.f);
    stack.setThickness(3.f);
    auto before = renderStack(fx.gfx, stack);
    stack.setRotation(37.f);
    auto after = renderStack(fx.gfx, stack);
    int diff = 0;
    for (int y = 0; y < before->getHeight(); ++y)
        for (int x = 0; x < before->getWidth(); ++x) {
            const Colorf a = before->getPixel(x, y);
            const Colorf b = after->getPixel(x, y);
            if (std::fabs(a.r - b.r) + std::fabs(a.g - b.g) + std::fabs(a.b - b.b) > 0.08f)
                ++diff;
        }
    CHECK_GT(diff, 300);
    for (ImageData *layer : layers) delete layer;
}

TEST_CASE("spritestack.render.batchUses2DStacks") {
    WindowFixture fx;
    auto layers = SpriteStack::create()->slicePrimitive("sphere", 8, 48, 48, "y", 0.f);
    SpriteStack2D left;
    SpriteStack2D right;
    for (SpriteStack2D *stack : {&left, &right}) {
        stack->setLayerCount(int(layers.size()));
        for (int i = 0; i < int(layers.size()); ++i) stack->setLayerImage(fx.gfx, layers[size_t(i)], i);
        stack->setSize(70.f, 70.f);
        stack->setThickness(2.f);
    }
    left.setPosition(105.f, 145.f);
    right.setPosition(215.f, 145.f);
    right.setRotation(30.f);
    SpriteStackBatch batch;
    batch.add(&left);
    batch.add(&right);
    CHECK_EQ(batch.getStackCount(), 2);
    for (int i = 0; i < 3; ++i) {
        fx.gfx->clear(std::nullopt, std::nullopt, std::nullopt);
        batch.render(fx.gfx);
        fx.gfx->present();
    }
    std::unique_ptr<ImageData> frame(fx.gfx->newImageData());
    savePng(frame.get(), testOutDir() + "/sprite_stack_2d_batch.png");
    for (ImageData *layer : layers) delete layer;
}
