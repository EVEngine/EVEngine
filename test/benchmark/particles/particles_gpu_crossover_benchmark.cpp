// eve-test-policy:
// {"schema":"evengine.test-policy","version":1,"role":"tests","default":"performance","module":"particles"}
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>
#include "graphics/Graphics.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleRuntime.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::particles;
namespace {
double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}
}  // namespace
TEST_CASE("particles.gpu.mainThreadCrossoverAtSixteenThousandParticles") {
    auto *window = eve::window::Window::create();
    auto *gfx    = eve::graphics::Graphics::create();
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
    auto              *whiteTexture  = gfx->newTexture(1, 1, whitePixel);
    REQUIRE(whiteTexture != nullptr);

    auto makeEmitters = [&](bool gpuEnabled) {
        std::vector<ParticleEmitter *> result;
        for (int i = 0; i < 4; ++i) {
            auto *emitter = Particles::create()->newEmitter(4096);
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
                const auto &stats = particleFrameStats();
                samples.push_back(stats.simulationMs + stats.renderMs);
            }
            gfx->present();
        }
        return median(std::move(samples));
    };

    auto         cpuEmitters = makeEmitters(false);
    const double cpuMs       = measureMainThread(3, 12);
    REQUIRE_EQ(particleFrameStats().particlesAfter, 16384);
    for (auto *emitter : cpuEmitters) emitter->release();

    auto         gpuEmitters = makeEmitters(true);
    const double gpuCpuMs    = measureMainThread(5, 12);
    REQUIRE_EQ(particleFrameStats().gpuResidentEmitters, 4);
    REQUIRE_EQ(particleFrameStats().gpuResidentParticles, 16384);
    std::fprintf(stdout, "[particles.perf] cpu-main=%.3fms gpu-main=%.3fms particles=16384\n", cpuMs, gpuCpuMs);
    REQUIRE_LT(gpuCpuMs, cpuMs);

    for (auto *emitter : gpuEmitters) {
        emitter->setGpuSimulation(false);
        emitter->release();
    }
    window->close();
}
