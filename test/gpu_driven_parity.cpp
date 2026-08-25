#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#include "Fixtures.h"

#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "window/Window.h"

#ifdef EVENGINE_WEBGPU
#include "graphics/webgpu/Graphics.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace eve::graphics;

namespace {

std::vector<float> captureLuma(Graphics *gfx) {
    for (int i = 0; i < 4; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    const int width = gfx->getWidth();
    const int height = gfx->getHeight();
    std::vector<float> pixels;
    pixels.reserve(size_t(width / 4) * size_t(height / 4));
    for (int y = 0; y < height; y += 4) {
        for (int x = 0; x < width; x += 4) {
            const Color c = gfx->getPixel(x, y);
            pixels.push_back((c.r + c.g + c.b) / 3.f);
        }
    }
    return pixels;
}

}  // namespace

/** @brief Backend-neutral parity for opaque GPU culling and indirect submission. */
TEST_CASE("GpuDrivenParity.opaqueStage1") {
    eve::window::Window *window = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(window, gfx, 320, 240);
    REQUIRE(gfx->supportsGpuDriven3D());

    auto *camera = Camera3D::createCamera();
    camera->setEye(0.f, 3.5f, 5.5f);
    camera->setTarget(0.f, 0.f, 0.f);
    camera->setAmbient(0.08f, 0.08f, 0.1f);

    const uint8_t red[4] = {210, 65, 55, 255};
    const uint8_t green[4] = {55, 205, 85, 255};
    Mesh *mesh = gfx->newMeshSphere(24, 16);
    auto addBall = [&](float x, float z, const uint8_t *rgba) {
        Material *material = gfx->newMaterial();
        material->setAlbedoTexture(gfx->newTexture(1, 1, rgba));
        material->setRoughness(0.5f);
        auto *object = Renderable3D::create();
        object->setMesh(mesh);
        object->setMaterial(material);
        object->setPosition(x, 0.35f, z);
        object->setScale(0.55f, 0.55f, 0.55f);
    };
    addBall(-0.9f, 0.f, red);
    addBall(0.9f, 0.f, green);
    // Fully behind the first red ball from this camera. The previous-frame
    // depth test must remove it while preserving the legacy-equivalent image.
    addBall(-0.9f, -1.5f, red);
    addBall(0.f, 20.f, red);  // Behind the camera; must be rejected by culling.

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.55f, 1.f, 0.35f);
    sun->setColor(1.f, 1.f, 1.f, 2.5f);
    sun->setCastShadow(false);

    gfx->setScreenReadbackEnabled(true);
    RenderControl *control = gfx->getRenderControl();
    // Isolate opaque submission and reconstruction from temporal/post passes.
    control->disable("ao");
    control->disable("gi");
    control->disable("aa");
    control->disable("atmosphere");
    control->disable("msaa");
    control->disable("gpuDriven");
    const std::vector<float> legacy = captureLuma(gfx);
    control->enable("gpuDriven");
    const std::vector<float> driven = captureLuma(gfx);
    REQUIRE(gfx->gpuDrivenEnabled());
#ifdef EVENGINE_WEBGPU
    auto *webgpu = dynamic_cast<eve::graphics::webgpu::Graphics *>(gfx);
    REQUIRE(webgpu != nullptr);
    REQUIRE(webgpu->debugGpuDrivenVisibleCount() == 3);
    REQUIRE(webgpu->debugGpuDrivenHzbMipCount() > 1);
    REQUIRE(webgpu->debugGpuDrivenGpuVisibleCount() == 2);
    REQUIRE(webgpu->debugGpuDrivenDispatchCount() == 4);
    REQUIRE(webgpu->debugGpuDrivenIndirectDrawCount() == 2);
#endif
    REQUIRE(legacy.size() == driven.size());
    float maxDelta = 0.f;
    float legacySum = 0.f;
    float drivenSum = 0.f;
    size_t maxIndex = 0;
    for (size_t i = 0; i < legacy.size(); ++i)
    {
        const float delta = std::abs(legacy[i] - driven[i]);
        if (delta > maxDelta) {
            maxDelta = delta;
            maxIndex = i;
        }
        legacySum += legacy[i];
        drivenSum += driven[i];
    }
    std::printf("GpuDrivenParity forward maxDelta=%f index=%zu legacy=%f driven=%f "
                "legacySum=%f drivenSum=%f\n",
                maxDelta, maxIndex, legacy[maxIndex], driven[maxIndex], legacySum, drivenSum);
    REQUIRE(maxDelta < 0.03f);

