#include "RenderImageAudit.h"
#include "ResourceTestSupport.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>
#include "filesystem/FileData.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <assimp/mesh.h>
#include <glm/gtc/matrix_transform.hpp>

#include "animation/AnimClip.h"
#include "animation/AnimImporter.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"
#include "avatar/Avatar.h"
#include "data/ByteData.h"
#include "filesystem/Filesystem.h"
#include "font/Font.h"
#include "font/FontData.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/ClipSpace.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Outline.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/TextureSampler.h"
#include "graphics/Volumetric.h"
#include "graphics/shaders/custom2d_frag_spv.inc"
#include "image/Image.h"
#include "image/ImageData.h"
#include "map/DualGrid.h"
#include "map/Fov.h"
#include "map/Map.h"
#include "map/TileLayer.h"
#include "map/TileOrientation.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#include "physics/Body.h"
#include "physics/Fluid2D.h"
#include "physics/Physics.h"
#include "physics/World.h"
#include "physics/cloth/Cloth.h"
#include "procgen/Params.h"
#include "procgen/Procgen.h"
#include "scene/NodeDesc.h"
#include "scene/SceneHost.h"
#include "scene/TransformSystem.h"
#include "stylize/Stylize.h"
#include "ui/UI.h"
#include "voxel/VoxelWorld.h"
#include "window/Window.h"

using eve::image::ImageData;
using namespace eve::graphics;

// The CPU-side framebuffer-audit implementation -- auditRgba8 / auditImage /
// auditFlickerRgba8 / auditFlicker / mergeAudit / paintDefectOverlay /
// appendAuditReport / saveAuditOverlay, RenderAuditResult::hasSevere and their
// tile/scan helpers -- lives in RenderImageAuditIo.cpp.
//
// Reason: a `unit_test_<domain>` executable is its own link unit, and the procgen
// domain (test/procgen.cpp) calls auditImage while this file is graphics-only and
// carries graphics-only TEST_CASEs. The shared helper TU is compiled into every
// consuming domain (scripts/test_domains.py SHARED_SOURCES); see RenderImageAuditIo.cpp.

namespace {

[[maybe_unused]] ImageData *makeRgba(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    auto *img = new ImageData(w, h, "RGBA8");
    auto *p = static_cast<uint8_t *>(img->getData());
    for (int i = 0; i < w * h; ++i) {
        p[i * 4 + 0] = r;
        p[i * 4 + 1] = g;
        p[i * 4 + 2] = b;
        p[i * 4 + 3] = 255;
    }
    return img;
}

[[maybe_unused]] void fillRect(ImageData *img, int x0, int y0, int rw, int rh, uint8_t r, uint8_t g, uint8_t b) {
    const int w = img->getWidth();
    const int h = img->getHeight();
    auto *p = static_cast<uint8_t *>(img->getData());
    for (int y = y0; y < y0 + rh && y < h; ++y) {
        for (int x = x0; x < x0 + rw && x < w; ++x) {
            uint8_t *q = p + (y * w + x) * 4;
            q[0] = r;
            q[1] = g;
            q[2] = b;
            q[3] = 255;
        }
    }
}

[[maybe_unused]] bool hasKind(const RenderAuditResult &r, RenderDefect::Kind k) {
    for (const auto &d : r.defects)
        if (d.kind == k) return true;
    return false;
}

[[maybe_unused]] std::string auditOutDir() {
    return std::string(EVENGINE_TEST_BINARY_DIR) + "/out/classic_scenes";
}

[[maybe_unused]] Texture *makeSolid(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

[[maybe_unused]] Texture *makeStudioCubemap(Graphics *gfx, int face = 8) {
    const uint8_t faceRgb[6][3] = {
        {220, 180, 140}, {120, 150, 210}, {245, 245, 250}, {40, 40, 45}, {200, 210, 230}, {180, 160, 140},
    };
    const size_t faceBytes = size_t(face) * size_t(face) * 4u;
    std::vector<uint8_t> faces(faceBytes * 6u);
    for (int f = 0; f < 6; ++f) {
        uint8_t *dst = faces.data() + size_t(f) * faceBytes;
        for (size_t i = 0; i < faceBytes; i += 4) {
            dst[i + 0] = faceRgb[f][0];
            dst[i + 1] = faceRgb[f][1];
            dst[i + 2] = faceRgb[f][2];
            dst[i + 3] = 255;
        }
    }
    return gfx->newCubemap(face, faces.data());
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
            data->envMap = nullptr;
            data->envIntensity = 1.f;
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
    if (ecs::current()->getManager<Light2D>() != nullptr) {
        auto lightView = ecs::View<Light2D, Light2D::Data>();
        for (auto it = lightView.begin(); it != lightView.end(); ++it) {
            auto [d] = *it;
            d->enabled = false;
        }
    }
    if (ecs::current()->getManager<Camera2D>() != nullptr) {
        auto camView = ecs::View<Camera2D, Camera2D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [d] = *it;
            d->active = false;
        }
    }
    if (ecs::current()->getManager<eve::particles::ParticleEmitter>() != nullptr) {
        auto view = ecs::View<eve::particles::ParticleEmitter, eve::particles::ParticleEmitter::Draw>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [d] = *it;
            d->visible = false;
        }
    }
    if (ecs::current()->getManager<eve::map::TileLayer>() != nullptr) {
        auto view = ecs::View<eve::map::TileLayer, eve::map::TileLayer::Draw>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [d] = *it;
            d->visible = false;
        }
    }
}

