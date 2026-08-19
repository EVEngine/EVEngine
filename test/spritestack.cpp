#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "common/Exception.h"
#include "filesystem/FileData.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "spritestack/SpriteStack.h"
#include "window/Window.h"
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using eve::graphics::Camera3D;
using eve::graphics::Graphics;
using eve::graphics::Light3D;
using eve::graphics::Renderable2D;
using eve::graphics::Renderable3D;
using eve::graphics::RenderSystem;
using eve::graphics::RenderSystem3D;
using eve::graphics::Texture;
using eve::image::ImageData;
using eve::spritestack::SpriteStack;
using eve::spritestack::SpriteStackBatch;
using eve::spritestack::SpriteStack3D;
using Colorf = ImageData::Colorf;

namespace {

int countAlpha(const ImageData *img) {
    int n = 0;
    const int w = img->getWidth();
    const int h = img->getHeight();
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (img->getPixel(x, y).a > 0.5f) ++n;
    return n;
}

bool centerCovered(const ImageData *img) {
    const int w = img->getWidth();
    const int h = img->getHeight();
    return img->getPixel(w / 2, h / 2).a > 0.5f;
}

std::string testOutDir() { return std::string(EVENGINE_TEST_BINARY_DIR) + "/out"; }

void saveImageDataPng(ImageData *frame, const std::string &path) {
    REQUIRE(frame != nullptr);
    eve::image::Image::create();
    eve::filesystem::FileData *png =
        frame->encode(eve::image::ImageData::FormatHandler::ENCODED_PNG, "spritestack.png", false);
    REQUIRE(png != nullptr);
    REQUIRE(png->getSize() > 0);

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    {
        std::ofstream out(path, std::ios::binary);
        REQUIRE(out.good());
        out.write(static_cast<const char *>(png->getData()),
                  static_cast<std::streamsize>(png->getSize()));
        REQUIRE(out.good());
    }
    delete png;
    std::printf("spritestack render saved: %s\n", path.c_str());
}

void resetScene3D() {
    if (ecs::current()->getManager<Renderable3D>() != nullptr) {
        auto view = ecs::View<Renderable3D, Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [mr] = *it;
            mr->visible = false;
        }
    }
    if (ecs::current()->getManager<Camera3D>() != nullptr) {
        auto camView = ecs::View<Camera3D, Camera3D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data] = *it;
            data->active = false;
        }
    }
    if (ecs::current()->getManager<Renderable2D>() != nullptr) {
        auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [sp] = *it;
            sp->visible = false;
        }
    }
}

}  // namespace

TEST_CASE("spritestack.slice.boxLayers") {
    auto *mod = SpriteStack::create();
    REQUIRE(mod != nullptr);
    CHECK_EQ(mod->getName(), std::string("SpriteStack"));

    std::vector<ImageData *> layers =
        mod->slicePrimitive("box", 6, 48, 48, "z", 0.f);
    REQUIRE_EQ(layers.size(), size_t(6));
    for (auto *img : layers) {
        REQUIRE(img != nullptr);
        CHECK_EQ(img->getWidth(), 48);
        CHECK_EQ(img->getHeight(), 48);
        CHECK_EQ(img->getFormat(), std::string("RGBA8"));
        delete img;
    }
}

TEST_CASE("spritestack.slice.sphereVariesAcrossLayers") {
    // A sphere sliced along Z: middle slabs are wide, outer slabs are thin.
    std::vector<ImageData *> layers =
        SpriteStack::create()->slicePrimitive("sphere", 8, 64, 64, "z", 0.f);
    REQUIRE_EQ(layers.size(), size_t(8));

    const int middle = countAlpha(layers[3]);
    const int outer = countAlpha(layers[0]);
    CHECK_GT(middle, 0);
    CHECK_GT(outer, 0);
    CHECK_GT(middle, outer);  // center slab covers more area than the cap slab
    CHECK(centerCovered(layers[3]));

    for (auto *img : layers) delete img;
}

