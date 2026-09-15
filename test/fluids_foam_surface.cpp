#include <cstring>
#include <limits>
#include "fluids/FluidSurfaceRenderer.h"
#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidDiffuse.h"
#include "graphics/Graphics.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::fluids;

TEST_CASE("fluids.ssf.uniformVolumeTintUsesReconstructedCoverage") {
    VolumeFluidSettings settings;
    settings.gravity = glm::vec3(0.f);
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                             solver = std::move(created).takeValue();
    std::vector<VolumeFluidParticle> particles(512);
    for (size_t i = 0; i < particles.size(); ++i) {
        particles[i].position = {0.f, 1.f, 0.f};
        particles[i].color    = {0.8f, 0.1f, 0.05f, 0.5f};
    }
    REQUIRE(solver->emit(particles).ok());

    FluidSurfaceParams params;
    params.width            = 65;
    params.height           = 65;
    params.eye              = {0, 0, 3};
    params.target           = {0, 1, 0};
    params.smoothIterations = 0;
    FluidSurfaceRenderer renderer(params, false);
    renderer.renderVolume(*solver);

    const size_t middle = (32 * 65 + 32) * 4;
    REQUIRE(renderer.color()[middle + 3] > 0);
    REQUIRE(renderer.color()[middle] > renderer.color()[middle + 1]);
    REQUIRE(renderer.color()[0] == 0);
    REQUIRE(renderer.color()[3] == 0);
}

TEST_CASE("fluids.ssf.surfaceDisabledVolumeIncludesEveryPhaseWhileGasPathFilters") {
    VolumeFluidSettings settings;
    settings.gravity = glm::vec3(0.f);
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle liquid;
    liquid.position       = {0.f, 1.f, 0.f};
    liquid.color          = {0.9f, 0.1f, 0.05f, 1.f};
    liquid.material.phase = VolumeFluidPhase::Liquid;
    REQUIRE(solver->emit(std::span(&liquid, 1)).ok());

    FluidSurfaceParams params;
    params.width  = 65;
    params.height = 65;
    params.eye    = {0.f, 0.f, 3.f};
    params.target = {0.f, 1.f, 0.f};
    FluidSurfaceRenderer renderer(params, false);
    REQUIRE(renderer.renderGasVolume(*solver, 5.f).ok());
    const size_t middle = (32u * 65u + 32u) * 4u;
    REQUIRE(renderer.color()[middle + 3u] == 0u);

    REQUIRE(renderer.renderVolumeWithoutSurface(*solver, 5.f).ok());
    REQUIRE(renderer.color()[middle + 3u] > 0u);
    REQUIRE(renderer.color()[middle] > renderer.color()[middle + 1u]);
    const auto allPhase = renderer.color();
    REQUIRE(!renderer.renderVolumeWithoutSurface(*solver, 0.f).ok());
    REQUIRE(renderer.color() == allPhase);
}