[[maybe_unused]] void auditGpuFrame(Graphics *gfx, const char *scene, const char *phase, const RenderAuditBg &bg,
                   bool flicker = false, const std::function<void()> &after3D = {},
                   const char *rawCaptureName = nullptr) {
    auto drawOnce = [&]() {
        RenderSystem3D::render(*gfx);
        if (after3D) after3D();
        RenderSystem::render(*gfx);
    };
    gfx->setScreenReadbackEnabled(false);
    for (int i = 0; i < 2; ++i) drawOnce();
    gfx->setScreenReadbackEnabled(true);
    drawOnce();
    std::unique_ptr<ImageData> a(gfx->newImageData());
    REQUIRE(a.get() != nullptr);
    if (rawCaptureName)
        REQUIRE(saveImagePng(*a, auditOutDir() + "/" + rawCaptureName));
    RenderAuditConfig cfg{scene, phase, "", 0};
    auto result = auditImage(*a, cfg, bg, /*step=*/2);
    if (flicker) {
        drawOnce();
        std::unique_ptr<ImageData> b(gfx->newImageData());
        REQUIRE(b.get() != nullptr);
        mergeAudit(result, auditFlicker(*a, *b, cfg, bg, /*step=*/2));
    }
    gfx->setScreenReadbackEnabled(false);
    if (result.hasDefects()) {
        appendAuditReport(auditOutDir(), result);
        saveAuditOverlay(*a, result, auditOutDir());
    }
    REQUIRE(result.interiorOccupancy > 0.03f);
    REQUIRE(!result.hasSevere());
}

[[maybe_unused]] void auditGpu2D(Graphics *gfx, const char *scene, const char *phase, const RenderAuditBg &bg) {
    gfx->setScreenReadbackEnabled(false);
    for (int i = 0; i < 2; ++i) RenderSystem::render(*gfx);
    gfx->setScreenReadbackEnabled(true);
    RenderSystem::render(*gfx);
    std::unique_ptr<ImageData> a(gfx->newImageData());
    REQUIRE(a.get() != nullptr);
    RenderAuditConfig cfg{scene, phase, "", 0};
    auto result = auditImage(*a, cfg, bg, /*step=*/2);
    gfx->setScreenReadbackEnabled(false);
    if (result.hasDefects()) {
        appendAuditReport(auditOutDir(), result);
        saveAuditOverlay(*a, result, auditOutDir());
    }
    REQUIRE(result.interiorOccupancy > 0.03f);
    REQUIRE(!result.hasSevere());
}

