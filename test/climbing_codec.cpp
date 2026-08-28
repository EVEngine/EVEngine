#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "climbing/ClimbingCodec.h"

#include <cmath>

namespace {

eve::climbing::ClimbingActionDefinition action(std::string id) {
    eve::climbing::ClimbingActionDefinition result;
    result.id                     = std::move(id);
    result.kind                   = eve::climbing::ClimbingActionKind::Vault;
    result.minHeight              = 0.3f;
    result.maxHeight              = 1.1f;
    result.duration               = eve::Duration::fromNanoseconds(450000000);
    result.requiredNotifies       = {"contact.left_hand", "land"};
    result.tags                   = {"parkour", "vault"};
    result.sourceModes            = eve::climbing::ClimbingSourceMode::Grounded;
    result.requiredCommand        = eve::climbing::ClimbingCommandRequirement::Jump;
    result.probeRecipe            = eve::climbing::ClimbingProbeRecipe::Obstacle;
    result.minDepth               = 0.2f;
    result.maxDepth               = 1.4f;
    result.minDistance            = 0.1f;
    result.maxDistance            = 1.5f;
    result.minSurfaceNormalY      = -0.2f;
    result.maxSlopeRadians        = 1.2f;
    result.maxCurvature           = 0.3f;
    result.requiredSupportTags    = {"solid"};
    result.trajectory             = eve::climbing::ClimbingTrajectoryKind::BallisticArc;
    result.animation              = {"anim:vault_low", "climbing.vault", "Root", false};
    result.branchWindows          = {{0.45f, 0.7f, "combo:mantle"}};
    result.contactConstraints     = {{eve::climbing::ClimbingContactTarget::LeftHand, 0.2f, 0.6f, 0.9f}};
    result.landingPolicy          = eve::climbing::ClimbingLandingPolicy::PreserveMomentum;
    result.terminalVelocityPolicy = eve::climbing::ClimbingTerminalVelocityPolicy::ClampDownward;
    result.comboTags              = {"combo:flow", "combo:mantle"};
    result.repetitionPenalty      = 125;
    result.requiredConditionTags  = {"ability:parkour"};
    result.staminaCost            = 7.5f;
    result.cameraCue              = "camera:vault";
    result.eventMetadata          = {"event:parkour"};
    result.extensionMetadata      = {{"plugin.cameraCue", eve::Value("close_follow")}};
    return result;
}

}  // namespace

TEST_CASE("climbing.codec.profileRoundTripPreservesUnknownFields") {
    eve::climbing::ClimbingProfileDefinition profile;
    profile.inputBufferTicks       = 9;
    profile.coyoteTicks            = 7;
    profile.pathValidationSegments = 18;
    profile.maxPelvisDeviation     = 0.28f;
    profile.groundAcceleration     = 31.f;
    profile.groundBraking          = 35.f;
    profile.airControl             = 0.55f;
    profile.gravity                = 26.f;
    profile.jumpSpeed              = 7.4f;
    profile.probeSectors           = 12;
    profile.maxCandidates          = 5;
    profile.autoAssistStrength     = 0.7f;
    profile.scoreWeights           = {900, 800, 700, 600, 500, 400, 300};
    profile.maxTotalWarpBudget     = 1.35f;
    profile.defaultActionIds       = {"parkour:vault_low"};
    profile.allowedActionTags      = {"parkour"};
    profile.deniedActionTags       = {"disabled"};
    profile.staminaPolicy          = eve::climbing::ClimbingStaminaPolicy::UseWhenPresent;
    profile.staminaAdapter         = "attributes:stamina";
    profile.cameraCueProfile       = "camera:climbing";
    auto vault                     = action("parkour:vault_low");
    vault.rootMotionScaleMin       = 0.8f;
    vault.rootMotionScaleMax       = 1.2f;
    vault.warpWindows              = {{0.1f, 0.45f, true, false, true}, {0.6f, 0.9f, false, true, false}};
    profile.actions.push_back(vault);
    profile.extensionMetadata.emplace("studio.balanceRevision", eve::Value(std::int64_t(7)));

    auto encoded = eve::climbing::encodeClimbingProfileDefinition(profile);
    REQUIRE(encoded.ok());
    auto decoded = eve::climbing::decodeClimbingProfileDefinition(encoded.value());
    REQUIRE(decoded.ok());
    REQUIRE_EQ(decoded.value().actions.size(), std::size_t{1});
    CHECK_EQ(decoded.value().actions.front().id, std::string("parkour:vault_low"));
    CHECK(decoded.value().extensionMetadata.contains("studio.balanceRevision"));
    CHECK(decoded.value().actions.front().extensionMetadata.contains("plugin.cameraCue"));
    CHECK_EQ(decoded.value().inputBufferTicks, std::uint64_t(9));
    CHECK_EQ(decoded.value().coyoteTicks, std::uint64_t(7));
    CHECK_EQ(decoded.value().pathValidationSegments, std::uint32_t(18));
    CHECK(std::fabs(decoded.value().maxPelvisDeviation - 0.28f) < 0.0001f);
    CHECK_EQ(decoded.value().probeSectors, std::uint32_t(12));
    CHECK_EQ(decoded.value().maxCandidates, std::uint32_t(5));
    CHECK_EQ(decoded.value().cameraCueProfile, std::string("camera:climbing"));
    CHECK_EQ(decoded.value().actions.front().cameraCue, std::string("camera:vault"));
    CHECK_EQ(decoded.value().actions.front().animation.clipId, std::string("anim:vault_low"));
    CHECK_EQ(decoded.value().actions.front().branchWindows.front().comboTag, std::string("combo:mantle"));
    CHECK_EQ(decoded.value().actions.front().contactConstraints.size(), std::size_t(1));
    REQUIRE_EQ(decoded.value().actions.front().warpWindows.size(), std::size_t(2));
    CHECK(decoded.value().actions.front().warpWindows.front().horizontal);
    CHECK(!decoded.value().actions.front().warpWindows.front().vertical);

    auto reencoded = eve::climbing::encodeClimbingProfileDefinition(decoded.value());
    REQUIRE(reencoded.ok());
    CHECK(reencoded.value() == encoded.value());
}

