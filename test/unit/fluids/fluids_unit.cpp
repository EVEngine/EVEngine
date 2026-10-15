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

TEST_CASE("fluids.math.kernels") {
    const float h = 0.2f;
    // Poly6 is positive inside the support and zero at/outside the cutoff.
    CHECK(fluidPoly6(0.001f, h) > 0.f);
    CHECK(std::fabs(fluidPoly6(h * h, h)) < 1e-9f);
    CHECK(std::fabs(fluidPoly6((h + 0.1f) * (h + 0.1f), h)) < 1e-12f);
    // Poly6 is radially decreasing (denser near the center).
    CHECK(fluidPoly6(0.001f, h) > fluidPoly6(0.01f, h));

    // With dx = p_i - p_j, the radially decreasing kernel gradient points toward j.
    const glm::vec3 dx(0.1f, 0.f, 0.f);
    const glm::vec3 g = fluidSpikyGrad(dx, h);
    CHECK(g.x < 0.f);
    CHECK(std::fabs(g.y) < 1e-9f);
    CHECK(glm::length(fluidSpikyGrad(glm::vec3(h, 0.f, 0.f), h)) < 1e-6f);
    CHECK(glm::length(fluidSpikyGrad(glm::vec3(0.f), h)) < 1e-9f);

    // Viscosity Laplacian is positive inside and zero at cutoff.
    CHECK(fluidViscLaplacian(0.05f, h) > 0.f);
    CHECK(std::fabs(fluidViscLaplacian(h, h)) < 1e-6f);
}

TEST_CASE("fluids.sdf.sphere") {
    const glm::vec3 center(0.f, 1.f, 0.f);
    const float     radius = 1.f;
    const MeshSdf   sdf    = MeshSdf::makeSphere(center, radius, glm::ivec3(32));
    REQUIRE(sdf.voxelCount() == 32 * 32 * 32);

    // Center is deep inside (distance ≈ -radius).
    CHECK(std::fabs(sdf.sample(center) + radius) < 0.2f);
    // Point on the surface reads ~0.
    CHECK(std::fabs(sdf.sample(center + glm::vec3(radius, 0.f, 0.f))) < 0.2f);
    // Far outside reads positive.
    CHECK(sdf.sample(center + glm::vec3(2.f * radius, 0.f, 0.f)) > 0.5f);
    CHECK(std::fabs(sdf.sample(center + glm::vec3(2.f * radius, 0.f, 0.f)) - radius) < 0.2f);

    // Gradient points outward along +Y at the top.
    const glm::vec3 g = sdf.gradient(center + glm::vec3(0.f, radius, 0.f));
    CHECK(glm::dot(g, glm::vec3(0.f, 1.f, 0.f)) > 0.f);
    CHECK(glm::distance(g, sdf.sampleWithGradient(center + glm::vec3(0.f, radius, 0.f)).gradient) < 1e-6f);
}

TEST_CASE("fluids.sdf.triangleMesh") {
    auto [verts, tris] = makeIcosphere(1.f, 2);
    REQUIRE(verts.size() >= 100);
    REQUIRE((tris.size() % 3) == 0);
    const MeshSdf mesh     = MeshSdf::makeFromTriangles(verts, tris, glm::ivec3(32));
    const MeshSdf analytic = MeshSdf::makeSphere(glm::vec3(0.f), 1.f, glm::ivec3(32));

    // The voxelized mesh should match the analytic sphere within ~2 voxels.
    const float                  tol    = 2.f * analytic.cellSize;
    const std::vector<glm::vec3> probes = {
        glm::vec3(0.f, 1.2f, 0.f), glm::vec3(1.1f, 0.f, 0.f),  glm::vec3(0.2f, -1.15f, 0.f),
        glm::vec3(0.f, 0.f, 0.f),  glm::vec3(0.9f, 0.6f, 0.f),
    };
    for (const glm::vec3& p : probes) {
        CHECK(std::fabs(mesh.sample(p) - analytic.sample(p)) < tol);
    }
    // Inside the mesh is negative.
    CHECK(mesh.sample(glm::vec3(0.f)) < 0.f);
}

