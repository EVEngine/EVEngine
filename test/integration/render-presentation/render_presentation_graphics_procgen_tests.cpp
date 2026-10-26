// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "render-window",
  "module": "particles",
  "modules": [
    "image",
    "particles",
    "procgen",
    "rpg"
  ],
  "origins": [
    "particles_gpu_resident.cpp",
    "particles_p2_p3.cpp",
    "particles_playback.cpp",
    "particles_reference_effects.cpp",
    "particles_scalability.cpp",
    "procgen.cpp",
    "procgen_lsystem.cpp",
    "procgen_preview.cpp",
    "resource_lifetime.cpp",
    "rpg_simulation.cpp",
    "procgen_pointset.cpp"
  ],
  "cases": {
    "render-offscreen": [
      "procgen.terrain.materialShaderRendersPortableSixteenLayerContract",
      "procgen.terrain.waterShaderShipsEmbeddedSpirv"
    ],
    "script-binding": [
      "procgen.script.newLSystemIsTheDocumentedEntryPoint",
      "procgen.script.pointsetPolygonAndBatchApiAreBound"
    ]
  }
}
*/
// clang-format on

// Related integration contracts share one translation unit to amortize header parsing.

// Contract section: particles gpu resident
#include "PrivateTestScope.h"
#include "zeroerr/assert.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include "PrivateTestScope.h"
#include "graphics/GpuParticles.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/RenderSystem.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleRuntime.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#include "window/Window.h"
#include "zeroerr/unittest.h"
// particles_gpu_resident.cpp
namespace test_particles_gpu_resident {
using namespace eve::particles;

namespace {

[[maybe_unused]] eve::graphics::Mesh *makeLeftHalfDepthPlane(eve::graphics::Graphics *gfx) {
    const std::array<float, 12> positions = {
        -1.f, -1.f, 0.5f, 0.f, -1.f, 0.5f, 0.f, 1.f, 0.5f, -1.f, 1.f, 0.5f,
    };
    const std::array<float, 12> normals = {
        0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f,
    };
    const std::array<float, 8>         uvs     = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const std::array<std::uint32_t, 6> indices = {0, 2, 1, 0, 3, 2};
    return gfx->newMeshFromArrays(positions.data(), normals.data(), uvs.data(), 4, indices.data(), 6);
}

[[maybe_unused]] eve::graphics::Mesh *makeFullscreenPlane(eve::graphics::Graphics *gfx) {
    const std::array<float, 12> positions = {
        -1.f, -1.f, 0.5f, 1.f, -1.f, 0.5f, 1.f, 1.f, 0.5f, -1.f, 1.f, 0.5f,
    };
    const std::array<float, 12> normals = {
        0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f,
    };
    const std::array<float, 8>         uvs     = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const std::array<std::uint32_t, 6> indices = {0, 2, 1, 0, 3, 2};
    return gfx->newMeshFromArrays(positions.data(), normals.data(), uvs.data(), 4, indices.data(), 6);
}

[[maybe_unused]] float colorDistance(const Color &a, const Color &b) {
    const float dr = a.r - b.r;
    const float dg = a.g - b.g;
    const float db = a.b - b.b;
    return std::sqrt(dr * dr + dg * dg + db * db);
}

}  // namespace


TEST_CASE("particles.gpu.residentSimulationAndIndirectRendering") {
    auto *window = eve::window::Window::create();
    auto *gfx    = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 800;
    settings.height   = 520;
    settings.centered = true;
    settings.vsync    = 0;
    REQUIRE(window->setWindowSettings(settings));
    REQUIRE(gfx->gpuParticles());
    REQUIRE(gfx->gpuParticles()->supportsGpuParticles());
    gfx->setScreenReadbackEnabled(true);

    const std::uint8_t whitePixel[4] = {255, 255, 255, 255};
    auto              *whiteTexture  = gfx->newTexture(1, 1, whitePixel);
    REQUIRE(whiteTexture != nullptr);

    const std::array<std::array<float, 3>, 6> colors{{
        {0.2f, 0.75f, 1.f},
        {0.45f, 0.25f, 1.f},
        {1.f, 0.18f, 0.5f},
        {1.f, 0.55f, 0.08f},
        {0.3f, 1.f, 0.5f},
        {0.95f, 0.95f, 0.3f},
    }};
    std::vector<ParticleEmitter *>            emitters;
    emitters.reserve(colors.size());
    for (std::size_t i = 0; i < colors.size(); ++i) {
        auto *emitter = Particles::create()->newEmitter(8192);
        emitter->setTexture(whiteTexture);
        emitter->setRandomSeed(20260826 + int(i));
        emitter->setGpuSimulation(true);
        emitter->setPosition(115.f + float(i) * 114.f, 400.f);
        emitter->setEmissionRate(0.f);
        emitter->setMaxSpawnPerFrame(256);
        emitter->setParticleLifetime(0.3f, 0.4f);
        emitter->setParticleSize(9.f, 18.f);
        emitter->setSizes(1.f, 0.12f);
        emitter->setSpeed(95.f, 245.f);
        emitter->setSpread(6.2831853f);
        emitter->setGravity(0.f, 105.f);
        emitter->setDamping(0.16f);
        emitter->setSpin(-4.f, 4.f);
        if (i % 3 == 1) {
            emitter->setRenderMode("axis");
            emitter->setRenderAxis(i % 2 == 0 ? 35.f : -35.f);
        } else if (i % 3 == 2) {
            emitter->setRenderMode("velocity", 0.075f);
        }
        emitter->setBlendMode("additive");
        emitter->setColorStart(colors[i][0], colors[i][1], colors[i][2], 0.95f);
        emitter->setColorEnd(colors[i][0] * 0.25f, colors[i][1] * 0.12f, colors[i][2] * 0.3f, 0.f);
        emitter->addBurst(0.f, 256);
        emitter->start();
        emitters.push_back(emitter);
    }

    gfx->setBackgroundColorRGBA(0.008f, 0.012f, 0.035f, 1.f);
    constexpr float dt = 1.f / 60.f;
    for (int frame = 0; frame < 8; ++frame) {
        ParticleSimSystem::update(dt);
        gfx->clearScreen();
        gfx->drawSolidRectRGBA(24.f, 22.f, 752.f, 476.f, 0.015f, 0.025f, 0.075f, 1.f);
        ParticleRenderSystem::render(gfx);
        gfx->present();
    }

    std::uint64_t submittedFrames   = 0;
    std::uint32_t delayedAlive      = 0;
    std::uint32_t indirectInstances = 0;
    int           estimatedAlive    = 0;
    for (auto *emitter : emitters) {
        auto gpu = emitter->gpuSim();
        REQUIRE(gpu->residentActive);
        REQUIRE(emitter->isGpuSimulationActive());
        REQUIRE_NE(gpu->residentHandle, std::uint64_t(0));
        REQUIRE_EQ(emitter->sim()->alive, 0);
        const auto gpuStats = gfx->gpuParticles()->getGpuParticleStats(gpu->residentHandle);
        submittedFrames += gpuStats.submittedFrames;
        delayedAlive += gpuStats.alive;
        indirectInstances += gpuStats.instances;
        estimatedAlive += gpu->estimatedAlive;
    }
    REQUIRE_GE(submittedFrames, std::uint64_t(24));
    REQUIRE_GT(delayedAlive, std::uint32_t(1000));
    REQUIRE_GT(indirectInstances, std::uint32_t(1000));
    REQUIRE_GT(estimatedAlive, 1000);
    REQUIRE_GT(particleFrameStats().renderedParticles, 1000);
    REQUIRE_EQ(particleFrameStats().gpuResidentEmitters, 6);
    REQUIRE_GT(particleFrameStats().gpuResidentParticles, 1000);

    const std::string output = std::string(EVENGINE_TEST_BINARY_DIR) + "/particle_gpu_resident.png";
    CHECK(gfx->saveFramePng(output));
    CHECK(std::filesystem::exists(output));
    REQUIRE(std::filesystem::file_size(output) > std::uintmax_t(4096));

    // Enabling a gameplay-coupled feature at runtime must not be silently
    // ignored by a previously activated GPU emitter. It resets to CPU safely.
    emitters.front()->setCollision("bounce", 2.f, 0.5f, 0.f);
    emitters.front()->setEmissionRate(240.f);
    ParticleSimSystem::update(dt);
    REQUIRE(!emitters.front()->isGpuSimulationActive());
    REQUIRE_GT(emitters.front()->sim()->alive, 0);

    for (auto *emitter : emitters) {
        emitter->setVisible(false);
        emitter->setGpuSimulation(false);
    }
    window->close();
}

TEST_CASE("particles.gpu.residentSortModesStayOnGpuAndRender") {
    auto *window = eve::window::Window::create();
    auto *gfx    = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 640;
    settings.height   = 360;
    settings.centered = true;
    settings.vsync    = 0;
    REQUIRE(window->setWindowSettings(settings));
    REQUIRE(gfx->gpuParticles());
    REQUIRE(gfx->gpuParticles()->supportsGpuParticles());
    gfx->setScreenReadbackEnabled(true);

    const std::uint8_t whitePixel[4] = {255, 255, 255, 255};
    auto              *texture       = gfx->newTexture(1, 1, whitePixel);
    REQUIRE(texture != nullptr);
    auto *camera = eve::graphics::Camera2D::createCamera();
    REQUIRE(camera != nullptr);
    camera->data()->x    = 320.f;
    camera->data()->y    = 180.f;
    camera->data()->zoom = 1.f;

    const std::array<const char *, 3> modes{"oldest", "youngest", "distance"};
    const std::array<float, 3>        xs{160.f, 320.f, 480.f};
    std::array<ParticleEmitter *, 3>  emitters{};
    for (std::size_t i = 0; i < emitters.size(); ++i) {
        auto *emitter = Particles::create()->newEmitter(256);
        emitter->setTexture(texture);
        emitter->setRandomSeed(9100 + int(i));
        emitter->setGpuSimulation(true);
        emitter->setSortMode(modes[i]);
        emitter->setCamera(camera);
        emitter->setPosition(xs[i], 180.f);
        emitter->setEmissionRate(240.f);
        emitter->setMaxSpawnPerFrame(16);
        emitter->setParticleLifetime(1.2f, 1.8f);
        emitter->setParticleSize(28.f, 36.f);
        emitter->setSizes(1.f, 0.2f);
        emitter->setSpeed(20.f, 80.f);
        emitter->setSpread(6.2831853f);
        emitter->setBlendMode("alpha");
        emitter->setColorStart(0.2f + 0.3f * float(i), 0.85f, 1.f - 0.25f * float(i), 0.9f);
        emitter->setColorEnd(0.1f, 0.2f, 0.4f, 0.f);
        REQUIRE(emitter->isGpuFeatureSetSupported());
        // Eligible for resident GPU; activation happens on the first sim submit.
        REQUIRE_EQ(std::string(emitter->getGpuFallbackReason()), std::string("pending_activation"));
        emitter->start();
        emitters[i] = emitter;
    }

    gfx->setBackgroundColorRGBA(0.01f, 0.015f, 0.04f, 1.f);
    for (int frame = 0; frame < 45; ++frame) {
        ParticleSimSystem::update(1.f / 60.f);
        gfx->clearScreen();
        ParticleRenderSystem::render(gfx);
        gfx->present();
    }

    std::uint32_t totalInstances = 0;
    for (std::size_t i = 0; i < emitters.size(); ++i) {
        REQUIRE(emitters[i]->isGpuSimulationActive());
        REQUIRE_EQ(emitters[i]->getSortMode(), std::string(modes[i]));
        REQUIRE_EQ(emitters[i]->getSimulationBackend(), std::string("gpu"));
        REQUIRE_EQ(std::string(emitters[i]->getGpuFallbackReason()), std::string());
        const auto stats = gfx->gpuParticles()->getGpuParticleStats(emitters[i]->gpuSim()->residentHandle);
        totalInstances += stats.instances;
        REQUIRE_GT(stats.submittedFrames, std::uint64_t(20));
    }
    REQUIRE_GT(totalInstances, std::uint32_t(30));
    REQUIRE_EQ(particleFrameStats().gpuResidentEmitters, 3);

    const auto left   = gfx->getPixel(160, 180);
    const auto middle = gfx->getPixel(320, 180);
    const auto right  = gfx->getPixel(480, 180);
    REQUIRE_GT(left.r + left.g + left.b, 0.15f);
    REQUIRE_GT(middle.r + middle.g + middle.b, 0.15f);
    REQUIRE_GT(right.r + right.g + right.b, 0.15f);

    const std::string output = std::string(EVENGINE_TEST_BINARY_DIR) + "/particle_gpu_sort_modes.png";
    CHECK(gfx->saveFramePng(output));
    CHECK(std::filesystem::exists(output));

    for (auto *emitter : emitters) {
        emitter->setGpuSimulation(false);
        emitter->release();
    }
    window->close();
}

TEST_CASE("particles.gpu.residentRibbonStaysOnGpuAndRendersSegments") {
    auto *window = eve::window::Window::create();
    auto *gfx    = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 640;
    settings.height   = 360;
    settings.centered = true;
    settings.vsync    = 0;
    REQUIRE(window->setWindowSettings(settings));
    REQUIRE(gfx->gpuParticles());
    REQUIRE(gfx->gpuParticles()->supportsGpuParticles());
    gfx->setScreenReadbackEnabled(true);

    const std::uint8_t whitePixel[4] = {255, 255, 255, 255};
    auto              *texture       = gfx->newTexture(1, 1, whitePixel);
    REQUIRE(texture != nullptr);

    auto *emitter = Particles::create()->newEmitter(128);
    emitter->setTexture(texture);
    emitter->setRandomSeed(4400);
    emitter->setGpuSimulation(true);
    emitter->setRibbon(0.8f, 2.f);
    emitter->setPosition(80.f, 180.f);
    emitter->setEmissionRate(0.f);
    emitter->setEmissionRateOverDistance(0.35f);
    emitter->setParticleLifetime(2.5f, 2.5f);
    emitter->setParticleSize(10.f, 10.f);
    emitter->setSizes(1.f, 1.f);
    emitter->setSpeed(0.f, 0.f);
    emitter->setSpread(0.f);
    emitter->setBlendMode("alpha");
    emitter->setColorStart(1.f, 0.35f, 0.1f, 0.95f);
    emitter->setColorEnd(1.f, 0.1f, 0.05f, 0.2f);
    REQUIRE(emitter->isGpuFeatureSetSupported());
    emitter->start();
    emitter->emit(1);

    gfx->setBackgroundColorRGBA(0.02f, 0.02f, 0.05f, 1.f);
    constexpr float dt = 1.f / 60.f;
    for (int frame = 0; frame < 12; ++frame) {
        emitter->setPosition(80.f + float(frame) * 35.f, 180.f + std::sin(float(frame) * 0.12f) * 40.f);
        ParticleSimSystem::update(dt);
        gfx->clearScreen();
        ParticleRenderSystem::render(gfx);
        gfx->present();
    }

    REQUIRE(emitter->isGpuSimulationActive());
    REQUIRE_EQ(emitter->getSimulationBackend(), std::string("gpu"));
    REQUIRE_EQ(std::string(emitter->getGpuFallbackReason()), std::string());
    const auto stats = gfx->gpuParticles()->getGpuParticleStats(emitter->gpuSim()->residentHandle);
    REQUIRE_GT(stats.instances, std::uint32_t(4));
    REQUIRE_GT(stats.submittedFrames, std::uint64_t(4));

    float brightest = 0.f;
    for (int y = 140; y <= 220; y += 10) {
        for (int x = 120; x <= 520; x += 20) {
            const auto pixel = gfx->getPixel(x, y);
            brightest        = std::max(brightest, pixel.r + pixel.g + pixel.b);
        }
    }
    REQUIRE_GT(brightest, 0.2f);

    const std::string output = std::string(EVENGINE_TEST_BINARY_DIR) + "/particle_gpu_ribbon.png";
    CHECK(gfx->saveFramePng(output));
    CHECK(std::filesystem::exists(output));

    emitter->setGpuSimulation(false);
    emitter->release();
    window->close();
}

TEST_CASE("particles.gpu.multipleResidentEmittersKeepIndependentDrawState") {
    auto *window = eve::window::Window::create();
    auto *gfx    = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 640;
    settings.height   = 360;
    settings.centered = true;
    settings.vsync    = 0;
    REQUIRE(window->setWindowSettings(settings));
    REQUIRE(gfx->gpuParticles());
    REQUIRE(gfx->gpuParticles()->supportsGpuParticles());
    gfx->setScreenReadbackEnabled(true);

    const std::uint8_t whitePixel[4] = {255, 255, 255, 255};
    auto              *texture       = gfx->newTexture(1, 1, whitePixel);
    REQUIRE(texture != nullptr);

    const std::array<float, 3>                positions{160.f, 320.f, 480.f};
    const std::array<std::array<float, 3>, 3> colors{{
        {1.f, 0.f, 0.f},
        {0.f, 1.f, 0.f},
        {0.f, 0.f, 1.f},
    }};
    std::array<ParticleEmitter *, 3>          emitters{};
    for (std::size_t i = 0; i < emitters.size(); ++i) {
        auto *emitter = Particles::create()->newEmitter(8);
        emitter->setTexture(texture);
        emitter->setRandomSeed(7200 + int(i));
        emitter->setGpuSimulation(true);
        emitter->setPosition(positions[i], 180.f);
        emitter->setEmissionRate(0.f);
        emitter->setParticleLifetime(10.f, 10.f);
        emitter->setParticleSize(64.f, 64.f);
        emitter->setSpeed(0.f, 0.f);
        emitter->setSpread(0.f);
        emitter->setBlendMode("alpha");
        emitter->setColorStart(colors[i][0], colors[i][1], colors[i][2], 1.f);
        emitter->setColorEnd(colors[i][0], colors[i][1], colors[i][2], 1.f);
        emitter->emit(1);
        emitters[i] = emitter;
    }

    gfx->setBackgroundColorRGBA(0.f, 0.f, 0.f, 1.f);
    for (int frame = 0; frame < 5; ++frame) {
        ParticleSimSystem::update(1.f / 60.f);
        gfx->clearScreen();
        ParticleRenderSystem::render(gfx);
        gfx->present();
    }

    for (std::size_t i = 0; i < emitters.size(); ++i) {
        REQUIRE(emitters[i]->isGpuSimulationActive());
        const auto stats = gfx->gpuParticles()->getGpuParticleStats(emitters[i]->gpuSim()->residentHandle);
        REQUIRE_EQ(stats.instances, std::uint32_t(1));
    }
    const auto red   = gfx->getPixel(160, 180);
    const auto green = gfx->getPixel(320, 180);
    const auto blue  = gfx->getPixel(480, 180);
    REQUIRE_GT(red.r, 0.8f);
    REQUIRE_LT(red.g + red.b, 0.2f);
    REQUIRE_GT(green.g, 0.8f);
    REQUIRE_LT(green.r + green.b, 0.2f);
    REQUIRE_GT(blue.b, 0.8f);
    REQUIRE_LT(blue.r + blue.g, 0.2f);

    const std::string output = std::string(EVENGINE_TEST_BINARY_DIR) + "/particle_gpu_multi_emitter.png";
    CHECK(gfx->saveFramePng(output));
    for (auto *emitter : emitters) {
        emitter->setGpuSimulation(false);
        emitter->release();
    }
    window->close();
}

TEST_CASE("particles.renderer.normalMappedLitMaterialUsesSceneLights") {
    auto *window = eve::window::Window::create();
    auto *gfx    = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 480;
    settings.height   = 280;
    settings.centered = true;
    settings.vsync    = 0;
    REQUIRE(window->setWindowSettings(settings));
    gfx->setScreenReadbackEnabled(true);

    const std::uint8_t albedoPixel[4] = {80, 80, 80, 255};
    const std::uint8_t normalPixel[4] = {128, 128, 255, 255};
    auto              *albedo         = gfx->newTexture(1, 1, albedoPixel);
    auto              *normal         = gfx->newTexture(1, 1, normalPixel);
    REQUIRE(albedo != nullptr);
    REQUIRE(normal != nullptr);

    auto makeEmitter = [&](float x, bool lit) {
        auto *emitter = Particles::create()->newEmitter(8);
        emitter->setTexture(albedo);
        emitter->setNormalTexture(normal);
        emitter->setMaterialMode(lit ? "lit" : "unlit");
        emitter->setGpuSimulation(lit);
        emitter->setPosition(x, 140.f);
        emitter->setEmissionRate(0.f);
        emitter->setParticleLifetime(10.f, 10.f);
        emitter->setParticleSize(104.f, 104.f);
        emitter->setSpeed(0.f, 0.f);
        emitter->setSpread(0.f);
        emitter->setBlendMode("alpha");
        emitter->setColorStart(1.f, 1.f, 1.f, 1.f);
        emitter->setColorEnd(1.f, 1.f, 1.f, 1.f);
        emitter->emit(1);
        return emitter;
    };
    auto *lit   = makeEmitter(150.f, true);
    auto *unlit = makeEmitter(330.f, false);

    auto *light = eve::graphics::Light2D::createLight("point");
    light->setPosition(150.f, 140.f);
    light->setColor(1.f, 0.75f, 0.45f, 4.f);
    light->setRadius(180.f);
    light->setEnabled(true);

    gfx->setBackgroundColorRGBA(0.005f, 0.007f, 0.012f, 1.f);
    for (int frame = 0; frame < 4; ++frame) {
        ParticleSimSystem::update(1.f / 60.f);
        gfx->clearScreen();
        ParticleRenderSystem::render(gfx);
        gfx->present();
    }

    REQUIRE(!lit->isGpuSimulationActive());
    const auto litPixel   = gfx->getPixel(150, 140);
    const auto unlitPixel = gfx->getPixel(330, 140);
    REQUIRE_GT(litPixel.r, unlitPixel.r + 0.2f);
    REQUIRE_GT(litPixel.r, litPixel.b + 0.1f);

    const std::string output = std::string(EVENGINE_TEST_BINARY_DIR) + "/particle_lit_material.png";
    CHECK(gfx->saveFramePng(output));
    CHECK(std::filesystem::exists(output));

    lit->release();
    unlit->release();
    light->setEnabled(false);
    window->close();
}

TEST_CASE("particles.renderer.distortionRefractsResolvedSceneColor") {
    auto *window = eve::window::Window::create();
    auto *gfx    = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 640;
    settings.height   = 360;
    settings.centered = true;
    settings.vsync    = 0;
    REQUIRE(window->setWindowSettings(settings));
    gfx->setScreenReadbackEnabled(true);

    constexpr int             width   = 640;
    constexpr int             height  = 360;
    constexpr int             effectX = 304;
    std::vector<std::uint8_t> stripePixels(std::size_t(width) * 4u);
    for (int x = 0; x < width; ++x) {
        const bool cyan  = (x / 32) % 2 == 0;
        auto      *pixel = &stripePixels[std::size_t(x) * 4u];
        pixel[0]         = cyan ? 12 : 218;
        pixel[1]         = cyan ? 188 : 18;
        pixel[2]         = cyan ? 218 : 184;
        pixel[3]         = 255;
    }
    auto *backgroundTexture = gfx->newTexture(width, 1, stripePixels.data());
    auto *scenePlane        = makeFullscreenPlane(gfx);
    REQUIRE(backgroundTexture != nullptr);
    REQUIRE(scenePlane != nullptr);
    eve::graphics::Lighting3DPack sceneLighting;
    sceneLighting.ambient = glm::vec4(1.f, 1.f, 1.f, 0.f);
    gfx->setMesh3DLighting(sceneLighting);
    gfx->setMesh3DMaterial(0.f, 1.f);

    constexpr int             fieldSize = 96;
    std::vector<std::uint8_t> fieldPixels(std::size_t(fieldSize * fieldSize) * 4u);
    for (int y = 0; y < fieldSize; ++y) {
        for (int x = 0; x < fieldSize; ++x) {
            const float nx      = (float(x) + 0.5f) / float(fieldSize) * 2.f - 1.f;
            const float ny      = (float(y) + 0.5f) / float(fieldSize) * 2.f - 1.f;
            const float feather = std::clamp((1.f - std::sqrt(nx * nx + ny * ny)) * 5.f, 0.f, 1.f);
            auto       *pixel   = &fieldPixels[std::size_t(y * fieldSize + x) * 4u];
            pixel[0]            = 255;
            pixel[1]            = 128;
            pixel[2]            = 128;
            pixel[3]            = std::uint8_t(feather * 255.f);
        }
    }
    auto *displacement = gfx->newTexture(fieldSize, fieldSize, fieldPixels.data());
    REQUIRE(displacement != nullptr);

    auto renderScene = [&] {
        gfx->begin3DFrame();
        gfx->drawMesh(scenePlane, glm::mat4(1.f), backgroundTexture, Color(1.f, 1.f, 1.f, 1.f));
        gfx->clearScreen();
    };

    renderScene();
    gfx->present();
    CHECK(gfx->saveFramePng(std::string(EVENGINE_TEST_BINARY_DIR) + "/particle_scene_distortion_control.png"));
    const Color source = gfx->getPixel(effectX, height / 2);
    const Color target = gfx->getPixel(effectX + 32, height / 2);
    REQUIRE_GT(colorDistance(source, target), 0.08f);

    auto *emitter = Particles::create()->newEmitter(4);
    emitter->setTexture(displacement);
    emitter->setMaterialMode("distortion");
    emitter->setDistortionStrength(32.f);
    emitter->setPosition(float(effectX), float(height / 2));
    emitter->setEmissionRate(0.f);
    emitter->setParticleLifetime(10.f, 10.f);
    emitter->setParticleSize(180.f, 180.f);
    emitter->setSpeed(0.f, 0.f);
    emitter->setSpread(0.f);
    emitter->setColorStart(1.f, 1.f, 1.f, 1.f);
    emitter->setColorEnd(1.f, 1.f, 1.f, 1.f);
    emitter->emit(1);

    ParticleSimSystem::update(1.f / 60.f);
    renderScene();
    ParticleRenderSystem::render(gfx);
    gfx->present();

    const Color refracted = gfx->getPixel(effectX, height / 2);
    CHECK_LT(colorDistance(refracted, target), colorDistance(refracted, source));
    CHECK_LT(colorDistance(refracted, target), 0.35f);

    const std::string output = std::string(EVENGINE_TEST_BINARY_DIR) + "/particle_scene_distortion.png";
    CHECK(gfx->saveFramePng(output));
    CHECK(std::filesystem::exists(output));
    REQUIRE(std::filesystem::file_size(output) > std::uintmax_t(4096));

    emitter->release();
    window->close();
}


TEST_CASE("particles.gpu.softDepthFadeUsesSceneLinearDepth") {
    auto *window = eve::window::Window::create();
    auto *gfx    = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 640;
    settings.height   = 360;
    settings.centered = true;
    settings.vsync    = 0;
    REQUIRE(window->setWindowSettings(settings));
    REQUIRE(gfx->gpuParticles());
    REQUIRE(gfx->gpuParticles()->supportsGpuParticles());
    gfx->setScreenReadbackEnabled(true);

    const std::uint8_t whitePixel[4] = {255, 255, 255, 255};
    const std::uint8_t darkPixel[4]  = {5, 8, 14, 255};
    auto              *particleTex   = gfx->newTexture(1, 1, whitePixel);
    auto              *planeTex      = gfx->newTexture(1, 1, darkPixel);
    auto              *depthPlane    = makeLeftHalfDepthPlane(gfx);
    REQUIRE(particleTex != nullptr);
    REQUIRE(planeTex != nullptr);
    REQUIRE(depthPlane != nullptr);

    auto makeEmitter = [&](float x) {
        auto *emitter = Particles::create()->newEmitter(16);
        emitter->setTexture(particleTex);
        emitter->setRandomSeed(20260826);
        emitter->setGpuSimulation(true);
        emitter->setPosition(x, 180.f);
        emitter->setEmissionRate(480.f);
        emitter->setMaxSpawnPerFrame(8);
        emitter->setParticleLifetime(10.f, 10.f);
        emitter->setParticleSize(112.f, 112.f);
        emitter->setSpeed(0.f, 0.f);
        emitter->setSpread(0.f);
        emitter->setBlendMode("alpha");
        emitter->setColorStart(1.f, 0.05f, 0.75f, 1.f);
        emitter->setColorEnd(1.f, 0.05f, 0.75f, 1.f);
        emitter->setSoftParticles(false, 0.5f, 0.05f);
        emitter->start();
        return emitter;
    };
    auto *occluded = makeEmitter(160.f);

    gfx->setBackgroundColorRGBA(0.004f, 0.006f, 0.014f, 1.f);
    for (int frame = 0; frame < 8; ++frame) {
        ParticleSimSystem::update(1.f / 60.f);
        gfx->beginGBufferPass(640, 360);
        gfx->drawMeshGBuffer(depthPlane, glm::mat4(1.f), glm::mat4(1.f), 0.1f, 100.f, planeTex);
        gfx->endGBufferPass();
        gfx->clearScreen();
        ParticleRenderSystem::render(gfx);
        gfx->present();
    }

    REQUIRE(occluded->isGpuSimulationActive());
    const auto        controlPixel   = gfx->getPixel(160, 180);
    const float       controlMagenta = controlPixel.r + controlPixel.b - controlPixel.g;
    const std::string controlOutput  = std::string(EVENGINE_TEST_BINARY_DIR) + "/particle_soft_depth_control.png";
    CHECK(gfx->saveFramePng(controlOutput));
    CHECK(std::filesystem::exists(controlOutput));

    occluded->setSoftParticles(true, 0.5f, 0.05f);
    bool observedSoftDepth = false;
    for (int frame = 0; frame < 4; ++frame) {
        ParticleSimSystem::update(1.f / 60.f);
        gfx->beginGBufferPass(640, 360);
        gfx->drawMeshGBuffer(depthPlane, glm::mat4(1.f), glm::mat4(1.f), 0.1f, 100.f, planeTex);
        gfx->endGBufferPass();
        gfx->clearScreen();
        ParticleRenderSystem::render(gfx);
        observedSoftDepth = observedSoftDepth || occluded->isSoftParticlesActive();
        gfx->present();
    }
    REQUIRE(observedSoftDepth);
    const auto  fadedPixel   = gfx->getPixel(160, 180);
    const float fadedMagenta = fadedPixel.r + fadedPixel.b - fadedPixel.g;
    CHECK_GT(controlMagenta, fadedMagenta + 0.7f);

    const std::string output = std::string(EVENGINE_TEST_BINARY_DIR) + "/particle_soft_depth.png";
    CHECK(gfx->saveFramePng(output));
    CHECK(std::filesystem::exists(output));
    REQUIRE(std::filesystem::file_size(output) > std::uintmax_t(100));

    occluded->setGpuSimulation(false);
    occluded->release();
    window->close();
}
}  // namespace test_particles_gpu_resident

