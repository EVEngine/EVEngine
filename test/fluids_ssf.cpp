#include "fluids/FluidSurfaceRenderer.h"
#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidCodec.h"
#include "graphics/Graphics.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>

using namespace eve::fluids;

TEST_CASE("fluids.ssf.uniformVolumeColorOnlyGpuPath") {
    auto* graphics = eve::graphics::Graphics::create();
    REQUIRE(graphics != nullptr);
    graphics->initHeadless(64, 64);
    VolumeFluidSettings settings;
    settings.gravity = glm::vec3(0.f);
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position = glm::vec3(0.f, 1.f, 0.f);
    particle.color    = glm::vec4(.8f, .1f, .05f, .5f);
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    FluidSurfaceParams params;
    params.width            = 64;
    params.height           = 64;
    params.eye              = glm::vec3(0.f, 1.f, 3.f);
    params.target           = glm::vec3(0.f, 1.f, 0.f);
    params.smoothIterations = 0;
    FluidSurfaceRenderer renderer(params, true);
    const auto           depthBefore = renderer.depth();
    renderer.renderVolumeColorOnly(*solver);
    REQUIRE(renderer.usingGpu());
    const size_t middle = (32u * 64u + 32u) * 4u;
    REQUIRE(renderer.color()[middle + 3u] > 0);
    REQUIRE(renderer.color()[middle] > renderer.color()[middle + 1u]);
    REQUIRE(renderer.depth() == depthBefore);
    const auto         colorBefore = renderer.color();
    std::vector<float> sceneDepth(64u * 64u, 1e30f);
    REQUIRE(!renderer.occludeWithSceneDepth(sceneDepth).ok());
    REQUIRE(renderer.color() == colorBefore);
}

TEST_CASE("fluids.ssf.customMaterialUniformColorOnlyRemainsGpuFastPath") {
    auto* graphics = eve::graphics::Graphics::create();
    REQUIRE(graphics != nullptr);
    graphics->initHeadless(64, 64);
    VolumeFluidSettings settings;
    settings.gravity = glm::vec3(0.f);
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position = glm::vec3(0.f, 1.f, 0.f);
    particle.color    = glm::vec4(.65f, .2f, .08f, .75f);
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());

    FluidSurfaceParams params;
    params.width = params.height = 64;
    params.eye                   = glm::vec3(0.f, 1.f, 3.f);
    params.target                = glm::vec3(0.f, 1.f, 0.f);
    params.smoothIterations      = 0;
    FluidSurfaceRenderer cpu(params, false), gpu(params, true);
    for (auto* renderer : {&cpu, &gpu}) {
        REQUIRE(renderer->configureSurface(1.f, .2f, .08f, 0).ok());
        REQUIRE(renderer->configureMaterial(true, .35f, .4f, 1.25f, .3f, .7f).ok());
        REQUIRE(renderer->configureColors(glm::vec3(.1f, .2f, .3f), glm::vec3(.2f, .7f, .9f)).ok());
    }

    cpu.renderVolume(*solver);
    const auto depthBefore = gpu.depth();
    gpu.renderVolumeColorOnly(*solver);
    REQUIRE(gpu.usingGpu());
    REQUIRE(gpu.depth() == depthBefore);
    const size_t middle = (32u * 64u + 32u) * 4u;
    for (size_t channel = 0; channel < 4; ++channel)
        CHECK(std::abs(int(cpu.color()[middle + channel]) - int(gpu.color()[middle + channel])) <= 3);
}

