#include <array>
#include <glm/geometric.hpp>
#include "stylize/BeamEffect.h"
#include "stylize/BeamSkillEffect.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// stylize_beam_effect.cpp
using namespace eve::stylize;

TEST_CASE("stylize.beamEffect.buildsExpectedRibbonTopology") {
    BeamEffectConfig config;
    config.segments       = 8;
    config.noiseAmplitude = 0.f;
    const auto beam       = buildBeamEffect({0.f, 0.f, 0.f}, {0.f, 0.f, 4.f}, {0.f, -1.f, 0.f}, config);
    REQUIRE_EQ(static_cast<int>(beam.status), static_cast<int>(BeamBuildStatus::Built));
    REQUIRE_EQ(beam.mesh.vertices.size(), 18u);
    REQUIRE_EQ(beam.mesh.indices.size(), 48u);
    REQUIRE(glm::length(beam.mesh.vertices.front().position - beam.mesh.vertices[1].position) > 0.11f);
}

TEST_CASE("stylize.beamEffect.noiseAndBranchesAreDeterministic") {
    BeamEffectConfig config;
    config.segments       = 10;
    config.noiseAmplitude = 0.2f;
    config.randomSeed     = 91;
    config.branchCount    = 3;
    config.branchSegments = 4;
    const auto first      = buildBeamEffect({1.f, 2.f, 3.f}, {5.f, 4.f, 9.f}, {0.f, 0.f, -1.f}, config);
    const auto second     = buildBeamEffect({1.f, 2.f, 3.f}, {5.f, 4.f, 9.f}, {0.f, 0.f, -1.f}, config);
    REQUIRE_EQ(first.mesh.vertices.size(), 52u);
    REQUIRE_EQ(first.mesh.indices.size(), 132u);
    REQUIRE_EQ(first.mesh.vertices.size(), second.mesh.vertices.size());
    for (std::size_t i = 0; i < first.mesh.vertices.size(); ++i) {
        REQUIRE_EQ(first.mesh.vertices[i].position, second.mesh.vertices[i].position);
        REQUIRE_EQ(first.mesh.vertices[i].alpha, second.mesh.vertices[i].alpha);
    }
}

TEST_CASE("stylize.beamEffect.rejectsDegenerateAndInvalidInputs") {
    const auto degenerate = buildBeamEffect({1.f, 1.f, 1.f}, {1.f, 1.f, 1.f}, {0.f, 0.f, -1.f});
    REQUIRE_EQ(static_cast<int>(degenerate.status), static_cast<int>(BeamBuildStatus::DegenerateInput));
    REQUIRE(degenerate.mesh.vertices.empty());

    BeamEffectConfig invalid;
    invalid.widthStart  = -1.f;
    const auto rejected = buildBeamEffect({0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 0.f, -1.f}, invalid);
    REQUIRE_EQ(static_cast<int>(rejected.status), static_cast<int>(BeamBuildStatus::InvalidConfig));
}

// stylize_beam_skill_effect.cpp
using namespace eve::stylize;

TEST_CASE("stylize.beamSkill.presetsExposeProductionDefaults") {
    BeamSkillEffect laser(BeamSkillPreset::Laser);
    BeamSkillEffect lightning(BeamSkillPreset::Lightning);
    REQUIRE(laser.geometry().noiseAmplitude == 0.f);
    REQUIRE(lightning.geometry().noiseAmplitude > 0.f);
    REQUIRE(lightning.geometry().branchCount > 0u);
    REQUIRE(laser.effect().playback().loop);
    REQUIRE(!lightning.effect().playback().loop);
}

TEST_CASE("stylize.beamSkill.chainMergesSegmentsWithRebasedIndices") {
    BeamSkillEffect  chain(BeamSkillPreset::ChainLightning);
    const std::array points{glm::vec3(0.f, 0.f, 0.f), glm::vec3(1.f, 1.f, 0.f), glm::vec3(2.f, 0.f, 1.f),
                            glm::vec3(3.f, 1.f, 1.f)};
    chain.setPath(points);
    const auto built = chain.build({0.f, 0.f, -1.f});
    REQUIRE_EQ(static_cast<int>(built.status), static_cast<int>(BeamBuildStatus::Built));
    REQUIRE(!built.mesh.vertices.empty());
    REQUIRE(!built.mesh.indices.empty());
    for (const auto index : built.mesh.indices) REQUIRE(index < built.mesh.vertices.size());
}

TEST_CASE("stylize.beamSkill.lifecycleControlsSubmissionEnvelope") {
    BeamSkillEffect lightning(BeamSkillPreset::Lightning);
    REQUIRE_EQ(static_cast<int>(lightning.effect().state()), static_cast<int>(MeshEffectState::Stopped));
    lightning.play();
    lightning.update(0.05f);
    REQUIRE(lightning.effect().intensity() > 0.f);
    lightning.stop(0.f);
    REQUIRE_EQ(static_cast<int>(lightning.effect().state()), static_cast<int>(MeshEffectState::Finished));
}

TEST_CASE("stylize.beamSkill.rejectsMissingPath") {
    BeamSkillEffect laser(BeamSkillPreset::Laser);
    const auto      built = laser.build({0.f, 0.f, -1.f});
    REQUIRE_EQ(static_cast<int>(built.status), static_cast<int>(BeamBuildStatus::DegenerateInput));
}