// Contract section: particles p2 p3
#include "PrivateTestScope.h"

#include <fstream>
#include <memory>
#include <sstream>
#include "common/ParticleSdf.h"
#include "particles/ParticleEffect.h"
#include "particles/ParticleEmitterPool.h"
// particles_p2_p3.cpp
namespace test_particles_p2_p3 {
using namespace eve::particles;

namespace {

class CircleSdf final : public eve::IParticleSdfField {
public:
    CircleSdf(float x, float y, float radius) : x_(x), y_(y), radius_(radius) {}

    float sample(float x, float y) const override {
        const float dx = x - x_;
        const float dy = y - y_;
        return std::sqrt(dx * dx + dy * dy) - radius_;
    }

    eve::ParticleSdfGradientStatus gradient(float x, float y, float &outNx, float &outNy) const override {
        const float dx  = x - x_;
        const float dy  = y - y_;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-5f) {
            outNx = 0.f;
            outNy = 1.f;
            return eve::ParticleSdfGradientStatus::Defined;
        }
        outNx = dx / len;
        outNy = dy / len;
        return eve::ParticleSdfGradientStatus::Defined;
    }

private:
    float x_      = 0.f;
    float y_      = 0.f;
    float radius_ = 1.f;
};

[[maybe_unused]] eve::graphics::Texture *makeSoftDisc(eve::graphics::Graphics *gfx, int size) {
    std::vector<std::uint8_t> pixels(std::size_t(size * size) * 4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float nx = (float(x) + 0.5f) / float(size) * 2.f - 1.f;
            const float ny = (float(y) + 0.5f) / float(size) * 2.f - 1.f;
            const float r  = std::sqrt(nx * nx + ny * ny);
            const float a  = std::clamp(1.f - r, 0.f, 1.f);
            auto       *px = &pixels[std::size_t(y * size + x) * 4u];
            const auto  v  = std::uint8_t(a * 255.f);
            px[0]          = v;
            px[1]          = v;
            px[2]          = v;
            px[3]          = v;
        }
    }
    return gfx->newTexture(size, size, pixels.data());
}

[[maybe_unused]] std::string readFile(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

[[maybe_unused]] float luma(const Color &color) { return color.r * 0.2126f + color.g * 0.7152f + color.b * 0.0722f; }

}  // namespace


TEST_CASE("particles.reference.effectPackLoadsAndRenders") {
    auto *window = eve::window::Window::create();
    auto *gfx    = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 960;
    settings.height   = 540;
    settings.centered = true;
    settings.vsync    = 0;
    REQUIRE(window->setWindowSettings(settings));
    gfx->setScreenReadbackEnabled(true);

    auto *texture = makeSoftDisc(gfx, 48);
    REQUIRE(texture != nullptr);

    const std::string root    = std::string(EVENGINE_SOURCE_DIR) + "/examples/particle-effects/";
    const char       *names[] = {"fire.effect.json", "smoke.effect.json", "impact.effect.json", "trail.effect.json",
                                 "weather.effect.json"};
    std::vector<std::unique_ptr<ParticleEffect>> effects;
    constexpr float                              centersX[] = {120.f, 300.f, 480.f, 660.f, 840.f};
    for (int i = 0; i < 5; ++i) {
        auto parsed = ParticleEffect::tryFromText(readFile(root + names[i]), root + names[i]);
        REQUIRE(parsed.ok());
        effects.emplace_back(parsed.value());
        CHECK_EQ(effects.back()->getVersion(), 2);
        for (int e = 0; e < effects.back()->getEmitterCount(); ++e) {
            if (auto *emitter = effects.back()->getEmitter(e)) {
                emitter->setTexture(texture);
                emitter->setRandomSeed(20261001 + i * 17 + e);
                emitter->setAutoRandomSeed(false);
            }
        }
        effects.back()->setPosition(centersX[i], 270.f);
        effects.back()->setFloatParameter("intensity", 1.15f);
        effects.back()->start();
        if (std::string(names[i]) == "trail.effect.json") {
            // Distance emission needs motion to spawn ribbon control points.
            for (int step = 0; step < 12; ++step) {
                effects.back()->setPosition(centersX[i] + float(step) * 6.f, 270.f - float(step));
                Particles::create()->update(1.f / 60.f);
            }
        }
    }

    auto *particles = Particles::create();
    for (int frame = 0; frame < 24; ++frame) particles->update(1.f / 60.f);

    gfx->setBackgroundColorRGBA(0.015f, 0.02f, 0.035f, 1.f);
    for (int frame = 0; frame < 2; ++frame) {
        gfx->clearScreen();
        ParticleRenderSystem::render(gfx);
        gfx->present();
    }

    const std::string output = std::string(EVENGINE_TEST_BINARY_DIR) + "/particle_p2_reference_pack.png";
    CHECK(gfx->saveFramePng(output));
    CHECK(std::filesystem::exists(output));
    REQUIRE(std::filesystem::file_size(output) > std::uintmax_t(8000));

    float packPeak = 0.f;
    for (float x : centersX) {
        for (int y = 140; y <= 400; y += 8) {
            for (int sx = int(x) - 70; sx <= int(x) + 70; sx += 8) {
                packPeak = std::max(packPeak, luma(gfx->getPixel(sx, y)));
            }
        }
    }
    REQUIRE_GT(packPeak, 0.08f);

    for (auto &effect : effects) effect->stop();
    window->close();
}
}  // namespace test_particles_p2_p3

// Contract section: particles playback
#include "PrivateTestScope.h"