TEST_CASE("fluids.ssf.diffuseDepthOcclusionAndFade") {
    FluidSurfaceParams params;
    params.width            = 65;
    params.height           = 65;
    params.eye              = {0, 0, 3};
    params.target           = {0, 0, 0};
    params.smoothIterations = 0;
    FluidSurfaceRenderer renderer(params, false);
    renderer.render(std::vector<glm::vec3>{{0, 0, 0}}, .5f);
    const auto                 original  = renderer.color();
    const auto                 depth     = renderer.depth();
    const auto                 normals   = renderer.normals();
    const auto                 thickness = renderer.thickness();
    VolumeFluidDiffuse         pool;
    VolumeFluidDiffuseParticle p{{0, 0, -1}, {0, 0, 0}, 1.f};
    REQUIRE(pool.emit(std::span(&p, 1)).ok());
    REQUIRE(renderer.compositeDiffuse(pool, .1f, 1.f, .5f).ok());
    REQUIRE(renderer.color() == original);
    pool.clear();
    p.position.z = 1.f;
    REQUIRE(pool.emit(std::span(&p, 1)).ok());
    REQUIRE(renderer.compositeDiffuse(pool, .1f, 1.f, .5f).ok());
    const size_t middle = (32 * 65 + 32) * 4;
    REQUIRE(renderer.color()[middle] == 255);
    REQUIRE(renderer.color()[middle + 3] == 255);
    REQUIRE(renderer.depth() == depth);
    REQUIRE(renderer.normals() == normals);
    REQUIRE(renderer.thickness() == thickness);
    renderer.render({}, .5f);
    pool.clear();
    p.life = .25f;
    REQUIRE(pool.emit(std::span(&p, 1)).ok());
    REQUIRE(renderer.compositeDiffuse(pool, .1f, 1.f, .5f).ok());
    REQUIRE(renderer.color()[middle] == 255);
    REQUIRE(renderer.color()[middle + 3] == 128);
    renderer.render({}, .5f);
    REQUIRE(renderer.color()[middle + 3] == 0);
}
TEST_CASE("fluids.ssf.diffuseBudgetAtomicityAndClipping") {
    FluidSurfaceParams params;
    params.width  = 65;
    params.height = 65;
    params.eye    = {0, 0, 3};
    params.target = {0, 0, 0};
    FluidSurfaceRenderer renderer(params, false);
    renderer.render({}, .1f);
    const auto         original = renderer.color();
    VolumeFluidDiffuse pool;
    auto               state = pool.snapshot();
    state.particles.resize(1000);
    for (auto& p : state.particles) p.position = {0, 0, 2.8f};
    REQUIRE(pool.restore(state).ok());
    REQUIRE(!renderer.compositeDiffuse(pool, 1.f, 1.f, .5f).ok());
    REQUIRE(renderer.color() == original);
    pool.clear();
    VolumeFluidDiffuseParticle p{{1e30f, 0, 0}, {0, 0, 0}, 1.f};
    REQUIRE(pool.emit(std::span(&p, 1)).ok());
    REQUIRE(renderer.compositeDiffuse(pool, .1f, 1.f, .5f).ok());
    REQUIRE(renderer.color() == original);
    REQUIRE(!renderer.compositeDiffuse(pool, 0.f, 1.f, .5f).ok());
    REQUIRE(!renderer.compositeDiffuse(pool, .1f, std::numeric_limits<float>::quiet_NaN(), .5f).ok());
    REQUIRE(renderer.color() == original);
}

TEST_CASE("fluids.ssf.foamToggleAndDownsampleAreAtomicAndPreserveAuxiliaries") {
    FluidSurfaceParams params;
    params.width            = 65;
    params.height           = 65;
    params.eye              = {0, 0, 3};
    params.target           = {0, 0, 0};
    params.smoothIterations = 0;
    FluidSurfaceRenderer renderer(params, false);
    renderer.render(std::vector<glm::vec3>{{0, 0, 0}}, .5f);
    const auto                 depth     = renderer.depth();
    const auto                 normals   = renderer.normals();
    const auto                 thickness = renderer.thickness();
    VolumeFluidDiffuse         pool;
    VolumeFluidDiffuseParticle foam{{0, 0, 1.f}, {0, 0, 0}, 1.f};
    REQUIRE(pool.emit(std::span(&foam, 1)).ok());

    REQUIRE(renderer.configureFoam(false, 2).ok());
    const auto disabled = renderer.color();
    REQUIRE(renderer.compositeDiffuse(pool, .1f, 1.f, .5f).ok());
    REQUIRE(renderer.color() == disabled);
    REQUIRE(renderer.configureFoam(true, 2).ok());
    REQUIRE(!renderer.configureFoam(false, 0).ok());
    REQUIRE(renderer.compositeDiffuse(pool, .1f, 1.f, .5f).ok());
    const size_t center = (32u * 65u + 32u) * 4u;
    REQUIRE(renderer.color()[center] == 255u);
    REQUIRE(renderer.color()[center + 3u] > disabled[center + 3u]);
    REQUIRE(renderer.depth() == depth);
    REQUIRE(renderer.normals() == normals);
    REQUIRE(renderer.thickness() == thickness);
}

