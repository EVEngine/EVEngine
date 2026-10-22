#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fluids_shallow_water)
#include <cmath>
#include <cstdint>
#include <vector>
#include "PrivateTestScope.h"
#include "fluids/ShallowWaterField.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// fluids_shallow_water.cpp
namespace test_fluids_shallow_water {
using namespace eve::fluids;

TEST_CASE("fluids.shallowWater.createRejectsInvalidResolution") {
    ShallowWaterSettings settings;
    settings.resolutionX = 2;
    auto created         = ShallowWaterField::create(settings);
    REQUIRE(!created.ok());
}

TEST_CASE("fluids.shallowWater.fillAndQueriesSurfaceHeight") {
    ShallowWaterSettings settings;
    settings.resolutionX  = 32;
    settings.resolutionZ  = 32;
    settings.cellSize     = 0.5f;
    settings.initialDepth = 0.4f;
    settings.bedHeight    = 0.1f;
    settings.preferGpu    = false;
    auto created          = ShallowWaterField::create(settings);
    REQUIRE(created.ok());
    auto field = std::move(created).takeValue();
    REQUIRE(field->resolutionX() == 32);
    REQUIRE(!field->usingGpu());
    const float midX = settings.origin.x + 8.f;
    const float midZ = settings.origin.z + 8.f;
    REQUIRE(std::abs(field->surfaceHeightAt(midX, midZ) - 0.5f) < 0.02f);
    REQUIRE(std::abs(field->depthAt(midX, midZ) - 0.4f) < 0.02f);
}

TEST_CASE("fluids.shallowWater.impulsePropagatesAsWave") {
    ShallowWaterSettings settings;
    settings.resolutionX  = 48;
    settings.resolutionZ  = 48;
    settings.cellSize     = 0.25f;
    settings.initialDepth = 0.5f;
    settings.preferGpu    = false;
    auto created          = ShallowWaterField::create(settings);
    REQUIRE(created.ok());
    auto field = std::move(created).takeValue();

    const float cx = 6.f;
    const float cz = 6.f;
    REQUIRE(field->addImpulse(cx, cz, 1.4f, 0.45f).ok());
    const float probeX    = cx + 2.5f;
    const float beforeFar = field->surfaceHeightAt(probeX, cz);
    for (int i = 0; i < 90; ++i) REQUIRE(field->step(1.f / 60.f).ok());
    const float afterFar = field->surfaceHeightAt(probeX, cz);
    // A disturbance near the center must move measurable energy outward.
    REQUIRE(std::abs(afterFar - beforeFar) > 0.001f);
}

TEST_CASE("fluids.shallowWater.capsuleLegDisplacesWater") {
    ShallowWaterSettings settings;
    settings.resolutionX  = 40;
    settings.resolutionZ  = 40;
    settings.cellSize     = 0.2f;
    settings.initialDepth = 0.55f;
    settings.preferGpu    = false;
    auto created          = ShallowWaterField::create(settings);
    REQUIRE(created.ok());
    auto field = std::move(created).takeValue();

    const float cx     = 4.f;
    const float cz     = 4.f;
    const float before = field->depthAt(cx, cz);

    ShallowWaterCollider leg;
    leg.label             = 1;
    leg.shape             = ShallowWaterColliderShape::Capsule;
    leg.center            = {cx, 0.35f, cz};
    leg.halfExtent        = {0.12f, 0.45f, 0.12f};
    leg.radius            = 0.12f;
    leg.velocity          = {0.8f, 0.f, 0.f};
    leg.displacementScale = 2.5f;
    leg.dragScale         = 1.8f;
    REQUIRE(field->setColliders(std::span(&leg, 1)).ok());

    for (int i = 0; i < 45; ++i) REQUIRE(field->step(1.f / 60.f).ok());

    const float underFoot = field->depthAt(cx, cz);
    const float beside    = field->depthAt(cx + 0.8f, cz);
    const auto  flow      = field->velocityAt(cx + settings.cellSize, cz);
    // Check direct momentum transfer one cell downstream; wave propagation is tested separately.
    // Leg immersion lowers local depth and pushes water / momentum sideways.
    REQUIRE(underFoot < before);
    REQUIRE(beside > underFoot - 0.05f);
    REQUIRE(std::abs(flow.x) + std::abs(flow.y) > 0.01f);
}

TEST_CASE("fluids.shallowWater.setCollidersIsAtomicOnDuplicateLabels") {
    ShallowWaterSettings settings;
    settings.preferGpu = false;
    auto created       = ShallowWaterField::create(settings);
    REQUIRE(created.ok());
    auto field = std::move(created).takeValue();

    ShallowWaterCollider a;
    a.label  = 7;
    a.radius = 0.2f;
    REQUIRE(field->setColliders(std::span(&a, 1)).ok());
    REQUIRE(field->colliders().size() == 1);

    ShallowWaterCollider pair[2];
    pair[0]        = a;
    pair[1]        = a;
    pair[1].label  = 7;
    pair[1].center = {1.f, 1.f, 1.f};
    REQUIRE(!field->setColliders(std::span(pair, 2)).ok());
    REQUIRE(field->colliders().size() == 1);
    REQUIRE(field->colliders()[0].label == 7);
}

TEST_CASE("fluids.shallowWater.buildSurfaceMeshHasNormalsAndTriangles") {
    ShallowWaterSettings settings;
    settings.resolutionX = 16;
    settings.resolutionZ = 12;
    settings.preferGpu   = false;
    auto created         = ShallowWaterField::create(settings);
    REQUIRE(created.ok());
    auto field = std::move(created).takeValue();
    REQUIRE(field->addImpulse(2.f, 1.5f, 0.8f, 0.25f).ok());

    std::vector<float>    pos, nrm, uv;
    std::vector<uint32_t> idx;
    REQUIRE(field->buildSurfaceMesh(pos, nrm, uv, idx).ok());
    REQUIRE(pos.size() == size_t(16 * 12 * 3));
    REQUIRE(nrm.size() == pos.size());
    REQUIRE(uv.size() == size_t(16 * 12 * 2));
    REQUIRE(idx.size() == size_t(15 * 11 * 6));
    REQUIRE(idx.back() < uint32_t(16 * 12));

    std::vector<float> colors;
    REQUIRE(field->buildSurfaceMesh(pos, nrm, uv, idx, 4.f, &colors).ok());
    REQUIRE(colors.size() == size_t(16 * 12 * 4));
    float ymin = pos[1], ymax = pos[1];
    for (size_t i = 0; i < pos.size(); i += 3) {
        ymin = std::min(ymin, pos[i + 1]);
        ymax = std::max(ymax, pos[i + 1]);
    }
    REQUIRE(ymax - ymin > 0.05f);
}

TEST_CASE("fluids.shallowWater.copyFieldToImageEncodesDepthAndVelocity") {
    ShallowWaterSettings settings;
    settings.resolutionX  = 16;
    settings.resolutionZ  = 16;
    settings.cellSize     = 0.25f;
    settings.initialDepth = 0.5f;
    settings.maxDepth     = 2.5f;
    settings.preferGpu    = false;
    auto created          = ShallowWaterField::create(settings);
    REQUIRE(created.ok());
    auto field = std::move(created).takeValue();

    REQUIRE(field->addImpulse(2.f, 2.f, 1.f, 0.2f).ok());
    for (int i = 0; i < 8; ++i) REQUIRE(field->step(1.f / 60.f).ok());

    eve::image::ImageData fieldImage(16, 16, "RGBA8");
    REQUIRE(field->copyFieldToImage(fieldImage, 2.f).ok());
    eve::image::ImageData contrastImage(16, 16, "RGBA8");
    REQUIRE(field->copyFieldToImage(contrastImage, 2.f, 0.08f).ok());
    eve::image::ImageData albedo(16, 16, "RGBA8");
    REQUIRE(field->copyShadedAlbedoToImage(albedo, 0.5f, 2.f).ok());
    eve::image::ImageData normals(16, 16, "RGBA8");
    REQUIRE(field->copyNormalToImage(normals, 6.f).ok());

    const auto* fieldPx     = static_cast<const uint8_t*>(fieldImage.getData());
    bool        depthVaries = false;
    bool        hasFlow     = false;
    for (int i = 0; i < 16 * 16; ++i) {
        const uint8_t r = fieldPx[size_t(i) * 4u + 0];
        const uint8_t g = fieldPx[size_t(i) * 4u + 1];
        const uint8_t b = fieldPx[size_t(i) * 4u + 2];
        const uint8_t a = fieldPx[size_t(i) * 4u + 3];
        if (r < 100 || r > 140) depthVaries = true;  // rest ≈ 0.5/2.5 → ~51; impulse raises it
        if (std::abs(int(g) - 128) > 4 || std::abs(int(b) - 128) > 4 || a > 4) hasFlow = true;
    }
    REQUIRE(depthVaries);
    REQUIRE(hasFlow);

    // Crest-heat packing: rest cells stay dark in R; raised water lights up.
    const auto* contrastPx = static_cast<const uint8_t*>(contrastImage.getData());
    uint8_t     rMin = 255, rMax = 0;
    for (int i = 0; i < 16 * 16; ++i) {
        const uint8_t r = contrastPx[size_t(i) * 4u + 0];
        rMin            = std::min(rMin, r);
        rMax            = std::max(rMax, r);
    }
    REQUIRE(rMin < 40);
    REQUIRE(rMax > 120);

    eve::image::ImageData wrong(8, 8, "RGBA8");
    REQUIRE(!field->copyFieldToImage(wrong, 2.f).ok());
}

TEST_CASE("fluids.shallowWater.cpuMassStaysBoundedAfterSteps") {
    ShallowWaterSettings settings;
    settings.resolutionX  = 24;
    settings.resolutionZ  = 24;
    settings.initialDepth = 0.5f;
    settings.preferGpu    = false;
    auto created          = ShallowWaterField::create(settings);
    REQUIRE(created.ok());
    auto field = std::move(created).takeValue();
    REQUIRE(field->addImpulse(3.f, 3.f, 1.f, 0.2f).ok());

    double mass0 = 0.0;
    for (float h : field->depths()) mass0 += double(h);
    for (int i = 0; i < 60; ++i) REQUIRE(field->step(1.f / 60.f).ok());
    double mass1 = 0.0;
    for (float h : field->depths()) mass1 += double(h);
    // Lax-Friedrichs dissipates energy but should not explode mass.
    REQUIRE(mass1 > mass0 * 0.5);
    REQUIRE(mass1 < mass0 * 1.5);
}
}  // namespace test_fluids_shallow_water
#endif