// particles_playback.cpp
namespace test_particles_playback {
using namespace eve::particles;

TEST_CASE("particles.playback.renderDistanceTrailAndLoopingBurst") {
    auto *window = eve::window::Window::create();
    auto *gfx    = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 640;
    settings.height   = 420;
    settings.centered = true;
    settings.vsync    = 0;
    REQUIRE(window->setWindowSettings(settings));
    gfx->setScreenReadbackEnabled(true);

    auto         *particles     = Particles::create();
    const uint8_t whitePixel[4] = {255, 255, 255, 255};
    auto         *whiteTexture  = gfx->newTexture(1, 1, whitePixel);
    auto         *trail         = particles->newEmitter(900);
    trail->setTexture(whiteTexture);
    trail->setRandomSeed(20260826);
    trail->setEmissionRateOverDistance(0.38f);
    trail->setParticleLifetime(0.65f, 1.15f);
    trail->setParticleSize(22.f, 22.f);
    trail->setSizes(1.f, 0.45f);
    trail->setSizeVariation(0.35f);
    trail->setSpeed(0.f, 0.f);
    trail->setSpread(6.2831853f);
    trail->setGravity(0.f, 0.f);
    trail->setDamping(0.25f);
    trail->setBlendMode("alpha");
    trail->setColorStart(0.35f, 0.9f, 1.f, 1.f);
    trail->setColorEnd(0.45f, 0.15f, 1.f, 0.35f);
    trail->setRibbon(0.55f, 2.f);
    trail->setFixedTimeStep(1.f / 120.f, 8);
    trail->setPosition(320.f, 210.f);
    trail->start();

    auto *burst = particles->newEmitter(600);
    burst->setTexture(whiteTexture);
    burst->setRandomSeed(424242);
    burst->setPosition(320.f, 210.f);
    burst->setEmitterLifetime(0.08f);
    burst->setLooping(true);
    burst->setEmissionRate(0.f);
    burst->addBurst(0.f, 72);
    burst->setParticleLifetime(0.7f, 1.4f);
    burst->setParticleSize(18.f, 18.f);
    burst->setSizes(1.2f, 0.4f);
    burst->setSpeed(80.f, 210.f);
    burst->setSpread(6.2831853f);
    burst->setGravity(0.f, 95.f);
    burst->setDamping(0.18f);
    burst->setBlendMode("alpha");
    burst->clearColorGradient();
    burst->addColorStop(0.f, 1.f, 0.96f, 0.65f, 1.f);
    burst->addColorStop(0.35f, 1.f, 0.35f, 0.08f, 0.9f);
    burst->addColorStop(1.f, 0.3f, 0.04f, 0.01f, 0.3f);
    burst->setFixedTimeStep(1.f / 120.f, 8);
    burst->start();

    gfx->setBackgroundColorRGBA(0.025f, 0.035f, 0.065f, 1.f);
    constexpr float dt = 1.f / 60.f;
    for (int frame = 0; frame < 24; ++frame) {
        const float t = float(frame) * dt;
        trail->setPosition(320.f + std::cos(t * 1.35f) * 240.f, 210.f + std::sin(t * 2.05f) * 125.f);
        ParticleSimSystem::update(dt);
        gfx->clearScreen();
        gfx->drawSolidRectRGBA(28.f, 26.f, 584.f, 368.f, 0.04f, 0.065f, 0.12f, 1.f);
        ParticleRenderSystem::render(gfx);
        gfx->present();
    }

    REQUIRE_GT(trail->getCount(), 20);
    REQUIRE_GT(burst->getCount(), 72);
    int particlesInView = 0;
    for (auto *emitter : {trail, burst}) {
        for (int i = 0; i < emitter->getCount(); ++i) {
            const Particle &p = emitter->sim()->particles[size_t(i)];
            if (p.x >= 0.f && p.x < 640.f && p.y >= 0.f && p.y < 420.f) ++particlesInView;
        }
    }
    REQUIRE_GT(particlesInView, 20);

    const std::string output = std::string(EVENGINE_TEST_BINARY_DIR) + "/particle_playback_lab.png";
    CHECK(gfx->saveFramePng(output));
    CHECK(std::filesystem::exists(output));
    REQUIRE(std::filesystem::file_size(output) > uintmax_t(1024));
    window->close();
}
}  // namespace test_particles_playback

// Contract section: particles reference effects
#include "PrivateTestScope.h"

// particles_reference_effects.cpp
namespace test_particles_reference_effects {
using namespace eve::particles;

namespace {

eve::graphics::Texture *makeVfxTexture(eve::graphics::Graphics *gfx, int size, bool ring) {
    std::vector<std::uint8_t> pixels(std::size_t(size * size) * 4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float nx    = (float(x) + 0.5f) / float(size) * 2.f - 1.f;
            const float ny    = (float(y) + 0.5f) / float(size) * 2.f - 1.f;
            const float r     = std::sqrt(nx * nx + ny * ny);
            const float a     = ring ? std::clamp(1.f - std::abs(r - 0.68f) * 13.f, 0.f, 1.f)
                                     : std::clamp(1.f - r, 0.f, 1.f) * std::clamp(1.4f - r, 0.f, 1.f);
            auto       *pixel = &pixels[std::size_t(y * size + x) * 4u];
            const auto  value = std::uint8_t(a * 255.f);
            pixel[0]          = value;
            pixel[1]          = value;
            pixel[2]          = value;
            pixel[3]          = value;
        }
    }
    return gfx->newTexture(size, size, pixels.data());
}

ParticleEmitter *makeLayer(eve::graphics::Texture *texture, float x, float y, int capacity) {
    auto *emitter = Particles::create()->newEmitter(capacity);
    emitter->setTexture(texture);
    emitter->setPosition(x, y);
    emitter->setEmissionRate(0.f);
    emitter->setRandomSeed(20260826 + int(x));
    emitter->setAutoRandomSeed(false);
    return emitter;
}

struct Palette {
    float r;
    float g;
    float b;
};

void makeCompositeBurst(eve::graphics::Texture *glow, eve::graphics::Texture *ring, float x, float y, float direction,
                        const Palette &color, std::vector<ParticleEmitter *> &emitters) {
    auto *core = makeLayer(glow, x, y, 4);
    core->setParticleLifetime(2.f, 2.f);
    core->setParticleSize(42.f, 42.f);
    core->setSpeed(0.f, 0.f);
    core->setBlendMode("additive");
    core->setColorStart(color.r, color.g, color.b, 0.95f);
    core->setColorEnd(color.r, color.g, color.b, 0.95f);
    core->emit(1);
    emitters.push_back(core);

    auto *shockwave = makeLayer(ring, x, y, 4);
    shockwave->setParticleLifetime(2.f, 2.f);
    shockwave->setParticleSize(176.f, 176.f);
    shockwave->setSpeed(0.f, 0.f);
    shockwave->setBlendMode("additive");
    shockwave->setColorStart(color.r, color.g, color.b, 0.82f);
    shockwave->setColorEnd(color.r, color.g, color.b, 0.82f);
    shockwave->emit(1);
    emitters.push_back(shockwave);

    auto *sparks = makeLayer(glow, x, y, 96);
    sparks->setParticleLifetime(0.75f, 1.15f);
    sparks->setParticleSize(7.f, 24.f);
    sparks->setSizes(1.f, 0.15f);
    sparks->setDirection(direction);
    sparks->setSpread(0.72f);
    sparks->setSpeed(150.f, 285.f);
    sparks->setDamping(0.12f);
    sparks->setRenderMode("velocity", 0.075f);
    sparks->setBlendMode("additive");
    sparks->setColorStart(1.f, 0.95f, 0.72f, 1.f);
    sparks->setColorEnd(color.r, color.g, color.b, 0.f);
    sparks->emit(48);
    emitters.push_back(sparks);

    auto *smoke = makeLayer(glow, x - std::cos(direction) * 18.f, y - std::sin(direction) * 18.f, 48);
    smoke->setParticleLifetime(1.2f, 1.9f);
    smoke->setParticleSize(18.f, 18.f);
    smoke->setSizes(0.65f, 1.2f);
    smoke->setSizeVariation(0.35f);
    smoke->setDirection(direction + 3.14159265f);
    smoke->setSpread(1.15f);
    smoke->setSpeed(18.f, 58.f);
    smoke->setDamping(0.3f);
    smoke->setBlendMode("additive");
    smoke->setColorStart(color.r * 0.18f, color.g * 0.16f, color.b * 0.22f, 0.22f);
    smoke->setColorEnd(0.02f, 0.025f, 0.055f, 0.f);
    smoke->emit(12);
    emitters.push_back(smoke);
}

float luma(const Color &color) { return color.r * 0.2126f + color.g * 0.7152f + color.b * 0.0722f; }

}  // namespace

TEST_CASE("particles.reference.compositedBurstsRenderAtMultipleAngles") {
    auto *window = eve::window::Window::create();
    auto *gfx    = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 960;
    settings.height   = 420;
    settings.centered = true;
    settings.vsync    = 0;
    REQUIRE(window->setWindowSettings(settings));
    gfx->setScreenReadbackEnabled(true);

    auto *glow = makeVfxTexture(gfx, 64, false);
    auto *ring = makeVfxTexture(gfx, 64, true);
    REQUIRE(glow != nullptr);
    REQUIRE(ring != nullptr);

    constexpr std::array<float, 3>   centers  = {170.f, 480.f, 790.f};
    constexpr std::array<float, 3>   angles   = {0.f, 0.78539816f, 1.57079633f};
    constexpr std::array<Palette, 3> palettes = {
        Palette{0.08f, 0.78f, 1.f},
        Palette{1.f, 0.28f, 0.06f},
        Palette{0.62f, 0.16f, 1.f},
    };
    std::vector<ParticleEmitter *> emitters;
    for (std::size_t i = 0; i < centers.size(); ++i)
        makeCompositeBurst(glow, ring, centers[i], 210.f, angles[i], palettes[i], emitters);

    for (int frame = 0; frame < 18; ++frame) ParticleSimSystem::update(1.f / 60.f);

    gfx->setBackgroundColorRGBA(0.006f, 0.009f, 0.024f, 1.f);
    for (int frame = 0; frame < 3; ++frame) {
        gfx->clearScreen();
        for (std::size_t i = 0; i < centers.size(); ++i) {
            gfx->drawSolidRect(centers[i] - 142.f, 40.f, 284.f, 340.f, Color(0.f, 0.f, 0.f, 1.f));
            gfx->drawSolidRect(centers[i] - 142.f, 40.f, 284.f, 2.f,
                               Color(palettes[i].r, palettes[i].g, palettes[i].b, 0.8f));
        }
        ParticleRenderSystem::render(gfx);
        gfx->present();
    }

    const std::string output = std::string(EVENGINE_TEST_BINARY_DIR) + "/particle_composite_reference_angles.png";
    CHECK(gfx->saveFramePng(output));
    CHECK(std::filesystem::exists(output));
    REQUIRE(std::filesystem::file_size(output) > std::uintmax_t(12000));

    for (std::size_t i = 0; i < centers.size(); ++i) {
        float peakLuma   = 0.f;
        float peakChroma = 0.f;
        for (int y = 120; y <= 300; y += 4) {
            for (int x = int(centers[i]) - 110; x <= int(centers[i]) + 110; x += 4) {
                const Color sample = gfx->getPixel(x, y);
                peakLuma           = std::max(peakLuma, luma(sample));
                peakChroma         = std::max(
                    peakChroma, std::max({sample.r, sample.g, sample.b}) - std::min({sample.r, sample.g, sample.b}));
            }
        }
        REQUIRE_GT(peakLuma, 0.3f);
        REQUIRE_GT(peakChroma, 0.12f);
    }

    for (auto *emitter : emitters) emitter->release();
    window->close();
}
}  // namespace test_particles_reference_effects

// Contract section: particles scalability
#include "PrivateTestScope.h"

// particles_scalability.cpp
namespace test_particles_scalability {
using namespace eve::particles;

namespace {

[[maybe_unused]] void resetScalability() {
    auto &budget                = particleBudgetConfig();
    budget.maxParticles         = 0;
    budget.maxSimulatedEmitters = 0;
    budget.qualityLevel         = 3;
    if (ecs::current()->getManager<ParticleEmitter>() == nullptr) return;
    std::vector<ParticleEmitter *> existing;
    auto                           view = ecs::View<ParticleEmitter, ParticleEmitter::Config>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [cfg] = *it;
        if (cfg->entity) existing.push_back(cfg->entity);
    }
    for (auto *emitter : existing) {
        emitter->stop();
        emitter->reset();
    }
}

}  // namespace


TEST_CASE("particles.scalability.renderBudgetStressScene") {
    resetScalability();
    particleBudgetConfig().maxParticles         = 900;
    particleBudgetConfig().maxSimulatedEmitters = 12;

    auto *window = eve::window::Window::create();
    auto *gfx    = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 720;
    settings.height   = 480;
    settings.centered = true;
    settings.vsync    = 0;
    REQUIRE(window->setWindowSettings(settings));
    gfx->setScreenReadbackEnabled(true);

    const uint8_t whitePixel[4] = {255, 255, 255, 255};
    auto         *whiteTexture  = gfx->newTexture(1, 1, whitePixel);
    for (int i = 0; i < 24; ++i) {
        auto *emitter = Particles::create()->newEmitter(180);
        emitter->setTexture(whiteTexture);
        emitter->setPriority(24 - i);
        emitter->setRandomSeed(9000 + i);
        emitter->setPosition(74.f + float(i % 6) * 114.f, 82.f + float(i / 6) * 104.f);
        emitter->setEmissionRate(0.f);
        emitter->setMaxSpawnPerFrame(96);
        emitter->setParticleLifetime(0.3f, 0.4f);
        emitter->setParticleSize(13.f, 13.f);
        emitter->setSizes(1.f, 0.25f);
        emitter->setSpeed(18.f, 62.f);
        emitter->setSpread(6.2831853f);
        emitter->setGravity(0.f, 22.f);
        emitter->setDamping(0.18f);
        const float hue = float(i % 6) / 6.f;
        emitter->setColorStart(0.25f + hue * 0.7f, 0.85f - hue * 0.35f, 1.f - hue * 0.5f, 0.95f);
        emitter->setColorEnd(0.15f, 0.05f, 0.35f + hue * 0.5f, 0.1f);
        emitter->addBurst(0.f, 96);
        emitter->start();
    }

    gfx->setBackgroundColorRGBA(0.018f, 0.025f, 0.055f, 1.f);
    constexpr float dt = 1.f / 60.f;
    for (int frame = 0; frame < 8; ++frame) {
        ParticleSimSystem::update(dt);
        gfx->clearScreen();
        gfx->drawSolidRectRGBA(22.f, 20.f, 676.f, 440.f, 0.035f, 0.055f, 0.11f, 1.f);
        ParticleRenderSystem::render(gfx);
        gfx->present();
    }

    const auto &stats = particleFrameStats();
    REQUIRE_EQ(stats.emittersTotal, 24);
    REQUIRE_EQ(stats.emittersSimulated, 12);
    REQUIRE_EQ(stats.emittersBudgetSkipped, 12);
    REQUIRE(stats.particlesAfter <= 900);
    REQUIRE_GT(stats.renderedParticles, 100);
    REQUIRE(stats.simulationMs >= 0.0);
    REQUIRE(stats.renderMs >= 0.0);

    const std::string output = std::string(EVENGINE_TEST_BINARY_DIR) + "/particle_scalability_stress.png";
    CHECK(gfx->saveFramePng(output));
    CHECK(std::filesystem::exists(output));
    REQUIRE(std::filesystem::file_size(output) > uintmax_t(1024));
    window->close();
}
}  // namespace test_particles_scalability

// Contract section: procgen
#include "PrivateTestScope.h"

