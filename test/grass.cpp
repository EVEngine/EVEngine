#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "filesystem/FileData.h"
#include "graphics/Grass.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Shader.h"
#include "graphics/shaders/mesh3d_grass_frag_spv.inc"
#include "graphics/shaders/mesh3d_grass_vert_spv.inc"
#include "image/Image.h"
#include "image/ImageData.h"
#include "window/Window.h"

#include <SDL2/SDL.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using eve::graphics::Camera3D;
using eve::graphics::Graphics;
using eve::graphics::GrassField;
using eve::graphics::Light3D;
using eve::graphics::Mesh;
using eve::graphics::Renderable2D;
using eve::graphics::Renderable3D;
using eve::graphics::RenderSystem;
using eve::graphics::RenderSystem3D;
using eve::graphics::Shader;
using eve::graphics::Texture;
using eve::graphics::grass::BillboardMesh;
using eve::graphics::grass::Point;
using eve::graphics::grass::SampleParams;

namespace {

void makeTestPlane(std::vector<float> &pos, std::vector<float> &nrm, std::vector<uint32_t> &idx) {
    eve::graphics::grass::makePlane(4.f, 4.f, 4, 4, pos, nrm, idx);
}

float minPairDist(const std::vector<Point> &pts) {
    float best = 1e9f;
    for (size_t i = 0; i < pts.size(); ++i) {
        for (size_t j = i + 1; j < pts.size(); ++j) {
            const glm::vec3 d = pts[i].position - pts[j].position;
            best = std::min(best, glm::length(d));
        }
    }
    return best;
}

}  // namespace

TEST_CASE("graphics.Grass.bindDefaults") {
    Shader sh;
    sh.setKind(Shader::Kind::eMesh3D);
    eve::graphics::grass::bindDefaults(&sh);
    CHECK(sh.hasUniform("time"));
    CHECK(sh.hasUniform("frameDuration"));
    CHECK(sh.hasUniform("lightGreen"));
    CHECK(sh.hasUniform("darkGreen"));
    CHECK(sh.hasUniform("alwaysDark"));
    CHECK_EQ(sh.getUniformIndex("time"), 0);
    CHECK_EQ(sh.getUniformIndex("alwaysDark"), 5);
    CHECK_EQ(sh.getUniformIndex("lightGreen"), 6);
    CHECK_EQ(sh.getUniformIndex("darkGreen"), 9);
    CHECK_EQ(sh.getUniformIndex("frameCount"), 12);
    CHECK_EQ(sh.usedFloats(), 13);
}

TEST_CASE("graphics.Grass.paramNames") {
    CHECK_EQ(eve::graphics::grass::paramCount(), 13);
    CHECK_EQ(eve::graphics::grass::paramName(0), std::string("time"));
    CHECK_EQ(eve::graphics::grass::paramName(5), std::string("alwaysDark"));
    CHECK_EQ(eve::graphics::grass::paramName(12), std::string("frameCount"));
}

TEST_CASE("graphics.Grass.spvMagic") {
    CHECK(mesh3d_grass_vert_spv_count > 0);
    CHECK(mesh3d_grass_frag_spv_count > 0);
    CHECK_EQ(mesh3d_grass_vert_spv[0], 0x07230203u);
    CHECK_EQ(mesh3d_grass_frag_spv[0], 0x07230203u);
}

TEST_CASE("graphics.Grass.poissonMinDistance") {
    std::vector<float> pos, nrm;
    std::vector<uint32_t> idx;
    makeTestPlane(pos, nrm, idx);

    SampleParams p;
    p.radius = 0.45f;
    p.maxPoints = 200;
    p.seed = 7;
    p.minSlopeDot = 0.1f;
    const auto pts =
        eve::graphics::grass::samplePoisson(pos.data(), nrm.data(), int(pos.size() / 3), idx.data(),
                                            int(idx.size()), p);
    CHECK(pts.size() >= 8);
    CHECK(int(pts.size()) <= p.maxPoints);
    const float minD = minPairDist(pts);
    CHECK(minD + 1e-4f >= p.radius);

    for (const auto &pt : pts) {
        CHECK(std::abs(pt.position.y) < 1e-4f);
        CHECK(pt.position.x >= -2.01f);
        CHECK(pt.position.x <= 2.01f);
        CHECK(pt.position.z >= -2.01f);
        CHECK(pt.position.z <= 2.01f);
    }
}