#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fluids)
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
#include "PrivateTestScope.h"
#include "common/SdfField3D.h"
#include "fluids/FluidMath.h"
#include "fluids/FluidSdf.h"
#include "fluids/FluidSimulation.h"
#include "fluids/FluidSurfaceBinding.h"
#include "fluids/FluidSurfaceRenderer.h"
#include "fluids/Fluids.h"
#include "fluids/MeshSdfField.h"
#include "fluids/SurfaceDropletSimulation.h"
#include "fluids/SurfaceFluidRenderData.h"
#include "fluids/SurfaceFluidSceneRenderer.h"
#include "fluids/SurfaceWetnessField.h"
#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidCodec.h"
#include "fluids/VolumeFluidDiffuse.h"
#include "graphics/Graphics.h"
#include "physics/DistanceField3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// fluids.cpp
namespace test_fluids_unit {
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
[[maybe_unused]] std::pair<std::vector<glm::vec3>, std::vector<uint32_t>> makeIcosphere(float radius,
                                                                                        int   subdivisions) {
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

TEST_CASE("fluids.sdf.sharedInterfaceAndDistanceFieldConvert") {
    const MeshSdf sdf = MeshSdf::makeSphere(glm::vec3(0.f), 0.5f, glm::ivec3(16));
    MeshSdfField  adapter;
    adapter.bind(sdf);
    REQUIRE(adapter.revision() == 1u);
    REQUIRE(adapter.sample(0.f, 0.f, 0.f) < 0.f);
    float nx = 0, ny = 0, nz = 0;
    REQUIRE(adapter.gradient(0.f, 0.6f, 0.f, nx, ny, nz) == eve::SdfField3DGradientStatus::Defined);
    REQUIRE(ny > 0.5f);

    auto field = distanceFieldFromMeshSdf(sdf, 1000.f);
    REQUIRE(field.ok());
    REQUIRE(std::fabs(field.value().sample(0.f, 0.f, 0.f) - sdf.sample(glm::vec3(0.f))) < 1e-5f);
    auto back = meshSdfFromDistanceField(field.value());
    REQUIRE(back.ok());
    REQUIRE(back.value().distances == sdf.distances);

    auto blob = sdfField3DBlobFromMeshSdf(sdf);
    REQUIRE(blob.ok());
    auto encoded = eve::encodeSdfField3DBlob(blob.value());
    REQUIRE(encoded.ok());
    auto decoded = eve::decodeSdfField3DBlob(encoded.value());
    REQUIRE(decoded.ok());
    auto fromBlob = meshSdfFromSdfField3DBlob(decoded.value());
    REQUIRE(fromBlob.ok());
    REQUIRE(fromBlob.value().distances == sdf.distances);
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
}  // namespace test_fluids_unit
#endif


#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fluids_volume)
#include <cmath>
#include <glm/geometric.hpp>
#include <limits>
#include "PrivateTestScope.h"
#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidCodec.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// fluids_volume.cpp
namespace test_fluids_volume {
using namespace eve::fluids;

static eve::Value withoutRecentMaterialFields(eve::Value particle) {
    const auto* current = particle.find("material");
    eve::Value  material(eve::Value::Object{});
    for (const auto& key : current->keys())
        if (key != "atmosphericPressure" && key != "smoothing" && key != "rollingContacts" &&
            key != "rollingFriction" && key != "dynamicFriction" && key != "staticFriction" && key != "stickiness" &&
            key != "stickDistance" && key != "frictionCombine" && key != "stickinessCombine")
            material.set(key, *current->find(key));
    eve::Value legacy(eve::Value::Object{});
    for (const auto& key : particle.keys())
        if (key != "angularVelocity") legacy.set(key, *particle.find(key));
    legacy.set("material", std::move(material));
    return legacy;
}
static eve::Value withoutSdfColliders(const eve::Value& snapshot) {
    eve::Value legacy(eve::Value::Object{});
    for (const auto& key : snapshot.keys()) {
        if (key == "sdfColliders" || key == "heightFieldColliders" || key == "stitches") continue;
        if (key != "colliders") {
            legacy.set(key, *snapshot.find(key));
            continue;
        }
        eve::Value::Array colliders;
        const auto*       source = snapshot.find(key);
        for (size_t i = 0; i < source->arraySize(); ++i) {
            eve::Value collider(eve::Value::Object{});
            for (const auto& field : source->at(i).keys())
                if (field != "isTrigger" && field != "staticFriction" && field != "rollingFriction" &&
                    field != "stickiness" && field != "stickDistance" && field != "frictionCombine" &&
                    field != "stickinessCombine" && field != "rollingContacts")
                    collider.set(field, *source->at(i).find(field));
            colliders.push_back(std::move(collider));
        }
        legacy.set(key, eve::Value(std::move(colliders)));
    }
    return legacy;
}

TEST_CASE("fluids.volume.renderCopyOwnership") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle p{{0.f, 1.f, 0.f}, {0.f, 0.f, 0.f}};
    p.radii       = {.04f, .08f, .12f};
    p.orientation = {0.f, 0.f, .70710677f, .70710677f};
    REQUIRE(solver->emit(std::span(&p, 1)).ok());
    std::vector<glm::vec3> positions;
    std::vector<glm::vec4> colors;
    REQUIRE(solver->copyRenderData(positions, colors) == 0.1f);
    REQUIRE(solver->particleCount() == 1);
    REQUIRE(positions[0].y == 1.f);
    const auto view = solver->particleView();
    REQUIRE(view.size() == 1);
    REQUIRE(view[0].position == p.position);
    std::vector<glm::vec3> radii;
    std::vector<glm::vec4> orientations;
    REQUIRE(solver->copySurfaceRenderData(positions, colors, radii, orientations) == .1f);
    REQUIRE(radii[0] == p.radii);
    REQUIRE(glm::length(orientations[0] - p.orientation) < 1e-6f);
    const auto* radiiStorage = radii.data();
    const auto* storage      = positions.data();
    REQUIRE(solver->step(1.f / 60.f).ok());
    REQUIRE(positions[0].y == 1.f);
    REQUIRE(solver->copyRenderData(positions, colors) == 0.1f);
    REQUIRE(positions.data() == storage);
    REQUIRE(solver->copySurfaceRenderData(positions, colors, radii, orientations) == .1f);
    REQUIRE(radii.data() == radiiStorage);
    REQUIRE(positions[0].y < 1.f);
    solver->clear();
    REQUIRE(solver->particleCount() == 0);
    REQUIRE(solver->copyRenderData(positions, colors) == 0.1f);
    REQUIRE(positions.empty());
    REQUIRE(colors.empty());
    REQUIRE(solver->copySurfaceRenderData(positions, colors, radii, orientations) == .1f);
    REQUIRE(radii.empty());
    REQUIRE(orientations.empty());
}

TEST_CASE("fluids.volume.renderInterpolationTracksCompactionTeleportAndRestore") {
    VolumeFluidSettings settings;
    settings.gravity  = {0.f, 0.f, 0.f};
    settings.capacity = 4;
    auto created      = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                             solver = std::move(created).takeValue();
    std::vector<VolumeFluidParticle> particles(3);
    particles[0].position = {-1.f, 1.f, 0.f};
    particles[0].velocity = {1.f, 0.f, 0.f};
    particles[1].position = {0.f, 1.f, 0.f};
    particles[1].velocity = {2.f, 0.f, 0.f};
    particles[1].life     = .001f;
    particles[2].position = {1.f, 1.f, 0.f};
    particles[2].velocity = {3.f, 0.f, 0.f};
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    std::vector<glm::vec3> positions;
    REQUIRE(solver->copyInterpolatedPositions(0.f, positions).ok());
    REQUIRE(positions.size() == 2);
    REQUIRE(positions[0] == glm::vec3(-1.f, 1.f, 0.f));
    REQUIRE(positions[1] == glm::vec3(1.f, 1.f, 0.f));
    REQUIRE(solver->copyInterpolatedPositions(.5f, positions).ok());
    REQUIRE(std::abs(positions[0].x - (-1.f + .5f / 120.f)) < 1e-5f);
    REQUIRE(std::abs(positions[1].x - (1.f + 1.5f / 120.f)) < 1e-5f);
    const auto preserved = positions;
    REQUIRE(!solver->copyInterpolatedPositions(1.01f, positions).ok());
    REQUIRE(positions == preserved);
    REQUIRE(solver->killParticle(0).ok());
    REQUIRE(solver->copyInterpolatedPositions(0.f, positions).ok());
    REQUIRE(positions[0].x == 1.f);
    REQUIRE(solver->teleportActor({0, 0, 0}, {0, 0, 0, 1}, {.5f, 0, 0}, {0, 0, 0, 1}).ok());
    REQUIRE(solver->copyInterpolatedPositions(0.f, positions).ok());
    REQUIRE(positions[0] == solver->particleView()[0].position);
    const auto saved = solver->snapshot();
    REQUIRE(solver->restore(saved).ok());
    REQUIRE(solver->copyInterpolatedPositions(0.f, positions).ok());
    REQUIRE(positions[0] == saved.particles[0].position);
}

TEST_CASE("fluids.volume.killParticleRemapsOwnedLinksAtomically") {
    VolumeFluidSnapshot state;
    state.settings.gravity  = {0, 0, 0};
    state.settings.capacity = 8;
    state.particles.resize(3);
    for (unsigned i = 0; i < 3; ++i) state.particles[i].position = {-.3f + .3f * float(i), 1.f, 0.f};
    state.particles[2].material.phase = VolumeFluidPhase::Solid;
    VolumeFluidCollider collider;
    collider.label = 7;
    state.colliders.push_back(collider);
    state.attachments.push_back({2, 7, {0, 1, 0}, {0, 0, 0, 1}});
    state.simplexes = {{{0, 2, 0}, 2}, {{1, 2, 0}, 2}};
    auto created    = VolumeFluid::create(state.settings);
    REQUIRE(created.ok());
    auto solver = std::move(created).takeValue();
    REQUIRE(solver->restore(state).ok());

    REQUIRE(solver->killParticle(1).ok());
    const auto after = solver->snapshot();
    REQUIRE(after.particles.size() == 2);
    REQUIRE(after.settings.capacity == 8);
    REQUIRE(after.attachments.size() == 1);
    REQUIRE(after.attachments[0].particleIndex == 1);
    REQUIRE(after.simplexes.size() == 1);
    REQUIRE(after.simplexes[0].particleIndices[0] == 0);
    REQUIRE(after.simplexes[0].particleIndices[1] == 1);
    REQUIRE(solver->availableCapacity() == 6);

    const auto beforeInvalid = solver->snapshot();
    REQUIRE(!solver->killParticle(2).ok());
    const auto unchanged = solver->snapshot();
    REQUIRE(unchanged.particles.size() == beforeInvalid.particles.size());
    REQUIRE(unchanged.attachments.size() == beforeInvalid.attachments.size());
    REQUIRE(unchanged.simplexes.size() == beforeInvalid.simplexes.size());
}

TEST_CASE("fluids.volume.killActorParticlesCompactsOnceAndRemapsOwnedLinks") {
    VolumeFluidSnapshot state;
    state.settings.gravity  = {0, 0, 0};
    state.settings.capacity = 8;
    state.particles.resize(4);
    for (unsigned i = 0; i < 4; ++i) {
        state.particles[i].position   = {-.6f + .4f * float(i), 1.f, 0.f};
        state.particles[i].actorGroup = (i == 0 || i == 2) ? 7 : 8;
    }
    state.particles[3].material.phase = VolumeFluidPhase::Solid;
    VolumeFluidCollider collider;
    collider.label = 71;
    state.colliders.push_back(collider);
    state.attachments.push_back({3, 71, state.particles[3].position, {0, 0, 0, 1}});
    state.stitches.push_back({1, 3, .01f});
    state.simplexes = {{{0, 1, 0}, 2}, {{1, 3, 0}, 2}};
    auto created    = VolumeFluid::create(state.settings);
    REQUIRE(created.ok());
    auto solver = std::move(created).takeValue();
    REQUIRE(solver->restore(state).ok());
    REQUIRE(solver->killActorParticles(7).value() == 2);
    const auto after = solver->snapshot();
    REQUIRE(after.particles.size() == 2);
    CHECK(after.particles[0].actorGroup == 8);
    CHECK(after.particles[1].actorGroup == 8);
    REQUIRE(after.attachments.size() == 1);
    CHECK(after.attachments[0].particleIndex == 1);
    REQUIRE(after.stitches.size() == 1);
    CHECK(after.stitches[0].particleIndex1 == 0);
    CHECK(after.stitches[0].particleIndex2 == 1);
    REQUIRE(after.simplexes.size() == 1);
    CHECK(after.simplexes[0].particleIndices[0] == 0);
    CHECK(after.simplexes[0].particleIndices[1] == 1);
    CHECK(solver->availableCapacity() == 6);
    const auto unchanged = solver->snapshot();
    CHECK(!solver->killActorParticles(7).ok());
    CHECK(solver->snapshot().particles.size() == unchanged.particles.size());
    CHECK(solver->snapshot().attachments.size() == unchanged.attachments.size());
}

TEST_CASE("fluids.volume.freeFall") {
    auto result = VolumeFluid::create({});
    REQUIRE(result.ok());
    auto                      solver = std::move(result).takeValue();
    const VolumeFluidParticle p{{0.f, 2.f, 0.f}, {0.f, 0.f, 0.f}};
    REQUIRE(solver->emit(std::span(&p, 1)).ok());
    for (int i = 0; i < 12; ++i) REQUIRE(solver->step(1.f / 60.f).ok());
    const auto out = solver->particles();
    REQUIRE(std::abs(out[0].velocity.y + 9.81f * 0.2f) < 0.001f);
    REQUIRE(std::abs(out[0].position.y - (2.f - 0.5f * 9.81f * 0.04f)) < 0.005f);
}

TEST_CASE("fluids.volume.worldSpaceGravityUpdatesAtomically") {
    VolumeFluidSettings settings;
    settings.gravity = {0.f, 0.f, 0.f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position = {0.f, 1.f, 0.f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    REQUIRE(solver->setGravity({0.f, 9.81f, 0.f}).ok());
    REQUIRE(solver->gravity() == glm::vec3(0.f, 9.81f, 0.f));
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particleView()[0].velocity.y > 0.f);
    const auto before = solver->snapshot();
    REQUIRE(!solver->setGravity({std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f}).ok());
    REQUIRE(!solver->setGravity({1001.f, 0.f, 0.f}).ok());
    REQUIRE(solver->gravity() == before.settings.gravity);
    REQUIRE(solver->snapshot().settings.gravity == before.settings.gravity);
}

TEST_CASE("fluids.volume.actorMassPropertiesAreWeightedAndExplicit") {
    VolumeFluidSettings settings;
    settings.spacing = .1f;
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position         = {0.f, 1.f, 0.f};
    particles[0].actorGroup       = 7;
    particles[0].material.density = 1000.f;
    particles[1].position         = {1.f, 1.f, 0.f};
    particles[1].actorGroup       = 7;
    particles[1].material.density = 500.f;
    particles[2].position         = {1.5f, 1.f, 0.f};
    particles[2].actorGroup       = 8;
    particles[2].material.density = 1000.f;
    REQUIRE(solver->emit(particles).ok());

    auto properties = solver->actorMassProperties(7);
    REQUIRE(properties.ok());
    CHECK(properties.value().particleCount == 2);
    CHECK(std::abs(properties.value().mass - 1.5f) < 1e-6f);
    CHECK(std::abs(properties.value().centerOfMass.x - 1.f / 3.f) < 1e-6f);
    CHECK(properties.value().centerOfMass.y == 1.f);
    CHECK(!solver->actorMassProperties(9).ok());
    CHECK(!solver->actorMassProperties(0x01000000u).ok());
}

TEST_CASE("fluids.volume.particleDragIsBoundedConstantWork") {
    VolumeFluidSettings settings;
    settings.gravity = {0.f, 0.f, 0.f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position = {0.f, 1.f, 0.f};
    particle.velocity = {1.f, 0.f, 0.f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());

    REQUIRE(solver->applyParticleDrag(0, {1.f, 1.f, 0.f}, 12.f, 2.f, .01f).ok());
    CHECK(std::abs(solver->particleView()[0].velocity.x - 1.1f) < 1e-6f);
    const auto before = solver->snapshot();
    CHECK(!solver->applyParticleDrag(1, {1.f, 1.f, 0.f}, 12.f, 2.f, .01f).ok());
    CHECK(!solver->applyParticleDrag(0, {100.f, 1.f, 0.f}, 10000.f, 0.f, 1.f / 30.f).ok());
    CHECK(!solver->applyParticleDrag(0, {1.f, 1.f, 0.f}, -1.f, 2.f, .01f).ok());
    CHECK(solver->snapshot().particles[0].velocity == before.particles[0].velocity);
}

TEST_CASE("fluids.volume.admissionAtomic") {
    VolumeFluidSettings settings;
    settings.capacity = 1;
    auto result       = VolumeFluid::create(settings);
    REQUIRE(result.ok());
    auto                      solver  = std::move(result).takeValue();
    const VolumeFluidParticle batch[] = {{{0.f, 1.f, 0.f}, {}}, {{0.f, 2.f, 0.f}, {}}};
    REQUIRE(!solver->emit(batch).ok());
    REQUIRE(solver->particles().empty());
    REQUIRE(solver->emit(std::span(batch, 1)).ok());
    REQUIRE(!solver->step(std::numeric_limits<float>::quiet_NaN()).ok());
    REQUIRE(!solver->step(0.03f, 1).ok());
    REQUIRE(solver->particles()[0].position.y == 1.f);
    auto invalid = VolumeFluid::create(VolumeFluidSettings{.capacity = 0});
    REQUIRE(!invalid.ok());
}

TEST_CASE("fluids.volume.damBreakContainedRepeatable") {
    VolumeFluidSettings settings;
    settings.spacing = 0.12f;
    auto result      = VolumeFluid::create(settings);
    auto second      = VolumeFluid::create(settings);
    REQUIRE(result.ok());
    REQUIRE(second.ok());
    auto                             solver = std::move(result).takeValue();
    auto                             replay = std::move(second).takeValue();
    std::vector<VolumeFluidParticle> batch;
    for (int z = 0; z < 5; ++z)
        for (int y = 0; y < 9; ++y)
            for (int x = 0; x < 5; ++x)
                batch.push_back({{-1.7f + x * 0.12f, 0.1f + y * 0.12f, -0.24f + z * 0.12f}, {}});
    REQUIRE(solver->emit(batch).ok());
    REQUIRE(replay->emit(batch).ok());
    for (int i = 0; i < 60; ++i) {
        REQUIRE(solver->step(1.f / 60.f).ok());
        REQUIRE(replay->step(1.f / 60.f).ok());
    }
    const auto a = solver->particles(), b = replay->particles();
    float      maxX = -2.f;
    for (size_t i = 0; i < a.size(); ++i) {
        REQUIRE(std::isfinite(a[i].position.x));
        REQUIRE(a[i].position.y >= 0.06f);
        REQUIRE(a[i].position.y <= 2.94f);
        REQUIRE(a[i].position.x >= -1.94f);
        REQUIRE(a[i].position.x <= 1.94f);
        REQUIRE(a[i].position.z == b[i].position.z);
        REQUIRE(a[i].position.x == b[i].position.x);
        REQUIRE(a[i].position.y == b[i].position.y);
        maxX = std::max(maxX, a[i].position.x);
    }
    REQUIRE(maxX > -1.f);
}

TEST_CASE("fluids.volume.gasDiffusionLifetimeAndGranules") {
    auto result = VolumeFluid::create({});
    REQUIRE(result.ok());
    auto                solver = std::move(result).takeValue();
    VolumeFluidParticle pair[2];
    pair[0].position       = {0.f, 1.f, 0.f};
    pair[1].position       = {0.08f, 1.f, 0.f};
    pair[0].material.phase = pair[1].material.phase = VolumeFluidPhase::Gas;
    pair[0].material.buoyancy = pair[1].material.buoyancy = -1.f;
    pair[0].material.diffusion = pair[1].material.diffusion = 20.f;
    pair[0].color                                           = {1.f, 0.f, 0.f, 1.f};
    pair[1].color                                           = {0.f, 0.f, 1.f, 1.f};
    pair[0].life                                            = 0.1f;
    REQUIRE(solver->emit(pair).ok());
    REQUIRE(solver->step(1.f / 60.f).ok());
    auto state = solver->particles();
    REQUIRE(state[0].velocity.y > 0.f);
    REQUIRE(state[0].color.b > 0.f);
    REQUIRE(state[1].color.r > 0.f);
    REQUIRE(std::abs(state[0].color.r + state[1].color.r - 1.f) < 1e-5f);
    for (int i = 0; i < 8; ++i) REQUIRE(solver->step(1.f / 60.f).ok());
    REQUIRE(solver->particles().size() == 1);
    solver->clear();
    pair[0].life           = 0.f;
    pair[0].material.phase = pair[1].material.phase = VolumeFluidPhase::Granular;
    REQUIRE(solver->emit(pair).ok());
    REQUIRE(solver->step(1.f / 60.f).ok());
    state           = solver->particles();
    const auto diff = state[0].position - state[1].position;
    REQUIRE(std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z) >= 0.099f);
}

TEST_CASE("fluids.volume.phaseRenderCopyIsBoundedAndReportsTotal") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[3];
    for (auto& particle : particles) particle.position.y = 1.f;
    particles[0].material.phase = VolumeFluidPhase::Gas;
    particles[0].position.x     = .1f;
    particles[1].material.phase = VolumeFluidPhase::Liquid;
    particles[1].position.x     = .2f;
    particles[2].material.phase = VolumeFluidPhase::Gas;
    particles[2].position.x     = .3f;
    REQUIRE(solver->emit(particles).ok());
    std::vector<glm::vec3> positions;
    std::vector<glm::vec4> colors;
    CHECK(solver->copyPhaseRenderData(VolumeFluidPhase::Gas, positions, colors, 1) == 2);
    REQUIRE(positions.size() == 1);
    CHECK(positions[0].x == .1f);
    CHECK(colors.size() == 1);
}

TEST_CASE("fluids.volume.granularAngularIntegrationAndRollingContact") {
    VolumeFluidSettings settings;
    settings.gravity    = {0, 0, 0};
    settings.iterations = 1;
    auto created        = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle granular;
    granular.position                 = {0, .54f, 0};
    granular.velocity                 = {1, 0, 0};
    granular.angularVelocity          = {0, 0, 3};
    granular.material.phase           = VolumeFluidPhase::Granular;
    granular.material.rollingContacts = true;
    granular.material.rollingFriction = 0.f;
    VolumeFluidCollider floor;
    floor.shape      = VolumeFluidColliderShape::Box;
    floor.center     = {0, 0, 0};
    floor.halfExtent = {2, .5f, 2};
    floor.friction   = 1.f;
    REQUIRE(solver->setColliders(std::span(&floor, 1)).ok());
    REQUIRE(solver->emit(std::span(&granular, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    const auto contacted = solver->particles()[0];
    REQUIRE(std::abs(contacted.angularVelocity.z - 3.f) > .1f);
    REQUIRE(std::abs(contacted.orientation.z) > .001f);

    solver->clear();
    granular.velocity                 = {0, 0, 0};
    granular.angularVelocity          = {0, 0, 10};
    granular.material.rollingFriction = 1.f;
    REQUIRE(solver->emit(std::span(&granular, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(glm::length(solver->particles()[0].angularVelocity) < .001f);

    solver->clear();
    auto liquid                     = granular;
    liquid.position                 = {0, 1, 0};
    liquid.material.phase           = VolumeFluidPhase::Liquid;
    liquid.material.rollingContacts = false;
    liquid.angularVelocity          = {0, 0, 3};
    const auto original             = liquid.orientation;
    REQUIRE(solver->setColliders({}).ok());
    REQUIRE(solver->emit(std::span(&liquid, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particles()[0].orientation == original);
    auto invalid                                  = solver->snapshot();
    invalid.particles[0].material.rollingFriction = 1.01f;
    REQUIRE(!solver->restore(invalid).ok());
    REQUIRE(solver->snapshot().particles[0].material.rollingFriction == 1.f);
    solver->clear();
    granular.angularVelocity = {0, 0, 1001};
    REQUIRE(!solver->emit(std::span(&granular, 1)).ok());
}

TEST_CASE("fluids.volume.granularPairStaticDynamicAndRollingFriction") {
    VolumeFluidSettings settings;
    settings.gravity    = {0, 0, 0};
    settings.iterations = 1;
    auto run            = [&](float friction, bool rolling) {
        auto created = VolumeFluid::create(settings);
        REQUIRE(created.ok());
        auto                solver = std::move(created).takeValue();
        VolumeFluidParticle pair[2];
        pair[0].position = {-.045f, 1, 0};
        pair[1].position = {.045f, 1, 0};
        pair[0].velocity = {0, 0, 1};
        pair[1].velocity = {0, 0, -1};
        for (auto& p : pair) {
            p.material.phase           = VolumeFluidPhase::Granular;
            p.material.cohesion        = 0;
            p.material.dynamicFriction = friction;
            p.material.staticFriction  = friction;
            p.material.rollingContacts = rolling;
            p.material.rollingFriction = 0;
        }
        REQUIRE(solver->emit(pair).ok());
        REQUIRE(solver->step(1.f / 120.f, 1).ok());
        return solver;
    };
    auto       frictionless = run(0, false), frictional = run(1, true);
    const auto freeRelative =
        std::abs(frictionless->particles()[0].velocity.z - frictionless->particles()[1].velocity.z);
    const auto frictionRelative =
        std::abs(frictional->particles()[0].velocity.z - frictional->particles()[1].velocity.z);
    REQUIRE(frictionRelative < freeRelative - .1f);
    REQUIRE(std::abs(frictional->particles()[0].angularVelocity.y) > .1f);
    REQUIRE(std::abs(frictional->particles()[1].angularVelocity.y) > .1f);
    auto rolling                       = run(0, true);
    auto rollingState                  = rolling->snapshot();
    rollingState.particles[0].position = {-.045f, 1, 0};
    rollingState.particles[1].position = {.045f, 1, 0};
    rollingState.particles[0].velocity = rollingState.particles[1].velocity = glm::vec3(0.f);
    rollingState.particles[0].angularVelocity                               = {0, 10, 0};
    rollingState.particles[1].angularVelocity                               = {0, -10, 0};
    for (auto& p : rollingState.particles) p.material.rollingFriction = 1.f;
    REQUIRE(rolling->restore(rollingState).ok());
    REQUIRE(rolling->step(1.f / 120.f, 1).ok());
    REQUIRE(glm::length(rolling->particles()[0].angularVelocity - rolling->particles()[1].angularVelocity) < .001f);
    auto invalid                                 = rolling->snapshot();
    invalid.particles[0].material.staticFriction = 1.01f;
    REQUIRE(!rolling->restore(invalid).ok());
    REQUIRE(rolling->snapshot().particles[0].material.staticFriction == 0.f);
}

TEST_CASE("fluids.volume.atmosphericSurfacePressureDragAndMigration") {
    VolumeFluidSettings settings;
    settings.gravity    = {0, 0, 0};
    settings.iterations = 1;
    auto created        = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle pair[2];
    pair[0].position = {-.075f, 1, 0};
    pair[1].position = {.075f, 1, 0};
    for (auto& p : pair) {
        p.material.cohesion            = 0;
        p.material.viscosity           = 0;
        p.material.atmosphericPressure = 10.f;
    }
    REQUIRE(solver->emit(pair).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    const auto pressured = solver->particles();
    REQUIRE(pressured[0].velocity.x > 0.f);
    REQUIRE(pressured[1].velocity.x < 0.f);
    REQUIRE(glm::length(pressured[0].velocity + pressured[1].velocity) < 1e-6f);

    solver->clear();
    VolumeFluidParticle exposed;
    exposed.position           = {0, 1, 0};
    exposed.velocity           = {1, 0, 0};
    exposed.material.cohesion  = 0;
    exposed.material.viscosity = 0;
    exposed.material.drag      = 60.f;
    REQUIRE(solver->emit(std::span(&exposed, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particles()[0].velocity.x > 0.f);
    REQUIRE(solver->particles()[0].velocity.x < .7f);

    auto state    = solver->snapshot();
    auto encoded  = encodeVolumeFluid(state);
    auto particle = encoded.find("particles")->at(0);
    encoded.set("particles", eve::Value(eve::Value::Array{withoutRecentMaterialFields(std::move(particle))}));
    encoded = withoutSdfColliders(encoded);
    encoded.set("version", 9);
    auto migrated = decodeVolumeFluid(encoded);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().version == 20);
    REQUIRE(migrated.value().particles[0].material.atmosphericPressure == 0.f);
    auto invalid                                      = state;
    invalid.particles[0].material.atmosphericPressure = 1001.f;
    REQUIRE(!solver->restore(invalid).ok());
    REQUIRE(solver->snapshot().particles[0].material.atmosphericPressure == 0.f);
}

TEST_CASE("fluids.volume.transientPerParticleWind") {
    VolumeFluidSettings settings;
    settings.gravity    = {0, 0, 0};
    settings.iterations = 1;
    auto created        = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position           = {0, 1, 0};
    particle.material.cohesion  = 0;
    particle.material.viscosity = 0;
    particle.material.drag      = 60;
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    const glm::vec3 wind[] = {glm::vec3(6, 0, 0)};
    REQUIRE(solver->setParticleWinds(wind).ok());
    const glm::vec3 invalid[] = {glm::vec3(0), glm::vec3(0)};
    REQUIRE(!solver->setParticleWinds(invalid).ok());
    REQUIRE(!solver->step(-1.f, 1).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    const float driven = solver->particles()[0].velocity.x;
    REQUIRE(driven > 2.f);
    REQUIRE(driven < 3.f);
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particles()[0].velocity.x > 0.f);
    REQUIRE(solver->particles()[0].velocity.x < driven);
    REQUIRE(solver->setParticleWinds(wind).ok());
    REQUIRE(solver->setParticleWinds({}).ok());
    const float before = solver->particles()[0].velocity.x;
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particles()[0].velocity.x < before);
    const glm::vec3 nonfinite[] = {glm::vec3(std::numeric_limits<float>::quiet_NaN(), 0, 0)};
    REQUIRE(!solver->setParticleWinds(nonfinite).ok());
}

TEST_CASE("fluids.volume.boundedFluid3DWindZones") {
    VolumeFluidSettings settings;
    settings.gravity    = {0, 0, 0};
    settings.iterations = 1;
    auto created        = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position           = {1, 1, 0};
    particle.material.cohesion  = 0;
    particle.material.viscosity = 0;
    particle.material.drag      = 60;
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    VolumeFluidWindZone ambient;
    ambient.direction = {0, 1, 0};
    ambient.intensity = 2;
    VolumeFluidWindZone sphere;
    sphere.type                       = VolumeFluidWindZoneType::Spherical;
    sphere.center                     = {0, 1, 0};
    sphere.radius                     = 2;
    sphere.intensity                  = 4;
    sphere.radial                     = true;
    const VolumeFluidWindZone zones[] = {ambient, sphere};
    REQUIRE(solver->accumulateWindZones(zones, 0.f).ok());
    auto invalid   = sphere;
    invalid.radius = 0;
    REQUIRE(!solver->accumulateWindZones(std::span(&invalid, 1), 0.f).ok());
    REQUIRE(!solver->accumulateWindZones(zones, -1.f).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    const auto velocity = solver->particles()[0].velocity;
    REQUIRE(velocity.x > 0.f);
    REQUIRE(velocity.y > 0.f);
    REQUIRE(std::abs(velocity.x / velocity.y - 1.5f) < .001f);

    eve::Value zoneValue = eve::fluids::encodeVolumeFluidWindZone(ambient);
    auto       decoded   = decodeVolumeFluidWindZones(eve::Value(eve::Value::Array{zoneValue}));
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().size() == 1);
    REQUIRE(decoded.value()[0].intensity == 2.f);
    zoneValue.set("unknown", 1);
    REQUIRE(!decodeVolumeFluidWindZones(eve::Value(eve::Value::Array{zoneValue})).ok());
    REQUIRE(!decodeVolumeFluidWindZones(eve::Value(eve::Value::Array(65, eve::Value{}))).ok());

    VolumeFluidSettings largeSettings;
    largeSettings.capacity = 65536;
    auto largeCreated      = VolumeFluid::create(largeSettings);
    REQUIRE(largeCreated.ok());
    auto                             large = std::move(largeCreated).takeValue();
    std::vector<VolumeFluidParticle> particles(62501);
    for (auto& p : particles) p.position = {0, 1, 0};
    REQUIRE(large->emit(particles).ok());
    std::vector<VolumeFluidWindZone> spherical(64, sphere);
    REQUIRE(!large->accumulateWindZones(spherical, 0.f).ok());
}

TEST_CASE("fluids.volume.externalForcesUseInverseMassAndClearAfterSuccessfulStep") {
    VolumeFluidSettings settings;
    settings.gravity = {0.f, 0.f, 0.f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[2];
    particles[0].position         = {-.2f, 1.f, 0.f};
    particles[1].position         = {.2f, 1.f, 0.f};
    particles[0].material.density = 1000.f;
    particles[1].material.density = 2000.f;
    REQUIRE(solver->emit(particles).ok());
    const glm::vec3 forces[2] = {{8.f, 0.f, 0.f}, {8.f, 0.f, 0.f}};
    REQUIRE(solver->setParticleExternalForces(forces).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    const auto first = solver->particles();
    REQUIRE(std::abs(first[0].velocity.x - 2.f * first[1].velocity.x) < 1e-5f);
    REQUIRE(first[0].velocity.x > 0.f);
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    const auto second = solver->particles();
    REQUIRE(std::abs(second[0].velocity.x - first[0].velocity.x) < 1e-5f);
    REQUIRE(std::abs(second[1].velocity.x - first[1].velocity.x) < 1e-5f);
}

TEST_CASE("fluids.volume.externalForceZonesAreBoundedAndAtomic") {
    VolumeFluidSettings settings;
    settings.gravity = {0.f, 0.f, 0.f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position = {0.f, 1.f, 0.f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    VolumeFluidWindZone zone;
    zone.direction = {1.f, 0.f, 0.f};
    zone.intensity = 8.f;
    REQUIRE(solver->accumulateExternalForceZones(std::span(&zone, 1), 0.f).ok());
    auto invalid      = zone;
    invalid.intensity = 1001.f;
    REQUIRE(!solver->accumulateExternalForceZones(std::span(&invalid, 1), 0.f).ok());
    REQUIRE(!solver->step(1.f / 120.f, 0).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particles()[0].velocity.x > 0.f);
}

TEST_CASE("fluids.volume.perParticleSmoothingRadiusAndMigration") {
    VolumeFluidSettings settings;
    settings.gravity    = {0, 0, 0};
    settings.iterations = 1;
    auto run            = [&](float firstSmoothing, float secondSmoothing) {
        auto created = VolumeFluid::create(settings);
        REQUIRE(created.ok());
        auto                solver = std::move(created).takeValue();
        VolumeFluidParticle pair[2];
        pair[0].position = {-.125f, 1, 0};
        pair[1].position = {.125f, 1, 0};
        pair[0].velocity = {1, 0, 0};
        pair[1].velocity = {-1, 0, 0};
        for (auto& p : pair) {
            p.material.cohesion  = 0;
            p.material.viscosity = 100;
        }
        pair[0].material.smoothing = firstSmoothing;
        pair[1].material.smoothing = secondSmoothing;
        REQUIRE(solver->emit(pair).ok());
        REQUIRE(solver->step(1.f / 120.f, 1).ok());
        return solver;
    };
    auto compact = run(2, 2), mixed = run(1, 4);
    REQUIRE(std::abs(compact->particles()[0].velocity.x - 1.f) < 1e-6f);
    REQUIRE(mixed->particles()[0].velocity.x < .999f);
    REQUIRE(std::abs(mixed->particles()[0].velocity.x + mixed->particles()[1].velocity.x) < 1e-6f);

    auto              current = mixed->snapshot();
    auto              encoded = encodeVolumeFluid(current);
    eve::Value::Array particles;
    for (size_t i = 0; i < encoded.find("particles")->arraySize(); ++i) {
        const auto& currentParticle = encoded.find("particles")->at(i);
        const auto* source          = currentParticle.find("material");
        eve::Value  material(eve::Value::Object{});
        for (const auto& key : source->keys())
            if (key != "smoothing" && key != "rollingContacts" && key != "rollingFriction" &&
                key != "dynamicFriction" && key != "staticFriction" && key != "stickiness" && key != "stickDistance" &&
                key != "frictionCombine" && key != "stickinessCombine")
                material.set(key, *source->find(key));
        eve::Value particle(eve::Value::Object{});
        for (const auto& key : currentParticle.keys())
            if (key != "material" && key != "angularVelocity") particle.set(key, *currentParticle.find(key));
        particle.set("material", std::move(material));
        particles.push_back(std::move(particle));
    }
    encoded.set("particles", eve::Value(std::move(particles)));
    encoded = withoutSdfColliders(encoded);
    encoded.set("version", 10);
    auto migrated = decodeVolumeFluid(encoded);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().version == 20);
    REQUIRE(migrated.value().particles[0].material.smoothing == 2.f);
    REQUIRE(!migrated.value().particles[0].material.rollingContacts);
    REQUIRE(migrated.value().particles[0].material.rollingFriction == 0.f);
    REQUIRE(migrated.value().particles[0].material.dynamicFriction == .2f);
    REQUIRE(migrated.value().particles[0].material.staticFriction == .2f);
    REQUIRE(migrated.value().particles[0].angularVelocity == glm::vec3(0.f));
    auto invalid                            = current;
    invalid.particles[0].material.smoothing = 4.01f;
    REQUIRE(!mixed->restore(invalid).ok());
    REQUIRE(mixed->snapshot().particles[0].material.smoothing == 1.f);
}

TEST_CASE("fluids.volume.colliderSolidifyMelt") {
    auto result = VolumeFluid::create({});
    REQUIRE(result.ok());
    auto                solver = std::move(result).takeValue();
    VolumeFluidCollider collider;
    collider.label    = 9;
    collider.solidify = true;
    REQUIRE(solver->setColliders(std::span(&collider, 1)).ok());
    VolumeFluidParticle p;
    p.position   = {0.f, 1.25f, 0.f};
    p.actorGroup = 23;
    REQUIRE(solver->emit(std::span(&p, 1)).ok());
    REQUIRE(solver->step(1.f / 60.f).ok());
    REQUIRE(!solver->contacts().empty());
    REQUIRE(solver->contacts()[0].colliderLabel == 9);
    REQUIRE(solver->contacts()[0].particleIndex == 0);
    REQUIRE(solver->contacts()[0].actorGroup == 23);
    REQUIRE(solver->contacts()[0].impulse.y < 0.f);
    REQUIRE(solver->particles()[0].material.phase == VolumeFluidPhase::Solid);
    const auto frozen = solver->particles()[0].position;
    REQUIRE(solver->step(1.f / 60.f).ok());
    REQUIRE(solver->particles()[0].position.y == frozen.y);
    REQUIRE(solver->setColliders({}).ok());
    REQUIRE(solver->paint(frozen, 0.1f, {}).ok());
    REQUIRE(solver->step(1.f / 60.f).ok());
    REQUIRE(solver->particles()[0].position.y < frozen.y);
}

TEST_CASE("fluids.volume.solidifyStopsFurtherColliderProjection") {
    auto result = VolumeFluid::create({});
    REQUIRE(result.ok());
    auto                solver = std::move(result).takeValue();
    VolumeFluidCollider first;
    first.label    = 1;
    first.solidify = true;
    VolumeFluidCollider second;
    second.label                          = 2;
    second.center                         = {0.1f, 1.3f, 0.f};
    const VolumeFluidCollider colliders[] = {first, second};
    REQUIRE(solver->setColliders(colliders).ok());
    VolumeFluidParticle particle;
    particle.position = {0.f, 1.25f, 0.f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    const auto frozen = solver->particles()[0];
    REQUIRE(frozen.material.phase == VolumeFluidPhase::Solid);
    REQUIRE(frozen.position.x == 0.f);
    REQUIRE(solver->contacts().size() == 1);
    REQUIRE(solver->contacts()[0].colliderLabel == 1);
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particles()[0].position == frozen.position);
}

TEST_CASE("fluids.volume.solidifyDoesNotInjectViscousVelocity") {
    VolumeFluidSettings settings;
    settings.gravity = glm::vec3(0.f);
    auto result      = VolumeFluid::create(settings);
    REQUIRE(result.ok());
    auto                solver = std::move(result).takeValue();
    VolumeFluidCollider collider;
    collider.solidify = true;
    REQUIRE(solver->setColliders(std::span(&collider, 1)).ok());
    VolumeFluidParticle particles[2];
    particles[0].position = {0.f, 1.25f, 0.f};
    particles[1].position = {0.f, 1.44f, 0.f};
    for (auto& particle : particles) {
        particle.material.viscosity = 50.f;
        particle.material.cohesion  = 0.f;
    }
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    const auto state = solver->particles();
    REQUIRE(state[0].material.phase == VolumeFluidPhase::Solid);
    REQUIRE(state[1].material.phase == VolumeFluidPhase::Liquid);
    REQUIRE(glm::length(state[1].velocity) < 1e-6f);
}

TEST_CASE("fluids.volume.viscosityDampsShearWithoutBulkDrag") {
    const auto run = [&](float viscosity) {
        VolumeFluidSettings settings;
        settings.gravity = glm::vec3(0.f);
        auto result      = VolumeFluid::create(settings);
        REQUIRE(result.ok());
        auto                solver = std::move(result).takeValue();
        VolumeFluidParticle particles[2];
        particles[0].position = {-0.06f, 1.f, 0.f};
        particles[1].position = {0.06f, 1.f, 0.f};
        particles[0].velocity = {0.f, 0.04f, 0.1f};
        particles[1].velocity = {0.f, -0.04f, 0.1f};
        for (auto& particle : particles) {
            particle.material.viscosity = viscosity;
            particle.material.cohesion  = 0.f;
        }
        REQUIRE(solver->emit(particles).ok());
        for (int tick = 0; tick < 60; ++tick) REQUIRE(solver->step(1.f / 120.f, 1).ok());
        const auto state = solver->particles();
        const auto mean  = (state[0].velocity + state[1].velocity) * 0.5f;
        // Viscosity damps relative motion, not the common translation.
        REQUIRE(glm::length(mean - glm::vec3(0.f, 0.f, 0.1f)) < 1e-4f);
        return glm::length(state[0].velocity - state[1].velocity);
    };
    const float inviscid = run(0.f);
    const float medium   = run(10.f);
    const float viscous  = run(100.f);
    REQUIRE(std::abs(inviscid - 0.08f) < 1e-4f);
    REQUIRE(medium < inviscid * 0.95f);
    REQUIRE(viscous < medium * 0.5f);
}
}  // namespace test_fluids_volume
#endif


#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fluids_volume_part2)
#include <cmath>
#include <glm/geometric.hpp>
#include <limits>
#include "PrivateTestScope.h"
#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidCodec.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// fluids_volume_part2.cpp
namespace test_fluids_volume_part2 {
using namespace eve::fluids;

static eve::Value withoutSdfColliders(const eve::Value& snapshot) {
    eve::Value legacy(eve::Value::Object{});
    for (const auto& key : snapshot.keys()) {
        if (key == "sdfColliders" || key == "heightFieldColliders" || key == "stitches") continue;
        if (key != "colliders") {
            legacy.set(key, *snapshot.find(key));
            continue;
        }
        eve::Value::Array colliders;
        const auto*       source = snapshot.find(key);
        for (size_t i = 0; i < source->arraySize(); ++i) {
            eve::Value collider(eve::Value::Object{});
            for (const auto& field : source->at(i).keys())
                if (field != "isTrigger" && field != "staticFriction" && field != "rollingFriction" &&
                    field != "stickiness" && field != "stickDistance" && field != "frictionCombine" &&
                    field != "stickinessCombine" && field != "rollingContacts")
                    collider.set(field, *source->at(i).find(field));
            colliders.push_back(std::move(collider));
        }
        legacy.set(key, eve::Value(std::move(colliders)));
    }
    return legacy;
}

TEST_CASE("fluids.volume.multiphaseViscosityConservesMomentum") {
    VolumeFluidSettings settings;
    settings.gravity = glm::vec3(0.f);
    auto result      = VolumeFluid::create(settings);
    REQUIRE(result.ok());
    auto                solver = std::move(result).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position   = {-0.14f, 1.f, 0.f};
    particles[1].position   = {0.f, 1.f, 0.f};
    particles[2].position   = {0.1f, 1.f, 0.f};
    const float density[]   = {500.f, 2000.f, 1000.f};
    const float viscosity[] = {0.f, 30.f, 100.f};
    const float shear[]     = {0.04f, -0.02f, 0.02f};
    glm::vec3   initialMomentum(0.f);
    float       initialEnergy = 0.f;
    for (int i = 0; i < 3; ++i) {
        particles[i].material.density   = density[i];
        particles[i].material.viscosity = viscosity[i];
        particles[i].material.yield     = float(i) * 5.f;
        particles[i].material.cohesion  = 0.f;
        particles[i].velocity           = {0.f, shear[i], 0.1f};
        const float mass                = density[i] * 0.001f;
        initialMomentum += mass * particles[i].velocity;
        initialEnergy += 0.5f * mass * glm::dot(particles[i].velocity, particles[i].velocity);
    }
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    glm::vec3 momentum(0.f);
    float     energy = 0.f;
    for (const auto& particle : solver->particles()) {
        const float mass = particle.material.density * 0.001f;
        momentum += mass * particle.velocity;
        energy += 0.5f * mass * glm::dot(particle.velocity, particle.velocity);
    }
    REQUIRE(glm::length(momentum - initialMomentum) < 1e-5f);
    REQUIRE(energy < initialEnergy);
}

TEST_CASE("fluids.volume.orientedBoxQuery") {
    auto result = VolumeFluid::create({});
    REQUIRE(result.ok());
    auto                solver = std::move(result).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position = {0.2f, 1.f, 0.f};
    particles[1].position = {0.f, 1.2f, 0.f};
    particles[2].position = {0.25f, 1.f, 0.f};
    REQUIRE(solver->emit(particles).ok());
    auto aligned = solver->overlapBox({0.f, 1.f, 0.f}, {0.25f, 0.05f, 0.05f}, {0.f, 0.f, 0.f, 1.f});
    REQUIRE(aligned.ok());
    REQUIRE(aligned.value().size() == 2);
    REQUIRE(aligned.value()[1].position.x == 0.25f);
    auto rotated = solver->overlapBox({0.f, 1.f, 0.f}, {0.25f, 0.05f, 0.05f}, {0.f, 0.f, 0.70710678f, 0.70710678f});
    REQUIRE(rotated.ok());
    REQUIRE(rotated.value().size() == 1);
    REQUIRE(rotated.value()[0].position.y == 1.2f);
    auto point = solver->overlapBox(particles[0].position, glm::vec3(0.f), {0.f, 0.f, 0.f, 1.f});
    REQUIRE(point.ok());
    REQUIRE(point.value().size() == 1);
    REQUIRE(!solver->overlapBox({}, {-1.f, 1.f, 1.f}, {0.f, 0.f, 0.f, 1.f}).ok());
    REQUIRE(!solver->overlapBox({}, {1.f, 1.f, 1.f}, {0.f, 0.f, 0.f, 2.f}).ok());
    REQUIRE(
        !solver->overlapBox({std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f}, {1.f, 1.f, 1.f}, {0.f, 0.f, 0.f, 1.f})
             .ok());
    REQUIRE(solver->particleCount() == 3);
}

TEST_CASE("fluids.volume.diffusionConservesConcentration") {
    VolumeFluidSettings settings;
    settings.gravity = glm::vec3(0.f);
    auto result      = VolumeFluid::create(settings);
    REQUIRE(result.ok());
    auto                solver = std::move(result).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position = {-0.14f, 1.f, 0.f};
    particles[1].position = {0.f, 1.f, 0.f};
    particles[2].position = {0.1f, 1.f, 0.f};
    glm::vec4 initialColor(0.f), initialData(0.f);
    for (int i = 0; i < 3; ++i) {
        particles[i].material.diffusion = float(i) * 50.f;
        particles[i].material.cohesion  = 0.f;
        particles[i].color              = glm::vec4(0.f, 0.f, 0.f, 1.f);
        particles[i].color[i]           = 1.f;
        particles[i].data               = glm::vec4(float(i), float(i * i), -float(i), 0.5f);
        initialColor += particles[i].color;
        initialData += particles[i].data;
    }
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    glm::vec4 color(0.f), data(0.f);
    for (const auto& particle : solver->particles()) {
        color += particle.color;
        data += particle.data;
        REQUIRE(glm::all(glm::greaterThanEqual(particle.color, glm::vec4(0.f))));
        REQUIRE(glm::all(glm::lessThanEqual(particle.color, glm::vec4(1.f))));
    }
    REQUIRE(glm::length(color - initialColor) < 1e-6f);
    REQUIRE(glm::length(data - initialData) < 1e-6f);
    REQUIRE(solver->particles()[1].color.g < 1.f);
}

TEST_CASE("fluids.volume.materialChannelsMapAtomically") {
    auto result = VolumeFluid::create({});
    REQUIRE(result.ok());
    auto                solver = std::move(result).takeValue();
    VolumeFluidParticle particles[2];
    particles[0].position = {-0.1f, 1.f, 0.f};
    particles[1].position = {0.1f, 1.f, 0.f};
    particles[0].data     = {30.f, 1.5f, -3.f, 7.f};
    particles[1].data     = {-1.f, 0.5f, 0.f, 0.f};
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(!solver->applyMaterialChannels().ok());
    REQUIRE(solver->particles()[0].material.viscosity == particles[0].material.viscosity);
    solver->clear();
    particles[1].data.x = 100.f;
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->applyMaterialChannels().ok());
    const auto state = solver->particles();
    REQUIRE(state[0].material.viscosity == 30.f);
    REQUIRE(state[0].material.cohesion == 1.5f);
    REQUIRE(state[1].material.viscosity == 100.f);
    REQUIRE(state[1].material.cohesion == 0.5f);
    REQUIRE(state[0].data == particles[0].data);
    REQUIRE(state[0].position == particles[0].position);
}

TEST_CASE("fluids.volume.thermalContactIsBoundedAndIterationIndependent") {
    const auto run = [&](unsigned iterations, float rate) {
        VolumeFluidSettings settings;
        settings.iterations = iterations;
        auto result         = VolumeFluid::create(settings);
        REQUIRE(result.ok());
        auto                solver = std::move(result).takeValue();
        VolumeFluidCollider collider;
        collider.label = 17;
        REQUIRE(solver->setColliders(std::span(&collider, 1)).ok());
        VolumeFluidParticle particles[2];
        particles[0].position = {0.f, 1.25f, 0.f};
        particles[1].position = {0.8f, 1.25f, 0.f};
        particles[0].data = particles[1].data = {5.f, 1.f, 7.f, -2.f};
        REQUIRE(solver->emit(particles).ok());
        VolumeFluidThermalRule rule;
        rule.colliderLabel = 17;
        rule.rate          = rate;
        REQUIRE(solver->stepWithThermalContacts(1.f / 60.f, 4, std::span(&rule, 1)).ok());
        REQUIRE(!solver->contacts().empty());
        const auto state = solver->particles();
        REQUIRE(state[1].data == particles[1].data);
        REQUIRE(state[0].data.z == 7.f);
        REQUIRE(state[0].data.w == -2.f);
        REQUIRE(state[0].material.viscosity == particles[0].material.viscosity);
        return state[0].data;
    };
    const auto hot = run(1, -6.f);
    REQUIRE(glm::length(hot - glm::vec4(4.9f, 0.9f, 7.f, -2.f)) < 1e-6f);
    REQUIRE(glm::length(run(10, -6.f) - hot) < 1e-6f);
    REQUIRE(glm::length(run(5, 6.f) - glm::vec4(5.1f, 1.1f, 7.f, -2.f)) < 1e-6f);
    REQUIRE(glm::length(run(5, -1000000.f) - glm::vec4(0.05f, 0.5f, 7.f, -2.f)) < 1e-6f);
    REQUIRE(glm::length(run(5, 1000000.f) - glm::vec4(10.f, 2.f, 7.f, -2.f)) < 1e-6f);
}

TEST_CASE("fluids.volume.thermalRuleRejectsBeforeStepping") {
    auto result = VolumeFluid::create({});
    REQUIRE(result.ok());
    auto                solver = std::move(result).takeValue();
    VolumeFluidParticle particle;
    particle.position = {0.f, 1.f, 0.f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    VolumeFluidThermalRule rule;
    rule.colliderLabel = 42;
    REQUIRE(!solver->stepWithThermalContacts(1.f / 60.f, 4, std::span(&rule, 1)).ok());
    REQUIRE(solver->particles()[0].position == particle.position);
    VolumeFluidCollider collider;
    collider.label = 42;
    REQUIRE(solver->setColliders(std::span(&collider, 1)).ok());
    rule.minimumViscosity = 20.f;
    REQUIRE(!solver->stepWithThermalContacts(1.f / 60.f, 4, std::span(&rule, 1)).ok());
    REQUIRE(solver->particles()[0].position == particle.position);
    rule.minimumViscosity                     = 0.05f;
    const VolumeFluidThermalRule duplicates[] = {rule, rule};
    REQUIRE(!solver->stepWithThermalContacts(1.f / 60.f, 4, duplicates).ok());
    REQUIRE(solver->particles()[0].position == particle.position);
}

TEST_CASE("fluids.volume.snapshotStrictAndAtomic") {
    auto result = VolumeFluid::create({});
    REQUIRE(result.ok());
    auto                solver = std::move(result).takeValue();
    VolumeFluidParticle p;
    p.position = {0.f, 1.f, 0.f};
    REQUIRE(solver->emit(std::span(&p, 1)).ok());
    auto original = solver->snapshot();
    auto encoded  = encodeVolumeFluid(original);
    auto json     = encoded.toJson();
    REQUIRE(json.ok());
    auto parsed = eve::Value::fromJson(json.value());
    REQUIRE(parsed.ok());
    auto decoded = decodeVolumeFluid(parsed.value());
    REQUIRE(decoded.ok());
    solver->clear();
    REQUIRE(solver->restore(decoded.value()).ok());
    REQUIRE(solver->particles().size() == 1);
    auto bad    = original;
    bad.version = 99;
    REQUIRE(!solver->restore(bad).ok());
    REQUIRE(solver->particles()[0].position.y == 1.f);
    encoded.set("unexpected", 3);
    REQUIRE(!decodeVolumeFluid(encoded).ok());
    bad                               = original;
    bad.particles[0].material.density = -1.f;
    REQUIRE(!solver->restore(bad).ok());
    REQUIRE(solver->particles()[0].position.y == 1.f);
}

TEST_CASE("fluids.volume.densityLayerInversion") {
    VolumeFluidSettings settings;
    settings.minimum = {-0.3f, 0.f, -0.3f};
    settings.maximum = {0.3f, 1.4f, 0.3f};
    auto result      = VolumeFluid::create(settings);
    REQUIRE(result.ok());
    auto                             solver = std::move(result).takeValue();
    std::vector<VolumeFluidParticle> batch;
    for (int z = 0; z < 5; ++z)
        for (int y = 0; y < 10; ++y)
            for (int x = 0; x < 5; ++x) {
                VolumeFluidParticle p;
                // A resolved perturbation seeds the unstable interface; an exactly flat
                // interface is an equilibrium even though it is physically unstable.
                p.position = {-0.2f + x * 0.1f, 0.071f + y * 0.1f + 0.015f * std::sin(float(x + z)), -0.2f + z * 0.1f};
                p.position.x += 0.015f * std::sin(float(y + z));
                p.position.z += 0.015f * std::cos(float(x + y));
                p.material.density   = y < 5 ? 700.f : 1300.f;
                p.material.cohesion  = 0.f;
                p.material.viscosity = 0.05f;
                batch.push_back(p);
            }
    REQUIRE(solver->emit(batch).ok());
    for (int i = 0; i < 360; ++i) REQUIRE(solver->step(1.f / 60.f).ok());
    float light = 0.f, heavy = 0.f;
    for (const auto& p : solver->particles()) {
        if (p.material.density < 1000.f)
            light += p.position.y;
        else
            heavy += p.position.y;
    }
    REQUIRE(heavy < light);
}

TEST_CASE("fluids.volume.coincidentLiquidSamplesSeparate") {
    VolumeFluidSettings settings;
    settings.gravity = {0, 0, 0};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle p;
    p.position                       = {0, 1, 0};
    p.material.cohesion              = 0;
    p.material.viscosity             = 0;
    const VolumeFluidParticle pair[] = {p, p};
    REQUIRE(solver->emit(pair).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    const auto particles = solver->particles();
    REQUIRE(glm::length(particles[0].position - particles[1].position) >= .04999f);
    REQUIRE(glm::length((particles[0].position + particles[1].position) * .5f - p.position) < 1e-6f);
}

TEST_CASE("fluids.volume.unequalMassSeparationPreservesCenter") {
    VolumeFluidSettings settings;
    settings.gravity = {0, 0, 0};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle a, b;
    a.position = b.position = {0, 1, 0};
    a.material.density      = 700.f;
    b.material.density      = 1300.f;
    a.material.cohesion = b.material.cohesion = 0;
    a.material.viscosity = b.material.viscosity = 0;
    const VolumeFluidParticle pair[]            = {a, b};
    REQUIRE(solver->emit(pair).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    const auto p      = solver->particles();
    const auto center = (p[0].position * 700.f + p[1].position * 1300.f) / 2000.f;
    REQUIRE(glm::length(center - a.position) < 1e-6f);
    REQUIRE(glm::length(p[0].position - p[1].position) >= .0499f);
}

TEST_CASE("fluids.volume.stableDensityLayersRemainOrdered") {
    VolumeFluidSettings settings;
    settings.minimum = {-.3f, 0, -.3f};
    settings.maximum = {.3f, 1.4f, .3f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                             solver = std::move(created).takeValue();
    std::vector<VolumeFluidParticle> batch;
    for (int z = 0; z < 5; ++z)
        for (int y = 0; y < 10; ++y)
            for (int x = 0; x < 5; ++x) {
                VolumeFluidParticle p;
                p.position           = {-.2f + x * .1f + .015f * std::sin(float(y + z)),
                                        .071f + y * .1f + .015f * std::sin(float(x + z)),
                                        -.2f + z * .1f + .015f * std::cos(float(x + y))};
                p.material.density   = y < 5 ? 1300.f : 700.f;
                p.material.cohesion  = 0;
                p.material.viscosity = .05f;
                batch.push_back(p);
            }
    REQUIRE(solver->emit(batch).ok());
    for (int i = 0; i < 360; ++i) REQUIRE(solver->step(1.f / 60.f).ok());
    float heavy = 0, light = 0;
    for (const auto& p : solver->particles()) (p.material.density > 1000 ? heavy : light) += p.position.y;
    REQUIRE(heavy < light);
}

TEST_CASE("fluids.volume.rotatedBoxAndAngularSurfaceVelocity") {
    VolumeFluidSettings settings;
    settings.gravity = {0, 0, 0};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidCollider box;
    box.shape      = VolumeFluidColliderShape::Box;
    box.center     = {0, 1, 0};
    box.halfExtent = {.4f, .05f, .1f};
    box.rotation   = {0, 0, .70710678118f, .70710678118f};
    box.friction   = 0;
    REQUIRE(solver->setColliders(std::span(&box, 1)).ok());
    VolumeFluidParticle p;
    p.position           = {.06f, 1.2f, 0};
    p.material.cohesion  = 0;
    p.material.viscosity = 0;
    REQUIRE(solver->emit(std::span(&p, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particles()[0].position.x >= .09999f);
    REQUIRE(!solver->contacts().empty());
    REQUIRE(solver->contacts()[0].normal.x > .99f);
    solver->clear();
    box.shape           = VolumeFluidColliderShape::Sphere;
    box.radius          = .2f;
    box.angularVelocity = {0, 0, 4};
    box.friction        = 1;
    REQUIRE(solver->setColliders(std::span(&box, 1)).ok());
    p.position = {.245f, 1, 0};
    REQUIRE(solver->emit(std::span(&p, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particles()[0].velocity.y > 0.f);
    float oppositeImpulse = 0;
    for (const auto& contact : solver->contacts()) oppositeImpulse += contact.impulse.y;
    REQUIRE(oppositeImpulse < 0.f);
}

TEST_CASE("fluids.volume.colliderPoseRejectsAtomically") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidCollider box;
    box.label = 17;
    REQUIRE(solver->setColliders(std::span(&box, 1)).ok());
    box.rotation = {0, 0, 0, 0};
    REQUIRE(!solver->setColliders(std::span(&box, 1)).ok());
    REQUIRE(solver->snapshot().colliders[0].rotation.w == 1.f);
}

TEST_CASE("fluids.volume.colliderVersionOneMigration") {
    VolumeFluidSnapshot state;
    state.colliders.push_back(VolumeFluidCollider{});
    auto        encoded = encodeVolumeFluid(state);
    const auto& current = encoded.find("colliders")->at(0);
    eve::Value  oldCollider(eve::Value::Object{});
    for (const auto& key : current.keys())
        if (key != "rotation" && key != "angularVelocity" && key != "collisionFilter" && key != "isTrigger" &&
            key != "staticFriction" && key != "rollingFriction" && key != "stickiness" && key != "stickDistance" &&
            key != "frictionCombine" && key != "stickinessCombine" && key != "rollingContacts")
            oldCollider.set(key, *current.find(key));
    eve::Value::Array colliders;
    colliders.push_back(oldCollider);
    eve::Value legacy(eve::Value::Object{});
    for (const auto& key : encoded.keys())
        if (key != "attachments" && key != "simplexes") legacy.set(key, *encoded.find(key));
    encoded = std::move(legacy);
    encoded = withoutSdfColliders(encoded);
    encoded.set("version", 1);
    encoded.set("colliders", eve::Value(std::move(colliders)));
    auto migrated = decodeVolumeFluid(encoded);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().version == 20);
    REQUIRE(migrated.value().attachments.empty());
    REQUIRE(migrated.value().colliders[0].rotation.w == 1.f);
    REQUIRE(glm::length(migrated.value().colliders[0].angularVelocity) == 0.f);
}

TEST_CASE("fluids.volume.failedCoupledStepRestoresSamples") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidCollider original;
    original.label = 7;
    REQUIRE(solver->setColliders(std::span(&original, 1)).ok());
    VolumeFluidCollider replacement;
    replacement.label = 9;
    REQUIRE(!solver->stepWithColliders(-1.f, 4, std::span(&replacement, 1)).ok());
    REQUIRE(solver->snapshot().colliders.size() == 1);
    REQUIRE(solver->snapshot().colliders[0].label == 7);
}

TEST_CASE("fluids.volume.thermalCoupledStepRestoresSamples") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidCollider original;
    original.label = 7;
    REQUIRE(solver->setColliders(std::span(&original, 1)).ok());
    VolumeFluidParticle particle;
    particle.position = {0, 1.25f, 0};
    particle.data     = {5, 1, 7, -2};
    particle.life     = 1;
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    VolumeFluidCollider replacement;
    replacement.label = 9;
    VolumeFluidThermalRule rule;
    rule.colliderLabel = 7;
    rule.rate          = -6;
    REQUIRE(!solver->stepWithColliders(1.f / 60.f, 4, std::span(&replacement, 1), std::span(&rule, 1)).ok());
    REQUIRE(solver->snapshot().colliders[0].label == 7);
    REQUIRE(solver->particles()[0].position.y == particle.position.y);
    REQUIRE(solver->particles()[0].data.x == 5.f);
    REQUIRE(solver->particles()[0].life == 1.f);
    rule.colliderLabel = 9;
    REQUIRE(solver->stepWithColliders(1.f / 60.f, 4, std::span(&replacement, 1), std::span(&rule, 1)).ok());
    REQUIRE(std::abs(solver->particles()[0].data.x - 4.9f) < 1e-5f);
    REQUIRE(std::abs(solver->particles()[0].life - (1.f - 1.f / 60.f)) < 1e-6f);
    REQUIRE(solver->snapshot().colliders[0].label == 9);
}

TEST_CASE("fluids.volume.viscosityGradientOrderAndAtomicity") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                             solver = std::move(created).takeValue();
    std::vector<VolumeFluidParticle> particles(5);
    const float                      viscosity[] = {0, 2, 4, 6, 100};
    for (size_t i = 0; i < particles.size(); ++i) {
        particles[i].position           = {0, 1, 0};
        particles[i].material.viscosity = viscosity[i];
        particles[i].data               = {8, 1, 7, -2};
    }
    REQUIRE(solver->emit(particles).ok());
    const VolumeFluidViscosityColorKey keys[] = {{2, {1, 0, 0, .2f}}, {6, {0, 0, 1, .8f}}, {10, {0, 1, 0, 1}}};
    REQUIRE(solver->applyMaterialChannels(keys).ok());
    auto mapped = solver->particles();
    REQUIRE(mapped[0].color.x == 1.f);
    REQUIRE(mapped[1].color.x == 1.f);
    REQUIRE(std::abs(mapped[2].color.x - .5f) < 1e-6f);
    REQUIRE(std::abs(mapped[2].color.w - .5f) < 1e-6f);
    REQUIRE(mapped[3].color.z == 1.f);
    REQUIRE(mapped[4].color.y == 1.f);
    for (const auto& p : mapped) {
        REQUIRE(p.material.viscosity == 8.f);
        REQUIRE(p.data.z == 7.f);
    }
    auto invalidKeys         = std::vector<VolumeFluidViscosityColorKey>(std::begin(keys), std::end(keys));
    invalidKeys[1].viscosity = 2;
    REQUIRE(!solver->applyMaterialChannels(invalidKeys).ok());
    REQUIRE(solver->particles()[0].color.x == 1.f);
    invalidKeys[1]         = keys[1];
    invalidKeys[2].color.w = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!solver->applyMaterialChannels(invalidKeys).ok());
    REQUIRE(solver->particles()[0].color.x == 1.f);
    auto snapshot                    = solver->snapshot();
    snapshot.particles.back().data.y = -1;
    REQUIRE(solver->restore(snapshot).ok());
    REQUIRE(!solver->applyMaterialChannels(keys).ok());
    REQUIRE(solver->particles()[0].color.x == 1.f);
    REQUIRE(solver->particles()[0].material.viscosity == 8.f);
}

TEST_CASE("fluids.volume.viscosityGradientStrictCodec") {
    eve::Value key(eve::Value::Object{});
    key.set("viscosity", 2.f);
    key.set("color", eve::Value(eve::Value::Array{1.f, 0.f, 0.f, 1.f}));
    auto value = eve::Value(eve::Value::Array{key, key});
    auto valid = decodeVolumeFluidViscosityColors(value);
    REQUIRE(valid.ok());
    REQUIRE(valid.value().size() == 2);
    key.set("extra", 1);
    REQUIRE(!decodeVolumeFluidViscosityColors(eve::Value(eve::Value::Array{key})).ok());
    REQUIRE(!decodeVolumeFluidViscosityColors(eve::Value(eve::Value::Array(33, key))).ok());
}

TEST_CASE("fluids.volume.solidAttachmentMovesRotatesAndDetaches") {
    VolumeFluidSettings settings;
    settings.gravity = {0, 0, 0};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle p;
    p.position = {0, 1.25f, 0};
    VolumeFluidCollider c;
    c.label    = 17;
    c.solidify = true;
    REQUIRE(solver->setColliders(std::span(&c, 1)).ok());
    REQUIRE(solver->emit(std::span(&p, 1)).ok());
    REQUIRE(solver->step(1.f / 60.f).ok());
    auto frozen = solver->snapshot();
    REQUIRE(frozen.attachments.size() == 1);
    REQUIRE(frozen.attachments[0].particleIndex == 0);
    REQUIRE(frozen.attachments[0].colliderLabel == 17);
    c.center.x = .2f;
    c.rotation = {0, 0, .70710678118f, .70710678118f};
    REQUIRE(solver->stepWithColliders(1.f / 60.f, 4, std::span(&c, 1)).ok());
    const auto      local    = frozen.attachments[0].localPosition;
    const glm::vec3 expected = c.center + glm::vec3(-local.y, local.x, local.z);
    REQUIRE(glm::length(solver->particles()[0].position - expected) < 1e-5f);
    REQUIRE(glm::length(solver->particles()[0].velocity - (expected - frozen.particles[0].position) * 60.f) < 1e-4f);
    REQUIRE(std::abs(solver->particles()[0].orientation.z - .70710678f) < 1e-5f);
    REQUIRE(std::abs(solver->particles()[0].orientation.w - .70710678f) < 1e-5f);
    auto decoded = decodeVolumeFluid(encodeVolumeFluid(solver->snapshot()));
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().attachments.size() == 1);
    REQUIRE(solver->setColliders({}).ok());
    REQUIRE(solver->snapshot().attachments.empty());
    c.center.x = .4f;
    REQUIRE(solver->stepWithColliders(1.f / 60.f, 4, std::span(&c, 1)).ok());
    REQUIRE(glm::length(solver->particles()[0].position - expected) < 1e-5f);
    REQUIRE(glm::length(solver->particles()[0].velocity) == 0.f);
    REQUIRE(solver->restore(decoded.value()).ok());
    c.solidify = false;
    REQUIRE(solver->setColliders(std::span(&c, 1)).ok());
    REQUIRE(solver->paint(expected, .001f, VolumeFluidMaterial{}).ok());
    REQUIRE(solver->snapshot().attachments.empty());
}

TEST_CASE("fluids.volume.addRandomVelocityUsesOneDeterministicActorImpulse") {
    auto makeSolver = [&] {
        auto created = VolumeFluid::create({});
        REQUIRE(created.ok());
        auto                solver = std::move(created).takeValue();
        VolumeFluidParticle particles[2];
        particles[0].position = {-.2f, 1.f, 0.f};
        particles[0].velocity = {1.f, 2.f, 3.f};
        particles[1].position = {.2f, 1.f, 0.f};
        particles[1].velocity = {-2.f, 1.f, .5f};
        REQUIRE(solver->emit(particles).ok());
        return solver;
    };
    auto       first = makeSolver(), replay = makeSolver();
    const auto before = first->particles();
    REQUIRE(first->addRandomVelocity(5.f, 73).ok());
    REQUIRE(replay->addRandomVelocity(5.f, 73).ok());
    const auto      after  = first->particles();
    const glm::vec3 delta0 = after[0].velocity - before[0].velocity;
    const glm::vec3 delta1 = after[1].velocity - before[1].velocity;
    CHECK(glm::length(delta0 - delta1) < 1e-6f);
    CHECK(std::abs(glm::length(delta0) - 5.f) < 1e-5f);
    CHECK(after[0].velocity == replay->particles()[0].velocity);
    const auto stable = first->snapshot();
    REQUIRE(!first->addRandomVelocity(-1.f, 73).ok());
    CHECK(first->snapshot().particles[0].velocity == stable.particles[0].velocity);

    auto overflow               = makeSolver();
    auto state                  = overflow->snapshot();
    state.particles[1].velocity = glm::normalize(delta0) * 99.f;
    REQUIRE(overflow->restore(state).ok());
    REQUIRE(!overflow->addRandomVelocity(2.f, 73).ok());
    CHECK(overflow->particles()[0].velocity == state.particles[0].velocity);
    CHECK(overflow->particles()[1].velocity == state.particles[1].velocity);
}

TEST_CASE("fluids.volume.teleportActorTransformsPoseZerosMotionAndRejectsAtomically") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[2];
    particles[0].position        = {-.2f, 1.f, 0.f};
    particles[0].velocity        = {1.f, 2.f, 3.f};
    particles[0].angularVelocity = {.5f, 0.f, 0.f};
    particles[1].position        = {.2f, 1.f, 0.f};
    particles[1].velocity        = {-1.f, 0.f, 2.f};
    REQUIRE(solver->emit(particles).ok());
    const float half = std::sqrt(.5f);
    REQUIRE(solver->teleportActor({0.f, 1.f, 0.f}, {0.f, 0.f, 0.f, 1.f}, {1.f, 1.f, 0.f}, {0.f, 0.f, half, half}).ok());
    const auto transformed = solver->particles();
    CHECK(glm::length(transformed[0].position - glm::vec3(1.f, .8f, 0.f)) < 1e-5f);
    CHECK(glm::length(transformed[1].position - glm::vec3(1.f, 1.2f, 0.f)) < 1e-5f);
    CHECK(glm::length(transformed[0].velocity) == 0.f);
    CHECK(glm::length(transformed[0].angularVelocity) == 0.f);
    CHECK(std::abs(transformed[0].orientation.z - half) < 1e-5f);
    CHECK(std::abs(transformed[0].orientation.w - half) < 1e-5f);

    const auto stable = solver->snapshot();
    REQUIRE(
        !solver->teleportActor({1.f, 1.f, 0.f}, {0.f, 0.f, half, half}, {3.f, 1.f, 0.f}, {0.f, 0.f, 0.f, 1.f}).ok());
    CHECK(solver->snapshot().particles[0].position == stable.particles[0].position);
    REQUIRE(!solver->teleportActor({}, {0.f, 0.f, 0.f, 0.f}, {}, {0.f, 0.f, 0.f, 1.f}).ok());
    CHECK(solver->snapshot().particles[1].position == stable.particles[1].position);
}

TEST_CASE("fluids.volume.referenceVelocityAndActorGroupColorizers") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position   = {0, 1, 0};
    particles[0].velocity   = {-.2f, 0, .2f};
    particles[0].actorGroup = 0;
    particles[1].position   = {.2f, 1, 0};
    particles[1].velocity   = {-.4f, .4f, 0};
    particles[1].actorGroup = 1;
    particles[2].position   = {.4f, 1, 0};
    particles[2].velocity   = {0, 0, 0};
    particles[2].actorGroup = 26;
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->applyVelocityColors(.2f).ok());
    const auto velocityColors = solver->particles();
    REQUIRE(glm::length(velocityColors[0].color - glm::vec4(0, .5f, 1, 1)) < 1e-6f);
    REQUIRE(glm::length(velocityColors[1].color - glm::vec4(0, 1, .5f, 1)) < 1e-6f);
    REQUIRE(!solver->applyVelocityColors(0).ok());
    REQUIRE(solver->particles()[0].color == velocityColors[0].color);
    REQUIRE(solver->applyActorGroupColors().ok());
    const auto groupColors = solver->particles();
    REQUIRE(glm::length(groupColors[0].color - glm::vec4(240 / 255.f, 163 / 255.f, 1, 1)) < 1e-6f);
    REQUIRE(glm::length(groupColors[1].color - glm::vec4(0, 117 / 255.f, 220 / 255.f, 1)) < 1e-6f);
    REQUIRE(groupColors[2].color == groupColors[0].color);
    REQUIRE(groupColors[0].velocity == particles[0].velocity);
}

TEST_CASE("fluids.volume.referenceDataAndSeededRandomColorizers") {
    auto ar = VolumeFluid::create({}), br = VolumeFluid::create({});
    REQUIRE(ar.ok());
    REQUIRE(br.ok());
    auto                a = std::move(ar).takeValue(), b = std::move(br).takeValue();
    VolumeFluidParticle particles[3];
    for (int i = 0; i < 3; ++i) {
        particles[i].position = {float(i) * .2f, 1, 0};
        particles[i].data.x   = float(i) * .5f;
    }
    REQUIRE(a->emit(particles).ok());
    REQUIRE(b->emit(particles).ok());
    const VolumeFluidColorKey gradient[] = {{0, {1, 0, 0, 1}}, {1, {0, 0, 1, 1}}};
    REQUIRE(a->applyDataColors(0, gradient).ok());
    auto colors = a->particles();
    REQUIRE(colors[0].color == glm::vec4(1, 0, 0, 1));
    REQUIRE(colors[1].color == glm::vec4(.5f, 0, .5f, 1));
    REQUIRE(colors[2].color == glm::vec4(0, 0, 1, 1));
    auto invalidGradient = std::array{gradient[1], gradient[0]};
    REQUIRE(!a->applyDataColors(0, invalidGradient).ok());
    REQUIRE(a->particles()[1].color == colors[1].color);
    REQUIRE(!a->applyDataColors(4, gradient).ok());
    REQUIRE(a->applyRandomColors(gradient, 91).ok());
    REQUIRE(b->applyRandomColors(gradient, 91).ok());
    for (size_t i = 0; i < 3; ++i) REQUIRE(a->particles()[i].color == b->particles()[i].color);
}

TEST_CASE("fluids.volume.rotatedCapsuleAndAngularSurfaceVelocity") {
    VolumeFluidSettings settings;
    settings.gravity = {0, 0, 0};
    settings.minimum = {-1, -1, -1};
    settings.maximum = {1, 1, 1};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidCollider capsule;
    capsule.label           = 81;
    capsule.shape           = VolumeFluidColliderShape::Capsule;
    capsule.center          = {0, 0, 0};
    capsule.radius          = .1f;
    capsule.halfExtent      = {.1f, .3f, .1f};
    capsule.rotation        = {0, 0, .70710678118f, .70710678118f};
    capsule.angularVelocity = {0, 0, 4};
    capsule.friction        = 1;
    REQUIRE(solver->setColliders(std::span(&capsule, 1)).ok());
    VolumeFluidParticle particle;
    particle.position           = {.15f, .11f, 0};
    particle.material.cohesion  = 0;
    particle.material.viscosity = 0;
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(!solver->contacts().empty());
    REQUIRE(solver->contacts()[0].colliderLabel == 81);
    REQUIRE(solver->contacts()[0].normal.y > .99f);
    REQUIRE(solver->particles()[0].position.y >= .1499f);
    REQUIRE(solver->particles()[0].velocity.y > 0.f);
    auto decoded = decodeVolumeFluid(encodeVolumeFluid(solver->snapshot()));
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().colliders[0].shape == VolumeFluidColliderShape::Capsule);
    auto invalid  = decoded.value().colliders[0];
    invalid.shape = static_cast<VolumeFluidColliderShape>(99);
    REQUIRE(!solver->setColliders(std::span(&invalid, 1)).ok());
    REQUIRE(solver->snapshot().colliders[0].shape == VolumeFluidColliderShape::Capsule);
}

TEST_CASE("fluids.volume.movingColliderPosesAreInterpolatedAcrossSubsteps") {
    VolumeFluidSettings settings;
    settings.gravity   = glm::vec3(0.f);
    settings.minimum   = {-1, -1, -1};
    settings.maximum   = {1, 1, 1};
    auto sphereCreated = VolumeFluid::create(settings);
    REQUIRE(sphereCreated.ok());
    auto                sphereSolver = std::move(sphereCreated).takeValue();
    VolumeFluidParticle particle;
    particle.position = glm::vec3(0.f);
    REQUIRE(sphereSolver->emit(std::span(&particle, 1)).ok());
    VolumeFluidCollider sphere;
    sphere.label    = 71;
    sphere.shape    = VolumeFluidColliderShape::Sphere;
    sphere.radius   = .05f;
    sphere.center   = {.16f, 0, 0};
    sphere.velocity = {9.6f, 0, 0};
    REQUIRE(sphereSolver->setColliders(std::span(&sphere, 1)).ok());
    REQUIRE(sphereSolver->step(1.f / 60.f, 2).ok());
    REQUIRE(!sphereSolver->contacts().empty());
    REQUIRE(sphereSolver->contacts().front().colliderLabel == 71);

    auto boxCreated = VolumeFluid::create(settings);
    REQUIRE(boxCreated.ok());
    auto boxSolver    = std::move(boxCreated).takeValue();
    particle.position = {.1f, .1f, 0};
    REQUIRE(boxSolver->emit(std::span(&particle, 1)).ok());
    VolumeFluidCollider box;
    box.label             = 72;
    box.shape             = VolumeFluidColliderShape::Box;
    box.center            = glm::vec3(0.f);
    box.halfExtent        = {.2f, .02f, .1f};
    const float halfAngle = .25f * 3.14159265359f;
    box.rotation          = {0, 0, std::sin(halfAngle), std::cos(halfAngle)};
    box.angularVelocity   = {0, 0, .5f * 3.14159265359f / (1.f / 60.f)};
    REQUIRE(boxSolver->setColliders(std::span(&box, 1)).ok());
    REQUIRE(boxSolver->step(1.f / 60.f, 2).ok());
    REQUIRE(!boxSolver->contacts().empty());
    REQUIRE(boxSolver->contacts().front().colliderLabel == 72);

    auto sdfCreated = VolumeFluid::create(settings);
    REQUIRE(sdfCreated.ok());
    auto sdfSolver    = std::move(sdfCreated).takeValue();
    particle.position = glm::vec3(0.f);
    REQUIRE(sdfSolver->emit(std::span(&particle, 1)).ok());
    VolumeFluidSdfCollider sdf;
    sdf.label    = 73;
    sdf.sdf      = MeshSdf::makeSphere(glm::vec3(0.f), .1f, {16, 16, 16});
    sdf.position = {.16f, 0, 0};
    sdf.velocity = {9.6f, 0, 0};
    sdf.friction = 0.f;
    REQUIRE(sdfSolver->setSdfColliders(std::span(&sdf, 1)).ok());
    const auto samples = sdfSolver->snapshot().sdfColliders[0].sdf.distances;
    REQUIRE(sdfSolver->step(1.f / 60.f, 2).ok());
    REQUIRE(!sdfSolver->contacts().empty());
    REQUIRE(sdfSolver->contacts().front().colliderLabel == 73);
    const auto after = sdfSolver->snapshot().sdfColliders[0];
    REQUIRE(after.position == sdf.position);
    REQUIRE(after.sdf.distances == samples);
}

TEST_CASE("fluids.volume.analyticColliderCandidateBudgetIsAtomic") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                             solver = std::move(created).takeValue();
    std::vector<VolumeFluidCollider> colliders(1024);
    for (size_t i = 0; i < colliders.size(); ++i) colliders[i].label = unsigned(i + 1);
    REQUIRE(solver->setColliders(colliders).ok());
    std::vector<VolumeFluidParticle> particles(3907);
    for (auto& particle : particles) particle.position = {0, 1, 0};
    REQUIRE(!solver->emit(particles).ok());
    REQUIRE(solver->particleCount() == 0);
}

TEST_CASE("fluids.volume.attachedSolidReturnsNeighborConstraintMomentum") {
    VolumeFluidSettings settings;
    settings.gravity    = {0, 0, 0};
    settings.iterations = 4;
    auto created        = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidSnapshot snapshot;
    snapshot.settings = settings;
    VolumeFluidParticle solid, liquid;
    solid.position          = {0, 1, 0};
    liquid.position         = {.08f, 1, 0};
    solid.material.phase    = VolumeFluidPhase::Solid;
    solid.material.cohesion = liquid.material.cohesion = 0.f;
    solid.material.viscosity = liquid.material.viscosity = 0.f;
    snapshot.particles                                   = {solid, liquid};
    VolumeFluidCollider collider;
    collider.label  = 17;
    collider.radius = .01f;
    snapshot.colliders.push_back(collider);
    snapshot.attachments.push_back({0, 17, {0, 0, 0}});
    REQUIRE(solver->restore(snapshot).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->contacts().empty());
    REQUIRE(solver->attachmentReactions().size() == 1);
    const auto  state    = solver->particles();
    const float mass     = settings.spacing * settings.spacing * settings.spacing * 1000.f;
    const auto  momentum = state[1].velocity * mass + solver->attachmentReactions()[0].impulse;
    REQUIRE(glm::length(momentum) < 1e-5f);
    REQUIRE(solver->attachmentReactions()[0].impulse.x < 0.f);
}

TEST_CASE("fluids.volume.attachedEllipsoidReturnsAngularInertia") {
    VolumeFluidSettings settings;
    settings.gravity = {0, 0, 0};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidSnapshot snapshot;
    snapshot.settings = settings;
    VolumeFluidParticle solid;
    solid.position       = {0, 1, 0};
    solid.material.phase = VolumeFluidPhase::Solid;
    solid.radii          = {.1f, .2f, .3f};
    snapshot.particles.push_back(solid);
    VolumeFluidCollider collider;
    collider.label           = 17;
    collider.radius          = .01f;
    collider.angularVelocity = {0, 0, 2};
    snapshot.colliders.push_back(collider);
    snapshot.attachments.push_back({0, 17, {0, 0, 0}});
    REQUIRE(solver->restore(snapshot).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->attachmentReactions().size() == 1);
    const auto& reaction = solver->attachmentReactions()[0];
    REQUIRE(glm::length(reaction.impulse) < 1e-7f);
    REQUIRE(std::abs(reaction.angularImpulse.z + .02f) < 1e-6f);
    REQUIRE(std::abs(solver->particles()[0].angularVelocity.z - 2.f) < 1e-6f);
}
}  // namespace test_fluids_volume_part2
#endif


#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fluids_volume_part3)
#include <cmath>
#include <glm/geometric.hpp>
#include <limits>
#include "PrivateTestScope.h"
#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidCodec.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// fluids_volume_part3.cpp
namespace test_fluids_volume_part3 {
using namespace eve::fluids;

static eve::Value withoutRecentMaterialFields(eve::Value particle) {
    const auto* current = particle.find("material");
    eve::Value  material(eve::Value::Object{});
    for (const auto& key : current->keys())
        if (key != "atmosphericPressure" && key != "smoothing" && key != "rollingContacts" &&
            key != "rollingFriction" && key != "dynamicFriction" && key != "staticFriction" && key != "stickiness" &&
            key != "stickDistance" && key != "frictionCombine" && key != "stickinessCombine")
            material.set(key, *current->find(key));
    eve::Value legacy(eve::Value::Object{});
    for (const auto& key : particle.keys())
        if (key != "angularVelocity") legacy.set(key, *particle.find(key));
    legacy.set("material", std::move(material));
    return legacy;
}
static eve::Value withoutSdfColliders(const eve::Value& snapshot) {
    eve::Value legacy(eve::Value::Object{});
    for (const auto& key : snapshot.keys()) {
        if (key == "sdfColliders" || key == "heightFieldColliders" || key == "stitches") continue;
        if (key != "colliders") {
            legacy.set(key, *snapshot.find(key));
            continue;
        }
        eve::Value::Array colliders;
        const auto*       source = snapshot.find(key);
        for (size_t i = 0; i < source->arraySize(); ++i) {
            eve::Value collider(eve::Value::Object{});
            for (const auto& field : source->at(i).keys())
                if (field != "isTrigger" && field != "staticFriction" && field != "rollingFriction" &&
                    field != "stickiness" && field != "stickDistance" && field != "frictionCombine" &&
                    field != "stickinessCombine" && field != "rollingContacts")
                    collider.set(field, *source->at(i).find(field));
            colliders.push_back(std::move(collider));
        }
        legacy.set(key, eve::Value(std::move(colliders)));
    }
    return legacy;
}

TEST_CASE("fluids.volume.ownedSdfColliderObstacleContainerAndCodec") {
    VolumeFluidSettings settings;
    settings.gravity    = {0, 0, 0};
    settings.iterations = 1;
    settings.minimum    = {-1.f, .2f, -.8f};
    settings.maximum    = {1.4f, 1.8f, .8f};
    auto created        = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                   solver = std::move(created).takeValue();
    VolumeFluidSdfCollider obstacle;
    obstacle.label    = 41;
    obstacle.sdf      = MeshSdf::makeSphere({0, 1, 0}, .5f, {24, 24, 24});
    obstacle.position = {.2f, 0, 0};
    obstacle.scale    = 1.5f;
    obstacle.friction = 0;
    REQUIRE(solver->setSdfColliders(std::span(&obstacle, 1)).ok());
    VolumeFluidParticle particle;
    particle.position           = {.9f, 1.5f, 0};
    particle.material.cohesion  = 0;
    particle.material.viscosity = 0;
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particles()[0].position.x > .98f);
    REQUIRE(!solver->contacts().empty());
    REQUIRE(solver->contacts()[0].colliderLabel == 41);
    auto encoded = encodeVolumeFluid(solver->snapshot());
    auto decoded = decodeVolumeFluid(encoded);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().version == 20);
    REQUIRE(decoded.value().sdfColliders.size() == 1);
    REQUIRE(decoded.value().sdfColliders[0].sdf.distances == obstacle.sdf.distances);

    VolumeFluidSdfPose pose;
    pose.label                 = 41;
    pose.position              = {.35f, .1f, 0};
    pose.scale                 = 1.25f;
    pose.velocity              = {.5f, 0, 0};
    pose.angularVelocity       = {0, 1, 0};
    const auto originalSamples = solver->snapshot().sdfColliders[0].sdf.distances;
    REQUIRE(solver->updateSdfColliderPoses(std::span(&pose, 1)).ok());
    const auto moved = solver->snapshot().sdfColliders[0];
    REQUIRE(moved.position == pose.position);
    REQUIRE(moved.scale == pose.scale);
    REQUIRE(moved.velocity == pose.velocity);
    REQUIRE(moved.angularVelocity == pose.angularVelocity);
    REQUIRE(moved.sdf.distances == originalSamples);
    auto              poseValue = encodeVolumeFluidSdfPose(pose);
    eve::Value::Array poseArray;
    poseArray.push_back(poseValue);
    auto decodedPoses = decodeVolumeFluidSdfPoses(eve::Value(std::move(poseArray)));
    REQUIRE(decodedPoses.ok());
    REQUIRE(decodedPoses.value().size() == 1);
    REQUIRE(decodedPoses.value()[0].position == pose.position);
    auto invalidPose  = pose;
    invalidPose.label = 999;
    REQUIRE(!solver->updateSdfColliderPoses(std::span(&invalidPose, 1)).ok());
    REQUIRE(solver->snapshot().sdfColliders[0].position == pose.position);

    auto invalid = obstacle;
    invalid.sdf.distances.pop_back();
    REQUIRE(!solver->setSdfColliders(std::span(&invalid, 1)).ok());
    REQUIRE(solver->snapshot().sdfColliders[0].label == 41);

    VolumeFluidSettings enclosedSettings = settings;
    enclosedSettings.minimum             = {-.7f, .3f, -.7f};
    enclosedSettings.maximum             = {.7f, 1.7f, .7f};
    auto enclosedCreated                 = VolumeFluid::create(enclosedSettings);
    REQUIRE(enclosedCreated.ok());
    auto                   enclosed = std::move(enclosedCreated).takeValue();
    VolumeFluidSdfCollider container;
    container.label    = 42;
    container.inverted = true;
    container.sdf      = MeshSdf::makeSphere({0, 1, 0}, .6f, {32, 32, 32});
    container.friction = 0;
    REQUIRE(enclosed->setSdfColliders(std::span(&container, 1)).ok());
    particle.position = {.62f, 1, 0};
    REQUIRE(enclosed->emit(std::span(&particle, 1)).ok());
    REQUIRE(enclosed->step(1.f / 120.f, 1).ok());
    REQUIRE(enclosed->particles()[0].position.x < .56f);
    container.sdf.origin.x = .5f;
    REQUIRE(!enclosed->setSdfColliders(std::span(&container, 1)).ok());
}

TEST_CASE("fluids.volume.raycastSortedFilteredAndBounded") {
    auto result = VolumeFluid::create({});
    REQUIRE(result.ok());
    auto                             solver = std::move(result).takeValue();
    std::vector<VolumeFluidParticle> particles(4);
    for (size_t i = 0; i < particles.size(); ++i) {
        particles[i].position       = {float(i) * .5f, 1.f, 0.f};
        particles[i].material.phase = VolumeFluidPhase(i);
    }
    REQUIRE(solver->emit(particles).ok());
    auto hits = solver->raycast({-1.f, 1.f, 0.f}, {5.f, 0.f, 0.f}, 2.1f, 4, 0x0f);
    REQUIRE(hits.ok());
    REQUIRE(hits.value().size() == 3);
    REQUIRE(hits.value()[0].particleIndex == 0);
    REQUIRE(std::abs(hits.value()[0].distance - .95f) < 1e-6f);
    REQUIRE(std::abs(hits.value()[0].point.x + .05f) < 1e-6f);
    REQUIRE(hits.value()[0].normal.x == -1.f);
    REQUIRE(hits.value()[1].particleIndex == 1);
    REQUIRE(hits.value()[2].particle.material.phase == VolumeFluidPhase::Granular);
    auto nearest = solver->raycast({-1.f, 1.f, 0.f}, {1.f, 0.f, 0.f}, 10.f, 2, 0x0f);
    REQUIRE(nearest.ok());
    REQUIRE(nearest.value().size() == 2);
    REQUIRE(nearest.value()[0].particleIndex == 0);
    REQUIRE(nearest.value()[1].particleIndex == 1);
    auto granular = solver->raycast({-1.f, 1.f, 0.f}, {1.f, 0.f, 0.f}, 10.f, 4, 1u << 2);
    REQUIRE(granular.ok());
    REQUIRE(granular.value().size() == 1);
    REQUIRE(granular.value()[0].particleIndex == 2);
    auto inside = solver->raycast(particles[0].position, {1.f, 0.f, 0.f}, 0.f, 1, 1);
    REQUIRE(inside.ok());
    REQUIRE(inside.value().size() == 1);
    REQUIRE(inside.value()[0].distance == 0.f);
    REQUIRE(inside.value()[0].normal.x == -1.f);
    REQUIRE(!solver->raycast({}, {}, 1.f).ok());
    REQUIRE(!solver->raycast({}, {0, 0, 1}, -1.f).ok());
    REQUIRE(!solver->raycast({}, {0, 0, 1}, 1.f, 0).ok());
    REQUIRE(!solver->raycast({}, {0, 0, 1}, 1.f, 1, 0x10).ok());
    REQUIRE(solver->particleCount() == 4);
}

TEST_CASE("fluids.volume.raycastRejectsUnboundedSource") {
    VolumeFluidSettings settings;
    settings.capacity = 65537;
    auto result       = VolumeFluid::create(settings);
    REQUIRE(result.ok());
    auto                             solver = std::move(result).takeValue();
    std::vector<VolumeFluidParticle> particles(settings.capacity);
    for (auto& particle : particles) particle.position = {0.f, 1.f, 0.f};
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(!solver->raycast({0.f, 1.f, -1.f}, {0.f, 0.f, 1.f}, 2.f).ok());
    REQUIRE(!solver->querySphere({0.f, 1.f, 0.f}, .1f, 0.f, 1.f).ok());
    REQUIRE(!solver->queryBox({0.f, 1.f, 0.f}, {.1f, .1f, .1f}, {0, 0, 0, 1}, 0.f, 1.f).ok());
    REQUIRE(solver->particleCount() == settings.capacity);
}

TEST_CASE("fluids.volume.sphereDistanceQuerySignedOffsetAndFilter") {
    auto result = VolumeFluid::create({});
    REQUIRE(result.ok());
    auto                             solver = std::move(result).takeValue();
    std::vector<VolumeFluidParticle> particles(3);
    particles[0].position       = {0.f, 1.f, 0.f};
    particles[1].position       = {.2f, 1.f, 0.f};
    particles[1].material.phase = VolumeFluidPhase::Gas;
    particles[2].position       = {.4f, 1.f, 0.f};
    particles[2].material.phase = VolumeFluidPhase::Granular;
    REQUIRE(solver->emit(particles).ok());
    auto hits = solver->querySphere({0.f, 1.f, 0.f}, .1f, .02f, .1f, 4, 0x0f);
    REQUIRE(hits.ok());
    REQUIRE(hits.value().size() == 2);
    REQUIRE(hits.value()[0].particleIndex == 0);
    REQUIRE(std::abs(hits.value()[0].distance + .17f) < 1e-6f);
    REQUIRE(hits.value()[0].normal.y == 1.f);
    REQUIRE(std::abs(hits.value()[0].queryPoint.y - 1.12f) < 1e-6f);
    REQUIRE(hits.value()[1].particleIndex == 1);
    REQUIRE(std::abs(hits.value()[1].distance - .03f) < 1e-6f);
    REQUIRE(std::abs(hits.value()[1].queryPoint.x - .12f) < 1e-6f);
    REQUIRE(hits.value()[1].normal.x == 1.f);
    auto gas = solver->querySphere({0.f, 1.f, 0.f}, .5f, 0.f, 0.f, 1, 1u << 1);
    REQUIRE(gas.ok());
    REQUIRE(gas.value().size() == 1);
    REQUIRE(gas.value()[0].particle.material.phase == VolumeFluidPhase::Gas);
    hits.value()[0].particle.position.x = 9.f;
    REQUIRE(solver->particles()[0].position.x == 0.f);
    REQUIRE(!solver->querySphere({}, -1.f, 0.f, 1.f).ok());
    REQUIRE(!solver->querySphere({}, 1.f, -1.f, 1.f).ok());
    REQUIRE(!solver->querySphere({}, 1.f, 0.f, -1.f).ok());
    REQUIRE(!solver->querySphere({}, 1.f, 0.f, 1.f, 0, 15).ok());
    REQUIRE(!solver->querySphere({}, 1.f, 0.f, 1.f, 1, 16).ok());
    REQUIRE(solver->particleCount() == 3);
}

TEST_CASE("fluids.volume.orientedBoxDistanceInsideOutsideAndOffset") {
    auto result = VolumeFluid::create({});
    REQUIRE(result.ok());
    auto                             solver = std::move(result).takeValue();
    std::vector<VolumeFluidParticle> particles(3);
    particles[0].position       = {0.f, 1.f, 0.f};
    particles[1].position       = {0.f, 1.3f, 0.f};
    particles[1].material.phase = VolumeFluidPhase::Gas;
    particles[2].position       = {.3f, 1.f, 0.f};
    particles[2].material.phase = VolumeFluidPhase::Granular;
    REQUIRE(solver->emit(particles).ok());
    const glm::vec4 quarterTurn{0.f, 0.f, .70710678f, .70710678f};
    auto            hits = solver->queryBox({0.f, 1.f, 0.f}, {.2f, .05f, .05f}, quarterTurn, .02f, .04f, 4, 15);
    REQUIRE(hits.ok());
    REQUIRE(hits.value().size() == 2);
    REQUIRE(hits.value()[0].particleIndex == 0);
    REQUIRE(std::abs(hits.value()[0].distance + .12f) < 1e-5f);
    REQUIRE(hits.value()[0].normal.x > 0.999f);
    REQUIRE(std::abs(hits.value()[0].queryPoint.x - .07f) < 1e-5f);
    REQUIRE(hits.value()[1].particleIndex == 1);
    REQUIRE(std::abs(hits.value()[1].distance - .03f) < 1e-5f);
    REQUIRE(hits.value()[1].normal.y > 0.999f);
    REQUIRE(std::abs(hits.value()[1].queryPoint.y - 1.22f) < 1e-5f);
    auto gas = solver->queryBox({0.f, 1.f, 0.f}, {.2f, .05f, .05f}, quarterTurn, 0.f, 1.f, 1, 1u << 1);
    REQUIRE(gas.ok());
    REQUIRE(gas.value().size() == 1);
    REQUIRE(gas.value()[0].particleIndex == 1);
    hits.value()[0].particle.position.y = 9.f;
    REQUIRE(solver->particles()[0].position.y == 1.f);
    REQUIRE(!solver->queryBox({}, {-.1f, 1.f, 1.f}, {0, 0, 0, 1}, 0.f, 1.f).ok());
    REQUIRE(!solver->queryBox({}, {1.f, 1.f, 1.f}, {0, 0, 0, 2}, 0.f, 1.f).ok());
    REQUIRE(!solver->queryBox({}, {1.f, 1.f, 1.f}, {0, 0, 0, 1}, -1.f, 1.f).ok());
    REQUIRE(!solver->queryBox({}, {1.f, 1.f, 1.f}, {0, 0, 0, 1}, 0.f, -1.f).ok());
    REQUIRE(!solver->queryBox({}, {1.f, 1.f, 1.f}, {0, 0, 0, 1}, 0.f, 1.f, 0, 15).ok());
    REQUIRE(!solver->queryBox({}, {1.f, 1.f, 1.f}, {0, 0, 0, 1}, 0.f, 1.f, 1, 16).ok());
    REQUIRE(solver->particleCount() == 3);
}

TEST_CASE("fluids.volume.mixedQueryBatchIsGroupedBoundedAndAtomic") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                             solver = std::move(created).takeValue();
    std::vector<VolumeFluidParticle> particles(3);
    particles[0].position       = {0.f, 1.f, 0.f};
    particles[1].position       = {.2f, 1.f, 0.f};
    particles[1].material.phase = VolumeFluidPhase::Gas;
    particles[2].position       = {.4f, 1.f, 0.f};
    particles[2].material.phase = VolumeFluidPhase::Granular;
    REQUIRE(solver->emit(particles).ok());
    std::vector<VolumeFluidQueryShape> queries(3);
    queries[0].center        = {0.f, 1.f, 0.f};
    queries[0].size          = {.1f, 0.f, 0.f};
    queries[0].maxDistance   = .1f;
    queries[1].type          = VolumeFluidQueryType::Box;
    queries[1].center        = {.2f, 1.f, 0.f};
    queries[1].size          = {.1f, .1f, .1f};
    queries[1].maxDistance   = 0.f;
    queries[1].phaseMask     = 1u << 1;
    queries[2].type          = VolumeFluidQueryType::Ray;
    queries[2].center        = {-1.f, 1.f, 0.f};
    queries[2].size          = {1.f, 1.f, 0.f};
    queries[2].contactOffset = .5f;
    queries[2].maxDistance   = 99.f;
    auto hits                = solver->queryBatch(queries, 2);
    REQUIRE(hits.ok());
    REQUIRE(hits.value().size() == 5);
    REQUIRE(hits.value()[0].queryIndex == 0);
    REQUIRE(hits.value()[0].particleIndex == 0);
    REQUIRE(hits.value()[1].queryIndex == 0);
    REQUIRE(hits.value()[1].particleIndex == 1);
    REQUIRE(hits.value()[2].queryIndex == 1);
    REQUIRE(hits.value()[2].particleIndex == 1);
    REQUIRE(hits.value()[3].queryIndex == 2);
    REQUIRE(hits.value()[3].particleIndex == 0);
    REQUIRE(hits.value()[4].queryIndex == 2);
    REQUIRE(hits.value()[4].particleIndex == 1);
    REQUIRE(std::abs(hits.value()[3].distance + .5f) < 1e-6f);
    REQUIRE(std::abs(hits.value()[3].queryPoint.x - .45f) < 1e-6f);
    hits.value()[0].particle.position.x = 9.f;
    REQUIRE(solver->particles()[0].position.x == 0.f);
    queries[0].size.y = 1.f;
    REQUIRE(!solver->queryBatch(queries, 2).ok());
    REQUIRE(solver->particleCount() == 3);
    REQUIRE(!solver->queryBatch({}, 0).ok());
}

TEST_CASE("fluids.volume.mixedQueryBatchRejectsCandidateExplosion") {
    VolumeFluidSettings settings;
    settings.capacity = 20000;
    auto created      = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                             solver = std::move(created).takeValue();
    std::vector<VolumeFluidParticle> particles(settings.capacity);
    for (auto& particle : particles) particle.position = {0.f, 1.f, 0.f};
    REQUIRE(solver->emit(particles).ok());
    std::vector<VolumeFluidQueryShape> queries(201);
    for (auto& query : queries) query.size = {0.f, 0.f, 0.f};
    REQUIRE(!solver->queryBatch(queries, 1).ok());
    REQUIRE(solver->particleCount() == settings.capacity);
}

TEST_CASE("fluids.volume.queryColorsAreAtomicAndLaterOverlapsWin") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position = {-.2f, 1.f, 0.f};
    particles[1].position = {0.f, 1.f, 0.f};
    particles[2].position = {.2f, 1.f, 0.f};
    REQUIRE(solver->emit(particles).ok());
    VolumeFluidQueryShape queries[2];
    queries[0].type          = VolumeFluidQueryType::Box;
    queries[0].center        = {-.1f, 1.f, 0.f};
    queries[0].size          = {.3f, .2f, .2f};
    queries[1].type          = VolumeFluidQueryType::Box;
    queries[1].center        = {.1f, 1.f, 0.f};
    queries[1].size          = {.3f, .2f, .2f};
    const glm::vec4 colors[] = {{1.f, 0.f, 0.f, 1.f}, {1.f, 1.f, 0.f, 1.f}};
    auto            counts   = solver->applyQueryColors(queries, colors, {0.f, 1.f, 1.f, 1.f}, 3);
    REQUIRE(counts.ok());
    REQUIRE(counts.value().size() == 2);
    REQUIRE(counts.value()[0] == 2);
    REQUIRE(counts.value()[1] == 2);
    const auto colored = solver->particles();
    REQUIRE(colored[0].color == colors[0]);
    REQUIRE(colored[1].color == colors[1]);
    REQUIRE(colored[2].color == colors[1]);
    queries[1].rotation = {0.f, 0.f, 0.f, 2.f};
    REQUIRE(!solver->applyQueryColors(queries, colors, {0.f, 0.f, 1.f, 1.f}, 3).ok());
    REQUIRE(solver->particles()[0].color == colors[0]);
    const glm::vec4 invalid[] = {{2.f, 0.f, 0.f, 1.f}, {1.f, 1.f, 0.f, 1.f}};
    queries[1].rotation       = {0.f, 0.f, 0.f, 1.f};
    REQUIRE(!solver->applyQueryColors(queries, invalid, {0.f, 1.f, 1.f, 1.f}, 3).ok());
    REQUIRE(solver->particles()[1].color == colors[1]);
}

TEST_CASE("fluids.volume.persistentQueryColorsPreserveOutsideAndAreAtomic") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position = {-.2f, 1.f, 0.f};
    particles[1].position = {0.f, 1.f, 0.f};
    particles[2].position = {.2f, 1.f, 0.f};
    particles[0].color    = {0.f, 0.f, 1.f, 1.f};
    particles[1].color    = {0.f, 1.f, 0.f, 1.f};
    particles[2].color    = {0.f, 1.f, 1.f, 1.f};
    REQUIRE(solver->emit(particles).ok());
    VolumeFluidQueryShape queries[2];
    queries[0].type                      = VolumeFluidQueryType::Box;
    queries[0].center                    = {-.2f, 1.f, 0.f};
    queries[0].size                      = {.1f, .1f, .1f};
    queries[1].type                      = VolumeFluidQueryType::Box;
    queries[1].center                    = {0.f, 1.f, 0.f};
    queries[1].size                      = {.1f, .1f, .1f};
    const glm::vec4             colors[] = {{1.f, 0.f, 0.f, 1.f}, {1.f, 1.f, 0.f, 1.f}};
    auto                        counts   = solver->applyQueryColorsPreservingOutside(queries, colors, 3);
    const std::vector<unsigned> expectedCounts{1, 1};
    REQUIRE(counts.ok());
    REQUIRE(counts.value() == expectedCounts);
    REQUIRE(solver->particles()[0].color == colors[0]);
    REQUIRE(solver->particles()[1].color == colors[1]);
    REQUIRE(solver->particles()[2].color == particles[2].color);
    queries[1].rotation = {0.f, 0.f, 0.f, 2.f};
    REQUIRE(!solver->applyQueryColorsPreservingOutside(queries, colors, 3).ok());
    REQUIRE(solver->particles()[0].color == colors[0]);
    const glm::vec4 invalid[] = {{2.f, 0.f, 0.f, 1.f}, {1.f, 1.f, 0.f, 1.f}};
    queries[1].rotation       = {0.f, 0.f, 0.f, 1.f};
    REQUIRE(!solver->applyQueryColorsPreservingOutside(queries, invalid, 3).ok());
    REQUIRE(solver->particles()[1].color == colors[1]);
}

TEST_CASE("fluids.volume.mixedQueryBatchCodecIsStrictAndOwning") {
    eve::Value query(eve::Value::Object{});
    query.set("type", 0);
    query.set("center", eve::Value(eve::Value::Array{0.f, 1.f, 0.f}));
    query.set("size", eve::Value(eve::Value::Array{.1f, 0.f, 0.f}));
    query.set("rotation", eve::Value(eve::Value::Array{0.f, 0.f, 0.f, 1.f}));
    query.set("contactOffset", 0.f);
    query.set("maxDistance", 1.f);
    query.set("phaseMask", 15);
    query.set("collisionFilter", int64_t(0xffff0001u));
    auto decoded = decodeVolumeFluidQueries(eve::Value(eve::Value::Array{query}));
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().size() == 1);
    REQUIRE(decoded.value()[0].size.x == .1f);
    query.set("unknown", 1);
    REQUIRE(!decodeVolumeFluidQueries(eve::Value(eve::Value::Array{query})).ok());
    REQUIRE(!decodeVolumeFluidQueries(eve::Value(eve::Value::Array(257, query))).ok());
    auto colors =
        decodeVolumeFluidColors(eve::Value(eve::Value::Array{eve::Value(eve::Value::Array{1.f, 0.f, 0.f, 1.f})}));
    REQUIRE(colors.ok());
    REQUIRE(colors.value()[0].x == 1.f);
    REQUIRE(!decodeVolumeFluidColors(eve::Value(eve::Value::Array{eve::Value(eve::Value::Array{1.f, 0.f, 0.f})})).ok());
}

TEST_CASE("fluids.volume.queryCollisionFiltersMatchBothDirections") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle a, b;
    a.position                            = {0.f, 1.f, 0.f};
    b.position                            = {.2f, 1.f, 0.f};
    a.collisionFilter                     = 0x00020001u;
    b.collisionFilter                     = 0x00010002u;
    const VolumeFluidParticle particles[] = {a, b};
    REQUIRE(solver->emit(particles).ok());
    auto sphere = solver->querySphere({0.f, 1.f, 0.f}, .5f, 0.f, 0.f, 4, 15, 0x00010002u);
    REQUIRE(sphere.ok());
    REQUIRE(sphere.value().size() == 1);
    REQUIRE(sphere.value()[0].particleIndex == 0);
    VolumeFluidQueryShape query;
    query.center          = {0.f, 1.f, 0.f};
    query.size            = {.5f, 0.f, 0.f};
    query.collisionFilter = 0x00010002u;
    auto batch            = solver->queryBatch(std::span(&query, 1), 4);
    REQUIRE(batch.ok());
    REQUIRE(batch.value().size() == 1);
    REQUIRE(batch.value()[0].particleIndex == 0);
    query.collisionFilter = 0;
    REQUIRE(!solver->queryBatch(std::span(&query, 1), 4).ok());
    VolumeFluidParticle invalid = a;
    invalid.position.y          = 1.2f;
    invalid.collisionFilter     = 0;
    REQUIRE(!solver->emit(std::span(&invalid, 1)).ok());
    REQUIRE(solver->particleCount() == 2);
}

TEST_CASE("fluids.volume.particleInteractionsRequireReciprocalCollisionFilters") {
    VolumeFluidSettings settings;
    settings.gravity    = {0, 0, 0};
    settings.iterations = 2;
    auto make           = [&](unsigned secondFilter) {
        auto created = VolumeFluid::create(settings);
        REQUIRE(created.ok());
        auto                solver = std::move(created).takeValue();
        VolumeFluidParticle particles[2];
        particles[0].position        = {0, 1, 0};
        particles[1].position        = {0, 1, 0};
        particles[0].collisionFilter = 0x00020001u;
        particles[1].collisionFilter = secondFilter;
        particles[0].actorGroup      = 1;
        particles[1].actorGroup      = 2;
        REQUIRE(solver->emit(particles).ok());
        REQUIRE(solver->step(1.f / 120.f, 1).ok());
        return solver;
    };
    auto       oneWay   = make(0x00020002u);
    const auto isolated = oneWay->particles();
    REQUIRE(glm::length(isolated[0].position - isolated[1].position) == 0.f);
    REQUIRE(isolated[0].collisionFilter == 0x00020001u);
    REQUIRE(isolated[1].collisionFilter == 0x00020002u);
    auto       reciprocal  = make(0x00010002u);
    const auto interacting = reciprocal->particles();
    REQUIRE(glm::length(interacting[0].position - interacting[1].position) > .001f);
}

TEST_CASE("fluids.volume.actorGroupsRequireMutualSelfCollision") {
    VolumeFluidSettings settings;
    settings.gravity    = {0, 0, 0};
    settings.iterations = 2;
    auto make           = [&](unsigned secondGroup, bool firstSelf, bool secondSelf) {
        auto created = VolumeFluid::create(settings);
        REQUIRE(created.ok());
        auto                solver = std::move(created).takeValue();
        VolumeFluidParticle particles[2];
        particles[0].position        = {0, 1, 0};
        particles[1].position        = {0, 1, 0};
        particles[0].collisionFilter = 0x00020001u;
        particles[1].collisionFilter = 0x00020002u;
        particles[0].actorGroup      = 7;
        particles[1].actorGroup      = secondGroup;
        particles[0].selfCollide     = firstSelf;
        particles[1].selfCollide     = secondSelf;
        REQUIRE(solver->emit(particles).ok());
        REQUIRE(solver->step(1.f / 120.f, 1).ok());
        return solver;
    };
    REQUIRE(glm::length(make(7, true, true)->particles()[0].position - glm::vec3(0, 1, 0)) > .001f);
    REQUIRE(make(7, false, true)->particles()[0].position == glm::vec3(0, 1, 0));
    REQUIRE(make(7, true, false)->particles()[0].position == glm::vec3(0, 1, 0));
    REQUIRE(make(8, true, true)->particles()[0].position == glm::vec3(0, 1, 0));

    auto              current = make(7, true, true)->snapshot();
    auto              encoded = encodeVolumeFluid(current);
    eve::Value::Array particles;
    for (size_t i = 0; i < encoded.find("particles")->arraySize(); ++i) {
        const auto& source = encoded.find("particles")->at(i);
        eve::Value  legacy(eve::Value::Object{});
        for (const auto& key : source.keys())
            if (key != "actorGroup" && key != "selfCollide") legacy.set(key, *source.find(key));
        particles.push_back(withoutRecentMaterialFields(std::move(legacy)));
    }
    encoded.set("particles", eve::Value(std::move(particles)));
    encoded = withoutSdfColliders(encoded);
    encoded.set("version", 8);
    auto migrated = decodeVolumeFluid(encoded);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().version == 20);
    REQUIRE(migrated.value().particles[0].actorGroup == 0);
    REQUIRE(migrated.value().particles[0].selfCollide);
    VolumeFluidParticle invalid;
    invalid.actorGroup = 0x01000000u;
    auto created       = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    REQUIRE(!created.value()->emit(std::span(&invalid, 1)).ok());
}

TEST_CASE("fluids.volume.colliderInteractionsRequireReciprocalCollisionFilters") {
    VolumeFluidSettings settings;
    settings.gravity    = {0, 0, 0};
    settings.iterations = 1;
    auto make           = [&](unsigned colliderFilter) {
        auto created = VolumeFluid::create(settings);
        REQUIRE(created.ok());
        auto                solver = std::move(created).takeValue();
        VolumeFluidParticle particle;
        particle.position        = {0, 1, 0};
        particle.collisionFilter = 0x00020001u;
        VolumeFluidCollider collider;
        collider.center          = {0, 1, 0};
        collider.collisionFilter = colliderFilter;
        REQUIRE(solver->setColliders(std::span(&collider, 1)).ok());
        REQUIRE(solver->emit(std::span(&particle, 1)).ok());
        REQUIRE(solver->step(1.f / 120.f, 1).ok());
        return solver;
    };
    auto oneWay = make(0x00020002u);
    REQUIRE(oneWay->particles()[0].position == glm::vec3(0, 1, 0));
    REQUIRE(oneWay->contacts().empty());
    auto reciprocal = make(0x00010002u);
    REQUIRE(glm::length(reciprocal->particles()[0].position - glm::vec3(0, 1, 0)) > .001f);
    REQUIRE(reciprocal->contacts().size() == 1);
    auto before             = reciprocal->snapshot();
    auto invalid            = before.colliders[0];
    invalid.collisionFilter = 0;
    REQUIRE(!reciprocal->setColliders(std::span(&invalid, 1)).ok());
    REQUIRE(reciprocal->snapshot().colliders[0].collisionFilter == 0x00010002u);

    auto        encoded = encodeVolumeFluid(before);
    const auto& current = encoded.find("colliders")->at(0);
    eve::Value  oldCollider(eve::Value::Object{});
    for (const auto& key : current.keys())
        if (key != "collisionFilter" && key != "isTrigger") oldCollider.set(key, *current.find(key));
    const auto& currentParticle = encoded.find("particles")->at(0);
    eve::Value  oldParticle(eve::Value::Object{});
    for (const auto& key : currentParticle.keys())
        if (key != "actorGroup" && key != "selfCollide") oldParticle.set(key, *currentParticle.find(key));
    encoded.set("particles", eve::Value(eve::Value::Array{withoutRecentMaterialFields(std::move(oldParticle))}));
    encoded.set("colliders", eve::Value(eve::Value::Array{oldCollider}));
    encoded = withoutSdfColliders(encoded);
    encoded.set("version", 7);
    auto migrated = decodeVolumeFluid(encoded);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().version == 20);
    REQUIRE(migrated.value().colliders[0].collisionFilter == 0xffff0001u);
}