#include <SDL2/SDL.h>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <numeric>
#include <set>
#include <unordered_set>
#include "RenderImageAudit.h"
#include "asset/procgen/TerrainMaterialAtlas.h"
#include "data/ByteData.h"
#include "filesystem/FileData.h"
#include "graphics/Canvas.h"
#include "graphics/ClipSpace.h"
#include "graphics/DrawItem2D.h"
#include "graphics/GBuffer.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "map/TileLayer.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/MeshBuild.h"
#include "procgen/Procgen.h"
#include "procgen/ProcgenSystem.h"
#include "procgen/Semantic.h"
// procgen.cpp
namespace test_procgen {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::procgen;
using namespace eve::graphics;

namespace {

struct ParamsLease {
    ProcgenParamsHandleRef handle{};
    explicit ParamsLease(ProcgenParamsHandleRef value) : handle(value) {}
    ~ParamsLease() {
        if (!handle.isValid()) return;
        auto result = Procgen::release(handle);
        result.ignore("procgen test params cleanup");
    }
    ParamsLease(const ParamsLease &)            = delete;
    ParamsLease &operator=(const ParamsLease &) = delete;
    ParamsLease(ParamsLease &&other) noexcept : handle(other.handle) { other.handle = {}; }
    [[nodiscard]] eve::script::Borrowed<Params> view() const noexcept { return Procgen::resolve(handle); }
};

[[maybe_unused]] ParamsLease requireParams(const Params &source) {
    auto result = Procgen::newParamsHandle();
    REQUIRE(result.ok());
    ParamsLease lease{std::move(result).takeValue()};
    auto        view = lease.view();
    REQUIRE(view.isBound());
    *view = source;
    return lease;
}

struct MeshLease {
    Procgen                  *owner = nullptr;
    ProcgenMeshBuildHandleRef handle{};
    MeshLease(Procgen &proc, ProcgenMeshBuildHandleRef value) : owner(&proc), handle(value) {}
    ~MeshLease() {
        if (!owner || !handle.isValid()) return;
        auto result = owner->releaseMeshBuild(handle);
        result.ignore("procgen test mesh cleanup");
    }
    MeshLease(const MeshLease &)            = delete;
    MeshLease &operator=(const MeshLease &) = delete;
    MeshLease(MeshLease &&other) noexcept : owner(other.owner), handle(other.handle) {
        other.owner  = nullptr;
        other.handle = {};
    }
    [[nodiscard]] eve::script::Borrowed<MeshBuild> view() const noexcept {
        return owner ? owner->resolveMeshBuild(handle) : eve::script::Borrowed<MeshBuild>();
    }
};

[[maybe_unused]] MeshLease requireMesh(Procgen &proc, const std::string &recipe, ProcgenParamsHandleRef params) {
    auto result = proc.buildMeshHandle(recipe, params);
    REQUIRE(result.ok());
    return MeshLease(proc, std::move(result).takeValue());
}

[[maybe_unused]] bool approxEq(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }

}  // namespace


TEST_CASE("procgen.terrain.materialShaderRendersPortableSixteenLayerContract") {
    auto *gfx = eve::graphics::Graphics::create();
    if (!gfx->isHeadless()) gfx->initHeadless(64, 64);
    Procgen procgen;
    auto   *shader = procgen.createTerrainMaterialShader(gfx);
    REQUIRE(shader != nullptr);
    CHECK_EQ(shader->usedFloats(), 32);
    CHECK(shader->hasUniform("terrainLayer0ST"));
    CHECK(shader->hasUniform("terrainLayer3ST"));
    CHECK(shader->hasUniform("terrainMetallic"));
    CHECK(shader->hasUniform("terrainNormalScale"));
    CHECK(shader->hasUniform("terrainSmoothness"));
    CHECK(shader->hasUniform("terrainFeatures"));
    float normalScale[4]{};
    CHECK_EQ(shader->getFromVar("terrainNormalScale", normalScale, sizeof(normalScale)), int(sizeof(normalScale)));
    for (float value : normalScale) CHECK_EQ(value, 1.f);

    const float positions[] = {
        -1.f, -1.f, 0.5f, 1.f, -1.f, 0.5f, 1.f, 1.f, 0.5f, -1.f, 1.f, 0.5f,
    };
    const float normals[] = {
        0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f,
    };
    const float    uvs[]     = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const uint32_t indices[] = {0, 2, 1, 2, 0, 3};
    auto          *mesh      = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    auto solidImage = [](std::uint32_t width, std::uint32_t height, std::array<std::uint8_t, 4> color) {
        eve::asset_procgen::TerrainAtlasImage image{width, height};
        image.pixels.resize(std::size_t(width) * height * 4);
        for (std::size_t offset = 0; offset < image.pixels.size(); offset += 4)
            std::copy(color.begin(), color.end(), image.pixels.begin() + std::ptrdiff_t(offset));
        return image;
    };
    eve::asset_procgen::TerrainMaterialAtlases    atlases;
    eve::asset_procgen::TerrainMaterialAtlasGroup group;
    group.firstLayer = 0;
    group.layerCount = 1;
    group.tileWidth  = 1;
    group.tileHeight = 1;
    group.albedo     = solidImage(2, 2, {16, 240, 24, 255});
    group.normal     = solidImage(2, 2, {128, 128, 255, 255});
    group.mask       = solidImage(2, 2, {0, 0, 0, 255});
    group.control    = solidImage(1, 1, {255, 0, 0, 255});
    atlases.groups.push_back(std::move(group));
    atlases.holes = solidImage(1, 1, {255, 255, 255, 255});
    auto uploaded = eve::asset_procgen::uploadTerrainMaterialAtlases(*gfx, atlases);
    REQUIRE(uploaded.ok());
    auto materialRef = eve::AssetRef::parse("asset://550e8400-e29b-41d4-a716-446655440000");
    REQUIRE(materialRef.ok());
    eve::asset_procgen::LoadedTerrainMaterial material{materialRef.value()};
    material.layers.resize(1);
    material.layers[0].normalConvention = "opengl";
    REQUIRE(eve::asset_procgen::bindTerrainMaterialGroup(*shader, uploaded.value(), material, 0).ok());

    eve::graphics::Lighting3DPack lighting{};
    lighting.ambient = glm::vec4(1.f, 1.f, 1.f, 0.f);
    gfx->setMesh3DLighting(lighting);
    gfx->setMesh3DViewProj(glm::mat4(1.f));
    gfx->setMesh3DView(glm::mat4(1.f));
    gfx->setMesh3DCameraPos(glm::vec3(0.f, 0.f, 3.f));
    auto *canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    gfx->begin3DFrameToCanvas(canvas);
    gfx->drawMeshShader(mesh, glm::mat4(1.f), uploaded.value().groups[0].control, glm::vec4(1.f), shader);
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> pixels(canvas->newImageData());
    REQUIRE(pixels != nullptr);
    const auto *rgba = static_cast<const uint8_t *>(pixels->getData()) + (32 * 64 + 32) * 4;
    CHECK(rgba[1] > rgba[0] + 40);
    CHECK(rgba[1] > rgba[2] + 40);
    REQUIRE(eve::asset_procgen::releaseTerrainMaterialAtlases(*gfx, uploaded.value()).ok());

    eve::asset_procgen::TerrainMaterialAtlases    grouped;
    eve::asset_procgen::TerrainMaterialAtlasGroup firstGroup;
    firstGroup.firstLayer = 0;
    firstGroup.layerCount = 4;
    firstGroup.tileWidth = firstGroup.tileHeight = 1;
    firstGroup.albedo                            = solidImage(2, 2, {240, 16, 16, 255});
    firstGroup.normal                            = solidImage(2, 2, {128, 128, 255, 255});
    firstGroup.mask                              = solidImage(2, 2, {0, 0, 0, 255});
    firstGroup.control                           = solidImage(1, 1, {0, 0, 0, 0});
    eve::asset_procgen::TerrainMaterialAtlasGroup secondGroup;
    secondGroup.firstLayer = 4;
    secondGroup.layerCount = 1;
    secondGroup.tileWidth = secondGroup.tileHeight = 1;
    secondGroup.albedo                             = solidImage(2, 2, {16, 240, 24, 255});
    secondGroup.normal                             = solidImage(2, 2, {128, 128, 255, 255});
    secondGroup.mask                               = solidImage(2, 2, {0, 0, 0, 255});
    secondGroup.control                            = solidImage(1, 1, {255, 0, 0, 0});
    grouped.groups                                 = {std::move(firstGroup), std::move(secondGroup)};
    grouped.holes                                  = solidImage(1, 1, {255, 255, 255, 255});
    material.layers.resize(5);
    for (auto &layer : material.layers) layer.normalConvention = "opengl";
    auto packed = eve::asset_procgen::packTerrainMaterialAtlases(grouped, material);
    REQUIRE(packed.ok());
    auto packedGpu = eve::asset_procgen::uploadPackedTerrainMaterialAtlases(*gfx, packed.value());
    REQUIRE(packedGpu.ok());
    REQUIRE(eve::asset_procgen::bindPackedTerrainMaterial(*shader, packedGpu.value()).ok());
    auto *packedCanvas = gfx->newCanvas(64, 64);
    REQUIRE(packedCanvas != nullptr);
    gfx->begin3DFrameToCanvas(packedCanvas);
    gfx->drawMeshShader(mesh, glm::mat4(1.f), packedGpu.value().controls, glm::vec4(1.f), shader);
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> packedPixels(packedCanvas->newImageData());
    REQUIRE(packedPixels != nullptr);
    const auto *packedRgba = static_cast<const uint8_t *>(packedPixels->getData()) + (32 * 64 + 32) * 4;
    CHECK(packedRgba[1] > packedRgba[0] + 40);
    CHECK(packedRgba[1] > packedRgba[2] + 40);
    REQUIRE(eve::asset_procgen::releasePackedTerrainMaterialAtlases(*gfx, packedGpu.value()).ok());
}

TEST_CASE("procgen.terrain.waterShaderShipsEmbeddedSpirv") {
    // Regression guard for the portability break this stage used to cause: it was
    // compiled with glslc at runtime, which packaged builds cannot rely on. It is
    // embedded SPIR-V now, so creating and drawing it must succeed without any
    // external compiler.
    auto *gfx = eve::graphics::Graphics::create();
    if (!gfx->isHeadless()) gfx->initHeadless(64, 64);
    Procgen procgen;
    auto   *shader = procgen.createTerrainWaterShader(gfx);
    REQUIRE(shader != nullptr);
    CHECK(shader->getKind() == eve::graphics::Shader::Kind::eMesh3D);
    CHECK_EQ(shader->usedFloats(), 0);
    CHECK(!shader->fragmentSpirv().empty());
    CHECK_EQ(shader->fragmentSpirv().front(), 0x07230203u);
    CHECK(!shader->vertexSpirv().empty());

    const float    positions[] = {-1.f, -1.f, 0.5f, 1.f, -1.f, 0.5f, 1.f, 1.f, 0.5f, -1.f, 1.f, 0.5f};
    const float    normals[]   = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const float    uvs[]       = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const uint32_t indices[]   = {0, 2, 1, 2, 0, 3};
    auto          *mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    const uint8_t white[4] = {255, 255, 255, 255};
    auto         *control  = gfx->newTexture(1, 1, white);
    REQUIRE(control != nullptr);

    eve::graphics::Lighting3DPack lighting{};
    lighting.count               = 1;
    lighting.ambient             = glm::vec4(0.35f, 0.45f, 0.55f, 0.f);
    lighting.lights[0].posRadius = glm::vec4(0.f, 1.f, 0.f, 0.f);
    lighting.lights[0].color     = glm::vec4(1.f, 1.f, 1.f, 0.f);
    gfx->setMesh3DLighting(lighting);
    gfx->setMesh3DViewProj(glm::mat4(1.f));
    gfx->setMesh3DView(glm::mat4(1.f));
    gfx->setMesh3DCameraPos(glm::vec3(0.f, 0.f, 3.f));
    auto *canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    gfx->begin3DFrameToCanvas(canvas);
    gfx->drawMeshShader(mesh, glm::mat4(1.f), control, glm::vec4(1.f), shader);
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> pixels(canvas->newImageData());
    REQUIRE(pixels != nullptr);
    const auto *rgba = static_cast<const uint8_t *>(pixels->getData()) + (32 * 64 + 32) * 4;
    // Shallow-water tint: blue/green dominant, red far behind.
    CHECK(rgba[2] > rgba[0]);
    CHECK(rgba[1] > rgba[0]);
}


TEST_CASE("procgen.render.cloudShadowsDarkenGround") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 256;
    settings.height   = 256;
    settings.centered = true;
    settings.vsync    = 0;
    REQUIRE(win->setWindowSettings(settings));

    // Flat ground grid over XZ.
    const int             N = 24;
    const float           S = 40.f;  // world extent
    std::vector<float>    pos, nrm, uv;
    std::vector<uint32_t> idx;
    for (int y = 0; y <= N; ++y) {
        for (int x = 0; x <= N; ++x) {
            pos.push_back(-S / 2 + S * float(x) / N);
            pos.push_back(0.f);
            pos.push_back(-S / 2 + S * float(y) / N);
            nrm.push_back(0.f);
            nrm.push_back(1.f);
            nrm.push_back(0.f);
            uv.push_back(float(x) / N);
            uv.push_back(float(y) / N);
        }
    }
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            const uint32_t a = uint32_t(y * (N + 1) + x);
            const uint32_t b = a + 1;
            const uint32_t c = a + uint32_t(N + 1);
            const uint32_t d = c + 1;
            idx.push_back(a);
            idx.push_back(c);
            idx.push_back(b);
            idx.push_back(b);
            idx.push_back(c);
            idx.push_back(d);
        }
    }
    Mesh *groundMesh =
        gfx->newMeshFromArrays(pos.data(), nrm.data(), uv.data(), int(pos.size() / 3), idx.data(), int(idx.size()));
    REQUIRE(groundMesh != nullptr);
    const uint8_t grass[4] = {96, 150, 70, 255};
    Texture      *grassTex = gfx->newTexture(1, 1, grass);
    REQUIRE(grassTex != nullptr);

    auto *ground = Renderable3D::create();
    ground->setMesh(groundMesh);
    ground->setTexture(grassTex);
    ground->setRoughness(0.9f);
    ground->setCastShadow(false);
    ground->setReceiveShadow(false);

    auto *camera = Camera3D::createCamera();
    camera->setEye(0.f, 18.f, 6.f);
    camera->setTarget(0.f, 0.f, 0.f);
    camera->setFov(55.f);
    camera->setAmbient(0.12f, 0.14f, 0.16f);

    gfx->setBackgroundColor(Color(0.02f, 0.03f, 0.05f, 1.f));
    gfx->setScreenReadbackEnabled(true);
    RenderSystem3D::setDirectionalLight(0.3f, 1.f, 0.2f, 1.15f, 1.05f, 0.95f);

    auto meanLuma = [&]() -> float {
        for (int frame = 0; frame < 3; ++frame) {
            RenderSystem3D::render(*gfx);
            RenderSystem::render(*gfx);
        }
        std::unique_ptr<eve::image::ImageData> img(gfx->newImageData());
        REQUIRE(img.get() != nullptr);
        const uint8_t *px  = static_cast<const uint8_t *>(img->getData());
        const int      w   = img->getWidth();
        const int      h   = img->getHeight();
        double         sum = 0.0;
        for (int i = 0; i < w * h; ++i) {
            const size_t o = size_t(i) * 4u;
            sum += 0.2126 * px[o] + 0.7152 * px[o + 1] + 0.0722 * px[o + 2];
        }
        return float(sum / double(w * h));
    };

    // No clouds 鈫?fully lit (baseline).
    gfx->setCloudShadows(0.f, 1.5f, 0.f, 4.f, 0.f, 0.5f, 0.5f);
    const float lit = meanLuma();

    // Dense, strong clouds 鈫?ground visibly darker.
    gfx->setCloudShadows(0.9f, 2.0f, 1.5f, 4.f, 0.f, 0.5f, 0.5f);
    const float cloudy = meanLuma();

    gfx->setCloudShadows(0.f, 1.5f, 0.f, 4.f, 0.f, 0.5f, 0.5f);
    const float litAgain = meanLuma();

    std::printf("cloud shadows render: lit=%.3f cloudy=%.3f litAgain=%.3f\n", lit, cloudy, litAgain);
    CHECK_GT(lit, 10.f);                  // baseline is lit
    CHECK(cloudy < lit * 0.85f);          // clouds meaningfully darken the ground
    CHECK(approxEq(lit, litAgain, 2.f));  // disabled again 鈫?back to baseline
    win->close();
}


TEST_CASE("procgen.render.castlePng") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 900;
    settings.height   = 700;
    settings.centered = true;
    settings.vsync    = 0;
    REQUIRE(win->setWindowSettings(settings));

    Procgen *procgen = Procgen::create();
    Params   params;
    params.setSeed(20260826);
    params.setFloat("width", 48.f);
    params.setFloat("depth", 40.f);
    params.setInt("rings", 2);
    params.setInt("keepFloors", 4);
    params.setInt("detail", 2);
    params.setFloat("towerSpacing", 15.f);
    auto castleParams = requireParams(params);
    auto castleBuild  = requireMesh(*procgen, "mesh.castle", castleParams.handle);
    auto castleView   = castleBuild.view();
    REQUIRE(castleView.isBound());

    const uint8_t stone[16] = {
        126, 116, 98, 255, 150, 139, 116, 255, 105, 96, 82, 255, 169, 157, 132, 255,
    };
    Texture *stoneTexture = gfx->newTexture(2, 2, stone);
    REQUIRE(stoneTexture != nullptr);
    // Exercise the graph-style group filter + CPU-to-GPU upload path. Each
    // semantic component can now have independent material/collision policy.
    const float groupTint[][3] = {
        {.74f, .69f, .58f}, {.82f, .76f, .64f}, {.66f, .61f, .52f}, {.58f, .53f, .46f},
        {.48f, .43f, .36f}, {.71f, .65f, .54f}, {.60f, .55f, .45f},
    };
    std::vector<eve::graphics::Renderable3D *> castleParts;
    for (int group = 0; group < castleView->getGroupCount(); ++group) {
        auto part = castleView->copyGroup(group);
        if (!part) continue;
        auto gpuPart = procgen->uploadMeshBorrowed(*part, *gfx);
        REQUIRE(gpuPart.isBound());
        auto *renderable = eve::graphics::Renderable3D::create();
        renderable->setMesh(gpuPart.get());
        renderable->setTexture(stoneTexture);
        const int tint = std::min(group, 6);
        renderable->setTint(groupTint[tint][0], groupTint[tint][1], groupTint[tint][2], 1.f);
        renderable->setMetallic(0.01f);
        renderable->setRoughness(0.94f);
        renderable->setCastShadow(true);
        renderable->setReceiveShadow(true);
        castleParts.push_back(renderable);
    }
    CHECK_EQ(castleParts.size(), size_t(7));

    auto *camera = eve::graphics::Camera3D::createCamera();
    // View from the south-east so the gate opening and both wall stair systems
    // are part of the visual contract rather than hidden behind the north wall.
    camera->setEye(42.f, 32.f, -64.f);
    camera->setTarget(0.f, 5.5f, 0.f);
    camera->setFov(43.f);
    camera->setAmbient(0.19f, 0.20f, 0.23f);
    auto *present             = eve::graphics::Renderable2D::create();
    present->sprite()->width  = 1.f;
    present->sprite()->height = 1.f;
    present->sprite()->a      = 0.f;
    gfx->setBackgroundColor(Color(0.09f, 0.13f, 0.19f, 1.f));
    gfx->setScreenReadbackEnabled(true);
    RenderSystem3D::setDirectionalLight(0.6f, 0.95f, 0.8f, 1.35f, 1.18f, 0.96f);
    for (int frame = 0; frame < 8; ++frame) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }

    [[maybe_unused]] auto *const           imageModule = eve::image::Image::create();
    std::unique_ptr<eve::image::ImageData> image(gfx->newImageData());
    REQUIRE(static_cast<bool>(image));
    RenderAuditConfig auditCfg;
    auditCfg.scene                = "procgen";
    auditCfg.phase                = "castle";
    auditCfg.width                = image->getWidth();
    auditCfg.height               = image->getHeight();
    const RenderAuditResult audit = auditImage(*image, auditCfg, {0.09f, 0.13f, 0.19f}, 2);
    CHECK(!audit.empty);
    CHECK(audit.occupancy > 0.08f);
    CHECK(audit.meanLuma > 12.f);
    const std::filesystem::path outDir = std::filesystem::path(EVENGINE_TEST_BINARY_DIR) / "out";
    std::filesystem::create_directories(outDir);
    const std::filesystem::path outPath = outDir / "castle.png";
    REQUIRE(saveImagePng(*image, outPath.string()));
    std::printf("castle render saved: %s occupancy=%.3f luma=%.3f\n", outPath.string().c_str(), audit.occupancy,
                audit.meanLuma);
    win->close();
}


// --- Water (graphics): sky reflection + animated edge waves + middle drop ripples ---

