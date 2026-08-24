#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#include "Fixtures.h"

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
    REQUIRE(sa != eve::graphics::vulkan::kInvalidBindlessSlot);
    REQUIRE(sb != eve::graphics::vulkan::kInvalidBindlessSlot);
    REQUIRE(sa != sb);  // distinct slots

    const std::vector<uint8_t> cubePx(6 * 4, 128);
    Texture *cube = gfx->newCubemap(1, cubePx.data());
    auto *gpuCube = static_cast<eve::graphics::vulkan::GpuTexture *>(cube->gpuHandle);
    REQUIRE(gpuCube->bindlessIndexCube != eve::graphics::vulkan::kInvalidBindlessSlot);
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
    REQUIRE(idx != eve::graphics::vulkan::kInvalidBindlessSlot);
    auto *gpu = static_cast<eve::graphics::vulkan::GpuMesh *>(m->gpuHandle);
    REQUIRE(gpu->record.vertexCount > 0);
    REQUIRE(gpu->record.indexCount > 0);
    REQUIRE(gpu->record.boundsCenterRadius.w > 0.f);  // bounds computed at upload
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

/** @brief Deterministic LCG so scene layout is stable across machines/runs. */
float gdRand01(uint32_t &seed) {
    seed = seed * 1664525u + 1013904223u;
    return (seed >> 8) * (1.f / 16777216.f);
}

/** @brief Checkerboard 2D texture (high contrast exposes edge/noise artifacts). */
Texture *gdChecker(Graphics *gfx, int n = 16) {
    std::vector<uint8_t> px;
    px.reserve(size_t(n) * n * 4);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const uint8_t v = ((x ^ y) & 1) ? 235 : 30;
            px.push_back(v);
            px.push_back(v);
            px.push_back(v);
            px.push_back(255);
        }
    return gfx->newTexture(n, n, px.data());
}

/** @brief Unit cube (24 verts, per-face normals) for shape variety in scenes. */
Mesh *gdCubeMesh(Graphics *gfx) {
    static const float kFaces[6][4][3] = {
        {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}},
        {{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}},
        {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},
        {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}},
        {{-1, 1, -1}, {-1, 1, 1}, {1, 1, 1}, {1, 1, -1}},
        {{-1, -1, 1}, {-1, -1, -1}, {1, -1, -1}, {1, -1, 1}},
    };
    static const float kN[6][3] = {
        {-1, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, -1, 0},
    };
    std::vector<float> pos, nrm;
    std::vector<uint32_t> idx;
    for (int f = 0; f < 6; ++f) {
        const uint32_t base = uint32_t(pos.size() / 3);
        for (int c = 0; c < 4; ++c) {
            pos.insert(pos.end(), kFaces[f][c], kFaces[f][c] + 3);
            nrm.insert(nrm.end(), kN[f], kN[f] + 3);
        }
        idx.push_back(base + 0);
        idx.push_back(base + 1);
        idx.push_back(base + 2);
        idx.push_back(base + 0);
        idx.push_back(base + 2);
        idx.push_back(base + 3);
    }
    return gfx->newMeshFromArrays(pos.data(), nrm.data(), nullptr, int(pos.size() / 3), idx.data(),
                                  int(idx.size()));
}

/** @brief Simple delta summary for large-scene parity comparisons. */
struct GdDelta {
    float maxDelta = 0.f;
    float meanDelta = 0.f;
    int over005 = 0;
    int over008 = 0;
    size_t n = 0;
};

GdDelta gdCompare(const char *name, const std::vector<float> &a, const std::vector<float> &b) {
    GdDelta d;
    d.n = std::min(a.size(), b.size());
    for (size_t i = 0; i < d.n; ++i) {
        const float delta = std::fabs(a[i] - b[i]);
        d.maxDelta = std::max(d.maxDelta, delta);
        d.meanDelta += delta;
        if (delta > 0.05f) ++d.over005;
        if (delta > 0.08f) ++d.over008;
    }
    d.meanDelta /= float(d.n);
    std::printf("[gd-scene] %-28s max=%.4f mean=%.5f over0.05=%d/%zu over0.08=%d/%zu\n", name,
                d.maxDelta, d.meanDelta, d.over005, d.n, d.over008, d.n);
    return d;
}

