// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "climbing_anchor_authoring.cpp",
    "climbing_authored_motion.cpp",
    "climbing_anchor_graph.cpp",
    "climbing_ecs.cpp",
    "climbing_input.cpp",
    "climbing_parkour.cpp",
    "climbing_services.cpp",
    "climbing.cpp",
    "climbing_events.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_climbing_anchor_authoring)
#include "PrivateTestScope.h"
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
namespace test_climbing_anchor_authoring_unit {
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
}  // namespace test_climbing_anchor_authoring_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_climbing_authored_motion)
#include "PrivateTestScope.h"
#include <cmath>
#include <memory>
#include "climbing/Climbing.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// climbing_authored_motion.cpp
namespace test_climbing_authored_motion {
using namespace eve::climbing;
namespace {
eve::Duration seconds(double value) {
    auto result = eve::Duration::fromSeconds(value);
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}
struct Fixture {
    std::unique_ptr<eve::physics::World3D> world{eve::physics::Physics::create()->newWorld3D(0, 0, 0, false)};
    ClimbingRuntime                        runtime;
    Fixture() {
        world->newBody("static", 0, .5f, 1)->newBoxShape(2, 1, .5f);
        ClimbingActionDefinition action{"mantle", .4f, 1.2f, 0, seconds(1), .6f, .7f, 0};
        REQUIRE(runtime.upsertAction(action).ok());
        REQUIRE(runtime.tryBegin(*world, {{0, 0, 0}, {0, 0, 1}, 1, -1}, eve::SimulationTick(1)).ok());
    }
};
}  // namespace
TEST_CASE("climbing.prewarpedMotionPreservesTimingAndRejectsInvalidPoliciesAtomically") {
    Fixture             f;
    ClimbingMotionInput input;
    input.rootMotionPolicy = ClimbingRootMotionPolicy::PreserveSuppliedDelta;
    auto rejected          = f.runtime.advance(*f.world, {eve::SimulationTick(2), seconds(.1)}, input);
    CHECK(!rejected.ok());
    input.hasRootMotion   = true;
    input.rootTranslation = {0, .02f, .03f};
    auto advanced         = f.runtime.advance(*f.world, {eve::SimulationTick(2), seconds(.1)}, input);
    REQUIRE(advanced.ok());
    CHECK(std::abs(advanced.value().feet.y - .02f) < .0001f);
    CHECK(std::abs(advanced.value().feet.z - .03f) < .0001f);
    CHECK(advanced.value().appliedWarp.y == 0.f);
    input.obstacleCollision = static_cast<ClimbingObstacleCollision>(99);
    CHECK(!f.runtime.advance(*f.world, {eve::SimulationTick(3), seconds(.1)}, input).ok());
    input.obstacleCollision = ClimbingObstacleCollision::Collide;
    REQUIRE(f.runtime.advance(*f.world, {eve::SimulationTick(3), seconds(.1)}, input).ok());
}
TEST_CASE("climbing.prewarpedCollisionExcludesOnlyTraversedShapeForOneCall") {
    Fixture             f;
    ClimbingMotionInput input;
    input.hasRootMotion     = true;
    input.rootMotionPolicy  = ClimbingRootMotionPolicy::PreserveSuppliedDelta;
    input.obstacleCollision = ClimbingObstacleCollision::IgnoreTraversedShape;
    input.rootTranslation   = {0, 0, .9f};
    auto crossed            = f.runtime.advance(*f.world, {eve::SimulationTick(2), seconds(.1)}, input);
    REQUIRE(crossed.ok());
    CHECK(std::abs(crossed.value().feet.z - .9f) < .001f);
    // A separate query still collides with the original obstacle.
    auto normal = f.world->moveCapsuleOwned(0, .3f, 0, 0, 1.5f, 0, .3f, 0, 0, .9f, {});
    REQUIRE(normal.ok());
    CHECK(normal.value().deltaZ < .6f);
    f.world->newBody("static", 0, .7f, 1.8f)->newBoxShape(2, 1.4f, .1f);
    input.rootTranslation = {0, 0, 1.f};
    auto blocked          = f.runtime.advance(*f.world, {eve::SimulationTick(3), seconds(.1)}, input);
    REQUIRE(blocked.ok());
    CHECK(blocked.value().constrained);
    CHECK(blocked.value().feet.z < 1.6f);
}
}  // namespace test_climbing_authored_motion
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_climbing_anchor_graph) || \
    defined(EVE_TEST_ORIGIN_climbing_ecs) || defined(EVE_TEST_ORIGIN_climbing_input) ||    \
    defined(EVE_TEST_ORIGIN_climbing_parkour) || defined(EVE_TEST_ORIGIN_climbing_services)