TEST_CASE("fluids.volume.orientedEllipsoidQueriesAndCanonicalDefaults") {
    VolumeFluidSettings settings;
    settings.spacing = .2f;
    settings.gravity = {0, 0, 0};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position = {0.f, 1.f, 0.f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    REQUIRE(solver->particles()[0].radii == glm::vec3(.1f));
    solver->clear();
    particle.radii       = {.2f, .05f, .05f};
    particle.orientation = {0.f, 0.f, .70710678f, .70710678f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    auto vertical = solver->querySphere({0.f, 1.3f, 0.f}, 0.f, 0.f, 1.f, 1);
    REQUIRE(vertical.ok());
    REQUIRE(vertical.value().size() == 1);
    REQUIRE(std::abs(vertical.value()[0].distance - .1f) < 1e-5f);
    auto horizontal = solver->querySphere({.3f, 1.f, 0.f}, 0.f, 0.f, 1.f, 1);
    REQUIRE(horizontal.ok());
    REQUIRE(std::abs(horizontal.value()[0].distance - .25f) < 1e-5f);
    auto ray = solver->raycast({0.f, .7f, 0.f}, {0.f, 1.f, 0.f}, 1.f, 1);
    REQUIRE(ray.ok());
    REQUIRE(ray.value().size() == 1);
    REQUIRE(std::abs(ray.value()[0].distance - .1f) < 1e-5f);
    REQUIRE(ray.value()[0].normal.y < -.999f);
    auto state = solver->snapshot();
    REQUIRE(state.version == 20);
    REQUIRE(state.particles[0].radii == particle.radii);
    VolumeFluidParticle invalid = particle;
    invalid.radii               = {.1f, 0.f, .1f};
    REQUIRE(!solver->emit(std::span(&invalid, 1)).ok());
    invalid             = particle;
    invalid.orientation = {0, 0, 0, 2};
    REQUIRE(!solver->emit(std::span(&invalid, 1)).ok());
    REQUIRE(solver->particleCount() == 1);
}

TEST_CASE("fluids.volume.solidAttachmentRollbackAndLifetimeRemap") {
    VolumeFluidSettings settings;
    settings.gravity = {0, 0, 0};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle expired, live, shortSolid;
    expired.position                      = {1, 1, 0};
    expired.life                          = .001f;
    live.position                         = {0, 1.25f, 0};
    shortSolid.position                   = {.25f, 1, 0};
    shortSolid.life                       = .001f;
    const VolumeFluidParticle particles[] = {expired, live, shortSolid};
    VolumeFluidCollider       c;
    c.label    = 17;
    c.solidify = true;
    REQUIRE(solver->setColliders(std::span(&c, 1)).ok());
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->step(1.f / 60.f).ok());
    const auto before = solver->snapshot();
    REQUIRE(before.particles.size() == 1);
    REQUIRE(before.attachments.size() == 1);
    REQUIRE(before.attachments[0].particleIndex == 0);
    VolumeFluidThermalRule badRule;
    badRule.colliderLabel = 17;
    REQUIRE(!solver->stepWithColliders(1.f / 60.f, 4, {}, std::span(&badRule, 1)).ok());
    REQUIRE(solver->snapshot().attachments.size() == 1);
    REQUIRE(solver->snapshot().colliders[0].label == 17);
    c.center.x = 10;
    REQUIRE(!solver->stepWithColliders(1.f / 60.f, 4, std::span(&c, 1)).ok());
    REQUIRE(solver->snapshot().colliders[0].center.x == 0.f);
    REQUIRE(glm::length(solver->particles()[0].position - before.particles[0].position) == 0.f);
    auto invalid                         = before;
    invalid.attachments[0].particleIndex = 1;
    REQUIRE(!solver->restore(invalid).ok());
    invalid                              = before;
    invalid.attachments[0].colliderLabel = 99;
    REQUIRE(!solver->restore(invalid).ok());
    invalid = before;
    invalid.attachments.push_back(invalid.attachments[0]);
    REQUIRE(!solver->restore(invalid).ok());
    REQUIRE(solver->snapshot().attachments.size() == 1);
    solver->clear();
    REQUIRE(solver->snapshot().attachments.empty());
}

TEST_CASE("fluids.volume.versionTwoMigratesWorldFixedSolids") {
    VolumeFluidSnapshot original;
    VolumeFluidParticle p;
    p.position       = {0, 1, 0};
    p.material.phase = VolumeFluidPhase::Solid;
    original.particles.push_back(p);
    auto        current         = encodeVolumeFluid(original);
    const auto& currentParticle = current.find("particles")->at(0);
    eve::Value  oldParticle(eve::Value::Object{});
    for (const auto& key : currentParticle.keys())
        if (key != "collisionFilter" && key != "radii" && key != "orientation" && key != "actorGroup" &&
            key != "selfCollide")
            oldParticle.set(key, *currentParticle.find(key));
    current.set("particles", eve::Value(eve::Value::Array{withoutRecentMaterialFields(std::move(oldParticle))}));
    eve::Value old(eve::Value::Object{});
    for (const auto& key : current.keys())
        if (key != "attachments" && key != "simplexes") old.set(key, *current.find(key));
    old = withoutSdfColliders(old);
    old.set("version", 2);
    auto decoded = decodeVolumeFluid(old);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().version == 20);
    REQUIRE(decoded.value().attachments.empty());
    REQUIRE(decoded.value().particles[0].material.phase == VolumeFluidPhase::Solid);
    REQUIRE(decoded.value().particles[0].collisionFilter == 0xffff0001u);
    REQUIRE(decoded.value().particles[0].radii == glm::vec3(.05f));
    old.set("attachments", eve::Value(eve::Value::Array{}));
    REQUIRE(!decodeVolumeFluid(old).ok());
}

