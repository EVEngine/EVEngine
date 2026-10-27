// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "render-window",
  "module": "animation",
  "modules": [
    "animation",
    "graphics"
  ],
  "origins": [
    "RenderImageAudit.cpp",
    "RenderSceneEffects.cpp"
  ]
}
*/
// clang-format on

// Related integration contracts share one translation unit to amortize header parsing.

// Contract section: RenderImageAudit
#include "PrivateTestScope.h"
#include "zeroerr/assert.h"

#include <SDL2/SDL.h>
#include <assimp/mesh.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <string>
#include <vector>
#include "ManualPreview.h"
#include "PathBesideSource.h"
#include "RenderImageAudit.h"
#include "ResourceTestSupport.h"
#include "animation/AnimClip.h"
#include "animation/AnimImporter.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"
#include "avatar/Avatar.h"
#include "data/ByteData.h"
#include "filesystem/FileData.h"
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
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/TextureSampler.h"
#include "graphics/Volumetric.h"
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
#include "zeroerr/unittest.h"
// RenderImageAudit.cpp
namespace test_RenderImageAudit {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;


#include "graphics/shaders/custom2d_frag_spv.inc"


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
    auto *p   = static_cast<uint8_t *>(img->getData());
    for (int i = 0; i < w * h; ++i) {
        p[i * 4 + 0] = r;
        p[i * 4 + 1] = g;
        p[i * 4 + 2] = b;
        p[i * 4 + 3] = 255;
    }
    return img;
}


[[maybe_unused]] std::string auditOutDir() { return std::string(EVENGINE_TEST_BINARY_DIR) + "/out/classic_scenes"; }

[[maybe_unused]] Texture *makeSolid(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

[[maybe_unused]] Texture *makeStudioCubemap(Graphics *gfx, int face = 8) {
    const uint8_t faceRgb[6][3] = {
        {220, 180, 140}, {120, 150, 210}, {245, 245, 250}, {40, 40, 45}, {200, 210, 230}, {180, 160, 140},
    };
    const size_t         faceBytes = size_t(face) * size_t(face) * 4u;
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
            auto [mr]   = *it;
            mr->visible = false;
        }
    }
    if (ecs::current()->getManager<Camera3D>() != nullptr) {
        auto camView = ecs::View<Camera3D, Camera3D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data]        = *it;
            data->active       = false;
            data->envMap       = nullptr;
            data->envIntensity = 1.f;
        }
    }
    if (ecs::current()->getManager<Light3D>() != nullptr) {
        auto lightView = ecs::View<Light3D, Light3D::Data>();
        for (auto it = lightView.begin(); it != lightView.end(); ++it) {
            auto [d]   = *it;
            d->enabled = false;
        }
    }
    if (ecs::current()->getManager<Renderable2D>() != nullptr) {
        auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [sp]   = *it;
            sp->visible = false;
        }
    }
    if (ecs::current()->getManager<Light2D>() != nullptr) {
        auto lightView = ecs::View<Light2D, Light2D::Data>();
        for (auto it = lightView.begin(); it != lightView.end(); ++it) {
            auto [d]   = *it;
            d->enabled = false;
        }
    }
    if (ecs::current()->getManager<Camera2D>() != nullptr) {
        auto camView = ecs::View<Camera2D, Camera2D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [d]  = *it;
            d->active = false;
        }
    }
    if (ecs::current()->getManager<eve::particles::ParticleEmitter>() != nullptr) {
        auto view = ecs::View<eve::particles::ParticleEmitter, eve::particles::ParticleEmitter::Draw>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [d]   = *it;
            d->visible = false;
        }
    }
    if (ecs::current()->getManager<eve::map::TileLayer>() != nullptr) {
        auto view = ecs::View<eve::map::TileLayer, eve::map::TileLayer::Draw>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [d]   = *it;
            d->visible = false;
        }
    }
}

[[maybe_unused]] void auditGpuFrame(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, Graphics *gfx, const char *scene,
                                    const char *phase, const RenderAuditBg &bg, bool flicker = false,
                                    const std::function<void()> &after3D = {}, const char *rawCaptureName = nullptr) {
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
    if (rawCaptureName && eve::test::manualPreviewEnabled())
        REQUIRE(saveImagePng(*a, auditOutDir() + "/" + rawCaptureName));
    RenderAuditConfig cfg{scene, phase, "", 0};
    auto              result = auditImage(*a, cfg, bg, /*step=*/2);
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

[[maybe_unused]] void auditGpu2D(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, Graphics *gfx, const char *scene,
                                 const char *phase, const RenderAuditBg &bg) {
    gfx->setScreenReadbackEnabled(false);
    for (int i = 0; i < 2; ++i) RenderSystem::render(*gfx);
    gfx->setScreenReadbackEnabled(true);
    RenderSystem::render(*gfx);
    std::unique_ptr<ImageData> a(gfx->newImageData());
    REQUIRE(a.get() != nullptr);
    RenderAuditConfig cfg{scene, phase, "", 0};
    auto              result = auditImage(*a, cfg, bg, /*step=*/2);
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

[[maybe_unused]] void auditSwapchain(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, Graphics *gfx, const char *scene,
                                     const char *phase, const RenderAuditBg &bg) {
    std::unique_ptr<ImageData> a(gfx->newImageData());
    REQUIRE(a.get() != nullptr);
    RenderAuditConfig cfg{scene, phase, "", 0};
    auto              result = auditImage(*a, cfg, bg, /*step=*/2);
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
            const bool   on = ((x / 4) + (y / 4)) & 1;
            const size_t i  = size_t((y * w + x) * 4);
            px[i + 0]       = on ? 210 : 50;
            px[i + 1]       = on ? 210 : 50;
            px[i + 2]       = on ? 215 : 55;
            px[i + 3]       = 255;
        }
    }
    // Studio / classic-style 3D audits: mipmaps + anisotropy (engine default stays off).
    TextureCreateInfo info = TextureCreateInfo::withMipmaps(true);
    info.sampler.repeatU   = repeat;
    info.sampler.repeatV   = repeat;
    return gfx->newTexture(w, h, px.data(), info);
}

struct CloseWin {
    eve::window::Window *w = nullptr;
    ~CloseWin() {
        if (w) w->close();
    }
};


EVE_DEFINE_PATH_BESIDE_SOURCE()

[[maybe_unused]] Mesh *makeFloor(Graphics *gfx, float half = 4.5f) {
    const float pos[] = {
        -half, 0.f, -half, -half, 0.f, half, half, 0.f, half, half, 0.f, -half,
    };
    const float nrm[] = {
        0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f,
    };
    const float    uv[]  = {0.f, 0.f, 0.f, 4.f, 4.f, 4.f, 4.f, 0.f};
    const uint32_t idx[] = {0, 1, 2, 0, 2, 3};
    return gfx->newMeshFromArrays(pos, nrm, uv, 4, idx, 6);
}

[[maybe_unused]] Texture *makeAtlas4(Graphics *gfx, int cell = 16) {
    const int            w = cell * 2;
    const int            h = cell * 2;
    std::vector<uint8_t> px(size_t(w * h * 4));
    const uint8_t        cols[4][3] = {{220, 70, 60}, {60, 180, 80}, {60, 90, 220}, {230, 200, 50}};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int    ci = (y < cell ? 0 : 2) + (x < cell ? 0 : 1);
            const size_t i  = size_t((y * w + x) * 4);
            px[i + 0]       = cols[ci][0];
            px[i + 1]       = cols[ci][1];
            px[i + 2]       = cols[ci][2];
            px[i + 3]       = 255;
        }
    }
    return gfx->newTexture(w, h, px.data());
}

[[maybe_unused]] Texture *makeFlatNormal(Graphics *gfx) {
    const uint8_t px[4] = {128, 128, 255, 255};
    return gfx->newTexture(1, 1, px);
}

struct Studio3D {
    Camera3D     *cam     = nullptr;
    Renderable3D *ground  = nullptr;
    Renderable3D *subject = nullptr;
    Light3D      *sun     = nullptr;
    Renderable2D *hud     = nullptr;
    Mesh         *sphere  = nullptr;
    Mesh         *floor   = nullptr;
};

[[maybe_unused]] Studio3D makeStudio3D(Graphics *gfx) {
    Studio3D s;
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    s.cam = Camera3D::createCamera();
    s.cam->setEye(0.f, 1.5f, 4.0f);
    s.cam->setTarget(0.f, 0.15f, 0.f);
    s.cam->data()->nearZ = 0.05f;
    s.cam->data()->farZ  = 40.f;
    s.cam->setAmbient(0.06f, 0.06f, 0.07f);
    s.cam->setEnvMap(makeStudioCubemap(gfx));
    s.cam->setEnvIntensity(0.f);
    s.floor  = makeFloor(gfx, 4.5f);
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
    s.hud                          = Renderable2D::create();
    s.hud->transform()->x          = 0;
    s.hud->transform()->y          = 0;
    s.hud->sprite()->width         = 2;
    s.hud->sprite()->height        = 2;
    s.hud->sprite()->receiveLight  = false;
    s.hud->sprite()->castOcclusion = false;
    return s;
}

[[maybe_unused]] Texture *capturePresented(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, Graphics *gfx) {
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
    auto *sp                    = Renderable2D::create();
    sp->transform()->x          = x;
    sp->transform()->y          = y;
    sp->sprite()->width         = w;
    sp->sprite()->height        = h;
    sp->sprite()->texture       = tex;
    sp->sprite()->receiveLight  = receiveLight;
    sp->sprite()->castOcclusion = false;
    return sp;
}

[[maybe_unused]] Texture *makeAtlasGrid(Graphics *gfx, int cells, int cell = 8) {
    const int            w = cell * cells;
    const int            h = cell * cells;
    std::vector<uint8_t> px(size_t(w * h * 4));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int    cx = x / cell;
            const int    cy = y / cell;
            const int    id = cy * cells + cx;
            const size_t i  = size_t((y * w + x) * 4);
            px[i + 0]       = uint8_t(40 + (id * 37) % 200);
            px[i + 1]       = uint8_t(50 + (id * 53) % 190);
            px[i + 2]       = uint8_t(70 + (id * 19) % 180);
            px[i + 3]       = 255;
        }
    }
    return gfx->newTexture(w, h, px.data());
}

[[maybe_unused]] Mesh *makeHairCard(Graphics *gfx) {
    const float    pos[] = {-0.45f, 0.f, 0.f, 0.45f, 0.f, 0.f, 0.45f, 1.3f, 0.f, -0.45f, 1.3f, 0.f};
    const float    nrm[] = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const float    uv[]  = {0.f, 1.f, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f};
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
    return gfx->newMeshFromArrays(posXYZ.data(), nrm.data(), uv.data(), vc, idx.data(), int(idx.size()));
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
    Font       *font = gfx->getFont();
    const char *icon = "\xEF\x80\x80";
    std::string row;
    for (int i = 0; i < 8; ++i) row += icon;
    gfx->drawText(font, row, 8.f, 8.f, Color(1.f, 0.82f, 0.2f, 1.f), 2.4f);
    return true;
}

}  // namespace


TEST_CASE("graphics.imageAudit.pipelineConfigs") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 480;
    s.height   = 360;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    RenderAuditBg bg{0.08f, 0.09f, 0.11f};

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 1.6f, 4.2f);
    cam->setTarget(0.f, 0.2f, 0.f);
    cam->data()->nearZ = 0.05f;
    cam->data()->farZ  = 40.f;
    cam->setAmbient(0.06f, 0.06f, 0.07f);
    cam->setEnvMap(makeStudioCubemap(gfx));
    cam->setEnvIntensity(0.f);

    auto *ground = Renderable3D::create();
    ground->setMesh(gfx->newMeshSphere(16, 8));
    ground->setTexture(makeSolid(gfx, 70, 72, 78));
    ground->setPosition(0.f, -0.7f, 0.f);
    ground->setScale(3.2f, 0.08f, 3.2f);
    ground->setMetallic(0.f);
    ground->setRoughness(0.92f);
    ground->setCastShadow(false);
    ground->setReceiveShadow(true);

    auto *box = Renderable3D::create();
    box->setMesh(gfx->newMeshSphere(20, 12));
    box->setTexture(makeSolid(gfx, 210, 210, 215));
    box->setPosition(0.f, 0.15f, 0.f);
    box->setScale(0.55f, 0.55f, 0.55f);
    box->setMetallic(0.15f);
    box->setRoughness(0.45f);
    box->setCastShadow(true);
    box->setReceiveShadow(true);

    auto *hud             = Renderable2D::create();
    hud->transform()->x   = 0;
    hud->transform()->y   = 0;
    hud->sprite()->width  = 2;
    hud->sprite()->height = 2;
    hud->sprite()->r      = 0.f;
    hud->sprite()->g      = 0.f;
    hud->sprite()->b      = 0.f;

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.45f, 1.f, 0.35f);
    sun->setColor(1.f, 0.97f, 0.92f, 2.4f);
    sun->setCastShadow(false);
    sun->setEnabled(false);

    auto *point = Light3D::createLight("point");
    point->setPosition(-1.2f, 1.1f, 1.0f);
    point->setColor(1.f, 0.6f, 0.4f, 5.f);
    point->setRadius(6.f);
    point->setEnabled(false);

    auto *pointB = Light3D::createLight("point");
    pointB->setPosition(1.1f, 0.9f, -0.8f);
    pointB->setColor(0.4f, 0.55f, 1.f, 4.5f);
    pointB->setRadius(6.f);
    pointB->setEnabled(false);

    std::vector<Light3D *> clusterLights;
    clusterLights.reserve(10);
    for (int i = 0; i < 10; ++i) {
        auto       *l = Light3D::createLight("point");
        const float a = float(i) / 10.f * 6.2831853f;
        l->setPosition(std::cos(a) * 1.7f, 0.55f + 0.15f * std::sin(a * 2.f), std::sin(a) * 1.7f);
        l->setColor(0.85f, 0.9f, 1.f, 2.2f);
        l->setRadius(3.2f);
        l->setEnabled(false);
        clusterLights.push_back(l);
    }

    RenderControl *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);
    auto resetRc = [&]() {
        rc->enable("depthTest");
        rc->enable("shadow");
        rc->enable("forward");
        rc->enable("hair");
        rc->enable("clustered");
        rc->disable("gbuffer");
        rc->compile();
    };
    resetRc();

    struct Phase {
        const char *name;
        int         light;  // 0 amb 1 dir 2 shadows 3 point 4 ibl 5 dir+point 6 shadows+ibl 7 full
        const char *enableFeat;
        const char *disableFeat;
        bool        manyPoints;
        bool        flicker;
    };
    const Phase phases[] = {
        {"ambient", 0, nullptr, nullptr, false, true},
        {"dir_lit", 1, nullptr, nullptr, false, true},
        {"dir_shadows", 2, nullptr, nullptr, false, true},
        {"point_lit", 3, nullptr, nullptr, false, false},
        {"ibl_reflect", 4, nullptr, nullptr, false, true},
        {"dir_point", 5, nullptr, nullptr, false, false},
        {"shadows_ibl", 6, nullptr, nullptr, false, false},
        {"full_lit", 7, nullptr, nullptr, false, true},
        {"gbuffer", 1, "gbuffer", nullptr, false, false},
        {"gbuffer_albedo", 1, "gbufferAlbedo", nullptr, false, false},
        {"no_shadow_pass", 2, nullptr, "shadow", false, false},
        {"clustered_many", 1, "clustered", nullptr, true, false},
        {"packed_only", 1, nullptr, "clustered", true, false},
    };

    const std::string outDir = auditOutDir();

    for (const auto &ph : phases) {
        sun->setEnabled(false);
        sun->setCastShadow(false);
        point->setEnabled(false);
        pointB->setEnabled(false);
        for (auto *l : clusterLights) l->setEnabled(false);
        cam->setEnvIntensity(0.f);
        cam->setAmbient(0.18f, 0.18f, 0.2f);
        resetRc();
        if (ph.enableFeat) {
            rc->enable(ph.enableFeat);
            rc->compile();
        }
        if (ph.disableFeat) {
            rc->disable(ph.disableFeat);
            rc->compile();
        }

        auto enableSun = [&](bool shadows) {
            cam->setAmbient(0.05f, 0.05f, 0.06f);
            sun->setEnabled(true);
            sun->setCastShadow(shadows);
            sun->setShadowStrength(1.f);
            sun->setColor(1.f, 0.97f, 0.92f, 2.4f);
        };
        switch (ph.light) {
            case 1: enableSun(false); break;
            case 2: enableSun(true); break;
            case 3:
                cam->setAmbient(0.04f, 0.04f, 0.05f);
                point->setEnabled(true);
                break;
            case 4:
                cam->setAmbient(0.03f, 0.03f, 0.04f);
                sun->setEnabled(true);
                cam->setEnvIntensity(1.2f);
                break;
            case 5:
                enableSun(false);
                point->setEnabled(true);
                pointB->setEnabled(true);
                break;
            case 6:
                enableSun(true);
                cam->setEnvIntensity(1.1f);
                break;
            case 7:
                enableSun(true);
                point->setEnabled(true);
                pointB->setEnabled(true);
                cam->setEnvIntensity(1.0f);
                break;
            default: break;
        }
        if (ph.manyPoints) {
            for (auto *l : clusterLights) l->setEnabled(true);
        }

        gfx->setScreenReadbackEnabled(false);
        for (int i = 0; i < 2; ++i) {
            RenderSystem3D::render(*gfx);
            RenderSystem::render(*gfx);
        }
        gfx->setScreenReadbackEnabled(true);
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        std::unique_ptr<ImageData> a(gfx->newImageData());
        REQUIRE(a.get() != nullptr);
        RenderAuditConfig cfg{"pipeline_cube", ph.name, "", 0};
        auto              result = auditImage(*a, cfg, bg, /*step=*/2);

        if (ph.flicker) {
            RenderSystem3D::render(*gfx);
            RenderSystem::render(*gfx);
            std::unique_ptr<ImageData> b(gfx->newImageData());
            REQUIRE(b.get() != nullptr);
            mergeAudit(result, auditFlicker(*a, *b, cfg, bg, /*step=*/2));
        }
        gfx->setScreenReadbackEnabled(false);

        if (result.hasDefects()) {
            appendAuditReport(outDir, result);
            saveAuditOverlay(*a, result, outDir);
        } else if (eve::test::manualPreviewEnabled()) {
            saveImagePng(*a, outDir + "/pipeline_cube_" + std::string(ph.name) + ".png");
        }

        REQUIRE(result.interiorOccupancy > 0.04f);
        REQUIRE(!result.hasSevere());
    }

    resetRc();
    win->close();
}