TEST_CASE("fluids.ssf.sceneDepthOccludesLiquidAndFoamAtomically") {
    FluidSurfaceParams params;
    params.width            = 65;
    params.height           = 65;
    params.eye              = {0, 0, 3};
    params.target           = {0, 0, 0};
    params.smoothIterations = 0;
    FluidSurfaceRenderer renderer(params, false);
    renderer.render(std::vector<glm::vec3>{{0, 0, 0}}, .5f);
    VolumeFluidDiffuse         pool;
    VolumeFluidDiffuseParticle foam{{0, 0, 1.f}, {0, 0, 0}, 1.f};
    REQUIRE(pool.emit(std::span(&foam, 1)).ok());
    REQUIRE(renderer.compositeDiffuse(pool, .1f, 1.f, .5f).ok());
    const auto   fluidDepth = renderer.depth();
    const auto   normals    = renderer.normals();
    const auto   thickness  = renderer.thickness();
    const size_t center     = 32u * 65u + 32u;
    REQUIRE(renderer.color()[center * 4u + 3u] > 0);

    std::vector<float> sceneDepth(fluidDepth.size(), 1e30f);
    sceneDepth[center] = fluidDepth[center] - .1f;
    REQUIRE(renderer.occludeWithSceneDepth(sceneDepth, .001f).ok());
    REQUIRE(renderer.color()[center * 4u + 3u] == 0);
    REQUIRE(renderer.depth() == fluidDepth);
    REQUIRE(renderer.normals() == normals);
    REQUIRE(renderer.thickness() == thickness);

    renderer.render(std::vector<glm::vec3>{{0, 0, 0}}, .5f);
    const auto original = renderer.color();
    sceneDepth[center]  = fluidDepth[center] + .1f;
    REQUIRE(renderer.occludeWithSceneDepth(sceneDepth, .001f).ok());
    REQUIRE(renderer.color() == original);
    sceneDepth[0] = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!renderer.occludeWithSceneDepth(sceneDepth, .001f).ok());
    REQUIRE(renderer.color() == original);
}

TEST_CASE("fluids.ssf.sceneRefractionCompositesWithoutChangingAuxiliaries") {
    FluidSurfaceParams params;
    params.width            = 65;
    params.height           = 65;
    params.eye              = {0, 0, 3};
    params.target           = {0, 0, 0};
    params.smoothIterations = 0;
    FluidSurfaceRenderer renderer(params, false);
    renderer.render(std::vector<glm::vec3>{{0, 0, 0}}, .5f);
    const auto           fluid     = renderer.color();
    const auto           depth     = renderer.depth();
    const auto           normals   = renderer.normals();
    const auto           thickness = renderer.thickness();
    std::vector<uint8_t> scene(65u * 65u * 4u, 255u);
    for (size_t y = 0; y < 65u; ++y)
        for (size_t x = 0; x < 65u; ++x) {
            const size_t at = (y * 65u + x) * 4u;
            scene[at]       = (x < 32u) ? 220u : 20u;
            scene[at + 1u]  = (x < 32u) ? 20u : 220u;
            scene[at + 2u]  = 40u;
        }
    REQUIRE(renderer.compositeSceneRefraction(scene, 8.f, 3.f).ok());
    REQUIRE(renderer.color()[3u] == 255u);
    REQUIRE(renderer.color()[0u] == scene[0u]);
    bool changedFluid = false;
    for (size_t i = 0; i < 65u * 65u; ++i) {
        REQUIRE(renderer.color()[i * 4u + 3u] == 255u);
        if (fluid[i * 4u + 3u] > 0u && renderer.color()[i * 4u] != scene[i * 4u]) changedFluid = true;
    }
    REQUIRE(changedFluid);
    REQUIRE(renderer.depth() == depth);
    REQUIRE(renderer.normals() == normals);
    REQUIRE(renderer.thickness() == thickness);

    const auto composited = renderer.color();
    REQUIRE(
        !renderer.compositeSceneRefraction(std::span<const uint8_t>(scene.data(), scene.size() - 1u), 8.f, 3.f).ok());
    REQUIRE(!renderer.compositeSceneRefraction(scene, std::numeric_limits<float>::quiet_NaN(), 3.f).ok());
    REQUIRE(renderer.color() == composited);
    FluidSurfaceRenderer fresh(params, false);
    REQUIRE(!fresh.compositeSceneRefraction(scene, 8.f, 3.f).ok());
}

