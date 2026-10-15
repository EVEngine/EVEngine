#include "Fixtures.h"
#include "SceneColor.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>
#include <assimp/matrix4x4.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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
#include "graphics/Shadow.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "medialoader/model/ModelLoader.h"
#include "window/Window.h"

#include <glm/gtc/matrix_transform.hpp>
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

namespace {

[[maybe_unused]] float luma(const Color &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }


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
            data->envIntensity = 0.f;
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

[[maybe_unused]] Texture *makeSolid(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

[[maybe_unused]] void addHud() {
    auto *hud = Renderable2D::create();
    hud->transform()->x = 0;
    hud->transform()->y = 0;
    hud->sprite()->width = 2;
    hud->sprite()->height = 2;
}

[[maybe_unused]] void warmPresent(Graphics *gfx, int frames = 2) {
    for (int i = 0; i < frames; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Delay(8);
    }
}

[[maybe_unused]] float meanLumaRegion(Graphics* gfx, int x0, int y0, int x1, int y1, int step = 4, bool sceneLinear = false) {
    double sum = 0.0;
    int n = 0;
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
    int n = 0;
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
    int n = 0;
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
    auto scene = loader.loadFromMemory(kCubeObj, sizeof(kCubeObj) - 1, ".obj");
    REQUIRE(!scene.empty());
    REQUIRE(scene->mNumMeshes >= 1);
    Mesh *m = gfx->newMeshFromAssimp(*scene->mMeshes[0]);
    REQUIRE(m != nullptr);
    return m;
}

}  // namespace


TEST_CASE("RenderScenes.clipSpace.perspectiveVulkanFlipsY") {
    const glm::mat4 gl = glm::perspectiveRH_ZO(1.0f, 1.0f, 0.1f, 100.f);
    const glm::mat4 vk = perspectiveVulkanRH_ZO(1.0f, 1.0f, 0.1f, 100.f);
    CHECK(std::fabs(gl[1][1] + vk[1][1]) < 1e-5f);
    CHECK(vk[1][1] < 0.f);
    CHECK(gl[1][1] > 0.f);
}