[[maybe_unused]] void blitAndPresent(Graphics *gfx, Texture *tex) {
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.11f, 1.f);
    gfx->clearScreen();
    gfx->drawTexturedRectRGBA(tex, 0, 0, float(gfx->getWidth()), float(gfx->getHeight()), 1, 1, 1, 1);
    gfx->present();
}

[[maybe_unused]] void auditSwapchain(Graphics *gfx, const char *scene, const char *phase, const RenderAuditBg &bg) {
    std::unique_ptr<ImageData> a(gfx->newImageData());
    REQUIRE(a.get() != nullptr);
    RenderAuditConfig cfg{scene, phase, "", 0};
    auto result = auditImage(*a, cfg, bg, /*step=*/2);
    gfx->setScreenReadbackEnabled(false);
    if (result.hasDefects()) {
        appendAuditReport(auditOutDir(), result);
        saveAuditOverlay(*a, result, auditOutDir());
    }
    REQUIRE(result.interiorOccupancy > 0.03f);
    REQUIRE(!result.hasSevere());
}

[[maybe_unused]] Texture *makeChecker(Graphics *gfx, int w, int h, bool repeat = false) {
    std::vector<uint8_t> px(size_t(w * h * 4));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool on = ((x / 4) + (y / 4)) & 1;
            const size_t i = size_t((y * w + x) * 4);
            px[i + 0] = on ? 210 : 50;
            px[i + 1] = on ? 210 : 50;
            px[i + 2] = on ? 215 : 55;
            px[i + 3] = 255;
        }
    }
    // Studio / classic-style 3D audits: mipmaps + anisotropy (engine default stays off).
    TextureCreateInfo info = TextureCreateInfo::withMipmaps(true);
    info.sampler.repeatU = repeat;
    info.sampler.repeatV = repeat;
    return gfx->newTexture(w, h, px.data(), info);
}

struct CloseWin {
    eve::window::Window *w = nullptr;
    ~CloseWin() {
        if (w) w->close();
    }
};

#include "PathBesideSource.h"
EVE_DEFINE_PATH_BESIDE_SOURCE()

[[maybe_unused]] Mesh *makeFloor(Graphics *gfx, float half = 4.5f) {
    const float pos[] = {
        -half, 0.f, -half, -half, 0.f, half, half, 0.f, half, half, 0.f, -half,
    };
    const float nrm[] = {
        0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f,
    };
    const float uv[] = {0.f, 0.f, 0.f, 4.f, 4.f, 4.f, 4.f, 0.f};
    const uint32_t idx[] = {0, 1, 2, 0, 2, 3};
    return gfx->newMeshFromArrays(pos, nrm, uv, 4, idx, 6);
}

[[maybe_unused]] Texture *makeAtlas4(Graphics *gfx, int cell = 16) {
    const int w = cell * 2;
    const int h = cell * 2;
    std::vector<uint8_t> px(size_t(w * h * 4));
    const uint8_t cols[4][3] = {{220, 70, 60}, {60, 180, 80}, {60, 90, 220}, {230, 200, 50}};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int ci = (y < cell ? 0 : 2) + (x < cell ? 0 : 1);
            const size_t i = size_t((y * w + x) * 4);
            px[i + 0] = cols[ci][0];
            px[i + 1] = cols[ci][1];
            px[i + 2] = cols[ci][2];
            px[i + 3] = 255;
        }
    }
    return gfx->newTexture(w, h, px.data());
}

[[maybe_unused]] Texture *makeFlatNormal(Graphics *gfx) {
    const uint8_t px[4] = {128, 128, 255, 255};
    return gfx->newTexture(1, 1, px);
}