TEST_CASE("fluids.ssf.configuredRefractionSupportsSignedBendAndDownsampleAtomically") {
    FluidSurfaceParams params;
    params.width            = 65;
    params.height           = 65;
    params.eye              = {0, 0, 3};
    params.target           = {0, 0, 0};
    params.smoothIterations = 0;
    FluidSurfaceRenderer         renderer(params, false);
    const std::vector<glm::vec3> particle{{0, 0, 0}};
    std::vector<uint8_t>         scene(65u * 65u * 4u, 255u);
    for (size_t y = 0; y < 65u; ++y)
        for (size_t x = 0; x < 65u; ++x) {
            const size_t at = (y * 65u + x) * 4u;
            scene[at]       = uint8_t(x * 3u);
            scene[at + 1u]  = uint8_t(255u - x * 3u);
            scene[at + 2u]  = 32u;
        }

    renderer.render(particle, .8f);
    size_t probe     = 0;
    float  strongest = 0.f;
    for (size_t i = 0; i < renderer.normals().size(); ++i) {
        if (renderer.color()[i * 4u + 3u] == 0u) continue;
        const float strength = std::abs(renderer.normals()[i].x) * renderer.thickness()[i];
        if (strength > strongest) {
            strongest = strength;
            probe     = i;
        }
    }
    REQUIRE(strongest > .05f);
    REQUIRE(renderer.configureRefraction(1.f, 0.f, .1f, 2).ok());
    REQUIRE(renderer.compositeConfiguredSceneRefraction(scene).ok());
    const auto positive = renderer.color();

    renderer.render(particle, .8f);
    REQUIRE(renderer.configureRefraction(1.f, 0.f, -.1f, 2).ok());
    REQUIRE(!renderer.configureRefraction(1.f, 0.f, .11f, 2).ok());
    REQUIRE(renderer.compositeConfiguredSceneRefraction(scene).ok());
    const auto negative = renderer.color();
    REQUIRE(positive[probe * 4u] != negative[probe * 4u]);
    REQUIRE(renderer.depth()[probe] < 1e29f);

    const auto before = renderer.color();
    REQUIRE(
        !renderer.compositeConfiguredSceneRefraction(std::span<const uint8_t>(scene.data(), scene.size() - 1u)).ok());
    REQUIRE(renderer.color() == before);
    REQUIRE(!renderer.configureRefraction(-.01f, 0.f, 0.f, 1).ok());
    REQUIRE(!renderer.configureRefraction(1.f, 0.f, 0.f, 5).ok());
}

TEST_CASE("fluids.ssf.refractionToggleSkipsSceneWorkAndRestoresConfiguredControls") {
    FluidSurfaceParams params;
    params.width            = 65;
    params.height           = 65;
    params.eye              = {0, 0, 3};
    params.target           = {0, 0, 0};
    params.smoothIterations = 0;
    FluidSurfaceRenderer renderer(params, false);
    renderer.render(std::vector<glm::vec3>{{0, 0, 0}}, .8f);
    REQUIRE(renderer.configureRefraction(1.f, 0.f, .08f, 2).ok());
    REQUIRE(renderer.configureRefractionEnabled(false).ok());
    const auto                 fluid = renderer.color();
    const std::vector<uint8_t> emptyScene;
    REQUIRE(renderer.compositeConfiguredSceneRefraction(emptyScene).ok());
    REQUIRE(renderer.color() == fluid);

    REQUIRE(renderer.configureRefractionEnabled(true).ok());
    std::vector<uint8_t> scene(65u * 65u * 4u, 255u);
    for (size_t i = 0; i < 65u * 65u; ++i) {
        scene[i * 4u]      = 20u;
        scene[i * 4u + 1u] = 180u;
        scene[i * 4u + 2u] = 40u;
    }
    REQUIRE(renderer.compositeConfiguredSceneRefraction(scene).ok());
    REQUIRE(renderer.color() != fluid);
}