TEST_CASE("graphics.imageAudit.materialsAndCamera") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    RenderAuditBg bg{0.08f, 0.09f, 0.11f};

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 1.5f, 4.0f);
    cam->setTarget(0.f, 0.15f, 0.f);
    cam->data()->nearZ = 0.05f;
    cam->data()->farZ  = 40.f;
    cam->setAmbient(0.06f, 0.06f, 0.07f);
    cam->setEnvMap(makeStudioCubemap(gfx));
    cam->setEnvIntensity(0.f);

    Mesh    *sphere  = gfx->newMeshSphere(18, 12);
    Mesh    *cyl     = gfx->newMeshCylinder(16, 1, true);
    Texture *checker = makeChecker(gfx, 32, 32);
    Texture *height  = makeChecker(gfx, 32, 32);

    auto *ground = Renderable3D::create();
    ground->setMesh(sphere);
    ground->setTexture(makeSolid(gfx, 70, 72, 78));
    ground->setPosition(0.f, -0.7f, 0.f);
    ground->setScale(3.0f, 0.08f, 3.0f);
    ground->setMetallic(0.f);
    ground->setRoughness(0.92f);
    ground->setCastShadow(false);
    ground->setReceiveShadow(true);

    auto *box = Renderable3D::create();
    box->setMesh(sphere);
    box->setTexture(makeSolid(gfx, 210, 210, 215));
    box->setPosition(0.f, 0.15f, 0.f);
    box->setScale(0.55f, 0.55f, 0.55f);
    box->setMetallic(0.15f);
    box->setRoughness(0.45f);
    box->setCastShadow(true);
    box->setReceiveShadow(true);

    auto *hud             = Renderable2D::create();
    hud->transform()->x   = 0;
    hud->transform()->y   = 0;
    hud->sprite()->width  = 2;
    hud->sprite()->height = 2;

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.45f, 1.f, 0.35f);
    sun->setColor(1.f, 0.97f, 0.92f, 2.4f);
    sun->setCastShadow(true);
    sun->setShadowStrength(1.f);
    sun->setEnabled(true);

    Material *unlit = gfx->newMaterial();
    unlit->setShadingModel("unlit");
    unlit->setAlbedoTexture(makeSolid(gfx, 200, 50, 50));
    unlit->setReceiveLight(false);

    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "materials", "pbr_default", bg, true);

    box->setMaterial(unlit);
    box->setReceiveLight(false);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "materials", "unlit", bg);
    box->setMaterial(nullptr);
    box->setReceiveLight(true);
    box->setTexture(makeSolid(gfx, 210, 210, 215));

    box->setMetallic(1.f);
    box->setRoughness(0.08f);
    cam->setEnvIntensity(1.3f);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "materials", "metal_ibl", bg);
    cam->setEnvIntensity(0.f);
    box->setMetallic(0.f);
    box->setRoughness(0.95f);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "materials", "dielectric_rough", bg);
    box->setMetallic(0.15f);
    box->setRoughness(0.45f);

    box->setTexture(checker);
    box->setTexCellBomb(4.f, 0.85f, 1.f);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "materials", "tex_cell_bomb", bg);
    box->setTexCellBomb(4.f, 0.f, 1.f);
    box->setHeightTexture(height);
    box->setParallax(0.06f, 8.f, 24.f);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "materials", "parallax", bg);
    box->setParallax(0.f);
    box->setHeightTexture(nullptr);
    box->setTexture(makeSolid(gfx, 210, 210, 215));

    box->setHair(true);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "materials", "hair", bg);
    box->setHair(false);

    box->setMesh(cyl);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "materials", "cylinder", bg);
    box->setMesh(sphere);

    ground->setReceiveShadow(false);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "materials", "receive_shadow_off", bg);
    ground->setReceiveShadow(true);

    sun->setShadowBias(0.02f);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "materials", "shadow_bias_high", bg);
    sun->setShadowBias(0.002f);

    cam->setFov(28.f);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "materials", "fov_narrow", bg);
    cam->setFov(75.f);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "materials", "fov_wide", bg);
    cam->setFov(60.f);

    cam->data()->nearZ = 0.8f;
    cam->data()->farZ  = 6.f;
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "materials", "near_far_tight", bg);
    cam->data()->nearZ = 0.05f;
    cam->data()->farZ  = 40.f;

    delete unlit;
    win->close();
}

TEST_CASE("graphics.imageAudit.vertexColorsReachPresentedPixels") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 400;
    settings.height   = 300;
    settings.centered = true;
    REQUIRE(win->setWindowSettings(settings));
    CloseWin closer{win};
    resetScene3D();

    gfx->setBackgroundColor(Color(0.02f, 0.02f, 0.02f, 1.f));
    auto *camera = Camera3D::createCamera();
    camera->setEye(0.f, 0.f, 3.f);
    camera->setTarget(0.f, 0.f, 0.f);
    camera->setAmbient(1.f, 1.f, 1.f);
    camera->setEnvIntensity(0.f);

    const float         positions[] = {-1.4f, -1.f, 0.f, 1.4f, -1.f, 0.f, 1.4f, 1.f, 0.f, -1.4f, 1.f, 0.f};
    const float         normals[]   = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const float         uv[]        = {0.f, 1.f, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f};
    const float         colors[]    = {1.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f, 1.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 0.f, 1.f};
    const std::uint32_t indices[]   = {0, 1, 2, 0, 2, 3};
    Mesh               *mesh        = gfx->newMeshFromArraysColored(positions, normals, uv, colors, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    auto *material = gfx->newMaterial();
    REQUIRE(material != nullptr);
    material->setShadingModel("unlit");
    material->setAlbedoTexture(makeSolid(gfx, 255, 255, 255));
    material->setReceiveLight(false);
    auto *quad = Renderable3D::create();
    quad->setMesh(mesh);
    quad->setMaterial(material);
    quad->setReceiveLight(false);
    quad->setCastShadow(false);

    gfx->setScreenReadbackEnabled(false);
    for (int frame = 0; frame < 2; ++frame) RenderSystem3D::render(*gfx);
    gfx->setScreenReadbackEnabled(true);
    RenderSystem3D::render(*gfx);
    std::unique_ptr<ImageData> image(gfx->newImageData());
    REQUIRE(image != nullptr);
    gfx->setScreenReadbackEnabled(false);

    int redDominant   = 0;
    int greenDominant = 0;
    for (int y = 70; y < 230; y += 2)
        for (int x = 70; x < 330; x += 2) {
            const auto pixel = image->getPixel(x, y);
            if (pixel.r > pixel.g * 1.35f && pixel.r > 0.15f) ++redDominant;
            if (pixel.g > pixel.r * 1.35f && pixel.g > 0.15f) ++greenDominant;
        }
    CHECK(redDominant > 300);
    CHECK(greenDominant > 300);
    if (eve::test::manualPreviewEnabled()) saveImagePng(*image, auditOutDir() + "/vertex_colors_presented.png");

    delete material;
    resetScene3D();
    win->close();
}

TEST_CASE("graphics.imageAudit.postFx") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    const int     w = 400;
    const int     h = 300;

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 1.5f, 4.0f);
    cam->setTarget(0.f, 0.15f, 0.f);
    cam->data()->nearZ = 0.05f;
    cam->data()->farZ  = 40.f;
    cam->setAmbient(0.05f, 0.05f, 0.06f);
    cam->setEnvMap(makeStudioCubemap(gfx));
    cam->setEnvIntensity(0.f);

    Mesh *sphere = gfx->newMeshSphere(18, 12);
    auto *ground = Renderable3D::create();
    ground->setMesh(sphere);
    ground->setTexture(makeSolid(gfx, 70, 72, 78));
    ground->setPosition(0.f, -0.7f, 0.f);
    ground->setScale(3.0f, 0.08f, 3.0f);
    ground->setRoughness(0.92f);
    ground->setCastShadow(false);
    ground->setReceiveShadow(true);

    auto *box = Renderable3D::create();
    box->setMesh(sphere);
    box->setTexture(makeSolid(gfx, 210, 210, 215));
    box->setPosition(0.f, 0.15f, 0.f);
    box->setScale(0.55f, 0.55f, 0.55f);
    box->setCastShadow(true);

    auto *hud             = Renderable2D::create();
    hud->transform()->x   = 0;
    hud->transform()->y   = 0;
    hud->sprite()->width  = 2;
    hud->sprite()->height = 2;

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.45f, 1.f, 0.35f);
    sun->setColor(1.f, 0.97f, 0.92f, 2.4f);
    sun->setCastShadow(true);
    sun->setEnabled(true);

    RenderControl *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);
    rc->enable("gbuffer");
    rc->enable("gbufferAlbedo");
    rc->compile();

    gfx->setScreenReadbackEnabled(false);
    for (int i = 0; i < 2; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    gfx->setScreenReadbackEnabled(true);
    RenderSystem3D::render(*gfx);
    RenderSystem::render(*gfx);

    std::unique_ptr<ImageData> sceneImg(gfx->newImageData());
    REQUIRE(sceneImg.get() != nullptr);
    Texture *sceneTex = gfx->newTexture(sceneImg.get());
    REQUIRE(sceneTex != nullptr);

    GBuffer *gb = rc->getGBuffer();
    REQUIRE(gb != nullptr);
    Texture *depth = gb->getDepthTexture();
    REQUIRE(depth != nullptr);

    std::unique_ptr<AntiAliasing> aa(gfx->newAntiAliasing());
    aa->setQuality("low");
    Canvas *aaDest = gfx->newCanvas(w, h);
    REQUIRE(aaDest != nullptr);
    const char *aaModes[] = {"fxaa", "smaa", "nfaa"};
    for (const char *mode : aaModes) {
        aa->setMode(mode);
        aa->applyTo(gfx, sceneTex, aaDest);
        blitAndPresent(gfx, aaDest->getTexture());
        auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "postfx", (std::string("aa_") + mode).c_str(), bg);
    }

    std::unique_ptr<AmbientOcclusion> ao(gfx->newAmbientOcclusion());
    ao->setQuality("low");
    ao->setCamera(0.f, 1.5f, 4.0f, 0.f, 0.15f, 0.f, 0.f, 1.f, 0.f, 60.f, float(w) / float(h), 0.05f, 40.f);
    Canvas *aoMap    = gfx->newCanvas(ao->resolutionFor(w), ao->resolutionFor(h));
    Canvas *composed = gfx->newCanvas(w, h);
    REQUIRE(aoMap != nullptr);
    REQUIRE(composed != nullptr);
    const char *aoModes[] = {"ssao", "hbao", "gtao"};
    for (const char *mode : aoModes) {
        ao->setMode(mode);
        ao->computeTo(gfx, depth, aoMap);
        gfx->setCanvas(composed);
        gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.11f, 1.f);
        gfx->clearScreen();
        gfx->drawTexturedRectRGBA(sceneTex, 0, 0, float(w), float(h), 1, 1, 1, 1);
        ao->applyOverlay(gfx, aoMap->getTexture());
        gfx->setCanvas();
        blitAndPresent(gfx, composed->getTexture());
        auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "postfx", (std::string("ao_") + mode).c_str(), bg);
    }

    std::unique_ptr<Volumetric> vol(gfx->newVolumetric());
    vol->setQuality("low");
    vol->setCamera(0.f, 1.5f, 4.0f, 0.f, 0.15f, 0.f, 0.f, 1.f, 0.f, 60.f, float(w) / float(h), 0.05f, 40.f);
    vol->setFogColor(0.45f, 0.5f, 0.55f);
    vol->setFogHeight(-0.7f);
    vol->setFogHeightFalloff(0.8f);
    vol->setFogStart(1.2f);
    vol->setFogEnd(9.f);
    vol->setDensity(0.35f);
    vol->setMode("fog");
    gfx->setCanvas(composed);
    gfx->clearScreen();
    gfx->drawTexturedRectRGBA(sceneTex, 0, 0, float(w), float(h), 1, 1, 1, 1);
    vol->applyFog(gfx, depth);
    gfx->setCanvas();
    blitAndPresent(gfx, composed->getTexture());
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "postfx", "vol_fog", bg);

    vol->setMode("raymarch");
    vol->setLightDirection(0.45f, 1.f, 0.35f);
    vol->setLightScreenUV(0.62f, 0.28f);
    vol->setIntensity(0.8f);
    gfx->setCanvas(composed);
    gfx->clearScreen();
    gfx->drawTexturedRectRGBA(sceneTex, 0, 0, float(w), float(h), 1, 1, 1, 1);
    vol->rayMarch(gfx, depth);
    gfx->setCanvas();
    blitAndPresent(gfx, composed->getTexture());
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "postfx", "vol_raymarch", bg);

    vol->setMode("screenspace");
    vol->applyFromSceneTo(gfx, sceneTex, composed);
    blitAndPresent(gfx, composed->getTexture());
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "postfx", "vol_screenspace", bg);

    rc->disable("gbuffer");
    rc->compile();
    win->close();
}

TEST_CASE("graphics.imageAudit.composite2d3d") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto          studio          = makeStudio3D(gfx);
    studio.hud->sprite()->visible = false;

    Texture *cells[4] = {
        makeSolid(gfx, 220, 80, 70),
        makeSolid(gfx, 70, 190, 90),
        makeSolid(gfx, 70, 110, 220),
        makeSolid(gfx, 230, 200, 60),
    };
    Texture      *flatN = makeFlatNormal(gfx);
    Renderable2D *packed[4];
    const float   ox = 80.f, oy = 60.f, cw = 120.f, ch = 90.f;
    for (int i = 0; i < 4; ++i) {
        packed[i] = makeSprite(cells[i], ox + float(i % 2) * cw, oy + float(i / 2) * ch, cw, ch, false);
        packed[i]->setNormalTexture(flatN);
    }
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "composite", "sprites_over_3d", bg);

    auto *pt = Light2D::createLight("point");
    pt->setPosition(200.f, 150.f);
    pt->setColor(1.f, 0.95f, 0.85f, 2.2f);
    pt->setRadius(280.f);
    pt->setEnabled(true);
    for (auto *sp : packed) sp->sprite()->receiveLight = true;
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "composite", "lit2d_normal", bg);

    pt->setEnabled(false);
    auto *dir2 = Light2D::createLight("dir");
    dir2->setDirection(0.6f, -0.4f);
    dir2->setColor(0.95f, 0.9f, 1.f, 1.6f);
    dir2->setEnabled(true);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "composite", "dir2d", bg);

    dir2->setEnabled(false);
    for (auto *sp : packed) sp->sprite()->visible = false;
    Texture *atlas          = makeAtlas4(gfx, 16);
    auto    *atlasSp        = makeSprite(atlas, 80.f, 60.f, 240.f, 180.f, false);
    atlasSp->sprite()->quad = gfx->newQuad(0, 0, 16, 16);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "composite", "atlas_quad", bg);
}

TEST_CASE("graphics.imageAudit.stylizeAndSsaa") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto          studio = makeStudio3D(gfx);
    Mesh         *cyl    = gfx->newMeshCylinder(24, 1, true);
    studio.subject->setMesh(cyl);
    studio.subject->setTexture(makeChecker(gfx, 32, 32));
    studio.subject->setScale(0.45f, 0.7f, 0.45f);

    auto   *stylize = eve::stylize::Stylize::create();
    Shader *toon    = stylize->newMeshShader(gfx, "cartoon");
    REQUIRE(toon != nullptr);
    if (toon->hasUniform("bands")) toon->sendFloat("bands", 3.f);
    if (toon->hasUniform("rimPower")) toon->sendFloat("rimPower", 2.6f);
    if (toon->hasUniform("rimStrength")) toon->sendFloat("rimStrength", 0.45f);
    studio.subject->setShader(toon);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "stylize", "mesh_cartoon", bg);

    Shader *ink = stylize->newMeshShader(gfx, "ink");
    REQUIRE(ink != nullptr);
    studio.subject->setShader(ink);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "stylize", "mesh_ink", bg);
    studio.subject->setShader(nullptr);

    Texture *sceneTex = capturePresented(_ZEROERR_TEST_CONTEXT, gfx);
    REQUIRE(sceneTex != nullptr);
    const int w = 400, h = 300;
    Canvas   *dest = gfx->newCanvas(w, h);
    REQUIRE(dest != nullptr);
    const char *posts[] = {"cartoon", "watercolor", "ink", "pixel"};
    for (const char *id : posts) {
        eve::stylize::StylePass *pass = stylize->newPass(gfx, id);
        REQUIRE(pass != nullptr);
        pass->applyTo(gfx, sceneTex, dest);
        blitAndPresent(gfx, dest->getTexture());
        auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "stylize", (std::string("post_") + id).c_str(), bg);
        delete pass;
    }

    std::unique_ptr<AntiAliasing> aa(gfx->newAntiAliasing());
    aa->setQuality("low");
    aa->setMode("ssaa");
    aa->applyTo(gfx, sceneTex, dest);
    blitAndPresent(gfx, dest->getTexture());
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "stylize", "aa_ssaa", bg);
}

TEST_CASE("graphics.imageAudit.multiObjectLod") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto          studio = makeStudio3D(gfx);
    Mesh         *cyl    = gfx->newMeshCylinder(12, 1, true);
    Mesh         *hi     = gfx->newMeshSphere(20, 14);
    Mesh         *lo     = gfx->newMeshSphere(8, 6);

    auto *left = Renderable3D::create();
    left->setMesh(studio.sphere);
    left->setTexture(makeSolid(gfx, 210, 90, 80));
    left->setPosition(-1.1f, 0.2f, 0.f);
    left->setScale(0.4f, 0.4f, 0.4f);
    left->setCastShadow(true);

    auto *right = Renderable3D::create();
    right->setMesh(cyl);
    right->setTexture(makeSolid(gfx, 80, 160, 210));
    right->setPosition(1.1f, 0.25f, 0.f);
    right->setScale(0.35f, 0.55f, 0.35f);
    right->setCastShadow(true);

    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "multi", "gallery", bg);

    studio.subject->setTint(1.f, 0.25f, 0.2f, 1.f);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "multi", "tint_red", bg);
    studio.subject->setTint(1.f, 1.f, 1.f, 0.72f);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "multi", "tint_alpha", bg);
    studio.subject->setTint(1.f, 1.f, 1.f, 1.f);

    left->setVisible(false);
    right->setVisible(false);
    studio.subject->setMesh(nullptr);
    auto *matA = gfx->newMaterial();
    matA->setShadingModel("pbr");
    matA->setAlbedoTexture(makeSolid(gfx, 220, 180, 70));
    auto *matB = gfx->newMaterial();
    matB->setShadingModel("pbr");
    matB->setAlbedoTexture(makeSolid(gfx, 70, 140, 210));
    studio.subject->setPart(0, "body", hi, matA);
    studio.subject->setPart(1, "cap", cyl, matB);
    studio.subject->setScale(0.5f, 0.5f, 0.5f);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "multi", "mesh_parts", bg);
    studio.subject->clearParts();
    studio.subject->setMesh(hi);
    left->setVisible(true);
    right->setVisible(true);

    studio.subject->setMeshLod(0, hi);
    studio.subject->setMeshLod(1, lo, 3.2f);
    studio.cam->setEye(0.f, 1.2f, 2.2f);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "multi", "lod_near", bg);
    studio.cam->setEye(0.f, 2.2f, 7.5f);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "multi", "lod_far", bg);
    studio.cam->setEye(4.2f, 1.5f, 0.1f);
    studio.cam->setTarget(0.f, 0.15f, 0.f);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "multi", "camera_side", bg);
    studio.cam->setEye(0.f, 1.5f, 4.0f);

    Canvas *cv = gfx->newCanvas(400, 300);
    REQUIRE(cv != nullptr);
    Texture *cellA       = makeSolid(gfx, 200, 90, 70);
    Texture *cellB       = makeSolid(gfx, 70, 180, 110);
    auto    *c0          = makeSprite(cellA, 40.f, 30.f, 160.f, 120.f);
    auto    *c1          = makeSprite(cellB, 200.f, 30.f, 160.f, 120.f);
    auto    *c2          = makeSprite(cellB, 40.f, 150.f, 160.f, 120.f);
    auto    *c3          = makeSprite(cellA, 200.f, 150.f, 160.f, 120.f);
    c0->sprite()->canvas = cv;
    c1->sprite()->canvas = cv;
    c2->sprite()->canvas = cv;
    c3->sprite()->canvas = cv;
    RenderSystem::render(*gfx);
    blitAndPresent(gfx, cv->getTexture());
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "multi", "canvas_2d", bg);
}

TEST_CASE("graphics.imageAudit.lightsAndXform") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto          studio = makeStudio3D(gfx);

    auto *pR = Light3D::createLight("point");
    pR->setPosition(-1.3f, 1.0f, 0.8f);
    pR->setColor(1.f, 0.25f, 0.2f, 5.f);
    pR->setRadius(5.f);
    pR->setEnabled(true);
    auto *pG = Light3D::createLight("point");
    pG->setPosition(1.2f, 0.9f, 0.6f);
    pG->setColor(0.2f, 1.f, 0.35f, 4.5f);
    pG->setRadius(5.f);
    pG->setEnabled(true);
    auto *pB = Light3D::createLight("point");
    pB->setPosition(0.1f, 1.2f, -1.0f);
    pB->setColor(0.25f, 0.4f, 1.f, 5.f);
    pB->setRadius(5.f);
    pB->setEnabled(true);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "lights", "dir_rgb_points", bg);

    studio.sun->setShadowStrength(0.35f);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "lights", "shadow_soft", bg);
    studio.sun->setShadowStrength(1.f);
    studio.subject->setCastShadow(false);
    studio.subject->setReceiveShadow(false);
    studio.ground->setReceiveShadow(false);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "lights", "cast_receive_off", bg);
    studio.subject->setCastShadow(true);
    studio.subject->setReceiveShadow(true);
    studio.ground->setReceiveShadow(true);

    studio.subject->setRotation(0.9f, 0.4f, 0.15f);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "lights", "yaw_pitch_roll", bg);
    studio.subject->setRotation(0.f, 0.f, 0.f);

    studio.cam->setUp(0.25f, 0.97f, 0.f);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "lights", "camera_roll", bg);
    studio.cam->setUp(0.f, 1.f, 0.f);

    pR->setEnabled(false);
    pG->setEnabled(false);
    pB->setEnabled(false);
    studio.sun->setEnabled(false);
    studio.subject->setMetallic(1.f);
    studio.subject->setRoughness(0.12f);
    studio.cam->setEnvIntensity(1.8f);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "lights", "ibl_metal", bg);
}