struct Studio3D {
    Camera3D *cam = nullptr;
    Renderable3D *ground = nullptr;
    Renderable3D *subject = nullptr;
    Light3D *sun = nullptr;
    Renderable2D *hud = nullptr;
    Mesh *sphere = nullptr;
    Mesh *floor = nullptr;
};

[[maybe_unused]] Studio3D makeStudio3D(Graphics *gfx) {
    Studio3D s;
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    s.cam = Camera3D::createCamera();
    s.cam->setEye(0.f, 1.5f, 4.0f);
    s.cam->setTarget(0.f, 0.15f, 0.f);
    s.cam->data()->nearZ = 0.05f;
    s.cam->data()->farZ = 40.f;
    s.cam->setAmbient(0.06f, 0.06f, 0.07f);
    s.cam->setEnvMap(makeStudioCubemap(gfx));
    s.cam->setEnvIntensity(0.f);
    s.floor = makeFloor(gfx, 4.5f);
    s.sphere = gfx->newMeshSphere(18, 12);
    s.ground = Renderable3D::create();
    s.ground->setMesh(s.floor);
    s.ground->setTexture(makeSolid(gfx, 70, 72, 78));
    s.ground->setPosition(0.f, -0.7f, 0.f);
    s.ground->setMetallic(0.f);
    s.ground->setRoughness(0.92f);
    s.ground->setCastShadow(false);
    s.ground->setReceiveShadow(true);
    s.subject = Renderable3D::create();
    s.subject->setMesh(s.sphere);
    s.subject->setTexture(makeSolid(gfx, 210, 210, 215));
    s.subject->setPosition(0.f, 0.15f, 0.f);
    s.subject->setScale(0.55f, 0.55f, 0.55f);
    s.subject->setMetallic(0.15f);
    s.subject->setRoughness(0.45f);
    s.subject->setCastShadow(true);
    s.subject->setReceiveShadow(true);
    s.sun = Light3D::createLight("dir");
    s.sun->setDirection(0.45f, 1.f, 0.35f);
    s.sun->setColor(1.f, 0.97f, 0.92f, 2.4f);
    s.sun->setCastShadow(true);
    s.sun->setEnabled(true);
    s.hud = Renderable2D::create();
    s.hud->transform()->x = 0;
    s.hud->transform()->y = 0;
    s.hud->sprite()->width = 2;
    s.hud->sprite()->height = 2;
    s.hud->sprite()->receiveLight = false;
    s.hud->sprite()->castOcclusion = false;
    return s;
}

[[maybe_unused]] Texture *capturePresented(Graphics *gfx) {
    gfx->setScreenReadbackEnabled(false);
    for (int i = 0; i < 2; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    gfx->setScreenReadbackEnabled(true);
    RenderSystem3D::render(*gfx);
    RenderSystem::render(*gfx);
    std::unique_ptr<ImageData> img(gfx->newImageData());
    REQUIRE(img.get() != nullptr);
    Texture *tex = gfx->newTexture(img.get());
    gfx->setScreenReadbackEnabled(false);
    return tex;
}

[[maybe_unused]] Renderable2D *makeSprite(Texture *tex, float x, float y, float w, float h, bool receiveLight = false) {
    auto *sp = Renderable2D::create();
    sp->transform()->x = x;
    sp->transform()->y = y;
    sp->sprite()->width = w;
    sp->sprite()->height = h;
    sp->sprite()->texture = tex;
    sp->sprite()->receiveLight = receiveLight;
    sp->sprite()->castOcclusion = false;
    return sp;
}

[[maybe_unused]] Texture *makeAtlasGrid(Graphics *gfx, int cells, int cell = 8) {
    const int w = cell * cells;
    const int h = cell * cells;
    std::vector<uint8_t> px(size_t(w * h * 4));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int cx = x / cell;
            const int cy = y / cell;
            const int id = cy * cells + cx;
            const size_t i = size_t((y * w + x) * 4);
            px[i + 0] = uint8_t(40 + (id * 37) % 200);
            px[i + 1] = uint8_t(50 + (id * 53) % 190);
            px[i + 2] = uint8_t(70 + (id * 19) % 180);
            px[i + 3] = 255;
        }
    }
    return gfx->newTexture(w, h, px.data());
}