TEST_CASE("fluids.ssf.surfaceAndMaterialControlsAreAtomicAndObservable") {
    FluidSurfaceParams params;
    params.width            = 65;
    params.height           = 65;
    params.eye              = {0, 0, 3};
    params.target           = {0, 0, 0};
    params.smoothIterations = 0;
    FluidSurfaceRenderer renderer(params, false);
    renderer.render(std::vector<glm::vec3>{{0, 0, 0}}, .5f);
    const size_t center   = (32u * 65u + 32u) * 4u;
    const auto   baseline = renderer.color();
    REQUIRE(renderer.configureColors({.8f, .05f, .02f}, {.1f, .9f, .2f}).ok());
    REQUIRE(renderer.configureMaterial(false, 0.f, 0.f, 1.f, 0.f, 2.f).ok());
    REQUIRE(renderer.configureSurface(1.f, 0.f, .03f, 1).ok());
    renderer.render(std::vector<glm::vec3>{{0, 0, 0}}, .5f);
    REQUIRE(renderer.color()[center] > renderer.color()[center + 1u]);
    REQUIRE(renderer.color()[center + 3u] > baseline[center + 3u]);
    const auto coverage = [](const std::vector<uint8_t>& color) {
        size_t count = 0;
        for (size_t i = 3; i < color.size(); i += 4) count += color[i] > 0;
        return count;
    };
    const size_t uncutCoverage = coverage(renderer.color());

    REQUIRE(renderer.configureSurface(1.f, 5.f, .03f, 0).ok());
    renderer.render(std::vector<glm::vec3>{{0, 0, 0}}, .5f);
    REQUIRE(renderer.color()[center + 3u] > 0u);
    REQUIRE(coverage(renderer.color()) < uncutCoverage);
    const auto discarded = renderer.color();
    REQUIRE(!renderer.configureSurface(-1.f, 0.f, .03f, 0).ok());
    REQUIRE(!renderer.configureMaterial(true, 2.f, 0.f, 1.f, .2f, .35f).ok());
    REQUIRE(!renderer.configureColors({0, 0, std::numeric_limits<float>::quiet_NaN()}, {0, 0, 0}).ok());
    renderer.render(std::vector<glm::vec3>{{0, 0, 0}}, .5f);
    REQUIRE(renderer.color() == discarded);
}

TEST_CASE("fluids.ssf.reflectionToggleRestoresConfiguredCoefficient") {
    FluidSurfaceParams params;
    params.width            = 65;
    params.height           = 65;
    params.eye              = {0, 0, 3};
    params.target           = {0, 0, 0};
    params.smoothIterations = 0;
    FluidSurfaceRenderer renderer(params, false);
    REQUIRE(renderer.configureColors({0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}).ok());
    REQUIRE(renderer.configureMaterial(false, 0.f, 0.f, 0.f, 1.f, 1.f).ok());
    renderer.render(std::vector<glm::vec3>{{0, 0, 0}}, .5f);
    const size_t center    = (32u * 65u + 32u) * 4u;
    const auto   reflected = renderer.color()[center];
    REQUIRE(reflected > 0u);

    REQUIRE(renderer.configureReflection(false).ok());
    renderer.render(std::vector<glm::vec3>{{0, 0, 0}}, .5f);
    REQUIRE(renderer.color()[center] < reflected);
    REQUIRE(renderer.configureReflection(true).ok());
    renderer.render(std::vector<glm::vec3>{{0, 0, 0}}, .5f);
    REQUIRE(renderer.color()[center] == reflected);
}