TEST_CASE("graphics.Grass.haltonCountAndCoverage") {
    std::vector<float> pos, nrm;
    std::vector<uint32_t> idx;
    makeTestPlane(pos, nrm, idx);

    const auto pts = eve::graphics::grass::sampleHalton(
        pos.data(), nrm.data(), int(pos.size() / 3), idx.data(), int(idx.size()), 64, 3, 0.1f);
    CHECK_EQ(int(pts.size()), 64);
    CHECK_EQ(pts.front().id, 0u);
    CHECK_EQ(pts.back().id, 63u);

    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (const auto &pt : pts) {
        minX = std::min(minX, pt.position.x);
        maxX = std::max(maxX, pt.position.x);
        minZ = std::min(minZ, pt.position.z);
        maxZ = std::max(maxZ, pt.position.z);
    }
    CHECK(maxX - minX > 2.f);
    CHECK(maxZ - minZ > 2.f);
}

TEST_CASE("graphics.Grass.sparseIsSparser") {
    std::vector<float> pos, nrm;
    std::vector<uint32_t> idx;
    makeTestPlane(pos, nrm, idx);

    SampleParams dense;
    dense.radius = 0.3f;
    dense.maxPoints = 400;
    dense.seed = 1;
    SampleParams sparse = dense;
    sparse.radius = 1.2f;
    sparse.maxPoints = 80;
    sparse.seed = 99;

    const auto d = eve::graphics::grass::samplePoisson(
        pos.data(), nrm.data(), int(pos.size() / 3), idx.data(), int(idx.size()), dense);
    const auto s = eve::graphics::grass::samplePoisson(
        pos.data(), nrm.data(), int(pos.size() / 3), idx.data(), int(idx.size()), sparse);
    CHECK(d.size() > s.size());
    CHECK(s.size() >= 1);
}

TEST_CASE("graphics.Grass.billboardRootPivot") {
    std::vector<Point> pts(1);
    pts[0].position = glm::vec3(3.f, 1.5f, -2.f);
    pts[0].id = 11;
    pts[0].scale = 1.25f;
    const BillboardMesh mesh = eve::graphics::grass::buildBillboards(pts, 0.4f, 0.8f);
    CHECK_EQ(int(mesh.posXYZ.size()), 12);
    CHECK_EQ(int(mesh.uvST.size()), 8);
    CHECK_EQ(int(mesh.indices.size()), 6);
    for (int i = 0; i < 4; ++i) {
        CHECK(std::abs(mesh.posXYZ[size_t(i * 3 + 0)] - 3.f) < 1e-5f);
        CHECK(std::abs(mesh.posXYZ[size_t(i * 3 + 1)] - 1.5f) < 1e-5f);
        CHECK(std::abs(mesh.posXYZ[size_t(i * 3 + 2)] + 2.f) < 1e-5f);
        CHECK(std::abs(mesh.nrmXYZ[size_t(i * 3 + 0)] - 11.f) < 1e-5f);
        CHECK(std::abs(mesh.nrmXYZ[size_t(i * 3 + 1)] - 1.25f) < 1e-5f);
        CHECK(std::abs(mesh.nrmXYZ[size_t(i * 3 + 2)]) < 1e-5f);
    }
    // Bottom edge UVs sit at v=0; their midpoint is the root (u=0.5).
    CHECK(std::abs(mesh.uvST[1]) < 1e-5f);
    CHECK(std::abs(mesh.uvST[3]) < 1e-5f);
    CHECK(std::abs(0.5f * (mesh.uvST[0] + mesh.uvST[2]) - 0.5f) < 1e-5f);
}

TEST_CASE("graphics.Grass.swayDesyncByInstanceId") {
    const int a = eve::graphics::grass::swayFrame(0.f, 0.12f, 0, 4);
    const int b = eve::graphics::grass::swayFrame(0.f, 0.12f, 1, 4);
    const int c = eve::graphics::grass::swayFrame(0.f, 0.12f, 2, 4);
    CHECK(a >= 0);
    CHECK(a < 4);
    CHECK(b >= 0);
    CHECK(b < 4);
    CHECK(c >= 0);
    CHECK(c < 4);
    CHECK((a != b || b != c));

    const int later = eve::graphics::grass::swayFrame(0.48f, 0.12f, 0, 4);
    CHECK(later != a);
}