#include "PrivateTestScope.h"
#include <cmath>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "climbing/Climbing.h"
#include "climbing/ClimbingAnchorGraph.h"
#include "climbing/ClimbingCodec.h"
#include "climbing/ClimbingECS.h"
#include "climbing/ClimbingInput.h"
#include "climbing/ClimbingServices.h"
#include "common/Capability.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Responsibility: climbing_controller_rules
namespace test_climbing_controller_rules {
// climbing_anchor_graph.cpp
namespace {
namespace climbing_anchor_graph {
namespace {

[[maybe_unused]] eve::climbing::ClimbingAnchorGraphDefinition graphDefinition() {
    eve::climbing::ClimbingAnchorNodeDefinition left;
    left.id              = "ledge:left";
    left.localPosition   = {0.f, 1.f, 0.f};
    left.leftHandSocket  = {-0.2f, 1.f, 0.f};
    left.rightHandSocket = {0.2f, 1.f, 0.f};
    left.feetSocket      = {0.f, 0.f, -0.2f};
    left.occupancySlots  = 2;
    left.tags            = {"stone", "shimmy"};

    eve::climbing::ClimbingAnchorNodeDefinition right = left;
    right.id                                          = "ledge:right";
    right.localPosition.x                             = 1.f;
    right.leftHandSocket.x += 1.f;
    right.rightHandSocket.x += 1.f;
    right.feetSocket.x += 1.f;
    right.occupancySlots = 1;

    eve::climbing::ClimbingAnchorEdgeDefinition edge;
    edge.from          = left.id;
    edge.to            = right.id;
    edge.kind          = eve::climbing::ClimbingAnchorEdgeKind::Shimmy;
    edge.bidirectional = true;
    edge.requiredTags  = {"shimmy"};

    eve::climbing::ClimbingAnchorGraphDefinition graph;
    graph.id                      = "wall:route-a";
    graph.sourceGeometryContentId = "sha256:geometry-a";
    graph.buildSettingsHash       = "sha256:settings-a";
    graph.nodes                   = {std::move(right), std::move(left)};
    graph.edges                   = {std::move(edge)};
    return graph;
}

struct GraphWorld {
    GraphWorld() {
        world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        body  = world->newBody("kinematic", 3.f, 2.f, 4.f);
        shape = body->newBoxShape(3.f, 2.f, 0.5f);
    }
    std::unique_ptr<eve::physics::World3D> world;
    eve::physics::Body3D*                  body  = nullptr;
    eve::physics::Shape3D*                 shape = nullptr;
};

}  // namespace

TEST_CASE("climbing.anchorGraph.codecIsCanonicalAndPreservesUnknownFields") {
    auto graph = graphDefinition();
    graph.extensionMetadata.emplace("editorColor", eve::Value("blue"));
    graph.nodes.front().extensionMetadata.emplace("designerNote", eve::Value("right end"));
    auto encoded = eve::climbing::encodeClimbingAnchorGraphDefinition(graph);
    REQUIRE(encoded.ok());
    auto decoded = eve::climbing::decodeClimbingAnchorGraphDefinition(encoded.value());
    REQUIRE(decoded.ok());
    CHECK_EQ(decoded.value().nodes.front().id, std::string("ledge:left"));
    auto reencoded = eve::climbing::encodeClimbingAnchorGraphDefinition(decoded.value());
    REQUIRE(reencoded.ok());
    CHECK(reencoded.value() == encoded.value());

    eve::Value future = encoded.value();
    future.set("schemaVersion", eve::Value(eve::climbing::ClimbingAnchorGraphDefinition::SchemaVersion + 1));
    auto rejected = eve::climbing::decodeClimbingAnchorGraphDefinition(future);
    CHECK(!rejected.ok());
    CHECK_EQ(static_cast<int>(rejected.error()->code()), static_cast<int>(eve::DiagnosticCode::UnknownVersion));
}
}  // namespace climbing_anchor_graph
}  // namespace

// climbing_ecs.cpp
namespace {
namespace climbing_ecs {
namespace {

class TestClimber : public ecs::Entity {
public:
    ENTITY(TestClimber, ecs::Entity)

