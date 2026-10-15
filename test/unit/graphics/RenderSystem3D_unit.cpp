#include "zeroerr/assert.h"
#include <cstdio>
#include "zeroerr/unittest.h"
#include "Fixtures.h"

#include <SDL2/SDL.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "filesystem/FileData.h"
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
#include "medialoader/image/FormatHandler.h"
#include "medialoader/model/ModelLoader.h"
#include "window/Window.h"

#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>
#include <fstream>
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

// Cube with per-face UVs (0..1). Front face (+Z) is first so Assimp preserves it.
static const char kCubeObj[] =
    // +Z front
    "v -0.5 -0.5  0.5\n"
    "v  0.5 -0.5  0.5\n"
    "v  0.5  0.5  0.5\n"
    "v -0.5  0.5  0.5\n"
    "vt 0 0\n"
    "vt 1 0\n"
    "vt 1 1\n"
    "vt 0 1\n"
    "vn 0 0 1\n"
    "f 1/1/1 2/2/1 3/3/1\n"
    "f 1/1/1 3/3/1 4/4/1\n"
    // -Z back
    "v  0.5 -0.5 -0.5\n"
    "v -0.5 -0.5 -0.5\n"
    "v -0.5  0.5 -0.5\n"
    "v  0.5  0.5 -0.5\n"
    "vn 0 0 -1\n"
    "f 5/1/2 6/2/2 7/3/2\n"
    "f 5/1/2 7/3/2 8/4/2\n"
    // +X right
    "v  0.5 -0.5  0.5\n"
    "v  0.5 -0.5 -0.5\n"
    "v  0.5  0.5 -0.5\n"
    "v  0.5  0.5  0.5\n"
    "vn 1 0 0\n"
    "f 9/1/3 10/2/3 11/3/3\n"
    "f 9/1/3 11/3/3 12/4/3\n"
    // -X left
    "v -0.5 -0.5 -0.5\n"
    "v -0.5 -0.5  0.5\n"
    "v -0.5  0.5  0.5\n"
    "v -0.5  0.5 -0.5\n"
    "vn -1 0 0\n"
    "f 13/1/4 14/2/4 15/3/4\n"
    "f 13/1/4 15/3/4 16/4/4\n"
    // +Y top
    "v -0.5  0.5  0.5\n"
    "v  0.5  0.5  0.5\n"
    "v  0.5  0.5 -0.5\n"
    "v -0.5  0.5 -0.5\n"
    "vn 0 1 0\n"
    "f 17/1/5 18/2/5 19/3/5\n"
    "f 17/1/5 19/3/5 20/4/5\n"
    // -Y bottom
    "v -0.5 -0.5 -0.5\n"
    "v  0.5 -0.5 -0.5\n"
    "v  0.5 -0.5  0.5\n"
    "v -0.5 -0.5  0.5\n"
    "vn 0 -1 0\n"
    "f 21/1/6 22/2/6 23/3/6\n"
    "f 21/1/6 23/3/6 24/4/6\n";

[[maybe_unused]] static float luma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }


[[maybe_unused]] static Mesh *loadUvCube(Graphics *gfx) {
    medialoader::ModelLoader loader;
    auto scene = loader.loadFromMemory(kCubeObj, sizeof(kCubeObj) - 1, ".obj");
    REQUIRE(!scene.empty());
    REQUIRE(scene->mNumMeshes >= 1);
    REQUIRE(scene->mMeshes[0]->HasTextureCoords(0));
    Mesh *m = gfx->newMeshFromAssimp(*scene->mMeshes[0]);
    REQUIRE(m != nullptr);
    REQUIRE_GT(m->indexCount, 0);
    return m;
}

[[maybe_unused]] static Texture *makeChecker(Graphics *gfx, int size, int cell) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            uint8_t c = ((x / cell) ^ (y / cell)) & 1 ? 255 : 0;
            size_t i = size_t(y * size + x) * 4;
            px[i] = c;
            px[i + 1] = c;
            px[i + 2] = c;
            px[i + 3] = 255;
        }
    }
    return gfx->newTexture(size, size, px.data(), TextureCreateInfo::withMipmaps(true));
}

[[maybe_unused]] static Texture *makeSolidGray(Graphics *gfx, uint8_t g) {
    uint8_t px[4] = {g, g, g, 255};
    return gfx->newTexture(1, 1, px);
}

