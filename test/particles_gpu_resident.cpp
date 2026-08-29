#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/Graphics.h"
#include "graphics/Light.h"
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

eve::graphics::Mesh* makeLeftHalfDepthPlane(eve::graphics::Graphics* gfx) {
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

eve::graphics::Mesh* makeFullscreenPlane(eve::graphics::Graphics* gfx) {
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

float colorDistance(const Color& a, const Color& b) {
    const float dr = a.r - b.r;
    const float dg = a.g - b.g;
    const float db = a.b - b.b;
    return std::sqrt(dr * dr + dg * dg + db * db);
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
        if (i % 3 == 1) {
            emitter->setRenderMode("axis");
            emitter->setRenderAxis(i % 2 == 0 ? 35.f : -35.f);
        } else if (i % 3 == 2) {
            emitter->setRenderMode("velocity", 0.075f);
        }
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

TEST_CASE("particles.gpu.multipleResidentEmittersKeepIndependentDrawState") {
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
    gfx->setScreenReadbackEnabled(true);

    const std::uint8_t whitePixel[4] = {255, 255, 255, 255};
    auto*              texture       = gfx->newTexture(1, 1, whitePixel);
    REQUIRE(texture != nullptr);

    const std::array<float, 3> positions{160.f, 320.f, 480.f};
    const std::array<std::array<float, 3>, 3> colors{{
        {1.f, 0.f, 0.f},
        {0.f, 1.f, 0.f},
        {0.f, 0.f, 1.f},
    }};
    std::array<ParticleEmitter*, 3> emitters{};
    for (std::size_t i = 0; i < emitters.size(); ++i) {
        auto* emitter = Particles::create()->newEmitter(8);
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
        const auto stats = gfx->getGpuParticleStats(emitters[i]->gpuSim()->residentHandle);
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
    for (auto* emitter : emitters) {
        emitter->setGpuSimulation(false);
        emitter->release();
    }
    window->close();
}

TEST_CASE("particles.renderer.normalMappedLitMaterialUsesSceneLights") {
    auto* window = eve::window::Window::create();
    auto* gfx    = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 480;
    settings.height   = 280;
    settings.centered = true;
    REQUIRE(window->setWindowSettings(settings));
    gfx->setScreenReadbackEnabled(true);

    const std::uint8_t albedoPixel[4] = {80, 80, 80, 255};
    const std::uint8_t normalPixel[4] = {128, 128, 255, 255};
    auto*              albedo         = gfx->newTexture(1, 1, albedoPixel);
    auto*              normal         = gfx->newTexture(1, 1, normalPixel);
    REQUIRE(albedo != nullptr);
    REQUIRE(normal != nullptr);

    auto makeEmitter = [&](float x, bool lit) {
        auto* emitter = Particles::create()->newEmitter(8);
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
    auto* lit   = makeEmitter(150.f, true);
    auto* unlit = makeEmitter(330.f, false);

    auto* light = eve::graphics::Light2D::createLight("point");
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
    auto* window = eve::window::Window::create();
    auto* gfx    = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 640;
    settings.height   = 360;
    settings.centered = true;
    REQUIRE(window->setWindowSettings(settings));
    gfx->setScreenReadbackEnabled(true);

    constexpr int             width   = 640;
    constexpr int             height  = 360;
    constexpr int             effectX = 304;
    std::vector<std::uint8_t> stripePixels(std::size_t(width) * 4u);
    for (int x = 0; x < width; ++x) {
        const bool cyan  = (x / 32) % 2 == 0;
        auto*      pixel = &stripePixels[std::size_t(x) * 4u];
        pixel[0]         = cyan ? 12 : 218;
        pixel[1]         = cyan ? 188 : 18;
        pixel[2]         = cyan ? 218 : 184;
        pixel[3]         = 255;
    }
    auto* backgroundTexture = gfx->newTexture(width, 1, stripePixels.data());
    auto* scenePlane        = makeFullscreenPlane(gfx);
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
            auto*       pixel   = &fieldPixels[std::size_t(y * fieldSize + x) * 4u];
            pixel[0]            = 255;
            pixel[1]            = 128;
            pixel[2]            = 128;
            pixel[3]            = std::uint8_t(feather * 255.f);
        }
    }
    auto* displacement = gfx->newTexture(fieldSize, fieldSize, fieldPixels.data());
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

    auto* emitter = Particles::create()->newEmitter(4);
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

TEST_CASE("particles.gpu.softDepthFadeUsesSceneLinearDepth") {
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
    gfx->setScreenReadbackEnabled(true);

    const std::uint8_t whitePixel[4] = {255, 255, 255, 255};
    const std::uint8_t darkPixel[4]  = {5, 8, 14, 255};
    auto*              particleTex   = gfx->newTexture(1, 1, whitePixel);
    auto*              planeTex      = gfx->newTexture(1, 1, darkPixel);
    auto*              depthPlane    = makeLeftHalfDepthPlane(gfx);
    REQUIRE(particleTex != nullptr);
    REQUIRE(planeTex != nullptr);
    REQUIRE(depthPlane != nullptr);

    auto makeEmitter = [&](float x) {
        auto* emitter = Particles::create()->newEmitter(16);
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
    auto* occluded = makeEmitter(160.f);

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
