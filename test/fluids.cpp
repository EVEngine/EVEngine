#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "fluids/FluidMath.h"
#include "fluids/FluidSdf.h"
#include "fluids/FluidSimulation.h"
#include "fluids/Fluids.h"
#include "graphics/Graphics.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

using namespace eve::fluids;

namespace {

bool tryInitHeadlessGfx() {
    auto* gfx = eve::graphics::Graphics::create();
    if (!gfx) return false;
    gfx->initHeadless(320, 240);
    return true;
}

/** @brief Subdivided icosahedron sphere mesh (positions + triangle indices). */
std::pair<std::vector<glm::vec3>, std::vector<uint32_t>> makeIcosphere(float radius, int subdivisions) {
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

float distToCenter(const glm::vec3& p) { return glm::length(p); }

}  // namespace

TEST_CASE("fluids.math.kernels") {
    const float h = 0.2f;
    // Poly6 is positive inside the support and zero at/outside the cutoff.
    CHECK(fluidPoly6(0.001f, h) > 0.f);
    CHECK(std::fabs(fluidPoly6(h * h, h)) < 1e-9f);
    CHECK(std::fabs(fluidPoly6((h + 0.1f) * (h + 0.1f), h)) < 1e-12f);
    // Poly6 is radially decreasing (denser near the center).
    CHECK(fluidPoly6(0.001f, h) > fluidPoly6(0.01f, h));

    // Spiky gradient points away from the neighbor and vanishes at cutoff.
    const glm::vec3 dx(0.1f, 0.f, 0.f);
    const glm::vec3 g = fluidSpikyGrad(dx, h);
    CHECK(g.x > 0.f);
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

    // Gradient points outward along +Y at the top.
    const glm::vec3 g = sdf.gradient(center + glm::vec3(0.f, radius, 0.f));
    CHECK(glm::dot(g, glm::vec3(0.f, 1.f, 0.f)) > 0.f);
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
}

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
    for (const glm::vec3& p : pos) {
        meanY1 += p.y;
        if (distToCenter(p) < 1.f - 0.05f) allOnSurface = false;
    }
    meanY1 /= float(pos.size());
    CHECK(meanY1 < meanY0 - 0.05f);
    CHECK(allOnSurface);
}
