// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "module": "animation",
  "modules": [
    "action",
    "animation"
  ],
  "origins": [
    "action_upper_body_orientation.cpp"
  ]
}
*/
// clang-format on

#include "action/ActionTimeline.h"
#include "animation/AnimClip.h"
#include "animation/AnimLayerMixer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/MontageCoordinator.h"
#include "animation/UpperBodyOrientationCompensator.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <memory>

namespace {

eve::LogicalId orientationId(const char* value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

eve::action::ActionTimeline orientationTimeline(const char* uri) {
    eve::action::ActionTimeline timeline;
    timeline.actionId = orientationId("combat:upper-body-orientation");
    timeline.duration = eve::Duration::fromSeconds(1.0).takeValue();
    eve::action::ActionAnimationSection section;
    section.id           = orientationId("montage-section:upper-body-orientation");
    section.animationUri = uri;
    section.end          = timeline.duration;
    timeline.animationSections.push_back(std::move(section));
    return timeline;
}

std::vector<eve::animation::MontageClipAsset> orientationClips(const char* uri, float hipsAngle,
                                                               float spineAngle) {
    auto clip = std::make_unique<eve::animation::AnimClip>(uri);
    clip->setDuration(1.0f);
    clip->setLoop(false);
    clip->addRotationKey(1, 0.0f, 0.0f, std::sin(hipsAngle * 0.5f), 0.0f, std::cos(hipsAngle * 0.5f));
    clip->addRotationKey(2, 0.0f, 0.0f, std::sin(spineAngle * 0.5f), 0.0f, std::cos(spineAngle * 0.5f));
    std::vector<eve::animation::MontageClipAsset> result;
    result.push_back({uri, std::move(clip)});
    return result;
}

eve::action::ActionAdvance activeAdvance(eve::action::ActionExecutionId id) {
    eve::action::ActionAdvance advance;
    advance.id           = id;
    advance.phase        = eve::action::ActionPhase::Active;
    advance.totalElapsed = eve::Duration::fromSeconds(0.5).takeValue();
    return advance;
}

}  // namespace

TEST_CASE("actionMontage.upperBodyProfileReplacementIsTransactional") {
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("hips", 0);
    skeleton.addBone("spine", 1);
    eve::animation::UpperBodyOrientationCompensator compensator(skeleton);

    REQUIRE(compensator.configure({"root", "hips", "spine"}).ok());
    CHECK(compensator.isConfigured());
    auto missing = compensator.configure({"root", "missing", "spine"});
    REQUIRE(!missing.ok());
    CHECK(compensator.isConfigured());

    eve::animation::AnimPose accumulated(3), sampled(3);
    skeleton.applyBindPose(&accumulated);
    skeleton.applyBindPose(&sampled);
    accumulated.setLocalRotation(1, 0.0f, std::sin(0.4f), 0.0f, std::cos(0.4f));
    sampled.setLocalRotation(2, 0.0f, std::sin(0.2f), 0.0f, std::cos(0.2f));
    REQUIRE(compensator.apply(accumulated, sampled, 1.0f).ok());
    accumulated.computeWorld(&skeleton);
    CHECK(std::fabs(accumulated.world(2).qy - std::sin(0.2f)) < 1e-5f);
    CHECK(std::fabs(accumulated.world(2).qw - std::cos(0.2f)) < 1e-5f);
}

TEST_CASE("actionMontage.upperBodyCompensationObservesProviderMaskAndFullBodyPolicy") {
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("hips", 0);
    skeleton.addBone("spine", 1);
    eve::animation::MontageCoordinator coordinator(skeleton);
    REQUIRE(coordinator.setLayerRole(0, eve::animation::MontageLayerRole::UpperBody).ok());

    eve::animation::AnimBoneMask upperMask(&skeleton);
    REQUIRE(upperMask.setBoneAndChildren("spine", 1.0f));
    REQUIRE(coordinator.setLayerProceduralUpperBodyMask(0, upperMask).ok());
    auto maskSource = coordinator.layerBoneMaskSource(0);
    REQUIRE(maskSource.ok());
    CHECK_EQ(maskSource.value(), eve::animation::MontageBoneMaskSource::ProceduralUpperBody);

    const auto upperId = eve::action::ActionExecutionId(801);
    auto upper = coordinator.play(0, orientationTimeline("memory://upper"), orientationClips("memory://upper", 0.0f, 0.2f),
                                  upperId, eve::SimulationTick(1));
    REQUIRE(upper.ok());
    REQUIRE(coordinator.present(upper.value(), activeAdvance(upperId), eve::SimulationTick(2)).ok());

    eve::animation::AnimPose base(3);
    skeleton.applyBindPose(&base);
    base.setLocalRotation(1, 0.0f, std::sin(0.4f), 0.0f, std::cos(0.4f));
    REQUIRE(coordinator.compose(base).ok());
    CHECK_EQ(coordinator.upperBodyOrientationStatus(), eve::animation::UpperBodyOrientationStatus::Unconfigured);

    REQUIRE(coordinator.configureUpperBodyOrientation({"root", "hips", "spine"}).ok());
    auto corrected = coordinator.compose(base);
    REQUIRE(corrected.ok());
    CHECK_EQ(coordinator.upperBodyOrientationStatus(), eve::animation::UpperBodyOrientationStatus::Applied);
    CHECK(std::fabs(corrected.value().get().world(2).qy - std::sin(0.2f)) < 1e-5f);

    REQUIRE(coordinator.setLayerBoneMask(0, upperMask).ok());
    REQUIRE(coordinator.compose(base).ok());
    CHECK_EQ(coordinator.upperBodyOrientationStatus(),
             eve::animation::UpperBodyOrientationStatus::BypassedCustomMask);

    REQUIRE(coordinator.setLayerProceduralUpperBodyMask(0, upperMask).ok());
    REQUIRE(coordinator.setLayerRole(1, eve::animation::MontageLayerRole::FullBody).ok());
    const auto fullId = eve::action::ActionExecutionId(802);
    auto full = coordinator.play(1, orientationTimeline("memory://full"), orientationClips("memory://full", 0.0f, 0.5f),
                                 fullId, eve::SimulationTick(3));
    REQUIRE(full.ok());
    REQUIRE(coordinator.present(full.value(), activeAdvance(fullId), eve::SimulationTick(4)).ok());
    REQUIRE(coordinator.compose(base).ok());
    CHECK_EQ(coordinator.upperBodyOrientationStatus(),
             eve::animation::UpperBodyOrientationStatus::BypassedFullBody);
}

TEST_CASE("actionMontage.upperBodyCompensationRunsBeforeAdditiveLayersDeterministically") {
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("hips", 0);
    skeleton.addBone("spine", 1);
    eve::animation::MontageCoordinator coordinator(skeleton);
    REQUIRE(coordinator.configureUpperBodyOrientation({"root", "hips", "spine"}).ok());
    REQUIRE(coordinator.setLayerRole(0, eve::animation::MontageLayerRole::UpperBody).ok());
    eve::animation::AnimBoneMask upperMask(&skeleton);
    REQUIRE(upperMask.setBoneAndChildren("spine", 1.0f));
    REQUIRE(coordinator.setLayerProceduralUpperBodyMask(0, upperMask).ok());
    const auto upperId = eve::action::ActionExecutionId(811);
    auto upper = coordinator.play(0, orientationTimeline("memory://upper-order"),
                                  orientationClips("memory://upper-order", 0.0f, 0.2f), upperId,
                                  eve::SimulationTick(1));
    REQUIRE(upper.ok());
    REQUIRE(coordinator.present(upper.value(), activeAdvance(upperId), eve::SimulationTick(2)).ok());

    REQUIRE(coordinator.setLayerRole(1, eve::animation::MontageLayerRole::Additive).ok());
    REQUIRE(coordinator.setLayerBoneMask(1, upperMask).ok());
    const auto additiveId = eve::action::ActionExecutionId(812);
    auto additive = coordinator.play(1, orientationTimeline("memory://additive-order"),
                                     orientationClips("memory://additive-order", 0.0f, 0.1f), additiveId,
                                     eve::SimulationTick(3));
    REQUIRE(additive.ok());
    REQUIRE(coordinator.present(additive.value(), activeAdvance(additiveId), eve::SimulationTick(4)).ok());

    eve::animation::AnimPose base(3);
    skeleton.applyBindPose(&base);
    base.setLocalRotation(1, 0.0f, std::sin(0.4f), 0.0f, std::cos(0.4f));
    auto first = coordinator.compose(base);
    REQUIRE(first.ok());
    const float qy = first.value().get().world(2).qy;
    CHECK(std::fabs(qy - std::sin(0.3f)) < 1e-5f);
    auto second = coordinator.compose(base);
    REQUIRE(second.ok());
    CHECK_EQ(second.value().get().world(2).qy, qy);
}