TEST_CASE("spritestack.slice.axisYTopDown") {
    // Top-down slicing ("y" axis) of a cylinder yields circular layers.
    std::vector<ImageData *> layers =
        SpriteStack::create()->slicePrimitive("cylinder", 6, 64, 64, "y", 0.f);
    REQUIRE_EQ(layers.size(), size_t(6));
    for (int i = 0; i < 6; ++i) {
        REQUIRE(layers[i] != nullptr);
        // Every horizontal cut of a full cylinder covers the center.
        CHECK(centerCovered(layers[i]));
        delete layers[i];
    }
}

TEST_CASE("spritestack.slice.meshArrays") {
    // 2x2x2 box built from raw arrays (unit corners), sliced along Z.
    const float h = 1.f;
    const float pos[] = {
        -h, -h, -h, h, -h, -h, h, h, -h, -h, h, -h,
        -h, -h, h,  h, -h, h,  h, h, h,  -h, h, h,
    };
    const uint32_t idx[] = {0, 1, 2, 0, 2, 3, 5, 4, 7, 5, 7, 6,
                            4, 0, 3, 4, 3, 7, 1, 5, 6, 1, 6, 2,
                            4, 5, 1, 4, 1, 0, 3, 2, 6, 3, 6, 7};
    eve::spritestack::SliceInput in{};
    in.posXYZ = pos;
    in.vertexCount = 8;
    in.indices = idx;
    in.indexCount = int(sizeof(idx) / sizeof(idx[0]));

    eve::spritestack::SliceOptions opt;
    opt.layerCount = 4;
    opt.imageW = 32;
    opt.imageH = 32;
    opt.thickness = 0.5f;  // box depth 2 / 4 slabs
    std::vector<ImageData *> layers = eve::spritestack::sliceMeshToLayers(in, opt);
    REQUIRE_EQ(layers.size(), size_t(4));
    // Every slab of a full box covers its center column.
    for (auto *img : layers) {
        REQUIRE(img != nullptr);
        CHECK(centerCovered(img));
        delete img;
    }

    bool threw = false;
    try {
        eve::spritestack::SliceInput bad{};
        bad.posXYZ = pos;
        bad.vertexCount = 0;
        bad.indices = idx;
        bad.indexCount = 3;
        (void)eve::spritestack::sliceMeshToLayers(bad, opt);
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("spritestack.render.verticalStack") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 256;
    s.height = 192;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    resetScene3D();

    auto *cam = Camera3D::createCamera();
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setEye(3.0f, 2.2f, 4.0f);
    cam->data()->nearZ = 0.1f;
    cam->data()->farZ = 50.f;
    cam->data()->ambientR = 0.30f;
    cam->data()->ambientG = 0.32f;
    cam->data()->ambientB = 0.36f;

    // Slices are self-contained: no Renderable3D needed in the scene.
    auto *hud = Renderable2D::create();
    hud->transform()->x = 0;
    hud->transform()->y = 0;
    hud->sprite()->width = 2;
    hud->sprite()->height = 2;
    hud->sprite()->r = 0.f;
    hud->sprite()->g = 0.f;
    hud->sprite()->b = 0.f;
    hud->sprite()->a = 0.f;
    hud->sprite()->visible = true;

    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(::Color(0.14f, 0.16f, 0.20f, 1.f));
    RenderSystem3D::setDirectionalLight(0.55f, 0.95f, 1.15f, 1.05f, 1.0f, 0.95f);
    std::vector<ImageData *> layers =
        SpriteStack::create()->slicePrimitive("cylinder", 16, 96, 96, "z", 0.f);
    REQUIRE_EQ(layers.size(), size_t(16));

    SpriteStack3D stack;
    stack.setLayerCount(16);
    for (int i = 0; i < 16; ++i) stack.setLayerImage(gfx, layers[i], i);
    stack.setThickness(0.16f);
    stack.setSize(1.6f, 1.6f);
    stack.setPosition(0.f, 0.f, 0.f);
    stack.setTint(0.35f, 0.78f, 0.72f, 1.f);
    auto renderFrame = [&](float yaw) -> ImageData * {
        resetScene3D();
        cam->data()->active = true;
        stack.setYaw(yaw);
        for (int i = 0; i < 3; ++i) {
            RenderSystem3D::render(*gfx);
            stack.render(gfx);
            RenderSystem::render(*gfx);
        }
        return gfx->newImageData();
    };

    std::unique_ptr<ImageData> frameA(renderFrame(0.0f));
    REQUIRE(frameA.get() != nullptr);
    saveImageDataPng(frameA.get(), testOutDir() + "/sprite_stack_vertical.png");

    // The stacked cylinder should paint a visible blob near the screen center.
    int lit = 0;
    const int w = frameA->getWidth();
    const int h = frameA->getHeight();
    for (int y = h / 4; y < 3 * h / 4; ++y) {
        for (int x = w / 4; x < 3 * w / 4; ++x) {
            Colorf c = frameA->getPixel(x, y);
            if (c.r + c.g + c.b > 0.25f && c.a > 0.5f) ++lit;
        }
    }
    CHECK_GT(lit, 400);

    // Rotating the stack around Y must change the rendered pixels.
    std::unique_ptr<ImageData> frameB(renderFrame(1.3f));
    REQUIRE(frameB.get() != nullptr);
    int diff = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Colorf a = frameA->getPixel(x, y);
            Colorf b = frameB->getPixel(x, y);
            if (std::fabs(a.r - b.r) + std::fabs(a.g - b.g) + std::fabs(a.b - b.b) > 0.08f) ++diff;
        }
    }
    CHECK_GT(diff, 64);

    for (auto *img : layers) delete img;
    win->close();
}

