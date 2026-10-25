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

#include "zeroerr/unittest.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_climbing_anchor_authoring)
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "PrivateTestScope.h"
#include "climbing/ClimbingAnchorAuthoring.h"
#include "climbing/ClimbingCodec.h"
#include "climbing/ClimbingTelemetry.h"

namespace test_climbing_anchor_authoring_unit {
using namespace eve;
using namespace eve::climbing;
namespace {
ClimbingAnchorBakeRequest authoredRoute() {
    ClimbingAnchorBakeRequest request;
    request.graphId                  = "playground.route";
    request.sourceGeometryContentId  = "sha256:playground-geometry";
    request.settings.handSpacing     = 0.4f;
    request.settings.hangingFeetDrop = 1.4f;
    ClimbingLedgeBakeSource ledge;
    ledge.id           = "wall";
    ledge.points       = {{0, 2, 0}, {1, 2, 0}, {1, 2, 1}};
    ledge.localNormals = {{0, 0, -1}, {-1, 0, 0}, {-1, 0, 0}};
    ledge.tags         = {"stone"};
    request.ledges.push_back(std::move(ledge));
    request.ladders.push_back({"ladder", {3, .3f, 0}, {0, 1, 0}, {0, 0, -1}, .3f, .5f, 4, {"metal"}});
    return request;
}
ClimbingActionDefinition vault(std::string id) {
    ClimbingActionDefinition action;
    action.id                = std::move(id);
    action.kind              = ClimbingActionKind::Vault;
    action.minHeight         = .3f;
    action.maxHeight         = 1.1f;
    action.duration          = Duration::fromNanoseconds(450000000);
    action.requiredNotifies  = {"contact.left_hand", "land"};
    action.tags              = {"parkour", "vault"};
    action.sourceModes       = ClimbingSourceMode::Grounded;
    action.requiredCommand   = ClimbingCommandRequirement::Jump;
    action.probeRecipe       = ClimbingProbeRecipe::Obstacle;
    action.minDepth          = .2f;
    action.maxDepth          = 1.4f;
    action.minDistance       = .1f;
    action.maxDistance       = 1.5f;
    action.animation         = {"anim:vault_low", "climbing.vault", "Root", false};
    action.warpWindows       = {{.1f, .45f, true, false, true}, {.6f, .9f, false, true, false}};
    action.extensionMetadata = {{"plugin.cameraCue", Value("close_follow")}};
    return action;
}
}  // namespace

TEST_CASE("Climbing anchor bake validates authored geometry and produces canonical executable graph") {
    SUB_CASE("ledge and ladder become stable nodes with traversable edges") {
        auto first  = bakeClimbingAnchorGraph(authoredRoute());
        auto second = bakeClimbingAnchorGraph(authoredRoute());
        REQUIRE(first.ok());
        REQUIRE(second.ok());
        CHECK_EQ(first.value().ledgeNodeCount, 3u);
        CHECK_EQ(first.value().ladderNodeCount, 4u);
        CHECK_EQ(first.value().graph.buildSettingsHash, second.value().graph.buildSettingsHash);
        auto encoded   = encodeClimbingAnchorGraphDefinition(first.value().graph);
        auto duplicate = encodeClimbingAnchorGraphDefinition(second.value().graph);
        REQUIRE(encoded.ok());
        REQUIRE(duplicate.ok());
        CHECK_EQ(encoded.value(), duplicate.value());
        bool mount = false, climb = false, dismount = false;
        for (const auto& edge : first.value().graph.edges) {
            mount |= edge.kind == ClimbingAnchorEdgeKind::Mount;
            climb |= edge.kind == ClimbingAnchorEdgeKind::Climb;
            dismount |= edge.kind == ClimbingAnchorEdgeKind::Dismount;
        }
        CHECK(mount);
        CHECK(climb);
        CHECK(dismount);
    };
    SUB_CASE("source and tag ordering do not affect canonical hash or encoded graph") {
        auto                    canonical = authoredRoute();
        ClimbingLedgeBakeSource balcony;
        balcony.id          = "balcony";
        balcony.points      = {{4, 1, 0}, {5, 1, 0}};
        balcony.localNormal = {0, 0, -1};
        balcony.tags        = {"outdoor", "stone"};
        canonical.ledges.push_back(balcony);
        canonical.ladders.front().tags = {"metal", "outdoor"};
        auto permuted                  = canonical;
        std::reverse(permuted.ledges.begin(), permuted.ledges.end());
        for (auto& ledge : permuted.ledges) std::reverse(ledge.tags.begin(), ledge.tags.end());
        std::reverse(permuted.ladders.front().tags.begin(), permuted.ladders.front().tags.end());
        auto a = bakeClimbingAnchorGraph(canonical);
        auto b = bakeClimbingAnchorGraph(permuted);
        REQUIRE(a.ok());
        REQUIRE(b.ok());
        CHECK_EQ(a.value().graph.buildSettingsHash, b.value().graph.buildSettingsHash);
        auto aJson = encodeClimbingAnchorGraphDefinition(a.value().graph);
        auto bJson = encodeClimbingAnchorGraphDefinition(b.value().graph);
        REQUIRE(aJson.ok());
        REQUIRE(bJson.ok());
        CHECK_EQ(aJson.value(), bJson.value());
    };
    SUB_CASE("request codec round trips and rejects future version") {
        auto encoded = encodeClimbingAnchorBakeRequest(authoredRoute());
        REQUIRE(encoded.ok());
        auto decoded = decodeClimbingAnchorBakeRequest(encoded.value());
        REQUIRE(decoded.ok());
        auto roundTrip = encodeClimbingAnchorBakeRequest(decoded.value());
        REQUIRE(roundTrip.ok());
        CHECK_EQ(roundTrip.value(), encoded.value());
        auto future = encoded.value();
        future.set("schemaVersion", Value(ClimbingAnchorBakeRequest::SchemaVersion + 1));
        auto rejected = decodeClimbingAnchorBakeRequest(future);
        REQUIRE(!rejected.ok());
        CHECK(rejected.error()->code() == DiagnosticCode::UnknownVersion);
    };
    SUB_CASE("overlay owns node and edge copies and invalid geometry fails") {
        auto baked = bakeClimbingAnchorGraph(authoredRoute());
        REQUIRE(baked.ok());
        auto overlay = inspectClimbingAnchorGraphAuthoring(baked.value().graph);
        REQUIRE(overlay.ok());
        CHECK_EQ(overlay.value().graphId, std::string("playground.route"));
        CHECK_EQ(overlay.value().nodes.size(), baked.value().graph.nodes.size());
        auto invalid                      = baked.value().graph;
        invalid.nodes.front().localNormal = {};
        CHECK(!inspectClimbingAnchorGraphAuthoring(invalid).ok());
        auto request                        = authoredRoute();
        request.ladders.front().rungSpacing = 0;
        CHECK(!bakeClimbingAnchorGraph(request).ok());
    };
}

TEST_CASE("Climbing action profile codec version and validation contract") {
    SUB_CASE("profile and action retain extensions and authored motion fields") {
        ClimbingProfileDefinition profile;
        profile.inputBufferTicks       = 9;
        profile.coyoteTicks            = 7;
        profile.pathValidationSegments = 18;
        profile.maxPelvisDeviation     = .28f;
        profile.cameraCueProfile       = "camera:climbing";
        profile.actions.push_back(vault("parkour:vault_low"));
        profile.extensionMetadata.emplace("studio.balanceRevision", Value(std::int64_t(7)));
        auto encoded = encodeClimbingProfileDefinition(profile);
        REQUIRE(encoded.ok());
        auto decoded = decodeClimbingProfileDefinition(encoded.value());
        REQUIRE(decoded.ok());
        REQUIRE_EQ(decoded.value().actions.size(), 1u);
        CHECK_EQ(decoded.value().actions[0].id, std::string("parkour:vault_low"));
        CHECK_EQ(decoded.value().actions[0].animation.clipId, std::string("anim:vault_low"));
        CHECK_EQ(decoded.value().actions[0].warpWindows.size(), 2u);
        CHECK(decoded.value().actions[0].extensionMetadata.contains("plugin.cameraCue"));
        CHECK(decoded.value().extensionMetadata.contains("studio.balanceRevision"));
        auto reencoded = encodeClimbingProfileDefinition(decoded.value());
        REQUIRE(reencoded.ok());
        CHECK_EQ(reencoded.value(), encoded.value());
    };
    SUB_CASE("v3 action migration fills explicit v4 defaults") {
        auto encoded = encodeClimbingActionDefinition(vault("parkour:legacy"));
        REQUIRE(encoded.ok());
        auto* fields = encoded.value().getIf<Value::Object>();
        REQUIRE(fields != nullptr);
        fields->at("schemaVersion") = Value(std::int64_t(3));
        for (const std::string& name : {"tags",
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
                                        "eventMetadata"})
            fields->erase(name);
        auto migrated = decodeClimbingActionDefinition(encoded.value());
        REQUIRE(migrated.ok());
        CHECK(migrated.value().tags.empty());
        CHECK(migrated.value().sourceModes == ClimbingSourceMode::Any);
        CHECK_EQ(migrated.value().animation.rootBone, std::string("Root"));
    };
    SUB_CASE("invalid version window and duplicate action reject whole candidate") {
        auto encoded = encodeClimbingActionDefinition(vault("parkour:vault_low"));
        REQUIRE(encoded.ok());
        auto future = encoded.value();
        future.set("schemaVersion", Value(std::int64_t(ClimbingActionDefinition::SchemaVersion + 1)));
        auto unknown = decodeClimbingActionDefinition(future);
        REQUIRE(!unknown.ok());
        CHECK(unknown.error()->code() == DiagnosticCode::UnknownVersion);
        auto overlap        = vault("parkour:overlap");
        overlap.warpWindows = {{0, .6f, true, false, false}, {.5f, 1, false, true, false}};
        CHECK(!encodeClimbingActionDefinition(overlap).ok());
        auto empty        = vault("parkour:empty");
        empty.warpWindows = {{0, 1, false, false, false}};
        CHECK(!encodeClimbingActionDefinition(empty).ok());
        ClimbingProfileDefinition profile;
        profile.actions = {vault("parkour:duplicate"), vault("parkour:duplicate")};
        CHECK(!encodeClimbingProfileDefinition(profile).ok());
        profile.actions.clear();
        profile.inputBufferTicks = 0;
        CHECK(!encodeClimbingProfileDefinition(profile).ok());
    };
}

TEST_CASE("Climbing telemetry uses a bounded ring and exact workload summary") {
    SUB_CASE("percentiles and query maximum reflect samples") {
        ClimbingTelemetryBuffer buffer;
        for (std::uint64_t i = 1; i <= 100; ++i) {
            ClimbingRuntimeCounters counters;
            counters.workload    = ClimbingWorkload::CandidateProbe;
            counters.queryCount  = static_cast<std::uint32_t>(i % 17);
            counters.queryBudget = ClimbingQueryBudgets::CandidateProbe;
            buffer.record({SimulationTick(i), i * 100, counters});
        }
        const auto summary = buffer.summarize(ClimbingWorkload::CandidateProbe);
        CHECK_EQ(summary.sampleCount, 100u);
        CHECK_EQ(summary.p50Nanoseconds, 5000u);
        CHECK_EQ(summary.p95Nanoseconds, 9500u);
        CHECK_EQ(summary.maxQueryCount, 16u);
    };
    SUB_CASE("new samples overwrite oldest without allocating beyond capacity") {
        ClimbingTelemetryBuffer buffer;
        for (std::uint64_t i = 0; i < ClimbingTelemetryBuffer::Capacity + 20; ++i) {
            ClimbingRuntimeCounters counters;
            counters.workload    = ClimbingWorkload::Active;
            counters.queryBudget = ClimbingQueryBudgets::Active;
            counters.queryCount  = 9;
            counters.budgetState = ClimbingQueryBudgetState::Exceeded;
            buffer.record({SimulationTick(i + 1), i, counters});
        }
        CHECK_EQ(buffer.samples().size(), ClimbingTelemetryBuffer::Capacity);
        auto summary = buffer.summarize(ClimbingWorkload::Active);
        CHECK_EQ(summary.sampleCount, ClimbingTelemetryBuffer::Capacity);
        REQUIRE_EQ(summary.budgetExceededCount, ClimbingTelemetryBuffer::Capacity);
    };
}
}  // namespace test_climbing_anchor_authoring_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_climbing_authored_motion)
#include <cmath>
#include <memory>
#include "PrivateTestScope.h"
#include "climbing/Climbing.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/World3D.h"
namespace test_climbing_authored_motion {
using namespace eve::climbing;
namespace {
eve::Duration seconds(double value) { return eve::Duration::fromSeconds(value).expect("valid climbing duration"); }
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
TEST_CASE("Climbing authored root motion validates policy and collision scope per step") {
    SUB_CASE("preserved delta requires supplied motion and advances exact translation") {
        Fixture             fixture;
        ClimbingMotionInput input;
        input.rootMotionPolicy = ClimbingRootMotionPolicy::PreserveSuppliedDelta;
        CHECK(!fixture.runtime.advance(*fixture.world, {eve::SimulationTick(2), seconds(.1)}, input).ok());
        input.hasRootMotion   = true;
        input.rootTranslation = {0, .02f, .03f};
        auto advanced         = fixture.runtime.advance(*fixture.world, {eve::SimulationTick(2), seconds(.1)}, input);
        REQUIRE(advanced.ok());
        CHECK(std::abs(advanced.value().feet.y - .02f) < .0001f);
        CHECK(std::abs(advanced.value().feet.z - .03f) < .0001f);
        CHECK_EQ(advanced.value().appliedWarp.y, 0.f);
        input.obstacleCollision = static_cast<ClimbingObstacleCollision>(99);
        CHECK(!fixture.runtime.advance(*fixture.world, {eve::SimulationTick(3), seconds(.1)}, input).ok());
        input.obstacleCollision = ClimbingObstacleCollision::Collide;
        REQUIRE(fixture.runtime.advance(*fixture.world, {eve::SimulationTick(3), seconds(.1)}, input).ok());
    };
    SUB_CASE("ignore traversed shape affects only this motion call") {
        Fixture             fixture;
        ClimbingMotionInput input;
        input.hasRootMotion     = true;
        input.rootMotionPolicy  = ClimbingRootMotionPolicy::PreserveSuppliedDelta;
        input.obstacleCollision = ClimbingObstacleCollision::IgnoreTraversedShape;
        input.rootTranslation   = {0, 0, .9f};
        auto crossing           = fixture.runtime.advance(*fixture.world, {eve::SimulationTick(2), seconds(.1)}, input);
        REQUIRE(crossing.ok());
        CHECK(std::abs(crossing.value().feet.z - .9f) < .001f);
        auto normal = fixture.world->moveCapsuleOwned(0, .3f, 0, 0, 1.5f, 0, .3f, 0, 0, .9f, {});
        REQUIRE(normal.ok());
        CHECK(normal.value().deltaZ < .6f);
        fixture.world->newBody("static", 0, .7f, 1.8f)->newBoxShape(2, 1.4f, .1f);
        input.rootTranslation = {0, 0, 1};
        auto blocked          = fixture.runtime.advance(*fixture.world, {eve::SimulationTick(3), seconds(.1)}, input);
        REQUIRE(blocked.ok());
        CHECK(blocked.value().constrained);
        CHECK(blocked.value().feet.z < 1.6f);
    };
}
}  // namespace test_climbing_authored_motion
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_climbing_anchor_graph) || \
    defined(EVE_TEST_ORIGIN_climbing_ecs) || defined(EVE_TEST_ORIGIN_climbing_input) ||    \
    defined(EVE_TEST_ORIGIN_climbing_parkour) || defined(EVE_TEST_ORIGIN_climbing_services)