    void release() override { ecs::DestroyEntity(this); }

    COMPONENT(eve::climbing::ClimbingBody, climbingBody)
    COMPONENT(eve::climbing::ClimbingIntent, climbingIntent)
    COMPONENT(eve::climbing::ClimbingState, climbingState)
    COMPONENT(eve::climbing::ClimbingLinks, climbingLinks)
    COMPONENT(eve::climbing::ClimbingCandidateBuffer, climbingCandidates)
    COMPONENT(eve::climbing::ClimbingPoseProjection, climbingPose)
    COMPONENT(eve::climbing::ClimbingEventBatch, climbingEvents)
};

[[maybe_unused]] eve::climbing::ClimbingActionDefinition vaultAction() {
    eve::climbing::ClimbingActionDefinition action;
    action.id             = "parkour:vault_low";
    action.kind           = eve::climbing::ClimbingActionKind::Vault;
    action.minHeight      = 0.4f;
    action.maxHeight      = 1.2f;
    action.duration       = eve::Duration::fromNanoseconds(600000000);
    action.landingForward = 0.7f;
    action.apexHeight     = 0.7f;
    return action;
}

struct EcsClimbingFixture {
    EcsClimbingFixture() {
        world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        character = world->newBody("kinematic", 0.f, 0.9f, 0.f);
        character->newCapsuleShape(1.8f, 0.3f);
        obstacle = world->newBody("static", 0.f, 0.5f, 1.f);
        obstacle->newBoxShape(2.f, 1.f, 0.5f);
    }

    std::unique_ptr<eve::physics::World3D> world;
    eve::physics::Body3D*                  character = nullptr;
    eve::physics::Body3D*                  obstacle  = nullptr;
};

}  // namespace


TEST_CASE("climbing.ecs.fixedBuffersAreTransactionalAndContractsAreComplete") {
    eve::climbing::ClimbingCandidateBuffer candidates;
    eve::climbing::ClimbingCandidate       sentinel;
    sentinel.actionId = "test:sentinel";
    const std::vector<eve::climbing::ClimbingCandidate> initial{sentinel};
    REQUIRE(candidates.replace(initial, eve::SimulationTick(3)).ok());

    std::vector<eve::climbing::ClimbingCandidate> overflow(eve::climbing::ClimbingCandidateBuffer::Capacity + 1);
    auto                                          rejected = candidates.replace(overflow, eve::SimulationTick(4));
    CHECK(!rejected.ok());
    CHECK_EQ(candidates.values().size(), std::size_t{1});
    CHECK_EQ(candidates.values().front().actionId, std::string("test:sentinel"));
    CHECK(candidates.tick() == eve::SimulationTick(3));

    const auto contracts = eve::climbing::climbingSystemContracts();
    CHECK_EQ(contracts.size(), std::size_t{5});
    for (const auto& contract : contracts) {
        CHECK(!contract.name.empty());
        CHECK(!contract.entityScope.empty());
        CHECK(!contract.view.empty());
        CHECK(!contract.readSet.empty());
        CHECK(!contract.writeSet.empty());
        CHECK(!contract.phase.empty());
        CHECK(!contract.determinism.empty());
    }
}
}  // namespace climbing_ecs
}  // namespace

// climbing_input.cpp
namespace {
namespace climbing_input {
namespace {

[[maybe_unused]] eve::climbing::ClimbingActionDefinition mantleAction() {
    eve::climbing::ClimbingActionDefinition action;
    action.id        = "parkour:mantle";
    action.minHeight = 0.4f;
    action.maxHeight = 1.2f;
    action.duration  = eve::Duration::fromNanoseconds(600000000);
    return action;
}

struct ClimbingWorld {
    ClimbingWorld() {
        world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        obstacle = world->newBody("static", 0.f, 0.5f, 1.f);
        obstacle->newBoxShape(2.f, 1.f, 0.5f);
    }

