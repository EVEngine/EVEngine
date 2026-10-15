#include "fluids/Fluids.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>
#include "fluids/FluidMath.h"
#include "fluids/FluidSdf.h"
#include "fluids/FluidSimulation.h"
#include "fluids/FluidSurfaceBinding.h"
#include "fluids/FluidSurfaceRenderer.h"
#include "fluids/SurfaceDropletSimulation.h"
#include "fluids/SurfaceFluidRenderData.h"
#include "fluids/SurfaceFluidSceneRenderer.h"
#include "fluids/SurfaceWetnessField.h"
#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidCodec.h"
#include "fluids/VolumeFluidDiffuse.h"
#include "graphics/Graphics.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// fluids.cpp
using namespace eve::fluids;

namespace {

[[maybe_unused]] bool tryInitHeadlessGfx() {
    auto* gfx = eve::graphics::Graphics::create();
    if (!gfx) return false;
    gfx->initHeadless(320, 240);
    return true;
}

/** @brief Subdivided icosahedron sphere mesh (positions + triangle indices). */
[[maybe_unused]] std::pair<std::vector<glm::vec3>, std::vector<uint32_t>> makeIcosphere(float radius, int subdivisions) {
    const float            t     = (1.f + std::sqrt(5.f)) / 2.f;
    std::vector<glm::vec3> verts = {
        glm::normalize(glm::vec3(-1.f, t, 0.f)),  glm::normalize(glm::vec3(1.f, t, 0.f)),
        glm::normalize(glm::vec3(-1.f, -t, 0.f)), glm::normalize(glm::vec3(1.f, -t, 0.f)),
        glm::normalize(glm::vec3(0.f, -1.f, t)),  glm::normalize(glm::vec3(0.f, 1.f, t)),
        glm::normalize(glm::vec3(0.f, -1.f, -t)), glm::normalize(glm::vec3(0.f, 1.f, -t)),
        glm::normalize(glm::vec3(t, 0.f, -1.f)),  glm::normalize(glm::vec3(t, 0.f, 1.f)),
        glm::normalize(glm::vec3(-t, 0.f, -1.f)), glm::normalize(glm::vec3(-t, 0.f, 1.f)),
    };
    std::vector<uint32_t> tris = {
        0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4,  11, 10, 2,  10, 7, 6, 7, 1, 8,
        3, 9,  4, 3, 4, 2, 3, 2, 6, 3, 6, 8,  3, 8,  9,  4, 9, 5, 2, 4,  11, 6,  2,  10, 8,  6, 7, 9, 8, 1,
    };
    for (int s = 0; s < subdivisions; ++s) {
        std::vector<uint32_t> nextTris;
        nextTris.reserve(tris.size() * 4);
        for (size_t i = 0; i < tris.size(); i += 3) {
            const uint32_t a = tris[i], b = tris[i + 1], c = tris[i + 2];
            const uint32_t ab = uint32_t(verts.size());
            const uint32_t bc = uint32_t(verts.size()) + 1;
            const uint32_t ca = uint32_t(verts.size()) + 2;
            verts.push_back(glm::normalize((verts[a] + verts[b]) * 0.5f));
            verts.push_back(glm::normalize((verts[b] + verts[c]) * 0.5f));
            verts.push_back(glm::normalize((verts[c] + verts[a]) * 0.5f));
            nextTris.insert(nextTris.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
        }
        tris.swap(nextTris);
    }
    for (glm::vec3& v : verts) v *= radius;
    return {std::move(verts), std::move(tris)};
}

[[maybe_unused]] float distToCenter(const glm::vec3& p) { return glm::length(p); }

}  // namespace


TEST_CASE("fluids.gpu.surfaceFlow") {
    if (!tryInitHeadlessGfx()) return;
    FluidParams params;
    params.gravity   = glm::vec3(0.f, -9.8f, 0.f);
    params.viscosity = 0.02f;
    FluidSimulator sim(2048, params, true);
    sim.setSdfSphere(0.f, 0.f, 0.f, 1.f, 32);
    sim.spawnDrop(glm::vec3(0.f, 1.5f, 0.f), 0.25f, 200);
    sim.step(1.f / 60.f);

    if (!sim.usingGpu()) return;  // no Vulkan device / no shader compiler — skip

    std::vector<float> dens;
    sim.readDensities(dens);
    REQUIRE(dens.size() == size_t(sim.getParticleCount()));
    for (float d : dens) CHECK(d > 0.f);

    float                  meanY0 = 0.f;
    std::vector<glm::vec3> pos;
    sim.readPositions(pos);
    for (const glm::vec3& p : pos) meanY0 += p.y;
    meanY0 /= float(pos.size());

    for (int s = 0; s < 120; ++s) sim.step(1.f / 60.f);
    sim.readPositions(pos);

    float meanY1       = 0.f;
    bool  allOnSurface = true;
    float maxP         = 0.f;
    for (const glm::vec3& p : pos) {
        meanY1 += p.y;
        maxP = std::max(maxP, distToCenter(p));
        if (distToCenter(p) < 1.f - 0.05f) allOnSurface = false;
    }
    meanY1 /= float(pos.size());
    CHECK(meanY1 < meanY0 - 0.05f);
    CHECK(allOnSurface);
    // Particles must stay glued to the SDF; a broken upload would let the
    // projection push them straight off the model (regression guard).
    CHECK(maxP < 3.f);
}

TEST_CASE("fluids.gpu.pbfCohesionClusterRemainsBounded") {
    if (!tryInitHeadlessGfx()) return;
    FluidParams params;
    params.gravity       = glm::vec3(0.f, -9.8f, 0.f);
    params.viscosity     = 0.05f;
    params.cohesion      = 0.5f;
    params.adhesion      = 0.5f;
    params.pbfIterations = 2;
    FluidSimulator sim(1024, params, true);
    sim.setSdfSphere(0.f, 0.f, 0.f, 1.f, 32);
    sim.spawnDrop(glm::vec3(0.f, 1.45f, 0.f), 0.22f, 128);
    sim.step(1.f / 60.f);
    if (!sim.usingGpu()) return;

    auto clusterRadius = [&]() {
        std::vector<glm::vec3> pos;
        sim.readPositions(pos);
        glm::vec3 c(0.f);
        for (const glm::vec3& p : pos) c += p;
        c /= float(pos.size());
        double sum = 0.0;
        for (const glm::vec3& p : pos) sum += glm::length(p - c);
        return float(sum / double(pos.size()));
    };

    const float before = clusterRadius();
    for (int s = 0; s < 60; ++s) sim.step(1.f / 60.f);
    const float after = clusterRadius();
    // Gravity, density relaxation and cohesion compete on the curved surface.
    // The GPU contract is bounded retention; CPU coverage above separately proves
    // that cohesion contracts a force-isolated cloud.
    CHECK(after < before * 1.25f);

    std::vector<glm::vec3> pos;
    sim.readPositions(pos);
    bool allOnSurface = true;
    for (const glm::vec3& p : pos) {
        if (distToCenter(p) < 1.f - 0.06f) allOnSurface = false;
    }
    CHECK(allOnSurface);
}

TEST_CASE("fluids.ssf.gpu.pipeline") {
    if (!tryInitHeadlessGfx()) return;
    FluidSurfaceParams params;
    params.width            = 64;
    params.height           = 64;
    params.eye              = glm::vec3(0.f, 0.f, -3.f);
    params.particleRadius   = 0.08f;
    params.smoothIterations = 0;
    FluidSurfaceRenderer r(params, true);
    r.render(std::vector<glm::vec3>{glm::vec3(0.f)}, 0.08f);
    if (!r.usingGpu()) return;

    const int cx = 32;
    const int cy = 32;
    CHECK(std::fabs(r.depth()[size_t(cy * 64 + cx)] - 3.f) < 0.25f);
    CHECK(r.normals()[size_t(cy * 64 + cx)].z > 0.8f);
    CHECK(r.thickness()[size_t(cy * 64 + cx)] > 0.f);
    CHECK(r.color()[size_t(cy * 64 + cx) * 4u + 3] > 0u);

    r.writePpm("fluid_ssf_gpu.ppm");
    std::ifstream f("fluid_ssf_gpu.ppm", std::ios::binary | std::ios::ate);
    REQUIRE(f.good());
    const long size = long(f.tellg());
    f.close();
    CHECK(size > 100);
    std::remove("fluid_ssf_gpu.ppm");
}

// fluids_foam_surface.cpp
using namespace eve::fluids;


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

// fluids_ssf.cpp
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

// fluids_surface.cpp
using namespace eve::fluids;