TEST_CASE("fluids.volume.outOfBoundsSolidificationRollsBackAttachment") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidCollider c;
    c.solidify = true;
    c.center   = {0, 2.9f, 0};
    VolumeFluidParticle p;
    p.position = {0, 2.94f, 0};
    REQUIRE(solver->setColliders(std::span(&c, 1)).ok());
    REQUIRE(solver->emit(std::span(&p, 1)).ok());
    REQUIRE(!solver->step(1.f / 60.f).ok());
    REQUIRE(solver->snapshot().attachments.empty());
    REQUIRE(solver->contacts().empty());
    REQUIRE(solver->particles()[0].material.phase == VolumeFluidPhase::Liquid);
    REQUIRE(solver->particles()[0].position.y == p.position.y);
}

TEST_CASE("fluids.volume.solidificationPropagatesOneContactWave") {
    for (unsigned substeps : {1u, 4u})
        for (unsigned iterations : {1u, 10u}) {
            VolumeFluidSnapshot input;
            input.settings.gravity    = {0, 0, 0};
            input.settings.iterations = iterations;
            VolumeFluidCollider collider;
            collider.label    = 17;
            collider.radius   = .01f;
            collider.solidify = true;
            input.colliders.push_back(collider);
            for (unsigned i = 0; i < 4; ++i) {
                VolumeFluidParticle p;
                p.position           = {float(i) * .1f, 1, 0};
                p.material.cohesion  = 0;
                p.material.viscosity = 0;
                if (i == 0) p.material.phase = VolumeFluidPhase::Solid;
                input.particles.push_back(p);
            }
            input.attachments.push_back({0, 17, {0, 0, 0}});
            auto created = VolumeFluid::create(input.settings);
            REQUIRE(created.ok());
            auto solver = std::move(created).takeValue();
            REQUIRE(solver->restore(input).ok());
            for (unsigned wave = 1; wave <= 3; ++wave) {
                REQUIRE(solver->step(1.f / 120.f, substeps).ok());
                auto snapshot = solver->snapshot();
                REQUIRE(snapshot.attachments.size() == wave + 1);
                for (unsigned i = 0; i < 4; ++i)
                    REQUIRE((snapshot.particles[i].material.phase == VolumeFluidPhase::Solid) == (i <= wave));
            }
            collider.center.x = .1f;
            REQUIRE(solver->stepWithColliders(1.f / 120.f, 1, std::span(&collider, 1)).ok());
            for (unsigned i = 0; i < 4; ++i)
                REQUIRE(std::abs(solver->particles()[i].position.x - (float(i) * .1f + .1f)) < 1e-5f);
        }
}