    std::unique_ptr<eve::physics::World3D> world;
    eve::physics::Body3D*                  obstacle = nullptr;
};

}  // namespace

TEST_CASE("climbing.input.buffersAndConsumesEdgeExactlyOnce") {
    eve::climbing::ClimbingIntent intent;
    REQUIRE(eve::climbing::ClimbingInputSystem::submit(intent, eve::climbing::ClimbingCommand::Climb,
                                                       eve::SimulationTick(10), 4)
                .ok());
    auto consumed = eve::climbing::ClimbingInputSystem::consume(
        intent, eve::climbing::ClimbingCommand::Climb, eve::SimulationTick(12), eve::climbing::ClimbingExecutionId(7));
    REQUIRE(consumed.ok());
    REQUIRE(consumed.value().has_value());
    CHECK_EQ(consumed.value()->consumedExecutionId.value(), std::uint64_t(7));

    auto repeated = eve::climbing::ClimbingInputSystem::consume(
        intent, eve::climbing::ClimbingCommand::Climb, eve::SimulationTick(12), eve::climbing::ClimbingExecutionId(8));
    REQUIRE(repeated.ok());
    CHECK(!repeated.value().has_value());
    CHECK_EQ(static_cast<int>(repeated.code()), static_cast<int>(eve::StatusCode::NoOp));
}

TEST_CASE("climbing.input.expiryDuplicateAndOverflowAreDeterministic") {
    eve::climbing::ClimbingIntent intent;
    REQUIRE(eve::climbing::ClimbingInputSystem::submit(intent, eve::climbing::ClimbingCommand::Jump,
                                                       eve::SimulationTick(20), 2)
                .ok());
    auto duplicate = eve::climbing::ClimbingInputSystem::submit(intent, eve::climbing::ClimbingCommand::Jump,
                                                                eve::SimulationTick(20), 2);
    CHECK(!duplicate.ok());
    CHECK_EQ(static_cast<int>(duplicate.code()), static_cast<int>(eve::StatusCode::Conflict));

    auto expired = eve::climbing::ClimbingInputSystem::consume(
        intent, eve::climbing::ClimbingCommand::Jump, eve::SimulationTick(23), eve::climbing::ClimbingExecutionId(1));
    REQUIRE(expired.ok());
    CHECK(!expired.value().has_value());

    auto overflow =
        eve::climbing::ClimbingInputSystem::submit(intent, eve::climbing::ClimbingCommand::Drop,
                                                   eve::SimulationTick(std::numeric_limits<std::uint64_t>::max()), 1);
    CHECK(!overflow.ok());
}

TEST_CASE("climbing.input.coyoteWindowUsesOnlyTicks") {
    CHECK_EQ(static_cast<int>(eve::climbing::ClimbingInputSystem::coyoteWindowState(eve::SimulationTick(106),
                                                                                    eve::SimulationTick(100), 6)),
             static_cast<int>(eve::climbing::ClimbingCoyoteState::Eligible));
    CHECK_EQ(static_cast<int>(eve::climbing::ClimbingInputSystem::coyoteWindowState(eve::SimulationTick(107),
                                                                                    eve::SimulationTick(100), 6)),
             static_cast<int>(eve::climbing::ClimbingCoyoteState::Outside));
    CHECK_EQ(static_cast<int>(eve::climbing::ClimbingInputSystem::coyoteWindowState(eve::SimulationTick(99),
                                                                                    eve::SimulationTick(100), 6)),
             static_cast<int>(eve::climbing::ClimbingCoyoteState::Outside));
}
}  // namespace climbing_input
}  // namespace

// climbing_parkour.cpp
namespace {
namespace climbing_parkour {
namespace {

[[maybe_unused]] eve::Duration seconds(double value) {
    auto duration = eve::Duration::fromSeconds(value);
    REQUIRE(duration.ok());
    return std::move(duration).takeValue();
}

[[maybe_unused]] eve::climbing::ClimbingActionDefinition action(std::string                       id,
                                                                eve::climbing::ClimbingActionKind kind) {
    eve::climbing::ClimbingActionDefinition result;
    result.id             = std::move(id);
    result.kind           = kind;
    result.minHeight      = 0.f;
    result.maxHeight      = 4.f;
    result.minSpeed       = 2.f;
    result.duration       = seconds(0.2);
    result.landingForward = 1.f;
    result.apexHeight     = 0.f;
    return result;
}

struct ParkourWorld {
    ParkourWorld() { world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false)); }

