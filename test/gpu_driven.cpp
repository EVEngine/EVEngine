#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/GpuDriven.h"
#include "window/Window.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace eve::graphics;

namespace {

void openGfxWindow(eve::window::Window *&win, Graphics *&gfx, int w = 320, int h = 240) {
    win = eve::window::Window::create();
    gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = w;
    s.height = h;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
}

}  // namespace

// Direct SDL Vulkan probe, independent of the engine init path. If this
// crashes, the problem is in SDL/driver state rather than Graphics::init.
TEST_CASE("GpuDriven.capsAvailable") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    CHECK(vg->gpuDrivenCaps().gpuDrivenAvailable());
    win->close();
}

TEST_CASE("GpuDriven.bindlessTextureRegistration") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    if (!vg->gpuDrivenCaps().gpuDrivenAvailable()) {
        win->close();
        return;  // environment without 1.2 features: nothing to validate
    }

    const uint8_t red[4] = {255, 0, 0, 255};
    const uint8_t green[4] = {0, 255, 0, 255};
    Texture *a = gfx->newTexture(1, 1, red);
    Texture *b = gfx->newTexture(1, 1, green);
    const uint32_t sa = vg->debugBindlessIndex(a);
    const uint32_t sb = vg->debugBindlessIndex(b);
    CHECK(sa != eve::graphics::vulkan::kInvalidBindlessSlot);
    CHECK(sb != eve::graphics::vulkan::kInvalidBindlessSlot);
    CHECK(sa != sb);  // distinct slots

    const std::vector<uint8_t> cubePx(6 * 4, 128);
    Texture *cube = gfx->newCubemap(1, cubePx.data());
    auto *gpuCube = static_cast<eve::graphics::vulkan::GpuTexture *>(cube->gpuHandle);
    CHECK(gpuCube->bindlessIndexCube != eve::graphics::vulkan::kInvalidBindlessSlot);
    win->close();
}

TEST_CASE("GpuDriven.meshTableRegistration") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    if (!vg->gpuDrivenCaps().gpuDrivenAvailable()) {
        win->close();
        return;
    }

    Mesh *m = gfx->newMeshSphere(8, 4);
    REQUIRE(m != nullptr);
    const uint32_t idx = vg->debugMeshRecordIndex(m);
    CHECK(idx != eve::graphics::vulkan::kInvalidBindlessSlot);
    auto *gpu = static_cast<eve::graphics::vulkan::GpuMesh *>(m->gpuHandle);
    CHECK(gpu->record.vertexCount > 0);
    CHECK(gpu->record.indexCount > 0);
    CHECK(gpu->record.boundsCenterRadius.w > 0.f);  // bounds computed at upload
    win->close();
}

namespace {

float gdLuma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }

void gdWarmPresent(Graphics *gfx) {
    for (int i = 0; i < 4; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
}

std::vector<float> gdCaptureLuma(Graphics *gfx) {
    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    std::vector<float> out(size_t(w) * size_t(h), 0.f);
    for (int y = 0; y < h; y += 4) {
        for (int x = 0; x < w; x += 4) {
            out[size_t(y * w + x)] = gdLuma(gfx->getPixel(x, y));
        }
    }
    return out;
}

Texture *gdSolid(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

}  // namespace

/**
 * @brief The opaque forward pass must produce the same image with the legacy
 * per-draw path and the GPU-driven (indirect + bindless) path.
 */
TEST_CASE("GpuDriven.opaqueForwardParity") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    if (!vg->gpuDrivenCaps().gpuDrivenAvailable()) {
        win->close();
        return;
    }

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 3.5f, 5.5f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.08f, 0.08f, 0.10f);

    Material *ballMat = gfx->newMaterial();
    ballMat->setAlbedoTexture(gdSolid(gfx, 205, 70, 60));  // single texture -> slot 0
    ballMat->setNormalTexture(nullptr);
    ballMat->setRoughness(0.5f);
    ballMat->setMetallic(0.1f);
    auto *ball = Renderable3D::create();
    ball->setMesh(gfx->newMeshSphere(24, 16));
    ball->setMaterial(ballMat);
    ball->setPosition(0.f, 0.35f, 0.f);
    ball->setScale(0.55f, 0.55f, 0.55f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.55f, 1.f, 0.35f);
    sun->setColor(1.f, 1.f, 1.f, 2.5f);
    sun->setCastShadow(false);  // isolate the direct-light path first

    gfx->setScreenReadbackEnabled(true);
    RenderControl *rc = gfx->getRenderControl();
    rc->disable("gpuDriven");
    gdWarmPresent(gfx);
    const auto legacy = gdCaptureLuma(gfx);

    rc->enable("gpuDriven");
    gdWarmPresent(gfx);
    const auto gpuDriven = gdCaptureLuma(gfx);

    // Same shading source, different emission path: allow small float noise.
    float maxDelta = 0.f;
    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    for (int y = 0; y < h; y += 4) {
        for (int x = 0; x < w; x += 4) {
            const size_t i = size_t(y * w + x);
            const float d = std::fabs(legacy[i] - gpuDriven[i]);
            maxDelta = std::max(maxDelta, d);
        }
    }
    rc->disable("gpuDriven");
    CHECK(maxDelta < 0.03f);
    win->close();
}