TEST_CASE("graphics.imageAudit.samplerMorph") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto          studio  = makeStudio3D(gfx);
    Texture      *checker = makeChecker(gfx, 32, 32);
    studio.subject->setTexture(checker);

    gfx->setTextureSampler(checker, "nearest", "none", 1.f, 0.f);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "sampler", "nearest", bg);
    gfx->setTextureSampler(checker, "linear", "none", 1.f, 0.f);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "sampler", "linear", bg);

    TextureCreateInfo    mip = TextureCreateInfo::withMipmaps(true, 8.f);
    std::vector<uint8_t> mipPx(32 * 32 * 4);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const bool   on = ((x / 4) + (y / 4)) & 1;
            const size_t i  = size_t((y * 32 + x) * 4);
            mipPx[i + 0]    = on ? 210 : 50;
            mipPx[i + 1]    = on ? 210 : 50;
            mipPx[i + 2]    = on ? 215 : 55;
            mipPx[i + 3]    = 255;
        }
    }
    Texture *mipTex = gfx->newTexture(32, 32, mipPx.data(), mip);
    studio.subject->setTexture(mipTex);
    gfx->setTextureSampler(mipTex, "linear", "linear", 8.f, 2.5f);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "sampler", "mip_lod_bias", bg);

    Texture *rep = makeChecker(gfx, 16, 16, true);
    studio.ground->setTexture(rep);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "sampler", "repeat_uv", bg);

    const float hs    = 0.45f;
    const float pos[] = {
        -hs, -hs, -hs, hs, -hs, -hs, hs, hs, -hs, -hs, hs, -hs, -hs, -hs, hs, hs, -hs, hs, hs, hs, hs, -hs, hs, hs,
    };
    float nrm[24];
    float uv[16];
    for (int i = 0; i < 8; ++i) {
        float x = pos[i * 3 + 0], y = pos[i * 3 + 1], z = pos[i * 3 + 2];
        float len = std::sqrt(x * x + y * y + z * z);
        if (len < 1e-5f) len = 1.f;
        nrm[i * 3 + 0] = x / len;
        nrm[i * 3 + 1] = y / len;
        nrm[i * 3 + 2] = z / len;
        uv[i * 2 + 0]  = (i & 1) ? 1.f : 0.f;
        uv[i * 2 + 1]  = (i & 2) ? 1.f : 0.f;
    }
    const uint32_t idx[]     = {0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1,
                                3, 2, 6, 3, 6, 7, 0, 3, 7, 0, 7, 4, 1, 5, 6, 1, 6, 2};
    Mesh          *morphMesh = gfx->newMeshFromArrays(pos, nrm, uv, 8, idx, 36);
    morphMesh->initMorphBase(8, pos, nrm, uv);
    float delta[24];
    for (int i = 0; i < 8; ++i) {
        delta[i * 3 + 0] = nrm[i * 3 + 0] * 0.28f;
        delta[i * 3 + 1] = nrm[i * 3 + 1] * 0.28f;
        delta[i * 3 + 2] = nrm[i * 3 + 2] * 0.28f;
    }
    REQUIRE(morphMesh->addMorphTarget("inflate", delta));
    studio.subject->setMesh(morphMesh);
    studio.subject->setTexture(makeSolid(gfx, 200, 160, 90));
    gfx->bakeMeshMorph(morphMesh);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "sampler", "morph_base", bg);
    morphMesh->setMorphWeight("inflate", 1.f);
    gfx->bakeMeshMorph(morphMesh);
}

TEST_CASE("graphics.imageAudit.gbufferViews") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    auto studio = makeStudio3D(gfx);
    (void)studio;
    RenderControl *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);
    rc->enable("gbuffer");
    rc->enable("gbufferAlbedo");
    rc->compile();

    gfx->setScreenReadbackEnabled(false);
    for (int i = 0; i < 2; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    GBuffer *gb = rc->getGBuffer();
    REQUIRE(gb != nullptr);
    Texture *albedo = gb->getAlbedoTexture();
    Texture *normal = gb->getNormalTexture();
    Texture *depth  = gb->getDepthTexture();
    REQUIRE(albedo != nullptr);
    REQUIRE(normal != nullptr);
    REQUIRE(depth != nullptr);

    // G-buffer alpha stores depth (albedo) or packed PBR parameters (normal),
    // not opacity. Display RGB without blending against the swapchain clear.
    // The attachments themselves clear to black, so empty buffers must still
    // fail the same occupancy requirement rather than count as foreground.
    auto presentBuffer = [gfx](Texture *texture) {
        gfx->setScreenReadbackEnabled(true);
        gfx->setBackgroundColorRGBA(0.f, 0.f, 0.f, 1.f);
        gfx->clearScreen();
        gfx->drawTexturedRectShaderUV(texture, nullptr, 0, 0, float(gfx->getWidth()), float(gfx->getHeight()), 0, 0, 1,
                                      1, Color(1, 1, 1, 1), false, BlendMode::Opaque);
        gfx->present();
    };
    const RenderAuditBg bufferBackground{0.f, 0.f, 0.f};
    presentBuffer(albedo);
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "gbuffer", "albedo", bufferBackground);
    presentBuffer(normal);
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "gbuffer", "normal", bufferBackground);

    rc->disable("gbuffer");
    rc->compile();
}

TEST_CASE("graphics.imageAudit.occlusionScatter") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    const int w = 400, h = 300;

    auto *plate = makeSprite(makeSolid(gfx, 190, 175, 150), 0.f, 0.f, float(w), float(h), false);
    plate->setCastOcclusion(false);
    auto *occA = makeSprite(makeSolid(gfx, 40, 40, 48), 70.f, 80.f, 90.f, 110.f, false);
    occA->setCastOcclusion(true);
    auto *occB = makeSprite(makeSolid(gfx, 48, 40, 40), 230.f, 90.f, 80.f, 100.f, false);
    occB->setCastOcclusion(true);

    gfx->setScreenReadbackEnabled(true);
    RenderSystem::render(*gfx);
    std::unique_ptr<ImageData> sceneImg(gfx->newImageData());
    REQUIRE(sceneImg.get() != nullptr);
    Texture *sceneTex = gfx->newTexture(sceneImg.get());
    gfx->setScreenReadbackEnabled(false);

    std::unique_ptr<Volumetric> vol(gfx->newVolumetric());
    vol->setQuality("low");
    vol->setMode("screenspace");
    vol->setLightScreenPos(320.f, 40.f, float(w), float(h));
    vol->setIntensity(0.9f);

    Canvas *occMap   = gfx->newCanvas(w, h);
    Canvas *composed = gfx->newCanvas(w, h);
    REQUIRE(occMap != nullptr);
    REQUIRE(composed != nullptr);

    gfx->setCanvas(occMap);
    vol->beginOcclusionMap(gfx, 320.f, 40.f, 22.f);
    vol->drawOccluders2D(gfx);
    gfx->setCanvas();
    gfx->setCanvas(composed);
    gfx->clearScreen();
    gfx->drawTexturedRectRGBA(sceneTex, 0, 0, float(w), float(h), 1, 1, 1, 1);
    vol->scatter(gfx, occMap->getTexture());
    gfx->setCanvas();
    blitAndPresent(gfx, composed->getTexture());
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "scatter", "scatter_on", bg);

    occA->setCastOcclusion(false);
    occB->setCastOcclusion(false);
    gfx->setCanvas(occMap);
    vol->beginOcclusionMap(gfx, 320.f, 40.f, 22.f);
    vol->drawOccluders2D(gfx);
    gfx->setCanvas();
    gfx->setCanvas(composed);
    gfx->clearScreen();
    gfx->drawTexturedRectRGBA(sceneTex, 0, 0, float(w), float(h), 1, 1, 1, 1);
    vol->scatter(gfx, occMap->getTexture());
    gfx->setCanvas();
    blitAndPresent(gfx, composed->getTexture());
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "scatter", "scatter_off", bg);
}

TEST_CASE("graphics.imageAudit.textureSources") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto          studio = makeStudio3D(gfx);

    std::unique_ptr<ImageData> dst(makeRgba(32, 32, 50, 90, 160));
    std::unique_ptr<ImageData> src(makeRgba(16, 16, 220, 70, 40));
    dst->paste(src.get(), 8, 8, 0, 0, 16, 16);
    Texture *pasted = gfx->newTexture(dst.get());
    REQUIRE(pasted != nullptr);
    studio.subject->setTexture(pasted);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "texsrc", "imagedata_paste", bg);

    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs != nullptr);
    REQUIRE(fs->setIdentity("ev_ut_image_audit_texfile", true));
    REQUIRE(fs->setupWriteDirectory());
    const std::string texDir = pathBesideThisSource("Textures");
    fs->allowMountingForPath(texDir);
    REQUIRE(fs->mount(texDir, "", false));
    std::unique_ptr<eve::filesystem::FileData> diffFile(fs->read("Rock1_Diffuse.png"));
    std::unique_ptr<eve::filesystem::FileData> nrmFile(fs->read("Rock_Normal.png"));
    REQUIRE(diffFile.get() != nullptr);
    REQUIRE(nrmFile.get() != nullptr);
    auto                      *imgMod = eve::image::Image::create();
    std::unique_ptr<ImageData> diffImg(imgMod->newImageData(diffFile.get()));
    std::unique_ptr<ImageData> nrmImg(imgMod->newImageData(nrmFile.get()));
    REQUIRE(diffImg.get() != nullptr);
    REQUIRE(nrmImg.get() != nullptr);
    Texture *diff = gfx->newTexture(diffImg.get(), TextureCreateInfo::withMipmaps(true));
    Texture *nrm  = gfx->newTexture(nrmImg.get(), TextureCreateInfo::withMipmaps(true));
    REQUIRE(diff != nullptr);
    REQUIRE(nrm != nullptr);
    REQUIRE(diff->getMipmapCount() > 1);
    REQUIRE(diff->getSampler().maxAnisotropy > 1.f);
    studio.subject->setTexture(diff);
    studio.subject->setNormalTexture(nrm);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "texsrc", "file_rock", bg);
}

TEST_CASE("graphics.imageAudit.renderControlToggles") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg  bg{0.08f, 0.09f, 0.11f};
    auto           studio = makeStudio3D(gfx);
    RenderControl *rc     = gfx->getRenderControl();
    REQUIRE(rc != nullptr);

    rc->disable("depthTest");
    rc->compile();
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "rctrl", "depth_test_off", bg);
    rc->enable("depthTest");

    rc->disable("hair");
    rc->compile();
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "rctrl", "hair_off", bg);
    rc->enable("hair");

    Shader *hairSh = gfx->newHairShader();
    REQUIRE(hairSh != nullptr);
    auto *card = Renderable3D::create();
    card->setMesh(makeHairCard(gfx));
    card->setTexture(makeSolid(gfx, 210, 160, 70));
    card->setShader(hairSh);
    card->setHair(true);
    card->setPosition(0.f, 0.05f, 0.35f);
    card->setCastShadow(false);
    studio.subject->setVisible(false);
    rc->disable("forward");
    rc->compile();
    makeSprite(makeSolid(gfx, 90, 140, 200), 0.f, 0.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 200, 90, 70), 200.f, 0.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 70, 180, 110), 0.f, 150.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 220, 200, 60), 200.f, 150.f, 200.f, 150.f, false);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "rctrl", "forward_off_hair", bg);
    rc->enable("forward");
    rc->compile();
    studio.subject->setVisible(true);
    card->setVisible(false);

    TextureCreateInfo    mip = TextureCreateInfo::withMipmaps(true, 16.f);
    std::vector<uint8_t> px(32 * 32 * 4);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const bool   on = ((x / 4) + (y / 4)) & 1;
            const size_t i  = size_t((y * 32 + x) * 4);
            px[i + 0]       = on ? 210 : 50;
            px[i + 1]       = on ? 180 : 70;
            px[i + 2]       = on ? 90 : 160;
            px[i + 3]       = 255;
        }
    }
    Texture *aniso = gfx->newTexture(32, 32, px.data(), mip);
    gfx->setTextureSampler(aniso, "linear", "linear", 16.f, 0.f);
    studio.ground->setTexture(aniso);
    studio.subject->setTexture(aniso);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "rctrl", "aniso_mip", bg);
}

TEST_CASE("graphics.imageAudit.sprite2dCamera") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    Texture *cells[4] = {
        makeSolid(gfx, 220, 80, 70),
        makeSolid(gfx, 70, 190, 90),
        makeSolid(gfx, 70, 110, 220),
        makeSolid(gfx, 230, 200, 60),
    };
    Renderable2D *packed[4];
    for (int i = 0; i < 4; ++i) {
        packed[i] = makeSprite(cells[i], float((i % 2) * 200), float((i / 2) * 150), 200.f, 150.f);
    }
    auditGpu2D(_ZEROERR_TEST_CONTEXT, gfx, "sprite2d", "identity", bg);

    auto *cam = Camera2D::createCamera();
    cam->setPosition(200.f, 150.f);
    cam->setZoom(1.3f);
    const float visW = 400.f / 1.3f;
    const float visH = 300.f / 1.3f;
    const float x0   = 200.f - visW * 0.5f;
    const float y0   = 150.f - visH * 0.5f;
    const float cw   = visW * 0.5f;
    const float ch   = visH * 0.5f;
    for (int i = 0; i < 4; ++i) {
        packed[i]->transform()->x   = x0 + float(i % 2) * cw;
        packed[i]->transform()->y   = y0 + float(i / 2) * ch;
        packed[i]->sprite()->width  = cw;
        packed[i]->sprite()->height = ch;
        packed[i]->transform()->sx  = 1.f;
        packed[i]->transform()->sy  = 1.f;
    }
    auditGpu2D(_ZEROERR_TEST_CONTEXT, gfx, "sprite2d", "zoom_1_3", bg);

    cam->setZoom(1.f);
    for (int i = 0; i < 4; ++i) {
        packed[i]->transform()->x   = float((i % 2) * 200);
        packed[i]->transform()->y   = float((i / 2) * 150);
        packed[i]->sprite()->width  = 100.f;
        packed[i]->sprite()->height = 75.f;
        packed[i]->transform()->sx  = 2.f;
        packed[i]->transform()->sy  = 2.f;
    }
    auditGpu2D(_ZEROERR_TEST_CONTEXT, gfx, "sprite2d", "scale_sx_sy", bg);

    cam->data()->active = false;
    std::vector<uint32_t> frag(custom2d_frag_spv, custom2d_frag_spv + custom2d_frag_spv_count);
    Shader               *sh = gfx->newShaderFromSpv({}, frag);
    REQUIRE(sh != nullptr);
    sh->declareFloat("factor");
    sh->sendFloat("factor", 0.55f);
    for (int i = 0; i < 4; ++i) {
        packed[i]->transform()->x   = float((i % 2) * 200);
        packed[i]->transform()->y   = float((i / 2) * 150);
        packed[i]->sprite()->width  = 200.f;
        packed[i]->sprite()->height = 150.f;
        packed[i]->transform()->sx  = 1.f;
        packed[i]->transform()->sy  = 1.f;
        packed[i]->sprite()->shader = sh;
    }
    auditGpu2D(_ZEROERR_TEST_CONTEXT, gfx, "sprite2d", "custom_spv", bg);
}

TEST_CASE("graphics.imageAudit.overlayFx") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto          studio = makeStudio3D(gfx);
    (void)studio;
    Texture *sceneTex = capturePresented(_ZEROERR_TEST_CONTEXT, gfx);
    REQUIRE(sceneTex != nullptr);

    auto                     *stylize = eve::stylize::Stylize::create();
    eve::stylize::StyleChain *chain   = stylize->newChain();
    eve::stylize::StylePass  *cartoon = stylize->newPass(gfx, "cartoon");
    eve::stylize::StylePass  *pixel   = stylize->newPass(gfx, "pixel");
    REQUIRE(chain != nullptr);
    REQUIRE(cartoon != nullptr);
    REQUIRE(pixel != nullptr);
    chain->add(cartoon);
    chain->add(pixel);
    Canvas *dest = gfx->newCanvas(400, 300);
    Canvas *temp = gfx->newCanvas(400, 300);
    chain->apply(gfx, sceneTex, dest, temp);
    blitAndPresent(gfx, dest->getTexture());
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "overlay", "stylechain_cartoon_pixel", bg);
    delete pixel;
    delete cartoon;
    delete chain;

    auto raw = [&]() {
        std::ifstream in(pathBesideThisSource("fonts/FontAwesome.ttf"), std::ios::binary);
        return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }();
    REQUIRE(!raw.empty());
    eve::data::ByteData                  bytes(raw.data(), raw.size());
    std::unique_ptr<eve::font::FontData> fd(eve::font::Font::create()->newFontData(&bytes, 32));
    REQUIRE(fd.get() != nullptr);
    Font *gpuFont = gfx->newFont(fd.get(), std::string("\xEF\x80\x80"));
    REQUIRE(gpuFont != nullptr);
    gfx->setFont(gpuFont);
    gfx->setPresentOverlay(&printOverlayThunk, gfx);
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "overlay", "font_draw_text", bg);
    gfx->setPresentOverlay(nullptr, nullptr);

    auto                            *parts = eve::particles::Particles::create();
    eve::particles::ParticleEmitter *em    = parts->newEmitter(64);
    em->setPosition(200.f, 140.f);
    em->setEmissionArea("rect", 120.f, 70.f);
    em->setParticleLifetime(8.f, 8.f);
    em->setSpeed(0.f, 0.f);
    em->setParticleSize(36.f, 36.f);
    em->setColorStart(1.f, 0.55f, 0.15f, 1.f);
    em->setColorEnd(1.f, 0.55f, 0.15f, 1.f);
    em->setTexture(makeSolid(gfx, 230, 140, 50));
    em->emit(48);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "overlay", "particles", bg, false,
                  [&]() { eve::particles::ParticleRenderSystem::render(gfx); });
}

TEST_CASE("graphics.imageAudit.voxelAndHair") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto          studio = makeStudio3D(gfx);
    studio.subject->setVisible(false);
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);

    eve::voxel::VoxelWorld world;
    for (int z = 0; z < 14; ++z) {
        for (int x = 0; x < 14; ++x) world.setVoxel(x, 0, z, uint8_t(1 + (x + z) % 4));
    }
    for (int y = 1; y < 8; ++y) {
        for (int z = 5; z < 8; ++z)
            for (int x = 5; x < 8; ++x) world.setVoxel(x, y, z, 3);
    }
    REQUIRE(world.remeshDirty() > 0);
    Texture *atlas = makeAtlasGrid(gfx, 2, 16);
    studio.cam->setEye(7.f, 9.f, 18.f);
    studio.cam->setTarget(7.f, 2.f, 7.f);
    auto drawVoxels = [&]() {
        auto            cd = studio.cam->data();
        const glm::vec3 eye(cd->eyeX, cd->eyeY, cd->eyeZ);
        const glm::vec3 target(cd->targetX, cd->targetY, cd->targetZ);
        const glm::vec3 up(cd->upX, cd->upY, cd->upZ);
        const glm::mat4 viewM  = glm::lookAtRH(eye, target, up);
        const float     aspect = float(gfx->getWidth()) / float(std::max(gfx->getHeight(), 1));
        const glm::mat4 projM =
            perspectiveVulkanRH_ZO(cd->fovYDeg * 0.017453292519943295f, aspect, cd->nearZ, cd->farZ);
        const glm::mat4 vp = projM * viewM;
        gfx->setMesh3DViewProj(vp);
        world.selectVisible(&vp[0][0], cd->eyeX, cd->eyeY, cd->eyeZ, 40.f, true);
        world.drawVisible(gfx, atlas, 2);
    };
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "voxel_hair", "voxels", bg, false, drawVoxels);

    studio.cam->setEye(0.f, 1.5f, 4.0f);
    studio.cam->setTarget(0.f, 0.15f, 0.f);
    studio.ground->setVisible(true);
    Shader *hairSh = gfx->newHairShader();
    REQUIRE(hairSh != nullptr);
    auto *card = Renderable3D::create();
    card->setMesh(makeHairCard(gfx));
    card->setTexture(makeSolid(gfx, 200, 150, 80));
    card->setShader(hairSh);
    card->setHair(true);
    card->setPosition(0.f, 0.05f, 0.2f);
    card->setCastShadow(false);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "voxel_hair", "hair_cards", bg);
}