TEST_CASE("fluids.ssf.multicolorVolumeColorOnlyRemainsDeviceLocal") {
    auto* graphics = eve::graphics::Graphics::create();
    REQUIRE(graphics != nullptr);
    graphics->initHeadless(64, 64);
    VolumeFluidSettings settings;
    settings.gravity = glm::vec3(0.f);
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[2];
    particles[0].position    = glm::vec3(-.025f, 1.f, 0.f);
    particles[0].color       = glm::vec4(.9f, .1f, .05f, .8f);
    particles[0].radii       = glm::vec3(.1f, .25f, .1f);
    particles[0].orientation = glm::vec4(0.f, 0.f, .70710677f, .70710677f);
    particles[1].position    = glm::vec3(.025f, 1.f, 0.f);
    particles[1].color       = glm::vec4(.05f, .2f, .9f, .6f);
    particles[1].radii       = glm::vec3(.1f, .25f, .1f);
    particles[1].orientation = particles[0].orientation;
    REQUIRE(solver->emit(particles).ok());

    FluidSurfaceParams params;
    params.width = params.height = 64;
    params.eye                   = glm::vec3(0.f, 1.f, 3.f);
    params.target                = glm::vec3(0.f, 1.f, 0.f);
    params.smoothIterations      = 0;
    FluidSurfaceRenderer cpu(params, false), gpu(params, true);
    REQUIRE(cpu.configureAnisotropy(true).ok());
    REQUIRE(gpu.configureAnisotropy(true).ok());
    cpu.renderVolume(*solver);
    const auto depthBefore = gpu.depth();
    gpu.renderVolumeColorOnly(*solver);
    REQUIRE(gpu.usingGpu());
    REQUIRE(gpu.depth() == depthBefore);
    const size_t middle = (32u * 64u + 32u) * 4u;
    REQUIRE(gpu.color()[middle + 3u] > 0);
    for (size_t channel = 0; channel < 4; ++channel)
        CHECK(std::abs(int(cpu.color()[middle + channel]) - int(gpu.color()[middle + channel])) <= 4);
}