namespace {

struct WaterLumaGrid {
    int                grid = 16;
    std::vector<float> cells;
};

/** Render the current scene and downsample luma into a grid (for comparison). */
[[maybe_unused]] WaterLumaGrid waterCaptureLuma(Graphics *gfx, int grid) {
    for (int frame = 0; frame < 3; ++frame) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    std::unique_ptr<eve::image::ImageData> img(gfx->newImageData());
    REQUIRE(img.get() != nullptr);
    const uint8_t *px = static_cast<const uint8_t *>(img->getData());
    const int      w  = img->getWidth();
    const int      h  = img->getHeight();
    WaterLumaGrid  out;
    out.grid = grid;
    out.cells.assign(size_t(grid * grid), 0.f);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t o  = (size_t(y) * w + size_t(x)) * 4u;
            const float  l  = 0.2126f * px[o] + 0.7152f * px[o + 1] + 0.0722f * px[o + 2];
            const int    gx = std::min(grid - 1, x * grid / w);
            const int    gy = std::min(grid - 1, y * grid / h);
            out.cells[size_t(gy * grid + gx)] += l;
        }
    }
    const float scale = 1.f / (float(w / grid) * float(h / grid));
    for (float &c : out.cells) c *= scale;
    return out;
}

[[maybe_unused]] float waterDiff(const WaterLumaGrid &a, const WaterLumaGrid &b) {
    float     sum = 0.f;
    const int n   = a.grid * a.grid;
    for (int i = 0; i < n; ++i) sum += std::fabs(a.cells[size_t(i)] - b.cells[size_t(i)]);
    return sum / float(n);
}

}  // namespace

TEST_CASE("graphics.waterfall.paramsRoundTrip") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 128;
    settings.height   = 128;
    settings.centered = true;
    settings.vsync    = 0;
    REQUIRE(win->setWindowSettings(settings));
    Waterfall *wf = gfx->newWaterfall();
    REQUIRE(wf != nullptr);
    REQUIRE(wf->getShader() != nullptr);

    wf->setFlowSpeed(2.0f);
    CHECK(approxEq(wf->getFlowSpeed(), 2.0f, 1e-5f));
    wf->setTurbulence(0.9f);
    CHECK(approxEq(wf->getTurbulence(), 0.9f, 1e-5f));
    wf->setStreakCount(5);
    CHECK_EQ(wf->getStreakCount(), 5);
    wf->setStreakScale(7.f);
    CHECK(approxEq(wf->getStreakScale(), 7.f, 1e-5f));
    wf->setTopFoam(0.08f);
    CHECK(approxEq(wf->getTopFoam(), 0.08f, 1e-5f));
    wf->setBottomFoam(0.15f);
    CHECK(approxEq(wf->getBottomFoam(), 0.15f, 1e-5f));
    wf->setFoamAmount(0.9f);
    CHECK(approxEq(wf->getFoamAmount(), 0.9f, 1e-5f));
    wf->setWaterColor(0.1f, 0.2f, 0.3f);
    wf->setReflectionIntensity(0.4f);
    CHECK(approxEq(wf->getReflectionIntensity(), 0.4f, 1e-5f));
    wf->setSunIntensity(0.8f);
    CHECK(approxEq(wf->getSunIntensity(), 0.8f, 1e-5f));
    wf->bindParams();  // must not throw

    wf->createSheet(10.f, 16.f, 8, 12);
    REQUIRE(wf->getMesh() != nullptr);
    CHECK(Waterfall::paramCount() > 0);
    CHECK(!Waterfall::paramName(0).empty());
    delete wf;
    win->close();
}

TEST_CASE("graphics.waterfall.render.flowAndFoam") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 256;
    settings.height   = 256;
    settings.centered = true;
    settings.vsync    = 0;
    REQUIRE(win->setWindowSettings(settings));

    auto *camera = Camera3D::createCamera();
    camera->setEye(0.f, 0.f, 7.f);
    camera->setTarget(0.f, 0.f, 0.f);
    camera->setFov(50.f);
    camera->setAmbient(0.2f, 0.22f, 0.25f);

    gfx->setBackgroundColor(Color(0.01f, 0.015f, 0.025f, 1.f));
    gfx->setScreenReadbackEnabled(true);
    RenderSystem3D::setDirectionalLight(0.2f, 0.8f, 0.5f, 1.f, 0.95f, 0.85f);

    auto *present             = Renderable2D::create();
    present->sprite()->width  = 1.f;
    present->sprite()->height = 1.f;
    present->sprite()->a      = 0.f;

    Waterfall *wf = gfx->newWaterfall();
    REQUIRE(wf != nullptr);
    SUB_CASE("graphics.waterfall.render.rasterSurvivesSurfaceRebuildAndInvalidInput") {
        auto *shader = wf->getShader();
        REQUIRE(shader != nullptr);
        if (gfx->getBackendName() == "webgpu") {
            const auto previous    = shader->meshRasterState();
            auto       requested   = previous;
            requested.depthCompare = MeshDepthCompare::LessEqual;
            auto absent            = gfx->configureMeshShaderRaster(*shader, requested);
            REQUIRE(!absent.ok());
            REQUIRE(absent.status().code() == eve::StatusCode::Unsupported);
            REQUIRE(shader->meshRasterState().depthCompare == previous.depthCompare);
            REQUIRE(shader->meshRasterState().colorWriteMask == previous.colorWriteMask);
            return;
        }
        REQUIRE(shader->meshRasterState().depthCompare == MeshDepthCompare::LessEqual);
        REQUIRE(gfx->configureMeshShaderSurface(*shader, BlendMode::Opaque, false, true).ok());
        REQUIRE(gfx->configureMeshShaderSurface(*shader, BlendMode::Opaque, true, true).ok());
        const auto committed = shader->meshRasterState();
        REQUIRE(committed.depthCompare == MeshDepthCompare::LessEqual);
        auto invalid           = committed;
        invalid.colorWriteMask = 16;
        REQUIRE(!gfx->configureMeshShaderRaster(*shader, invalid).ok());
        const auto retained = shader->meshRasterState();
        REQUIRE(retained.depthCompare == committed.depthCompare);
        REQUIRE(retained.depthBiasConstant == committed.depthBiasConstant);
        REQUIRE(retained.depthBiasSlope == committed.depthBiasSlope);
        REQUIRE(retained.colorWriteMask == committed.colorWriteMask);
    };
    wf->createCurvedSheet(4.f, 6.f, 24, 32, 0.35f, 0.15f);
    wf->setWaterColor(0.05f, 0.28f, 0.5f);
    wf->setReflectionIntensity(0.65f);
    wf->setSunIntensity(0.8f);
    wf->setTurbulence(0.75f);
    wf->setStreakCount(7);

    auto *waterfallEnt = Renderable3D::create();
    waterfallEnt->setMesh(wf->getMesh());
    waterfallEnt->setShader(wf->getShader());
    waterfallEnt->setReceiveShadow(false);
    waterfallEnt->setCastShadow(false);
    waterfallEnt->setCamera(camera);

    auto capture = [&](float time, float foam) {
        wf->setTime(time);
        wf->setFoamAmount(foam);
        wf->bindParams();
        return waterCaptureLuma(gfx, 16);
    };

    const WaterLumaGrid t0       = capture(0.f, 0.8f);
    const WaterLumaGrid t1       = capture(0.4f, 0.8f);
    float               rendered = 0.f;
    for (float cell : t0.cells) rendered += cell;
    rendered /= float(t0.cells.size());
    const float flowDiff = waterDiff(t0, t1);

    const WaterLumaGrid noFoam   = capture(0.2f, 0.f);
    const WaterLumaGrid foam     = capture(0.2f, 1.1f);
    const float         foamDiff = waterDiff(noFoam, foam);
    std::printf("waterfall render: rendered=%.2f flowDiff=%.2f foamDiff=%.2f\n", rendered, flowDiff, foamDiff);
    REQUIRE(rendered > 1.f);
    REQUIRE(flowDiff > 0.15f);
    REQUIRE(foamDiff > 0.15f);

    delete wf;
    win->close();
}

TEST_CASE("graphics.water.paramsRoundTrip") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 128;
    settings.height   = 128;
    settings.centered = true;
    settings.vsync    = 0;
    REQUIRE(win->setWindowSettings(settings));
    Water *w = gfx->newWater();
    REQUIRE(w != nullptr);
    REQUIRE(w->getShader() != nullptr);

    w->setWaveSpeed(2.5f);
    CHECK(approxEq(w->getWaveSpeed(), 2.5f, 1e-5f));
    w->setWaveAmplitude(0.8f);
    CHECK(approxEq(w->getWaveAmplitude(), 0.8f, 1e-5f));
    w->setRippleAmplitude(0.9f);
    CHECK(approxEq(w->getRippleAmplitude(), 0.9f, 1e-5f));
    w->setEdgeFalloff(0.25f);
    CHECK(approxEq(w->getEdgeFalloff(), 0.25f, 1e-5f));
    w->setRippleCount(4);
    CHECK_EQ(w->getRippleCount(), 4);
    w->setRippleInterval(1.2f);
    CHECK(approxEq(w->getRippleInterval(), 1.2f, 1e-5f));
    w->setWaveScale(10.f);
    CHECK(approxEq(w->getWaveScale(), 10.f, 1e-5f));
    w->setWaterColor(0.1f, 0.2f, 0.3f);
    w->setReflectionTint(0.5f, 0.6f, 0.7f);
    w->setReflectionIntensity(0.4f);
    CHECK(approxEq(w->getReflectionIntensity(), 0.4f, 1e-5f));
    w->setSunIntensity(0.8f);
    CHECK(approxEq(w->getSunIntensity(), 0.8f, 1e-5f));
    w->setScreenSpaceReflection(true, 0.9f);
    CHECK(w->getScreenSpaceReflection());
    CHECK(approxEq(w->getScreenSpaceReflectionStrength(), 0.9f, 1e-5f));
    w->setScreenSpaceReflection(true, -1.f);
    CHECK(approxEq(w->getScreenSpaceReflectionStrength(), 0.f, 1e-5f));
    w->setViewport(320.f, 180.f);
    CHECK(approxEq(w->getViewportWidth(), 320.f, 1e-5f));
    CHECK(approxEq(w->getViewportHeight(), 180.f, 1e-5f));
    w->bindParams();  // must not throw

    w->createPlane(10.f, 8.f, 8, 6);
    REQUIRE(w->getMesh() != nullptr);
    CHECK(Water::paramCount() > 0);
    CHECK(!Water::paramName(0).empty());
    delete w;
    win->close();
}

TEST_CASE("graphics.water.render.dynamicRipplesAndReflection") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 256;
    settings.height   = 256;
    settings.centered = true;
    settings.vsync    = 0;
    REQUIRE(win->setWindowSettings(settings));

    // Blue-ish sky cubemap so reflection is visible.
    [[maybe_unused]] auto *const imageModule        = eve::image::Image::create();
    const int                    fs                 = 4;
    const uint8_t                sky[6 * 4 * 4 * 4] = {0};  // 6 faces 脳 4脳4 脳 RGBA
    for (int f = 0; f < 6; ++f)
        for (int i = 0; i < fs * fs; ++i) {
            const size_t o           = size_t(f) * fs * fs * 4 + size_t(i) * 4;
            ((uint8_t *)&sky)[o]     = 120;
            ((uint8_t *)&sky)[o + 1] = 160;
            ((uint8_t *)&sky)[o + 2] = 220;
            ((uint8_t *)&sky)[o + 3] = 255;
        }
    Texture *skyTex = gfx->newCubemap(fs, sky);
    REQUIRE(skyTex != nullptr);

    auto *camera = Camera3D::createCamera();
    camera->setEye(0.f, 10.f, 4.f);
    camera->setTarget(0.f, 0.f, 0.f);
    camera->setFov(55.f);
    camera->setAmbient(0.15f, 0.18f, 0.22f);
    camera->setEnvMap(skyTex);
    camera->setEnvIntensity(1.f);

    gfx->setBackgroundColor(Color(0.02f, 0.03f, 0.05f, 1.f));
    gfx->setScreenReadbackEnabled(true);
    RenderSystem3D::setDirectionalLight(0.3f, 1.f, 0.2f, 1.1f, 1.0f, 0.9f);

    // Transparent 2D entity drives the normal present path used by render tests.
    auto *present             = Renderable2D::create();
    present->transform()->x   = 0.f;
    present->transform()->y   = 0.f;
    present->sprite()->width  = 1.f;
    present->sprite()->height = 1.f;
    present->sprite()->a      = 0.f;

    Water *w = gfx->newWater();
    REQUIRE(w != nullptr);
    w->createPlane(20.f, 20.f, 40, 40);
    w->setWaveAmplitude(0.5f);
    w->setRippleAmplitude(0.8f);
    w->setRippleCount(6);
    w->setReflectionIntensity(0.8f);
    w->setSunIntensity(0.8f);

    // Attach the water plane + shader to a scene-graph renderable (standard path).
    auto *waterEnt = Renderable3D::create();
    waterEnt->setMesh(w->getMesh());
    waterEnt->setShader(w->getShader());
    waterEnt->setTexture(nullptr);
    waterEnt->setReceiveShadow(false);
    waterEnt->setCastShadow(false);
    waterEnt->setCamera(camera);

    auto captureWater = [&](float time) {
        w->setTime(time);
        w->bindParams();
        return waterCaptureLuma(gfx, 16);
    };

    // Dynamic ripples: different times give different patterns (and it renders).
    const WaterLumaGrid t0       = captureWater(0.f);
    const WaterLumaGrid t1       = captureWater(0.35f);
    const float         dynamic  = waterDiff(t0, t1);
    float               rendered = 0.f;
    for (float c : t0.cells) rendered += c;
    rendered /= float(t0.cells.size());
    std::printf("water render: dynamic=%.2f rendered=%.2f\n", dynamic, rendered);
    REQUIRE(rendered > 1.f);  // water surface is actually drawn
    REQUIRE(dynamic > 0.3f);  // ripples move over time

    // Edge waves + middle drop ripples: flat (no ripples) differs from rippled.
    const WaterLumaGrid flat = [&] {
        w->setRippleAmplitude(0.f);
        w->setWaveAmplitude(0.f);
        w->setReflectionIntensity(0.f);
        w->bindParams();
        w->setTime(0.6f);
        return waterCaptureLuma(gfx, 16);
    }();
    const WaterLumaGrid wavy = [&] {
        w->setRippleAmplitude(0.9f);
        w->setWaveAmplitude(0.5f);
        w->setReflectionIntensity(0.8f);
        w->bindParams();
        w->setTime(0.6f);
        return waterCaptureLuma(gfx, 16);
    }();
    const float rippleDiff = waterDiff(flat, wavy);
    std::printf("water render: rippleDiff=%.2f\n", rippleDiff);
    REQUIRE(rippleDiff > 0.2f);  // ripples (edge + middle) change the surface

    delete w;
    win->close();
}

/** Build a unit cube ([-0.5,0.5]鲁) with per-face UVs in [0,1]虏. */
[[maybe_unused]] static Mesh *makeUnitCube(Graphics *gfx) {
    std::vector<float>    pos, nrm, uv;
    std::vector<uint32_t> idx;
    // 6 faces: +X,-X,+Y,-Y,+Z,-Z. Each face 4 corners (CCW from outside).
    const float nx[6] = {1, -1, 0, 0, 0, 0};
    const float ny[6] = {0, 0, 1, -1, 0, 0};
    const float nz[6] = {0, 0, 0, 0, 1, -1};
    // Corner offsets relative to face center (unit cube half-extent 0.5).
    const float ox[4] = {0.5f, -0.5f, -0.5f, 0.5f};
    const float oy[4] = {0.5f, 0.5f, -0.5f, -0.5f};
    const float oz[4] = {0.5f, -0.5f, -0.5f, 0.5f};
    const float uu[4] = {0.f, 1.f, 1.f, 0.f};
    const float vv[4] = {0.f, 0.f, 1.f, 1.f};
    for (int f = 0; f < 6; ++f) {
        const uint32_t base = uint32_t(pos.size() / 3);
        for (int c = 0; c < 4; ++c) {
            // Tangents spanning the face so corners are correct for each normal axis.
            float px = 0.f, py = 0.f, pz = 0.f;
            if (nx[f] != 0.f) {
                px = nx[f] * 0.5f;
                py = oy[c];
                pz = oz[c];
            } else if (ny[f] != 0.f) {
                py = ny[f] * 0.5f;
                px = ox[c];
                pz = oz[c];
            } else {
                pz = nz[f] * 0.5f;
                px = ox[c];
                py = oy[c];
            }
            pos.push_back(px);
            pos.push_back(py);
            pos.push_back(pz);
            nrm.push_back(nx[f]);
            nrm.push_back(ny[f]);
            nrm.push_back(nz[f]);
            uv.push_back(uu[c]);
            uv.push_back(vv[c]);
        }
        idx.push_back(base + 0);
        idx.push_back(base + 1);
        idx.push_back(base + 2);
        idx.push_back(base + 0);
        idx.push_back(base + 2);
        idx.push_back(base + 3);
    }
    return gfx->newMeshFromArrays(pos.data(), nrm.data(), uv.data(), int(pos.size() / 3), idx.data(), int(idx.size()));
}


