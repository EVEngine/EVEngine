#include "zeroerr/unittest.h"
#include "zeroerr/assert.h"

#include "stylize/BeamEffect.h"

#include <glm/geometric.hpp>

using namespace eve::stylize;

TEST_CASE("stylize.beamEffect.buildsExpectedRibbonTopology") {
    BeamEffectConfig config;
    config.segments = 8;
    config.noiseAmplitude = 0.f;
    const auto beam = buildBeamEffect({0.f, 0.f, 0.f}, {0.f, 0.f, 4.f},
                                      {0.f, -1.f, 0.f}, config);
    REQUIRE_EQ(static_cast<int>(beam.status), static_cast<int>(BeamBuildStatus::Built));
    REQUIRE_EQ(beam.mesh.vertices.size(), 18u);
    REQUIRE_EQ(beam.mesh.indices.size(), 48u);
    REQUIRE(glm::length(beam.mesh.vertices.front().position -
                        beam.mesh.vertices[1].position) > 0.11f);
}

TEST_CASE("stylize.beamEffect.noiseAndBranchesAreDeterministic") {
    BeamEffectConfig config;
    config.segments = 10;
    config.noiseAmplitude = 0.2f;
    config.randomSeed = 91;
    config.branchCount = 3;
    config.branchSegments = 4;
    const auto first = buildBeamEffect({1.f, 2.f, 3.f}, {5.f, 4.f, 9.f},
                                       {0.f, 0.f, -1.f}, config);
    const auto second = buildBeamEffect({1.f, 2.f, 3.f}, {5.f, 4.f, 9.f},
                                        {0.f, 0.f, -1.f}, config);
    REQUIRE_EQ(first.mesh.vertices.size(), 52u);
    REQUIRE_EQ(first.mesh.indices.size(), 132u);
    REQUIRE_EQ(first.mesh.vertices.size(), second.mesh.vertices.size());
    for (std::size_t i = 0; i < first.mesh.vertices.size(); ++i) {
        REQUIRE_EQ(first.mesh.vertices[i].position, second.mesh.vertices[i].position);
        REQUIRE_EQ(first.mesh.vertices[i].alpha, second.mesh.vertices[i].alpha);
    }
}

TEST_CASE("stylize.beamEffect.rejectsDegenerateAndInvalidInputs") {
    const auto degenerate = buildBeamEffect({1.f, 1.f, 1.f}, {1.f, 1.f, 1.f},
                                            {0.f, 0.f, -1.f});
    REQUIRE_EQ(static_cast<int>(degenerate.status),
               static_cast<int>(BeamBuildStatus::DegenerateInput));
    REQUIRE(degenerate.mesh.vertices.empty());

    BeamEffectConfig invalid;
    invalid.widthStart = -1.f;
    const auto rejected = buildBeamEffect({0.f, 0.f, 0.f}, {1.f, 0.f, 0.f},
                                          {0.f, 0.f, -1.f}, invalid);
    REQUIRE_EQ(static_cast<int>(rejected.status),
               static_cast<int>(BeamBuildStatus::InvalidConfig));
}