TEST_CASE("fluids.cpu.surfaceFlowDownhill") {
    FluidParams params;
    params.gravity   = glm::vec3(0.f, -9.8f, 0.f);
    params.viscosity = 0.02f;
    FluidSimulation sim(2048, params);
    sim.setSdf(MeshSdf::makeSphere(glm::vec3(0.f), 1.f, glm::ivec3(32)));
    const int added = sim.spawnDrop(glm::vec3(0.f, 1.5f, 0.f), 0.25f, 200);
    REQUIRE(added == 200);

    // Spawn projects particles onto the surface (nothing inside the sphere).
    for (int i = 0; i < sim.particleCount(); ++i) {
        CHECK(distToCenter(sim.particles()[size_t(i)].pos) >= 1.f - 0.02f);
    }

    float meanY0 = 0.f;
    for (int i = 0; i < sim.particleCount(); ++i) meanY0 += sim.particles()[size_t(i)].pos.y;
    meanY0 /= float(sim.particleCount());

    for (int s = 0; s < 150; ++s) sim.step(1.f / 60.f);

    float meanY1       = 0.f;
    bool  allOnSurface = true;
    for (int i = 0; i < sim.particleCount(); ++i) {
        meanY1 += sim.particles()[size_t(i)].pos.y;
        if (distToCenter(sim.particles()[size_t(i)].pos) < 1.f - 0.03f) allOnSurface = false;
    }
    meanY1 /= float(sim.particleCount());

    // Water runs down the sphere.
    CHECK(meanY1 < meanY0 - 0.1f);
    // Projection keeps every particle glued to the surface.
    CHECK(allOnSurface);
}

TEST_CASE("fluids.cpu.viscosityDampsRelativeMotion") {
    FluidParams params;
    params.gravity    = glm::vec3(0.f);
    params.viscosity  = 0.5f;
    params.damping    = 0.f;
    params.iterations = 1;
    FluidSimulation sim(16, params);
    sim.setSdf(MeshSdf::makePlane(-2.f, glm::ivec3(8), 2.f));

    const int added = sim.spawnDrop(glm::vec3(0.f, 0.f, 0.f), 0.02f, 2);
    REQUIRE(added == 2);
    REQUIRE(sim.particleCount() == 2);
    // Co-locate the pair with opposing velocities: XSPH pulls them together.
    sim.particles()[0].pos = glm::vec3(0.f);
    sim.particles()[1].pos = glm::vec3(0.03f, 0.f, 0.f);
    sim.particles()[0].vel = glm::vec3(5.f, 0.f, 0.f);
    sim.particles()[1].vel = glm::vec3(-5.f, 0.f, 0.f);

    const float rel0 = glm::length(sim.particles()[0].vel - sim.particles()[1].vel);
    sim.step(1.f / 60.f);
    const float rel1 = glm::length(sim.particles()[0].vel - sim.particles()[1].vel);
    CHECK(rel1 < rel0);
}

TEST_CASE("fluids.cpu.densityPositiveNearNeighbors") {
    FluidParams     params;
    FluidSimulation sim(64, params);
    sim.setSdf(MeshSdf::makePlane(-2.f, glm::ivec3(8), 2.f));
    sim.spawnDrop(glm::vec3(0.f), 0.12f, 32);
    sim.step(1.f / 60.f);
    REQUIRE(sim.particleCount() > 0);
    for (int i = 0; i < sim.particleCount(); ++i) {
        CHECK(sim.densities()[size_t(i)] > 0.f);
    }
}

