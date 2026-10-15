#include "ManualPreview.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common/Exception.h"
#include "filesystem/FileData.h"
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
#include "stylize/Stylize.h"
#include "stylize/StyleInstance.h"
#include "stylize/StyleRecipe.h"
#include "window/Window.h"
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using eve::graphics::Canvas;
using eve::graphics::Graphics;
using eve::graphics::Mesh;
using eve::graphics::Renderable2D;
using eve::graphics::Renderable3D;
using eve::graphics::Camera3D;
using eve::graphics::RenderSystem;
using eve::graphics::RenderSystem3D;
using eve::graphics::Shader;
using eve::graphics::Texture;
using eve::image::ImageData;
using eve::stylize::StyleChain;
using eve::stylize::StyleInstance;
using eve::stylize::StylePass;
using eve::stylize::StyleRecipe;
using eve::stylize::Stylize;
using Colorf = ImageData::Colorf;

namespace {

[[maybe_unused]] bool near(float a, float b, float eps = 0.08f) { return std::fabs(a - b) <= eps; }

[[maybe_unused]] float luma(const ::Color &c) { return (c.r + c.g + c.b) / 3.f; }

[[maybe_unused]] ImageData *makeGradient(int w, int h) {
    auto *img = new ImageData(w, h, "RGBA8");
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float u = float(x) / float(std::max(w - 1, 1));
            float v = float(y) / float(std::max(h - 1, 1));
            Colorf c{u, 0.35f + 0.4f * v, 1.f - u, 1.f};
            // Hard edge for Sobel / outline styles.
            if (x > w / 2 && y > h / 3 && y < 2 * h / 3) c = Colorf{0.95f, 0.85f, 0.2f, 1.f};
            img->setPixel(x, y, c);
        }
    }
    return img;
}

[[maybe_unused]] Texture *makeCylinderAlbedo(Graphics *gfx, int size = 256) {
    // Bold horizontal stripes only (no vertical seams — seams become false ink/toon outlines).
    // Avoid near-white bands so PBR/toon highlights keep readable form.
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4);
    const int bandH = size / 5;
    const uint8_t palette[3][3] = {
        {210, 92, 58},   // terracotta
        {232, 186, 110}, // warm sand
        {48, 148, 158},  // teal
    };
    for (int y = 0; y < size; ++y) {
        const int band = (y / std::max(bandH, 1)) % 3;
        for (int x = 0; x < size; ++x) {
            const size_t i = size_t(y * size + x) * 4;
            px[i + 0] = palette[band][0];
            px[i + 1] = palette[band][1];
            px[i + 2] = palette[band][2];
            px[i + 3] = 255;
        }
    }
    return gfx->newTexture(size, size, px.data());
}

[[maybe_unused]] void resetScene3D() {
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

[[maybe_unused]] void saveImageDataPng(ImageData *frame, const std::string &path) {
    REQUIRE(frame != nullptr);
    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();
    eve::filesystem::FileData *png =
        frame->encode(medialoader::FormatHandler::ENCODED_PNG, "stylize.png", false);
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
    std::printf("stylize render saved: %s\n", path.c_str());
}

[[maybe_unused]] std::string testOutDir() {
    return std::string(EVENGINE_TEST_BINARY_DIR) + "/out";
}

/** Render cylinder with optional mesh shader; return screen ImageData (caller owns). */
[[maybe_unused]] ImageData *renderCylinderFrame(Graphics *gfx, Mesh *mesh, Texture *albedo, Shader *meshShader) {
    resetScene3D();

    auto *ent = Renderable3D::create();
    ent->meshRenderer()->mesh = mesh;
    ent->meshRenderer()->texture = albedo;
    ent->meshRenderer()->shader = meshShader;
    ent->meshRenderer()->visible = true;
    ent->setTint(1.f, 1.f, 1.f, 1.f);
    ent->transform()->yaw = 0.55f;
    ent->transform()->pitch = -0.15f;

    auto *cam = Camera3D::createCamera();
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setEye(2.4f, 1.5f, 3.2f);
    cam->data()->nearZ = 0.1f;
    cam->data()->farZ = 50.f;
    // Balanced fill so form reads without crushing shadows to black.
    cam->data()->ambientR = 0.28f;
    cam->data()->ambientG = 0.30f;
    cam->data()->ambientB = 0.34f;

    // Tiny 2D sprite keeps RenderSystem happy / present path consistent with model3d tests.
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
    // Lifted slate so object silhouettes separate cleanly for outline/ink.
    gfx->setBackgroundColor(::Color(0.14f, 0.16f, 0.20f, 1.f));
    // Empirically +Z / camera-side lights the visible face (see RenderSystem3D.lightingAffectsPixels).
    // Keep intensity near 1 so highlights don't blow cel/watercolor pigment to pure white.
    RenderSystem3D::setDirectionalLight(0.55f, 0.95f, 1.15f, 1.05f, 1.0f, 0.95f);

    for (int i = 0; i < 8; ++i) {
        ent->transform()->yaw = 0.45f + float(i) * 0.04f;
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }

    ::Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.12f);
    CHECK(luma(mid) < 0.92f);

    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();
    ImageData *frame = gfx->newImageData();
    REQUIRE(frame != nullptr);
    return frame;
}

