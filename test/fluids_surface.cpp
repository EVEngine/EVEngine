#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "fluids/FluidSurfaceBinding.h"
#include "fluids/SurfaceDropletSimulation.h"
#include "fluids/SurfaceFluidRenderData.h"
#include "fluids/SurfaceFluidSceneRenderer.h"
#include "fluids/SurfaceWetnessField.h"

#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

using namespace eve::fluids;

TEST_CASE("fluids.surfaceBinding.rigidPosePreservesMaterialLocation") {
    FluidSurfaceBinding binding;
    const std::vector<glm::vec3> positions = {
        {0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
    };
    const std::vector<uint32_t>  indices = {0, 1, 2};
    const std::vector<glm::vec2> uvs = {{0.f, 0.f}, {1.f, 0.f}, {0.f, 1.f}};
    REQUIRE(binding.build(positions, indices, uvs));

    const SurfaceLocation location{0, glm::vec3(0.25f, 0.25f, 0.5f)};
    const SurfaceSample initial = binding.evaluate(location, 1.f);
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
    FluidSurfaceBinding binding;
    const std::vector<glm::vec3> positions = {
        {-1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
    };
    REQUIRE(binding.build(positions, {0, 1, 2}));
    std::vector<glm::vec3> deformed = positions;
    deformed[2].z = 2.f;
    REQUIRE(binding.setDeformedPositions(deformed));

    const SurfaceSample anchored = binding.evaluate({0, glm::vec3(0.f, 0.f, 1.f)}, 0.25f);
    REQUIRE(glm::distance(anchored.position, glm::vec3(0.f, 1.f, 2.f)) < 1e-5f);
    REQUIRE(glm::distance(anchored.previousPosition, glm::vec3(0.f, 1.f, 0.f)) < 1e-5f);
    REQUIRE(glm::distance(anchored.velocity, glm::vec3(0.f, 0.f, 8.f)) < 1e-5f);
}

TEST_CASE("fluids.surfaceBinding.walkCrossesSharedEdge") {
    FluidSurfaceBinding binding;
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
    const SurfaceLocation start{0, glm::vec3(0.25f, 0.25f, 0.5f)};
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
        {-10.f, -10.f, 0.f}, {10.f, -10.f, 0.f}, {-10.f, 10.f, 0.f},
    };
    FluidSurfaceBinding oneFrameBinding;
    FluidSurfaceBinding fixedBinding;
    REQUIRE(oneFrameBinding.build(positions, {0, 1, 2}));
    REQUIRE(fixedBinding.build(positions, {0, 1, 2}));
    SurfaceDropletParams params;
    params.friction = 0.f;
    SurfaceDropletSimulation oneFrame(&oneFrameBinding, params);
    SurfaceDropletSimulation fixed(&fixedBinding, params);
    const SurfaceLocation start{0, glm::vec3(0.25f, 0.25f, 0.5f)};
    REQUIRE(oneFrame.addDroplet(start));
    REQUIRE(fixed.addDroplet(start));
    oneFrame.step(0.2f);
    for (int i = 0; i < 4; ++i) fixed.step(0.05f);
    REQUIRE(oneFrame.droplets().size() == 1u);
    REQUIRE(fixed.droplets().size() == 1u);
    const glm::vec3 oneFramePosition =
        oneFrameBinding.evaluate(oneFrame.droplets().front().location, 0.f).position;
    const glm::vec3 fixedPosition =
        fixedBinding.evaluate(fixed.droplets().front().location, 0.f).position;
    CHECK(glm::distance(oneFramePosition, fixedPosition) < 1e-4f);
    CHECK(glm::distance(oneFrame.droplets().front().relativeVelocity,
                        fixed.droplets().front().relativeVelocity) < 1e-4f);
}

TEST_CASE("fluids.surfaceDroplet.openEdgeDetachesToWorld") {
    FluidSurfaceBinding binding;
    REQUIRE(binding.build({{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}}, {0, 1, 2}));
    SurfaceDropletParams params;
    params.gravity = glm::vec3(0.f);
    params.friction = 0.f;
    SurfaceDropletSimulation droplets(&binding, params);
    REQUIRE(droplets.addDroplet({0, glm::vec3(0.8f, 0.1f, 0.1f)}, 2.f,
                                 glm::vec3(-5.f, -5.f, 0.f)));

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
    params.gravity = glm::vec3(0.f);
    params.friction = 0.f;
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
    REQUIRE(binding.build({{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {1.f, 1.f, 0.f}},
                          {0, 1, 2, 2, 1, 3}));
    SurfaceWetnessField wetness;
    REQUIRE(wetness.build(binding));
    wetness.deposit({0, glm::vec3(1.f, 0.f, 0.f)}, 1.f);
    REQUIRE(wetness.values()[0] > 0.99f);
    REQUIRE(wetness.values()[3] < 1e-6f);

    SurfaceWetnessParams params;
    params.diffusion = 1.f;
    params.evaporation = 0.5f;
    wetness.step(0.1f, params);
    REQUIRE(wetness.values()[1] > 0.f);
    REQUIRE(wetness.values()[0] < 1.f);
}

TEST_CASE("fluids.surfaceWetness.diffusionConservesFilmMass") {
    FluidSurfaceBinding binding;
    REQUIRE(binding.build({{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {1.f, 1.f, 0.f}},
                          {0, 1, 2, 2, 1, 3}));
    SurfaceWetnessField wetness;
    REQUIRE(wetness.build(binding));
    wetness.deposit({0, glm::vec3(1.f, 0.f, 0.f)}, 0.8f);
    const float before = std::accumulate(wetness.values().begin(), wetness.values().end(), 0.f);
    SurfaceWetnessParams params;
    params.diffusion = 1.f;
    params.evaporation = 0.f;
    params.maxWetness = 10.f;
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
    REQUIRE(simulation.addDroplet({0, glm::vec3(0.25f, 0.25f, 0.5f)}, 0.001f,
                                  glm::vec3(2.f, 0.f, 0.f)));
    SurfaceFluidRenderData renderData;
    renderData.update(binding, simulation);
    REQUIRE(renderData.droplets().size() == 1u);
    const SurfaceDropletRenderInstance& instance = renderData.droplets().front();
    CHECK(std::fabs(glm::dot(instance.normal, instance.majorAxis)) < 1e-6f);
    CHECK(std::fabs(glm::dot(instance.normal, instance.minorAxis)) < 1e-6f);
    CHECK(glm::length(instance.majorAxis) > glm::length(instance.minorAxis));
    const float footprint = glm::length(instance.majorAxis) * glm::length(instance.minorAxis);
    const float radius = simulation.dropletRadius(0.001f);
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
    instance.position = glm::vec3(2.f, 3.f, 4.f);
    instance.normal = glm::normalize(glm::vec3(0.2f, 1.f, 0.3f));
    instance.majorAxis = glm::vec3(0.4f, 0.f, 0.f);
    instance.minorAxis = glm::vec3(0.f, 0.f, 0.2f);
    instance.capHeight = 0.1f;
    const glm::mat4 model = SurfaceFluidSceneRenderer::modelMatrix(instance);
    const glm::vec3 lowerPole = glm::vec3(model * glm::vec4(0.f, -1.f, 0.f, 1.f));
    CHECK(glm::distance(lowerPole, instance.position) < 1e-6f);
    CHECK(glm::distance(glm::vec3(model[0]), instance.majorAxis) < 1e-6f);
    CHECK(glm::distance(glm::vec3(model[2]), instance.minorAxis) < 1e-6f);
}