TEST_CASE("fluids.module.create") {
    auto* mod = Fluids::create();
    REQUIRE(mod != nullptr);
    CHECK_EQ(mod->getName(), std::string("Fluids"));
    FluidSimulator* sim = mod->newSimulator(1024);
    REQUIRE(sim != nullptr);
    CHECK_EQ(sim->getMaxParticles(), 1024);
    CHECK_EQ(mod->getSimulatorCount(), 1);
    CHECK_EQ(sim->getParticleCount(), 0);
    FluidSurfaceRenderer* r = mod->newSurfaceRenderer(32, 32);
    REQUIRE(r != nullptr);
    CHECK_EQ(mod->getRendererCount(), 1);
    CHECK_EQ(r->getWidth(), 32);
    r->render(*sim);
    CHECK_EQ(r->getHeight(), 32);
}

TEST_CASE("fluids.cpu.pbfSeparatesCompressedParticles") {
    FluidParams params;
    params.gravity       = glm::vec3(0.f);
    params.viscosity     = 0.02f;
    params.damping       = 0.2f;
    params.pbfIterations = 4;
    params.iterations    = 1;
    FluidSimulation sim(64, params);
    sim.setSdf(MeshSdf::makePlane(-2.f, glm::ivec3(8), 2.f));
    REQUIRE(sim.spawnDrop(glm::vec3(0.f), 0.03f, 12) == 12);

    auto meanPairDist = [&]() {
        double sum = 0.0;
        int    n   = 0;
        for (int i = 0; i < sim.particleCount(); ++i) {
            for (int j = i + 1; j < sim.particleCount(); ++j) {
                sum += glm::length(sim.particles()[size_t(i)].pos - sim.particles()[size_t(j)].pos);
                ++n;
            }
        }
        return float(sum / double(std::max(n, 1)));
    };

    const float before = meanPairDist();
    for (int s = 0; s < 40; ++s) sim.step(1.f / 60.f);
    const float after = meanPairDist();
    // A compressed cloud must relax toward rest density (particles spread).
    CHECK(after > before * 1.2f);
}

TEST_CASE("fluids.cpu.cohesionClumpsScatteredParticles") {
    FluidParams params;
    params.gravity       = glm::vec3(0.f);
    params.viscosity     = 0.08f;
    params.cohesion      = 0.5f;
    params.pbfIterations = 0;  // isolate the cohesion force
    params.damping       = 0.05f;
    FluidSimulation sim(64, params);
    sim.setSdf(MeshSdf::makePlane(-2.f, glm::ivec3(8), 2.f));
    REQUIRE(sim.spawnDrop(glm::vec3(0.f), 0.15f, 12) == 12);

    auto meanToCentroid = [&]() {
        glm::vec3 c(0.f);
        for (int i = 0; i < sim.particleCount(); ++i) c += sim.particles()[size_t(i)].pos;
        c /= float(sim.particleCount());
        double sum = 0.0;
        for (int i = 0; i < sim.particleCount(); ++i) sum += glm::length(sim.particles()[size_t(i)].pos - c);
        return float(sum / double(sim.particleCount()));
    };

    const float before = meanToCentroid();
    for (int s = 0; s < 90; ++s) sim.step(1.f / 60.f);
    const float after = meanToCentroid();
    // Surface tension (cohesion) pulls the scattered film into a tighter drop.
    CHECK(after < before * 0.75f);
}

TEST_CASE("fluids.cpu.adhesionKeepsDropOnUpsideDownSurface") {
    FluidParams sticky;
    sticky.gravity       = glm::vec3(0.f, -9.8f, 0.f);
    sticky.viscosity     = 0.05f;
    sticky.adhesion      = 1.5f;
    sticky.pbfIterations = 2;
    FluidSimulation sim(128, sticky);
    sim.setSdf(MeshSdf::makeSphere(glm::vec3(0.f), 1.f, glm::ivec3(32)));
    REQUIRE(sim.spawnDrop(glm::vec3(0.f, -1.45f, 0.f), 0.2f, 64) == 64);
    for (int s = 0; s < 90; ++s) sim.step(1.f / 60.f);

    float meanR = 0.f;
    for (int i = 0; i < sim.particleCount(); ++i) meanR += distToCenter(sim.particles()[size_t(i)].pos);
    meanR /= float(sim.particleCount());
    // Adhesion holds the drop on the underside of the sphere.
    CHECK(meanR < 1.08f);
}