TEST_CASE("climbing.codec.migratesV3ActionWithExplicitV4Defaults") {
    auto encoded = eve::climbing::encodeClimbingActionDefinition(action("parkour:vault_legacy"));
    REQUIRE(encoded.ok());
    auto* object = encoded.value().getIf<eve::Value::Object>();
    REQUIRE(object != nullptr);
    object->at("schemaVersion")             = eve::Value(std::int64_t(3));
    const std::vector<std::string> v4Fields = {"tags",
                                               "sourceModes",
                                               "requiredCommand",
                                               "probeRecipe",
                                               "minDepth",
                                               "maxDepth",
                                               "minDistance",
                                               "maxDistance",
                                               "minSurfaceNormalY",
                                               "maxSlopeRadians",
                                               "maxCurvature",
                                               "requiredSupportTags",
                                               "trajectory",
                                               "animation",
                                               "branchWindows",
                                               "contactConstraints",
                                               "landingPolicy",
                                               "terminalVelocityPolicy",
                                               "comboTags",
                                               "repetitionPenalty",
                                               "requiredConditionTags",
                                               "staminaCost",
                                               "cameraCue",
                                               "eventMetadata"};
    for (const std::string& field : v4Fields) object->erase(field);

    auto decoded = eve::climbing::decodeClimbingActionDefinition(encoded.value());
    REQUIRE(decoded.ok());
    CHECK(decoded.value().tags.empty());
    CHECK_EQ(static_cast<int>(decoded.value().sourceModes), static_cast<int>(eve::climbing::ClimbingSourceMode::Any));
    CHECK_EQ(decoded.value().animation.rootBone, std::string("Root"));
}

TEST_CASE("climbing.codec.rejectsUnknownVersionAndInvalidKnownFields") {
    auto encoded = eve::climbing::encodeClimbingActionDefinition(action("parkour:vault_low"));
    REQUIRE(encoded.ok());

    eve::Value future = encoded.value();
    future.set("schemaVersion", eve::Value(std::int64_t(eve::climbing::ClimbingActionDefinition::SchemaVersion + 1)));
    auto unknown = eve::climbing::decodeClimbingActionDefinition(future);
    CHECK(!unknown.ok());
    CHECK_EQ(static_cast<int>(unknown.error()->code()), static_cast<int>(eve::DiagnosticCode::UnknownVersion));

    eve::Value malformed = encoded.value();
    malformed.set("maxHeight", eve::Value(-1.0));
    auto invalid = eve::climbing::decodeClimbingActionDefinition(malformed);
    CHECK(!invalid.ok());
    CHECK_EQ(static_cast<int>(invalid.code()), static_cast<int>(eve::StatusCode::Rejected));
}

TEST_CASE("climbing.codec.duplicateActionsRejectWholeProfileCandidate") {
    eve::climbing::ClimbingProfileDefinition profile;
    profile.actions.push_back(action("parkour:vault_low"));
    profile.actions.push_back(action("parkour:vault_low"));
    auto encoded = eve::climbing::encodeClimbingProfileDefinition(profile);
    CHECK(!encoded.ok());
    CHECK_EQ(static_cast<int>(encoded.code()), static_cast<int>(eve::StatusCode::Conflict));
}

TEST_CASE("climbing.codec.rejectsInvalidInputTickWindows") {
    eve::climbing::ClimbingProfileDefinition profile;
    profile.inputBufferTicks = 0;
    auto encoded             = eve::climbing::encodeClimbingProfileDefinition(profile);
    CHECK(!encoded.ok());
    CHECK_EQ(static_cast<int>(encoded.code()), static_cast<int>(eve::StatusCode::Rejected));
}

TEST_CASE("climbing.codec.rejectsOverlappingAndChannelLessWarpWindows") {
    auto overlapping        = action("parkour:vault_overlap");
    overlapping.warpWindows = {{0.f, 0.6f, true, false, false}, {0.5f, 1.f, false, true, false}};
    auto overlapResult      = eve::climbing::encodeClimbingActionDefinition(overlapping);
    CHECK(!overlapResult.ok());
    CHECK_EQ(static_cast<int>(overlapResult.code()), static_cast<int>(eve::StatusCode::Rejected));

    auto channelLess        = action("parkour:vault_empty_window");
    channelLess.warpWindows = {{0.f, 1.f, false, false, false}};
    auto channelResult      = eve::climbing::encodeClimbingActionDefinition(channelLess);
    CHECK(!channelResult.ok());
    CHECK_EQ(static_cast<int>(channelResult.code()), static_cast<int>(eve::StatusCode::Rejected));
}