TEST_CASE("spritestack.render.gbufferOutline") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 256;
    s.height = 192;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    resetScene3D();

    auto *cam = Camera3D::createCamera();
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setEye(3.2f, 2.8f, 4.2f);
    cam->data()->nearZ = 0.1f;
    cam->data()->farZ = 50.f;

    auto *hud = Renderable2D::create();
    hud->transform()->x = 0;
    hud->transform()->y = 0;
    hud->sprite()->width = 2;
    hud->sprite()->height = 2;
    hud->sprite()->a = 0.f;
    hud->sprite()->visible = true;

    // A mesh so the GBuffer pass has content and the outline pass runs.
    auto *ground = Renderable3D::create();
    ground->meshRenderer()->mesh = gfx->newMeshCylinder(32, 1, true);
    ground->transform()->x = 0.f;
    ground->transform()->y = -1.5f;
    ground->transform()->sx = 6.f;
    ground->transform()->sy = 0.1f;
    ground->transform()->sz = 6.f;
    ground->setTint(0.20f, 0.26f, 0.22f, 1.f);

    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(::Color(0.12f, 0.13f, 0.16f, 1.f));
    gfx->getRenderControl()->enable("outline");

    std::vector<ImageData *> layers =
        SpriteStack::create()->slicePrimitive("cone", 12, 96, 96, "z", 0.f);
    REQUIRE_EQ(layers.size(), size_t(12));

    SpriteStack3D stack;
    stack.setLayerCount(12);
    for (int i = 0; i < 12; ++i) stack.setLayerImage(gfx, layers[i], i);
    stack.setThickness(0.13f);
    stack.setSize(1.5f, 1.6f);
    stack.setPosition(0.f, 0.2f, 0.f);
    stack.setTint(0.45f, 0.55f, 0.35f, 1.f);

    auto renderFrame = [&]() -> ImageData * {
        for (int i = 0; i < 3; ++i) {
            RenderSystem3D::render(*gfx);
            stack.render(gfx);
            RenderSystem::render(*gfx);
        }
        return gfx->newImageData();
    };

    // Without G-buffer participation the outline pass cannot see the slices.
    std::unique_ptr<ImageData> noG(renderFrame());
    REQUIRE(noG.get() != nullptr);

    stack.setGbufferEnabled(true);
    std::unique_ptr<ImageData> withG(renderFrame());
    REQUIRE(withG.get() != nullptr);
    saveImageDataPng(withG.get(), testOutDir() + "/sprite_stack_gbuffer_outline.png");

    // Outline pass paints a dark blue-ish rim (0.05,0.04,0.07) around the stack.
    int rimNoG = 0, rimG = 0;
    const int w = withG->getWidth();
    const int h = withG->getHeight();
    auto countRim = [&](ImageData *f) {
        int n = 0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                Colorf c = f->getPixel(x, y);
                if (std::fabs(c.r - 0.05f) < 0.05f && std::fabs(c.g - 0.04f) < 0.05f &&
                    std::fabs(c.b - 0.07f) < 0.05f && c.r + c.g + c.b < 0.3f)
                    ++n;
            }
        return n;
    };
    rimNoG = countRim(noG.get());
    rimG = countRim(withG.get());
    CHECK_GT(rimG, rimNoG + 20);

    stack.setGbufferEnabled(false);
    for (auto *img : layers) delete img;
    win->close();
}

