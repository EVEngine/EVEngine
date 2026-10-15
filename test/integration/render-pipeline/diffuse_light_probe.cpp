#include "common/ECS.h"
#include "common/ProcgenProbeSink.h"
#include "graphics/DiffuseLightProbeRegistry.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "image/ImageData.h"
#include <zeroerr/unittest.h>
#include <array>
#include <cmath>
#include <limits>
#include <memory>

using namespace eve;
using namespace eve::graphics;


TEST_CASE("graphics.lightProbe.customProvidedChangesRenderedPixels") {
    auto* gfx = Graphics::create();
    REQUIRE(gfx);
    gfx->initHeadless(96, 96);
    Canvas* canvas = gfx->newCanvas(96, 96);
    REQUIRE(canvas);
    const float positions[] = {-0.8f, -0.7f, 0.f, 0.8f, -0.7f, 0.f, 0.f, 0.8f, 0.f};
    const float normals[] = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const uint32_t indices[] = {0, 1, 2, 2, 1, 0};
    Mesh* mesh = gfx->newMeshFromArrays(positions, nullptr, normals, 3, indices, 6);
    REQUIRE(mesh);
    auto* camera = Camera3D::createCamera();
    camera->setEye(0.f, 0.f, 3.f);
    camera->setTarget(0.f, 0.f, 0.f);
    camera->setAmbient(0.f, 0.f, 0.f);
    auto* renderable = Renderable3D::create();
    renderable->setMeshLod(0, mesh);
    REQUIRE(renderable->setMeshLodRendererState(0, 0, 1, true, 1, true, 4, 0).ok());

    std::array<unsigned long long, 3> totals{};
    for (int pass = 0; pass < 2; ++pass) {
        REQUIRE(renderable->setCustomLightProbe(pass == 0 ? 1.f : 0.f, 0.f, pass == 0 ? 0.f : 1.f).ok());
        RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
        std::unique_ptr<eve::image::ImageData> pixels(canvas->newImageData());
        REQUIRE(pixels);
        const auto* data = static_cast<const unsigned char*>(pixels->getData());
        unsigned long long red = 0, blue = 0;
        for (size_t i = 0; i < 96u * 96u * 4u; i += 4) {
            red += data[i];
            blue += data[i + 2];
        }
        if (pass == 0) {
            totals[0] = red;
            totals[1] = blue;
        } else {
            totals[2] = blue;
            CHECK(red < totals[0]);
        }
    }
    CHECK(totals[0] > totals[1] + 1000);
    CHECK(totals[2] > totals[1] + 1000);
    unsigned long long directionalRed[2]{};
    for (int pass = 0; pass < 2; ++pass) {
        REQUIRE(renderable->setCustomLightProbe(0.45f, 0.f, 0.f).ok());
        REQUIRE(renderable->setCustomLightProbeCoefficient(2, pass == 0 ? 0.35f : -0.35f, 0.f, 0.f).ok());
        RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
        std::unique_ptr<eve::image::ImageData> pixels(canvas->newImageData());
        REQUIRE(pixels);
        const auto* data = static_cast<const unsigned char*>(pixels->getData());
        for (size_t i = 0; i < 96u * 96u * 4u; i += 4) directionalRed[pass] += data[i];
    }
    CHECK(directionalRed[0] > directionalRed[1] + 1000);

    ProcgenProbeDesc leftProbe;
    leftProbe.sourcePointId = 10;
    leftProbe.type = 1;
    leftProbe.x = -0.55f;
    leftProbe.extentX = leftProbe.extentY = leftProbe.extentZ = 2.f;
    leftProbe.irradianceR = 1.f;
    leftProbe.irradianceG = leftProbe.irradianceB = 0.f;
    ProcgenProbeDesc rightProbe = leftProbe;
    rightProbe.sourcePointId = 11;
    rightProbe.x = 0.55f;
    rightProbe.irradianceR = 0.f;
    rightProbe.irradianceB = 1.f;
    DiffuseLightProbeRegistry::instance().replaceBatch("pixel-volume-test", {leftProbe, rightProbe});
    REQUIRE(renderable->setMeshLodRendererState(0, 0, 1, true, 1, true, 2, 0).ok());
    RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> volumePixels(canvas->newImageData());
    REQUIRE(volumePixels);
    const auto* volumeData = static_cast<const unsigned char*>(volumePixels->getData());
    long long halfDominance[2]{};
    for (int y = 0; y < 96; ++y)
        for (int x = 0; x < 96; ++x) {
            const size_t offset = static_cast<size_t>(y * 96 + x) * 4;
            halfDominance[x >= 48] += static_cast<long long>(volumeData[offset]) - volumeData[offset + 2];
        }
    CHECK(std::abs(halfDominance[0]) > 1000);
    CHECK(std::abs(halfDominance[1]) > 1000);
    CHECK(halfDominance[0] * halfDominance[1] < 0);
    DiffuseLightProbeRegistry::instance().removeBatch("pixel-volume-test");
    ecs::DestroyEntity(renderable);
    ecs::DestroyEntity(camera);
}