    std::unique_ptr<eve::physics::World3D> world;
};

}  // namespace


TEST_CASE("climbing.parkour.highSpeedKindsRoundTripThroughActionCodec") {
    const eve::climbing::ClimbingActionKind kinds[] = {
        eve::climbing::ClimbingActionKind::WallRun,     eve::climbing::ClimbingActionKind::Slide,
        eve::climbing::ClimbingActionKind::BeamBalance, eve::climbing::ClimbingActionKind::PoleSwing,
        eve::climbing::ClimbingActionKind::BarSwing,
    };
    for (std::size_t index = 0; index < std::size(kinds); ++index) {
        auto source  = action("parkour:" + std::to_string(index), kinds[index]);
        auto encoded = eve::climbing::encodeClimbingActionDefinition(source);
        REQUIRE(encoded.ok());
        auto decoded = eve::climbing::decodeClimbingActionDefinition(encoded.value());
        REQUIRE(decoded.ok());
        CHECK_EQ(static_cast<int>(decoded.value().kind), static_cast<int>(kinds[index]));
    }
}
}  // namespace climbing_parkour
}  // namespace

// climbing_services.cpp
namespace {
namespace climbing_services {
namespace {

struct ServiceWorld {
    ServiceWorld() {
        world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        player = world->newBody("kinematic", 0.f, 0.9f, 0.f);
        player->newCapsuleShape(1.8f, 0.3f);
        obstacle = world->newBody("static", 0.f, 0.3f, 1.f);
        obstacle->newBoxShape(1.35f, 0.6f, 0.65f);
    }
    std::unique_ptr<eve::physics::World3D> world;
    eve::physics::Body3D*                  player   = nullptr;
    eve::physics::Body3D*                  obstacle = nullptr;
};

[[maybe_unused]] eve::climbing::ClimbingProfileDefinition serviceProfile() {
    eve::climbing::ClimbingActionDefinition action;
    action.id              = "parkour:stamina_vault";
    action.kind            = eve::climbing::ClimbingActionKind::Vault;
    action.minHeight       = 0.4f;
    action.maxHeight       = 0.8f;
    action.landingForward  = 0.75f;
    action.apexHeight      = 0.4f;
    action.requiredCommand = eve::climbing::ClimbingCommandRequirement::Jump;
    action.staminaCost     = 12.f;
    eve::climbing::ClimbingProfileDefinition profile;
    profile.staminaPolicy  = eve::climbing::ClimbingStaminaPolicy::RequireProvider;
    profile.staminaAdapter = "test:stamina";
    profile.actions.push_back(action);
    return profile;
}

[[maybe_unused]] eve::climbing::ClimbingPose pose(const ServiceWorld& fixture) {
    return {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 4.f, fixture.player->getId(), 0.f, true};
}

class Stamina final : public eve::climbing::IClimbingStaminaAuthority {
public:
    std::string_view adapterId() const noexcept override { return id; }

    eve::Result<eve::climbing::ClimbingStaminaReservation> prepare(eve::climbing::ClimbingServiceSubject subject,
                                                                   float cost, eve::SimulationTick tick) override {
        ++prepareCount;
        preparedSubject = subject;
        preparedCost    = cost;
        preparedTick    = tick;
        return eve::Result<eve::climbing::ClimbingStaminaReservation>::success(
            eve::climbing::ClimbingStaminaReservation(77));
    }
    void commitPrepared(eve::climbing::ClimbingStaminaReservation reservation,
                        eve::climbing::ClimbingExecutionId        executionId) noexcept override {
        ++commitCount;
        committedReservation = reservation;
        committedExecution   = executionId;
    }
    void cancelPrepared(eve::climbing::ClimbingStaminaReservation) noexcept override { ++cancelCount; }