/**
 * @brief Multi-texture parity: two spheres with distinct bindless slots
 * (albedo slot 0 and slot 1) must produce the same image on the legacy path
 * and the GPU-driven path. Regression test for descriptor-array dynamic
 * indexing at element > 0.
 */
TEST_CASE("GpuDriven.opaqueForwardParityMultiTexture") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    if (!vg->gpuDrivenCaps().gpuDrivenAvailable()) {
        win->close();
        return;
    }

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 3.5f, 5.5f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.08f, 0.08f, 0.10f);

    auto makeBall = [&](Mesh *mesh, Texture *albedo, float x) {
        Material *mat = gfx->newMaterial();
        mat->setAlbedoTexture(albedo);
        mat->setNormalTexture(nullptr);
        mat->setRoughness(0.5f);
        mat->setMetallic(0.1f);
        auto *obj = Renderable3D::create();
        obj->setMesh(mesh);
        obj->setMaterial(mat);
        obj->setPosition(x, 0.35f, 0.f);
        obj->setScale(0.55f, 0.55f, 0.55f);
        return obj;
    };
    Mesh *shared = gfx->newMeshSphere(24, 16);
    Texture *red = gdSolid(gfx, 205, 70, 60);   // bindless slot 0
    Texture *green = gdSolid(gfx, 60, 205, 90); // bindless slot 1
    makeBall(shared, red, -1.2f);   // material 0 -> slot 0
    makeBall(shared, green, 1.2f);  // material 1 -> slot 1

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.55f, 1.f, 0.35f);
    sun->setColor(1.f, 1.f, 1.f, 2.5f);
    sun->setCastShadow(false);

    gfx->setScreenReadbackEnabled(true);
    RenderControl *rc = gfx->getRenderControl();
    rc->disable("gpuDriven");
    gdWarmPresent(gfx);
    const auto legacy = gdCaptureLuma(gfx);

    rc->enable("gpuDriven");
    gdWarmPresent(gfx);
    CHECK(vg->debugLastGpuDrivenDrawCount() > 0);  // both spheres went through the path
    const auto gpuDriven = gdCaptureLuma(gfx);

    float maxDelta = 0.f;
    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    for (int y = 0; y < h; y += 4) {
        for (int x = 0; x < w; x += 4) {
            const size_t i = size_t(y * w + x);
            const float d = std::fabs(legacy[i] - gpuDriven[i]);
            maxDelta = std::max(maxDelta, d);
        }
    }
    rc->disable("gpuDriven");
    CHECK(maxDelta < 0.03f);
    win->close();
}

/**
 * @brief Stage 2 cull parity: instances outside the frustum (behind the camera
 * or far outside the sides) must be culled by the GPU chain while the final
 * image stays pixel-identical to the legacy path (GPU-clipped geometry never
 * contributed pixels anyway).
 */