TEST_CASE("fluids.ssf.anisotropicProjectionIsOptionalAndGpuMatchesCpu") {
    auto* graphics = eve::graphics::Graphics::create();
    REQUIRE(graphics != nullptr);
    graphics->initHeadless(64, 64);
    VolumeFluidSettings settings;
    settings.gravity = glm::vec3(0.f);
    settings.spacing = .2f;
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position    = {0, 1, 0};
    particle.radii       = {.1f, .4f, .1f};
    particle.orientation = {0, 0, .70710677f, .70710677f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    FluidSurfaceParams params;
    params.width            = 97;
    params.height           = 97;
    params.eye              = {0, 1, 3};
    params.target           = {0, 1, 0};
    params.smoothIterations = 0;
    auto bounds             = [](const FluidSurfaceRenderer& renderer) {
        int x0 = 97, y0 = 97, x1 = -1, y1 = -1;
        for (int y = 0; y < 97; ++y)
            for (int x = 0; x < 97; ++x)
                if (renderer.color()[(size_t(y) * 97u + size_t(x)) * 4u + 3u]) {
                    x0 = std::min(x0, x);
                    x1 = std::max(x1, x);
                    y0 = std::min(y0, y);
                    y1 = std::max(y1, y);
                }
        return glm::ivec4(x0, y0, x1, y1);
    };
    FluidSurfaceRenderer spherical(params, false);
    spherical.renderVolume(*solver);
    const auto           sphericalBounds = bounds(spherical);
    FluidSurfaceRenderer cpu(params, false);
    REQUIRE(cpu.configureAnisotropy(true).ok());
    cpu.renderVolume(*solver);
    const auto cpuBounds = bounds(cpu);
    REQUIRE(cpuBounds.z - cpuBounds.x > sphericalBounds.z - sphericalBounds.x + 4);
    REQUIRE(cpuBounds.w - cpuBounds.y <= sphericalBounds.w - sphericalBounds.y - 4);

    FluidSurfaceRenderer gpu(params, true);
    REQUIRE(gpu.configureAnisotropy(true).ok());
    REQUIRE(gpu.prepare().ok());
    gpu.renderVolume(*solver);
    REQUIRE(gpu.usingGpu());
    const auto gpuBounds = bounds(gpu);
    REQUIRE(glm::all(glm::lessThanEqual(glm::abs(gpuBounds - cpuBounds), glm::ivec4(1))));
    const size_t center = 48u * 97u + 48u;
    REQUIRE(std::abs(gpu.depth()[center] - cpu.depth()[center]) < 1e-4f);
}

TEST_CASE("fluids.ssf.isotropicPerParticleRadiusChangesRenderedCoverage") {
    auto makeSolver = [&](float radius) {
        VolumeFluidSettings settings;
        settings.gravity = glm::vec3(0.f);
        settings.spacing = .4f;
        auto created     = VolumeFluid::create(settings);
        REQUIRE(created.ok());
        auto                solver = std::move(created).takeValue();
        VolumeFluidParticle particle;
        particle.position = {0, 1, 0};
        particle.radii    = glm::vec3(radius);
        REQUIRE(solver->emit(std::span(&particle, 1)).ok());
        return solver;
    };
    auto               small = makeSolver(.05f), large = makeSolver(.2f);
    FluidSurfaceParams params;
    params.width = params.height = 97;
    params.eye                   = {0, 1, 3};
    params.target                = {0, 1, 0};
    params.smoothIterations      = 0;
    FluidSurfaceRenderer smallRenderer(params, false), largeRenderer(params, false);
    REQUIRE(smallRenderer.configureAnisotropy(true).ok());
    REQUIRE(largeRenderer.configureAnisotropy(true).ok());
    smallRenderer.renderVolume(*small);
    largeRenderer.renderVolume(*large);
    const auto coverage = [](const FluidSurfaceRenderer& renderer) {
        size_t count = 0;
        for (size_t i = 3; i < renderer.color().size(); i += 4u)
            if (renderer.color()[i] > 0u) ++count;
        return count;
    };
    REQUIRE(coverage(largeRenderer) > coverage(smallRenderer) * 4);
}

TEST_CASE("fluids.ssf.surfaceDownsampleRunsReducedCpuAndGpuTargets") {
    auto* graphics = eve::graphics::Graphics::create();
    REQUIRE(graphics != nullptr);
    graphics->initHeadless(64, 64);
    FluidSurfaceParams params;
    params.width            = 96;
    params.height           = 96;
    params.eye              = {0, 0, 3};
    params.target           = {0, 0, 0};
    params.smoothIterations = 0;
    const std::vector<glm::vec3> particles{{0, 0, 0}};
    FluidSurfaceRenderer         cpu(params, false);
    REQUIRE(cpu.configureSurfaceDownsample(2).ok());
    REQUIRE(!cpu.configureSurfaceDownsample(5).ok());
    cpu.render(particles, .5f);
    REQUIRE(cpu.color().size() == 96u * 96u * 4u);
    REQUIRE(cpu.depth().size() == 96u * 96u);
    for (int y = 42; y < 54; y += 2)
        for (int x = 42; x < 54; x += 2) {
            const size_t a = size_t(y) * 96u + size_t(x);
            REQUIRE(cpu.depth()[a] == cpu.depth()[a + 1u]);
            REQUIRE(cpu.depth()[a] == cpu.depth()[a + 96u]);
            REQUIRE(std::memcmp(cpu.color().data() + a * 4u, cpu.color().data() + (a + 1u) * 4u, 4u) == 0);
        }
    FluidSurfaceRenderer gpu(params, true);
    REQUIRE(gpu.configureSurfaceDownsample(2).ok());
    gpu.render(particles, .5f);
    REQUIRE(gpu.usingGpu());
    const size_t center = 48u * 96u + 48u;
    REQUIRE(std::abs(gpu.depth()[center] - cpu.depth()[center]) < 2e-5f);
    REQUIRE(glm::length(gpu.normals()[center] - cpu.normals()[center]) < .002f);
}

TEST_CASE("fluids.ssf.thicknessDownsampleIsIndependentBeforeVolumeShading") {
    auto* graphics = eve::graphics::Graphics::create();
    REQUIRE(graphics != nullptr);
    graphics->initHeadless(64, 64);
    VolumeFluidSettings settings;
    settings.gravity = glm::vec3(0.f);
    settings.spacing = .4f;
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position = {0, 1, 0};
    particle.color    = {.2f, .6f, .9f, 1.f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    FluidSurfaceParams params;
    params.width = params.height = 96;
    params.eye                   = {0, 1, 3};
    params.target                = {0, 1, 0};
    params.smoothIterations      = 0;
    FluidSurfaceRenderer cpu(params, false);
    REQUIRE(cpu.configureSurfaceDownsample(2).ok());
    REQUIRE(cpu.configureThicknessDownsample(4).ok());
    REQUIRE(!cpu.configureThicknessDownsample(0).ok());
    cpu.renderVolume(*solver);
    const size_t center = 48u * 96u + 48u;
    REQUIRE(cpu.color()[center * 4u + 3u] > 0u);
    for (int oy = 0; oy < 4; ++oy)
        for (int ox = 0; ox < 4; ++ox)
            REQUIRE(cpu.thickness()[center] == cpu.thickness()[size_t(48 + oy) * 96u + size_t(48 + ox)]);
    REQUIRE(cpu.depth()[center] == cpu.depth()[center + 1u]);

    FluidSurfaceRenderer gpu(params, true);
    REQUIRE(gpu.configureSurfaceDownsample(2).ok());
    REQUIRE(gpu.configureThicknessDownsample(4).ok());
    REQUIRE(gpu.prepare().ok());
    gpu.renderVolume(*solver);
    REQUIRE(gpu.usingGpu());
    REQUIRE(std::abs(gpu.depth()[center] - cpu.depth()[center]) < 2e-5f);
    REQUIRE(std::abs(gpu.thickness()[center] - cpu.thickness()[center]) < 1.f / 256.f + .000001f);
    REQUIRE(std::abs(int(gpu.color()[center * 4u + 3u]) - int(cpu.color()[center * 4u + 3u])) <= 1);
}