[[maybe_unused]] Mesh *makeHairCard(Graphics *gfx) {
    const float pos[] = {-0.45f, 0.f, 0.f, 0.45f, 0.f, 0.f, 0.45f, 1.3f, 0.f, -0.45f, 1.3f, 0.f};
    const float nrm[] = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const float uv[] = {0.f, 1.f, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f};
    const uint32_t idx[] = {0, 1, 2, 0, 2, 3};
    return gfx->newMeshFromArrays(pos, nrm, uv, 4, idx, 6);
}

[[maybe_unused]] Mesh *uploadSkinnedAiMesh(Graphics *gfx, const aiMesh *src, const std::vector<float> &posXYZ) {
    REQUIRE(src != nullptr);
    const int vc = int(src->mNumVertices);
    REQUIRE(vc > 0);
    REQUIRE(int(posXYZ.size()) == vc * 3);
    std::vector<float> nrm(size_t(vc) * 3u, 0.f);
    std::vector<float> uv(size_t(vc) * 2u, 0.f);
    for (int i = 0; i < vc; ++i) {
        if (src->mNormals) {
            nrm[size_t(i) * 3u + 0] = src->mNormals[i].x;
            nrm[size_t(i) * 3u + 1] = src->mNormals[i].y;
            nrm[size_t(i) * 3u + 2] = src->mNormals[i].z;
        } else {
            nrm[size_t(i) * 3u + 1] = 1.f;
        }
        if (src->mTextureCoords[0]) {
            uv[size_t(i) * 2u + 0] = src->mTextureCoords[0][i].x;
            uv[size_t(i) * 2u + 1] = src->mTextureCoords[0][i].y;
        }
    }
    std::vector<uint32_t> idx;
    idx.reserve(size_t(src->mNumFaces) * 3u);
    for (unsigned f = 0; f < src->mNumFaces; ++f) {
        const aiFace &face = src->mFaces[f];
        if (face.mNumIndices < 3) continue;
        for (unsigned k = 1; k + 1 < face.mNumIndices; ++k) {
            idx.push_back(face.mIndices[0]);
            idx.push_back(face.mIndices[k]);
            idx.push_back(face.mIndices[k + 1]);
        }
    }
    REQUIRE(!idx.empty());
    return gfx->newMeshFromArrays(posXYZ.data(), nrm.data(), uv.data(), vc, idx.data(),
                                  int(idx.size()));
}

[[maybe_unused]] eve::model3d::ModelData *loadCesiumMan(const char *fsIdentity) {
    const std::string dir = pathBesideThisSource("assets/skinned/cesium_man/glTF");
    REQUIRE(std::filesystem::is_regular_file(dir + "/CesiumMan.gltf"));
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs != nullptr);
    REQUIRE(fs->setIdentity(fsIdentity, true));
    REQUIRE(fs->setupWriteDirectory());
    fs->allowMountingForPath(dir);
    REQUIRE(fs->mount(dir, "", false));
    auto *mod = eve::model3d::Model3D::create();
    return mod->newModelDataFromFile("CesiumMan.gltf");
}

[[maybe_unused]] int findFirstSkinnedMesh(const eve::model3d::ModelData *model) {
    for (int i = 0; i < model->getMeshCount(); ++i) {
        if (model->hasBones(i)) return i;
    }
    return -1;
}

[[maybe_unused]] bool printOverlayThunk(void *userdata, void *commandBuffer) {
    auto *gfx = static_cast<Graphics *>(userdata);
    if (!gfx || !gfx->getFont()) return false;
    if (!commandBuffer) return true;
    Font *font = gfx->getFont();
    const char *icon = "\xEF\x80\x80";
    std::string row;
    for (int i = 0; i < 8; ++i) row += icon;
    gfx->drawText(font, row, 8.f, 8.f, Color(1.f, 0.82f, 0.2f, 1.f), 2.4f);
    return true;
}

}  // namespace