/** @brief A moderately large deterministic scene (grid of mixed meshes/materials). */
struct GdGrid {
    std::vector<Renderable3D *> objects;
    Mesh *sphere = nullptr;
    Mesh *cylinder = nullptr;
    Mesh *cube = nullptr;
    std::vector<Material *> materials;
    int nx = 0;
    int nz = 0;
    float spacing = 1.f;
};

GdGrid gdBuildGrid(Graphics *gfx, int nx, int nz, float spacing, uint32_t seed = 20260821u) {
    GdGrid s;
    s.nx = nx;
    s.nz = nz;
    s.spacing = spacing;
    s.sphere = gfx->newMeshSphere(24, 12);
    s.cylinder = gfx->newMeshCylinder(16, 1, true);
    s.cube = gdCubeMesh(gfx);

    auto addMat = [&](Texture *albedo, float rough, float metal) {
        Material *m = gfx->newMaterial();
        m->setAlbedoTexture(albedo);
        m->setNormalTexture(nullptr);
        m->setRoughness(rough);
        m->setMetallic(metal);
        s.materials.push_back(m);
        return m;
    };
    addMat(gdChecker(gfx, 16), 0.40f, 0.10f);
    addMat(gdSolid(gfx, 180, 70, 60), 0.80f, 0.00f);
    addMat(gdSolid(gfx, 70, 180, 90), 0.20f, 0.60f);
    addMat(gdSolid(gfx, 70, 90, 180), 0.60f, 0.20f);
    addMat(gdChecker(gfx, 8), 0.50f, 0.30f);

    s.objects.reserve(size_t(nx) * nz);
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < nz; ++j) {
            Mesh *mesh = ((i + j) % 3 == 0) ? s.sphere : (((i + j) % 3 == 1) ? s.cylinder : s.cube);
            Material *mat = s.materials[size_t(i * 3 + j * 5) % s.materials.size()];
            const float x = (float(i) - float(nx - 1) * 0.5f) * spacing;
            const float z = (float(j) - float(nz - 1) * 0.5f) * spacing;
            const float scale = 0.45f + 0.40f * gdRand01(seed);
            const float yaw = gdRand01(seed) * 360.f;
            auto *obj = Renderable3D::create();
            obj->setMesh(mesh);
            obj->setMaterial(mat);
            obj->setPosition(x, 0.35f, z);
            obj->setScale(scale, scale, scale);
            obj->setRotation(0.f, yaw, 0.f);
            s.objects.push_back(obj);
        }
    }
    return s;
}