TEST_CASE("fluids.volume.versionFiveMigratesAttachmentLocalOrientation") {
    VolumeFluidSnapshot current;
    VolumeFluidParticle particle;
    particle.position       = {0, 1, 0};
    particle.material.phase = VolumeFluidPhase::Solid;
    particle.radii          = glm::vec3(.05f);
    particle.orientation    = {0, 0, .70710678f, .70710678f};
    current.particles.push_back(particle);
    VolumeFluidCollider collider;
    collider.label    = 41;
    collider.rotation = {0, 0, .38268343f, .92387953f};
    current.colliders.push_back(collider);
    current.attachments.push_back({0, 41, {0, 1, 0}, {0, 0, .38268343f, .92387953f}});
    auto       encoded    = encodeVolumeFluid(current);
    auto       attachment = encoded.find("attachments")->at(0);
    eve::Value legacyAttachment(eve::Value::Object{});
    for (const auto& key : attachment.keys())
        if (key != "localOrientation" && key != "constrainOrientation" && key != "dynamic" && key != "compliance" &&
            key != "breakThreshold")
            legacyAttachment.set(key, *attachment.find(key));
    encoded.set("attachments", eve::Value(eve::Value::Array{legacyAttachment}));
    encoded = withoutSdfColliders(encoded);
    encoded.set("version", 5);
    const auto& encodedParticle = encoded.find("particles")->at(0);
    eve::Value  legacyParticle(eve::Value::Object{});
    for (const auto& key : encodedParticle.keys())
        if (key != "actorGroup" && key != "selfCollide") legacyParticle.set(key, *encodedParticle.find(key));
    encoded.set("particles", eve::Value(eve::Value::Array{withoutRecentMaterialFields(std::move(legacyParticle))}));
    const auto& encodedCollider = encoded.find("colliders")->at(0);
    eve::Value  legacyCollider(eve::Value::Object{});
    for (const auto& key : encodedCollider.keys())
        if (key != "collisionFilter") legacyCollider.set(key, *encodedCollider.find(key));
    encoded.set("colliders", eve::Value(eve::Value::Array{legacyCollider}));
    eve::Value legacy(eve::Value::Object{});
    for (const auto& key : encoded.keys())
        if (key != "simplexes") legacy.set(key, *encoded.find(key));
    auto decoded = decodeVolumeFluid(legacy);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().version == 20);
    REQUIRE(std::abs(decoded.value().attachments[0].localOrientation.z - .38268343f) < 1e-5f);
    REQUIRE(std::abs(decoded.value().attachments[0].localOrientation.w - .92387953f) < 1e-5f);
    REQUIRE(decoded.value().attachments[0].constrainOrientation);
}