TEST_CASE("spritestack.render.batchShadow") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 256;
    s.height = 192;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    resetScene3D();

    auto *cam = Camera3D::createCamera();
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setEye(3.2f, 2.8f, 4.2f);
    cam->data()->nearZ = 0.1f;
    cam->data()->farZ = 50.f;

    auto *hud = Renderable2D::create();
    hud->transform()->x = 0;
    hud->transform()->y = 0;
    hud->sprite()->width = 2;
    hud->sprite()->height = 2;
    hud->sprite()->a = 0.f;
    hud->sprite()->visible = true;

    auto *ground = Renderable3D::create();
    ground->meshRenderer()->mesh = gfx->newMeshCylinder(32, 1, true);
    ground->transform()->x = 0.f;
    ground->transform()->y = -1.5f;
    ground->transform()->sx = 6.f;
    ground->transform()->sy = 0.1f;
    ground->transform()->sz = 6.f;
    ground->setTint(0.20f, 0.26f, 0.22f, 1.f);

    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(::Color(0.12f, 0.13f, 0.16f, 1.f));

    std::vector<ImageData *> layers =
        SpriteStack::create()->slicePrimitive("sphere", 10, 64, 64, "z", 0.f);
    REQUIRE_EQ(layers.size(), size_t(10));
    std::vector<Texture *> texs;
    for (auto *img : layers) {
        texs.push_back(gfx->newTexture(img));
        delete img;
    }

    auto *a = new SpriteStack3D();
    auto *b = new SpriteStack3D();
    for (auto *st : {a, b}) {
        st->setLayerCount(10);
        for (int i = 0; i < 10; ++i) st->setLayerTexture(texs[size_t(i)], i);
        st->setThickness(0.14f);
        st->setSize(0.8f, 0.8f);
    }
    a->setPosition(-1.0f, 0.f, 0.f);
    a->setTint(0.8f, 0.45f, 0.25f, 1.f);
    b->setPosition(1.0f, 0.f, 0.f);
    b->setTint(0.3f, 0.65f, 0.85f, 1.f);

    SpriteStackBatch batch;
    batch.add(a);
    batch.add(b);
    b->setShadowEnabled(true);
    b->setShadowOpacity(0.45f);
    b->setShadowLight(0.35f, -1.f, 0.25f);
    b->setShadowPlaneY(-1.45f);

    auto renderFrame = [&]() -> ImageData * {
        for (int i = 0; i < 3; ++i) {
            RenderSystem3D::render(*gfx);
            batch.render(gfx);
            RenderSystem::render(*gfx);
        }
        return gfx->newImageData();
    };

    std::unique_ptr<ImageData> plain(renderFrame());
    REQUIRE(plain.get() != nullptr);
    b->setShadowEnabled(false);
    std::unique_ptr<ImageData> noShadow(renderFrame());
    REQUIRE(noShadow.get() != nullptr);

    // With the shadow batched in, dark pixels appear on the ground.
    int dark = 0;
    const int w = plain->getWidth();
    const int h = plain->getHeight();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Colorf a = noShadow->getPixel(x, y);
            Colorf b = plain->getPixel(x, y);
            if ((a.r + a.g + a.b) - (b.r + b.g + b.b) > 0.12f) ++dark;
        }
    }
    CHECK_GT(dark, 50);

    delete a;
    delete b;
    win->close();
}