TEST_CASE("graphics.imageAudit.uiAndMap") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    auto                *map   = eve::map::Map::create();
    Texture             *tiles = makeAtlasGrid(gfx, 4, 8);
    eve::map::TileLayer *layer = map->newLayer(25, 19, 16.f, 16.f);
    REQUIRE(layer != nullptr);
    layer->setTileset(tiles, 1, 4);
    layer->setTilesetTileSize(8, 8);
    for (int y = 0; y < 19; ++y)
        for (int x = 0; x < 25; ++x) layer->setTile(x, y, 1 + (x + y) % 15);

    auto *cam = Camera2D::createCamera();
    cam->setPosition(200.f, 150.f);
    cam->setZoom(1.f);
    layer->setCamera(cam);

    auto presentMap = [&]() {
        gfx->setScreenReadbackEnabled(true);
        gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.11f, 1.f);
        gfx->clearScreen();
        map->render(gfx);
        gfx->present();
    };
    presentMap();
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "uimap", "ortho_fill", bg);

    cam->setZoom(1.25f);
    presentMap();
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "uimap", "ortho_zoom", bg);
    cam->setZoom(1.f);

    auto *ui = eve::ui::UI::create();
    REQUIRE(ui->initBackend());
    ui->beginFrameAndRender();
    presentMap();
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "uimap", "imgui_hook", bg);
    ui->dispatchEvents();

    layer->setVisible(false);
    eve::map::TileLayer *logic   = map->newLayer(16, 12, 16.f, 16.f);
    eve::map::TileLayer *display = map->newLayer(1, 1, 16.f, 16.f);
    logic->setTileset(tiles, 1, 4);
    display->setTileset(tiles, 1, 4);
    display->setTilesetTileSize(8, 8);
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 16; ++x) logic->setTile(x, y, ((x + y) % 3 == 0) ? 1 : 0);
    eve::map::DualGridOptions opts;
    opts.filledGid = 1;
    opts.hideLogic = true;
    REQUIRE(eve::map::resolveDualGrid(logic, display, opts, nullptr));
    display->setCamera(cam);
    makeSprite(makeSolid(gfx, 70, 90, 130), 0.f, 0.f, 400.f, 300.f, false)->sprite()->layer = -1;
    presentMap();
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "uimap", "dual_grid", bg);

    display->setVisible(false);
    eve::map::TileLayer *iso   = map->newLayer(18, 18, 32.f, 16.f);
    iso->config()->orientation = eve::map::MapOrientation::Isometric;
    iso->config()->originX     = 200.f;
    iso->config()->originY     = 16.f;
    iso->setTileset(tiles, 1, 4);
    iso->setTilesetTileSize(8, 8);
    iso->setCamera(cam);
    for (int y = 0; y < 18; ++y)
        for (int x = 0; x < 18; ++x) iso->setTile(x, y, 1 + (x * 3 + y) % 15);
    presentMap();
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "uimap", "isometric", bg);
}

TEST_CASE("graphics.imageAudit.skinnedPoseContracts") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto          studio = makeStudio3D(gfx);
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);

    auto  pinnedModel = eve::test::pinResource(loadCesiumMan("ev_ut_image_audit_skin_contract"));
    auto *model       = pinnedModel.get();
    REQUIRE(model != nullptr);
    const int mi = findFirstSkinnedMesh(model);
    REQUIRE(mi >= 0);
    SUB_CASE("graphics.imageAudit.skinnedStill") {
        const aiMesh *src = model->getMesh(mi);
        REQUIRE(src != nullptr);
        Mesh *gpu = gfx->newMeshFromAssimp(*src);
        REQUIRE(gpu != nullptr);
        studio.subject->setMesh(gpu);
        studio.subject->setTexture(makeSolid(gfx, 200, 170, 120));
        studio.subject->setScale(1.15f, 1.15f, 1.15f);
        studio.subject->setPosition(0.f, 0.f, 0.f);
        studio.subject->setYaw(1.2f);
        studio.cam->setEye(0.f, 1.3f, 3.2f);
        studio.cam->setTarget(0.f, 0.7f, 0.f);
        studio.ground->setPosition(0.f, 0.f, 0.f);
        auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "skinned", "bind_pose", bg);
    };
    SUB_CASE("graphics.imageAudit.skinnedPose") {
        const aiMesh *src = model->getMesh(mi);
        REQUIRE(src != nullptr);

        using eve::animation::AnimClip;
        using eve::animation::AnimImporter;
        using eve::animation::AnimPose;
        using eve::animation::AnimSkeleton;
        using eve::animation::AnimSkin;
        std::unique_ptr<AnimSkeleton> sk(AnimImporter::loadSkeletonFromModel(model));
        REQUIRE(sk.get() != nullptr);
        std::unique_ptr<AnimClip> clip(AnimImporter::loadClipFromModel(model, sk.get(), 0));
        REQUIRE(clip.get() != nullptr);
        std::unique_ptr<AnimSkin> skin(AnimSkin::fromModel(model, mi, sk.get()));
        REQUIRE(skin.get() != nullptr);

        auto uploadAt = [&](float t) {
            AnimPose pose;
            clip->sample(t, &pose, sk.get());
            pose.computeWorld(sk.get());
            std::vector<float> pos;
            REQUIRE(skin->skinPositionsTo(&pose, pos));
            return uploadSkinnedAiMesh(gfx, src, pos);
        };

        studio.subject->setMesh(uploadAt(0.f));
        studio.subject->setTexture(makeSolid(gfx, 200, 170, 120));
        studio.subject->setScale(1.15f, 1.15f, 1.15f);
        studio.subject->setPosition(0.f, 0.f, 0.f);
        studio.subject->setYaw(1.2f);
        studio.cam->setEye(0.f, 1.3f, 3.2f);
        studio.cam->setTarget(0.f, 0.7f, 0.f);
        studio.ground->setPosition(0.f, 0.f, 0.f);
        auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "skinned", "pose_t0", bg);

        studio.subject->setMesh(uploadAt(clip->getDuration() * 0.5f));
        auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "skinned", "pose_tmid", bg);
    };
}


TEST_CASE("graphics.imageAudit.reloadTex") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto          studio = makeStudio3D(gfx);
    auto         *fs     = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_image_audit_texreload", true));
    REQUIRE(fs->setupWriteDirectory());
    const std::string texDir = pathBesideThisSource("Textures");
    fs->allowMountingForPath(texDir);
    REQUIRE(fs->mount(texDir, "", false));
    Texture *diff = gfx->newTextureFromFile("Rock1_Diffuse.png");
    REQUIRE(diff != nullptr);
    studio.subject->setTexture(diff);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "reload", "before", bg);
    REQUIRE(gfx->reloadTextureFromFile("Rock1_Diffuse.png"));
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "reload", "after", bg);
}

TEST_CASE("graphics.imageAudit.morphInflate") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto          studio = makeStudio3D(gfx);
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);

    const float hs    = 0.45f;
    const float pos[] = {
        -hs, -hs, -hs, hs, -hs, -hs, hs, hs, -hs, -hs, hs, -hs, -hs, -hs, hs, hs, -hs, hs, hs, hs, hs, -hs, hs, hs,
    };
    float nrm[24];
    float uv[16];
    for (int i = 0; i < 8; ++i) {
        float x = pos[i * 3 + 0], y = pos[i * 3 + 1], z = pos[i * 3 + 2];
        float len = std::sqrt(x * x + y * y + z * z);
        if (len < 1e-5f) len = 1.f;
        nrm[i * 3 + 0] = x / len;
        nrm[i * 3 + 1] = y / len;
        nrm[i * 3 + 2] = z / len;
        uv[i * 2 + 0]  = (i & 1) ? 1.f : 0.f;
        uv[i * 2 + 1]  = (i & 2) ? 1.f : 0.f;
    }
    const uint32_t idx[]     = {0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1,
                                3, 2, 6, 3, 6, 7, 0, 3, 7, 0, 7, 4, 1, 5, 6, 1, 6, 2};
    Mesh          *morphMesh = gfx->newMeshFromArrays(pos, nrm, uv, 8, idx, 36);
    morphMesh->initMorphBase(8, pos, nrm, uv);
    float delta[24];
    for (int i = 0; i < 8; ++i) {
        delta[i * 3 + 0] = nrm[i * 3 + 0] * 0.28f;
        delta[i * 3 + 1] = nrm[i * 3 + 1] * 0.28f;
        delta[i * 3 + 2] = nrm[i * 3 + 2] * 0.28f;
    }
    REQUIRE(morphMesh->addMorphTarget("inflate", delta));
    studio.subject->setMesh(morphMesh);
    studio.subject->setTexture(makeSolid(gfx, 200, 160, 90));
    gfx->bakeMeshMorph(morphMesh);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "morph", "base", bg);
    morphMesh->setMorphWeight("inflate", 1.f);
    gfx->bakeMeshMorph(morphMesh);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "morph", "inflate", bg);
    morphMesh->setMorphWeight("inflate", 0.45f);
    gfx->bakeMeshMorph(morphMesh);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "morph", "inflate_mid", bg);
}

TEST_CASE("graphics.imageAudit.procgenAssets") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto          studio = makeStudio3D(gfx);
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);

    auto *pg = eve::procgen::Procgen::create();
    REQUIRE(pg != nullptr);
    auto paramsResult = eve::procgen::Procgen::newParamsHandle();
    REQUIRE(paramsResult.ok());
    auto paramsHandle = std::move(paramsResult).takeValue();
    auto paramsView   = eve::procgen::Procgen::resolve(paramsHandle);
    REQUIRE(paramsView.isBound());
    eve::procgen::Params &params = *paramsView;
    params.setSeed(11);
    params.setSize(64, 64);
    params.setInt("colors", 6);
    params.setInt("pixelSize", 2);
    params.setInt("seamless", 1);
    const char *recipes[] = {"tex.soil", "tex.stone", "tex.marble"};
    for (const char *id : recipes) {
        auto textureView = pg->generateTextureBorrowed(id, paramsHandle, gfx);
        REQUIRE(textureView.isBound());
        studio.subject->setTexture(textureView.get());
        auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "procgen", id, bg);
    }

    auto normalResult = pg->generateNormalImageHandle("tex.stone", paramsHandle);
    REQUIRE(normalResult.ok());
    auto normalHandle = std::move(normalResult).takeValue();
    CHECK(!eve::procgen::Procgen::isStale(normalHandle));
    auto normalView = eve::procgen::Procgen::resolve(normalHandle);
    REQUIRE(normalView.isBound());
    Texture *nrm = gfx->newTextureFromImageData(normalView.get(), true, true);
    REQUIRE(nrm != nullptr);
    studio.subject->setNormalTexture(nrm);
    auto stoneTextureView = pg->generateTextureBorrowed("tex.stone", paramsHandle, gfx);
    REQUIRE(stoneTextureView.isBound());
    studio.subject->setTexture(stoneTextureView.get());
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "procgen", "stone_normal", bg);
    studio.subject->setNormalTexture(nullptr);
    auto releaseNormalResult = eve::procgen::Procgen::release(normalHandle);
    REQUIRE(releaseNormalResult.ok());
    CHECK(eve::procgen::Procgen::isStale(normalHandle));

    params.setSeed(1);
    params.setInt("resolution", 16);
    params.setString("field", "sphere");
    params.setFloat("radius", 0.7f);
    params.setFloat("isolevel", 0.f);
    auto meshView = pg->generateMeshBorrowed("mesh.marchingcubes", paramsHandle, gfx);
    REQUIRE(meshView.isBound());
    studio.subject->setMesh(meshView.get());
    auto marbleTextureView = pg->generateTextureBorrowed("tex.marble", paramsHandle, gfx);
    REQUIRE(marbleTextureView.isBound());
    studio.subject->setTexture(marbleTextureView.get());
    studio.subject->setScale(0.7f, 0.7f, 0.7f);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "procgen", "marching_cubes", bg);

    auto releaseParamsResult = eve::procgen::Procgen::release(paramsHandle);
    REQUIRE(releaseParamsResult.ok());

    Texture *sceneTex = capturePresented(_ZEROERR_TEST_CONTEXT, gfx);
    REQUIRE(sceneTex != nullptr);
    Canvas *dest = gfx->newCanvas(400, 300);
    REQUIRE(dest != nullptr);
    std::unique_ptr<AntiAliasing> aa(gfx->newAntiAliasing());
    aa->setMode("ssaa");
    aa->setQuality("medium");
    aa->applyTo(gfx, sceneTex, dest);
    blitAndPresent(gfx, dest->getTexture());
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "procgen", "ssaa_medium", bg);
    aa->setQuality("high");
    aa->applyTo(gfx, sceneTex, dest);
    blitAndPresent(gfx, dest->getTexture());
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "procgen", "ssaa_high", bg);
}

TEST_CASE("graphics.imageAudit.mapFovHex") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    auto                *map   = eve::map::Map::create();
    Texture             *tiles = makeAtlasGrid(gfx, 4, 8);
    eve::map::TileLayer *layer = map->newLayer(25, 19, 16.f, 16.f);
    REQUIRE(layer != nullptr);
    layer->setTileset(tiles, 1, 4);
    layer->setTilesetTileSize(8, 8);
    for (int y = 0; y < 19; ++y)
        for (int x = 0; x < 25; ++x) layer->setTile(x, y, 1 + (x + y) % 15);

    auto *cam = Camera2D::createCamera();
    cam->setPosition(200.f, 150.f);
    cam->setZoom(1.f);
    layer->setCamera(cam);

    auto presentMap = [&]() {
        gfx->setScreenReadbackEnabled(true);
        gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.11f, 1.f);
        gfx->clearScreen();
        map->render(gfx);
        gfx->present();
    };

    std::unique_ptr<eve::map::Fov> fov(map->newFov(layer));
    REQUIRE(fov.get() != nullptr);
    fov->setAlgorithm("shadowcast");
    fov->setRadiusMetric("chebyshev");
    for (int y = 0; y < 19; ++y) {
        if (y == 9) continue;
        fov->setOpaque(8, y, true);
        layer->setTile(8, y, 2);
    }
    fov->addRevealer(4, 9, 10);
    fov->compute();
    for (int y = 0; y < 19; ++y) {
        for (int x = 0; x < 25; ++x) {
            const bool vis = fov->isVisible(x, y);
            layer->setTile(x, y, vis ? (1 + (x + y) % 8) : (9 + (x * 3 + y) % 7));
        }
    }
    presentMap();
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "mapfov", "shadowcast", bg);

    fov->setAlgorithm("raycast");
    fov->compute();
    for (int y = 0; y < 19; ++y) {
        for (int x = 0; x < 25; ++x) {
            const bool vis = fov->isVisible(x, y);
            layer->setTile(x, y, vis ? (1 + (x + y) % 8) : (9 + (x * 3 + y) % 7));
        }
    }
    presentMap();
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "mapfov", "raycast", bg);

    std::vector<uint8_t> r8;
    REQUIRE(fov->fillMaskR8(r8));
    const int mw = fov->getWidth();
    const int mh = fov->getHeight();
    REQUIRE(mw == 25);
    REQUIRE(mh == 19);
    std::vector<uint8_t> rgba(size_t(mw * mh * 4));
    for (int i = 0; i < mw * mh; ++i) {
        const uint8_t v = r8[size_t(i)];
        const size_t  o = size_t(i) * 4u;
        rgba[o + 0]     = uint8_t(50 + int(v) * 180 / 255);
        rgba[o + 1]     = uint8_t(90 + int(v) * 110 / 255);
        rgba[o + 2]     = uint8_t(200 - int(v) * 70 / 255);
        rgba[o + 3]     = 255;
    }
    Texture *maskVis = gfx->newTexture(mw, mh, rgba.data());
    REQUIRE(maskVis != nullptr);
    REQUIRE(fov->buildMaskTexture(gfx) != nullptr);
    layer->setVisible(false);
    auto *maskSp = makeSprite(maskVis, 0.f, 0.f, 400.f, 300.f, false);
    presentMap();
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "mapfov", "mask_vis", bg);
    maskSp->sprite()->visible = false;

    auto *pg           = eve::procgen::Procgen::create();
    auto  paramsResult = eve::procgen::Procgen::newParamsHandle();
    REQUIRE(paramsResult.ok());
    auto paramsHandle = std::move(paramsResult).takeValue();
    auto paramsView   = eve::procgen::Procgen::resolve(paramsHandle);
    REQUIRE(paramsView.isBound());
    paramsView->setSeed(1);
    paramsView->setSize(25, 19);
    auto gridResult = pg->generateHandle("dungeon.bsp", paramsHandle);
    REQUIRE(gridResult.ok());
    auto gridHandle = std::move(gridResult).takeValue();
    auto gridView   = eve::procgen::Procgen::resolve(gridHandle);
    REQUIRE(gridView.isBound());
    pg->setPaletteGid("audit", "wall", 2);
    pg->setPaletteGid("audit", "floor", 5);
    pg->setPaletteGid("audit", "corridor", 6);
    layer->setVisible(true);
    auto applyResult = pg->applyToLayer(gridHandle, "audit", *layer);
    REQUIRE(applyResult.ok());
    for (int y = 0; y < 19; ++y)
        for (int x = 0; x < 25; ++x)
            if (layer->getTile(x, y) == 0) layer->setTile(x, y, 5);
    auto releaseGridResult = eve::procgen::Procgen::release(gridHandle);
    REQUIRE(releaseGridResult.ok());
    presentMap();
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "mapfov", "dungeon_bsp", bg);
    layer->setVisible(false);

    auto releaseParamsResult = eve::procgen::Procgen::release(paramsHandle);
    REQUIRE(releaseParamsResult.ok());

    eve::map::TileLayer *hex     = map->newLayer(18, 16, 32.f, 28.f);
    hex->config()->orientation   = eve::map::MapOrientation::Hexagonal;
    hex->config()->hexSideLength = 16.f;
    hex->config()->originX       = 16.f;
    hex->config()->originY       = 8.f;
    hex->setTileset(tiles, 1, 4);
    hex->setTilesetTileSize(8, 8);
    hex->setCamera(cam);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 18; ++x) hex->setTile(x, y, 1 + (x * 3 + y) % 15);
    makeSprite(makeSolid(gfx, 70, 90, 130), 0.f, 0.f, 400.f, 300.f, false)->sprite()->layer = -1;
    presentMap();
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "mapfov", "hexagonal", bg);
    hex->setVisible(false);

    eve::map::TileLayer *stag    = map->newLayer(18, 20, 32.f, 16.f);
    stag->config()->orientation  = eve::map::MapOrientation::Staggered;
    stag->config()->staggerAxis  = eve::map::StaggerAxis::Y;
    stag->config()->staggerIndex = eve::map::StaggerIndex::Odd;
    stag->config()->originX      = 0.f;
    stag->config()->originY      = 0.f;
    stag->setTileset(tiles, 1, 4);
    stag->setTilesetTileSize(8, 8);
    stag->setCamera(cam);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 18; ++x) stag->setTile(x, y, 1 + (x + y * 2) % 15);
    presentMap();
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "mapfov", "staggered", bg);
}