TEST_CASE("fluids.volume.simplexQueriesReturnBarycentricPointEdgeAndTriangleResults") {
    VolumeFluidSettings settings;
    settings.gravity = {0, 0, 0};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position = {-1, 1, 0};
    particles[1].position = {1, 1, 0};
    particles[2].position = {0, 2, 0};
    REQUIRE(solver->emit(particles).ok());
    VolumeFluidSimplex triangle{{0, 1, 2}, 3};
    REQUIRE(solver->setSimplexes(std::span(&triangle, 1)).ok());
    std::vector<VolumeFluidQueryShape> queries(3);
    queries[0].type        = VolumeFluidQueryType::Sphere;
    queries[0].center      = {0, .5f, 0};
    queries[0].size        = {.1f, 0, 0};
    queries[0].maxDistance = 2.f;
    queries[1].type        = VolumeFluidQueryType::Box;
    queries[1].center      = {0, 0, 0};
    queries[1].size        = {.4f, .4f, .4f};
    queries[1].maxDistance = 2.f;
    queries[2].type        = VolumeFluidQueryType::Ray;
    queries[2].center      = {0, 0, 0};
    queries[2].size        = {0, 3, 0};
    queries[2].maxDistance = 2.f;
    auto hits              = solver->querySimplexes(queries, 2);
    REQUIRE(hits.ok());
    REQUIRE(hits.value().size() == 3);
    REQUIRE(hits.value()[0].queryIndex == 0);
    REQUIRE(hits.value()[0].simplexIndex == 0);
    REQUIRE(std::abs(hits.value()[0].simplexBary.x - .5f) < 1e-5f);
    REQUIRE(std::abs(hits.value()[0].simplexBary.y - .5f) < 1e-5f);
    REQUIRE(std::abs(hits.value()[0].distance - .35f) < 1e-5f);
    REQUIRE(hits.value()[1].queryIndex == 1);
    REQUIRE(std::abs(hits.value()[1].distance - .75f) < 1e-5f);
    REQUIRE(hits.value()[2].queryIndex == 2);
    REQUIRE(hits.value()[2].distance < 0.f);
    REQUIRE(std::abs(hits.value()[2].simplexBary.x + hits.value()[2].simplexBary.y + hits.value()[2].simplexBary.z -
                     1.f) < 1e-5f);
    REQUIRE(solver->setSimplexes(std::span<const VolumeFluidSimplex>{}).ok());
    auto implicit = solver->querySimplexes(std::span(queries.data(), 1), 4);
    REQUIRE(implicit.ok());
    REQUIRE(implicit.value().size() == 3);
}
}  // namespace test_fluids_volume_part3
#endif