TEST_CASE("spritestack.render.atlasStrip") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 256;
    s.height = 192;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    resetScene3D();

    auto *cam = Camera3D::createCamera();
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setEye(3.0f, 2.2f, 4.0f);
    cam->data()->nearZ = 0.1f;
    cam->data()->farZ = 50.f;

    auto *hud = Renderable2D::create();
    hud->transform()->x = 0;
    hud->transform()->y = 0;
    hud->sprite()->width = 2;
    hud->sprite()->height = 2;
    hud->sprite()->a = 0.f;
    hud->sprite()->visible = true;

    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(::Color(0.14f, 0.16f, 0.20f, 1.f));

    // Build a horizontal atlas strip: 6 cells, each a filled circle whose
    // radius shrinks toward the last cell (a cone cross-section set).
    const int cell = 64;
    const int layers = 6;
    auto *strip = new ImageData(cell * layers, cell, "RGBA8");
    for (int li = 0; li < layers; ++li) {
        const float t = float(li) / float(layers - 1);
        const float radius = 0.42f * (1.f - 0.75f * t);
        for (int y = 0; y < cell; ++y) {
            for (int x = 0; x < cell; ++x) {
                const float nx = (float(x) + 0.5f) / float(cell) - 0.5f;
                const float ny = (float(y) + 0.5f) / float(cell) - 0.5f;
                Colorf c{0.f, 0.f, 0.f, 0.f};
                if (nx * nx + ny * ny <= radius * radius)
                    c = Colorf{0.5f + 0.4f * t, 0.7f - 0.35f * t, 0.35f + 0.3f * t, 1.f};
                strip->setPixel(li * cell + x, y, c);
            }
        }
    }
    Texture *atlas = gfx->newTexture(strip);
    delete strip;
    REQUIRE(atlas != nullptr);

    SpriteStack3D stack;
    stack.setLayersFromAtlas(gfx, atlas, layers);
    stack.setThickness(0.16f);
    stack.setSize(1.6f, 1.6f);
    stack.setPosition(0.f, 0.f, 0.f);
    stack.setTint(1.f, 1.f, 1.f, 1.f);

    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        stack.render(gfx);
        RenderSystem::render(*gfx);
    }
    std::unique_ptr<ImageData> frame(gfx->newImageData());
    REQUIRE(frame.get() != nullptr);
    saveImageDataPng(frame.get(), testOutDir() + "/sprite_stack_atlas.png");

    int lit = 0;
    const int w = frame->getWidth();
    const int h = frame->getHeight();
    for (int y = h / 4; y < 3 * h / 4; ++y) {
        for (int x = w / 4; x < 3 * w / 4; ++x) {
            Colorf c = frame->getPixel(x, y);
            if (c.r + c.g + c.b > 0.25f && c.a > 0.5f) ++lit;
        }
    }
    CHECK_GT(lit, 300);

    win->close();
}