    // Stage 3: disable MSAA (visibility attachments are single-sample), then
    // compare the non-indexed vis pass + fullscreen reconstruction against
    // the stage-2 forward indirect path.
    control->disable("visResolve");
    const std::vector<float> forward1x = captureLuma(gfx);
    control->enable("visResolve");
    const std::vector<float> resolved = captureLuma(gfx);
    REQUIRE(gfx->gpuDrivenResolveWanted());
    float resolveDelta = 0.f;
    for (size_t i = 0; i < forward1x.size(); ++i)
        resolveDelta = std::max(resolveDelta, std::abs(forward1x[i] - resolved[i]));
    REQUIRE(resolveDelta < 0.06f);
    control->disable("visResolve");
    window->close();
}

/** @brief Backend-neutral VG compute-cull, indirect-vis and resolve coverage. */
TEST_CASE("GpuDrivenParity.virtualGeometry") {
    eve::window::Window *window = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(window, gfx, 320, 240);
    REQUIRE(gfx->supportsGpuDriven3D());

    const float positions[] = {-1.1f, -0.9f, 0.f, 1.1f, -0.9f, 0.f, 0.f, 1.1f, 0.f};
    const float normals[] = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const uint32_t indices[] = {0, 1, 2};
    GpuVgCluster cluster{};
    auto bits = [](float value) {
        uint32_t out = 0;
        std::memcpy(&out, &value, sizeof(out));
        return out;
    };
    cluster.u0[0] = bits(0.f);
    cluster.u0[1] = bits(0.f);
    cluster.u0[2] = bits(0.f);
    cluster.u0[3] = bits(1.6f);
    cluster.u1[0] = 0;
    cluster.u1[1] = 1;
    GpuVgAssetUpload upload{};
    upload.positions = positions;
    upload.vertexCount = 3;
    upload.normals = normals;
    upload.triangles = indices;
    upload.triangleCount = 3;
    upload.clusters = &cluster;
    upload.clusterCount = 1;
    const uint32_t assetId = gfx->gpuDrivenVgUpload(upload);
    REQUIRE(assetId != kInvalidGpuDrivenSlot);

    Mesh *mesh = gfx->newMeshFromArrays(positions, normals, nullptr, 3, indices, 3);
    REQUIRE(mesh != nullptr);
    REQUIRE(gfx->gpuDrivenVgAttachToMesh(mesh, assetId));
    REQUIRE(gfx->gpuDrivenVgAssetId(mesh) == assetId);
    Material *material = gfx->newMaterial();
    material->setTint(0.9f, 0.2f, 0.1f);
    auto *object = Renderable3D::create();
    object->setMesh(mesh);
    object->setMaterial(material);

    auto *camera = Camera3D::createCamera();
    camera->setEye(0.f, 0.f, 3.2f);
    camera->setTarget(0.f, 0.f, 0.f);
    camera->setAmbient(0.35f, 0.35f, 0.35f);
    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.f, 0.f, -1.f);
    sun->setColor(1.f, 1.f, 1.f, 1.5f);
    sun->setCastShadow(false);

    gfx->setScreenReadbackEnabled(true);
    RenderControl *control = gfx->getRenderControl();
    control->disable("ao");
    control->disable("gi");
    control->disable("aa");
    control->disable("atmosphere");
    control->disable("msaa");
    control->enable("gpuDriven");
    // The visibility path owns its ID/bary/depth attachments; the legacy
    // material GBuffer pass is unrelated to this focused VG scene.
    control->disable("gbuffer");
    control->enable("visResolve");
    captureLuma(gfx);
    REQUIRE(gfx->gpuDrivenResolveWanted());
    const Color visible = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(visible.r > visible.b + 0.05f);
#ifdef EVENGINE_WEBGPU
    auto *webgpu = dynamic_cast<eve::graphics::webgpu::Graphics *>(gfx);
    REQUIRE(webgpu != nullptr);
    REQUIRE(webgpu->debugGpuDrivenVgDispatchCount() == 1);
    REQUIRE(webgpu->debugGpuDrivenVgIndirectDrawCount() == 1);
#endif

    control->disable("gpuDriven");
    control->disable("visResolve");
    window->close();
}