TEST_CASE("graphics.render3d.toCanvas") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 320;
    s.height   = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    const int            fs = 8;
    std::vector<uint8_t> sky(size_t(fs * fs * 4 * 6));
    for (size_t i = 0; i < sky.size(); i += 4) {
        sky[i]     = 135;
        sky[i + 1] = 180;
        sky[i + 2] = 235;
        sky[i + 3] = 255;
    }
    Texture *skyTex = gfx->newCubemap(fs, sky.data());
    REQUIRE(skyTex != nullptr);

    Canvas *refl = gfx->newCanvas(160, 120);
    REQUIRE(refl != nullptr);

    Mesh *cube = makeUnitCube(gfx);
    REQUIRE(cube != nullptr);
    const glm::mat4 view = glm::lookAtRH(glm::vec3(0.f, 2.f, 5.f), glm::vec3(0.f, 0.f, 0.f), glm::vec3(0.f, 1.f, 0.f));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), 160.f / 120.f, 0.1f, 50.f);
    const glm::mat4 viewProj = proj * view;

    gfx->setScreenReadbackEnabled(true);
    RenderSystem3D::setDirectionalLight(0.5f, 1.f, 0.3f, 1.4f, 1.3f, 1.2f);
    gfx->setMesh3DViewProj(viewProj);
    gfx->setMesh3DView(view);
    gfx->setMesh3DCameraPos(glm::vec3(0.f, 2.f, 5.f));
    gfx->setMesh3DEnv(skyTex, 1.f);
    const uint8_t red[4] = {220, 40, 40, 255};
    Texture      *redTex = gfx->newTexture(1, 1, red);

    gfx->begin3DFrameToCanvas(refl);
    const glm::mat4 model(1.f);
    gfx->drawMeshShader(cube, model, redTex, glm::vec4(1.f), nullptr);
    gfx->end3DFrameToCanvas();

    int    redPixels = 0, total = 0;
    double sr = 0, sg = 0, sb = 0;
    // Read the canvas once into an ImageData (getPixel per-pixel is O(N) readbacks).
    std::unique_ptr<eve::image::ImageData> img(refl->newImageData());
    REQUIRE(img.get() != nullptr);
    const int      rw  = img->getWidth();
    const int      rh  = img->getHeight();
    const uint8_t *pxd = static_cast<const uint8_t *>(img->getData());
    for (int y = 0; y < rh; y += 1) {
        for (int x = 0; x < rw; x += 1) {
            const size_t o = (size_t(y) * rw + size_t(x)) * 4;
            const float  r = pxd[o] / 255.f, g = pxd[o + 1] / 255.f, b = pxd[o + 2] / 255.f;
            ++total;
            sr += r;
            sg += g;
            sb += b;
            if (r > 0.25f && r > g + 0.1f && r > b + 0.1f) ++redPixels;
        }
    }
    std::printf("render3d.toCanvas: red=%d/%d avg=(%.2f,%.2f,%.2f)\n", redPixels, total, sr / total, sg / total,
                sb / total);
    CHECK(redPixels > total / 10);  // the red box was rendered into the canvas
    win->close();
}


TEST_CASE("graphics.water.render.planar") {
    const char *outPath = std::getenv("EVENGINE_PLANAR_RENDER_PNG");
    if (!outPath || !outPath[0]) return;

    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 640;
    settings.height   = 480;
    settings.centered = true;
    settings.vsync    = 0;
    REQUIRE(win->setWindowSettings(settings));
    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();

    const int            fs = 16;
    std::vector<uint8_t> sky(size_t(fs * fs * 4 * 6));
    for (size_t i = 0; i < sky.size(); i += 4) {
        sky[i]     = 135;
        sky[i + 1] = 180;
        sky[i + 2] = 235;
        sky[i + 3] = 255;
    }
    Texture *skyTex = gfx->newCubemap(fs, sky.data());
    REQUIRE(skyTex != nullptr);

    auto *camera = Camera3D::createCamera();
    camera->setEye(0.f, 5.f, 8.f);
    camera->setTarget(0.f, 0.5f, -3.f);
    camera->setFov(55.f);
    camera->setAmbient(0.28f, 0.32f, 0.40f);
    camera->setEnvMap(skyTex);
    camera->setEnvIntensity(1.f);
    camera->data()->nearZ = 0.1f;
    camera->data()->farZ  = 2000.f;

    gfx->setBackgroundColor(Color(0.05f, 0.09f, 0.14f, 1.f));
    gfx->setScreenReadbackEnabled(true);
    RenderSystem3D::setDirectionalLight(0.45f, 1.f, 0.3f, 1.3f, 1.2f, 1.1f);

    auto *present             = Renderable2D::create();
    present->transform()->x   = 0.f;
    present->transform()->y   = 0.f;
    present->sprite()->width  = 1.f;
    present->sprite()->height = 1.f;
    present->sprite()->a      = 0.f;

    // Boxes above the water to reflect.
    struct Box {
        float   x, y, z, sx, sy, sz;
        uint8_t r, g, b;
    };
    const Box boxes[] = {
        {0.f, 4.0f, -5.f, 3.0f, 3.0f, 3.0f, 210, 60, 60},
        {-3.5f, 3.0f, -3.f, 2.0f, 2.0f, 2.0f, 70, 180, 70},
        {3.5f, 3.5f, -4.f, 2.5f, 2.5f, 2.5f, 70, 110, 230},
    };
    Mesh *cube = makeUnitCube(gfx);
    struct Ent {
        Texture  *texture;
        glm::mat4 model;
    };
    std::vector<Ent> ents;
    for (const Box &b : boxes) {
        const uint8_t px[4]   = {b.r, b.g, b.b, 255};
        Texture      *texture = gfx->newTexture(1, 1, px);
        glm::mat4     m(1.f);
        m = glm::translate(m, glm::vec3(b.x, b.y, b.z));
        m = glm::scale(m, glm::vec3(b.sx, b.sy, b.sz));
        ents.push_back({texture, m});
    }

    Water *water = gfx->newWater();
    REQUIRE(water != nullptr);
    water->createPlane(14.f, 14.f, 48, 48);
    water->setWaveAmplitude(0.2f);
    water->setReflectionIntensity(1.0f);
    water->setScreenSpaceReflection(true, 0.9f);
    water->setViewport(float(settings.width), float(settings.height));

    Canvas *refl = gfx->newCanvas(settings.width, settings.height);
    REQUIRE(refl != nullptr);

    const glm::vec3 eye = glm::vec3(camera->data()->eyeX, camera->data()->eyeY, camera->data()->eyeZ);
    const glm::vec3 tgt = glm::vec3(camera->data()->targetX, camera->data()->targetY, camera->data()->targetZ);
    const glm::mat4 mView =
        glm::lookAtRH(glm::vec3(eye.x, -eye.y, eye.z), glm::vec3(tgt.x, -tgt.y, tgt.z), glm::vec3(0.f, -1.f, 0.f));
    const glm::mat4 mProj = perspectiveVulkanRH_ZO(glm::radians(camera->data()->fovYDeg),
                                                   settings.width / float(settings.height), 0.1f, 100.f);

    gfx->setMesh3DViewProj(mProj * mView);
    gfx->setMesh3DView(mView);
    gfx->setMesh3DCameraPos(glm::vec3(eye.x, -eye.y, eye.z));
    gfx->setMesh3DEnv(skyTex, 1.f);
    // Reflection background = bright sky (mirrors what the water should reflect).
    gfx->setBackgroundColor(Color(0.45f, 0.62f, 0.85f, 1.f));
    gfx->begin3DFrameToCanvas(refl);
    for (const Ent &en : ents) {
        gfx->drawMeshShader(cube, en.model, en.texture, glm::vec4(1.f), nullptr);
    }
    gfx->end3DFrameToCanvas();
    gfx->setBackgroundColor(Color(0.05f, 0.09f, 0.14f, 1.f));

    // Final frame: the public Water path borrows and samples the planar reflection.
    const glm::mat4 mainView = glm::lookAtRH(eye, tgt, glm::vec3(0.f, 1.f, 0.f));
    const glm::mat4 mainProj =
        perspectiveVulkanRH_ZO(glm::radians(camera->data()->fovYDeg), settings.width / float(settings.height),
                               camera->data()->nearZ, camera->data()->farZ);
    gfx->setMesh3DViewProj(mainProj * mainView);
    gfx->setMesh3DView(mainView);
    gfx->setMesh3DCameraPos(eye);
    gfx->setMesh3DClip(camera->data()->nearZ, camera->data()->farZ);
    gfx->setMesh3DEnv(skyTex, 1.f);
    water->setTime(0.5f);
    RenderSystem3D::render(*gfx);
    water->drawWithPlanarReflection(refl->getTexture(), 0.9F);
    RenderSystem::render(*gfx);

    // Read the reflection canvas once and check the red box was captured.
    std::unique_ptr<eve::image::ImageData> rim(refl->newImageData());
    REQUIRE(rim.get() != nullptr);
    REQUIRE(saveImagePng(*rim, std::string(outPath) + ".reflection.png"));
    int            red = 0, total = 0;
    const uint8_t *pd = static_cast<const uint8_t *>(rim->getData());
    const int      rw = rim->getWidth();
    const int      rh = rim->getHeight();
    for (int y = 0; y < rh; y += 2) {
        for (int x = 0; x < rw; x += 2) {
            const size_t o = (size_t(y) * rw + size_t(x)) * 4;
            const float  r = pd[o] / 255.f, g = pd[o + 1] / 255.f, b = pd[o + 2] / 255.f;
            ++total;
            if (r > 0.25f && r > g + 0.1f && r > b + 0.1f) ++red;
        }
    }
    std::printf("planar reflection canvas: red=%d/%d (%.1f%%)\n", red, total, 100.f * float(red) / float(total));
    CHECK(red > 50);

    std::unique_ptr<eve::image::ImageData> img(gfx->newImageData());
    REQUIRE(img.get() != nullptr);
    int         reflectedRed = 0;
    const auto *finalPixels  = static_cast<const uint8_t *>(img->getData());
    for (int y = 0; y < img->getHeight(); ++y) {
        for (int x = 0; x < img->getWidth(); ++x) {
            const size_t offset = (size_t(y) * size_t(img->getWidth()) + size_t(x)) * 4;
            const int    r      = finalPixels[offset];
            const int    g      = finalPixels[offset + 1];
            const int    b      = finalPixels[offset + 2];
            if (r > 120 && r > g + 20 && r > b + 20) ++reflectedRed;
        }
    }
    CHECK(reflectedRed > 1000);
    REQUIRE(saveImagePng(*img, outPath));
    std::printf("planar water render saved: %s\n", outPath);
    win->close();
}
}  // namespace test_procgen

// Contract section: procgen lsystem
#include "PrivateTestScope.h"

#include <simplesquirrel/simplesquirrel.hpp>
#include "common/Module.h"
#include "procgen/PointSet.h"
#include "procgen/algorithms/LSystem.h"
// procgen_lsystem.cpp
namespace test_procgen_lsystem {
using namespace eve::procgen;
using namespace eve::graphics;


TEST_CASE("procgen.script.newLSystemIsTheDocumentedEntryPoint") {
    // docs/usr/modules/procgen.md's L-system section starts from
    // `procgen.newLSystem()`. Like newShapeGrammar, the script class and the
    // handle/resolve/release trio all existed with no factory bound, so the documented
    // sample could not run. This drives the documented path instead of the C++ one.
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        procgen <- eve.Procgen();
        lsCreated <- procgen.newLSystem();
        ls <- lsCreated.ok ? lsCreated.value : null;
        lsIsProxy <- ls != null && ls instanceof eve.ProcgenLSystem;
        if (ls != null) {
            ls.setAxiom("F");
            ls.addRule('F', "F[+F]F[-F]F");
            ls.setIterations(3);
            ls.setAngle(26.0);
            ls.setSeed(42);
        }
        derived <- ls == null ? "" : ls.derive();
        roadCreated <- procgen.newPointSet();
        road <- roadCreated.ok ? roadCreated.value : null;
        if (ls != null && road != null) ls.trace(road);
        points <- road == null ? -1 : road.getCount();
    )"));

    REQUIRE(vm.find("lsCreated").find("ok").toBool());
    REQUIRE(vm.find("lsIsProxy").toBool());
    REQUIRE(vm.find("derived").toString().size() > 0);
    REQUIRE(vm.find("roadCreated").find("ok").toBool());
    REQUIRE(vm.find("points").toInt() > 0);
}
}  // namespace test_procgen_lsystem

// Contract section: procgen preview
#include "PrivateTestScope.h"

#include "ManualPreview.h"
// procgen_preview.cpp
namespace test_procgen_preview {
using namespace eve::procgen;
using namespace eve::graphics;

static Color colorForSemantic(int sem) {
    switch (sem) {
        case int(Semantic::Wall): return Color(0.22f, 0.24f, 0.30f, 1.f);
        case int(Semantic::Floor): return Color(0.72f, 0.68f, 0.55f, 1.f);
        case int(Semantic::Corridor): return Color(0.55f, 0.52f, 0.42f, 1.f);
        case int(Semantic::Water): return Color(0.25f, 0.45f, 0.85f, 1.f);
        case int(Semantic::Sand): return Color(0.85f, 0.78f, 0.45f, 1.f);
        case int(Semantic::Grass): return Color(0.35f, 0.65f, 0.30f, 1.f);
        case int(Semantic::Dirt): return Color(0.55f, 0.40f, 0.25f, 1.f);
        case int(Semantic::Stone): return Color(0.55f, 0.58f, 0.62f, 1.f);
        case int(Semantic::Snow): return Color(0.90f, 0.93f, 0.97f, 1.f);
        case int(Semantic::Door): return Color(0.75f, 0.45f, 0.20f, 1.f);
        default: return Color(0.05f, 0.05f, 0.07f, 1.f);
    }
}

static void drawGrid(Graphics *gfx, const Grid2D &grid, float originX, float originY, float cell) {
    const int w = grid.getWidth();
    const int h = grid.getHeight();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Color c = colorForSemantic(grid.getCell(x, y));
            gfx->drawSolidRect(originX + float(x) * cell, originY + float(y) * cell, cell, cell, c);
        }
    }
    // Markers for spawn / objects.
    for (int i = 0; i < grid.getObjectCount(); ++i) {
        const float ox = originX + grid.getObjectX(i) * cell;
        const float oy = originY + grid.getObjectY(i) * cell;
        gfx->drawSolidRect(ox - 2.f, oy - 2.f, 5.f, 5.f, Color(1.f, 0.3f, 0.3f, 1.f));
    }
}

TEST_CASE("procgen.render.dungeonCaveMazePreview") {
    GeneratorRegistry::instance().registerBuiltins();

    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 720;
    s.height   = 420;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    struct Algo {
        const char *id;
        int         w, h;
    };
    const Algo algos[] = {
        {"dungeon.bsp", 36, 28},
        {"cave.cellular", 36, 28},
        {"maze.backtrack", 31, 23},
        {"noise.terrain", 36, 28},
    };

    gfx->setBackgroundColorRGBA(0.06f, 0.07f, 0.09f, 1.f);
    gfx->setScreenReadbackEnabled(true);

    for (int ai = 0; ai < 4; ++ai) {
        const Algo &algo = algos[ai];
        Params      p;
        p.setSeed(42);
        p.setSize(algo.w, algo.h);
        if (std::string(algo.id) == "cave.cellular") {
            p.setInt("loops", 4);
            p.setFloat("fill", 0.45f);
        } else if (std::string(algo.id) == "noise.terrain") {
            p.setFloat("frequency", 5.f);
            p.setInt("octaves", 3);
        }

        Grid2D      grid;
        std::string err;
        REQUIRE(GeneratorRegistry::instance().generate(algo.id, p, grid, err));

        const float cell = std::min(
            16.f, std::min(float(gfx->getWidth() - 40) / float(algo.w), float(gfx->getHeight() - 40) / float(algo.h)));
        const float originX = (float(gfx->getWidth()) - float(algo.w) * cell) * 0.5f;
        const float originY = (float(gfx->getHeight()) - float(algo.h) * cell) * 0.5f;

        for (int frame = 0; frame < (eve::test::manualPreviewEnabled() ? 45 : 1); ++frame) {
            gfx->clearScreen();
            drawGrid(gfx, grid, originX, originY, cell);
            // Title bar strip so algorithm changes are readable without fonts.
            const float barW = float(gfx->getWidth()) * (0.15f + 0.2f * float(ai));
            gfx->drawSolidRect(12.f, 10.f, barW, 8.f, Color(0.9f, 0.85f, 0.4f, 1.f));
            gfx->present();


            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) break;
            }
            if (eve::test::manualPreviewEnabled()) SDL_Delay(16);
        }
        std::unique_ptr<eve::image::ImageData> image(gfx->newImageData());
        REQUIRE(image != nullptr);
        // ImageData uses framebuffer pixels; grid layout uses logical window units.
        const float   pixelScaleX = float(image->getWidth()) / float(gfx->getWidth());
        const float   pixelScaleY = float(image->getHeight()) / float(gfx->getHeight());
        std::set<int> semantics;
        int           matchingCells = 0;
        for (int y = 0; y < grid.getHeight(); ++y) {
            for (int x = 0; x < grid.getWidth(); ++x) {
                const int semantic = grid.getCell(x, y);
                semantics.insert(semantic);
                const auto expected = colorForSemantic(semantic);
                const auto actual   = image->getPixel(int((originX + (float(x) + 0.5f) * cell) * pixelScaleX),
                                                      int((originY + (float(y) + 0.5f) * cell) * pixelScaleY));
                if (std::fabs(actual.r - expected.r) < 0.08f && std::fabs(actual.g - expected.g) < 0.08f &&
                    std::fabs(actual.b - expected.b) < 0.08f)
                    ++matchingCells;
            }
        }
        REQUIRE(semantics.size() >= 2);
        // Object markers may cover a few cells; the grid must still be recognizable.
        REQUIRE(matchingCells > grid.getWidth() * grid.getHeight() * 9 / 10);
    }

    win->close();
}
}  // namespace test_procgen_preview

// Contract section: resource lifetime
#include "PrivateTestScope.h"

// resource_lifetime.cpp
namespace test_resource_lifetime {
#include "graphics/shaders/aa_fxaa_frag_spv.inc"


// ---------------------------------------------------------------------------
// Ownership contract under test
// ---------------------------------------------------------------------------
// Handles returned by Graphics::newTexture / newMesh* / newShader* are
// *borrowed*: the Graphics backend owns both the CPU facade and the GPU
// resource, and callers must not delete the handle directly (doing so double-
// frees the CPU facade the backend still tracks).
//
// Eager cleanup goes through releaseTexture / releaseMesh / releaseShader:
//   - returns true  when the resource was owned by this Graphics and has been
//     released (GPU resource freed, handle detached, gpuHandle == nullptr);
//   - returns false for null / foreign / internal / already-released handles;
//   - after a successful release the CPU facade is caller-owned and may be
//     deleted safely.
// ---------------------------------------------------------------------------

namespace {

using WindowSettings = eve::window::WindowSettings;

void openWindow(eve::window::Window *&win, eve::graphics::Graphics *&gfx, int w = 128, int h = 128) {
    win = eve::window::Window::create();
    gfx = eve::graphics::Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    WindowSettings s;
    s.width      = static_cast<uint16_t>(w);
    s.height     = static_cast<uint16_t>(h);
    s.centered   = true;
    s.fullscreen = false;
    s.resizable  = true;
    REQUIRE(win->setWindowSettings(s));
}

std::vector<uint8_t> rgbaPixels(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i]     = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = a;
    }
    return px;
}

}  // namespace