#include <algorithm>
#include <limits>
#include <span>
#include <string>
#include <vector>
#include "PrivateTestScope.h"
#include "climbing/ClimbingAnchorGraph.h"
#include "climbing/ClimbingCodec.h"
#include "climbing/ClimbingECS.h"
#include "climbing/ClimbingInput.h"
#include "climbing/ClimbingServices.h"
#include "common/Capability.h"
namespace test_climbing_controller_rules {
using namespace eve;
using namespace eve::climbing;
namespace {
ClimbingAnchorGraphDefinition graph() {
    ClimbingAnchorNodeDefinition left;
    left.id               = "ledge:left";
    left.localPosition    = {0, 1, 0};
    left.leftHandSocket   = {-.2f, 1, 0};
    left.rightHandSocket  = {.2f, 1, 0};
    left.feetSocket       = {0, 0, -.2f};
    left.occupancySlots   = 2;
    left.tags             = {"stone", "shimmy"};
    auto right            = left;
    right.id              = "ledge:right";
    right.localPosition.x = 1;
    right.leftHandSocket.x += 1;
    right.rightHandSocket.x += 1;
    right.feetSocket.x += 1;
    right.occupancySlots = 1;
    ClimbingAnchorEdgeDefinition edge;
    edge.from          = left.id;
    edge.to            = right.id;
    edge.kind          = ClimbingAnchorEdgeKind::Shimmy;
    edge.bidirectional = true;
    edge.requiredTags  = {"shimmy"};
    ClimbingAnchorGraphDefinition result;
    result.id                      = "wall:route-a";
    result.sourceGeometryContentId = "sha256:geometry-a";
    result.buildSettingsHash       = "sha256:settings-a";
    result.nodes                   = {right, left};
    result.edges                   = {edge};
    return result;
}
class EventSink final : public IClimbingEventSink {
public:
    Result<void> publish(ClimbingServiceSubject subject, std::span<const ClimbingEvent> events) override {
        seenSubject = subject;
        seenCount   = events.size();
        return Result<void>::success();
    }
    ClimbingServiceSubject seenSubject = ClimbingServiceSubject::zero();
    std::size_t            seenCount   = 0;
};
class PoseAdapter final : public IClimbingPoseAdapter {
public:
    Result<void> apply(ClimbingServiceSubject subject, const ClimbingAdvance& advance) override {
        seenSubject = subject;
        seenAction  = advance.actionId;
        return Result<void>::success();
    }
    ClimbingServiceSubject seenSubject = ClimbingServiceSubject::zero();
    std::string            seenAction;
};
}  // namespace
TEST_CASE("Climbing anchor graph and candidate buffer canonical data contract") {
    SUB_CASE("graph codec canonicalizes node order preserves extension and rejects future version") {
        auto source = graph();
        source.extensionMetadata.emplace("editorColor", Value("blue"));
        auto encoded = encodeClimbingAnchorGraphDefinition(source);
        REQUIRE(encoded.ok());
        auto decoded = decodeClimbingAnchorGraphDefinition(encoded.value());
        REQUIRE(decoded.ok());
        CHECK_EQ(decoded.value().nodes.front().id, std::string("ledge:left"));
        auto roundTrip = encodeClimbingAnchorGraphDefinition(decoded.value());
        REQUIRE(roundTrip.ok());
        CHECK_EQ(roundTrip.value(), encoded.value());
        auto future = encoded.value();
        future.set("schemaVersion", Value(ClimbingAnchorGraphDefinition::SchemaVersion + 1));
        auto rejected = decodeClimbingAnchorGraphDefinition(future);
        REQUIRE(!rejected.ok());
        CHECK(rejected.error()->code() == DiagnosticCode::UnknownVersion);
    };
    SUB_CASE("candidate buffer overflow preserves published values and tick") {
        ClimbingCandidateBuffer buffer;
        ClimbingCandidate       sentinel;
        sentinel.actionId = "test:sentinel";
        REQUIRE(buffer.replace(std::vector<ClimbingCandidate>{sentinel}, SimulationTick(3)).ok());
        std::vector<ClimbingCandidate> tooMany(ClimbingCandidateBuffer::Capacity + 1);
        CHECK(!buffer.replace(tooMany, SimulationTick(4)).ok());
        REQUIRE_EQ(buffer.values().size(), 1u);
        CHECK_EQ(buffer.values()[0].actionId, std::string("test:sentinel"));
        CHECK(buffer.tick() == SimulationTick(3));
        const auto contracts = climbingSystemContracts();
        CHECK_EQ(contracts.size(), 5u);
        for (const auto& contract : contracts) {
            CHECK(!contract.name.empty());
            CHECK(!contract.entityScope.empty());
            CHECK(!contract.readSet.empty());
            CHECK(!contract.writeSet.empty());
            CHECK(!contract.phase.empty());
        }
    };
}
TEST_CASE("Climbing input command and tick window contract") {
    SUB_CASE("buffered edge is consumed once by an execution") {
        ClimbingIntent intent;
        REQUIRE(ClimbingInputSystem::submit(intent, ClimbingCommand::Climb, SimulationTick(10), 4).ok());
        auto first =
            ClimbingInputSystem::consume(intent, ClimbingCommand::Climb, SimulationTick(12), ClimbingExecutionId(7));
        REQUIRE(first.ok());
        REQUIRE(first.value().has_value());
        CHECK_EQ(first.value()->consumedExecutionId.value(), 7u);
        auto again =
            ClimbingInputSystem::consume(intent, ClimbingCommand::Climb, SimulationTick(12), ClimbingExecutionId(8));
        REQUIRE(again.ok());
        CHECK(!again.value().has_value());
        CHECK(again.code() == StatusCode::NoOp);
    };
    SUB_CASE("duplicate expiry and tick overflow have explicit outcomes") {
        ClimbingIntent intent;
        REQUIRE(ClimbingInputSystem::submit(intent, ClimbingCommand::Jump, SimulationTick(20), 2).ok());
        auto duplicate = ClimbingInputSystem::submit(intent, ClimbingCommand::Jump, SimulationTick(20), 2);
        CHECK(!duplicate.ok());
        CHECK(duplicate.code() == StatusCode::Conflict);
        auto expired =
            ClimbingInputSystem::consume(intent, ClimbingCommand::Jump, SimulationTick(23), ClimbingExecutionId(1));
        REQUIRE(expired.ok());
        CHECK(!expired.value().has_value());
        CHECK(!ClimbingInputSystem::submit(intent, ClimbingCommand::Drop,
                                           SimulationTick(std::numeric_limits<std::uint64_t>::max()), 1)
                   .ok());
    };
    SUB_CASE("coyote eligibility includes last tick and excludes earlier or later ticks") {
        CHECK(ClimbingInputSystem::coyoteWindowState(SimulationTick(106), SimulationTick(100), 6) ==
              ClimbingCoyoteState::Eligible);
        CHECK(ClimbingInputSystem::coyoteWindowState(SimulationTick(107), SimulationTick(100), 6) ==
              ClimbingCoyoteState::Outside);
        CHECK(ClimbingInputSystem::coyoteWindowState(SimulationTick(99), SimulationTick(100), 6) ==
              ClimbingCoyoteState::Outside);
    };
}
TEST_CASE("Climbing optional service adapters report provider absence and dispatch once when present") {
    const ClimbingServiceSubject subject(21);
    std::vector<ClimbingEvent>   events(2);
    ClimbingAdvance              advance;
    advance.actionId  = "parkour:vault";
    auto absentEvents = dispatchClimbingEvents(subject, events);
    auto absentPose   = applyClimbingPose(subject, advance);
    REQUIRE(absentEvents.ok());
    REQUIRE(absentPose.ok());
    CHECK(absentEvents.value() == ClimbingOptionalServiceState::ProviderAbsent);
    CHECK(absentPose.value() == ClimbingOptionalServiceState::ProviderAbsent);
    EventSink   sink;
    PoseAdapter pose;
    eve::cap::provide<IClimbingEventSink>(&sink);
    eve::cap::provide<IClimbingPoseAdapter>(&pose);
    auto sentEvents = dispatchClimbingEvents(subject, events);
    auto sentPose   = applyClimbingPose(subject, advance);
    eve::cap::revoke<IClimbingEventSink>(&sink);
    eve::cap::revoke<IClimbingPoseAdapter>(&pose);
    REQUIRE(sentEvents.ok());
    REQUIRE(sentPose.ok());
    CHECK_EQ(sink.seenCount, 2u);
    CHECK_EQ(pose.seenAction, std::string("parkour:vault"));
}
}  // namespace test_climbing_controller_rules
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_climbing)
#include "PrivateTestScope.h"
#include "climbing/Climbing.h"
namespace test_climbing_unit {
TEST_CASE("Climbing runtime rejects invalid capsule and action without publishing either") {
    eve::climbing::ClimbingRuntime runtime;
    eve::climbing::ClimbingProfile profile;
    profile.capsuleRadius = .5f;
    profile.capsuleHeight = 1.f;
    auto invalidProfile   = runtime.setProfile(profile);
    REQUIRE(!invalidProfile.ok());
    CHECK(invalidProfile.code() == eve::StatusCode::Rejected);
    eve::climbing::ClimbingActionDefinition action;
    action.id          = "core:mantle";
    action.minHeight   = .4f;
    action.maxHeight   = 1.2f;
    action.duration    = eve::Duration::zero();
    auto invalidAction = runtime.upsertAction(std::move(action));
    REQUIRE(!invalidAction.ok());
    CHECK(invalidAction.code() == eve::StatusCode::Rejected);
}
}  // namespace test_climbing_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_climbing_events)
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include "climbing/ClimbingEventValue.h"
namespace test_climbing_local_boundary {
TEST_CASE("Climbing event kind table round trips every name and Value exposes stable wire fields") {
    std::vector<std::string> seen;
    for (std::uint8_t value = 0; value < eve::climbing::kClimbingEventKindCount; ++value) {
        const auto kind = static_cast<eve::climbing::ClimbingEventKind>(value);
        const auto name = eve::climbing::climbingEventKindName(kind);
        REQUIRE(!name.empty());
        eve::climbing::ClimbingEventKind parsed = eve::climbing::ClimbingEventKind::Failed;
        CHECK(eve::climbing::parseClimbingEventKind(name, parsed) == eve::climbing::EventKindParse::Parsed);
        CHECK(parsed == kind);
        seen.emplace_back(name);
    }
    std::sort(seen.begin(), seen.end());
    CHECK(std::adjacent_find(seen.begin(), seen.end()) == seen.end());
    eve::climbing::ClimbingEventKind unchanged = eve::climbing::ClimbingEventKind::Started;
    CHECK(eve::climbing::parseClimbingEventKind("unknown", unchanged) == eve::climbing::EventKindParse::Unknown);
    CHECK(unchanged == eve::climbing::ClimbingEventKind::Started);
    eve::climbing::ClimbingEvent event;
    event.kind        = eve::climbing::ClimbingEventKind::AnchorReached;
    event.actionId    = "climb:event_mantle";
    event.tick        = eve::SimulationTick(42);
    event.executionId = eve::climbing::ClimbingExecutionId(7);
    event.metadata    = {"event:parkour", "surface:stone"};
    const auto wire   = eve::climbing::climbingEventValue(event);
    REQUIRE(wire.isObject());
    CHECK_EQ(wire.find("kind")->asString(), std::string("anchor_reached"));
    CHECK_EQ(wire.find("tick")->asString(), std::string("42"));
    CHECK_EQ(wire.find("executionId")->asString(), std::string("7"));
    CHECK_EQ(wire.find("metadata")->arraySize(), 2u);
}
}  // namespace test_climbing_local_boundary
#endif