TEST_CASE("spritestack.render.csmShadow") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 320;
    s.height = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    resetScene3D();

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 3.5f, 5.5f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.06f, 0.06f, 0.07f);

    // Receiving ground + a directional CSM light (mirrors Shadow3D setup).
    auto *ground = Renderable3D::create();
    ground->meshRenderer()->mesh = gfx->newMeshSphere(16, 8);
    const uint8_t gray[4] = {219, 219, 219, 255};
    ground->meshRenderer()->texture = gfx->newTexture(1, 1, gray);
    ground->transform()->x = 0.f;
    ground->transform()->y = -1.2f;
    ground->transform()->sx = 4.f;
    ground->transform()->sy = 0.15f;
    ground->transform()->sz = 4.f;
    ground->setCastShadow(false);
    ground->setReceiveShadow(true);
    ground->setRoughness(0.85f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.55f, 1.f, 0.35f);
    sun->setColor(1.f, 1.f, 1.f, 2.5f);
    sun->setCastShadow(true);
    sun->setShadowStrength(1.f);
    sun->setShadowBias(0.003f);

    auto *hud = Renderable2D::create();
    hud->transform()->x = 0;
    hud->transform()->y = 0;
    hud->sprite()->width = 2;
    hud->sprite()->height = 2;
    hud->sprite()->a = 0.f;
    hud->sprite()->visible = true;

    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(::Color(0.10f, 0.11f, 0.14f, 1.f));

    std::vector<ImageData *> layers =
        SpriteStack::create()->slicePrimitive("cone", 12, 96, 96, "z", 0.f);
    REQUIRE_EQ(layers.size(), size_t(12));

    SpriteStack3D stack;
    stack.setLayerCount(12);
    for (int i = 0; i < 12; ++i) stack.setLayerImage(gfx, layers[i], i);
    stack.setThickness(0.13f);
    stack.setSize(1.4f, 1.5f);
    stack.setPosition(0.f, 0.2f, 0.f);
    stack.setTint(0.45f, 0.55f, 0.35f, 1.f);
    stack.setCastShadow(true);

    auto renderFrame = [&]() -> ImageData * {
        for (int i = 0; i < 3; ++i) {
            RenderSystem3D::render(*gfx);
            stack.render(gfx);
            RenderSystem::render(*gfx);
        }
        return gfx->newImageData();
    };

    auto lumaGrid = [&](ImageData *f) {
        std::vector<float> out;
        const int w = f->getWidth();
        const int h = f->getHeight();
        out.reserve(size_t(w * h));
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                Colorf c = f->getPixel(x, y);
                out.push_back((c.r + c.g + c.b) / 3.f);
            }
        return out;
    };

    // Casting on -> CSM umbra darkens some ground texels.
    std::unique_ptr<ImageData> on(renderFrame());
    REQUIRE(on.get() != nullptr);
    stack.setCastShadow(false);
    std::unique_ptr<ImageData> off(renderFrame());
    REQUIRE(off.get() != nullptr);
    saveImageDataPng(on.get(), testOutDir() + "/sprite_stack_csm_shadow.png");

    const auto L_on = lumaGrid(on.get());
    const auto L_off = lumaGrid(off.get());
    float best = -1.f;
    const int w = on->getWidth();
    const int h = on->getHeight();
    for (int y = 0; y < h; y += 4) {
        for (int x = 0; x < w; x += 4) {
            const size_t i = size_t(y * w + x);
            best = std::max(best, L_off[i] - L_on[i]);
        }
    }
    CHECK_GT(best, 0.02f);

    for (auto *img : layers) delete img;
    win->close();
}

TEST_CASE("spritestack.render.horizontalMode") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 256;
    s.height = 192;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    resetScene3D();

    auto *cam = Camera3D::createCamera();
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setEye(3.2f, 2.8f, 4.2f);
    cam->data()->nearZ = 0.1f;
    cam->data()->farZ = 50.f;

    auto *hud = Renderable2D::create();
    hud->transform()->x = 0;
    hud->transform()->y = 0;
    hud->sprite()->width = 2;
    hud->sprite()->height = 2;
    hud->sprite()->a = 0.f;
    hud->sprite()->visible = true;

    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(::Color(0.12f, 0.13f, 0.16f, 1.f));

    // Top-down slices of a cone viewed from a 3/4 camera read as a 3D cone.
    std::vector<ImageData *> layers =
        SpriteStack::create()->slicePrimitive("cone", 12, 96, 96, "y", 0.f);
    REQUIRE_EQ(layers.size(), size_t(12));

    SpriteStack3D stack;
    stack.setLayerCount(12);
    for (int i = 0; i < 12; ++i) stack.setLayerImage(gfx, layers[i], i);
    stack.setMode("horizontal");
    stack.setThickness(0.18f);
    stack.setSize(2.2f, 2.2f);
    stack.setPosition(0.f, -1.0f, 0.f);
    stack.setTint(0.55f, 0.42f, 0.30f, 1.f);

    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        stack.render(gfx);
        RenderSystem::render(*gfx);
    }
    std::unique_ptr<ImageData> frame(gfx->newImageData());
    REQUIRE(frame.get() != nullptr);
    saveImageDataPng(frame.get(), testOutDir() + "/sprite_stack_horizontal.png");

    int lit = 0;
    const int w = frame->getWidth();
    const int h = frame->getHeight();
    for (int y = h / 4; y < 3 * h / 4; ++y) {
        for (int x = w / 4; x < 3 * w / 4; ++x) {
            Colorf c = frame->getPixel(x, y);
            if (c.r + c.g + c.b > 0.25f && c.a > 0.5f) ++lit;
        }
    }
    CHECK_GT(lit, 300);

    for (auto *img : layers) delete img;
    win->close();
}