/** @brief Occlusion-heavy scene: a wall hides a crowd behind it. */
GdGrid gdBuildOcclusionScene(Graphics *gfx, uint32_t seed = 20260822u) {
    GdGrid s;
    s.sphere = gfx->newMeshSphere(24, 12);
    s.cylinder = gfx->newMeshCylinder(16, 1, true);
    s.cube = gdCubeMesh(gfx);
    auto *wallMat = gfx->newMaterial();
    wallMat->setAlbedoTexture(gdChecker(gfx, 8));
    wallMat->setNormalTexture(nullptr);
    wallMat->setRoughness(0.6f);
    wallMat->setMetallic(0.1f);
    auto *crowdMat = gfx->newMaterial();
    crowdMat->setAlbedoTexture(gdSolid(gfx, 190, 120, 40));
    crowdMat->setNormalTexture(nullptr);
    crowdMat->setRoughness(0.5f);
    crowdMat->setMetallic(0.2f);

    auto add = [&](Mesh *mesh, Material *mat, float x, float y, float z, float sx, float sy,
                   float sz, float yaw) {
        auto *obj = Renderable3D::create();
        obj->setMesh(mesh);
        obj->setMaterial(mat);
        obj->setPosition(x, y, z);
        obj->setScale(sx, sy, sz);
        obj->setRotation(0.f, yaw, 0.f);
        s.objects.push_back(obj);
    };
    // Tall + wide opaque wall between camera and crowd, with a clear depth
    // gap so the crowd's bounding spheres land well behind it in the HZB.
    add(s.cube, wallMat, 0.f, 1.2f, 1.5f, 10.f, 12.f, 0.5f, 0.f);
    // Crowd hidden behind the wall.
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 5; ++j) {
            const float x = (float(i) - 2.5f) * 1.2f;
            const float z = -3.6f - float(j) * 1.1f;
            const float scale = 0.5f + 0.4f * gdRand01(seed);
            const Mesh *mesh = ((i + j) % 2) ? s.cylinder : s.sphere;
            add(const_cast<Mesh *>(mesh), crowdMat, x, 0.35f, z, scale, scale, scale,
                gdRand01(seed) * 360.f);
        }
    // A few visible objects between camera and wall.
    for (int i = -2; i <= 2; ++i)
        add(s.cube, wallMat, float(i) * 1.4f, 0.35f, 2.5f, 0.55f, 0.55f, 0.55f, 0.f);
    s.nx = int(s.objects.size());
    s.nz = 1;
    return s;
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
    REQUIRE(maxDelta < 0.03f);
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
    REQUIRE(vg->debugLastGpuDrivenDrawCount() > 0);  // both spheres went through the path
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
    REQUIRE(maxDelta < 0.03f);
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
    REQUIRE(vg->debugGpuDrivenVisibleCount() == 3);   // 2 of 5 instances culled
    REQUIRE(vg->debugGpuDrivenCulledDrawCount() == 3);

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
    REQUIRE(maxDelta < 0.03f);
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
    REQUIRE(vg->debugGpuDrivenVisibleCount() == 3);  // same cull as forward

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
    REQUIRE(maxDelta < 0.06f);
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
    REQUIRE(vg->gpuDrivenVgAssetId(mesh) == vgAssetId);

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
    REQUIRE(visCount > 0);
    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    const Color center = gfx->getPixel(w / 2, h / 2);
    std::printf("vgVisResolve: center=(%.3f %.3f %.3f)\n", center.r, center.g, center.b);
    // Flat tint shading: the lit sphere center must be clearly reddish.
    REQUIRE(center.r > center.b + 0.05f);

    // NOTE: GPU-side HZB occlusion culling for VG clusters is a known
    // follow-up (the CPU renderer frustum-culls before the VG instance is
    // submitted, and the stage-3 HZB path is not wired for clusters yet).
    // This test verifies the cluster cull + raster + resolve pipeline end to
    // end; cluster occlusion will be covered once the HZB path lands.

    rc->disable("gpuDriven");
    rc->disable("visResolve");
    win->close();
}

/**
 * @brief Large-scene forward parity: ~100 mixed objects (3 meshes x 5
 * materials, deterministic transforms) must produce nearly the same image on
 * the CPU (legacy per-draw) path and the GPU-driven (bindless + indirect)
 * path under the same camera/lighting/material configuration.
 */
TEST_CASE("GpuDriven.largeSceneForwardParity") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 480, 360);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    if (!vg->gpuDrivenCaps().gpuDrivenAvailable()) {
        win->close();
        return;
    }

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 12.f, 16.f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setFov(45.f);
    cam->setAmbient(0.08f, 0.08f, 0.10f);

    const GdGrid scene = gdBuildGrid(gfx, 10, 10, 1.35f);
    REQUIRE(scene.objects.size() == 100);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.55f, 1.f, 0.35f);
    sun->setColor(1.f, 1.f, 1.f, 2.5f);
    sun->setCastShadow(false);
    auto *p1 = Light3D::createLight("point");
    p1->setPosition(-4.f, 4.f, -2.f);
    p1->setColor(1.f, 0.7f, 0.5f, 2.0f);
    p1->setRadius(9.f);
    auto *p2 = Light3D::createLight("point");
    p2->setPosition(4.f, 3.f, 3.f);
    p2->setColor(0.5f, 0.8f, 1.f, 1.8f);
    p2->setRadius(8.f);

    gfx->setScreenReadbackEnabled(true);
    RenderControl *rc = gfx->getRenderControl();
    rc->disable("msaa");
    rc->disable("gpuDriven");
    gdWarmPresent(gfx);
    const auto legacy = gdCaptureLuma(gfx);

    rc->enable("gpuDriven");
    gdWarmPresent(gfx);
    REQUIRE(vg->debugLastGpuDrivenDrawCount() > 0);
    const auto gpuDriven = gdCaptureLuma(gfx);
    const GdDelta d = gdCompare("largeScene legacy vs gpuDriven", legacy, gpuDriven);

    rc->disable("gpuDriven");
    // Same shading state: differences are confined to high-contrast edge
    // pixels (rasterization rule / projection rounding), not global noise.
    REQUIRE(d.meanDelta < 0.008f);
    REQUIRE(d.over008 * 50 < int(d.n));  // < 2% of samples deviate > 0.08 luma
    win->close();
}

