#include "zeroerr/unittest.h"
#include "zeroerr/assert.h"

#include "stylize/BeamSkillEffect.h"

#include <array>

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
    BeamSkillEffect chain(BeamSkillPreset::ChainLightning);
    const std::array points{glm::vec3(0.f, 0.f, 0.f), glm::vec3(1.f, 1.f, 0.f),
                            glm::vec3(2.f, 0.f, 1.f), glm::vec3(3.f, 1.f, 1.f)};
    chain.setPath(points);
    const auto built = chain.build({0.f, 0.f, -1.f});
    REQUIRE_EQ(static_cast<int>(built.status), static_cast<int>(BeamBuildStatus::Built));
    REQUIRE(!built.mesh.vertices.empty());
    REQUIRE(!built.mesh.indices.empty());
    for (const auto index : built.mesh.indices) REQUIRE(index < built.mesh.vertices.size());
}

TEST_CASE("stylize.beamSkill.lifecycleControlsSubmissionEnvelope") {
    BeamSkillEffect lightning(BeamSkillPreset::Lightning);
    REQUIRE_EQ(static_cast<int>(lightning.effect().state()),
               static_cast<int>(MeshEffectState::Stopped));
    lightning.play();
    lightning.update(0.05f);
    REQUIRE(lightning.effect().intensity() > 0.f);
    lightning.stop(0.f);
    REQUIRE_EQ(static_cast<int>(lightning.effect().state()),
               static_cast<int>(MeshEffectState::Finished));
}

TEST_CASE("stylize.beamSkill.rejectsMissingPath") {
    BeamSkillEffect laser(BeamSkillPreset::Laser);
    const auto built = laser.build({0.f, 0.f, -1.f});
    REQUIRE_EQ(static_cast<int>(built.status),
               static_cast<int>(BeamBuildStatus::DegenerateInput));
}