TEST_CASE("graphics.Grass.swayAtlasFourFrames") {
    std::vector<uint8_t> rgba;
    eve::graphics::grass::makeSwayAtlasRGBA(32, 32, 4, rgba);
    CHECK_EQ(eve::graphics::grass::swayAtlasWidth(32, 4), 128);
    CHECK_EQ(eve::graphics::grass::swayAtlasHeight(32), 32);
    CHECK_EQ(int(rgba.size()), 128 * 32 * 4);

    int opaque = 0;
    int perFrame[4] = {};
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 128; ++x) {
            const uint8_t a = rgba[(size_t(y) * 128u + size_t(x)) * 4u + 3u];
            if (a > 16) {
                ++opaque;
                perFrame[x / 32]++;
            }
        }
    }
    CHECK(opaque > 80);
    for (int f = 0; f < 4; ++f) CHECK(perFrame[f] > 10);

    // Frames actually differ (wind lean).
    int diffs = 0;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const uint8_t a0 = rgba[(size_t(y) * 128u + size_t(x)) * 4u + 3u];
            const uint8_t a3 = rgba[(size_t(y) * 128u + size_t(x + 96)) * 4u + 3u];
            if (a0 != a3) ++diffs;
        }
    }
    CHECK(diffs > 8);
}

TEST_CASE("graphics.Grass.layerFlag") {
    Shader sh;
    sh.setKind(Shader::Kind::eMesh3D);
    eve::graphics::grass::bindDefaults(&sh);
    eve::graphics::grass::bindLayer(&sh, true);
    float dark = 0.f;
    REQUIRE_EQ(sh.getFromVar("alwaysDark", &dark, sizeof(dark)), int(sizeof(dark)));
    CHECK(dark > 0.5f);
    eve::graphics::grass::bindLayer(&sh, false);
    REQUIRE_EQ(sh.getFromVar("alwaysDark", &dark, sizeof(dark)), int(sizeof(dark)));
    CHECK(dark < 0.5f);
}

TEST_CASE("graphics.Grass.billboardAlwaysDarkFlag") {
    std::vector<Point> pts(1);
    pts[0].position = glm::vec3(0.f, 0.f, 0.f);
    pts[0].id = 3;
    pts[0].scale = 1.f;
    const BillboardMesh dark = eve::graphics::grass::buildBillboards(pts, 0.4f, 0.8f, true);
    CHECK_EQ(int(dark.nrmXYZ.size()), 12);
    for (int i = 0; i < 4; ++i) CHECK(dark.nrmXYZ[size_t(i * 3 + 2)] > 0.5f);
}

