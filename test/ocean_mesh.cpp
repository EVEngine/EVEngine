#include "fluids/OceanMesh.h"
#include "fluids/OceanWaterView.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

using namespace eve::fluids;

TEST_CASE("fluids.ocean.meshHasNoInteriorBoundary") {
    OceanMeshBuilder builder;
    builder.settings = {32, 16, 0.5, 2000.0};
    auto mesh        = builder.build();
    REQUIRE(mesh.ok());
    const auto& data = mesh.value();
    CHECK(data.parameterOffsets.size() == 513);
    CHECK(data.radii.front() == 0.5);
    CHECK(data.radii.back() == 2000.0);
    std::map<std::pair<unsigned, unsigned>, std::pair<unsigned, int>> edges;
    double                                                            area = 0.0;
    for (std::size_t i = 0; i < data.indices.size(); i += 3) {
        const auto a = data.indices[i], b = data.indices[i + 1], c = data.indices[i + 2];
        REQUIRE(a < data.parameterOffsets.size());
        REQUIRE(b < data.parameterOffsets.size());
        REQUIRE(c < data.parameterOffsets.size());
        const auto   ab         = data.parameterOffsets[b] - data.parameterOffsets[a];
        const auto   ac         = data.parameterOffsets[c] - data.parameterOffsets[a];
        const double upwardArea = ab.y * ac.x - ab.x * ac.y;
        REQUIRE(upwardArea > 0.0);
        area += upwardArea * 0.5;
        for (auto [u, v] : {std::pair(a, b), std::pair(b, c), std::pair(c, a)}) {
            auto& edge = edges[std::minmax(u, v)];
            ++edge.first;
            edge.second += u < v ? 1 : -1;
        }
    }
    unsigned boundary = 0;
    for (const auto& [vertices, edge] : edges) {
        if (edge.first == 1) {
            ++boundary;
            CHECK(vertices.first > 1 + 14 * 32);
            CHECK(vertices.second > 1 + 14 * 32);
        } else {
            CHECK(edge.first == 2);
            CHECK(edge.second == 0);
        }
    }
    CHECK(boundary == 32);
    CHECK(std::abs(area - 16000000.0) < 1e-6);
    // Disk Euler characteristic: one connected, hole-free surface.
    CHECK(static_cast<long>(data.parameterOffsets.size()) - static_cast<long>(edges.size()) +
              static_cast<long>(data.indices.size() / 3) ==
          1);
    for (std::size_t i = 1; i < data.radii.size(); ++i) CHECK(data.radii[i] > data.radii[i - 1]);
}

TEST_CASE("fluids.ocean.meshQualityAndValidation") {
    OceanMeshBuilder builder;
    auto             standard = builder.build();
    REQUIRE(standard.ok());
    CHECK(standard.value().parameterOffsets.size() == 32769);
    CHECK(standard.value().indices.size() / 3 == 65280);
    CHECK(standard.value().radii.back() == 20000.0);
    builder.settings.segments = 31;
    CHECK(!builder.build().ok());
    builder.settings       = {};
    builder.settings.rings = 0;
    CHECK(!builder.build().ok());
    builder.settings             = {};
    builder.settings.outerRadius = std::numeric_limits<double>::infinity();
    CHECK(!builder.build().ok());
    builder.settings = {16, 2, 1.0, 2.0};
    auto minimal     = builder.build();
    REQUIRE(minimal.ok());
    CHECK(minimal.value().radii.front() == 1.0);
    CHECK(minimal.value().radii.back() == 2.0);
}

TEST_CASE("fluids.ocean.meshFloatingOriginMatchesPhysics") {
    OceanMeshBuilder topology;
    topology.settings = {16, 8, 0.5, 64.0};
    auto mesh         = topology.build();
    REQUIRE(mesh.ok());
    OceanSpectrumBuilder sea;
    sea.settings.bands                 = {{16, 64.0, 8.0, 64.0}};
    sea.settings.significantWaveHeight = 0.3;
    auto spectrum                      = sea.build();
    REQUIRE(spectrum.ok());
    auto frame = spectrum.value().frameBuilder(7.25).build();
    REQUIRE(frame.ok());
    OceanWaterView        water(frame.value());
    OceanMeshFrameBuilder render(mesh.value(), water);
    render.center = {100000000.25, -100000000.5};
    render.origin = {100000000.0, 0.0, -100000000.0};
    auto vertices = render.build();
    REQUIRE(vertices.ok());
    CHECK(vertices.value().time == 7.25);
    for (std::size_t i = 0; i < mesh.value().parameterOffsets.size(); ++i) {
        glm::dvec3 displayed(vertices.value().positions[i * 3], vertices.value().positions[i * 3 + 1],
                             vertices.value().positions[i * 3 + 2]);
        displayed += render.origin;
        auto physics = water.sample({displayed.x, displayed.z});
        REQUIRE(physics.ok());
        CHECK(std::abs(displayed.y - physics.value().height) < 2e-5);
        const glm::dvec3 normal(vertices.value().normals[i * 3], vertices.value().normals[i * 3 + 1],
                                vertices.value().normals[i * 3 + 2]);
        CHECK(glm::length(normal - physics.value().normal) < 2e-5);
    }
    // Changing render origin changes only coordinates, never the simulated surface.
    render.origin += glm::dvec3(32.0, 4.0, -16.0);
    auto shifted = render.build();
    REQUIRE(shifted.ok());
    for (std::size_t i = 0; i < mesh.value().parameterOffsets.size(); ++i) {
        CHECK(std::abs(shifted.value().positions[i * 3] + 32.0 - vertices.value().positions[i * 3]) < 1e-5);
        CHECK(std::abs(shifted.value().positions[i * 3 + 1] + 4.0 - vertices.value().positions[i * 3 + 1]) < 1e-5);
        CHECK(std::abs(shifted.value().positions[i * 3 + 2] - 16.0 - vertices.value().positions[i * 3 + 2]) < 1e-5);
    }
    render.center.x = std::numeric_limits<double>::quiet_NaN();
    CHECK(!render.build().ok());
}