[[maybe_unused]] ImageData *applyPostToFrame(Graphics *gfx, ImageData *frame, StylePass *pass) {
    REQUIRE(frame != nullptr);
    REQUIRE(pass != nullptr);
    Texture *tex = gfx->newTexture(frame);
    REQUIRE(tex != nullptr);

    Canvas *rt = gfx->newCanvas(frame->getWidth(), frame->getHeight());
    REQUIRE(rt != nullptr);
    gfx->setCanvas(rt);
    gfx->clear(::Color(0.14f, 0.16f, 0.20f, 1.f), std::nullopt, std::nullopt);
    if (pass->hasParam("time") && pass->getFloat("time") <= 0.f) pass->setTime(0.35f);
    pass->apply(gfx, tex);
    gfx->setCanvas();

    ImageData *out = rt->newImageData();
    REQUIRE(out != nullptr);
    return out;
}

}  // namespace

TEST_CASE("stylize.styles.registry") {
    auto *mod = Stylize::create();
    REQUIRE(mod != nullptr);
    CHECK_EQ(mod->getName(), std::string("Stylize"));
    CHECK_EQ(mod->getStyleCount(), 17);
    CHECK(mod->hasStyle("cartoon"));
    CHECK(mod->hasStyle("watercolor"));
    CHECK(mod->hasStyle("ink"));
    CHECK(mod->hasStyle("pixel"));
    CHECK(mod->hasStyle("xray"));
    CHECK(mod->hasStyle("rim"));
    CHECK(mod->hasStyle("dissolve"));
    CHECK(mod->hasStyle("hologram"));
    CHECK(mod->hasStyle("snow"));
    CHECK(mod->hasStyle("vignette"));
    CHECK(mod->hasStyle("chromatic"));
    CHECK(mod->hasStyle("grain"));
    CHECK(!mod->hasStyle("oil"));
    CHECK(mod->hasMeshStyle("cartoon"));
    CHECK(mod->hasMeshStyle("ink"));
    CHECK(!mod->hasMeshStyle("watercolor"));
    CHECK(!mod->hasMeshStyle("pixel"));
    CHECK(mod->hasMeshStyle("xray"));
    CHECK(mod->hasMeshStyle("rim"));
    CHECK(mod->hasMeshStyle("dissolve"));
    CHECK(mod->hasMeshStyle("hologram"));
    CHECK(mod->hasMeshStyle("snow"));
    CHECK(!mod->hasMeshStyle("vignette"));
    CHECK(!mod->hasMeshStyle("chromatic"));
    CHECK(!mod->hasMeshStyle("grain"));

    CHECK(mod->supports("cartoon", "post"));
    CHECK(mod->supports("cartoon", "mesh"));
    CHECK(mod->supports("watercolor", "cpu"));
    CHECK(!mod->supports("watercolor", "mesh"));
    CHECK(!mod->supports("ink", "gbuffer"));  // current shader is color/Sobel only
    CHECK(mod->supports("xray", "depth"));
    CHECK(!mod->supports("xray", "post"));
    CHECK(!mod->supports("xray", "cpu"));
    CHECK(!mod->supports("oil", "post"));

    CHECK(mod->supports("rim", "mesh"));
    CHECK(!mod->supports("rim", "post"));
    CHECK(mod->supports("vignette", "post"));
    CHECK(!mod->supports("vignette", "mesh"));
    CHECK(!mod->supports("vignette", "cpu"));

    CHECK_GT(mod->getStyleParamCount("pixel"), 0);
    CHECK_EQ(mod->getStyleParamName("pixel", 0), std::string("pixelSize"));
    CHECK(mod->getStyleParamName("pixel", 999).empty());

    bool sawCartoon = false;
    for (int i = 0; i < mod->getStyleCount(); ++i) {
        if (mod->getStyleId(i) == "cartoon") sawCartoon = true;
    }
    CHECK(sawCartoon);
}