TEST_CASE("fluids.cpu.yieldStressFreezesLowShearMotion") {
    auto run = [&](float yield) {
        FluidParams params;
        params.gravity       = glm::vec3(0.f);
        params.viscosity     = 0.01f;
        params.yieldStress   = yield;
        params.damping       = 0.f;
        params.pbfIterations = 0;
        FluidSimulation sim(64, params);
        sim.setSdf(MeshSdf::makePlane(-2.f, glm::ivec3(8), 2.f));
        REQUIRE(sim.spawnDrop(glm::vec3(0.f), 0.12f, 8) == 8);
        // Small deterministic velocities -> low shear rate.
        for (int i = 0; i < sim.particleCount(); ++i) {
            const float a                  = 0.02f;
            sim.particles()[size_t(i)].vel = glm::vec3(std::sin(float(i + 1)) * a, std::cos(float(i + 1) * 2.f) * a,
                                                       std::sin(float(i + 1) * 3.f) * a);
        }
        for (int s = 0; s < 30; ++s) sim.step(1.f / 60.f);
        glm::vec3 meanVelocity(0.f);
        for (int i = 0; i < sim.particleCount(); ++i) meanVelocity += sim.particles()[size_t(i)].vel;
        meanVelocity /= float(sim.particleCount());
        float ke = 0.f;
        for (int i = 0; i < sim.particleCount(); ++i) {
            const glm::vec3 relative = sim.particles()[size_t(i)].vel - meanVelocity;
            ke += 0.5f * glm::dot(relative, relative);
        }
        return ke;
    };

    const float keFree = run(0.f);
    const float keMud  = run(10.f);
    // Bingham yield stress turns low-shear flow into a quasi-solid.
    CHECK(keMud < keFree * 0.2f);
}

TEST_CASE("fluids.ssf.cpu.singleParticleDepthAndNormal") {
    FluidSurfaceParams params;
    // Odd dimensions place the particle center exactly on a pixel center.
    params.width            = 65;
    params.height           = 65;
    params.eye              = glm::vec3(0.f, 0.f, -3.f);
    params.particleRadius   = 0.08f;
    params.smoothIterations = 0;
    FluidSurfaceRenderer r(params, false);
    r.render(std::vector<glm::vec3>{glm::vec3(0.f)}, 0.08f);

    const int cx = 32;
    const int cy = 32;
    REQUIRE(std::fabs(r.depth()[size_t(cy * 65 + cx)] - (3.f - 0.08f)) < 1e-5f);
    const glm::vec3 n = r.normals()[size_t(cy * 65 + cx)];
    REQUIRE(n.z > 0.99f);
    REQUIRE(std::fabs(r.thickness()[size_t(cy * 65 + cx)] - 0.16f) < 1e-5f);
    REQUIRE(r.color()[size_t(cy * 65 + cx) * 4u + 3] > 0u);
}

TEST_CASE("fluids.ssf.cpu.waterVsMudShading") {
    auto renderMode = [](int mode) {
        FluidSurfaceParams params;
        params.width            = 64;
        params.height           = 64;
        params.eye              = glm::vec3(0.f, 0.f, -3.f);
        params.particleRadius   = 0.08f;
        params.smoothIterations = 0;
        params.mode             = mode;
        FluidSurfaceRenderer r(params, false);
        r.render(std::vector<glm::vec3>{glm::vec3(0.f)}, 0.08f);
        const size_t idx = size_t(32 * 64 + 32) * 4u;
        return std::array<uint8_t, 3>{r.color()[idx], r.color()[idx + 1], r.color()[idx + 2]};
    };

    const auto water = renderMode(0);
    const auto mud   = renderMode(1);
    // Water is blue-dominant; mud is brown-dominant (red > blue).
    CHECK(water[2] > water[0]);
    CHECK(mud[0] > mud[2]);
}


// fluids_foam_surface.cpp
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