TEST_CASE("graphics.imageAudit.syntheticDefects") {
    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    const uint8_t geoR = 180, geoG = 180, geoB = 185;

    {
        std::unique_ptr<ImageData> img(makeRgba(64, 64, geoR, geoG, geoB));
        RenderAuditConfig cfg{"synthetic", "clean", "", 0};
        auto r = auditImage(*img, cfg, bg);
        CHECK(!r.empty);
        CHECK(r.defects.empty());
        CHECK(r.occupancy > 0.9f);
    }

    {
        std::unique_ptr<ImageData> img(makeRgba(64, 64, geoR, geoG, geoB));
        // One full interior 16x16 tile; unaligned holes split across tiles and
        // stay above kEmptyTile so detectHoles never fires.
        fillRect(img.get(), 16, 16, 16, 16, 0, 0, 0);
        RenderAuditConfig cfg{"synthetic", "hole", "", 0};
        auto r = auditImage(*img, cfg, bg);
        REQUIRE(hasKind(r, RenderDefect::Kind::Incomplete));
        bool locOk = false;
        for (const auto &d : r.defects) {
            if (d.kind != RenderDefect::Kind::Incomplete) continue;
            const int cx = d.x + d.w / 2;
            const int cy = d.y + d.h / 2;
            if (cx >= 16 && cx < 48 && cy >= 16 && cy < 48) locOk = true;
        }
        CHECK(locOk);
        appendAuditReport(auditOutDir(), r);
        saveAuditOverlay(*img, r, auditOutDir());
    }

    {
        std::unique_ptr<ImageData> img(makeRgba(64, 64, geoR, geoG, geoB));
        fillRect(img.get(), 8, 8, 12, 12, 255, 0, 255);
        RenderAuditConfig cfg{"synthetic", "magenta", "", 0};
        auto r = auditImage(*img, cfg, bg);
        REQUIRE(hasKind(r, RenderDefect::Kind::Damage));
        REQUIRE(r.hasSevere());
        appendAuditReport(auditOutDir(), r);
    }

    {
        std::unique_ptr<ImageData> img(makeRgba(64, 64, geoR, geoG, geoB));
        fillRect(img.get(), 0, 30, 64, 1, 20, 23, 28);
        RenderAuditConfig cfg{"synthetic", "tearing", "", 0};
        auto r = auditImage(*img, cfg, bg);
        REQUIRE(hasKind(r, RenderDefect::Kind::Damage));
        appendAuditReport(auditOutDir(), r);
    }

    {
        std::unique_ptr<ImageData> a(makeRgba(64, 64, geoR, geoG, geoB));
        std::unique_ptr<ImageData> b(makeRgba(64, 64, geoR, geoG, geoB));
        fillRect(b.get(), 16, 16, 24, 24, 40, 40, 40);
        RenderAuditConfig cfg{"synthetic", "flicker", "", 0};
        auto r = auditFlicker(*a, *b, cfg, bg);
        REQUIRE(hasKind(r, RenderDefect::Kind::Flicker));
        bool locOk = false;
        for (const auto &d : r.defects) {
            if (d.kind != RenderDefect::Kind::Flicker) continue;
            const int cx = d.x + d.w / 2;
            const int cy = d.y + d.h / 2;
            if (cx >= 12 && cx < 44 && cy >= 12 && cy < 44) locOk = true;
        }
        CHECK(locOk);
        appendAuditReport(auditOutDir(), r);
    }

    {
        std::unique_ptr<ImageData> img(makeRgba(64, 64, 20, 23, 28));
        RenderAuditConfig cfg{"synthetic", "empty", "", 0};
        auto r = auditImage(*img, cfg, bg);
        REQUIRE(r.empty);
        REQUIRE(r.hasSevere());
        appendAuditReport(auditOutDir(), r);
    }
}