    int                                       prepareCount         = 0;
    int                                       commitCount          = 0;
    int                                       cancelCount          = 0;
    float                                     preparedCost         = 0.f;
    eve::SimulationTick                       preparedTick         = eve::SimulationTick::zero();
    eve::climbing::ClimbingServiceSubject     preparedSubject      = eve::climbing::ClimbingServiceSubject::zero();
    eve::climbing::ClimbingStaminaReservation committedReservation = eve::climbing::ClimbingStaminaReservation::zero();
    eve::climbing::ClimbingExecutionId        committedExecution   = eve::climbing::ClimbingExecutionId::zero();
    std::string                               id                   = "test:stamina";
};

class EventSink final : public eve::climbing::IClimbingEventSink {
public:
    eve::Result<void> publish(eve::climbing::ClimbingServiceSubject         subject,
                              std::span<const eve::climbing::ClimbingEvent> events) override {
        seenSubject = subject;
        seenCount   = events.size();
        return eve::Result<void>::success();
    }
    eve::climbing::ClimbingServiceSubject seenSubject = eve::climbing::ClimbingServiceSubject::zero();
    std::size_t                           seenCount   = 0;
};

class PoseAdapter final : public eve::climbing::IClimbingPoseAdapter {
public:
    eve::Result<void> apply(eve::climbing::ClimbingServiceSubject subject,
                            const eve::climbing::ClimbingAdvance& advance) override {
        seenSubject = subject;
        seenAction  = advance.actionId;
        return eve::Result<void>::success();
    }
    eve::climbing::ClimbingServiceSubject seenSubject = eve::climbing::ClimbingServiceSubject::zero();
    std::string                           seenAction;
};

class Conditions final : public eve::climbing::IClimbingConditionAuthority {
public:
    eve::Result<eve::climbing::ClimbingConditionDecision> evaluate(eve::climbing::ClimbingServiceSubject subject,
                                                                   std::span<const std::string>          requiredTags,
                                                                   eve::SimulationTick tick) override {
        seenSubject = subject;
        seenTick    = tick;
        seenCount   = requiredTags.size();
        return eve::Result<eve::climbing::ClimbingConditionDecision>::success(decision);
    }
    eve::climbing::ClimbingConditionDecision decision    = eve::climbing::ClimbingConditionDecision::Allowed;
    eve::climbing::ClimbingServiceSubject    seenSubject = eve::climbing::ClimbingServiceSubject::zero();
    eve::SimulationTick                      seenTick    = eve::SimulationTick::zero();
    std::size_t                              seenCount   = 0;
};

}  // namespace


TEST_CASE("climbing.services.eventAndPoseAdaptersReportAbsentAndPresent") {
    const eve::climbing::ClimbingServiceSubject subject(21);
    std::vector<eve::climbing::ClimbingEvent>   events(2);
    eve::climbing::ClimbingAdvance              advance;
    advance.actionId  = "parkour:vault";
    auto absentEvents = eve::climbing::dispatchClimbingEvents(subject, events);
    auto absentPose   = eve::climbing::applyClimbingPose(subject, advance);
    REQUIRE(absentEvents.ok());
    REQUIRE(absentPose.ok());
    CHECK_EQ(static_cast<int>(absentEvents.value()),
             static_cast<int>(eve::climbing::ClimbingOptionalServiceState::ProviderAbsent));
    CHECK_EQ(static_cast<int>(absentPose.value()),
             static_cast<int>(eve::climbing::ClimbingOptionalServiceState::ProviderAbsent));

    EventSink   eventSink;
    PoseAdapter poseAdapter;
    eve::cap::provide<eve::climbing::IClimbingEventSink>(&eventSink);
    eve::cap::provide<eve::climbing::IClimbingPoseAdapter>(&poseAdapter);
    auto presentEvents = eve::climbing::dispatchClimbingEvents(subject, events);
    auto presentPose   = eve::climbing::applyClimbingPose(subject, advance);
    eve::cap::revoke<eve::climbing::IClimbingEventSink>(&eventSink);
    eve::cap::revoke<eve::climbing::IClimbingPoseAdapter>(&poseAdapter);
    REQUIRE(presentEvents.ok());
    REQUIRE(presentPose.ok());
    CHECK_EQ(eventSink.seenCount, std::size_t{2});
    CHECK_EQ(poseAdapter.seenAction, std::string("parkour:vault"));
}
}  // namespace climbing_services
}  // namespace
}  // namespace test_climbing_controller_rules
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_climbing)
#include "PrivateTestScope.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include "animation/AnimClip.h"
#include "climbing/Climbing.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif
// climbing.cpp
namespace test_climbing_unit {
#if defined(_MSC_VER)

#endif

namespace {

#if defined(_MSC_VER)
std::size_t trackedHeapAllocations = 0;
bool        trackHeapAllocations   = false;

[[maybe_unused]] int __cdecl climbingAllocationHook(int allocationType, void*, std::size_t, int, long,
                                                    const unsigned char*, int) {
    if (trackHeapAllocations && allocationType == _HOOK_ALLOC) ++trackedHeapAllocations;
    return 1;
}
#endif

[[maybe_unused]] eve::Duration seconds(double value) {
    auto duration = eve::Duration::fromSeconds(value);
    REQUIRE(duration.ok());
    return std::move(duration).takeValue();
}

[[maybe_unused]] eve::climbing::ClimbingActionDefinition mantleAction(std::string id = "core:mantle") {
    return {std::move(id), 0.4f, 1.2f, 0.f, seconds(0.6), 0.6f, 0.7f, 0};
}

[[maybe_unused]] eve::climbing::ClimbingActionDefinition ledgeGrabAction() {
    auto action       = mantleAction("core:ledge_grab");
    action.kind       = eve::climbing::ClimbingActionKind::LedgeGrab;
    action.apexHeight = 0.1f;
    return action;
}

struct ClimbingWorld {
    ClimbingWorld() {
        world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        obstacle = world->newBody("static", 0.f, 0.5f, 1.f);
        shape    = obstacle->newBoxShape(2.f, 1.f, 0.5f);
    }