TEST_CASE("graphics.imageAudit.particleSkin") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto          studio = makeStudio3D(gfx);
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);

    auto model = eve::test::pinResource(loadCesiumMan("ev_ut_image_audit_skin_part"));
    REQUIRE(model.get() != nullptr);
    const int mi = findFirstSkinnedMesh(model.get());
    REQUIRE(mi >= 0);
    const aiMesh *src = model->getMesh(mi);
    REQUIRE(src != nullptr);
    Mesh *gpu = gfx->newMeshFromAssimp(*src);
    REQUIRE(gpu != nullptr);
    studio.subject->setMesh(gpu);
    studio.subject->setTexture(makeSolid(gfx, 200, 170, 120));
    studio.subject->setScale(1.15f, 1.15f, 1.15f);
    studio.subject->setPosition(0.f, 0.f, 0.f);
    studio.subject->setYaw(1.2f);
    studio.cam->setEye(0.f, 1.3f, 3.2f);
    studio.cam->setTarget(0.f, 0.7f, 0.f);
    studio.ground->setPosition(0.f, 0.f, 0.f);

    using eve::animation::AnimClip;
    using eve::animation::AnimImporter;
    using eve::animation::AnimPose;
    using eve::animation::AnimSkeleton;
    using eve::animation::AnimSkin;
    std::unique_ptr<AnimSkeleton> sk(AnimImporter::loadSkeletonFromModel(model.get()));
    REQUIRE(sk.get() != nullptr);
    std::unique_ptr<AnimClip> clip(AnimImporter::loadClipFromModel(model.get(), sk.get(), 0));
    REQUIRE(clip.get() != nullptr);
    std::unique_ptr<AnimSkin> skin(AnimSkin::fromModel(model.get(), mi, sk.get()));
    REQUIRE(skin.get() != nullptr);
    std::unique_ptr<AnimPose> pose(new AnimPose());
    clip->sample(0.f, pose.get(), sk.get());
    pose->computeWorld(sk.get());
    REQUIRE(skin->updateSkinnedPositions(pose.get()));

    auto                            *parts = eve::particles::Particles::create();
    eve::particles::ParticleEmitter *em    = parts->newEmitter(128);
    em->setParticleLifetime(8.f, 8.f);
    em->setSpeed(0.f, 0.f);
    em->setParticleSize(28.f, 28.f);
    em->setColorStart(1.f, 0.45f, 0.15f, 1.f);
    em->setColorEnd(1.f, 0.45f, 0.15f, 1.f);
    em->setTexture(makeSolid(gfx, 230, 120, 40));
    em->setEmissionArea("none", 0.f, 0.f);
    em->setSkinScale(90.f);
    em->setSkinPlane("xy");
    em->setSkinSource(skin.get(), pose.get());
    REQUIRE(em->hasSkinSource());
    em->emitFromSkin(64);
    REQUIRE(em->getCount() >= 16);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "partskin", "emit_from_skin", bg, false,
                  [&]() { eve::particles::ParticleRenderSystem::render(gfx); });

    em->clearSkinSource();
    std::unique_ptr<AnimSkeleton> attachSk(new AnimSkeleton());
    const int                     root = attachSk->addBone("root", -1);
    std::unique_ptr<AnimPose>     bonePose(new AnimPose());
    attachSk->applyBindPose(bonePose.get());
    bonePose->setLocalPosition(root, 200.f, 160.f, 0.f);
    bonePose->computeWorld(attachSk.get());
    em->setParticleSize(40.f, 40.f);
    em->attachToBone(bonePose.get(), root);
    em->syncAttach();
    em->emit(24);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "partskin", "attach_bone", bg, false,
                  [&]() { eve::particles::ParticleRenderSystem::render(gfx); });
}

TEST_CASE("graphics.imageAudit.materialHair") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto          studio      = makeStudio3D(gfx);
    auto         *auditBanner = makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);

    studio.subject->setReceiveLight(false);
    studio.subject->setTexture(makeSolid(gfx, 220, 90, 70));
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "mathair", "receive_off", bg);
    studio.subject->setReceiveLight(true);

    Material *hairMat = gfx->newMaterial();
    hairMat->setShadingModel("hair");
    hairMat->setAlbedoTexture(makeSolid(gfx, 210, 160, 70));
    Shader *hairSh = gfx->newHairShader();
    REQUIRE(hairSh != nullptr);
    hairMat->setShader(hairSh);
    auto *card = Renderable3D::create();
    card->setMesh(makeHairCard(gfx));
    card->setMaterial(hairMat);
    card->setPosition(0.f, 0.05f, 0.35f);
    card->setCastShadow(false);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "mathair", "hair_material", bg);

    // General-purpose transparent PBR materials must use the transparent
    // forward queue without pretending to be hair. Isolate this frame from the
    // preceding hair audit, then place one overlapping card behind the opaque
    // sphere and one in front to make depth testing and compositing unambiguous.
    card->setPosition(100.f, 100.f, 100.f);
    auditBanner->sprite()->visible = false;
    studio.subject->setTexture(makeSolid(gfx, 205, 210, 220));
    Material *glassBack = gfx->newMaterial();
    glassBack->setSurfaceMode("transparent");
    glassBack->setTint(0.08f, 0.35f, 1.f, 0.48f);
    glassBack->setSortPriority(0);
    auto *backCard = Renderable3D::create();
    backCard->setMesh(makeHairCard(gfx));
    backCard->setMaterial(glassBack);
    backCard->setPosition(-0.22f, -0.05f, -0.3f);
    backCard->setCastShadow(false);

    Material *glassFront = gfx->newMaterial();
    glassFront->setSurfaceMode("transparent");
    glassFront->setTint(1.f, 0.12f, 0.04f, 0.42f);
    glassFront->setSortPriority(0);
    auto *frontCard = Renderable3D::create();
    frontCard->setMesh(makeHairCard(gfx));
    frontCard->setMaterial(glassFront);
    frontCard->setPosition(0.22f, -0.05f, 0.65f);
    frontCard->setCastShadow(false);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "mathair", "transparent_pbr_overlap", bg, false, {},
                  "mathair_transparent_pbr_overlap.png");

    studio.subject->setVisible(false);
    makeSprite(makeSolid(gfx, 90, 140, 200), 0.f, 0.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 200, 90, 70), 200.f, 0.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 70, 180, 110), 0.f, 150.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 220, 200, 60), 200.f, 150.f, 200.f, 150.f, false);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "mathair", "hair_only_packed", bg);
}

TEST_CASE("graphics.imageAudit.clusteredHair") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto          studio = makeStudio3D(gfx);
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);
    studio.sun->setEnabled(false);
    studio.cam->setAmbient(0.08f, 0.08f, 0.09f);

    RenderControl *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);
    rc->enable("clustered");
    rc->enable("hair");
    rc->enable("forward");
    rc->compile();

    const float cols[12][3] = {
        {1.f, 0.25f, 0.2f}, {0.2f, 1.f, 0.3f},   {0.25f, 0.4f, 1.f},  {1.f, 0.9f, 0.2f},
        {1.f, 0.4f, 0.9f},  {0.2f, 0.95f, 0.9f}, {1.f, 0.55f, 0.15f}, {0.6f, 0.3f, 1.f},
        {0.4f, 1.f, 0.55f}, {1.f, 0.2f, 0.45f},  {0.55f, 0.75f, 1.f}, {0.95f, 0.95f, 0.7f},
    };
    for (int i = 0; i < 12; ++i) {
        auto       *l = Light3D::createLight("point");
        const float a = float(i) / 12.f * 6.2831853f;
        l->setPosition(std::cos(a) * 1.6f, 0.5f + 0.2f * std::sin(a * 2.f), std::sin(a) * 1.6f);
        l->setColor(cols[i][0], cols[i][1], cols[i][2], 3.2f);
        l->setRadius(3.5f);
        l->setEnabled(true);
    }
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "cluster", "twelve_points", bg);

    Shader *hairSh = gfx->newHairShader();
    REQUIRE(hairSh != nullptr);
    auto *card = Renderable3D::create();
    card->setMesh(makeHairCard(gfx));
    card->setTexture(makeSolid(gfx, 210, 160, 70));
    card->setShader(hairSh);
    card->setHair(true);
    card->setPosition(0.f, 0.05f, 0.4f);
    card->setCastShadow(false);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "cluster", "hair_plus_clustered", bg);

    rc->disable("clustered");
    rc->compile();
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "cluster", "packed_fallback", bg);
    rc->enable("clustered");
    rc->compile();
}

TEST_CASE("graphics.imageAudit.clothFluid2D") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    makeSprite(makeSolid(gfx, 90, 140, 200), 0.f, 0.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 200, 90, 70), 200.f, 0.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 70, 180, 110), 0.f, 150.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 220, 200, 60), 200.f, 150.f, 200.f, 150.f, false);

    auto presentExtra = [&](const std::function<void()> &extra) {
        gfx->setScreenReadbackEnabled(true);
        gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.11f, 1.f);
        gfx->clearScreen();
        std::vector<DrawItem2D> items;
        RenderSystem::collectSprites(items);
        RenderSystem::drawItems(*gfx, items, false);
        extra();
        gfx->present();
    };

    eve::physics::Cloth cloth(20, 16, 20.f, 8.f, 8.f);
    cloth.pinTopRow();
    cloth.setGravity(0.f, 0.f);
    cloth.setColor(0.95f, 0.35f, 0.2f, 1.f);
    cloth.update(0.016f);
    presentExtra([&]() { cloth.draw(gfx); });
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "phys2d", "cloth", bg);

    cloth.setGravity(0.f, 400.f);
    cloth.applyForce(80.f, 0.f);
    for (int i = 0; i < 8; ++i) cloth.update(0.016f);
    presentExtra([&]() { cloth.draw(gfx); });
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "phys2d", "cloth_wind", bg);

    eve::physics::Fluid2D fluid(512);
    fluid.setGravity(0.f, 0.f);
    fluid.setParticleSize(18.f);
    fluid.setColor(0.25f, 0.6f, 0.95f, 1.f);
    fluid.setBounds(0.f, 0.f, 400.f, 300.f);
    for (int y = 30; y < 280; y += 16)
        for (int x = 30; x < 380; x += 16) fluid.emit(float(x), float(y), 1);
    REQUIRE(fluid.getParticleCount() > 100);
    presentExtra([&]() { fluid.draw(gfx); });
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "phys2d", "fluid", bg);
}

TEST_CASE("graphics.imageAudit.particlePresets") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto          studio = makeStudio3D(gfx);
    (void)studio;
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);

    auto                            *parts = eve::particles::Particles::create();
    eve::particles::ParticleEmitter *em    = parts->newEmitter(128);
    em->setTexture(makeSolid(gfx, 230, 160, 50));
    em->setPosition(200.f, 160.f);

    auto burst = [&](const char *preset, const char *phase, float r, float g, float b) {
        em->reset();
        em->applyPreset(preset);
        em->setParticleLifetime(8.f, 8.f);
        em->setSpeed(0.f, 0.f);
        em->setParticleSize(32.f, 32.f);
        em->setColorStart(r, g, b, 1.f);
        em->setColorEnd(r, g, b, 1.f);
        em->setEmissionArea("rect", 140.f, 80.f);
        em->emit(48);
        REQUIRE(em->getCount() >= 16);
        auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "presets", phase, bg, false,
                      [&]() { eve::particles::ParticleRenderSystem::render(gfx); });
    };
    burst("spark", "spark", 1.f, 0.85f, 0.25f);
    burst("smoke", "smoke", 0.75f, 0.78f, 0.82f);
    burst("fire", "fire", 1.f, 0.4f, 0.12f);

    em->reset();
    REQUIRE(em->applyConfig("{\"preset\":\"fire\",\"x\":200,\"y\":160,\"particleSize\":[36,36]}"));
    em->setParticleLifetime(8.f, 8.f);
    em->setSpeed(0.f, 0.f);
    em->setColorStart(1.f, 0.45f, 0.15f, 1.f);
    em->setColorEnd(1.f, 0.45f, 0.15f, 1.f);
    em->setEmissionArea("rect", 140.f, 80.f);
    em->emit(40);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "presets", "json_fire", bg, false,
                  [&]() { eve::particles::ParticleRenderSystem::render(gfx); });
}

TEST_CASE("graphics.imageAudit.mapPath") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    auto                *map   = eve::map::Map::create();
    Texture             *tiles = makeAtlasGrid(gfx, 4, 8);
    eve::map::TileLayer *layer = map->newLayer(25, 19, 16.f, 16.f);
    REQUIRE(layer != nullptr);
    layer->setTileset(tiles, 1, 4);
    layer->setTilesetTileSize(8, 8);
    for (int y = 0; y < 19; ++y)
        for (int x = 0; x < 25; ++x) layer->setTile(x, y, 5);

    auto *cam = Camera2D::createCamera();
    cam->setPosition(200.f, 150.f);
    cam->setZoom(1.f);
    layer->setCamera(cam);

    auto presentMap = [&]() {
        gfx->setScreenReadbackEnabled(true);
        gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.11f, 1.f);
        gfx->clearScreen();
        map->render(gfx);
        gfx->present();
    };

    std::unique_ptr<eve::map::Pathfinder> pf(map->newPathfinder(layer));
    REQUIRE(pf.get() != nullptr);
    pf->setDiagonal(true);
    for (int y = 0; y < 19; ++y) {
        if (y == 9) continue;
        pf->setBlocked(12, y, true);
        layer->setTile(12, y, 2);
    }
    std::unique_ptr<eve::map::Path> path(pf->findPath(1, 9, 23, 9));
    REQUIRE(path.get() != nullptr);
    REQUIRE(path->getLength() > 4);
    for (int i = 0; i < path->getLength(); ++i) layer->setTile(path->getX(i), path->getY(i), 4);
    layer->setTile(1, 9, 8);
    layer->setTile(23, 9, 12);
    presentMap();
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "mappath", "astar", bg);

    std::unique_ptr<eve::map::FlowField> field(pf->buildFlowField(23, 9));
    REQUIRE(field.get() != nullptr);
    for (int y = 0; y < 19; ++y) {
        for (int x = 0; x < 25; ++x) {
            if (layer->getTile(x, y) == 2) continue;
            layer->setTile(x, y, field->isReachable(x, y) ? (6 + (x + y) % 3) : 10);
        }
    }
    presentMap();
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "mappath", "flow_field", bg);

    std::unique_ptr<eve::map::Path> group(pf->findGroupPath(2, 2, 23, 9));
    REQUIRE(group.get() != nullptr);
    for (int y = 0; y < 19; ++y)
        for (int x = 0; x < 25; ++x)
            if (layer->getTile(x, y) != 2) layer->setTile(x, y, 5);
    for (int i = 0; i < group->getLength(); ++i) layer->setTile(group->getX(i), group->getY(i), 14);
    presentMap();
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "mappath", "group_flow", bg);
}

TEST_CASE("graphics.imageAudit.reloadTexBytes") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto          studio = makeStudio3D(gfx);
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);

    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_image_audit_texbytes", true));
    REQUIRE(fs->setupWriteDirectory());
    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();

    auto writeSolidPng = [&](uint8_t r, uint8_t g, uint8_t b) {
        eve::image::ImageData img(32, 32, "RGBA8");
        auto                 *px = static_cast<uint8_t *>(img.getData());
        for (int i = 0; i < 32 * 32; ++i) {
            px[i * 4 + 0] = r;
            px[i * 4 + 1] = g;
            px[i * 4 + 2] = b;
            px[i * 4 + 3] = 255;
        }
        std::unique_ptr<eve::filesystem::FileData> encoded(
            img.encode(medialoader::FormatHandler::ENCODED_PNG, "audit_reload_swap.png", true));
        REQUIRE(encoded.get() != nullptr);
    };

    writeSolidPng(220, 60, 50);
    Texture *tex = gfx->newTextureFromFile("audit_reload_swap.png");
    REQUIRE(tex != nullptr);
    studio.subject->setTexture(tex);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "texbytes", "red", bg);

    writeSolidPng(50, 200, 80);
    REQUIRE(gfx->reloadTextureFromFile("audit_reload_swap.png"));
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "texbytes", "green", bg);

    writeSolidPng(60, 90, 220);
    REQUIRE(gfx->reloadTextureFromFile("audit_reload_swap.png"));
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "texbytes", "blue", bg);
}

TEST_CASE("graphics.imageAudit.postQuality") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto          studio = makeStudio3D(gfx);
    (void)studio;
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);
    const int w = 400, h = 300;

    RenderControl *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);
    rc->enable("gbuffer");
    rc->enable("gbufferAlbedo");
    rc->compile();

    Texture *sceneTex = capturePresented(_ZEROERR_TEST_CONTEXT, gfx);
    REQUIRE(sceneTex != nullptr);
    GBuffer *gb = rc->getGBuffer();
    REQUIRE(gb != nullptr);
    Texture *depth = gb->getDepthTexture();
    REQUIRE(depth != nullptr);

    Canvas *composed = gfx->newCanvas(w, h);
    REQUIRE(composed != nullptr);
    std::unique_ptr<AmbientOcclusion> ao(gfx->newAmbientOcclusion());
    ao->setCamera(0.f, 1.5f, 4.0f, 0.f, 0.15f, 0.f, 0.f, 1.f, 0.f, 60.f, float(w) / float(h), 0.05f, 40.f);
    ao->setMode("ssao");
    const char *qualities[] = {"low", "medium", "high"};
    for (const char *q : qualities) {
        ao->setQuality(q);
        Canvas *aoMap = gfx->newCanvas(ao->resolutionFor(w), ao->resolutionFor(h));
        REQUIRE(aoMap != nullptr);
        ao->computeTo(gfx, depth, aoMap);
        gfx->setCanvas(composed);
        gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.11f, 1.f);
        gfx->clearScreen();
        gfx->drawTexturedRectRGBA(sceneTex, 0, 0, float(w), float(h), 1, 1, 1, 1);
        ao->applyOverlay(gfx, aoMap->getTexture());
        gfx->setCanvas();
        blitAndPresent(gfx, composed->getTexture());
        auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "postq", (std::string("ssao_") + q).c_str(), bg);
    }

    std::unique_ptr<Volumetric> vol(gfx->newVolumetric());
    vol->setCamera(0.f, 1.5f, 4.0f, 0.f, 0.15f, 0.f, 0.f, 1.f, 0.f, 60.f, float(w) / float(h), 0.05f, 40.f);
    vol->setFogColor(0.45f, 0.5f, 0.55f);
    vol->setFogHeight(-0.7f);
    vol->setFogHeightFalloff(0.8f);
    vol->setFogStart(1.2f);
    vol->setFogEnd(9.f);
    vol->setDensity(0.35f);
    vol->setMode("fog");
    for (const char *q : qualities) {
        vol->setQuality(q);
        gfx->setCanvas(composed);
        gfx->clearScreen();
        gfx->drawTexturedRectRGBA(sceneTex, 0, 0, float(w), float(h), 1, 1, 1, 1);
        vol->applyFog(gfx, depth);
        gfx->setCanvas();
        blitAndPresent(gfx, composed->getTexture());
        auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "postq", (std::string("fog_") + q).c_str(), bg);
    }

    vol->setMode("raymarch");
    vol->setLightDirection(0.45f, 1.f, 0.35f);
    vol->setLightScreenUV(0.62f, 0.28f);
    vol->setIntensity(0.8f);
    vol->setQuality("high");
    gfx->setCanvas(composed);
    gfx->clearScreen();
    gfx->drawTexturedRectRGBA(sceneTex, 0, 0, float(w), float(h), 1, 1, 1, 1);
    vol->rayMarch(gfx, depth);
    gfx->setCanvas();
    blitAndPresent(gfx, composed->getTexture());
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "postq", "raymarch_high", bg);

    rc->disable("gbuffer");
    rc->compile();
}