// fluids_ssf.cpp
using namespace eve::fluids;


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


// fluids_surface.cpp
using namespace eve::fluids;

TEST_CASE("fluids.surfaceBinding.rigidPosePreservesMaterialLocation") {
    FluidSurfaceBinding          binding;
    const std::vector<glm::vec3> positions = {
        {0.f, 0.f, 0.f},
        {1.f, 0.f, 0.f},
        {0.f, 1.f, 0.f},
    };
    const std::vector<uint32_t>  indices = {0, 1, 2};
    const std::vector<glm::vec2> uvs     = {{0.f, 0.f}, {1.f, 0.f}, {0.f, 1.f}};
    REQUIRE(binding.build(positions, indices, uvs));

    const SurfaceLocation location{0, glm::vec3(0.25f, 0.25f, 0.5f)};
    const SurfaceSample   initial = binding.evaluate(location, 1.f);
    REQUIRE(glm::distance(initial.position, glm::vec3(0.25f, 0.5f, 0.f)) < 1e-5f);
    REQUIRE(glm::distance(initial.uv, glm::vec2(0.25f, 0.5f)) < 1e-5f);

    glm::mat4 pose(1.f);
    pose[3] = glm::vec4(2.f, -1.f, 3.f, 1.f);
    binding.setTransform(pose);
    const SurfaceSample moved = binding.evaluate(location, 0.5f);
    REQUIRE(glm::distance(moved.position, initial.position + glm::vec3(2.f, -1.f, 3.f)) < 1e-5f);
    REQUIRE(glm::distance(moved.velocity, glm::vec3(4.f, -2.f, 6.f)) < 1e-5f);
    REQUIRE(glm::distance(moved.uv, initial.uv) < 1e-5f);
}

TEST_CASE("fluids.surfaceBinding.deformationVelocityUsesBarycentricPose") {
    FluidSurfaceBinding          binding;
    const std::vector<glm::vec3> positions = {
        {-1.f, 0.f, 0.f},
        {1.f, 0.f, 0.f},
        {0.f, 1.f, 0.f},
    };
    REQUIRE(binding.build(positions, {0, 1, 2}));
    std::vector<glm::vec3> deformed = positions;
    deformed[2].z                   = 2.f;
    REQUIRE(binding.setDeformedPositions(deformed));

    const SurfaceSample anchored = binding.evaluate({0, glm::vec3(0.f, 0.f, 1.f)}, 0.25f);
    REQUIRE(glm::distance(anchored.position, glm::vec3(0.f, 1.f, 2.f)) < 1e-5f);
    REQUIRE(glm::distance(anchored.previousPosition, glm::vec3(0.f, 1.f, 0.f)) < 1e-5f);
    REQUIRE(glm::distance(anchored.velocity, glm::vec3(0.f, 0.f, 8.f)) < 1e-5f);
}

TEST_CASE("fluids.surfaceBinding.walkCrossesSharedEdge") {
    FluidSurfaceBinding          binding;
    const std::vector<glm::vec3> positions = {
        {0.f, 0.f, 0.f},
        {1.f, 0.f, 0.f},
        {0.f, 1.f, 0.f},
        {1.f, 1.f, 0.f},
    };
    REQUIRE(binding.build(positions, {0, 1, 2, 2, 1, 3}));
    REQUIRE_EQ(binding.adjacentTriangle(0, 0), 1);

    SurfaceLocation start;
    REQUIRE(binding.project(glm::vec3(0.25f, 0.25f, 0.1f), 0.2f, start));
    REQUIRE(start.triangle == 0);
    const SurfaceWalkResult walked = binding.walkAcrossSurface(start, glm::vec3(0.5f, 0.5f, 0.f));
    REQUIRE(walked.valid);
    REQUIRE(!walked.reachedBoundary);
    REQUIRE(walked.location.triangle == 1);
    const SurfaceSample end = binding.evaluate(walked.location, 0.f);
    REQUIRE(glm::distance(end.position, glm::vec3(0.75f, 0.75f, 0.f)) < 1e-4f);
}

