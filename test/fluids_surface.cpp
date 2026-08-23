#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "fluids/FluidSurfaceBinding.h"
#include "fluids/SurfaceDropletSimulation.h"
#include "fluids/SurfaceWetnessField.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace eve::fluids;

TEST_CASE("fluids.surfaceBinding.rigidPosePreservesMaterialLocation") {
    FluidSurfaceBinding binding;
    const std::vector<glm::vec3> positions = {
        {0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
    };
    const std::vector<glm::vec2> uvs = {{0.f, 0.f}, {1.f, 0.f}, {0.f, 1.f}};
    REQUIRE(binding.build(positions, {0, 1, 2}, uvs));

    const SurfaceLocation location{0, glm::vec3(0.25f, 0.25f, 0.5f)};
    const SurfaceSample initial = binding.evaluate(location, 1.f);
    CHECK(glm::distance(initial.position, glm::vec3(0.25f, 0.5f, 0.f)) < 1e-5f);
    CHECK(glm::distance(initial.uv, glm::vec2(0.25f, 0.5f)) < 1e-5f);

    glm::mat4 pose(1.f);
    pose[3] = glm::vec4(2.f, -1.f, 3.f, 1.f);
    binding.setTransform(pose);
    const SurfaceSample moved = binding.evaluate(location, 0.5f);
    CHECK(glm::distance(moved.position, initial.position + glm::vec3(2.f, -1.f, 3.f)) < 1e-5f);
    CHECK(glm::distance(moved.velocity, glm::vec3(4.f, -2.f, 6.f)) < 1e-5f);
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
    CHECK(glm::distance(anchored.position, glm::vec3(0.f, 1.f, 2.f)) < 1e-5f);
    CHECK(glm::distance(anchored.velocity, glm::vec3(0.f, 0.f, 8.f)) < 1e-5f);
}

TEST_CASE("fluids.surfaceBinding.walkCrossesSharedEdge") {
    FluidSurfaceBinding binding;
    REQUIRE(binding.build({{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {1.f, 1.f, 0.f}},
                          {0, 1, 2, 2, 1, 3}));
    CHECK_EQ(binding.adjacentTriangle(0, 0), 1);
    SurfaceLocation start;
    REQUIRE(binding.project(glm::vec3(0.25f, 0.25f, 0.1f), 0.2f, start));
    const SurfaceWalkResult walked = binding.walkAcrossSurface(start, glm::vec3(0.5f, 0.5f, 0.f));
    REQUIRE(walked.valid);
    CHECK(!walked.reachedBoundary);
    CHECK(walked.location.triangle == 1);
    CHECK(glm::distance(binding.evaluate(walked.location, 0.f).position,
                        glm::vec3(0.75f, 0.75f, 0.f)) < 1e-4f);
}

TEST_CASE("fluids.surfaceBinding.openEdgeReturnsDetachRemainder") {
    FluidSurfaceBinding binding;
    REQUIRE(binding.build({{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}}, {0, 1, 2}));
    const SurfaceWalkResult walked = binding.walkAcrossSurface(
        {0, glm::vec3(0.8f, 0.1f, 0.1f)}, glm::vec3(-0.5f, -0.5f, 0.f));
    REQUIRE(walked.valid);
    CHECK(walked.reachedBoundary);
    CHECK(glm::length(walked.remainingDisplacement) > 0.1f);
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
    const SurfaceDroplet& drop = droplets.droplets().front();
    CHECK(binding.evaluate(drop.location, 0.f).position.y < y0);
    CHECK(drop.relativeVelocity.y < 0.f);
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
    CHECK(droplets.droplets().empty());
    REQUIRE(droplets.detachedDroplets().size() == 1u);
    CHECK(std::fabs(droplets.detachedDroplets().front().volume - 2.f) < 1e-6f);
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
    glm::mat4 pose(1.f);
    pose[3].z = -0.1f;
    binding.setTransform(pose);
    droplets.step(0.1f);
    REQUIRE(droplets.droplets().size() == 1u);
    pose[3].z = -0.4f;
    binding.setTransform(pose);
    droplets.step(0.1f);
    CHECK(droplets.droplets().empty());
}

TEST_CASE("fluids.surfaceDroplet.nearbyCapsMergeConservingVolume") {
    FluidSurfaceBinding binding;
    REQUIRE(binding.build({{-2.f, -2.f, 0.f}, {2.f, -2.f, 0.f}, {-2.f, 2.f, 0.f}}, {0, 1, 2}));
    SurfaceDropletParams params;
    params.gravity = glm::vec3(0.f);
    SurfaceDropletSimulation droplets(&binding, params);
    REQUIRE(droplets.addDroplet({0, glm::vec3(0.25f, 0.25f, 0.5f)}, 0.001f));
    REQUIRE(droplets.addDroplet({0, glm::vec3(0.24f, 0.26f, 0.5f)}, 0.002f));
    droplets.step(0.01f);
    REQUIRE(droplets.droplets().size() == 1u);
    CHECK(std::fabs(droplets.droplets().front().volume - 0.003f) < 1e-6f);
}

TEST_CASE("fluids.surfaceWetness.depositDiffuseAndEvaporate") {
    FluidSurfaceBinding binding;
    REQUIRE(binding.build({{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {1.f, 1.f, 0.f}},
                          {0, 1, 2, 2, 1, 3}));
    SurfaceWetnessField wetness;
    REQUIRE(wetness.build(binding));
    wetness.deposit({0, glm::vec3(1.f, 0.f, 0.f)}, 1.f);
    SurfaceWetnessParams params;
    params.diffusion = 1.f;
    params.evaporation = 0.5f;
    wetness.step(0.1f, params);
    CHECK(wetness.values()[1] > 0.f);
    CHECK(wetness.values()[0] < 1.f);
}