TEST_CASE("graphics.imageAudit.dualIsoFov") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    auto    *map   = eve::map::Map::create();
    Texture *tiles = makeAtlasGrid(gfx, 4, 8);
    auto    *cam   = Camera2D::createCamera();
    cam->setPosition(200.f, 150.f);
    cam->setZoom(1.f);

    auto presentMap = [&]() {
        gfx->setScreenReadbackEnabled(true);
        gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.11f, 1.f);
        gfx->clearScreen();
        map->render(gfx);
        gfx->present();
    };

    eve::map::TileLayer *ortho = map->newLayer(25, 19, 16.f, 16.f);
    ortho->setTileset(tiles, 1, 4);
    ortho->setTilesetTileSize(8, 8);
    ortho->setCamera(cam);
    for (int y = 0; y < 19; ++y)
        for (int x = 0; x < 25; ++x) ortho->setTile(x, y, 1 + (x + y) % 15);

    std::unique_ptr<eve::map::Fov> fov(map->newFov(ortho));
    REQUIRE(fov.get() != nullptr);
    fov->setRadiusMetric("chebyshev");
    for (int y = 0; y < 19; ++y) {
        if (y == 9) continue;
        fov->setOpaque(8, y, true);
        ortho->setTile(8, y, 2);
    }
    fov->addRevealer(4, 9, 10);

    auto paintFov = [&]() {
        for (int y = 0; y < 19; ++y) {
            for (int x = 0; x < 25; ++x) {
                const bool vis = fov->isVisible(x, y);
                ortho->setTile(x, y, vis ? (1 + (x + y) % 8) : (9 + (x * 3 + y) % 7));
            }
        }
    };
    fov->setAlgorithm("permissive");
    fov->compute();
    paintFov();
    presentMap();
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "dualiso", "fov_permissive", bg);

    fov->setAlgorithm("rectangle");
    fov->compute();
    paintFov();
    presentMap();
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "dualiso", "fov_rectangle", bg);
    ortho->setVisible(false);

    eve::map::TileLayer *logic   = map->newLayer(16, 16, 32.f, 16.f);
    eve::map::TileLayer *display = map->newLayer(1, 1, 32.f, 16.f);
    logic->config()->orientation = eve::map::MapOrientation::Isometric;
    logic->config()->originX     = 200.f;
    logic->config()->originY     = 16.f;
    logic->setTileset(tiles, 1, 4);
    display->setTileset(tiles, 1, 4);
    display->setTilesetTileSize(8, 8);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) logic->setTile(x, y, ((x + y) % 3 == 0) ? 1 : 2);
    eve::map::DualGridOptions opts;
    opts.filledGid = 1;
    opts.hideLogic = true;
    REQUIRE(eve::map::resolveDualGrid(logic, display, opts, nullptr));
    display->setCamera(cam);
    makeSprite(makeSolid(gfx, 70, 90, 130), 0.f, 0.f, 400.f, 300.f, false)->sprite()->layer = -1;
    presentMap();
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "dualiso", "dual_isometric", bg);
}

TEST_CASE("graphics.imageAudit.avatarImage") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    auto *mod = eve::avatar::Avatar::create();
    REQUIRE(mod != nullptr);
    eve::avatar::AvatarInstance *av = mod->newImageAvatar();
    REQUIRE(av != nullptr);
    av->setPosition(0.f, 0.f);
    REQUIRE(av->addLayer("tl", makeSolid(gfx, 90, 140, 200), 0));
    REQUIRE(av->addLayer("tr", makeSolid(gfx, 200, 90, 70), 1));
    REQUIRE(av->addLayer("bl", makeSolid(gfx, 70, 180, 110), 2));
    REQUIRE(av->addLayer("br", makeSolid(gfx, 220, 200, 60), 3));
    av->setLayerSize("tl", 200.f, 150.f);
    av->setLayerSize("tr", 200.f, 150.f);
    av->setLayerSize("bl", 200.f, 150.f);
    av->setLayerSize("br", 200.f, 150.f);
    av->setLayerOffset("tr", 200.f, 0.f);
    av->setLayerOffset("bl", 0.f, 150.f);
    av->setLayerOffset("br", 200.f, 150.f);
    av->sync();
    auditGpu2D(_ZEROERR_TEST_CONTEXT, gfx, "avatar", "layers_packed", bg);

    av->setLayerColor("tl", 0.95f, 0.35f, 0.25f, 1.f);
    av->setLayerColor("tr", 0.25f, 0.55f, 0.95f, 1.f);
    av->sync();
    auditGpu2D(_ZEROERR_TEST_CONTEXT, gfx, "avatar", "recolor", bg);

    REQUIRE(av->defineExpression("flash", "tl=1;tr=1;bl=1;br=1"));
    REQUIRE(av->applyExpression("flash"));
    av->setLayerColor("bl", 0.95f, 0.85f, 0.2f, 1.f);
    av->setLayerColor("br", 0.7f, 0.3f, 0.9f, 1.f);
    av->sync();
    auditGpu2D(_ZEROERR_TEST_CONTEXT, gfx, "avatar", "expression", bg);
}

TEST_CASE("graphics.imageAudit.avatarVroid") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto          studio = makeStudio3D(gfx);
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);
    studio.subject->setVisible(false);

    auto model = eve::test::pinResource(loadCesiumMan("ev_ut_image_audit_vroid"));
    REQUIRE(model.get() != nullptr);
    const int mi = findFirstSkinnedMesh(model.get());
    REQUIRE(mi >= 0);
    const aiMesh *src = model->getMesh(mi);
    REQUIRE(src != nullptr);
    Mesh *gpu = gfx->newMeshFromAssimp(*src);
    REQUIRE(gpu != nullptr);

    auto                        *mod = eve::avatar::Avatar::create();
    eve::avatar::AvatarInstance *av  = mod->newVroidAvatar();
    REQUIRE(av != nullptr);
    REQUIRE(av->bindVroidModelData(model.get()));
    av->setMesh(gpu);
    av->setTexture(makeSolid(gfx, 200, 170, 120));
    av->setPosition3D(0.f, 0.f, 0.f);
    av->setRotation3D(1.2f, 0.f, 0.f);
    av->setScale3D(1.15f, 1.15f, 1.15f);
    studio.cam->setEye(0.f, 1.3f, 3.2f);
    studio.cam->setTarget(0.f, 0.7f, 0.f);
    studio.ground->setPosition(0.f, 0.f, 0.f);
    av->sync();
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "vroid", "bind_pose", bg);
    av->setRotation3D(0.4f, 0.15f, 0.f);
    av->sync();
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "vroid", "yaw_pitch", bg);
}

TEST_CASE("graphics.imageAudit.box2dDebug") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    makeSprite(makeSolid(gfx, 90, 140, 200), 0.f, 0.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 200, 90, 70), 200.f, 0.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 70, 180, 110), 0.f, 150.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 220, 200, 60), 200.f, 150.f, 200.f, 150.f, false);

    auto                                *phys = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World> world(phys->newWorld(0.f, 0.f, true));
    REQUIRE(world.get() != nullptr);
    eve::physics::Body *ground = world->newBody("static", 200.f, 260.f);
    ground->newRectangleFixture(360.f, 40.f, 0.f, 0.4f, 0.f);
    eve::physics::Body *wallL = world->newBody("static", 30.f, 150.f);
    wallL->newRectangleFixture(40.f, 220.f, 0.f, 0.4f, 0.f);
    eve::physics::Body *wallR = world->newBody("static", 370.f, 150.f);
    wallR->newRectangleFixture(40.f, 220.f, 0.f, 0.4f, 0.f);
    eve::physics::Body *box = world->newBody("dynamic", 200.f, 80.f);
    box->newRectangleFixture(70.f, 70.f, 1.f, 0.3f, 0.1f);
    eve::physics::Body *ball = world->newBody("dynamic", 120.f, 60.f);
    ball->newCircleFixture(28.f, 1.f, 0.2f, 0.4f);
    world->setGravity(0.f, 600.f);
    for (int i = 0; i < 20; ++i) world->update(1.f / 60.f);

    auto presentDebug = [&]() {
        gfx->setScreenReadbackEnabled(true);
        gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.11f, 1.f);
        gfx->clearScreen();
        std::vector<DrawItem2D> items;
        RenderSystem::collectSprites(items);
        RenderSystem::drawItems(*gfx, items, false);
        world->drawDebug(gfx);
        gfx->present();
    };
    presentDebug();
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "box2d", "debug_draw", bg);
}

TEST_CASE("graphics.imageAudit.imageRotate") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();
    ImageData                    src(64, 64, "RGBA8");
    auto                        *px = static_cast<uint8_t *>(src.getData());
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const bool   on = ((x / 8) + (y / 8)) & 1;
            const size_t i  = size_t((y * 64 + x) * 4);
            px[i + 0]       = on ? 220 : 50;
            px[i + 1]       = on ? 90 : 180;
            px[i + 2]       = on ? 70 : 210;
            px[i + 3]       = 255;
        }
    }

    std::unique_ptr<ImageData> rot90(src.rotate(1.5707963f, "nearest", true));
    REQUIRE(rot90.get() != nullptr);
    Texture *t90  = gfx->newTexture(rot90.get());
    auto    *full = makeSprite(t90, 0.f, 0.f, 400.f, 300.f, false);
    auditGpu2D(_ZEROERR_TEST_CONTEXT, gfx, "imrotate", "nearest_90", bg);

    std::unique_ptr<ImageData> rotSprite(src.rotate(1.5707963f, "rotsprite", true));
    REQUIRE(rotSprite.get() != nullptr);
    full->sprite()->texture = gfx->newTexture(rotSprite.get());
    auditGpu2D(_ZEROERR_TEST_CONTEXT, gfx, "imrotate", "rotsprite_90", bg);

    makeSprite(makeSolid(gfx, 90, 140, 200), 0.f, 0.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 200, 90, 70), 200.f, 0.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 70, 180, 110), 0.f, 150.f, 200.f, 150.f, false);
    makeSprite(makeSolid(gfx, 220, 200, 60), 200.f, 150.f, 200.f, 150.f, false);
    full->sprite()->visible = false;
    std::unique_ptr<ImageData> rot45(src.rotate(0.78539816f, "linear", true));
    REQUIRE(rot45.get() != nullptr);
    makeSprite(gfx->newTexture(rot45.get()), 80.f, 50.f, 240.f, 200.f, false);
    auditGpu2D(_ZEROERR_TEST_CONTEXT, gfx, "imrotate", "linear_45", bg);

    auto *cam = Camera2D::createCamera();
    cam->setPosition(200.f, 150.f);
    cam->setZoom(1.f);
    Texture *bigTex       = gfx->newTexture(&src);
    auto    *big          = makeSprite(bigTex, 0.f, 0.f, 800.f, 600.f, false);
    big->sprite()->camera = cam;
    big->sprite()->layer  = 50;
    auditGpu2D(_ZEROERR_TEST_CONTEXT, gfx, "imrotate", "cam_pan_tl", bg);
    cam->setPosition(400.f, 250.f);
    auditGpu2D(_ZEROERR_TEST_CONTEXT, gfx, "imrotate", "cam_pan_mid", bg);
}

TEST_CASE("graphics.imageAudit.sceneGraph") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    auto          studio = makeStudio3D(gfx);
    makeSprite(makeSolid(gfx, 180, 140, 70), 0.f, 0.f, 400.f, 84.f, false);

    auto hostResult = eve::scene::SceneHost::createHost("audit3d");
    REQUIRE(hostResult.ok());
    eve::scene::SceneHost *host = hostResult.value();
    REQUIRE(host != nullptr);
    host->setTree(
        eve::scene::node("root", {eve::scene::node("subject").withPosition(0.f, 0.15f, 0.f).withScale(0.55f)}));
    REQUIRE(host->linkRenderable3D("subject", studio.subject));
    eve::scene::TransformSystem::updateHost(host);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "scene", "linked_rest", bg);

    host->setTreeReconcile(eve::scene::node(
        "root",
        {eve::scene::node("subject").withPosition(0.7f, 0.2f, 0.f).withRotation(0.8f, 0.15f, 0.f).withScale(0.6f)}));
    eve::scene::TransformSystem::updateHost(host);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "scene", "linked_moved", bg);

    auto *left = Renderable3D::create();
    left->setMesh(studio.sphere);
    left->setTexture(makeSolid(gfx, 210, 90, 80));
    left->setCastShadow(true);
    host->setTree(eve::scene::node("root", {eve::scene::node("subject").withPosition(0.5f, 0.15f, 0.f).withScale(0.45f),
                                            eve::scene::node("left").withPosition(-1.0f, 0.2f, 0.f).withScale(0.4f)}));
    REQUIRE(host->linkRenderable3D("subject", studio.subject));
    REQUIRE(host->linkRenderable3D("left", left));
    eve::scene::TransformSystem::updateHost(host);
    auditGpuFrame(_ZEROERR_TEST_CONTEXT, gfx, "scene", "two_nodes", bg);
}

TEST_CASE("graphics.imageAudit.fovHeightHex") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 400;
    s.height   = 300;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    CloseWin closer{win};
    resetScene3D();

    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    gfx->setBackgroundColor(Color(0.08f, 0.09f, 0.11f, 1.f));
    auto    *map   = eve::map::Map::create();
    Texture *tiles = makeAtlasGrid(gfx, 4, 8);
    auto    *cam   = Camera2D::createCamera();
    cam->setPosition(200.f, 150.f);
    cam->setZoom(1.f);

    auto presentMap = [&]() {
        gfx->setScreenReadbackEnabled(true);
        gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.11f, 1.f);
        gfx->clearScreen();
        map->render(gfx);
        gfx->present();
    };

    eve::map::TileLayer *ortho = map->newLayer(25, 19, 16.f, 16.f);
    ortho->setTileset(tiles, 1, 4);
    ortho->setTilesetTileSize(8, 8);
    ortho->setCamera(cam);
    for (int y = 0; y < 19; ++y)
        for (int x = 0; x < 25; ++x) ortho->setTile(x, y, 1 + (x + y) % 15);

    std::unique_ptr<eve::map::Fov> fov(map->newFov(ortho));
    REQUIRE(fov.get() != nullptr);
    fov->setMode("heightmap");
    fov->setRadiusMetric("chebyshev");
    fov->setCliffBlock(1.f);
    fov->setEyeOffset(0.f);
    for (int y = 0; y < 19; ++y) fov->setElevation(10, y, 2.f);
    fov->addRevealer(3, 9, 12);
    fov->compute();
    for (int y = 0; y < 19; ++y) {
        for (int x = 0; x < 25; ++x) {
            const bool vis = fov->isVisible(x, y);
            ortho->setTile(x, y, vis ? (1 + (x + y) % 8) : (9 + (x * 3 + y) % 7));
        }
    }
    presentMap();
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "fovmore", "heightmap", bg);
    ortho->setVisible(false);

    eve::map::TileLayer *hex     = map->newLayer(18, 16, 32.f, 28.f);
    hex->config()->orientation   = eve::map::MapOrientation::Hexagonal;
    hex->config()->hexSideLength = 16.f;
    hex->config()->originX       = 16.f;
    hex->config()->originY       = 8.f;
    hex->setTileset(tiles, 1, 4);
    hex->setTilesetTileSize(8, 8);
    hex->setCamera(cam);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 18; ++x) hex->setTile(x, y, 1 + (x * 3 + y) % 15);
    fov->bindLayer(hex);
    fov->setMode("grid2d");
    fov->setTopology("hex");
    fov->setAlgorithm("shadowcast");
    fov->clearRevealers();
    fov->addRevealer(4, 6, 8);
    fov->compute();
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 18; ++x) {
            const bool vis = fov->isVisible(x, y);
            hex->setTile(x, y, vis ? (1 + (x + y) % 8) : (9 + (x * 3 + y) % 7));
        }
    }
    makeSprite(makeSolid(gfx, 70, 90, 130), 0.f, 0.f, 400.f, 300.f, false)->sprite()->layer = -1;
    presentMap();
    auditSwapchain(_ZEROERR_TEST_CONTEXT, gfx, "fovmore", "hex_topology", bg);
}
}  // namespace test_RenderImageAudit

// Contract section: RenderSceneEffects
#include "PrivateTestScope.h"

#include <assimp/matrix4x4.h>
#include <assimp/scene.h>
#include "Fixtures.h"
#include "SceneColor.h"
#include "graphics/Shadow.h"
#include "medialoader/model/ModelLoader.h"
// RenderSceneEffects.cpp
namespace test_RenderSceneEffects {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

namespace {

/** Assimp row-major → glm column-major for Graphics::newMeshFromAssimp. */
glm::mat4 aiToGlm(const aiMatrix4x4& m) {
    return glm::mat4(m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2, m.a3, m.b3, m.c3, m.d3, m.a4, m.b4, m.c4, m.d4);
}

[[maybe_unused]] float luma(const Color &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }


[[maybe_unused]] void resetScene3D() {
    if (ecs::current()->getManager<Renderable3D>() != nullptr) {
        auto view = ecs::View<Renderable3D, Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [mr]   = *it;
            mr->visible = false;
        }
    }
    if (ecs::current()->getManager<Camera3D>() != nullptr) {
        auto camView = ecs::View<Camera3D, Camera3D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data]        = *it;
            data->active       = false;
            data->envMap       = nullptr;
            data->envIntensity = 0.f;
        }
    }
    if (ecs::current()->getManager<Light3D>() != nullptr) {
        auto lightView = ecs::View<Light3D, Light3D::Data>();
        for (auto it = lightView.begin(); it != lightView.end(); ++it) {
            auto [d]   = *it;
            d->enabled = false;
        }
    }
    if (ecs::current()->getManager<Renderable2D>() != nullptr) {
        auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [sp]   = *it;
            sp->visible = false;
        }
    }
}