TEST_CASE("fluids.ssf.volumeRenderUsesFixedStepInterpolationWithoutReplacingOnFailure") {
    VolumeFluidSettings settings;
    settings.gravity = glm::vec3(0.f);
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position = {-.5f, 1.f, 0.f};
    particle.velocity = {60.f, 0.f, 0.f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    FluidSurfaceParams params;
    params.width = params.height = 65;
    params.eye                   = {0.f, 1.f, 3.f};
    params.target                = {0.f, 1.f, 0.f};
    params.orthographic          = true;
    params.orthographicSize      = 1.f;
    params.smoothIterations      = 0;
    FluidSurfaceRenderer renderer(params, false);
    REQUIRE(renderer.renderVolumeInterpolated(*solver, 0.f).ok());
    auto centroid = [](const std::vector<uint8_t>& rgba) {
        double sum = 0, weight = 0;
        for (size_t i = 0; i < rgba.size() / 4; ++i) {
            const double a = rgba[i * 4 + 3];
            sum += double(i % 65) * a;
            weight += a;
        }
        return sum / weight;
    };
    const double previousX = centroid(renderer.color());
    REQUIRE(renderer.renderVolumeInterpolated(*solver, 1.f).ok());
    const double currentX = centroid(renderer.color());
    REQUIRE(currentX > previousX + 10.0);
    const auto preserved = renderer.color();
    REQUIRE(!renderer.renderVolumeInterpolated(*solver, -.01f).ok());
    REQUIRE(renderer.color() == preserved);
}

TEST_CASE("fluids.ssf.gasVolumeSelectsGasAndAccumulatesOpacity") {
    VolumeFluidSettings settings;
    settings.gravity = glm::vec3(0.f);
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[2];
    particles[0].position       = {0.f, 1.f, 0.f};
    particles[0].color          = {.9f, .15f, .05f, .8f};
    particles[0].material.phase = VolumeFluidPhase::Gas;
    particles[1].position       = {.8f, 1.f, 0.f};
    particles[1].color          = {.05f, .1f, .95f, 1.f};
    particles[1].material.phase = VolumeFluidPhase::Liquid;
    REQUIRE(solver->emit(particles).ok());

    FluidSurfaceParams params;
    params.width  = 65;
    params.height = 65;
    params.eye    = {0.f, 1.f, 3.f};
    params.target = {0.f, 1.f, 0.f};
    FluidSurfaceRenderer renderer(params, false);
    auto                 rendered = renderer.renderGasVolume(*solver, 5.f);
    REQUIRE(rendered.ok());
    const size_t center = (32u * 65u + 32u) * 4u;
    REQUIRE(renderer.color()[center + 3u] > 0);
    CHECK(renderer.color()[center] > renderer.color()[center + 2u]);
    CHECK(glm::length(renderer.normals()[32u * 65u + 32u]) == 0.f);
    CHECK(!renderer.renderGasVolume(*solver, 0.f).ok());
}

TEST_CASE("fluids.ssf.generateSurfaceToggleSelectsExistingBoundedRenderPaths") {
    VolumeFluidSettings settings;
    settings.gravity = glm::vec3(0.f);
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position = {0.f, 1.f, 0.f};
    particle.color    = {.8f, .2f, .1f, 1.f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    FluidSurfaceParams params;
    params.width = params.height = 65;
    params.eye                   = {0.f, 1.f, 3.f};
    params.target                = {0.f, 1.f, 0.f};
    params.smoothIterations      = 0;
    FluidSurfaceRenderer renderer(params, false);

    REQUIRE(renderer.configureSurfaceEnabled(false).ok());
    REQUIRE(renderer.renderConfiguredVolume(*solver).ok());
    const size_t center = 32u * 65u + 32u;
    REQUIRE(renderer.color()[center * 4u + 3u] > 0);
    REQUIRE(glm::length(renderer.normals()[center]) == 0.f);
    REQUIRE(renderer.configureSurfaceEnabled(true).ok());
    REQUIRE(renderer.renderConfiguredVolume(*solver).ok());
    REQUIRE(renderer.color()[center * 4u + 3u] > 0);
    REQUIRE(glm::length(renderer.normals()[center]) > .5f);
}

TEST_CASE("fluids.ssf.completeFluid3DRendererSettingsApplyAtomically") {
    FluidSurfaceParams params;
    params.width = params.height = 32;
    FluidSurfaceRenderer  renderer(params, false);
    FluidRendererSettings settings;
    REQUIRE(settings.blendSource == 5);
    REQUIRE(settings.blendDestination == 10);
    REQUIRE(settings.thicknessCutoff == 1.2f);
    REQUIRE(settings.thicknessDownsample == 2);
    REQUIRE(settings.blurRadius == .02f);
    settings.generateSurface      = false;
    settings.generateReflection   = false;
    settings.generateRefraction   = false;
    settings.generateFoam         = false;
    settings.particleZWrite       = true;
    settings.surfaceDownsample    = 3;
    settings.refractionDownsample = 4;
    REQUIRE(renderer.configureRendererSettings(settings).ok());
    const auto applied = renderer.rendererSettings();
    REQUIRE(!applied.generateSurface);
    REQUIRE(!applied.generateReflection);
    REQUIRE(!applied.generateRefraction);
    REQUIRE(!applied.generateFoam);
    REQUIRE(applied.particleZWrite);
    REQUIRE(applied.surfaceDownsample == 3);
    REQUIRE(applied.refractionDownsample == 4);

    auto encoded = encodeFluidRendererSettings(settings);
    REQUIRE(decodeFluidRendererSettings(encoded).ok());
    encoded.set("futureField", int64_t(1));
    REQUIRE(!decodeFluidRendererSettings(encoded).ok());
    settings.foamDownsample = 5;
    REQUIRE(!renderer.configureRendererSettings(settings).ok());
    REQUIRE(renderer.rendererSettings().foamDownsample == applied.foamDownsample);
    REQUIRE(renderer.rendererSettings().surfaceDownsample == applied.surfaceDownsample);
}

TEST_CASE("fluids.ssf.gasVolumeParticleBudgetPreservesFrame") {
    VolumeFluidSettings settings;
    settings.capacity = 65537;
    settings.gravity  = glm::vec3(0.f);
    auto created      = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                             solver = std::move(created).takeValue();
    std::vector<VolumeFluidParticle> particles(settings.capacity);
    for (auto& particle : particles) {
        particle.position       = {0.f, 1.f, 0.f};
        particle.material.phase = VolumeFluidPhase::Gas;
    }
    REQUIRE(solver->emit(particles).ok());
    FluidSurfaceParams params;
    params.width = params.height = 16;
    FluidSurfaceRenderer renderer(params, false);
    renderer.render(std::vector<glm::vec3>{{0.f, 0.f, 0.f}}, .1f);
    const auto previous = renderer.color();
    auto       rendered = renderer.renderGasVolume(*solver, 5.f);
    REQUIRE(!rendered.ok());
    CHECK(renderer.color() == previous);
}

TEST_CASE("fluids.ssf.sphericalDepthAndNormalParity") {
    auto* graphics = eve::graphics::Graphics::create();
    REQUIRE(graphics != nullptr);
    graphics->initHeadless(64, 64);
    for (int passes : {0, 1, 2}) {
        FluidSurfaceParams params;
        params.width            = 64;
        params.height           = 64;
        params.eye              = {0, 0, 3};
        params.target           = {0, 0, 0};
        params.smoothIterations = passes;
        FluidSurfaceRenderer         cpu(params, false), gpu(params, true);
        const std::vector<glm::vec3> particles{{0, 0, 0}};
        cpu.render(particles, .6f);
        gpu.render(particles, .6f);
        REQUIRE(gpu.usingGpu());
        const size_t middle = 32 * 64 + 32, left = 32 * 64 + 26, right = 32 * 64 + 38;
        const size_t top = 26 * 64 + 32, bottom = 38 * 64 + 32;
        REQUIRE(cpu.depth()[middle] < 2.42f);
        REQUIRE(cpu.depth()[right] > cpu.depth()[middle] + .06f);
        REQUIRE(gpu.normals()[left].x < -.2f);
        REQUIRE(gpu.normals()[right].x > .2f);
        REQUIRE(gpu.normals()[top].y > .2f);
        REQUIRE(gpu.normals()[bottom].y < -.2f);
        REQUIRE(gpu.normals()[middle].z > .95f);
        for (size_t i = 0; i < cpu.depth().size(); ++i) {
            if (cpu.depth()[i] >= 1e29f) {
                REQUIRE(gpu.depth()[i] >= 1e29f);
                continue;
            }
            REQUIRE(std::abs(cpu.depth()[i] - gpu.depth()[i]) < .00002f);
            REQUIRE(glm::length(cpu.normals()[i] - gpu.normals()[i]) < .002f);
            // GPU thickness uses 1/256-unit fixed-point accumulation.
            REQUIRE(std::abs(cpu.thickness()[i] - gpu.thickness()[i]) < 1.f / 256.f + .000001f);
        }
        gpu.render({}, .6f);
        REQUIRE(gpu.depth()[middle] >= 1e29f);
        REQUIRE(glm::length(gpu.normals()[middle]) == 0.f);
        REQUIRE(gpu.thickness()[middle] == 0.f);
        REQUIRE(gpu.color()[middle * 4 + 3] == 0);
    }
}

TEST_CASE("fluids.ssf.worldSpaceBlurRadiusUsesExistingBoundedPass") {
    auto* graphics = eve::graphics::Graphics::create();
    REQUIRE(graphics != nullptr);
    graphics->initHeadless(64, 64);
    FluidSurfaceParams params;
    params.width = params.height = 64;
    params.eye                   = {0, 0, 3};
    params.target                = {0, 0, 0};
    params.smoothIterations      = 1;
    const std::vector<glm::vec3> particles{{-.18f, 0, 0}, {.18f, 0, -.12f}};
    FluidSurfaceRenderer         unblurred(params, false);
    REQUIRE(unblurred.configureSurfaceBlurRadius(0.f).ok());
    REQUIRE(!unblurred.configureSurfaceBlurRadius(.1001f).ok());
    unblurred.render(particles, .35f);
    auto noPassParams             = params;
    noPassParams.smoothIterations = 0;
    FluidSurfaceRenderer noPass(noPassParams, false);
    noPass.render(particles, .35f);
    REQUIRE(unblurred.depth() == noPass.depth());

    params.smoothIterations = 1;
    FluidSurfaceRenderer cpu(params, false), gpu(params, true);
    REQUIRE(cpu.configureSurfaceBlurRadius(.02f).ok());
    REQUIRE(gpu.configureSurfaceBlurRadius(.02f).ok());
    cpu.render(particles, .35f);
    gpu.render(particles, .35f);
    REQUIRE(gpu.usingGpu());
    for (size_t i = 0; i < cpu.depth().size(); ++i) {
        if (cpu.depth()[i] >= 1e29f || gpu.depth()[i] >= 1e29f) continue;
        REQUIRE(std::abs(cpu.depth()[i] - gpu.depth()[i]) < 1e-4f);
    }
}

TEST_CASE("fluids.ssf.configuredSurfaceBlendSupportsUnityFactorsAtomically") {
    FluidSurfaceParams params;
    params.width = params.height = 17;
    params.eye                   = {0.f, 0.f, 3.f};
    params.target                = {0.f, 0.f, 0.f};
    params.smoothIterations      = 0;
    FluidSurfaceRenderer renderer(params, false);
    renderer.render(std::vector<glm::vec3>{{0.f, 0.f, 0.f}}, .8f);
    const size_t         center = (8u * 17u + 8u) * 4u;
    const auto           source = renderer.color();
    std::vector<uint8_t> scene(source.size(), 0u);
    for (size_t i = 0; i < scene.size(); i += 4u) {
        scene[i + 0u] = 128u;
        scene[i + 1u] = 64u;
        scene[i + 2u] = 32u;
        scene[i + 3u] = 255u;
    }

    REQUIRE(renderer.configureSurfaceBlend(2, 0).ok());  // DstColor, Zero.
    REQUIRE(renderer.compositeConfiguredSurfaceBlend(scene).ok());
    CHECK(renderer.color()[center] == uint8_t((unsigned(source[center]) * 128u + 127u) / 255u));
    CHECK(renderer.color()[center + 1u] == uint8_t((unsigned(source[center + 1u]) * 64u + 127u) / 255u));

    const auto preserved = renderer.color();
    REQUIRE(!renderer.configureSurfaceBlend(-1, 10).ok());
    REQUIRE(!renderer.configureSurfaceBlend(5, 11).ok());
    REQUIRE(!renderer.compositeConfiguredSurfaceBlend(std::span<const uint8_t>(scene.data(), 4)).ok());
    CHECK(renderer.color() == preserved);
}

TEST_CASE("fluids.ssf.particleBlendAndDepthWriteMatchFluid3DColorPass") {
    VolumeFluidSettings settings;
    settings.gravity = glm::vec3(0.f);
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[2];
    particles[0].position = {-.05f, .5f, 0.f};
    particles[1].position = {.05f, .5f, 0.f};
    particles[0].color    = {.8f, .5f, .25f, .3f};
    particles[1].color    = {.5f, .25f, .8f, .7f};
    REQUIRE(solver->emit(particles).ok());

    FluidSurfaceParams params;
    params.width = params.height = 33;
    params.eye                   = {0.f, .5f, 3.f};
    params.target                = {0.f, .5f, 0.f};
    params.smoothIterations      = 0;
    FluidSurfaceRenderer renderer(params, false);
    REQUIRE(renderer.configureMaterial(false, 0.f, 0.f, 0.f, 0.f, 1.f).ok());
    REQUIRE(renderer.configureParticleBlend(2, 0, false).ok());
    renderer.renderVolume(*solver);
    const size_t center = (16u * 33u + 16u) * 4u;
    CHECK(std::abs(int(renderer.color()[center + 0u]) - int(255.f * .8f * .5f)) <= 1);
    CHECK(std::abs(int(renderer.color()[center + 1u]) - int(255.f * .5f * .25f)) <= 1);
    CHECK(std::abs(int(renderer.color()[center + 2u]) - int(255.f * .25f * .8f)) <= 1);

    REQUIRE(renderer.configureParticleBlend(2, 0, true).ok());
    renderer.renderVolume(*solver);
    CHECK(std::abs(int(renderer.color()[center + 0u]) - int(255.f * .8f)) <= 1);
    CHECK(std::abs(int(renderer.color()[center + 1u]) - int(255.f * .5f)) <= 1);
    CHECK(std::abs(int(renderer.color()[center + 2u]) - int(255.f * .25f)) <= 1);
    const auto preserved = renderer.color();
    REQUIRE(!renderer.configureParticleBlend(11, 0, false).ok());
    renderer.renderVolume(*solver);
    CHECK(renderer.color() == preserved);
}

TEST_CASE("fluids.ssf.orthographicDepthIndependentCoverageAndGpuParity") {
    auto* graphics = eve::graphics::Graphics::create();
    REQUIRE(graphics != nullptr);
    graphics->initHeadless(64, 64);
    FluidSurfaceParams params;
    params.width            = 64;
    params.height           = 64;
    params.eye              = {0, 0, 3};
    params.target           = {0, 0, 0};
    params.smoothIterations = 0;
    params.orthographic     = true;
    params.orthographicSize = 1.f;
    FluidSurfaceRenderer nearCpu(params, false), farCpu(params, false), gpu(params, true);
    nearCpu.render(std::vector<glm::vec3>{{0, 0, 0}}, .25f);
    farCpu.render(std::vector<glm::vec3>{{0, 0, -1}}, .25f);
    gpu.render(std::vector<glm::vec3>{{0, 0, 0}}, .25f);
    REQUIRE(gpu.usingGpu());

    size_t nearCoverage = 0, farCoverage = 0;
    for (size_t i = 0; i < nearCpu.depth().size(); ++i) {
        nearCoverage += nearCpu.depth()[i] < 1e29f;
        farCoverage += farCpu.depth()[i] < 1e29f;
        if (nearCpu.depth()[i] >= 1e29f) {
            REQUIRE(gpu.depth()[i] >= 1e29f);
            continue;
        }
        REQUIRE(std::abs(nearCpu.depth()[i] - gpu.depth()[i]) < .00002f);
        REQUIRE(glm::length(nearCpu.normals()[i] - gpu.normals()[i]) < .002f);
    }
    CHECK(nearCoverage == farCoverage);
    CHECK(nearCoverage > 150);

    const auto previous = nearCpu.depth();
    REQUIRE(!nearCpu.configureProjection(true, 0.f).ok());
    nearCpu.render(std::vector<glm::vec3>{{0, 0, 0}}, .25f);
    CHECK(nearCpu.depth() == previous);
}

TEST_CASE("fluids.ssf.orthographicVolumeTintAndGasCoverageAreDepthIndependent") {
    auto makeSolver = [&](float depthOffset, VolumeFluidPhase phase) {
        VolumeFluidSettings settings;
        settings.gravity = glm::vec3(0.f);
        auto created     = VolumeFluid::create(settings);
        REQUIRE(created.ok());
        auto                solver = std::move(created).takeValue();
        VolumeFluidParticle particles[2];
        particles[0].position       = {-.2f, .5f, depthOffset};
        particles[0].color          = {1.f, .1f, .05f, .8f};
        particles[0].material.phase = phase;
        particles[1].position       = {.2f, .5f, depthOffset};
        particles[1].color          = {.05f, .2f, 1.f, .8f};
        particles[1].material.phase = phase;
        REQUIRE(solver->emit(particles).ok());
        return solver;
    };
    FluidSurfaceParams params;
    params.width = params.height = 65;
    params.eye                   = {0.f, .5f, 3.f};
    params.target                = {0.f, .5f, 0.f};
    params.orthographic          = true;
    params.orthographicSize      = 1.f;
    params.smoothIterations      = 0;

    auto                 nearLiquid = makeSolver(0.f, VolumeFluidPhase::Liquid);
    auto                 farLiquid  = makeSolver(-.8f, VolumeFluidPhase::Liquid);
    FluidSurfaceRenderer nearLiquidRenderer(params, false), farLiquidRenderer(params, false);
    nearLiquidRenderer.renderVolume(*nearLiquid);
    farLiquidRenderer.renderVolume(*farLiquid);
    CHECK(nearLiquidRenderer.color() == farLiquidRenderer.color());

    auto                 nearGas = makeSolver(0.f, VolumeFluidPhase::Gas);
    auto                 farGas  = makeSolver(-.8f, VolumeFluidPhase::Gas);
    FluidSurfaceRenderer nearGasRenderer(params, false), farGasRenderer(params, false);
    REQUIRE(nearGasRenderer.renderGasVolume(*nearGas, 5.f).ok());
    REQUIRE(farGasRenderer.renderGasVolume(*farGas, 5.f).ok());
    CHECK(nearGasRenderer.color() == farGasRenderer.color());
}

TEST_CASE("fluids.ssf.occlusionDoesNotTiltForegroundNormals") {
    auto* graphics = eve::graphics::Graphics::create();
    REQUIRE(graphics != nullptr);
    graphics->initHeadless(64, 64);
    FluidSurfaceParams params;
    params.width            = 64;
    params.height           = 64;
    params.eye              = {0, 0, 3};
    params.target           = {0, 0, 0};
    params.smoothIterations = 0;
    FluidSurfaceRenderer foreground(params, false), combined(params, true), reference(params, false);
    foreground.render(std::vector<glm::vec3>{{0, 0, 0}}, .6f);
    const std::vector<glm::vec3> particles{{0, 0, 0}, {.9f, 0, -2.f}};
    combined.render(particles, .6f);
    reference.render(particles, .6f);
    REQUIRE(combined.usingGpu());
    unsigned occludedEdges = 0;
    for (size_t i = 0; i < foreground.depth().size(); ++i) {
        if (foreground.depth()[i] >= 1e29f) continue;
        REQUIRE(std::abs(combined.depth()[i] - foreground.depth()[i]) < .00002f);
        REQUIRE(glm::length(combined.normals()[i] - reference.normals()[i]) < .002f);
        const size_t x = i % 64;
        if (x < 63 && foreground.depth()[i + 1] >= 1e29f && combined.depth()[i + 1] < 1e29f) {
            ++occludedEdges;
            REQUIRE(glm::length(combined.normals()[i] - foreground.normals()[i]) < .002f);
        }
    }
    REQUIRE(occludedEdges >= 8);
}