/** Non-periodic motif so UV remapping (not just phase shift) is measurable. */
[[maybe_unused]] static Texture *makeMotif(Graphics *gfx, int size) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4);
    const float cx = float(size) * 0.35f;
    const float cy = float(size) * 0.4f;
    const float r = float(size) * 0.22f;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float dx = float(x) - cx;
            float dy = float(y) - cy;
            float d = std::sqrt(dx * dx + dy * dy);
            uint8_t v = d < r ? 255 : uint8_t((x * 13 + y * 7) % 180);
            if (x > size * 3 / 4 && y < size / 4) v = 40;
            size_t i = size_t(y * size + x) * 4;
            px[i] = v;
            px[i + 1] = uint8_t((v * 3) / 4);
            px[i + 2] = uint8_t(255 - v / 2);
            px[i + 3] = 255;
        }
    }
    TextureCreateInfo info = TextureCreateInfo::withMipmaps(true);
    info.sampler.repeatU = true;
    info.sampler.repeatV = true;
    return gfx->newTexture(size, size, px.data(), info);
}

// Graphics/ECS are process-wide singletons — hide leftovers from earlier cases.
[[maybe_unused]] static void resetScene3D() {
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

// NOTE: Graphics is a process-wide singleton — one window lifetime per process.


/** Left-half high / right-half low height so glancing POM shifts albedo UVs. */
[[maybe_unused]] static Texture *makeSplitHeight(Graphics *gfx, int size) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            uint8_t h = (x < size / 2) ? 255 : 0;
            size_t i = size_t(y * size + x) * 4;
            px[i] = h;
            px[i + 1] = h;
            px[i + 2] = h;
            px[i + 3] = 255;
        }
    }
    return gfx->newTexture(size, size, px.data(), true, true);
}


TEST_CASE("Camera3D.screenToRayPick") {
    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 5.f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setUp(0.f, 1.f, 0.f);
    cam->setFov(60.f);
    cam->screenToRay(160.f, 120.f, 320.f, 240.f);  // center pixel

    CHECK(std::fabs(cam->getScreenRayOriginX()) < 1e-4f);
    CHECK(std::fabs(cam->getScreenRayOriginY()) < 1e-4f);
    CHECK(std::fabs(cam->getScreenRayOriginZ() - 5.f) < 1e-4f);

    // Looking toward -Z; center ray should be mostly -Z.
    CHECK(std::fabs(cam->getScreenRayDirX()) < 0.05f);
    CHECK(std::fabs(cam->getScreenRayDirY()) < 0.05f);
    CHECK(cam->getScreenRayDirZ() < -0.9f);

    float len = std::sqrt(cam->getScreenRayDirX() * cam->getScreenRayDirX() +
                          cam->getScreenRayDirY() * cam->getScreenRayDirY() +
                          cam->getScreenRayDirZ() * cam->getScreenRayDirZ());
    CHECK(std::fabs(len - 1.f) < 1e-4f);
}

TEST_CASE("Camera3D.orthographicScreenRaysAreParallel") {
    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 10.f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setOrthographic(8.f);

    cam->screenToRay(20.f, 60.f, 160.f, 120.f);
    const float leftOrigin = cam->getScreenRayOriginX();
    const float leftDirX = cam->getScreenRayDirX();
    const float leftDirZ = cam->getScreenRayDirZ();
    cam->screenToRay(140.f, 60.f, 160.f, 120.f);

    CHECK(cam->getScreenRayOriginX() > leftOrigin + 1.f);
    CHECK(std::fabs(cam->getScreenRayDirX() - leftDirX) < 1e-4f);
    CHECK(std::fabs(cam->getScreenRayDirZ() - leftDirZ) < 1e-4f);
    CHECK(cam->getScreenRayDirZ() < -0.99f);
}

// ---------------------------------------------------------------------------
// GPU 验证：逐像素实体 ID mask（renderEntityIdMask）与通用纹理读回
// （readTextureToImageData）。这两条路径支撑 capture_render_frame 的
// "id" / "depth" / "normal" 缓冲区，必须在真实 Vulkan 设备上跑通。
// ---------------------------------------------------------------------------


TEST_CASE("RenderSystem3D.shadowExtraDrawerOwnerTokens") {
    using eve::graphics::RenderSystem3D;
    CHECK(RenderSystem3D::addShadowExtraDrawer({}) == 0);
    const auto first  = RenderSystem3D::addShadowExtraDrawer([](auto&, const auto&, const auto&) {});
    const auto second = RenderSystem3D::addShadowExtraDrawer([](auto&, const auto&, const auto&) {});
    REQUIRE(first != 0);
    REQUIRE(second != 0);
    CHECK(first != second);
    RenderSystem3D::removeShadowExtraDrawer(first);
    RenderSystem3D::removeShadowExtraDrawer(first);
    RenderSystem3D::removeShadowExtraDrawer(second);
    RenderSystem3D::removeShadowExtraDrawer(0);
}