    std::unique_ptr<eve::physics::World3D> world;
    eve::physics::Body3D*                  obstacle = nullptr;
    eve::physics::Shape3D*                 shape    = nullptr;
};

}  // namespace

TEST_CASE("climbing.profile.rejectsInvalidCapsuleAndActionDefinitions") {
    eve::climbing::ClimbingRuntime runtime;
    eve::climbing::ClimbingProfile profile;
    profile.capsuleRadius = 0.5f;
    profile.capsuleHeight = 1.f;
    auto invalidProfile   = runtime.setProfile(profile);
    CHECK(!invalidProfile.ok());
    CHECK_EQ(static_cast<int>(invalidProfile.code()), static_cast<int>(eve::StatusCode::Rejected));

    auto action        = mantleAction();
    action.duration    = eve::Duration::zero();
    auto invalidAction = runtime.upsertAction(std::move(action));
    CHECK(!invalidAction.ok());
    CHECK_EQ(static_cast<int>(invalidAction.code()), static_cast<int>(eve::StatusCode::Rejected));
}
}  // namespace test_climbing_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_climbing_events)
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#include <algorithm>
#include <string>
#include <vector>
#include "climbing/ClimbingEventValue.h"
// climbing_events.cpp
namespace test_climbing_local_boundary {
TEST_CASE("climbing.events.eventKindNamesRoundTripThroughTheSingleTable") {
    std::vector<std::string> seen;
    for (std::uint8_t raw = 0; raw < eve::climbing::kClimbingEventKindCount; ++raw) {
        const auto             kind = static_cast<eve::climbing::ClimbingEventKind>(raw);
        const std::string_view name = eve::climbing::climbingEventKindName(kind);
        REQUIRE(!name.empty());
        eve::climbing::ClimbingEventKind parsed = eve::climbing::ClimbingEventKind::Failed;
        REQUIRE(eve::climbing::parseClimbingEventKind(name, parsed) == eve::climbing::EventKindParse::Parsed);
        CHECK_EQ(static_cast<int>(parsed), static_cast<int>(raw));
        seen.emplace_back(name);
    }
    std::sort(seen.begin(), seen.end());
    CHECK(std::adjacent_find(seen.begin(), seen.end()) == seen.end());

    eve::climbing::ClimbingEventKind unknown = eve::climbing::ClimbingEventKind::Started;
    CHECK(eve::climbing::parseClimbingEventKind("not_a_kind", unknown) == eve::climbing::EventKindParse::Unknown);
    CHECK_EQ(static_cast<int>(unknown), static_cast<int>(eve::climbing::ClimbingEventKind::Started));

    // 事件 → Value 的形状（绑定与快照共用同一份；`tick`/`executionId` 是十进制字符串）
    eve::climbing::ClimbingEvent event;
    event.kind             = eve::climbing::ClimbingEventKind::AnchorReached;
    event.actionId         = "climb:event_mantle";
    event.tick             = eve::SimulationTick(42);
    event.executionId      = eve::climbing::ClimbingExecutionId(7);
    event.metadata         = {"event:parkour", "surface:stone"};
    const eve::Value value = eve::climbing::climbingEventValue(event);
    REQUIRE(value.isObject());
    CHECK_EQ(value.find("kind")->asString(), std::string("anchor_reached"));
    CHECK_EQ(value.find("actionId")->asString(), std::string("climb:event_mantle"));
    CHECK_EQ(value.find("tick")->asString(), std::string("42"));
    CHECK_EQ(value.find("executionId")->asString(), std::string("7"));
    REQUIRE(value.find("metadata")->isArray());
    CHECK_EQ(value.find("metadata")->arraySize(), std::size_t{2});
    CHECK_EQ(value.find("metadata")->at(1).asString(), std::string("surface:stone"));
}
}  // namespace test_climbing_local_boundary
#endif