[[maybe_unused]] Texture *makeSolid(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

[[maybe_unused]] void addHud() {
    auto *hud             = Renderable2D::create();
    hud->transform()->x   = 0;
    hud->transform()->y   = 0;
    hud->sprite()->width  = 2;
    hud->sprite()->height = 2;
}

[[maybe_unused]] void warmPresent(Graphics *gfx, int frames = 2) {
    for (int i = 0; i < frames; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
}

[[maybe_unused]] float meanLumaRegion(Graphics *gfx, int x0, int y0, int x1, int y1, int step = 4,
                                      bool sceneLinear = false) {
    double sum = 0.0;
    int    n   = 0;
    for (int y = y0; y < y1; y += step) {
        for (int x = x0; x < x1; x += step) {
            const auto pixel = gfx->getPixel(x, y);
            sum += luma(sceneLinear ? testSceneLinearColor(pixel) : pixel);
            ++n;
        }
    }
    return n ? float(sum / n) : 0.f;
}

[[maybe_unused]] float meanGreenRegion(Graphics *gfx, int x0, int y0, int x1, int y1, int step = 4) {
    double sum = 0.0;
    int    n   = 0;
    for (int y = y0; y < y1; y += step) {
        for (int x = x0; x < x1; x += step) {
            sum += gfx->getPixel(x, y).g;
            ++n;
        }
    }
    return n ? float(sum / n) : 0.f;
}

[[maybe_unused]] float meanRedRegion(Graphics *gfx, int x0, int y0, int x1, int y1, int step = 4) {
    double sum = 0.0;
    int    n   = 0;
    for (int y = y0; y < y1; y += step) {
        for (int x = x0; x < x1; x += step) {
            sum += gfx->getPixel(x, y).r;
            ++n;
        }
    }
    return n ? float(sum / n) : 0.f;
}

/** Minimal unit cube OBJ (outward CCW for RH Y-up). */
static const char kCubeObj[] =
    "v -0.5 -0.5  0.5\nv  0.5 -0.5  0.5\nv  0.5  0.5  0.5\nv -0.5  0.5  0.5\n"
    "vn 0 0 1\nf 1//1 2//1 3//1\nf 1//1 3//1 4//1\n"
    "v  0.5 -0.5 -0.5\nv -0.5 -0.5 -0.5\nv -0.5  0.5 -0.5\nv  0.5  0.5 -0.5\n"
    "vn 0 0 -1\nf 5//2 6//2 7//2\nf 5//2 7//2 8//2\n"
    "v  0.5 -0.5  0.5\nv  0.5 -0.5 -0.5\nv  0.5  0.5 -0.5\nv  0.5  0.5  0.5\n"
    "vn 1 0 0\nf 9//3 10//3 11//3\nf 9//3 11//3 12//3\n"
    "v -0.5 -0.5 -0.5\nv -0.5 -0.5  0.5\nv -0.5  0.5  0.5\nv -0.5  0.5 -0.5\n"
    "vn -1 0 0\nf 13//4 14//4 15//4\nf 13//4 15//4 16//4\n"
    "v -0.5  0.5  0.5\nv  0.5  0.5  0.5\nv  0.5  0.5 -0.5\nv -0.5  0.5 -0.5\n"
    "vn 0 1 0\nf 17//5 18//5 19//5\nf 17//5 19//5 20//5\n"
    "v -0.5 -0.5 -0.5\nv  0.5 -0.5 -0.5\nv  0.5 -0.5  0.5\nv -0.5 -0.5  0.5\n"
    "vn 0 -1 0\nf 21//6 22//6 23//6\nf 21//6 23//6 24//6\n";

[[maybe_unused]] Mesh *loadCube(Graphics *gfx) {
    medialoader::ModelLoader loader;
    auto                     scene = loader.loadFromMemory(kCubeObj, sizeof(kCubeObj) - 1, ".obj");
    REQUIRE(!scene.empty());
    REQUIRE(scene->mNumMeshes >= 1);
    Mesh *m = gfx->newMeshFromAssimp(*scene->mMeshes[0]);
    REQUIRE(m != nullptr);
    return m;
}

}  // namespace

TEST_CASE("RenderScenes.orientation.worldYMapsToScreenUp") {
    // Green sphere above origin, red below — top of framebuffer must read greener.
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.02f, 0.02f, 0.03f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(20, 12);
    auto *top    = Renderable3D::create();
    top->setMesh(sphere);
    top->setTexture(makeSolid(gfx, 40, 220, 40));
    top->setPosition(0.f, 1.05f, 0.f);
    top->setScale(0.55f, 0.55f, 0.55f);
    top->setMetallic(0.f);
    top->setRoughness(0.85f);

    auto *bot = Renderable3D::create();
    bot->setMesh(sphere);
    bot->setTexture(makeSolid(gfx, 220, 40, 40));
    bot->setPosition(0.f, -1.05f, 0.f);
    bot->setScale(0.55f, 0.55f, 0.55f);
    bot->setMetallic(0.f);
    bot->setRoughness(0.85f);

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 4.2f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.35f, 0.35f, 0.35f);
    cam->data()->nearZ = 0.05f;
    cam->data()->farZ  = 40.f;

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.2f, 1.f, 0.4f);
    sun->setColor(1.f, 1.f, 1.f, 1.6f);
    sun->setEnabled(true);

    addHud();
    warmPresent(gfx, 3);

    const int   w    = gfx->getWidth();
    const int   h    = gfx->getHeight();
    const float topG = meanGreenRegion(gfx, w / 4, 0, (w * 3) / 4, h / 3);
    const float botR = meanRedRegion(gfx, w / 4, (h * 2) / 3, (w * 3) / 4, h);
    const float topR = meanRedRegion(gfx, w / 4, 0, (w * 3) / 4, h / 3);
    const float botG = meanGreenRegion(gfx, w / 4, (h * 2) / 3, (w * 3) / 4, h);

    std::printf("RenderScenes.orientation topG=%.3f topR=%.3f botR=%.3f botG=%.3f\n", topG, topR, botR, botG);
    // Upside-down regression: top must prefer green, bottom prefer red.
    REQUIRE(topG > topR + 0.04f);
    REQUIRE(botR > botG + 0.04f);
    REQUIRE(topG > botG + 0.04f);
    REQUIRE(botR > topR + 0.04f);

    win->close();
}

TEST_CASE("RenderScenes.lighting.ambientHonorsCamera") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 280, 200);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.f, 0.f, 0.f, 1.f));

    auto *cube = Renderable3D::create();
    cube->setMesh(loadCube(gfx));
    cube->setTexture(makeSolid(gfx, 240, 240, 240));
    cube->setMetallic(0.f);
    cube->setRoughness(0.9f);

    auto *cam = Camera3D::createCamera();
    cam->setEye(1.6f, 1.2f, 2.4f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->data()->nearZ = 0.05f;
    cam->data()->farZ  = 30.f;

    // No lights — only ambient contributes for dielectrics.
    RenderSystem3D::setDirectionalLight(0.f, 1.f, 0.f, 0.f, 0.f, 0.f);
    addHud();

    cam->setAmbient(0.02f, 0.02f, 0.02f);
    warmPresent(gfx, 2);
    const Color darkPx = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    const float dark   = luma(darkPx);

    cam->setAmbient(0.55f, 0.55f, 0.55f);
    warmPresent(gfx, 2);
    const Color brightPx = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    const float bright   = luma(brightPx);

    std::printf("RenderScenes.ambient dark=%.4f bright=%.4f\n", dark, bright);
    // Without the old shader ambient floor, low ambient stays dark and tracks the camera.
    REQUIRE(dark < 0.18f);
    REQUIRE(bright > dark + 0.12f);
    REQUIRE(bright > 0.25f);

    win->close();
}

TEST_CASE("RenderScenes.lighting.pointDoesNotBreakOrientation") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.01f, 0.01f, 0.02f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(20, 12);
    auto *top    = Renderable3D::create();
    top->setMesh(sphere);
    top->setTexture(makeSolid(gfx, 30, 220, 30));
    top->setPosition(0.f, 1.15f, 0.f);
    top->setScale(0.6f, 0.6f, 0.6f);
    top->setMetallic(0.f);
    top->setRoughness(0.9f);

    auto *bot = Renderable3D::create();
    bot->setMesh(sphere);
    bot->setTexture(makeSolid(gfx, 220, 30, 30));
    bot->setPosition(0.f, -1.15f, 0.f);
    bot->setScale(0.6f, 0.6f, 0.6f);
    bot->setMetallic(0.f);
    bot->setRoughness(0.9f);

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 4.5f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.2f, 0.2f, 0.2f);

    auto *pt = Light3D::createLight("point");
    pt->setPosition(0.f, 0.f, 2.0f);
    pt->setColor(1.f, 1.f, 1.f, 6.f);
    pt->setRadius(8.f);
    pt->setEnabled(true);

    addHud();
    warmPresent(gfx, 3);

    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    // Sample near expected sphere centers rather than whole thirds.
    const Color topPx = gfx->getPixel(w / 2, h / 5);
    const Color botPx = gfx->getPixel(w / 2, (h * 4) / 5);
    std::printf("RenderScenes.pointOrient top=(%.2f,%.2f) bot=(%.2f,%.2f)\n", topPx.r, topPx.g, botPx.r, botPx.g);
    REQUIRE(topPx.g > topPx.r + 0.05f);
    REQUIRE(botPx.r > botPx.g + 0.05f);

    win->close();
}

TEST_CASE("RenderScenes.shadow.dirShadowDarkensWithoutFlip") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 360, 270);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.07f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(18, 12);
    auto *ground = Renderable3D::create();
    ground->setMesh(sphere);
    ground->setTexture(makeSolid(gfx, 180, 180, 185));
    ground->setPosition(0.f, -0.7f, 0.f);
    ground->setScale(3.5f, 0.08f, 3.5f);
    ground->setMetallic(0.f);
    ground->setRoughness(0.95f);
    ground->setCastShadow(false);
    ground->setReceiveShadow(true);

    auto *occluder = Renderable3D::create();
    occluder->setMesh(sphere);
    occluder->setTexture(makeSolid(gfx, 40, 200, 40));
    occluder->setPosition(0.f, 0.55f, 0.f);
    occluder->setScale(0.55f, 0.55f, 0.55f);
    occluder->setCastShadow(true);
    occluder->setReceiveShadow(true);

    auto *cam = Camera3D::createCamera();
    cam->setEye(2.8f, 2.2f, 3.6f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.08f, 0.08f, 0.09f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.35f, 1.f, 0.25f);
    sun->setColor(1.f, 0.98f, 0.94f, 2.8f);
    sun->setCastShadow(false);
    sun->setEnabled(true);

    addHud();
    warmPresent(gfx, 2);
    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    // Occluder sits above ground — with correct Y, green should dominate the upper half.
    const float topG0 = meanGreenRegion(gfx, w / 5, 0, (w * 4) / 5, h / 2);
    const float mid0  = meanLumaRegion(gfx, w / 3, h / 2, (w * 2) / 3, (h * 5) / 6);

    sun->setCastShadow(true);
    sun->setShadowStrength(1.f);
    warmPresent(gfx, 3);
    const float topG1 = meanGreenRegion(gfx, w / 5, 0, (w * 4) / 5, h / 2);
    const float mid1  = meanLumaRegion(gfx, w / 3, h / 2, (w * 2) / 3, (h * 5) / 6);

    std::printf("RenderScenes.shadow topG0=%.3f topG1=%.3f mid0=%.3f mid1=%.3f\n", topG0, topG1, mid0, mid1);
    REQUIRE(topG1 > 0.05f);                     // orientation still upright
    REQUIRE(mid1 + 0.01f < mid0);               // ground umbra darkens
    REQUIRE(std::fabs(topG1 - topG0) < 0.25f);  // occluder itself not nuked

    win->close();
}

TEST_CASE("RenderScenes.effects.gbufferAoDoesNotInvertOrientation") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.03f, 0.03f, 0.04f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(16, 10);
    auto *top    = Renderable3D::create();
    top->setMesh(sphere);
    top->setTexture(makeSolid(gfx, 40, 210, 40));
    top->setPosition(0.f, 1.0f, 0.f);
    top->setScale(0.5f, 0.5f, 0.5f);

    auto *bot = Renderable3D::create();
    bot->setMesh(sphere);
    bot->setTexture(makeSolid(gfx, 210, 40, 40));
    bot->setPosition(0.f, -1.0f, 0.f);
    bot->setScale(0.5f, 0.5f, 0.5f);

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 4.0f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.2f, 0.2f, 0.22f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.3f, 1.f, 0.2f);
    sun->setColor(1.f, 1.f, 1.f, 1.5f);
    sun->setEnabled(true);

    RenderControl *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);
    rc->enable("gbuffer");
    rc->compile();
    REQUIRE(rc->isCompiled());
    REQUIRE(rc->hasPass("gbuffer"));

    addHud();
    warmPresent(gfx, 3);

    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    REQUIRE(meanGreenRegion(gfx, w / 4, 0, (w * 3) / 4, h / 3) >
            meanRedRegion(gfx, w / 4, 0, (w * 3) / 4, h / 3) + 0.03f);

    // Disable gbuffer so later cases are not polluted.
    rc->disable("gbuffer");
    rc->compile();
    win->close();
}

TEST_CASE("RenderScenes.mesh.assimpNodeTransformBaked") {
    // Parent node translates +Y by 2 — baked upload must move verts with it.
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 280, 200);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.02f, 0.02f, 0.03f, 1.f));

    medialoader::ModelLoader loader;
    auto                     scene = loader.loadFromMemory(kCubeObj, sizeof(kCubeObj) - 1, ".obj");
    REQUIRE(!scene.empty());
    REQUIRE(scene->mNumMeshes >= 1);

    aiMatrix4x4 world;
    world.a4   = 0.f;
    world.b4   = 2.f;  // +Y translation
    world.c4   = 0.f;
    Mesh* mesh = gfx->newMeshFromAssimp(*scene->mMeshes[0], aiToGlm(world));
    REQUIRE(mesh != nullptr);

    auto *ent = Renderable3D::create();
    ent->setMesh(mesh);
    ent->setTexture(makeSolid(gfx, 40, 220, 40));
    ent->setMetallic(0.f);
    ent->setRoughness(0.8f);

    auto *cam = Camera3D::createCamera();
    // Aim at the baked +Y location; identity-local cube at origin would miss the frustum center.
    cam->setEye(0.f, 2.f, 4.0f);
    cam->setTarget(0.f, 2.f, 0.f);
    cam->setAmbient(0.25f, 0.25f, 0.25f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.2f, 1.f, 0.3f);
    sun->setColor(1.f, 1.f, 1.f, 1.4f);
    sun->setEnabled(true);

    addHud();
    warmPresent(gfx, 3);

    const int   w   = gfx->getWidth();
    const int   h   = gfx->getHeight();
    const float mid = meanLumaRegion(gfx, w / 3, h / 3, (w * 2) / 3, (h * 2) / 3);
    REQUIRE(mid > 0.08f);

    // Control: upload without bake, keep camera on +Y — center should be mostly background.
    resetScene3D();
    Mesh *local = gfx->newMeshFromAssimp(*scene->mMeshes[0]);
    auto *miss  = Renderable3D::create();
    miss->setMesh(local);
    miss->setTexture(makeSolid(gfx, 40, 220, 40));
    auto *cam2 = Camera3D::createCamera();
    cam2->setEye(0.f, 2.f, 4.0f);
    cam2->setTarget(0.f, 2.f, 0.f);
    cam2->setAmbient(0.25f, 0.25f, 0.25f);
    auto *sun2 = Light3D::createLight("dir");
    sun2->setDirection(0.2f, 1.f, 0.3f);
    sun2->setColor(1.f, 1.f, 1.f, 1.4f);
    sun2->setEnabled(true);
    addHud();
    warmPresent(gfx, 2);
    const float midMiss = meanLumaRegion(gfx, w / 3, h / 3, (w * 2) / 3, (h * 2) / 3);
    std::printf("RenderScenes.nodeBake mid=%.3f midMiss=%.3f\n", mid, midMiss);
    REQUIRE(mid > midMiss + 0.04f);

    win->close();
}


TEST_CASE("RenderScenes.pick.screenToRayMatchesUprightProjection") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 200, 150);
    resetScene3D();

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 5.f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setUp(0.f, 1.f, 0.f);
    cam->setFov(60.f);

    // Top-center pixel should ray toward +Y (world up), not -Y.
    cam->screenToRay(100.f, 10.f, 200.f, 150.f);
    REQUIRE(cam->getScreenRayDirY() > 0.05f);

    // Bottom-center pixel should ray toward -Y.
    cam->screenToRay(100.f, 140.f, 200.f, 150.f);
    REQUIRE(cam->getScreenRayDirY() < -0.05f);

    win->close();
}

TEST_CASE("RenderScenes.studio.threePointBrightensVsAmbient") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 360, 270);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.02f, 0.02f, 0.03f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(20, 12);
    auto *subj   = Renderable3D::create();
    subj->setMesh(sphere);
    subj->setTexture(makeSolid(gfx, 210, 210, 215));
    subj->setMetallic(0.15f);
    subj->setRoughness(0.45f);

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.6f, 3.2f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.04f, 0.04f, 0.05f);

    addHud();
    warmPresent(gfx, 2);
    const float ambOnly = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

    auto *key = Light3D::createLight("dir");
    key->setDirection(0.55f, 0.85f, 0.35f);
    key->setColor(1.f, 0.97f, 0.92f, 2.4f);
    key->setEnabled(true);

    auto *fill = Light3D::createLight("point");
    fill->setPosition(-1.6f, 0.4f, 1.2f);
    fill->setColor(0.55f, 0.65f, 1.f, 5.5f);
    fill->setRadius(5.f);
    fill->setEnabled(true);

    auto *rim = Light3D::createLight("point");
    rim->setPosition(0.2f, 1.2f, -1.8f);
    rim->setColor(1.f, 0.75f, 0.55f, 4.5f);
    rim->setRadius(5.f);
    rim->setEnabled(true);

    warmPresent(gfx, 3);
    const float lit = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));
    std::printf("RenderScenes.threePoint amb=%.3f lit=%.3f\n", ambOnly, lit);
    REQUIRE(lit > ambOnly + 0.05f);

    // Orientation still upright: sample above/below center after lighting.
    auto *topMark = Renderable3D::create();
    topMark->setMesh(sphere);
    topMark->setTexture(makeSolid(gfx, 40, 220, 40));
    topMark->setPosition(0.f, 1.35f, 0.f);
    topMark->setScale(0.35f, 0.35f, 0.35f);
    warmPresent(gfx, 2);
    const Color topPx = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 5);
    REQUIRE(topPx.g + 0.02f > topPx.r);

    win->close();
}

TEST_CASE("RenderScenes.ibl.metalBrighterThanDielectric") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 360, 240);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.01f, 0.01f, 0.02f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(24, 14);
    auto *metal  = Renderable3D::create();
    metal->setMesh(sphere);
    metal->setTexture(makeSolid(gfx, 220, 220, 225));
    metal->setPosition(-0.85f, 0.f, 0.f);
    metal->setScale(0.55f, 0.55f, 0.55f);
    metal->setMetallic(1.f);
    metal->setRoughness(0.12f);

    auto *diel = Renderable3D::create();
    diel->setMesh(sphere);
    diel->setTexture(makeSolid(gfx, 220, 220, 225));
    diel->setPosition(0.85f, 0.f, 0.f);
    diel->setScale(0.55f, 0.55f, 0.55f);
    diel->setMetallic(0.f);
    diel->setRoughness(0.55f);

    // Face-colored env so metals pick up tinted specular.
    const uint8_t faceRgb[6][3] = {
        {240, 180, 120}, {100, 140, 220}, {250, 250, 255}, {30, 30, 35}, {200, 210, 230}, {180, 160, 140},
    };
    const int            face = 12;
    std::vector<uint8_t> faces(size_t(face) * size_t(face) * 4u * 6u);
    for (int f = 0; f < 6; ++f) {
        uint8_t *dst = faces.data() + size_t(f) * size_t(face) * size_t(face) * 4u;
        for (int i = 0; i < face * face; ++i) {
            dst[i * 4 + 0] = faceRgb[f][0];
            dst[i * 4 + 1] = faceRgb[f][1];
            dst[i * 4 + 2] = faceRgb[f][2];
            dst[i * 4 + 3] = 255;
        }
    }
    Texture *env = gfx->newCubemap(face, faces.data());

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.35f, 3.4f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.03f, 0.03f, 0.03f);
    cam->setEnvMap(env);
    cam->setEnvIntensity(1.5f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.25f, 1.f, 0.2f);
    sun->setColor(0.4f, 0.4f, 0.45f, 0.6f);
    sun->setEnabled(true);

    addHud();
    warmPresent(gfx, 3);

    const int   w      = gfx->getWidth();
    const int   h      = gfx->getHeight();
    const float metalL = meanLumaRegion(gfx, w / 8, h / 3, w * 3 / 8, h * 2 / 3, 4, true);
    const float dielL  = meanLumaRegion(gfx, w * 5 / 8, h / 3, w * 7 / 8, h * 2 / 3, 4, true);
    std::printf("RenderScenes.ibl metalL=%.3f dielL=%.3f\n", metalL, dielL);
    REQUIRE(metalL > dielL + 0.02f);

    win->close();
}

TEST_CASE("RenderScenes.unlit.ignoresDirectionalLight") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 280, 200);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.f, 0.f, 0.f, 1.f));

    Material *unlit = gfx->newMaterial();
    unlit->setShadingModel("unlit");
    unlit->setAlbedoTexture(makeSolid(gfx, 180, 40, 40));
    unlit->setReceiveLight(false);

    auto *ent = Renderable3D::create();
    ent->setMesh(loadCube(gfx));
    ent->setMaterial(unlit);
    ent->setReceiveLight(false);

    auto *cam = Camera3D::createCamera();
    cam->setEye(1.8f, 1.4f, 2.6f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.2f, 0.2f, 0.2f);

    addHud();
    warmPresent(gfx, 2);
    const float before = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.3f, 1.f, 0.4f);
    sun->setColor(1.f, 1.f, 1.f, 4.f);
    sun->setEnabled(true);
    warmPresent(gfx, 2);
    const float after = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

    std::printf("RenderScenes.unlit before=%.3f after=%.3f\n", before, after);
    REQUIRE(before > 0.05f);
    // Unlit must not jump with a strong directional light.
    REQUIRE(std::fabs(after - before) < 0.08f);

    delete unlit;
    win->close();
}