TEST_CASE("fluids.surfaceBinding.openEdgeReturnsDetachRemainder") {
    FluidSurfaceBinding binding;
    REQUIRE(binding.build({{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}}, {0, 1, 2}));
    const SurfaceLocation   start{0, glm::vec3(0.8f, 0.1f, 0.1f)};
    const SurfaceWalkResult walked = binding.walkAcrossSurface(start, glm::vec3(-0.5f, -0.5f, 0.f));
    REQUIRE(walked.valid);
    REQUIRE(walked.reachedBoundary);
    REQUIRE(glm::length(walked.remainingDisplacement) > 0.1f);
    const SurfaceSample edge = binding.evaluate(walked.location, 0.f);
    REQUIRE(edge.position.x >= -1e-5f);
    REQUIRE(edge.position.y >= -1e-5f);
}

TEST_CASE("fluids.surfaceDroplet.gravityFlowsTangentially") {
    FluidSurfaceBinding binding;
    REQUIRE(binding.build({{-2.f, -2.f, 0.f}, {2.f, -2.f, 0.f}, {-2.f, 2.f, 0.f}}, {0, 1, 2}));
    SurfaceDropletParams params;
    params.friction = 0.f;
    SurfaceDropletSimulation droplets(&binding, params);
    const SurfaceLocation    start{0, glm::vec3(0.25f, 0.25f, 0.5f)};
    REQUIRE(droplets.addDroplet(start));
    const float y0 = binding.evaluate(start, 0.f).position.y;

    droplets.step(0.05f);
    REQUIRE(droplets.droplets().size() == 1u);
    const SurfaceDroplet& droplet = droplets.droplets().front();
    const SurfaceSample   sample  = binding.evaluate(droplet.location, 0.f);
    REQUIRE(sample.position.y < y0);
    REQUIRE(droplet.relativeVelocity.y < 0.f);
    REQUIRE(std::fabs(glm::dot(droplet.relativeVelocity, sample.normal)) < 1e-5f);
}

TEST_CASE("fluids.surfaceDroplet.largeStepMatchesFixedSubsteps") {
    const std::vector<glm::vec3> positions = {
        {-10.f, -10.f, 0.f},
        {10.f, -10.f, 0.f},
        {-10.f, 10.f, 0.f},
    };
    FluidSurfaceBinding oneFrameBinding;
    FluidSurfaceBinding fixedBinding;
    REQUIRE(oneFrameBinding.build(positions, {0, 1, 2}));
    REQUIRE(fixedBinding.build(positions, {0, 1, 2}));
    SurfaceDropletParams params;
    params.friction = 0.f;
    SurfaceDropletSimulation oneFrame(&oneFrameBinding, params);
    SurfaceDropletSimulation fixed(&fixedBinding, params);
    const SurfaceLocation    start{0, glm::vec3(0.25f, 0.25f, 0.5f)};
    REQUIRE(oneFrame.addDroplet(start));
    REQUIRE(fixed.addDroplet(start));
    oneFrame.step(0.2f);
    for (int i = 0; i < 4; ++i) fixed.step(0.05f);
    REQUIRE(oneFrame.droplets().size() == 1u);
    REQUIRE(fixed.droplets().size() == 1u);
    const glm::vec3 oneFramePosition = oneFrameBinding.evaluate(oneFrame.droplets().front().location, 0.f).position;
    const glm::vec3 fixedPosition    = fixedBinding.evaluate(fixed.droplets().front().location, 0.f).position;
    CHECK(glm::distance(oneFramePosition, fixedPosition) < 1e-4f);
    CHECK(glm::distance(oneFrame.droplets().front().relativeVelocity, fixed.droplets().front().relativeVelocity) <
          1e-4f);
}

