#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Shader.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "stylize/Stylize.h"
#include "window/Window.h"

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
using eve::stylize::StylePass;
using eve::stylize::Stylize;
using Colorf = ImageData::Colorf;

namespace {

bool near(float a, float b, float eps = 0.08f) { return std::fabs(a - b) <= eps; }

float luma(const ::Color &c) { return (c.r + c.g + c.b) / 3.f; }

ImageData *makeGradient(int w, int h) {
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

Texture *makeCylinderAlbedo(Graphics *gfx, int size = 128) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4);
    const int cell = size / 8;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool dark = ((x / cell) ^ (y / cell)) & 1;
            // Warm terracotta / cream checker so NPR styles stay readable.
            const uint8_t r = dark ? 196 : 238;
            const uint8_t g = dark ? 92 : 214;
            const uint8_t b = dark ? 64 : 180;
            const size_t i = size_t(y * size + x) * 4;
            px[i + 0] = r;
            px[i + 1] = g;
            px[i + 2] = b;
            px[i + 3] = 255;
        }
    }
    return gfx->newTexture(size, size, px.data());
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

void saveImageDataPng(ImageData *frame, const std::string &path) {
    REQUIRE(frame != nullptr);
    eve::image::Image::create();
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

std::string docsStylizeDir() {
#ifdef EVENGINE_SOURCE_DIR
    return std::string(EVENGINE_SOURCE_DIR) + "/docs/images/stylize";
#else
    std::string here = __FILE__;
    auto slash = here.find_last_of("/\\");
    std::string dir = (slash == std::string::npos) ? std::string(".") : here.substr(0, slash);
    return dir + "/../docs/images/stylize";
#endif
}

std::string testOutDir() {
    return std::string(EVENGINE_TEST_BINARY_DIR) + "/out";
}

/** Render cylinder with optional mesh shader; return screen ImageData (caller owns). */
ImageData *renderCylinderFrame(Graphics *gfx, Mesh *mesh, Texture *albedo, Shader *meshShader) {
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
    cam->data()->ambientR = 0.22f;
    cam->data()->ambientG = 0.23f;
    cam->data()->ambientB = 0.28f;

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
    gfx->setBackgroundColor(::Color(0.14f, 0.16f, 0.20f, 1.f));
    RenderSystem3D::setDirectionalLight(-0.45f, -0.85f, -0.35f, 1.7f, 1.65f, 1.55f);

    for (int i = 0; i < 8; ++i) {
        ent->transform()->yaw = 0.45f + float(i) * 0.04f;
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }

    ::Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);

    eve::image::Image::create();
    ImageData *frame = gfx->newImageData();
    REQUIRE(frame != nullptr);
    return frame;
}

