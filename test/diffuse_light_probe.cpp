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

TEST_CASE("graphics.lightProbe.generatedBatchBlendsAndRemovesAtomically") {
    ProcgenProbeDesc red;
    red.sourcePointId = 1;
    red.type = 1;
    red.x = -1.f;
    red.extentX = red.extentY = red.extentZ = 4.f;
    red.irradianceR = 1.f;
    red.irradianceG = red.irradianceB = 0.f;
    ProcgenProbeDesc blue = red;
    blue.sourcePointId = 2;
    blue.x = 1.f;
    blue.irradianceR = 0.f;
    blue.irradianceB = 1.f;

    auto& registry = DiffuseLightProbeRegistry::instance();
    registry.replaceBatch("light-probe-test", {red, blue});
    auto sample = registry.sample(glm::vec3(0.f), 4);
    REQUIRE(sample.ok());
    const glm::vec3 irradiance = glm::vec3(sample.value().coefficients[0]) * 0.2820947918f;
    CHECK(irradiance.r > 0.49f);
    CHECK(irradiance.r < 0.51f);
    CHECK(irradiance.b > 0.49f);
    CHECK(irradiance.b < 0.51f);
    CHECK(!registry.sample(glm::vec3(20.f), 4).ok());
    registry.removeBatch("light-probe-test");
    CHECK(!registry.sample(glm::vec3(0.f), 4).ok());
}

TEST_CASE("graphics.lightProbe.proxyVolumeSelectsBracketingGridCell") {
    std::vector<ProcgenProbeDesc> probes;
    std::uint64_t id = 100;
    for (float z : {-1.f, 1.f})
        for (float y : {-1.f, 1.f})
            for (float x : {-2.f, 0.f, 2.f}) {
                ProcgenProbeDesc probe;
                probe.sourcePointId = id++;
                probe.type = 1;
                probe.x = x;
                probe.y = y;
                probe.z = z;
                probe.extentX = probe.extentY = probe.extentZ = 2.f;
                probe.irradianceR = (x + 2.f) * 0.25f;
                probes.push_back(probe);
            }
    auto& registry = DiffuseLightProbeRegistry::instance();
    registry.replaceBatch("regular-grid-cell-test", probes);
    auto selected = registry.selectVolume(glm::vec3(0.5f, 0.25f, -0.25f));
    REQUIRE(selected.ok());
    CHECK(selected.value().trilinearCell);
    CHECK_EQ(selected.value().count, 8);
    for (int index = 0; index < selected.value().count; ++index) {
        const glm::vec3 p(selected.value().positions[static_cast<size_t>(index)]);
        CHECK((p.x == 0.f || p.x == 2.f));
        CHECK((p.y == -1.f || p.y == 1.f));
        CHECK((p.z == -1.f || p.z == 1.f));
    }
    registry.removeBatch("regular-grid-cell-test");
}

TEST_CASE("graphics.lightProbe.incompleteProxyGridUsesSparseFallback") {
    std::vector<ProcgenProbeDesc> probes;
    std::uint64_t id = 200;
    for (const glm::vec3 position : {glm::vec3(-1.f, -1.f, 0.f), glm::vec3(1.f, -1.f, 0.f),
                                     glm::vec3(-1.f, 1.f, 0.f)}) {
        ProcgenProbeDesc probe;
        probe.sourcePointId = id++;
        probe.type = 1;
        probe.x = position.x;
        probe.y = position.y;
        probe.z = position.z;
        probe.extentX = probe.extentY = probe.extentZ = 2.f;
        probes.push_back(probe);
    }
    auto& registry = DiffuseLightProbeRegistry::instance();
    registry.replaceBatch("incomplete-grid-test", probes);
    auto selected = registry.selectVolume(glm::vec3(0.f));
    REQUIRE(selected.ok());
    CHECK(!selected.value().trilinearCell);
    CHECK_EQ(selected.value().count, 3);
    registry.removeBatch("incomplete-grid-test");
}

TEST_CASE("graphics.lightProbe.customProvidedRejectsInvalidAndOwnsValue") {
    auto* renderable = Renderable3D::create();
    REQUIRE(renderable);
    CHECK(!renderable->setCustomLightProbe(-1.f, 0.f, 0.f).ok());
    CHECK(!renderable->setCustomLightProbe(std::numeric_limits<float>::infinity(), 0.f, 0.f).ok());
    REQUIRE(renderable->setCustomLightProbe(0.25f, 0.5f, 0.75f).ok());
    CHECK(renderable->meshRenderer()->customLightProbe);
    CHECK(std::abs(renderable->meshRenderer()->customLightProbeSh[0].x * 0.2820947918f - 0.25f) < 1e-5f);
    REQUIRE(renderable->setCustomLightProbeCoefficient(3, 0.5f, -0.25f, 0.1f).ok());
    CHECK(glm::vec3(renderable->meshRenderer()->customLightProbeSh[3]) == glm::vec3(0.5f, -0.25f, 0.1f));
    CHECK(!renderable->setCustomLightProbeCoefficient(9, 0.f, 0.f, 0.f).ok());
    renderable->clearCustomLightProbe();
    CHECK(!renderable->meshRenderer()->customLightProbe);
    ecs::DestroyEntity(renderable);
}

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