TEST_CASE("GpuDriven.opaqueForwardCullParity") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    if (!vg->gpuDrivenCaps().gpuDrivenAvailable()) {
        win->close();
        return;
    }

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 3.5f, 5.5f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.08f, 0.08f, 0.10f);

    auto makeBall = [&](Mesh *mesh, Texture *albedo, float x, float z) {
        Material *mat = gfx->newMaterial();
        mat->setAlbedoTexture(albedo);
        mat->setNormalTexture(nullptr);
        mat->setRoughness(0.5f);
        mat->setMetallic(0.1f);
        auto *obj = Renderable3D::create();
        obj->setMesh(mesh);
        obj->setMaterial(mat);
        obj->setPosition(x, 0.35f, z);
        obj->setScale(0.55f, 0.55f, 0.55f);
        return obj;
    };
    Mesh *shared = gfx->newMeshSphere(24, 16);
    Texture *albedo = gdSolid(gfx, 120, 140, 160);
    makeBall(shared, albedo, -1.2f, 0.f);  // visible
    makeBall(shared, albedo, 0.f, 0.f);    // visible
    makeBall(shared, albedo, 1.2f, 0.f);   // visible
    makeBall(shared, albedo, 0.f, 9.f);    // behind the camera -> culled
    makeBall(shared, albedo, -40.f, 0.f);  // outside the left frustum -> culled

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.55f, 1.f, 0.35f);
    sun->setColor(1.f, 1.f, 1.f, 2.5f);
    sun->setCastShadow(false);

    gfx->setScreenReadbackEnabled(true);
    RenderControl *rc = gfx->getRenderControl();
    rc->disable("gpuDriven");
    gdWarmPresent(gfx);
    const auto legacy = gdCaptureLuma(gfx);

    rc->enable("gpuDriven");
    gdWarmPresent(gfx);
    const auto gpuDriven = gdCaptureLuma(gfx);
    vg->waitForSharedGpuResources();
    CHECK(vg->debugGpuDrivenVisibleCount() == 3);   // 2 of 5 instances culled
    CHECK(vg->debugGpuDrivenCulledDrawCount() == 3);

    float maxDelta = 0.f;
    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    for (int y = 0; y < h; y += 4) {
        for (int x = 0; x < w; x += 4) {
            const size_t i = size_t(y * w + x);
            const float d = std::fabs(legacy[i] - gpuDriven[i]);
            maxDelta = std::max(maxDelta, d);
        }
    }
    rc->disable("gpuDriven");
    CHECK(maxDelta < 0.03f);
    win->close();
}

/**
 * @brief Stage 3 parity: the visibility-buffer path (GBuffer writes visID/
 * visBary, scene color pass runs a fullscreen resolve) must produce the same
 * image as the stage-2 forward shading, with the same GPU culling active.
 * Requires the 1x scene pass (resolve path gates on MSAA off).
 */
TEST_CASE("GpuDriven.visResolveParity") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    if (!vg->gpuDrivenCaps().gpuDrivenAvailable()) {
        win->close();
        return;
    }

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 3.5f, 5.5f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.08f, 0.08f, 0.10f);

    auto makeBall = [&](Mesh *mesh, Texture *albedo, float x, float z) {
        Material *mat = gfx->newMaterial();
        mat->setAlbedoTexture(albedo);
        mat->setNormalTexture(nullptr);
        mat->setRoughness(0.5f);
        mat->setMetallic(0.1f);
        auto *obj = Renderable3D::create();
        obj->setMesh(mesh);
        obj->setMaterial(mat);
        obj->setPosition(x, 0.35f, z);
        obj->setScale(0.55f, 0.55f, 0.55f);
        return obj;
    };
    Mesh *shared = gfx->newMeshSphere(24, 16);
    Texture *albedo = gdSolid(gfx, 120, 140, 160);
    makeBall(shared, albedo, -1.2f, 0.f);  // visible
    makeBall(shared, albedo, 0.f, 0.f);    // visible
    makeBall(shared, albedo, 1.2f, 0.f);   // visible
    makeBall(shared, albedo, 0.f, 9.f);    // behind the camera -> culled
    makeBall(shared, albedo, -40.f, 0.f);  // outside the left frustum -> culled

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.55f, 1.f, 0.35f);
    sun->setColor(1.f, 1.f, 1.f, 2.5f);
    sun->setCastShadow(false);

    gfx->setScreenReadbackEnabled(true);
    RenderControl *rc = gfx->getRenderControl();
    rc->disable("msaa");  // resolve path requires the 1x scene pass
    rc->disable("gpuDriven");
    gdWarmPresent(gfx);
    gdCaptureLuma(gfx);  // warm the 1x scene pass; legacy reference not needed

    rc->enable("gpuDriven");
    gdWarmPresent(gfx);
    const auto fwdGpu = gdCaptureLuma(gfx);

    rc->enable("visResolve");
    gdWarmPresent(gfx);
    const auto resolved = gdCaptureLuma(gfx);
    vg->waitForSharedGpuResources();
    CHECK(vg->debugGpuDrivenVisibleCount() == 3);  // same cull as forward

    float maxDelta = 0.f;
    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    for (int y = 0; y < h; y += 4) {
        for (int x = 0; x < w; x += 4) {
            const size_t i = size_t(y * w + x);
            const float d = std::fabs(fwdGpu[i] - resolved[i]);
            maxDelta = std::max(maxDelta, d);
        }
    }
    rc->disable("gpuDriven");
    rc->disable("visResolve");
    std::printf("GpuDriven.visResolveParity maxDelta=%f\n", maxDelta);
    CHECK(maxDelta < 0.06f);
    win->close();
}
