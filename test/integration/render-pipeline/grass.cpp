#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
#include "graphics/Material.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;
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
using eve::graphics::grass::PackedAtlasInfo;
using eve::graphics::grass::SampleParams;

namespace {

#include "PathBesideSource.h"
EVE_DEFINE_PATH_BESIDE_SOURCE()

[[maybe_unused]] void makeTestPlane(std::vector<float> &pos, std::vector<float> &nrm, std::vector<uint32_t> &idx) {
    eve::graphics::grass::makePlane(4.f, 4.f, 4, 4, pos, nrm, idx);
}

[[maybe_unused]] float minPairDist(const std::vector<Point> &pts) {
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


TEST_CASE("graphics.Grass.photoModeAuthorityOwnsFourRuntimeMultipliers") {
    auto* gfx=Graphics::create();auto* field=gfx->newGrassField();field->setPhotoModeAuthority(true);
    REQUIRE(field->applyPhotoModeField({"m_globalGrassDensity",eve::PhotoModeDomain::Grass,2.f}).ok());
    REQUIRE(field->applyPhotoModeField({"m_globalGrassDistance",eve::PhotoModeDomain::Grass,3.f}).ok());
    REQUIRE(field->applyPhotoModeField({"m_cameraCellDistance",eve::PhotoModeDomain::Grass,1.5f}).ok());
    REQUIRE(field->applyPhotoModeField({"m_cameraCellSubdivision",eve::PhotoModeDomain::Grass,int64_t{2}}).ok());
    CHECK_EQ(field->getPhotoModeDensity(),2.f);CHECK_EQ(field->getPhotoModeDistance(),3.f);
    CHECK_EQ(field->getPhotoModeCellDistance(),1.5f);CHECK_EQ(field->getPhotoModeCellSubdivision(),2);
    CHECK_EQ(field->getTerrainDetailHardDistance(),472.5f);CHECK_EQ(field->getTerrainDetailDensity(),1.f);
    CHECK(!field->applyPhotoModeField({"m_cameraCellSubdivision",eve::PhotoModeDomain::Grass,int64_t{9}}).ok());
    CHECK_EQ(field->getPhotoModeCellSubdivision(),2);delete field;
}


TEST_CASE("graphics.Grass.fileSwayAtlasPacksFourGrassAndTwoLeaf") {
    const std::vector<std::string> grass = {
        pathBesideThisSource("Texture/grass_sway_0.png"),
        pathBesideThisSource("Texture/grass_sway_1.png"),
        pathBesideThisSource("Texture/grass_sway_2.png"),
        pathBesideThisSource("Texture/grass_sway_3.png"),
    };
    const std::vector<std::string> leaf = {
        pathBesideThisSource("Texture/leaf_sway_0.png"),
        pathBesideThisSource("Texture/leaf_sway_1.png"),
    };
    for (const auto &p : grass) REQUIRE(std::filesystem::exists(p));
    for (const auto &p : leaf) REQUIRE(std::filesystem::exists(p));

    std::vector<uint8_t> rgba;
    PackedAtlasInfo packed;
    eve::graphics::grass::packSwayAtlasRGBA(grass, leaf, rgba, packed);
    CHECK_EQ(packed.grassVariants, 4);
    CHECK_EQ(packed.leafVariants, 2);
    CHECK_EQ(packed.atlasCols, 8);
    CHECK_EQ(packed.atlasRows, 4);
    CHECK_EQ(packed.leafRowOffset, 2);
    CHECK_EQ(packed.frames, 4);
    CHECK(packed.width >= 512);
    CHECK(packed.height >= 512);
    CHECK_EQ(int(rgba.size()), packed.width * packed.height * 4);

    auto opaqueInCell = [&](int col, int row) {
        const int cellW = packed.width / packed.atlasCols;
        const int cellH = packed.height / packed.atlasRows;
        int n = 0;
        for (int y = row * cellH; y < (row + 1) * cellH; ++y) {
            for (int x = col * cellW; x < (col + 1) * cellW; ++x) {
                if (rgba[(size_t(y) * size_t(packed.width) + size_t(x)) * 4u + 3u] > 16) ++n;
            }
        }
        return n;
    };
    // Each grass variant is a 2x2 block on the top two frame-rows.
    for (int v = 0; v < 4; ++v) {
        CHECK(opaqueInCell(v * 2, 0) + opaqueInCell(v * 2 + 1, 0) + opaqueInCell(v * 2, 1) +
                  opaqueInCell(v * 2 + 1, 1) >
              80);
    }
    // Leaf variants occupy the bottom two frame-rows.
    CHECK(opaqueInCell(0, 2) + opaqueInCell(1, 2) + opaqueInCell(0, 3) + opaqueInCell(1, 3) > 80);
    CHECK(opaqueInCell(2, 2) + opaqueInCell(3, 2) + opaqueInCell(2, 3) + opaqueInCell(3, 3) > 80);
}


namespace {

[[maybe_unused]] void resetGrassScene() {
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

[[maybe_unused]] Texture *makeSolidTex(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

[[maybe_unused]] Mesh *makeGroundMesh(Graphics *gfx, float sizeX, float sizeZ) {
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

[[maybe_unused]] void savePng(eve::image::ImageData *frame, const std::string &path) {
    REQUIRE(frame != nullptr);
    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();
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
    std::printf("\ngrass render saved: %s\n", path.c_str());
}

}  // namespace

TEST_CASE("graphics.Grass.gpuRenderScreenshot") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
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
    ground->setTexture(makeSolidTex(gfx, 68, 54, 30));
    ground->setPosition(0.f, 0.f, 0.f);
    ground->setMetallic(0.f);
    ground->setRoughness(0.92f);
    ground->setCastShadow(false);
    ground->setReceiveShadow(true);

    GrassField::BakeParams bake;
    bake.denseRadius = 0.10f;
    bake.sparseRadius = 0.46f;
    bake.maxDense = 7000;
    bake.maxSparse = 220;
    bake.width = 0.58f;
    bake.height = 0.98f;
    bake.seed = 11;
    bake.minSlopeDot = 0.2f;
    bake.grassAtlasFiles = {
        pathBesideThisSource("Texture/grass_sway_0.png"),
        pathBesideThisSource("Texture/grass_sway_1.png"),
        pathBesideThisSource("Texture/grass_sway_2.png"),
        pathBesideThisSource("Texture/grass_sway_3.png"),
    };
    bake.leafAtlasFiles = {
        pathBesideThisSource("Texture/leaf_sway_0.png"),
        pathBesideThisSource("Texture/leaf_sway_1.png"),
    };

    GrassField *field = gfx->newGrassField();
    REQUIRE(field != nullptr);
    field->bakePlane(kField, kField, 16, 16, bake);
    field->setTime(0.22f);
    std::printf("\ngrass dense=%d sparse=%d\n", field->getDenseCount(), field->getSparseCount());
    REQUIRE(field->getDenseCount() >= 800);
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
    cam->setEye(4.8f, 2.9f, 5.9f);
    cam->setTarget(0.f, 0.45f, 0.f);
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

    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();
    eve::image::ImageData *frame = gfx->newImageData();
    REQUIRE(frame != nullptr);
    std::unique_ptr<eve::image::ImageData> ownedFrame(frame);

    int greenish = 0;
    int darkGreen = 0;
    int litGreen = 0;
    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    for (int y = 0; y < h; y += 2) {
        for (int x = 0; x < w; x += 2) {
            const auto c = frame->getPixel(x, y);
            if (c.g > c.r + 0.04f && c.g > c.b + 0.02f) {
                ++greenish;
                const float luma = (c.r + c.g + c.b) / 3.f;
                if (luma < 0.28f) ++darkGreen;
                if (luma > 0.38f) ++litGreen;
            }
        }
    }
    // This fixed-size scene contains a broad field spanning multiple cascades.
    // A WebGPU regression that left empty cascade layers uncleared turned the
    // whole field dark while still satisfying the old handful-of-pixels check.
    CHECK(greenish > 50000);
    CHECK(darkGreen > 100);
    CHECK(litGreen > 50000);

    const std::string outDir = std::string(EVENGINE_TEST_BINARY_DIR) + "/out";
    savePng(frame, outDir + "/grass_field.png");
    savePng(frame, "/opt/cursor/artifacts/grass_field.png");

    std::vector<uint8_t> atlasRgba;
    PackedAtlasInfo atlasInfo;
    eve::graphics::grass::packSwayAtlasRGBA(bake.grassAtlasFiles, bake.leafAtlasFiles, atlasRgba,
                                            atlasInfo);
    auto *atlasImg = new eve::image::ImageData(atlasInfo.width, atlasInfo.height, "RGBA8",
                                               atlasRgba.data(), false);
    savePng(atlasImg, outDir + "/grass_atlas.png");
    savePng(atlasImg, "/opt/cursor/artifacts/grass_atlas.png");
    delete atlasImg;

    win->close();
}