TEST_CASE("spritestack.render.batch") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 256;
    s.height = 192;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    resetScene3D();

    auto *cam = Camera3D::createCamera();
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setEye(3.0f, 2.2f, 4.0f);
    cam->data()->nearZ = 0.1f;
    cam->data()->farZ = 50.f;

    auto *hud = Renderable2D::create();
    hud->transform()->x = 0;
    hud->transform()->y = 0;
    hud->sprite()->width = 2;
    hud->sprite()->height = 2;
    hud->sprite()->a = 0.f;
    hud->sprite()->visible = true;

    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(::Color(0.14f, 0.16f, 0.20f, 1.f));

    // Both stacks share the same layer textures so the batch groups them into
    // one combined mesh per texture (single draw call).
    std::vector<ImageData *> layers =
        SpriteStack::create()->slicePrimitive("sphere", 10, 64, 64, "z", 0.f);
    REQUIRE_EQ(layers.size(), size_t(10));
    std::vector<Texture *> texs;
    for (auto *img : layers) {
        texs.push_back(gfx->newTexture(img));
        delete img;
    }

    auto makeStack = [&](float x, float z) {
        auto *st = new SpriteStack3D();
        st->setLayerCount(10);
        for (int i = 0; i < 10; ++i) st->setLayerTexture(texs[size_t(i)], i);
        st->setThickness(0.14f);
        st->setSize(0.8f, 0.8f);
        st->setPosition(x, 0.f, z);
        return st;
    };

    std::unique_ptr<SpriteStack3D> a(makeStack(-1.0f, 0.f));
    std::unique_ptr<SpriteStack3D> b(makeStack(1.0f, 0.f));
    a->setTint(0.8f, 0.45f, 0.25f, 1.f);
    b->setTint(0.3f, 0.65f, 0.85f, 1.f);

    SpriteStackBatch batch;
    batch.add(a.get());
    batch.add(b.get());
    CHECK_EQ(batch.getStackCount(), 2);

    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        batch.render(gfx);
        RenderSystem::render(*gfx);
    }
    std::unique_ptr<ImageData> frame(gfx->newImageData());
    REQUIRE(frame.get() != nullptr);
    saveImageDataPng(frame.get(), testOutDir() + "/sprite_stack_batch.png");

    // Two batched spheres should paint two distinct lit blobs.
    int litLeft = 0, litRight = 0;
    const int w = frame->getWidth();
    const int h = frame->getHeight();
    for (int y = h / 3; y < 2 * h / 3; ++y) {
        for (int x = w / 4; x < w / 2; ++x) {
            Colorf c = frame->getPixel(x, y);
            if (c.r + c.g + c.b > 0.25f && c.a > 0.5f) ++litLeft;
        }
        for (int x = w / 2; x < 3 * w / 4; ++x) {
            Colorf c = frame->getPixel(x, y);
            if (c.r + c.g + c.b > 0.25f && c.a > 0.5f) ++litRight;
        }
    }
    CHECK_GT(litLeft, 120);
    CHECK_GT(litRight, 120);

    batch.remove(b.get());
    CHECK_EQ(batch.getStackCount(), 1);
    win->close();
}

