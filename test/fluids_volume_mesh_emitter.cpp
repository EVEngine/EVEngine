#include <glm/geometric.hpp>
#include "fluids/VolumeFluidEmitter.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::fluids;
TEST_CASE("fluids.volumeEmitter.meshInteriorAndScale") {
    const glm::vec3 vertices[] = {{-.3f, -.3f, -.3f}, {.3f, -.3f, -.3f}, {.3f, .3f, -.3f}, {-.3f, .3f, -.3f},
                                  {-.3f, -.3f, .3f},  {.3f, -.3f, .3f},  {.3f, .3f, .3f},  {-.3f, .3f, .3f}};
    const uint32_t  indices[]  = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
                                  3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5};
    auto            result     = buildVolumeFluidMeshDistribution(vertices, indices, {1, 1, 1}, .1f);
    REQUIRE(result.ok());
    REQUIRE(!result.value().empty());
    bool interior = false;
    for (const auto& p : result.value()) {
        if (glm::length(p.position) < .09f) interior = true;
        REQUIRE(glm::length(p.position) < .7f);
    }
    REQUIRE(interior);
    auto mirrored = buildVolumeFluidMeshDistribution(vertices, indices, {-1, 1, 1}, .1f);
    REQUIRE(mirrored.ok());
    REQUIRE(mirrored.value().size() == result.value().size());
    const uint32_t invalid[] = {0, 1, 99};
    REQUIRE(!buildVolumeFluidMeshDistribution(vertices, invalid, {1, 1, 1}, .1f).ok());
    REQUIRE(!buildVolumeFluidMeshDistribution(vertices, indices, {0, 1, 1}, .1f).ok());
}
TEST_CASE("fluids.volumeEmitter.meshOpenSurface") {
    const glm::vec3 vertices[] = {{-.2f, -.2f, 0}, {.2f, -.2f, 0}, {.2f, .2f, 0}, {-.2f, .2f, 0}};
    const uint32_t  indices[]  = {0, 1, 2, 0, 2, 3};
    auto            result     = buildVolumeFluidMeshDistribution(vertices, indices, {1, 1, 1}, .1f);
    REQUIRE(result.ok());
    REQUIRE(!result.value().empty());
    for (const auto& p : result.value()) REQUIRE(std::abs(p.position.z) < .00001f);
}

#include <cmath>
TEST_CASE("fluids.volumeEmitter.meshTorusPreservesHole") {
    std::vector<glm::vec3> vertices;
    std::vector<uint32_t>  indices;
    constexpr unsigned     rings = 24, sides = 12;
    for (unsigned u = 0; u < rings; ++u)
        for (unsigned v = 0; v < sides; ++v) {
            const float a = 6.28318530718f * float(u) / rings, b = 6.28318530718f * float(v) / sides;
            const float radius = .3f + .12f * std::cos(b);
            vertices.push_back({radius * std::cos(a), .12f * std::sin(b), radius * std::sin(a)});
        }
    for (unsigned u = 0; u < rings; ++u)
        for (unsigned v = 0; v < sides; ++v) {
            const uint32_t a = u * sides + v, b = ((u + 1) % rings) * sides + v,
                           c = ((u + 1) % rings) * sides + (v + 1) % sides, d = u * sides + (v + 1) % sides;
            indices.insert(indices.end(), {a, b, c, a, c, d});
        }
    auto result = buildVolumeFluidMeshDistribution(vertices, indices, {1, 1, 1}, .04f);
    REQUIRE(result.ok());
    REQUIRE(!result.value().empty());
    bool tubeInterior = false;
    for (const auto& p : result.value()) {
        REQUIRE(std::sqrt(p.position.x * p.position.x + p.position.z * p.position.z) > .12f);
        if (glm::length(p.position - glm::vec3(.3f, 0, 0)) < .05f) tubeInterior = true;
    }
    REQUIRE(tubeInterior);
}