TEST_CASE("RenderScenes.shadow.pointAndDirCoexist") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 360, 270);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.04f, 0.04f, 0.05f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(16, 10);
    auto *ground = Renderable3D::create();
    ground->setMesh(sphere);
    ground->setTexture(makeSolid(gfx, 170, 170, 175));
    ground->setPosition(0.f, -0.75f, 0.f);
    ground->setScale(3.2f, 0.08f, 3.2f);
    ground->setMetallic(0.f);
    ground->setRoughness(0.95f);
    ground->setCastShadow(false);
    ground->setReceiveShadow(true);

    auto *occ = Renderable3D::create();
    occ->setMesh(sphere);
    occ->setTexture(makeSolid(gfx, 40, 200, 40));
    occ->setPosition(0.f, 0.45f, 0.f);
    occ->setScale(0.5f, 0.5f, 0.5f);
    occ->setCastShadow(true);

    auto *cam = Camera3D::createCamera();
    cam->setEye(2.6f, 2.0f, 3.4f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.06f, 0.06f, 0.07f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.4f, 1.f, 0.2f);
    sun->setColor(1.f, 0.98f, 0.94f, 2.4f);
    sun->setCastShadow(true);
    sun->setShadowStrength(1.f);
    sun->setEnabled(true);

    auto *pt = Light3D::createLight("point");
    pt->setPosition(-1.2f, 1.0f, 1.0f);
    pt->setColor(0.4f, 0.55f, 1.f, 6.f);
    pt->setRadius(5.f);
    pt->setEnabled(true);

    addHud();
    warmPresent(gfx, 3);

    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    // Green occluder still reads in the upper half (orientation) while ground is lit.
    REQUIRE(meanGreenRegion(gfx, w / 5, 0, (w * 4) / 5, h / 2) > 0.04f);
    REQUIRE(meanLumaRegion(gfx, w / 4, h / 2, (w * 3) / 4, h) > 0.05f);

    win->close();
}

TEST_CASE("RenderScenes.winding.insideClosedRoomVisible") {
    // Colored left/right walls viewed from inside — verifies upright orientation + visibility.
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.02f, 0.02f, 0.03f, 1.f));

    Mesh *cube = loadCube(gfx);
    auto *back = Renderable3D::create();
    back->setMesh(cube);
    back->setTexture(makeSolid(gfx, 180, 180, 185));
    back->setPosition(0.f, 1.f, -1.4f);
    back->setScale(2.2f, 2.0f, 0.08f);
    back->setRoughness(0.9f);

    auto *left = Renderable3D::create();
    left->setMesh(cube);
    left->setTexture(makeSolid(gfx, 210, 35, 35));
    left->setPosition(-1.1f, 1.f, 0.f);
    left->setScale(0.08f, 2.0f, 2.2f);
    left->setRoughness(0.9f);

    auto *right = Renderable3D::create();
    right->setMesh(cube);
    right->setTexture(makeSolid(gfx, 35, 190, 35));
    right->setPosition(1.1f, 1.f, 0.f);
    right->setScale(0.08f, 2.0f, 2.2f);
    right->setRoughness(0.9f);

    auto *floor = Renderable3D::create();
    floor->setMesh(cube);
    floor->setTexture(makeSolid(gfx, 140, 140, 145));
    floor->setPosition(0.f, 0.f, 0.f);
    floor->setScale(2.2f, 0.08f, 2.2f);
    floor->setRoughness(0.95f);

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 1.0f, 1.5f);
    cam->setTarget(0.f, 1.0f, 0.f);
    cam->setAmbient(0.3f, 0.3f, 0.3f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.15f, 1.f, 0.1f);
    sun->setColor(1.f, 1.f, 1.f, 1.4f);
    sun->setEnabled(true);

    addHud();
    warmPresent(gfx, 3);

    const int   w = gfx->getWidth();
    const int   h = gfx->getHeight();
    const Color L = gfx->getPixel(w / 6, h / 2);
    const Color R = gfx->getPixel((w * 5) / 6, h / 2);
    std::printf("RenderScenes.room L=(%.2f,%.2f) R=(%.2f,%.2f)\n", L.r, L.g, R.r, R.g);
    REQUIRE(L.r > L.g + 0.05f);
    REQUIRE(R.g > R.r + 0.05f);
    // Floor toward bottom of frame (upright).
    const Color bot = gfx->getPixel(w / 2, (h * 5) / 6);
    REQUIRE(luma(bot) > 0.05f);

    win->close();
}

TEST_CASE("RenderScenes.gallery.cylinderAndSphereMaterials") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 400, 280);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.05f, 0.055f, 0.06f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(18, 12);
    Mesh *cyl    = gfx->newMeshCylinder(18, 1, true);

    const float metals[3] = {0.f, 0.5f, 1.f};
    const float roughs[3] = {0.2f, 0.5f, 0.85f};
    for (int i = 0; i < 3; ++i) {
        auto *s = Renderable3D::create();
        s->setMesh(sphere);
        s->setTexture(makeSolid(gfx, 200, 200, 205));
        s->setPosition(-1.2f + float(i) * 1.2f, 0.55f, 0.f);
        s->setScale(0.4f, 0.4f, 0.4f);
        s->setMetallic(metals[i]);
        s->setRoughness(roughs[i]);

        auto *c = Renderable3D::create();
        c->setMesh(cyl);
        c->setTexture(makeSolid(gfx, uint8_t(80 + i * 50), 160, uint8_t(200 - i * 40)));
        c->setPosition(-1.2f + float(i) * 1.2f, -0.35f, 0.f);
        c->setScale(0.28f, 0.35f, 0.28f);
        c->setMetallic(0.05f);
        c->setRoughness(0.7f);
    }

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 1.2f, 4.2f);
    cam->setTarget(0.f, 0.1f, 0.f);
    cam->setAmbient(0.1f, 0.1f, 0.11f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.35f, 1.f, 0.4f);
    sun->setColor(1.f, 0.98f, 0.95f, 2.2f);
    sun->setEnabled(true);

    addHud();
    warmPresent(gfx, 3);

    const float mid = meanLumaRegion(gfx, 40, 40, 360, 240);
    REQUIRE(mid > 0.04f);
    // Top row (spheres) and bottom row (cylinders) both contribute.
    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    REQUIRE(meanLumaRegion(gfx, w / 6, h / 8, (w * 5) / 6, h * 2 / 5) > 0.03f);
    REQUIRE(meanLumaRegion(gfx, w / 6, h * 3 / 5, (w * 5) / 6, (h * 7) / 8) > 0.03f);

    win->close();
}

TEST_CASE("RenderScenes.isolation.effectsDoNotFlipOrientation") {
    // Cycle shadow + gbuffer features; upright green/red markers must survive.
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.02f, 0.02f, 0.03f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(18, 12);
    auto *top    = Renderable3D::create();
    top->setMesh(sphere);
    top->setTexture(makeSolid(gfx, 40, 220, 40));
    top->setPosition(0.f, 1.05f, 0.f);
    top->setScale(0.5f, 0.5f, 0.5f);
    top->setCastShadow(true);

    auto *bot = Renderable3D::create();
    bot->setMesh(sphere);
    bot->setTexture(makeSolid(gfx, 220, 40, 40));
    bot->setPosition(0.f, -1.05f, 0.f);
    bot->setScale(0.5f, 0.5f, 0.5f);
    bot->setCastShadow(true);

    auto *ground = Renderable3D::create();
    ground->setMesh(sphere);
    ground->setTexture(makeSolid(gfx, 90, 90, 95));
    ground->setPosition(0.f, -1.7f, 0.f);
    ground->setScale(2.5f, 0.08f, 2.5f);
    ground->setReceiveShadow(true);
    ground->setCastShadow(false);

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.2f, 4.4f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.15f, 0.15f, 0.16f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.3f, 1.f, 0.25f);
    sun->setColor(1.f, 1.f, 1.f, 2.0f);
    sun->setEnabled(true);

    RenderControl *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);

    auto assertUpright = [&](const char *phase) {
        warmPresent(gfx, 2);
        const int   w = gfx->getWidth();
        const int   h = gfx->getHeight();
        const Color t = gfx->getPixel(w / 2, h / 5);
        const Color b = gfx->getPixel(w / 2, (h * 4) / 5);
        std::printf("RenderScenes.isolation[%s] topG=%.2f botR=%.2f\n", phase, t.g, b.r);
        REQUIRE(t.g > t.r + 0.04f);
        REQUIRE(b.r > b.g + 0.04f);
    };

    sun->setCastShadow(false);
    rc->disable("gbuffer");
    rc->compile();
    assertUpright("lit");

    sun->setCastShadow(true);
    sun->setShadowStrength(1.f);
    assertUpright("shadow");

    rc->enable("gbuffer");
    rc->compile();
    assertUpright("gbuffer");

    rc->disable("gbuffer");
    sun->setCastShadow(false);
    rc->compile();
    assertUpright("reset");

    win->close();
}

TEST_CASE("RenderScenes.clustered.manyPointsBrightenWithoutFlip") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.01f, 0.01f, 0.015f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(14, 10);
    auto *ground = Renderable3D::create();
    ground->setMesh(sphere);
    ground->setTexture(makeSolid(gfx, 160, 160, 165));
    ground->setPosition(0.f, -0.6f, 0.f);
    ground->setScale(3.0f, 0.08f, 3.0f);
    ground->setRoughness(0.95f);

    auto *marker = Renderable3D::create();
    marker->setMesh(sphere);
    marker->setTexture(makeSolid(gfx, 40, 210, 40));
    marker->setPosition(0.f, 0.7f, 0.f);
    marker->setScale(0.45f, 0.45f, 0.45f);

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 1.5f, 4.0f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.02f, 0.02f, 0.025f);

    // >8 lights triggers clustered path in RenderSystem3D.
    for (int i = 0; i < 12; ++i) {
        const float a  = float(i) / 12.f * 6.2831853f;
        auto       *pt = Light3D::createLight("point");
        pt->setPosition(std::cos(a) * 1.4f, 0.55f, std::sin(a) * 1.4f);
        pt->setColor(0.7f + 0.3f * std::cos(a), 0.7f, 0.7f + 0.3f * std::sin(a), 3.5f);
        pt->setRadius(3.5f);
        pt->setEnabled(true);
    }

    addHud();
    warmPresent(gfx, 3);

    const int   w   = gfx->getWidth();
    const int   h   = gfx->getHeight();
    const float mid = meanLumaRegion(gfx, w / 4, h / 4, (w * 3) / 4, (h * 3) / 4);
    REQUIRE(mid > 0.04f);
    const Color top = gfx->getPixel(w / 2, h / 4);
    REQUIRE(top.g + 0.01f > top.r);  // green marker stays in upper half

    win->close();
}

TEST_CASE("RenderScenes.depth.nearOccludesFar") {
    // Red cube in front of a larger green cube — center pixel must read red.
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 280, 200);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.02f, 0.02f, 0.03f, 1.f));

    Mesh *cube     = loadCube(gfx);
    auto *farGreen = Renderable3D::create();
    farGreen->setMesh(cube);
    farGreen->setTexture(makeSolid(gfx, 40, 210, 40));
    farGreen->setPosition(0.f, 0.f, -0.6f);
    farGreen->setScale(1.4f, 1.4f, 0.2f);
    farGreen->setRoughness(0.9f);

    auto *nearRed = Renderable3D::create();
    nearRed->setMesh(cube);
    nearRed->setTexture(makeSolid(gfx, 220, 40, 40));
    nearRed->setPosition(0.f, 0.f, 0.4f);
    nearRed->setScale(0.55f, 0.55f, 0.55f);
    nearRed->setRoughness(0.9f);

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.15f, 3.2f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.3f, 0.3f, 0.3f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.2f, 1.f, 0.3f);
    sun->setColor(1.f, 1.f, 1.f, 1.5f);
    sun->setEnabled(true);

    addHud();
    warmPresent(gfx, 3);

    const Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    std::printf("RenderScenes.depth mid=(%.2f,%.2f,%.2f)\n", mid.r, mid.g, mid.b);
    REQUIRE(mid.r > mid.g + 0.05f);
    REQUIRE(mid.r > 0.08f);

    win->close();
}

TEST_CASE("RenderScenes.scale.nonUniformKeepsUprightMarkers") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.02f, 0.02f, 0.03f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(16, 10);
    auto *top    = Renderable3D::create();
    top->setMesh(sphere);
    top->setTexture(makeSolid(gfx, 40, 220, 40));
    top->setPosition(0.f, 1.15f, 0.f);
    top->setScale(0.85f, 0.5f, 0.7f);
    top->setRoughness(0.85f);

    auto *bot = Renderable3D::create();
    bot->setMesh(sphere);
    bot->setTexture(makeSolid(gfx, 220, 40, 40));
    bot->setPosition(0.f, -1.15f, 0.f);
    bot->setScale(0.5f, 0.85f, 0.7f);
    bot->setRoughness(0.85f);

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 4.4f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.25f, 0.25f, 0.25f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.25f, 1.f, 0.3f);
    sun->setColor(1.f, 1.f, 1.f, 1.6f);
    sun->setEnabled(true);

    addHud();
    warmPresent(gfx, 3);

    const int   w    = gfx->getWidth();
    const int   h    = gfx->getHeight();
    const float topG = meanGreenRegion(gfx, w / 3, 0, (w * 2) / 3, h / 3);
    const float topR = meanRedRegion(gfx, w / 3, 0, (w * 2) / 3, h / 3);
    const float botR = meanRedRegion(gfx, w / 3, (h * 2) / 3, (w * 2) / 3, h);
    const float botG = meanGreenRegion(gfx, w / 3, (h * 2) / 3, (w * 2) / 3, h);
    REQUIRE(topG > topR + 0.03f);
    REQUIRE(botR > botG + 0.03f);

    win->close();
}

TEST_CASE("RenderScenes.checker.colorGridReadable") {
    // 2x2 tinted cubes — quadrant regions must keep their dominant channel.
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.01f, 0.01f, 0.015f, 1.f));

    Mesh *cube = loadCube(gfx);
    struct Cell {
        float   x, y;
        uint8_t r, g, b;
    };
    const Cell cells[4] = {
        {-1.05f, 1.05f, 220, 40, 40},
        {1.05f, 1.05f, 40, 220, 40},
        {-1.05f, -1.05f, 40, 40, 220},
        {1.05f, -1.05f, 220, 220, 40},
    };
    for (const auto &c : cells) {
        auto *e = Renderable3D::create();
        e->setMesh(cube);
        e->setTexture(makeSolid(gfx, c.r, c.g, c.b));
        e->setPosition(c.x, c.y, 0.f);
        e->setScale(0.7f, 0.7f, 0.7f);
        e->setRoughness(0.9f);
    }

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 4.5f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.35f, 0.35f, 0.35f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.2f, 1.f, 0.25f);
    sun->setColor(1.f, 1.f, 1.f, 1.2f);
    sun->setEnabled(true);

    addHud();
    warmPresent(gfx, 3);

    const int   w   = gfx->getWidth();
    const int   h   = gfx->getHeight();
    const float tlR = meanRedRegion(gfx, w / 8, h / 8, w * 3 / 8, h * 3 / 8);
    const float tlG = meanGreenRegion(gfx, w / 8, h / 8, w * 3 / 8, h * 3 / 8);
    const float trG = meanGreenRegion(gfx, w * 5 / 8, h / 8, w * 7 / 8, h * 3 / 8);
    const float trR = meanRedRegion(gfx, w * 5 / 8, h / 8, w * 7 / 8, h * 3 / 8);
    const float blB = meanLumaRegion(gfx, w / 8, h * 5 / 8, w * 3 / 8, h * 7 / 8);  // proxy
    (void)blB;
    double sumB = 0.0;
    int    nB   = 0;
    for (int y = h * 5 / 8; y < h * 7 / 8; y += 4) {
        for (int x = w / 8; x < w * 3 / 8; x += 4) {
            sumB += gfx->getPixel(x, y).b;
            ++nB;
        }
    }
    const float blBlue = nB ? float(sumB / nB) : 0.f;
    const float brR    = meanRedRegion(gfx, w * 5 / 8, h * 5 / 8, w * 7 / 8, h * 7 / 8);
    const float brG    = meanGreenRegion(gfx, w * 5 / 8, h * 5 / 8, w * 7 / 8, h * 7 / 8);
    std::printf("RenderScenes.checker tlR=%.2f trG=%.2f blB=%.2f brR=%.2f\n", tlR, trG, blBlue, brR);
    REQUIRE(tlR > tlG + 0.03f);
    REQUIRE(trG > trR + 0.03f);
    REQUIRE(blBlue > 0.08f);
    REQUIRE(brR > 0.08f);
    REQUIRE(brG > 0.08f);

    win->close();
}

TEST_CASE("RenderScenes.camera.moveKeepsUprightMarkers") {
    // Moving the active camera must not invert world-Y markers.
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.02f, 0.02f, 0.03f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(16, 10);
    auto *top    = Renderable3D::create();
    top->setMesh(sphere);
    top->setTexture(makeSolid(gfx, 40, 220, 40));
    top->setPosition(0.f, 1.0f, 0.f);
    top->setScale(0.5f, 0.5f, 0.5f);

    auto *bot = Renderable3D::create();
    bot->setMesh(sphere);
    bot->setTexture(makeSolid(gfx, 220, 40, 40));
    bot->setPosition(0.f, -1.0f, 0.f);
    bot->setScale(0.5f, 0.5f, 0.5f);

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 4.2f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.25f, 0.25f, 0.25f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.25f, 1.f, 0.3f);
    sun->setColor(1.f, 1.f, 1.f, 1.5f);
    sun->setEnabled(true);

    addHud();
    auto assertUpright = [&](const char *tag) {
        warmPresent(gfx, 2);
        const int   w    = gfx->getWidth();
        const int   h    = gfx->getHeight();
        const float topG = meanGreenRegion(gfx, w / 3, 0, (w * 2) / 3, h / 3);
        const float topR = meanRedRegion(gfx, w / 3, 0, (w * 2) / 3, h / 3);
        const float botR = meanRedRegion(gfx, w / 3, (h * 2) / 3, (w * 2) / 3, h);
        const float botG = meanGreenRegion(gfx, w / 3, (h * 2) / 3, (w * 2) / 3, h);
        std::printf("RenderScenes.cameraMove[%s] topG=%.2f botR=%.2f\n", tag, topG, botR);
        REQUIRE(topG > topR + 0.03f);
        REQUIRE(botR > botG + 0.03f);
    };
    assertUpright("near");

    cam->setEye(0.35f, 0.25f, 4.8f);
    cam->setTarget(0.f, 0.f, 0.f);
    assertUpright("orbit");

    win->close();
}

TEST_CASE("RenderScenes.shadow.receiveOffIgnoresUmbra") {
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 360, 270);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.05f, 0.05f, 0.06f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(16, 10);
    auto *ground = Renderable3D::create();
    ground->setMesh(sphere);
    ground->setTexture(makeSolid(gfx, 180, 180, 185));
    ground->setPosition(0.f, -0.7f, 0.f);
    ground->setScale(3.2f, 0.08f, 3.2f);
    ground->setRoughness(0.95f);
    ground->setCastShadow(false);
    ground->setReceiveShadow(false);  // should ignore caster umbra

    auto *occ = Renderable3D::create();
    occ->setMesh(sphere);
    occ->setTexture(makeSolid(gfx, 40, 200, 40));
    occ->setPosition(0.f, 0.55f, 0.f);
    occ->setScale(0.5f, 0.5f, 0.5f);
    occ->setCastShadow(true);

    auto *cam = Camera3D::createCamera();
    cam->setEye(2.8f, 2.2f, 3.6f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.08f, 0.08f, 0.09f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.35f, 1.f, 0.25f);
    sun->setColor(1.f, 0.98f, 0.94f, 2.8f);
    sun->setCastShadow(true);
    sun->setShadowStrength(1.f);
    sun->setEnabled(true);

    addHud();
    warmPresent(gfx, 3);

    const int   w   = gfx->getWidth();
    const int   h   = gfx->getHeight();
    const float mid = meanLumaRegion(gfx, w / 3, h / 2, (w * 2) / 3, (h * 5) / 6);
    // Ground stays reasonably bright despite an active shadow caster above it.
    REQUIRE(mid > 0.08f);
    REQUIRE(meanGreenRegion(gfx, w / 5, 0, (w * 4) / 5, h / 2) > 0.04f);

    win->close();
}
}  // namespace test_RenderSceneEffects