TEST_CASE("graphics.resourceLifetime.textureHandleIsBorrowedUntilReleased") {
    eve::window::Window     *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openWindow(win, gfx);

    auto                    px  = rgbaPixels(8, 8, 255, 0, 0, 255);
    eve::graphics::Texture *tex = gfx->newTexture(8, 8, px.data());
    REQUIRE(tex != nullptr);
    // A freshly created texture is backed by a live GPU resource.
    REQUIRE(tex->gpuHandle != nullptr);

    // Eager release detaches the handle and frees the GPU resource.
    CHECK(gfx->releaseTexture(tex));
    CHECK(tex->gpuHandle == nullptr);

    // Idempotent: a second release is a no-op.
    CHECK(!gfx->releaseTexture(tex));

    // After release the CPU facade is caller-owned and safe to delete.
    delete tex;

    // Null handles are rejected.
    CHECK(!gfx->releaseTexture(nullptr));

    win->close();
}

TEST_CASE("graphics.resourceLifetime.sharedTextureReusesContentKeyAndEvictsOnRelease") {
    eve::window::Window     *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openWindow(win, gfx);

    auto                  pixels = rgbaPixels(8, 8, 40, 90, 170, 255);
    eve::image::ImageData image(8, 8, "RGBA8", pixels.data(), false);
    CHECK(!gfx->newSharedTexture(nullptr, "invalid").ok());
    CHECK(!gfx->newSharedTexture(&image, "").ok());
    auto firstResult  = gfx->newSharedTexture(&image, "resource-lifetime-blue-v1");
    auto secondResult = gfx->newSharedTexture(&image, "resource-lifetime-blue-v1");
    REQUIRE(firstResult.ok());
    REQUIRE(secondResult.ok());
    auto *first  = &firstResult.value().get();
    auto *second = &secondResult.value().get();
    CHECK(second == first);

    REQUIRE(gfx->releaseTexture(first));
    delete first;
    auto replacementResult = gfx->newSharedTexture(&image, "resource-lifetime-blue-v1");
    REQUIRE(replacementResult.ok());
    auto *replacement = &replacementResult.value().get();
    REQUIRE(gfx->releaseTexture(replacement));
    delete replacement;
    win->close();
}

TEST_CASE("graphics.resourceLifetime.meshHandleIsBorrowedUntilReleased") {
    eve::window::Window     *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openWindow(win, gfx);

    eve::graphics::Mesh *mesh = gfx->newMeshCube(1.f);
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->gpuHandle != nullptr);

    CHECK(gfx->releaseMesh(mesh));
    CHECK(mesh->gpuHandle == nullptr);
    CHECK(!gfx->releaseMesh(mesh));
    delete mesh;
    CHECK(!gfx->releaseMesh(nullptr));

    win->close();
}

TEST_CASE("graphics.resourceLifetime.shaderHandleIsBorrowedUntilReleased") {
    eve::window::Window     *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openWindow(win, gfx);

    // Empty vertex stage selects the default textured vertex shader; the FXAA
    // fragment shader is a plain textured-pipeline post shader.
    std::vector<uint32_t> frag(aa_fxaa_frag_spv, aa_fxaa_frag_spv + aa_fxaa_frag_spv_count);
    if (gfx->getBackendName() == "webgpu") {
        // The WebGPU backend rejects SPIR-V custom shaders (browser WGSL only),
        // so there is no shader handle to release on that backend.
        bool threw = false;
        try {
            (void)gfx->newShaderFromSpv({}, frag);
        } catch (const std::exception &) {
            threw = true;
        }
        CHECK(threw);
        win->close();
        return;
    }
    eve::graphics::Shader *sh = gfx->newShaderFromSpv({}, frag);
    REQUIRE(sh != nullptr);
    REQUIRE(sh->gpuHandle != nullptr);

    CHECK(gfx->releaseShader(sh));
    CHECK(sh->gpuHandle == nullptr);
    CHECK(!gfx->releaseShader(sh));
    delete sh;
    CHECK(!gfx->releaseShader(nullptr));

    win->close();
}
}  // namespace test_resource_lifetime

// Contract section: rpg simulation
#include "PrivateTestScope.h"

#include "animation/Animation.h"
#include "animation/SpriteAnim.h"
#include "animation/SpriteClip.h"
#include "animation/SpriteSheet.h"
#include "building/BuildingDef.h"
#include "building/Ghost.h"
#include "building/PlacementSystem.h"
#include "building/PlacementWorld.h"
#include "map/Map.h"
#include "map/TileProjection.h"
#include "map/TileSystem.h"
#include "rpg/RPG.h"
#include "rpg/Settlement.h"
// rpg_simulation.cpp
namespace test_rpg_simulation {
// RPG cross-module simulation: a tiny "village defense" scenario that drives
// tilemap, sprite animation, coordinate conversion, 2D normals/lighting,
// buffs, and building placement together — the same surface an RPG game would use.


// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::rpg;
using namespace eve::map;
using namespace eve::building;
using namespace eve::graphics;
using namespace eve::animation;

namespace {

bool approxEq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }
bool approxEqD(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }

void hideAllTileLayers() {
    if (ecs::current()->getManager<TileLayer>() == nullptr) return;
    auto view = ecs::View<TileLayer, TileLayer::Draw>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [draw]   = *it;
        draw->visible = false;
    }
}

void setupVitals(RPGActor *actor, double maxHp, double maxMp, double maxStamina) {
    actor->setBaseAttribute("health", maxHp);
    actor->setBaseAttribute("mana", maxMp);
    actor->setBaseAttribute("stamina", maxStamina);
    actor->addAttributeModifier("health", "system", "clampMin", 0.0, 900);
    actor->addAttributeModifier("health", "system", "clampMax", maxHp, 901);
    actor->addAttributeModifier("mana", "system", "clampMin", 0.0, 900);
    actor->addAttributeModifier("mana", "system", "clampMax", maxMp, 901);
    actor->addAttributeModifier("stamina", "system", "clampMin", 0.0, 900);
    actor->addAttributeModifier("stamina", "system", "clampMax", maxStamina, 901);
}

float luma(const Color &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

Texture *makeSolidTexture(Graphics *gfx, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4, 255);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i + 0] = r;
        px[i + 1] = g;
        px[i + 2] = b;
    }
    eve::image::ImageData imageData(w, h, "RGBA8");
    std::memcpy(imageData.getData(), px.data(), px.size());
    return gfx->newTexture(&imageData);
}

Texture *makeBiasedNormal(Graphics *gfx, int w, int h) {
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t i  = (size_t(y) * size_t(w) + size_t(x)) * 4;
            float  nx = (x < w / 2) ? -0.7f : 0.7f;
            float  ny = 0.f;
            float  nz = 0.7f;
            px[i + 0] = uint8_t((nx * 0.5f + 0.5f) * 255.f);
            px[i + 1] = uint8_t((ny * 0.5f + 0.5f) * 255.f);
            px[i + 2] = uint8_t((nz * 0.5f + 0.5f) * 255.f);
            px[i + 3] = 255;
        }
    }
    eve::image::ImageData imageData(w, h, "RGBA8");
    std::memcpy(imageData.getData(), px.data(), px.size());
    return gfx->newTexture(&imageData);
}

}  // namespace

/**
 * Mini RPG: defend a village tilemap.
 *
 * Timeline (headless, deterministic):
 *  1) Build ortho tilemap + collect tile draw items
 *  2) Convert tile ↔ world ↔ screen ↔ placement cell
 *  3) Drive a walk sprite clip while the hero "moves" toward the gate
 *  4) Place a watchtower via ghost snap on the building grid
 *  5) Attach albedo+normal sprites + point light; collectSprites must pick litPath
 *  6) Buff → cast fireball (burn DOT) → settlement damage → tick until enemy dies
 *
 * Optional GPU append: if a Vulkan window is available, render the lit sprite and
 * assert normal-map side brightness (same contract as Lighting2D.normalMapLitSideBrighter).
 */
