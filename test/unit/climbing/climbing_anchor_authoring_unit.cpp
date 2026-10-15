#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include "climbing/ClimbingAnchorAuthoring.h"
#include "climbing/ClimbingCodec.h"
#include "climbing/ClimbingTelemetry.h"
#include "common/Runtime.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// climbing_anchor_authoring.cpp
namespace {

[[maybe_unused]] eve::climbing::ClimbingAnchorBakeRequest request() {
    eve::climbing::ClimbingAnchorBakeRequest value;
    value.graphId                  = "playground.route";
    value.sourceGeometryContentId  = "sha256:playground-geometry";
    value.settings.handSpacing     = 0.4f;
    value.settings.hangingFeetDrop = 1.4f;
    eve::climbing::ClimbingLedgeBakeSource ledge;
    ledge.id           = "wall";
    ledge.points       = {{0.f, 2.f, 0.f}, {1.f, 2.f, 0.f}, {1.f, 2.f, 1.f}};
    ledge.localNormals = {{0.f, 0.f, -1.f}, {-1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}};
    ledge.tags         = {"stone"};
    value.ledges.push_back(std::move(ledge));
    value.ladders.push_back({"ladder", {3.f, 0.3f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, -1.f}, 0.3f, 0.5f, 4, {"metal"}});
    return value;
}

}  // namespace

TEST_CASE("climbing.anchorAuthoring.bakeIsCanonicalAndExecutable") {
    auto first  = eve::climbing::bakeClimbingAnchorGraph(request());
    auto second = eve::climbing::bakeClimbingAnchorGraph(request());
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value().ledgeNodeCount, std::uint32_t(3));
    CHECK_EQ(first.value().ladderNodeCount, std::uint32_t(4));
    CHECK_EQ(first.value().graph.buildSettingsHash, second.value().graph.buildSettingsHash);

    auto firstEncoded  = eve::climbing::encodeClimbingAnchorGraphDefinition(first.value().graph);
    auto secondEncoded = eve::climbing::encodeClimbingAnchorGraphDefinition(second.value().graph);
    REQUIRE(firstEncoded.ok());
    REQUIRE(secondEncoded.ok());
    CHECK(firstEncoded.value() == secondEncoded.value());

    bool hasCorner   = false;
    bool hasMount    = false;
    bool hasClimb    = false;
    bool hasDismount = false;
    for (const auto& node : first.value().graph.nodes)
        hasCorner = hasCorner || node.kind == eve::climbing::ClimbingAnchorKind::CornerOuter ||
                    node.kind == eve::climbing::ClimbingAnchorKind::CornerInner;
    for (const auto& edge : first.value().graph.edges) {
        hasMount    = hasMount || edge.kind == eve::climbing::ClimbingAnchorEdgeKind::Mount;
        hasClimb    = hasClimb || edge.kind == eve::climbing::ClimbingAnchorEdgeKind::Climb;
        hasDismount = hasDismount || edge.kind == eve::climbing::ClimbingAnchorEdgeKind::Dismount;
    }
    CHECK(hasCorner);
    CHECK(hasMount);
    CHECK(hasClimb);
    CHECK(hasDismount);
}

TEST_CASE("climbing.anchorAuthoring.hashIgnoresSourceAndTagOrdering") {
    auto                                   canonical = request();
    eve::climbing::ClimbingLedgeBakeSource secondLedge;
    secondLedge.id          = "balcony";
    secondLedge.points      = {{4.f, 1.f, 0.f}, {5.f, 1.f, 0.f}};
    secondLedge.localNormal = {0.f, 0.f, -1.f};
    secondLedge.tags        = {"outdoor", "stone"};
    canonical.ledges.push_back(secondLedge);
    canonical.ladders.front().tags = {"metal", "outdoor"};

    auto permuted = canonical;
    std::reverse(permuted.ledges.begin(), permuted.ledges.end());
    std::reverse(permuted.ledges.front().tags.begin(), permuted.ledges.front().tags.end());
    std::reverse(permuted.ledges.back().tags.begin(), permuted.ledges.back().tags.end());
    std::reverse(permuted.ladders.front().tags.begin(), permuted.ladders.front().tags.end());

    auto first  = eve::climbing::bakeClimbingAnchorGraph(canonical);
    auto second = eve::climbing::bakeClimbingAnchorGraph(permuted);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value().graph.buildSettingsHash, second.value().graph.buildSettingsHash);
    auto firstEncoded  = eve::climbing::encodeClimbingAnchorGraphDefinition(first.value().graph);
    auto secondEncoded = eve::climbing::encodeClimbingAnchorGraphDefinition(second.value().graph);
    REQUIRE(firstEncoded.ok());
    REQUIRE(secondEncoded.ok());
    CHECK(firstEncoded.value() == secondEncoded.value());
}

