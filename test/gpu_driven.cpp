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
#include "virtualgeometry/Builder.h"
#include "virtualgeometry/VirtualGeometryAsset.h"
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

/** @brief UV sphere (radius r) with positions + normals + triangle indices. */
void gdSphere(float r, int slices, int stacks, std::vector<float> &pos,
              std::vector<float> &nrm, std::vector<uint32_t> &idx) {
    pos.clear();
    nrm.clear();
    idx.clear();
    for (int s = 0; s <= slices; ++s) {
        const float u = float(s) / float(slices);
        for (int t = 0; t <= stacks; ++t) {
            const float v = float(t) / float(stacks);
            const float theta = u * 6.2831853f;
            const float phi = v * 3.14159265f;
            const float x = std::sin(phi) * std::cos(theta);
            const float y = std::cos(phi);
            const float z = std::sin(phi) * std::sin(theta);
            pos.push_back(x * r);
            pos.push_back(y * r);
            pos.push_back(z * r);
            nrm.push_back(x);
            nrm.push_back(y);
            nrm.push_back(z);
        }
    }
    const int stride = stacks + 1;
    for (int s = 0; s < slices; ++s) {
        for (int t = 0; t < stacks; ++t) {
            const uint32_t i0 = uint32_t(s * stride + t);
            const uint32_t i1 = uint32_t(s * stride + t + 1);
            const uint32_t i2 = uint32_t((s + 1) * stride + t);
            const uint32_t i3 = uint32_t((s + 1) * stride + t + 1);
            idx.push_back(i0);
            idx.push_back(i2);
            idx.push_back(i1);
            idx.push_back(i1);
            idx.push_back(i2);
            idx.push_back(i3);
        }
    }
}

/** @brief Pack CPU VgCluster into the GPU GpuVgCluster layout (4 x uvec4). */
void gdPackVgClusters(const eve::virtualgeometry::VirtualGeometryAsset &asset,
                      std::vector<eve::graphics::GpuVgCluster> &out) {
    out.resize(asset.clusters.size());
    auto fb = [](float f) {
        union {
            float f;
            uint32_t u;
        } x;
        x.f = f;
        return x.u;
    };
    for (size_t i = 0; i < asset.clusters.size(); ++i) {
        const auto &c = asset.clusters[i];
        GpuVgCluster &g = out[i];
        g.u0[0] = fb(c.cx);
        g.u0[1] = fb(c.cy);
        g.u0[2] = fb(c.cz);
        g.u0[3] = fb(c.r);
        g.u1[0] = c.triStart;
        g.u1[1] = c.triCount;
        g.u1[2] = c.lodLevel;
        g.u1[3] = c.parent;
        g.u2[0] = fb(c.errorR);
        g.u2[1] = fb(c.errorRScreen);
        g.u2[2] = c.childCount;
        g.u2[3] = 0;
        for (int k = 0; k < 4; ++k) g.u3[k] = c.children[k];
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

/**
 * @brief Stage 3 VG: a mesh with an attached virtual-geometry asset is culled
 * (frustum + HZB) and hardware-rasterized into the vis buffer, then resolved
 * with flat material-tint shading. Verifies visible clusters and the resulting
 * pixels, plus frustum culling when the object moves behind the camera.
 */
TEST_CASE("GpuDriven.vgVisResolve") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    if (!vg->gpuDrivenCaps().gpuDrivenAvailable() ||
        !vg->gpuDrivenCaps().drawIndirectCount) {
        win->close();
        return;
    }

    // Shared geometry: UV sphere -> regular Mesh + VG cluster DAG.
    std::vector<float> pos, nrm;
    std::vector<uint32_t> idx;
    gdSphere(0.9f, 24, 12, pos, nrm, idx);
    eve::virtualgeometry::VirtualGeometryBuilder builder;
    eve::virtualgeometry::VirtualGeometryAsset asset;
    eve::virtualgeometry::VirtualGeometryBuilder::MeshInput in{
        int(pos.size() / 3), pos.data(), nrm.data(), idx.data(), int(idx.size())};
    REQUIRE(builder.build(in, eve::virtualgeometry::VirtualGeometryBuilder::Options{}, asset));
    REQUIRE(!asset.clusters.empty());
    REQUIRE(asset.totalTriangles() > 0);

    std::vector<eve::graphics::GpuVgCluster> packed;
    gdPackVgClusters(asset, packed);
    eve::graphics::GpuVgAssetUpload up{};
    up.positions = asset.positions.data();
    up.vertexCount = asset.vertexCount;
    up.normals = asset.normals.empty() ? nullptr : asset.normals.data();
    up.triangles = asset.triangles.data();
    up.triangleCount = int(asset.triangles.size());
    up.clusters = packed.data();
    up.clusterCount = int(packed.size());
    const uint32_t vgAssetId = vg->gpuDrivenVgUpload(up);
    REQUIRE(vgAssetId != eve::graphics::kInvalidGpuDrivenSlot);

    Mesh *mesh = gfx->newMeshFromArrays(pos.data(), nrm.data(), nullptr, int(pos.size() / 3),
                                        idx.data(), int(idx.size()));
    REQUIRE(mesh != nullptr);
    REQUIRE(vg->gpuDrivenVgAttachToMesh(mesh, vgAssetId));
    CHECK(vg->gpuDrivenVgAssetId(mesh) == vgAssetId);

    Material *mat = gfx->newMaterial();
    mat->setTint(0.9f, 0.2f, 0.1f);
    auto *obj = Renderable3D::create();
    obj->setMesh(mesh);
    obj->setMaterial(mat);
    obj->setPosition(0.f, 0.f, 0.f);
    obj->setScale(1.15f, 1.15f, 1.15f);

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 3.2f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.15f, 0.15f, 0.17f);
    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.4f, 1.f, 0.3f);
    sun->setColor(1.f, 1.f, 1.f, 2.2f);
    sun->setCastShadow(false);

    gfx->setScreenReadbackEnabled(true);
    RenderControl *rc = gfx->getRenderControl();
    rc->disable("msaa");
    rc->enable("gpuDriven");
    rc->enable("visResolve");
    gdWarmPresent(gfx);
    vg->waitForSharedGpuResources();
    std::printf("vgVisResolve: resolveWanted=%d cullEnabled=%d drawCount=%u\n",
                int(vg->gpuDrivenResolveWanted()), int(vg->gpuDrivenCullEnabled()),
                vg->debugLastGpuDrivenDrawCount());
    std::printf("vgVisResolve: rc== %d visResolve=%d msaa=%d gpuDriven=%d\n",
                int(rc == gfx->getRenderControl()), int(rc->isEnabled("visResolve")),
                int(rc->isEnabled("msaa")), int(rc->isEnabled("gpuDriven")));

    const uint32_t visCount = vg->debugGpuDrivenVgVisibleCount();
    std::printf("vgVisResolve: visible clusters=%u\n", visCount);
    CHECK(visCount > 0);
    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    const Color center = gfx->getPixel(w / 2, h / 2);
    std::printf("vgVisResolve: center=(%.3f %.3f %.3f)\n", center.r, center.g, center.b);
    // Flat tint shading: the lit sphere center must be clearly reddish.
    CHECK(center.r > center.b + 0.05f);

    // Frustum cull: move the object behind the camera -> no visible clusters.
    obj->setPosition(0.f, 0.f, 40.f);
    gdWarmPresent(gfx);
    vg->waitForSharedGpuResources();
    CHECK(vg->debugGpuDrivenVgVisibleCount() == 0);

    rc->disable("gpuDriven");
    rc->disable("visResolve");
    win->close();
}