TEST_CASE("rpg.simulation.villageDefenseSystems") {
    constexpr float kTile  = 32.f;
    constexpr int   kMapW  = 12;
    constexpr int   kMapH  = 8;
    constexpr float kViewW = 320.f;
    constexpr float kViewH = 240.f;

    // ------------------------------------------------------------------
    // 1) Tilemap: grass fill + path + wall gate
    // ------------------------------------------------------------------
    hideAllTileLayers();
    auto      *mapMod = Map::create();
    TileLayer *layer  = mapMod->newLayer(kMapW, kMapH, kTile, kTile);
    REQUIRE(layer != nullptr);
    layer->config()->orientation = MapOrientation::Orthogonal;
    layer->setOrigin(0.f, 0.f);
    layer->setVisible(true);
    layer->fill(1);                                           // grass
    for (int x = 0; x < kMapW; ++x) layer->setTile(x, 4, 2);  // road
    layer->setTile(5, 3, 3);                                  // wall
    layer->setTile(6, 3, 3);
    layer->setTile(7, 3, 0);  // gate opening
    CHECK_EQ(layer->getTile(0, 0), 1);
    CHECK_EQ(layer->getTile(7, 3), 0);
    CHECK_EQ(layer->getTile(5, 4), 2);

    std::vector<DrawItem2D> tileItems;
    TileRenderSystem::collect(tileItems);
    REQUIRE(tileItems.size() >= 2);
    // Non-empty tiles only; gate cell (7,3) is 0 and must not appear.
    size_t tilesBefore = tileItems.size();
    layer->setTile(7, 3, 4);
    tileItems.clear();
    TileRenderSystem::collect(tileItems);
    CHECK_EQ(tileItems.size(), tilesBefore + 1);
    layer->setTile(7, 3, 0);

    // ------------------------------------------------------------------
    // 2) Coordinate conversion: tile ↔ world ↔ screen ↔ placement cell
    // ------------------------------------------------------------------
    float heroWx = 0.f, heroWy = 0.f;
    layer->tileToWorld(2, 4, heroWx, heroWy);
    CHECK(approxEq(heroWx, 2.f * kTile));
    CHECK(approxEq(heroWy, 4.f * kTile));

    int backTx = -1, backTy = -1;
    layer->worldToTile(heroWx + kTile * 0.5f, heroWy + kTile * 0.5f, backTx, backTy);
    CHECK_EQ(backTx, 2);
    CHECK_EQ(backTy, 4);

    // Free function path must agree with layer wrappers.
    float projWx = 0.f, projWy = 0.f;
    tileToWorld(*layer->config(), 2, 4, projWx, projWy);
    CHECK(approxEq(projWx, heroWx));
    CHECK(approxEq(projWy, heroWy));

    auto *cam           = Camera2D::createCamera();
    cam->data()->active = true;
    cam->data()->x      = heroWx + 64.f;
    cam->data()->y      = heroWy;
    cam->data()->zoom   = 1.f;
    cam->setAmbient(0.12f, 0.12f, 0.14f);

    float screenX = cam->worldToScreenX(heroWx, heroWy, kViewW, kViewH);
    float screenY = cam->worldToScreenY(heroWx, heroWy, kViewW, kViewH);
    float roundWx = cam->screenToWorldX(screenX, screenY, kViewW, kViewH);
    float roundWy = cam->screenToWorldY(screenX, screenY, kViewW, kViewH);
    CHECK(approxEq(roundWx, heroWx));
    CHECK(approxEq(roundWy, heroWy));

    // Cursor over empty ground near the gate → placement cell.
    float pickWx = cam->screenToWorldX(kViewW * 0.5f, kViewH * 0.5f, kViewW, kViewH);
    float pickWy = cam->screenToWorldY(kViewW * 0.5f, kViewH * 0.5f, kViewW, kViewH);
    int   pickTx = -1, pickTy = -1;
    layer->worldToTile(pickWx, pickWy, pickTx, pickTy);
    CHECK(pickTx >= 0);
    CHECK(pickTy >= 0);

    // ------------------------------------------------------------------
    // 3) Sprite animation: walk cycle while advancing along the road
    // ------------------------------------------------------------------
    auto                        *animMod = Animation::create();
    std::unique_ptr<SpriteSheet> sheet(animMod->newSpriteSheet());
    sheet->setGrid(4, 1, 16, 16);
    std::unique_ptr<SpriteClip> walk(animMod->newSpriteClip("walk"));
    walk->setLoop(true);
    walk->addFrame(0, 0.1f);
    walk->addFrame(1, 0.1f);
    walk->addFrame(2, 0.1f);
    walk->addFrame(3, 0.1f);

    std::unique_ptr<SpriteAnim> walker(animMod->newSpriteAnim());
    walker->setSheet(sheet.get());
    Quad walkQuad;
    walker->bindQuad(&walkQuad);
    walker->play(walk.get());
    CHECK(walker->isPlaying());
    CHECK_EQ(walker->getSheetFrame(), 0);

    // Move hero three tiles east; animation should advance with dt.
    float moveWx = heroWx;
    float moveWy = heroWy;
    for (int step = 0; step < 3; ++step) {
        moveWx += kTile;
        animMod->update(0.12f);
    }
    CHECK_EQ(walker->getClipFrame(), 3);  // 0.36s into 0.4s loop → frame 3
    CHECK_EQ(walkQuad.getX(), 48);        // frame 3 * 16
    int moveTx = -1, moveTy = -1;
    layer->worldToTile(moveWx + 1.f, moveWy + 1.f, moveTx, moveTy);
    CHECK_EQ(moveTx, 5);
    CHECK_EQ(moveTy, 4);

    // ------------------------------------------------------------------
    // 4) Building placement: watchtower snapped to the same grid
    // ------------------------------------------------------------------
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
    PlacementSystem::ensureBuiltins();

    BuildingDefinition tower;
    tower.id             = "sim.watchtower";
    tower.displayName    = "Watchtower";
    tower.footprintW     = 1;
    tower.footprintH     = 1;
    tower.snapMode       = "grid";
    tower.tags           = {"defense", "tower"};
    tower.requireTerrain = {1};
    BuildingRegistry::registerBuilding(tower);

    PlacementWorld world(kMapW, kMapH, kTile);
    world.setId("village");
    world.setOrigin(0.f, 0.f);
    world.fillTerrain(1);  // buildable ground
    // Mirror road as non-buildable for a soft constraint check (terrain 0).
    for (int x = 0; x < kMapW; ++x) world.setTerrain(x, 4, 0);

    CHECK(!world.canPlace("sim.watchtower", 5, 4, 0.f));
    CHECK_EQ(world.canPlaceReason("sim.watchtower", 5, 4, 0.f), "terrain_mismatch");

    // Player clicks near tile (7,2) — ghost snaps to cell, then place.
    Ghost ghost;
    ghost.setBuildingId("sim.watchtower");
    float clickWx = 7.f * kTile + 10.f;
    float clickWy = 2.f * kTile + 6.f;
    ghost.setFromWorld(&world, clickWx, clickWy);
    CHECK_EQ(ghost.getCellX(), 7);
    CHECK_EQ(ghost.getCellY(), 2);
    CHECK(approxEq(ghost.getWorldX(), 7.f * kTile));
    CHECK(approxEq(ghost.getWorldY(), 2.f * kTile));
    CHECK(ghost.validate(&world));

    // Placement cell ↔ world must agree with tile projection for this ortho map.
    CHECK_EQ(world.worldToCellX(ghost.getWorldX()), 7);
    CHECK_EQ(world.worldToCellY(ghost.getWorldY()), 2);
    CHECK(approxEq(world.cellToWorldX(7), 7.f * kTile));
    CHECK(approxEq(world.cellToWorldY(2), 2.f * kTile));
    float tileGateWx = 0.f, tileGateWy = 0.f;
    layer->tileToWorld(7, 2, tileGateWx, tileGateWy);
    CHECK(approxEq(tileGateWx, world.cellToWorldX(7)));
    CHECK(approxEq(tileGateWy, world.cellToWorldY(2)));

    int towerId = world.placeGhost(&ghost);
    REQUIRE(towerId > 0);
    CHECK_EQ(world.getBuildingCount(), 1);
    CHECK(!world.isCellEmpty(7, 2));
    CHECK_EQ(PlacementSystem::events().size(), size_t(1));
    CHECK_EQ(PlacementSystem::events()[0].action, "place");
    CHECK_EQ(PlacementSystem::events()[0].buildingId, "sim.watchtower");

    // Occupancy conflict: cannot stack another tower on the same cell.
    CHECK(!world.canPlace("sim.watchtower", 7, 2, 0.f));
    CHECK_EQ(world.canPlaceReason("sim.watchtower", 7, 2, 0.f), "occupied");

    // ------------------------------------------------------------------
    // 5) 2D normals + lighting: litPath collection + sort order (headless)
    // ------------------------------------------------------------------
    Texture albedoTex;
    albedoTex.width  = 64;
    albedoTex.height = 64;
    Texture normalTex;
    normalTex.width  = 64;
    normalTex.height = 64;

    auto *heroSprite                    = Renderable2D::create();
    heroSprite->transform()->x          = moveWx;
    heroSprite->transform()->y          = moveWy;
    heroSprite->sprite()->width         = 16.f;
    heroSprite->sprite()->height        = 16.f;
    heroSprite->sprite()->texture       = &albedoTex;
    heroSprite->sprite()->normalTexture = &normalTex;
    heroSprite->sprite()->quad          = &walkQuad;
    heroSprite->sprite()->receiveLight  = true;
    heroSprite->sprite()->visible       = true;
    heroSprite->sprite()->layer         = 10;
    heroSprite->sprite()->camera        = cam;

    auto *unlitProp                    = Renderable2D::create();
    unlitProp->transform()->x          = world.getBuildingWorldX(towerId);
    unlitProp->transform()->y          = world.getBuildingWorldY(towerId);
    unlitProp->sprite()->width         = 24.f;
    unlitProp->sprite()->height        = 32.f;
    unlitProp->sprite()->texture       = &albedoTex;
    unlitProp->sprite()->normalTexture = nullptr;
    unlitProp->sprite()->receiveLight  = true;
    unlitProp->sprite()->visible       = true;
    unlitProp->sprite()->layer         = 10;
    unlitProp->sprite()->camera        = cam;

    auto *torch = Light2D::createLight("point");
    torch->setPosition(moveWx + 8.f, moveWy + 8.f);
    torch->setColor(1.f, 0.85f, 0.6f, 2.0f);
    torch->setRadius(96.f);
    torch->setEnabled(true);
    CHECK_EQ(torch->getType(), "point");
    CHECK(approxEq(torch->getRadius(), 96.f));
    CHECK(torch->isEnabled());

    std::vector<DrawItem2D> scene;
    TileRenderSystem::collect(scene);
    RenderSystem::collectSprites(scene);
    REQUIRE(scene.size() >= 2);

    bool foundLit           = false;
    bool foundUnlitTextured = false;
    for (const auto &it : scene) {
        if (it.texture == &albedoTex && it.normal == &normalTex) {
            CHECK(it.litPath);
            CHECK(it.receiveLight);
            CHECK(it.hasUV);
            foundLit = true;
        }
        if (it.texture == &albedoTex && it.normal == nullptr && it.w == 24.f) {
            CHECK(!it.litPath);
            foundUnlitTextured = true;
        }
    }
    CHECK(foundLit);
    CHECK(foundUnlitTextured);

    // Sort contract: same canvas/layer → unlit before lit (GPU batches lit path last).
    sortDrawItems2D(scene);
    int firstLit           = -1;
    int lastUnlitSameLayer = -1;
    for (int i = 0; i < int(scene.size()); ++i) {
        if (scene[i].layer != 10 || scene[i].canvas != nullptr) continue;
        if (!scene[i].litPath) lastUnlitSameLayer = i;
        if (scene[i].litPath && firstLit < 0) firstLit = i;
    }
    REQUIRE(firstLit >= 0);
    REQUIRE(lastUnlitSameLayer >= 0);
    CHECK(lastUnlitSameLayer < firstLit);

    // Force unlit when receiveLight=false even with a normal map.
    heroSprite->sprite()->receiveLight = false;
    scene.clear();
    RenderSystem::collectSprites(scene);
    for (const auto &it : scene) {
        if (it.texture == &albedoTex && it.normal == &normalTex) {
            CHECK(!it.litPath);
            CHECK(!it.receiveLight);
        }
    }
    heroSprite->sprite()->receiveLight = true;

    // ------------------------------------------------------------------
    // 6) Buff + combat loop (mirrors examples/rpg dungeon survival)
    // ------------------------------------------------------------------
    auto *rpg = RPG::create();
    rpg->clearEffectDefinitions();
    rpg->clearSkillDefinitions();
    SettlementPipeline::clearPipeline("sim.damage");

    int effectsLoaded = rpg->registerEffectsFromJson(R"([
        {"id":"sim.buff.power","durationPolicy":"duration","duration":6.0,
         "stackPolicy":"refresh",
         "modifiers":[{"attribute":"attack","op":"add","value":8.0}],
         "tags":["buff"]},
        {"id":"sim.dot.burn","durationPolicy":"duration","duration":3.0,"period":1.0,
         "stackPolicy":"stack","maxStacks":3,"tags":["dot","debuff"]},
        {"id":"sim.instant.heal","durationPolicy":"instant",
         "modifiers":[{"attribute":"health","op":"add","value":20.0}]}
    ])");
    CHECK_EQ(effectsLoaded, 3);

    int skillsLoaded = rpg->registerSkillsFromJson(R"([
        {"id":"sim.strike","cooldown":0.2,"castTime":0.0,
         "costs":[],"grantedEffects":[],"tags":["attack"]},
        {"id":"sim.fireball","cooldown":1.0,"castTime":0.0,
         "costs":[{"attribute":"mana","amount":10.0}],
         "grantedEffects":["sim.dot.burn"],"tags":["attack","magic"]},
        {"id":"sim.power","cooldown":2.0,"castTime":0.0,
         "costs":[{"attribute":"stamina","amount":5.0}],
         "grantedEffects":["sim.buff.power"],"tags":["buff"]}
    ])");
    CHECK_EQ(skillsLoaded, 3);

    SettlementPipeline::registerStage("sim.damage", "base", 0, [](SettlementContext &ctx) {
        ctx.set("result", ctx.get("attack") - ctx.get("defense"));
    });
    SettlementPipeline::registerStage("sim.damage", "floor", 10, [](SettlementContext &ctx) {
        if (ctx.get("result") < 1.0) ctx.set("result", 1.0);
    });

    RPGActor *player = rpg->newActor();
    RPGActor *enemy  = rpg->newActor();
    setupVitals(player, 100.0, 40.0, 50.0);
    setupVitals(enemy, 60.0, 0.0, 0.0);
    player->setBaseAttribute("attack", 14.0);
    player->setBaseAttribute("defense", 3.0);
    enemy->setBaseAttribute("attack", 8.0);
    enemy->setBaseAttribute("defense", 2.0);
    player->learnSkill("sim.strike");
    player->learnSkill("sim.fireball");
    player->learnSkill("sim.power");

    // Power stance buff raises attack before the opening strike.
    REQUIRE(player->beginCastSkill("sim.power", player));
    rpg->update(0.016f);
    CHECK(player->hasEffect("sim.buff.power"));
    CHECK(approxEqD(player->getFinalAttribute("attack"), 22.0));
    CHECK_EQ(rpg->getCastEventCount(), 1);
    CHECK_EQ(rpg->getCastEventSkillId(0), "sim.power");

    auto applyStrikeDamage = [&](RPGActor *caster, RPGActor *target) {
        SettlementContext ctx;
        ctx.set("attack", caster->getFinalAttribute("attack"));
        ctx.set("defense", target->getFinalAttribute("defense"));
        SettlementPipeline::run("sim.damage", ctx);
        double dmg = ctx.get("result");
        target->modifyBaseAttribute("health", -dmg);
        return dmg;
    };

    // Opening strike with buffed attack: 22 - 2 = 20
    REQUIRE(player->beginCastSkill("sim.strike", enemy));
    rpg->update(0.016f);
    double strikeDmg = applyStrikeDamage(player, enemy);
    CHECK(approxEqD(strikeDmg, 20.0));
    CHECK(approxEqD(enemy->getFinalAttribute("health"), 40.0));

    // Fireball spends mana and applies burn DOT.
    double manaBefore = player->getFinalAttribute("mana");
    REQUIRE(player->beginCastSkill("sim.fireball", enemy));
    // The zero-cast-time skill resolves synchronously. Poll the event cache
    // without aging the newly-created damage-over-time effect.
    rpg->update(0.0f);
    CHECK(enemy->hasEffect("sim.dot.burn"));
    CHECK(player->getFinalAttribute("mana") < manaBefore);

    // Burn ticks: each period deals 5 fixed DOT damage (game rule owned by sim).
    int burnTicks = 0;
    for (int frame = 0; frame < 4; ++frame) {
        rpg->update(1.0f);
        for (int i = 0; i < rpg->getTickEventCount(); ++i) {
            if (rpg->getTickEventEffectId(i) == "sim.dot.burn" && rpg->getTickEventActor(i) == enemy) {
                enemy->modifyBaseAttribute("health", -5.0);
                ++burnTicks;
            }
        }
        // Keep walk animation alive across combat frames.
        animMod->update(0.1f);
    }
    CHECK_GE(burnTicks, 3);
    // 40 - 5*3 = 25 (or lower if an extra tick landed)
    CHECK(enemy->getFinalAttribute("health") <= 25.0 + 1e-9);
    CHECK(enemy->getFinalAttribute("health") > 0.0);

    // Finish with strikes until enemy falls; heal potion mid-fight.
    const int healResult = player->applyEffect("sim.instant.heal");
    CHECK_EQ(healResult, 0);
    CHECK_EQ(player->getFinalAttribute("health"), 100.0);

    int safety = 0;
    while (enemy->getFinalAttribute("health") > 0.0 && safety < 20) {
        if (player->canCastSkill("sim.strike")) {
            player->beginCastSkill("sim.strike", enemy);
            rpg->update(0.25f);
            applyStrikeDamage(player, enemy);
        } else {
            rpg->update(0.25f);
        }
        ++safety;
    }
    CHECK(enemy->getFinalAttribute("health") <= 0.0);
    CHECK(player->getFinalAttribute("health") > 0.0);
    CHECK(world.hasBuilding(towerId));
    CHECK_GE(walker->getSheetFrame(), 0);

    // ------------------------------------------------------------------
    // Optional GPU: normal-map lit side brighter (soft-skip without window)
    // ------------------------------------------------------------------
    {
        auto *win = eve::window::Window::create();
        auto *gfx = Graphics::create();
        if (win && gfx) {
            eve::window::WindowSettings ws;
            ws.width    = 320;
            ws.height   = 240;
            ws.centered = true;
            if (win->setWindowSettings(ws)) {
                Canvas *rt = gfx->newCanvas(128, 64);
                REQUIRE(rt != nullptr);

                auto *gpuCam           = Camera2D::createCamera();
                gpuCam->data()->canvas = rt;
                gpuCam->data()->active = true;
                gpuCam->data()->x      = 64.f;
                gpuCam->data()->y      = 32.f;
                gpuCam->data()->zoom   = 1.f;
                gpuCam->setAmbient(0.08f, 0.08f, 0.08f);
                gpuCam->data()->r = 0.f;
                gpuCam->data()->g = 0.f;
                gpuCam->data()->b = 0.f;
                gpuCam->data()->a = 1.f;

                Texture *gpuAlbedo = makeSolidTexture(gfx, 32, 32, 220, 220, 220);
                Texture *gpuNormal = makeBiasedNormal(gfx, 32, 32);
                REQUIRE(gpuAlbedo != nullptr);
                REQUIRE(gpuNormal != nullptr);

                // Hide headless sprites so they do not pollute the offscreen pass.
                heroSprite->sprite()->visible = false;
                unlitProp->sprite()->visible  = false;
                torch->setEnabled(false);
                cam->data()->active = false;

                auto *litSp                    = Renderable2D::create();
                litSp->transform()->x          = 32.f;
                litSp->transform()->y          = 8.f;
                litSp->sprite()->width         = 64.f;
                litSp->sprite()->height        = 48.f;
                litSp->sprite()->texture       = gpuAlbedo;
                litSp->sprite()->normalTexture = gpuNormal;
                litSp->sprite()->receiveLight  = true;
                litSp->sprite()->canvas        = rt;
                litSp->sprite()->visible       = true;

                auto *sideLight = Light2D::createLight("point");
                sideLight->setCanvas(rt);
                sideLight->setPosition(110.f, 32.f);
                sideLight->setColor(1.f, 1.f, 1.f, 3.0f);
                sideLight->setRadius(90.f);
                sideLight->setEnabled(true);

                RenderSystem::render(*gfx);
                float leftL  = luma(rt->getPixel(40, 32));
                float rightL = luma(rt->getPixel(88, 32));
                // Software ICDs (Lavapipe) may not shade lit2d; skip when both samples are black.
                if (leftL + rightL > 1e-4f) {
                    CHECK(rightL > leftL + 0.05f);
                }

                litSp->sprite()->visible = false;
                sideLight->setEnabled(false);
                gpuCam->data()->active = false;
                win->close();
            }
        }
    }

    // Cleanup registries / actors (process-global).
    player->release();
    enemy->release();
    rpg->clearEffectDefinitions();
    rpg->clearSkillDefinitions();
    SettlementPipeline::clearPipeline("sim.damage");
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
    layer->setVisible(false);
    heroSprite->sprite()->visible = false;
    unlitProp->sprite()->visible  = false;
    torch->setEnabled(false);
    cam->data()->active = false;
}
}  // namespace test_rpg_simulation

// Contract section: procgen pointset
#include "PrivateTestScope.h"

#include "procgen/heightmap/Heightmap.h"
// procgen_pointset.cpp
namespace test_procgen_pointset {
using namespace eve::procgen;

namespace {

using PointSetHandle = ProcgenPointSetHandleRef;
using ContextHandle  = ProcgenContextHandleRef;

/** @brief Owns one module point-set handle for the duration of a test. */
struct PointSetLease {
    Procgen*       owner = nullptr;
    PointSetHandle handle{};

    PointSetLease() = default;
    PointSetLease(Procgen& proc, PointSetHandle value) : owner(&proc), handle(value) {}
    PointSetLease(const PointSetLease&)            = delete;
    PointSetLease& operator=(const PointSetLease&) = delete;
    PointSetLease(PointSetLease&& other) noexcept : owner(other.owner), handle(other.handle) {
        other.owner  = nullptr;
        other.handle = {};
    }
    PointSetLease& operator=(PointSetLease&& other) noexcept {
        if (this == &other) return *this;
        reset();
        owner        = other.owner;
        handle       = other.handle;
        other.owner  = nullptr;
        other.handle = {};
        return *this;
    }
    ~PointSetLease() { reset(); }

    void reset() noexcept {
        if (!owner || !handle.isValid()) return;
        auto result = owner->releasePointSet(handle);
        result.ignore("test point-set lease cleanup");
        owner  = nullptr;
        handle = {};
    }

    [[nodiscard]] eve::script::Borrowed<PointSet> view() const noexcept {
        return owner ? owner->resolvePointSet(handle) : eve::script::Borrowed<PointSet>();
    }
};

/** @brief Converts a successful point-set allocation into an owned test lease. */
PointSetLease requirePointSet(Procgen& proc, eve::Result<PointSetHandle>&& result) {
    const bool ok = result.ok();
    if (!ok) {
        const eve::Diagnostic* diagnostic = result.error();
        REQUIRE(diagnostic != nullptr);
        if (diagnostic) CHECK(diagnostic->isError());
    }
    REQUIRE(ok);
    return PointSetLease(proc, std::move(result).takeValue());
}

/** @brief Converts a successful rebuild-context allocation into a test lease. */
struct ContextLease {
    Procgen*      owner = nullptr;
    ContextHandle handle{};

    ContextLease() = default;
    ContextLease(Procgen& proc, ContextHandle value) : owner(&proc), handle(value) {}
    ContextLease(const ContextLease&)            = delete;
    ContextLease& operator=(const ContextLease&) = delete;
    ContextLease(ContextLease&& other) noexcept : owner(other.owner), handle(other.handle) {
        other.owner  = nullptr;
        other.handle = {};
    }
    ContextLease& operator=(ContextLease&& other) noexcept {
        if (this == &other) return *this;
        reset();
        owner        = other.owner;
        handle       = other.handle;
        other.owner  = nullptr;
        other.handle = {};
        return *this;
    }
    ~ContextLease() { reset(); }

    void reset() noexcept {
        if (!owner || !handle.isValid()) return;
        auto result = Procgen::release(handle);
        result.ignore("test context lease cleanup");
        owner  = nullptr;
        handle = {};
    }

    [[nodiscard]] eve::script::Borrowed<ProcgenContext> view() const noexcept {
        return owner ? Procgen::resolve(handle) : eve::script::Borrowed<ProcgenContext>();
    }
};

ContextLease requireContext(Procgen& proc, eve::Result<ContextHandle>&& result) {
    const bool ok = result.ok();
    if (!ok) {
        const eve::Diagnostic* diagnostic = result.error();
        REQUIRE(diagnostic != nullptr);
        if (diagnostic) CHECK(diagnostic->isError());
    }
    REQUIRE(ok);
    return ContextLease(proc, std::move(result).takeValue());
}

template <typename T>
void checkResult(const eve::Result<T>& result, eve::DiagnosticCode expected = eve::DiagnosticCode::None) {
    const bool ok = result.ok();
    CHECK(ok);
    if (!ok) {
        const eve::Diagnostic* diagnostic = result.error();
        REQUIRE(diagnostic != nullptr);
        if (diagnostic && expected != eve::DiagnosticCode::None) CHECK_EQ(diagnostic->code(), expected);
    }
}

}  // namespace


TEST_CASE("procgen.script.pointsetPolygonAndBatchApiAreBound") {
    // The point-set pipeline the user docs describe (pointset-pipeline.md): polygon
    // filtering and its `invert` twin, heightmap projection, and the manual batch
    // publication counters. All of them existed in C++ and were simply not exposed, so
    // the documented calls could not run.
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        procgen <- eve.Procgen();
        pts <- procgen.poissonDisk(40, 40, 5.0, 7, 200).value;
        total <- pts.getCount();

        poly <- procgen.newPointSet().value;
        poly.add(-10.0, 0.0, -10.0);
        poly.add( 10.0, 0.0, -10.0);
        poly.add( 10.0, 0.0,  10.0);
        poly.add(-10.0, 0.0,  10.0);

        inside  <- procgen.filterPolygon(pts, poly);
        outside <- procgen.excludePolygon(pts, poly);
        insideOk  <- inside.ok && inside.value.getCount() >= 0;
        outsideOk <- outside.ok && outside.value.getCount() >= 0;
        // `filterPolygon` keeps what is inside and `excludePolygon` the complement, so
        // the two partitions must add up to the input.
        partitionsSum <- inside.value.getCount() + outside.value.getCount();

        hm <- procgen.newHeightmap(32, 32).value;
        projected <- procgen.projectToHeightmap(pts, hm, 0.0, 0.0, 2.0, 80.0);
        projectedOk <- projected.ok && projected.value.getCount() >= 0;

        // No scene sink is provided here, so publication must report a failure instead
        // of crashing, and the counters stay zero for a batch that never landed.
        published <- procgen.publishInstances("test/manual", pts, "asset", "oak");
        publishReported <- !published.ok;
        count   <- procgen.getPublishedInstanceCount("test/manual");
        created <- procgen.getPublishedCreatedCount("test/manual");
        reused  <- procgen.getPublishedReusedCount("test/manual");
        removed <- procgen.getPublishedRemovedCount("test/manual");
    )"));

    REQUIRE(vm.find("total").toInt() > 0);
    REQUIRE(vm.find("insideOk").toBool());
    REQUIRE(vm.find("outsideOk").toBool());
    REQUIRE_EQ(vm.find("partitionsSum").toInt(), vm.find("total").toInt());
    REQUIRE(vm.find("projectedOk").toBool());
    REQUIRE(vm.find("publishReported").toBool());
    REQUIRE_EQ(vm.find("count").toInt(), 0);
    REQUIRE_EQ(vm.find("created").toInt(), 0);
    REQUIRE_EQ(vm.find("reused").toInt(), 0);
    REQUIRE_EQ(vm.find("removed").toInt(), 0);
}
}  // namespace test_procgen_pointset