TEST_CASE("stylize.processImage.cpuAllStyles") {
    auto *mod = Stylize::create();
    std::unique_ptr<ImageData> src(makeGradient(32, 24));

    for (const char *style : {"cartoon", "watercolor", "ink", "pixel"}) {
        std::unique_ptr<ImageData> out(mod->processImage(src.get(), style));
        REQUIRE(out.get() != nullptr);
        CHECK_EQ(out->getWidth(), 32);
        CHECK_EQ(out->getHeight(), 24);
        CHECK_EQ(out->getFormat(), std::string("RGBA8"));
        Colorf p = out->getPixel(8, 8);
        CHECK(p.a > 0.9f);
        // Must differ from a flat identity copy somewhere in the image.
        bool changed = false;
        for (int y = 0; y < 24 && !changed; ++y) {
            for (int x = 0; x < 32 && !changed; ++x) {
                Colorf a = src->getPixel(x, y);
                Colorf b = out->getPixel(x, y);
                if (!near(a.r, b.r, 0.02f) || !near(a.g, b.g, 0.02f) || !near(a.b, b.b, 0.02f))
                    changed = true;
            }
        }
        CHECK(changed);
    }

    bool threw = false;
    try {
        mod->processImage(src.get(), "unknown");
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("stylize.instance.parameterSchema") {
    auto *mod = Stylize::create();
    std::unique_ptr<StyleInstance> pixel(mod->newInstance("pixel"));
    REQUIRE(pixel.get() != nullptr);
    CHECK_EQ(pixel->getStyle(), std::string("pixel"));
    CHECK_EQ(pixel->getStage(), std::string("afterTonemap"));
    CHECK(pixel->requiresInput("color"));
    CHECK(!pixel->requiresInput("depth"));
    CHECK(pixel->hasParam("pixelSize"));
    CHECK(!pixel->hasParam("texelW"));
    CHECK_EQ(pixel->getFloat("pixelSize"), 5.f);

    pixel->setFloat("pixelSize", 999.f);
    CHECK_EQ(pixel->getFloat("pixelSize"), pixel->getParamMax("pixelSize"));
    CHECK(pixel->isOverridden("pixelSize"));
    pixel->reset("pixelSize");
    CHECK(!pixel->isOverridden("pixelSize"));
    CHECK_EQ(pixel->getFloat("pixelSize"), pixel->getParamDefault("pixelSize"));
}

TEST_CASE("stylize.processImage.inkIsDesaturated") {
    auto *mod = Stylize::create();
    std::unique_ptr<ImageData> src(makeGradient(48, 32));
    std::unique_ptr<ImageData> ink(mod->processImage(src.get(), "ink"));
    REQUIRE(ink.get() != nullptr);
    // Ink wash should collapse toward gray/sepia paper — chroma reduced.
    int nearGray = 0;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 48; ++x) {
            Colorf c = ink->getPixel(x, y);
            float mx = std::max(c.r, std::max(c.g, c.b));
            float mn = std::min(c.r, std::min(c.g, c.b));
            if (mx - mn < 0.25f) ++nearGray;
        }
    }
    CHECK_GT(nearGray, 48 * 32 / 2);
}

TEST_CASE("stylize.processImage.pixelQuantizes") {
    auto *mod = Stylize::create();
    std::unique_ptr<ImageData> src(makeGradient(64, 32));
    std::unique_ptr<ImageData> pix(mod->processImage(src.get(), "pixel"));
    REQUIRE(pix.get() != nullptr);
    // Neighboring pixels inside a 4x4 block should match (UV snap).
    Colorf a = pix->getPixel(0, 0);
    Colorf b = pix->getPixel(1, 1);
    CHECK(near(a.r, b.r, 0.001f));
    CHECK(near(a.g, b.g, 0.001f));
    CHECK(near(a.b, b.b, 0.001f));
}