TEST_CASE("fluids.surfaceDroplet.openEdgeDetachesToWorld") {
    FluidSurfaceBinding binding;
    REQUIRE(binding.build({{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}}, {0, 1, 2}));
    SurfaceDropletParams params;
    params.gravity  = glm::vec3(0.f);
    params.friction = 0.f;
    SurfaceDropletSimulation droplets(&binding, params);
    REQUIRE(droplets.addDroplet({0, glm::vec3(0.8f, 0.1f, 0.1f)}, 2.f, glm::vec3(-5.f, -5.f, 0.f)));

    droplets.step(0.05f);
    REQUIRE(droplets.droplets().empty());
    REQUIRE(droplets.detachedDroplets().size() == 1u);
    REQUIRE(std::fabs(droplets.detachedDroplets().front().volume - 2.f) < 1e-6f);
    REQUIRE(glm::length(droplets.detachedDroplets().front().velocity) > 5.f);
}

TEST_CASE("fluids.surfaceDroplet.surfaceAccelerationCanBreakAdhesion") {
    FluidSurfaceBinding binding;
    REQUIRE(binding.build({{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}}, {0, 1, 2}));
    SurfaceDropletParams params;
    params.gravity              = glm::vec3(0.f);
    params.friction             = 0.f;
    params.adhesionAcceleration = 10.f;
    SurfaceDropletSimulation droplets(&binding, params);
    REQUIRE(droplets.addDroplet({0, glm::vec3(0.4f, 0.3f, 0.3f)}));

    glm::mat4 pose1(1.f);
    pose1[3].z = -0.1f;
    binding.setTransform(pose1);
    droplets.step(0.1f);
    REQUIRE(droplets.droplets().size() == 1u);

    glm::mat4 pose2(1.f);
    pose2[3].z = -0.4f;
    binding.setTransform(pose2);
    droplets.step(0.1f);
    REQUIRE(droplets.droplets().empty());
    REQUIRE(droplets.detachedDroplets().size() == 1u);
    REQUIRE(droplets.detachedDroplets().front().velocity.z < -2.f);
}

TEST_CASE("fluids.surfaceDroplet.nearbyCapsMergeConservingVolume") {
    FluidSurfaceBinding binding;
    REQUIRE(binding.build({{-2.f, -2.f, 0.f}, {2.f, -2.f, 0.f}, {-2.f, 2.f, 0.f}}, {0, 1, 2}));
    // Both legacy scenarios used zero initial velocity: keep default and zero friction.
    for (float friction : {SurfaceDropletParams{}.friction, 0.f}) {
        SurfaceDropletParams params;
        params.gravity  = glm::vec3(0.f);
        params.friction = friction;
        SurfaceDropletSimulation droplets(&binding, params);
        REQUIRE(droplets.addDroplet({0, glm::vec3(0.25f, 0.25f, 0.5f)}, 0.001f));
        REQUIRE(droplets.addDroplet({0, glm::vec3(0.24f, 0.26f, 0.5f)}, 0.002f));

        droplets.step(0.01f);
        REQUIRE(droplets.droplets().size() == 1u);
        REQUIRE(std::fabs(droplets.droplets().front().volume - 0.003f) < 1e-6f);
        REQUIRE(droplets.dropletRadius(0.003f) > droplets.dropletRadius(0.001f));
    }
}

