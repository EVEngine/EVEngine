#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/Graphics.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleRuntime.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#include "window/Window.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace eve::particles;

namespace {

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

}  // namespace

TEST_CASE("particles.gpu.unsupportedFeaturesFallBackToCpu") {
    auto* emitter = Particles::create()->newEmitter(256);
    emitter->setRandomSeed(8128);
    emitter->setGpuSimulation(true);
    emitter->setCollision("bounce", 2.f, 0.5f, 0.f);
    emitter->setEmissionRate(120.f);
    emitter->setParticleLifetime(2.f, 2.f);
    emitter->start();

    ParticleSimSystem::update(0.25f);

    CHECK(emitter->getGpuSimulation());
    CHECK(!emitter->gpuSim()->residentActive);
    CHECK_EQ(emitter->gpuSim()->residentHandle, std::uint64_t(0));
    REQUIRE_GT(emitter->getCount(), 0);
    REQUIRE_EQ(emitter->getCount(), emitter->sim()->alive);
}

TEST_CASE("particles.gpu.residentSimulationAndIndirectRendering") {
    auto* window = eve::window::Window::create();
    auto* gfx    = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 800;
    settings.height   = 520;
    settings.centered = true;
    REQUIRE(window->setWindowSettings(settings));
    REQUIRE(gfx->supportsGpuParticles());
    gfx->setScreenReadbackEnabled(true);

    const std::uint8_t whitePixel[4] = {255, 255, 255, 255};
    auto*              whiteTexture  = gfx->newTexture(1, 1, whitePixel);
    REQUIRE(whiteTexture != nullptr);

    const std::array<std::array<float, 3>, 6> colors{{
        {0.2f, 0.75f, 1.f},
        {0.45f, 0.25f, 1.f},
        {1.f, 0.18f, 0.5f},
        {1.f, 0.55f, 0.08f},
        {0.3f, 1.f, 0.5f},
        {0.95f, 0.95f, 0.3f},
    }};
    std::vector<ParticleEmitter*>             emitters;
    emitters.reserve(colors.size());
    for (std::size_t i = 0; i < colors.size(); ++i) {
        auto* emitter = Particles::create()->newEmitter(8192);
        emitter->setTexture(whiteTexture);
        emitter->setRandomSeed(20260826 + int(i));
        emitter->setGpuSimulation(true);
        emitter->setPosition(115.f + float(i) * 114.f, 400.f);
        emitter->setEmissionRate(1500.f);
        emitter->setMaxSpawnPerFrame(32);
        emitter->setParticleLifetime(0.9f, 1.45f);
        emitter->setParticleSize(9.f, 18.f);
        emitter->setSizes(1.f, 0.12f);
        emitter->setSpeed(95.f, 245.f);
        emitter->setSpread(6.2831853f);
        emitter->setGravity(0.f, 105.f);
        emitter->setDamping(0.16f);
        emitter->setSpin(-4.f, 4.f);
        emitter->setBlendMode("additive");
        emitter->setColorStart(colors[i][0], colors[i][1], colors[i][2], 0.95f);
        emitter->setColorEnd(colors[i][0] * 0.25f, colors[i][1] * 0.12f, colors[i][2] * 0.3f, 0.f);
        emitter->start();
        emitters.push_back(emitter);
    }

    gfx->setBackgroundColorRGBA(0.008f, 0.012f, 0.035f, 1.f);
    constexpr float dt = 1.f / 60.f;
    for (int frame = 0; frame < 150; ++frame) {
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
    for (auto* emitter : emitters) {
        auto gpu = emitter->gpuSim();
        REQUIRE(gpu->residentActive);
        REQUIRE(emitter->isGpuSimulationActive());
        REQUIRE_NE(gpu->residentHandle, std::uint64_t(0));
        REQUIRE_EQ(emitter->sim()->alive, 0);
        const auto gpuStats = gfx->getGpuParticleStats(gpu->residentHandle);
        submittedFrames += gpuStats.submittedFrames;
        delayedAlive += gpuStats.alive;
        indirectInstances += gpuStats.instances;
        estimatedAlive += gpu->estimatedAlive;
    }
    REQUIRE_GT(submittedFrames, std::uint64_t(800));
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
    ParticleSimSystem::update(dt);
    CHECK(!emitters.front()->isGpuSimulationActive());
    REQUIRE_GT(emitters.front()->sim()->alive, 0);

    for (auto* emitter : emitters) {
        emitter->setVisible(false);
        emitter->setGpuSimulation(false);
    }
    window->close();
}

TEST_CASE("particles.gpu.mainThreadCrossoverAtSixteenThousandParticles") {
    auto* window = eve::window::Window::create();
    auto* gfx    = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 640;
    settings.height   = 360;
    settings.centered = true;
    REQUIRE(window->setWindowSettings(settings));
    REQUIRE(gfx->supportsGpuParticles());
    gfx->setVSync(false);

    const std::uint8_t whitePixel[4] = {255, 255, 255, 255};
    auto*              whiteTexture  = gfx->newTexture(1, 1, whitePixel);
    REQUIRE(whiteTexture != nullptr);

    auto makeEmitters = [&](bool gpuEnabled) {
        std::vector<ParticleEmitter*> result;
        for (int i = 0; i < 4; ++i) {
            auto* emitter = Particles::create()->newEmitter(4096);
            emitter->setTexture(whiteTexture);
            emitter->setRandomSeed(4100 + i);
            emitter->setGpuSimulation(gpuEnabled);
            emitter->setPosition(150.f + 110.f * float(i), 180.f);
            emitter->setEmissionRate(0.f);
            emitter->setParticleLifetime(30.f, 30.f);
            emitter->setParticleSize(4.f, 4.f);
            emitter->setSpeed(2.f, 8.f);
            emitter->setSpread(6.2831853f);
            emitter->emit(4096);
            result.push_back(emitter);
        }
        return result;
    };
    auto measureMainThread = [&](int warmupFrames, int measuredFrames) {
        std::vector<double> samples;
        for (int frame = 0; frame < warmupFrames + measuredFrames; ++frame) {
            ParticleSimSystem::update(1.f / 60.f);
            gfx->clearScreen();
            ParticleRenderSystem::render(gfx);
            if (frame >= warmupFrames) {
                const auto& stats = particleFrameStats();
                samples.push_back(stats.simulationMs + stats.renderMs);
            }
            gfx->present();
        }
        return median(std::move(samples));
    };

    auto         cpuEmitters = makeEmitters(false);
    const double cpuMs       = measureMainThread(3, 12);
    REQUIRE_EQ(particleFrameStats().particlesAfter, 16384);
    for (auto* emitter : cpuEmitters) emitter->release();

    auto         gpuEmitters = makeEmitters(true);
    const double gpuCpuMs    = measureMainThread(5, 12);
    REQUIRE_EQ(particleFrameStats().gpuResidentEmitters, 4);
    REQUIRE_EQ(particleFrameStats().gpuResidentParticles, 16384);
    std::fprintf(stdout, "[particles.perf] cpu-main=%.3fms gpu-main=%.3fms particles=16384\n", cpuMs, gpuCpuMs);
    REQUIRE_LT(gpuCpuMs, cpuMs);

    for (auto* emitter : gpuEmitters) {
        emitter->setGpuSimulation(false);
        emitter->release();
    }
    window->close();
}