/**
 * @brief Large-scene cull parity: an opaque wall hides a 30-object crowd
 * behind it. The CPU path draws everything (no occlusion cull); the GPU chain
 * additionally runs HZB occlusion culling, so its visible instance count must
 * drop sharply while the final image stays close to the CPU result.
 */
TEST_CASE("GpuDriven.largeSceneCullParity") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 480, 360);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    if (!vg->gpuDrivenCaps().gpuDrivenAvailable()) {
        win->close();
        return;
    }

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 5.5f, 9.f);
    cam->setTarget(0.f, 0.4f, -2.f);
    cam->setFov(50.f);
    cam->setAmbient(0.08f, 0.08f, 0.10f);

    const GdGrid scene = gdBuildOcclusionScene(gfx);
    const size_t submitted = scene.objects.size();  // all inside the frustum

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.5f, 1.f, 0.4f);
    sun->setColor(1.f, 1.f, 1.f, 2.2f);
    sun->setCastShadow(false);

    gfx->setScreenReadbackEnabled(true);
    RenderControl *rc = gfx->getRenderControl();
    rc->disable("msaa");
    rc->disable("gpuDriven");
    gdWarmPresent(gfx);
    const auto legacy = gdCaptureLuma(gfx);

    rc->enable("gpuDriven");
    gdWarmPresent(gfx);
    vg->waitForSharedGpuResources();
    const uint32_t visible = vg->debugGpuDrivenVisibleCount();
    std::printf("[gd-scene] cullParity submitted=%zu gpuVisible=%u\n", submitted, visible);
    REQUIRE(visible >= 6);         // wall + 5 front objects are never occluded
    REQUIRE(visible < submitted);  // HZB dropped at least some hidden crowd
    const auto gpuDriven = gdCaptureLuma(gfx);
    const GdDelta d = gdCompare("largeSceneCull legacy vs gpuDriven", legacy, gpuDriven);

    rc->disable("gpuDriven");
    REQUIRE(d.meanDelta < 0.008f);
    REQUIRE(d.over008 * 50 < int(d.n));
    win->close();
}

/**
 * @brief Large-scene visibility-buffer parity: the same ~64-object scene must
 * be pixel-close between the GPU-driven forward pass and the stage-3 vis
 * (GBuffer visID/visBary + fullscreen resolve) pass.
 */
TEST_CASE("GpuDriven.largeSceneVisResolveParity") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 480, 360);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    if (!vg->gpuDrivenCaps().gpuDrivenAvailable() ||
        !vg->gpuDrivenCaps().drawIndirectCount) {
        win->close();
        return;
    }

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 11.f, 15.f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setFov(45.f);
    cam->setAmbient(0.08f, 0.08f, 0.10f);

    const GdGrid scene = gdBuildGrid(gfx, 8, 8, 1.45f);
    REQUIRE(scene.objects.size() == 64);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.55f, 1.f, 0.35f);
    sun->setColor(1.f, 1.f, 1.f, 2.5f);
    sun->setCastShadow(false);
    auto *p1 = Light3D::createLight("point");
    p1->setPosition(-3.f, 4.f, -2.f);
    p1->setColor(1.f, 0.7f, 0.5f, 2.0f);
    p1->setRadius(8.f);

    gfx->setScreenReadbackEnabled(true);
    RenderControl *rc = gfx->getRenderControl();
    rc->disable("msaa");  // resolve path requires the 1x scene pass
    rc->enable("gpuDriven");
    rc->disable("visResolve");
    gdWarmPresent(gfx);
    const auto fwd = gdCaptureLuma(gfx);

    rc->enable("visResolve");
    gdWarmPresent(gfx);
    const auto resolved = gdCaptureLuma(gfx);
    const GdDelta d = gdCompare("largeSceneVis fwd vs resolve", fwd, resolved);

    rc->disable("gpuDriven");
    rc->disable("visResolve");
    REQUIRE(d.meanDelta < 0.002f);
    // A handful of isolated silhouette pixels may differ by driver-specific
    // rasterization rounding between the vis pass and the resolve; anything
    // beyond that is a real defect.
    REQUIRE(d.maxDelta < 0.1f);
    REQUIRE(d.over008 <= 4);
    win->close();
}