ImageData *applyPostToFrame(Graphics *gfx, ImageData *frame, StylePass *pass) {
    REQUIRE(frame != nullptr);
    REQUIRE(pass != nullptr);
    Texture *tex = gfx->newTexture(frame);
    REQUIRE(tex != nullptr);

    Canvas *rt = gfx->newCanvas(frame->getWidth(), frame->getHeight());
    REQUIRE(rt != nullptr);
    gfx->setCanvas(rt);
    gfx->clear(::Color(0.14f, 0.16f, 0.20f, 1.f), std::nullopt, std::nullopt);
    if (pass->hasParam("time")) pass->setTime(0.35f);
    if (pass->hasParam("pixelSize")) pass->setFloat("pixelSize", 3.f);
    if (pass->hasParam("paletteSteps")) pass->setFloat("paletteSteps", 10.f);
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
    CHECK_EQ(mod->getStyleCount(), 4);
    CHECK(mod->hasStyle("cartoon"));
    CHECK(mod->hasStyle("watercolor"));
    CHECK(mod->hasStyle("ink"));
    CHECK(mod->hasStyle("pixel"));
    CHECK(!mod->hasStyle("oil"));
    CHECK(mod->hasMeshStyle("cartoon"));
    CHECK(mod->hasMeshStyle("ink"));
    CHECK(!mod->hasMeshStyle("watercolor"));
    CHECK(!mod->hasMeshStyle("pixel"));

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

TEST_CASE("stylize.gpu.postPassAndMeshShader") {
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

    auto *mod = Stylize::create();

    for (const char *style : {"cartoon", "watercolor", "ink", "pixel"}) {
        StylePass *pass = mod->newPass(gfx, style);
        REQUIRE(pass != nullptr);
        CHECK_EQ(pass->getStyle(), std::string(style));
        REQUIRE(pass->getShader() != nullptr);
        REQUIRE(pass->getShader()->gpuHandle != nullptr);
        CHECK(pass->hasParam("texelW"));
        if (pass->hasParam("time")) pass->setTime(0.25f);

        std::unique_ptr<ImageData> img(makeGradient(64, 48));
        auto *tex = gfx->newTexture(img.get());
        REQUIRE(tex != nullptr);

        Canvas *rt = gfx->newCanvas(128, 96);
        REQUIRE(rt != nullptr);
        gfx->setCanvas(rt);
        gfx->clear(::Color(0.1f, 0.1f, 0.1f, 1.f), std::nullopt, std::nullopt);
        pass->apply(gfx, tex);
        gfx->setCanvas();

        ::Color p = rt->getPixel(64, 48);
        CHECK_GT(p.a, 0.5f);
        delete pass;
    }

    Shader *toonMesh = mod->newMeshShader(gfx, "cartoon");
    REQUIRE(toonMesh != nullptr);
    REQUIRE(toonMesh->gpuHandle != nullptr);
    CHECK(toonMesh->hasUniform("bands"));

    Shader *inkMesh = mod->newMeshShader(gfx, "ink");
    REQUIRE(inkMesh != nullptr);
    REQUIRE(inkMesh->gpuHandle != nullptr);
    CHECK(inkMesh->hasUniform("washLevels"));

    bool threw = false;
    try {
        mod->newMeshShader(gfx, "watercolor");
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("stylize.render.cylinderStyleGallery") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 512;
    s.height = 384;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    Mesh *cylinder = gfx->newMeshCylinder(48, 8, true);
    REQUIRE(cylinder != nullptr);
    CHECK_GT(cylinder->indexCount, 0);

    Texture *albedo = makeCylinderAlbedo(gfx, 128);
    REQUIRE(albedo != nullptr);

    auto *mod = Stylize::create();
    const std::string docsDir = docsStylizeDir();
    const std::string outDir = testOutDir();

    // cartoon: object-space cel mesh shader.
    // ink / watercolor / pixel: image-space post on the same lit base frame
    // (ink post reads better as xuan-paper wash than the dark mesh variant).
    struct StyleJob {
        const char *id;
        bool useMesh;
    };
    const StyleJob jobs[] = {
        {"cartoon", true},
        {"ink", false},
        {"watercolor", false},
        {"pixel", false},
    };

    for (const StyleJob &job : jobs) {
        Shader *meshSh = nullptr;
        if (job.useMesh) {
            meshSh = mod->newMeshShader(gfx, job.id);
            REQUIRE(meshSh != nullptr);
        }

        std::unique_ptr<ImageData> frame(renderCylinderFrame(gfx, cylinder, albedo, meshSh));
        REQUIRE(frame.get() != nullptr);

        std::unique_ptr<ImageData> styled;
        if (!job.useMesh) {
            StylePass *pass = mod->newPass(gfx, job.id);
            REQUIRE(pass != nullptr);
            if (std::string(job.id) == "watercolor") {
                pass->setFloat("blurAmount", 1.2f);
                pass->setFloat("edgeDarken", 1.1f);
                pass->setFloat("paperStrength", 0.4f);
                pass->setFloat("distortion", 0.55f);
                pass->setFloat("bleed", 0.45f);
                pass->setFloat("saturation", 0.9f);
                pass->setFloat("granulation", 0.35f);
            } else if (std::string(job.id) == "ink") {
                pass->setFloat("inkContrast", 1.15f);
                pass->setFloat("washLevels", 6.f);
                pass->setFloat("edgeThreshold", 0.22f);
                pass->setFloat("diffusion", 2.0f);
                pass->setFloat("inkDensity", 0.7f);
                pass->setFloat("edgeStrength", 1.0f);
            } else if (std::string(job.id) == "pixel") {
                pass->setFloat("pixelSize", 4.f);
                pass->setFloat("paletteSteps", 12.f);
                pass->setFloat("ditherStrength", 0.22f);
                pass->setFloat("toonBands", 3.f);
                pass->setFloat("sharpness", 1.f);
            }
            styled.reset(applyPostToFrame(gfx, frame.get(), pass));
            delete pass;
        } else {
            styled.reset(frame.release());
        }
        REQUIRE(styled.get() != nullptr);

        const std::string name = std::string("cylinder_") + job.id + ".png";
        saveImageDataPng(styled.get(), outDir + "/" + name);
        saveImageDataPng(styled.get(), docsDir + "/" + name);

        // Sanity: some non-background pixels exist (cylinder albedo is warm).
        int warm = 0;
        const int w = styled->getWidth();
        const int h = styled->getHeight();
        for (int y = h / 4; y < 3 * h / 4; y += 4) {
            for (int x = w / 4; x < 3 * w / 4; x += 4) {
                Colorf c = styled->getPixel(x, y);
                if (c.r + c.g + c.b > 0.2f) ++warm;
            }
        }
        CHECK_GT(warm, 20);
    }

    // Also keep an unstyled baseline next to the gallery for comparison.
    {
        std::unique_ptr<ImageData> base(renderCylinderFrame(gfx, cylinder, albedo, nullptr));
        saveImageDataPng(base.get(), outDir + "/cylinder_base.png");
        saveImageDataPng(base.get(), docsDir + "/cylinder_base.png");
    }

    win->close();
}