namespace {

void resetGrassScene() {
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
    if (ecs::current()->getManager<Light3D>() != nullptr) {
        auto lightView = ecs::View<Light3D, Light3D::Data>();
        for (auto it = lightView.begin(); it != lightView.end(); ++it) {
            auto [d] = *it;
            d->enabled = false;
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

Texture *makeSolidTex(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

Mesh *makeGroundMesh(Graphics *gfx, float sizeX, float sizeZ) {
    std::vector<float> pos, nrm;
    std::vector<uint32_t> idx;
    eve::graphics::grass::makePlane(sizeX, sizeZ, 12, 12, pos, nrm, idx);
    std::vector<float> uv(size_t(pos.size() / 3) * 2u, 0.f);
    for (size_t i = 0; i < pos.size() / 3; ++i) {
        uv[i * 2] = pos[i * 3] / sizeX + 0.5f;
        uv[i * 2 + 1] = pos[i * 3 + 2] / sizeZ + 0.5f;
    }
    return gfx->newMeshFromArrays(pos.data(), nrm.data(), uv.data(), int(pos.size() / 3),
                                  idx.data(), int(idx.size()));
}

void savePng(eve::image::ImageData *frame, const std::string &path) {
    REQUIRE(frame != nullptr);
    eve::image::Image::create();
    eve::filesystem::FileData *png =
        frame->encode(medialoader::FormatHandler::ENCODED_PNG, "grass.png", false);
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
    std::printf("grass render saved: %s\n", path.c_str());
}

}  // namespace

TEST_CASE("graphics.Grass.gpuRenderScreenshot") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    gfx->setMsaaSamples(0);
    eve::window::WindowSettings s;
    s.width = 960;
    s.height = 540;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    resetGrassScene();

    auto *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);
    rc->disable("aa");
    rc->disable("msaa");
    rc->disable("ao");
    rc->disable("gi");
    rc->enable("shadow");
    rc->enable("forward");
    rc->compile();

    constexpr float kField = 10.f;
    auto *ground = Renderable3D::create();
    ground->setMesh(makeGroundMesh(gfx, kField, kField));
    ground->setTexture(makeSolidTex(gfx, 92, 72, 42));
    ground->setPosition(0.f, 0.f, 0.f);
    ground->setMetallic(0.f);
    ground->setRoughness(0.92f);
    ground->setCastShadow(false);
    ground->setReceiveShadow(true);

    GrassField::BakeParams bake;
    bake.denseRadius = 0.28f;
    bake.sparseRadius = 1.15f;
    bake.maxDense = 1800;
    bake.maxSparse = 80;
    bake.width = 0.42f;
    bake.height = 0.78f;
    bake.seed = 11;
    bake.minSlopeDot = 0.2f;

    std::unique_ptr<GrassField> field(gfx->newGrassField());
    REQUIRE(field != nullptr);
    field->bakePlane(kField, kField, 16, 16, bake);
    field->setTime(0.22f);
    REQUIRE(field->getDenseCount() >= 40);
    REQUIRE(field->getDenseMesh() != nullptr);
    REQUIRE(field->getShader() != nullptr);
    REQUIRE(field->getAtlas() != nullptr);

    auto *dense = Renderable3D::create();
    dense->setMesh(field->getDenseMesh());
    dense->setTexture(field->getAtlas());
    dense->setShader(field->getShader());
    dense->setCastShadow(false);
    dense->setReceiveShadow(true);

    if (field->getSparseMesh()) {
        auto *sparse = Renderable3D::create();
        sparse->setMesh(field->getSparseMesh());
        sparse->setTexture(field->getAtlas());
        sparse->setShader(field->getShader());
        sparse->setCastShadow(false);
        sparse->setReceiveShadow(true);
    }

    auto *post = Renderable3D::create();
    post->setMesh(gfx->newMeshCylinder(18, 1, true));
    post->setTexture(makeSolidTex(gfx, 168, 148, 122));
    post->setPosition(1.35f, 0.95f, -0.4f);
    post->setScale(0.28f, 0.95f, 0.28f);
    post->setMetallic(0.f);
    post->setRoughness(0.7f);
    post->setCastShadow(true);
    post->setReceiveShadow(false);

    auto *cam = Camera3D::createCamera();
    cam->setEye(6.4f, 4.6f, 7.8f);
    cam->setTarget(0.f, 0.25f, 0.f);
    cam->setAmbient(0.18f, 0.20f, 0.16f);
    cam->data()->nearZ = 0.1f;
    cam->data()->farZ = 80.f;

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.62f, 1.05f, 0.28f);
    sun->setColor(1.f, 0.97f, 0.88f, 2.4f);
    sun->setCastShadow(true);
    sun->setShadowStrength(1.f);
    sun->setShadowBias(0.003f);

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
    gfx->setBackgroundColor(::Color(0.52f, 0.72f, 0.92f, 1.f));

    for (int i = 0; i < 4; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }

    int greenish = 0;
    int darkGreen = 0;
    int litGreen = 0;
    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    for (int y = 0; y < h; y += 2) {
        for (int x = 0; x < w; x += 2) {
            const ::Color c = gfx->getPixel(x, y);
            if (c.g > c.r + 0.04f && c.g > c.b + 0.02f) {
                ++greenish;
                const float luma = (c.r + c.g + c.b) / 3.f;
                if (luma < 0.28f) ++darkGreen;
                if (luma > 0.38f) ++litGreen;
            }
        }
    }
    CHECK(greenish > 80);
    CHECK(darkGreen > 4);
    CHECK(litGreen > 8);

    eve::image::Image::create();
    std::unique_ptr<eve::image::ImageData> frame(gfx->newImageData());
    REQUIRE(frame != nullptr);
    const std::string outDir = std::string(EVENGINE_TEST_BINARY_DIR) + "/out";
    savePng(frame.get(), outDir + "/grass_field.png");
    savePng(frame.get(), "/opt/cursor/artifacts/grass_field.png");

    win->close();
}