TEST_CASE("fluids.surfaceWetness.depositDiffuseAndEvaporate") {
    FluidSurfaceBinding binding;
    REQUIRE(binding.build({{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {1.f, 1.f, 0.f}}, {0, 1, 2, 2, 1, 3}));
    SurfaceWetnessField wetness;
    REQUIRE(wetness.build(binding));
    wetness.deposit({0, glm::vec3(1.f, 0.f, 0.f)}, 1.f);
    REQUIRE(wetness.values()[0] > 0.99f);
    REQUIRE(wetness.values()[3] < 1e-6f);

    SurfaceWetnessParams params;
    params.diffusion   = 1.f;
    params.evaporation = 0.5f;
    wetness.step(0.1f, params);
    REQUIRE(wetness.values()[1] > 0.f);
    REQUIRE(wetness.values()[0] < 1.f);
}

TEST_CASE("fluids.surfaceWetness.diffusionConservesFilmMass") {
    FluidSurfaceBinding binding;
    REQUIRE(binding.build({{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {1.f, 1.f, 0.f}}, {0, 1, 2, 2, 1, 3}));
    SurfaceWetnessField wetness;
    REQUIRE(wetness.build(binding));
    wetness.deposit({0, glm::vec3(1.f, 0.f, 0.f)}, 0.8f);
    const float          before = std::accumulate(wetness.values().begin(), wetness.values().end(), 0.f);
    SurfaceWetnessParams params;
    params.diffusion   = 1.f;
    params.evaporation = 0.f;
    params.maxWetness  = 10.f;
    wetness.step(0.1f, params);
    const float after = std::accumulate(wetness.values().begin(), wetness.values().end(), 0.f);
    CHECK(std::fabs(after - before) < 1e-6f);
}

TEST_CASE("fluids.surfaceRenderData.buildsTangentAreaPreservingCaps") {
    FluidSurfaceBinding binding;
    REQUIRE(binding.build({{-2.f, -2.f, 0.f}, {2.f, -2.f, 0.f}, {-2.f, 2.f, 0.f}}, {0, 1, 2}));
    SurfaceDropletParams params;
    params.gravity = glm::vec3(0.f);
    SurfaceDropletSimulation simulation(&binding, params);
    REQUIRE(simulation.addDroplet({0, glm::vec3(0.25f, 0.25f, 0.5f)}, 0.001f, glm::vec3(2.f, 0.f, 0.f)));
    SurfaceFluidRenderData renderData;
    renderData.update(binding, simulation);
    REQUIRE(renderData.droplets().size() == 1u);
    const SurfaceDropletRenderInstance& instance = renderData.droplets().front();
    CHECK(std::fabs(glm::dot(instance.normal, instance.majorAxis)) < 1e-6f);
    CHECK(std::fabs(glm::dot(instance.normal, instance.minorAxis)) < 1e-6f);
    CHECK(glm::length(instance.majorAxis) > glm::length(instance.minorAxis));
    const float footprint = glm::length(instance.majorAxis) * glm::length(instance.minorAxis);
    const float radius    = simulation.dropletRadius(0.001f);
    CHECK(std::fabs(footprint - radius * radius) < 1e-6f);
    CHECK(instance.capHeight > 0.f);
}

TEST_CASE("fluids.surfaceRenderData.wetnessDrivesPbrResponse") {
    const WetSurfaceMaterialSample dry = SurfaceFluidRenderData::evaluateMaterial(0.f);
    const WetSurfaceMaterialSample wet = SurfaceFluidRenderData::evaluateMaterial(1.f);
    CHECK(wet.roughness < dry.roughness);
    CHECK(wet.specular > dry.specular);
    CHECK(wet.darkening > dry.darkening);
    CHECK(wet.normalStrength > dry.normalStrength);
}

TEST_CASE("fluids.surfaceSceneRenderer.capModelTouchesBoundSurface") {
    SurfaceDropletRenderInstance instance;
    instance.position         = glm::vec3(2.f, 3.f, 4.f);
    instance.normal           = glm::normalize(glm::vec3(0.2f, 1.f, 0.3f));
    instance.majorAxis        = glm::vec3(0.4f, 0.f, 0.f);
    instance.minorAxis        = glm::vec3(0.f, 0.f, 0.2f);
    instance.capHeight        = 0.1f;
    const glm::mat4 model     = SurfaceFluidSceneRenderer::modelMatrix(instance);
    const glm::vec3 lowerPole = glm::vec3(model * glm::vec4(0.f, -1.f, 0.f, 1.f));
    CHECK(glm::distance(lowerPole, instance.position) < 1e-6f);
    CHECK(glm::distance(glm::vec3(model[0]), instance.majorAxis) < 1e-6f);
    CHECK(glm::distance(glm::vec3(model[2]), instance.minorAxis) < 1e-6f);
}