TEST_CASE("climbing.anchorAuthoring.requestCodecRoundTripsAndRejectsFuture") {
    auto encoded = eve::climbing::encodeClimbingAnchorBakeRequest(request());
    REQUIRE(encoded.ok());
    auto decoded = eve::climbing::decodeClimbingAnchorBakeRequest(encoded.value());
    REQUIRE(decoded.ok());
    auto reencoded = eve::climbing::encodeClimbingAnchorBakeRequest(decoded.value());
    REQUIRE(reencoded.ok());
    CHECK(reencoded.value() == encoded.value());

    eve::Value future = encoded.value();
    future.set("schemaVersion", eve::Value(eve::climbing::ClimbingAnchorBakeRequest::SchemaVersion + 1));
    auto rejected = eve::climbing::decodeClimbingAnchorBakeRequest(future);
    CHECK(!rejected.ok());
    CHECK_EQ(static_cast<int>(rejected.error()->code()), static_cast<int>(eve::DiagnosticCode::UnknownVersion));
}

TEST_CASE("climbing.anchorAuthoring.overlayOwnsCanonicalPrimitives") {
    auto baked = eve::climbing::bakeClimbingAnchorGraph(request());
    REQUIRE(baked.ok());
    auto overlay = eve::climbing::inspectClimbingAnchorGraphAuthoring(baked.value().graph);
    REQUIRE(overlay.ok());
    CHECK_EQ(overlay.value().graphId, std::string("playground.route"));
    CHECK_EQ(overlay.value().nodes.size(), baked.value().graph.nodes.size());
    CHECK_EQ(overlay.value().edges.size(), baked.value().graph.edges.size());
    CHECK_EQ(overlay.value().nodes.front().id, baked.value().graph.nodes.front().id);

    auto invalid                      = baked.value().graph;
    invalid.nodes.front().localNormal = {};
    auto rejected                     = eve::climbing::inspectClimbingAnchorGraphAuthoring(invalid);
    CHECK(!rejected.ok());
    CHECK_EQ(static_cast<int>(rejected.error()->code()), static_cast<int>(eve::DiagnosticCode::InvalidArgument));
}

TEST_CASE("climbing.anchorAuthoring.rejectsInvalidSourceWithoutPartialGraph") {
    auto candidate                        = request();
    candidate.ladders.front().rungSpacing = 0.f;
    auto rejected                         = eve::climbing::bakeClimbingAnchorGraph(candidate);
    CHECK(!rejected.ok());
    CHECK_EQ(static_cast<int>(rejected.error()->code()), static_cast<int>(eve::DiagnosticCode::InvalidArgument));
}

// climbing_codec.cpp
namespace {

[[maybe_unused]] eve::climbing::ClimbingActionDefinition action(std::string id) {
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

// climbing_playground.cpp


// climbing_telemetry.cpp
using namespace eve;
using namespace eve::climbing;

TEST_CASE("climbing.telemetry.fixedRingReportsExactPercentilesAndBudgets") {
    ClimbingTelemetryBuffer buffer;
    for (std::uint64_t index = 1; index <= 100; ++index) {
        ClimbingRuntimeCounters counters;
        counters.workload    = ClimbingWorkload::CandidateProbe;
        counters.queryCount  = static_cast<std::uint32_t>(index % 17);
        counters.queryBudget = ClimbingQueryBudgets::CandidateProbe;
        counters.budgetState = counters.queryCount > counters.queryBudget ? ClimbingQueryBudgetState::Exceeded
                                                                          : ClimbingQueryBudgetState::WithinBudget;
        buffer.record({SimulationTick(index), index * 100, counters});
    }

    const auto summary = buffer.summarize(ClimbingWorkload::CandidateProbe);
    CHECK(summary.sampleCount == 100);
    CHECK(summary.p50Nanoseconds == 5000);
    CHECK(summary.p95Nanoseconds == 9500);
    CHECK(summary.maxQueryCount == 16);
    CHECK(summary.budgetExceededCount == 0);
    CHECK(buffer.samples().size() == 100);
}

TEST_CASE("climbing.telemetry.ringOverwritesWithoutGrowing") {
    ClimbingTelemetryBuffer buffer;
    for (std::uint64_t index = 0; index < ClimbingTelemetryBuffer::Capacity + 20; ++index) {
        ClimbingRuntimeCounters counters;
        counters.workload    = ClimbingWorkload::Active;
        counters.queryBudget = ClimbingQueryBudgets::Active;
        counters.queryCount  = 9;
        counters.budgetState = ClimbingQueryBudgetState::Exceeded;
        buffer.record({SimulationTick(index + 1), index, counters});
    }
    const auto summary = buffer.summarize(ClimbingWorkload::Active);
    CHECK(buffer.samples().size() == ClimbingTelemetryBuffer::Capacity);
    CHECK(summary.sampleCount == ClimbingTelemetryBuffer::Capacity);
    CHECK(summary.budgetExceededCount == ClimbingTelemetryBuffer::Capacity);
}