TEST_CASE("spritestack.render.shadowAndOutline") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 256;
    s.height = 192;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    resetScene3D();

    auto *cam = Camera3D::createCamera();
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setEye(3.2f, 2.8f, 4.2f);
    cam->data()->nearZ = 0.1f;
    cam->data()->farZ = 50.f;

    auto *hud = Renderable2D::create();
    hud->transform()->x = 0;
    hud->transform()->y = 0;
    hud->sprite()->width = 2;
    hud->sprite()->height = 2;
    hud->sprite()->a = 0.f;
    hud->sprite()->visible = true;

    // Ground disc so the projected shadow has a surface to land on.
    auto *ground = Renderable3D::create();
    ground->meshRenderer()->mesh = gfx->newMeshCylinder(32, 1, true);
    ground->transform()->x = 0.f;
    ground->transform()->y = -1.5f;
    ground->transform()->sx = 6.f;
    ground->transform()->sy = 0.1f;
    ground->transform()->sz = 6.f;
    ground->setTint(0.20f, 0.26f, 0.22f, 1.f);

    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(::Color(0.12f, 0.13f, 0.16f, 1.f));

    std::vector<ImageData *> layers =
        SpriteStack::create()->slicePrimitive("cone", 12, 96, 96, "z", 0.f);
    REQUIRE_EQ(layers.size(), size_t(12));

    SpriteStack3D stack;
    stack.setLayerCount(12);
    for (int i = 0; i < 12; ++i) stack.setLayerImage(gfx, layers[i], i);
    stack.setThickness(0.13f);
    stack.setSize(1.5f, 1.6f);
    stack.setPosition(0.f, 0.2f, 0.f);
    stack.setTint(0.45f, 0.55f, 0.35f, 1.f);

    auto renderFrame = [&]() -> ImageData * {
        for (int i = 0; i < 3; ++i) {
            RenderSystem3D::render(*gfx);
            stack.render(gfx);
            RenderSystem::render(*gfx);
        }
        return gfx->newImageData();
    };

    std::unique_ptr<ImageData> plain(renderFrame());
    REQUIRE(plain.get() != nullptr);
    saveImageDataPng(plain.get(), testOutDir() + "/sprite_stack_plain.png");

    // Shadow: dark blob projected onto the ground below/right of the cone.
    stack.setShadowEnabled(true);
    stack.setShadowOpacity(0.42f);
    stack.setShadowLight(0.35f, -1.f, 0.25f);
    stack.setShadowPlaneY(-1.45f);
    std::unique_ptr<ImageData> shadow(renderFrame());
    REQUIRE(shadow.get() != nullptr);
    saveImageDataPng(shadow.get(), testOutDir() + "/sprite_stack_shadow.png");

    int dark = 0;
    const int w = shadow->getWidth();
    const int h = shadow->getHeight();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Colorf a = plain->getPixel(x, y);
            Colorf b = shadow->getPixel(x, y);
            const float diff = (a.r + a.g + a.b) - (b.r + b.g + b.b);
            if (diff > 0.12f) ++dark;
        }
    }
    CHECK_GT(dark, 60);

    // Outline: expanded dark silhouette grows the cone's footprint.
    stack.setShadowEnabled(false);
    stack.setOutline(0.12f, 0.0f, 0.0f, 0.0f);
    std::unique_ptr<ImageData> outlined(renderFrame());
    REQUIRE(outlined.get() != nullptr);
    saveImageDataPng(outlined.get(), testOutDir() + "/sprite_stack_outline.png");

    auto countDarkRim = [&](ImageData *f) {
        int n = 0;
        for (int y = h / 4; y < 3 * h / 4; ++y)
            for (int x = w / 4; x < 3 * w / 4; ++x) {
                Colorf c = f->getPixel(x, y);
                if (c.r + c.g + c.b < 0.15f) ++n;
            }
        return n;
    };
    // The outline is an opaque black rim; count those pixels appearing around
    // the cone's silhouette only when the outline is enabled.
    CHECK_GT(countDarkRim(outlined.get()), countDarkRim(plain.get()) + 30);

    for (auto *img : layers) delete img;
    win->close();
}
