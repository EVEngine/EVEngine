// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "physics-backend",
  "cases": {
    "module-composition": [
      "container.adaptersFailureInjectionLeavesMembershipUnchanged",
      "pixelworld_physics.terrain_contours_are_simplified_and_suppress_chunk_seams",
      "pixelworld_physics.character_probe_and_sweep_query_authoritative_material"
    ],
    "script-binding": [
      "climbing.services.scriptEventSinkFailureIsReportedNotSwallowed",
      "climbing.services.scriptProviderAnswersTheEventSinkThroughTheSeam"
    ]
  },
  "module": "climbing",
  "modules": [
    "building",
    "climbing",
    "fluids",
    "physics",
    "physics_backend",
    "physics_destruction",
    "physics_rope",
    "physics_trajectory",
    "pixelworld_physics"
  ],
  "origins": [
    "climbing_prediction.cpp",
    "climbing_reload.cpp",
    "climbing_services.cpp",
    "climbing_snapshot.cpp",
    "container_adapters.cpp",
    "fluids_volume_coupling.cpp",
    "gameplay_control_climbing.cpp",
    "gts_terrain_collider.cpp",
    "physics_artifact_provider.cpp",
    "physics_consumer_integration.cpp",
    "physics_contact_query.cpp",
    "physics_core_boundary.cpp",
    "physics_destruction.cpp",
    "physics_destruction_p3.cpp",
    "physics_joint_frames.cpp",
    "physics_jolt_cooking.cpp",
    "physics_mechanism.cpp",
    "physics_mechanism2d.cpp",
    "physics_rigid_backend.cpp",
    "physics_rope.cpp",
    "physics_trajectory_collision.cpp",
    "physics_world_snapshot_topology.cpp",
    "pixelworld_physics.cpp",
    "targeting.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_climbing_prediction)
#include "PrivateTestScope.h"
#include <memory>
#include "climbing/Climbing.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// climbing_prediction.cpp
namespace test_climbing_prediction {
namespace {

eve::Duration seconds(double value) {
    auto duration = eve::Duration::fromSeconds(value);
    REQUIRE(duration.ok());
    return std::move(duration).takeValue();
}

eve::climbing::ClimbingActionDefinition mantleAction() {
    eve::climbing::ClimbingActionDefinition action;
    action.id             = "core:mantle";
    action.kind           = eve::climbing::ClimbingActionKind::Mantle;
    action.minHeight      = 0.4f;
    action.maxHeight      = 1.2f;
    action.duration       = seconds(0.6);
    action.landingForward = 0.6f;
    action.apexHeight     = 0.7f;
    return action;
}

struct Fixture {
    Fixture() {
        world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        obstacle = world->newBody("static", 0.f, 0.5f, 1.f);
        shape    = obstacle->newBoxShape(2.f, 1.f, 0.5f);
    }

    eve::climbing::ClimbingPose pose() const { return {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1, 0.f, true}; }

    eve::climbing::ClimbingPredictionRequest requestFor(eve::climbing::ClimbingRuntime& runtime) const {
        auto candidates = runtime.probe(*world, pose());
        REQUIRE(candidates.ok());
        REQUIRE_EQ(candidates.value().size(), std::size_t(1));
        eve::climbing::ClimbingPredictionRequest request;
        request.sequence   = eve::climbing::ClimbingPredictionSequence(7);
        request.clientTick = eve::SimulationTick(100);
        request.candidate  = eve::climbing::makeClimbingCandidateKey(candidates.value().front(), 0);
        return request;
    }

    std::unique_ptr<eve::physics::World3D> world;
    eve::physics::Body3D*                  obstacle = nullptr;
    eve::physics::Shape3D*                 shape    = nullptr;
};

}  // namespace

TEST_CASE("climbing.prediction.acceptsOnlyServerReprobedExactCandidate") {
    Fixture                        fixture;
    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.upsertAction(mantleAction()).ok());
    auto request = fixture.requestFor(runtime);

    auto decision = runtime.tryBeginPredicted(*fixture.world, fixture.pose(), request, eve::SimulationTick(100), 2);
    REQUIRE(decision.ok());
    CHECK_EQ(static_cast<int>(decision.value().disposition),
             static_cast<int>(eve::climbing::ClimbingPredictionDisposition::Accepted));
    CHECK_EQ(static_cast<int>(decision.value().reason),
             static_cast<int>(eve::climbing::ClimbingPredictionReason::None));
    CHECK(decision.value().authoritativeCandidate == request.candidate);
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Requested));

    auto encoded = eve::climbing::encodeClimbingPredictionDecision(decision.value());
    REQUIRE(encoded.ok());
    auto decoded = eve::climbing::decodeClimbingPredictionDecision(encoded.value());
    REQUIRE(decoded.ok());
    CHECK_EQ(decoded.value().sequence.value(), std::uint64_t(7));
    CHECK(decoded.value().authoritativeCandidate == request.candidate);
}

TEST_CASE("climbing.prediction.mismatchCorrectsWithoutStartingClientTarget") {
    Fixture                        fixture;
    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.upsertAction(mantleAction()).ok());
    auto request = fixture.requestFor(runtime);
    request.candidate.fingerprint ^= 0x55u;

    auto decision = runtime.tryBeginPredicted(*fixture.world, fixture.pose(), request, eve::SimulationTick(100), 2);
    REQUIRE(decision.ok());
    CHECK_EQ(static_cast<int>(decision.value().disposition),
             static_cast<int>(eve::climbing::ClimbingPredictionDisposition::Corrected));
    CHECK_EQ(static_cast<int>(decision.value().reason),
             static_cast<int>(eve::climbing::ClimbingPredictionReason::CandidateMismatch));
    CHECK(decision.value().authoritativeCandidate.fingerprint != request.candidate.fingerprint);
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Idle));
}

TEST_CASE("climbing.prediction.tickLeadAndWireSchemaAreBounded") {
    Fixture                        fixture;
    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.upsertAction(mantleAction()).ok());
    auto request                            = fixture.requestFor(runtime);
    request.extensionMetadata["futureHint"] = eve::Value(std::string("keep-me"));

    auto encoded = eve::climbing::encodeClimbingPredictionRequest(request);
    REQUIRE(encoded.ok());
    auto decoded = eve::climbing::decodeClimbingPredictionRequest(encoded.value());
    REQUIRE(decoded.ok());
    CHECK(decoded.value().candidate == request.candidate);
    CHECK(decoded.value().extensionMetadata.contains("futureHint"));

    decoded.value().clientTick = eve::SimulationTick(110);
    auto rejected =
        runtime.tryBeginPredicted(*fixture.world, fixture.pose(), decoded.value(), eve::SimulationTick(100), 2);
    REQUIRE(rejected.ok());
    CHECK_EQ(static_cast<int>(rejected.value().disposition),
             static_cast<int>(eve::climbing::ClimbingPredictionDisposition::Rejected));
    CHECK_EQ(static_cast<int>(rejected.value().reason),
             static_cast<int>(eve::climbing::ClimbingPredictionReason::TickTooFarAhead));
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Idle));

    auto object             = *encoded.value().getIf<eve::Value::Object>();
    object["schemaVersion"] = eve::Value(std::int64_t(2));
    auto future             = eve::climbing::decodeClimbingPredictionRequest(eve::Value(std::move(object)));
    CHECK(!future.ok());
    REQUIRE(future.error() != nullptr);
    CHECK_EQ(static_cast<int>(future.error()->code()), static_cast<int>(eve::DiagnosticCode::UnknownVersion));
}

TEST_CASE("climbing.prediction.decisionRejectsMalformedOrFutureRuntimeSnapshot") {
    Fixture                        fixture;
    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.upsertAction(mantleAction()).ok());
    auto request  = fixture.requestFor(runtime);
    auto accepted = runtime.tryBeginPredicted(*fixture.world, fixture.pose(), request, eve::SimulationTick(100), 2);
    REQUIRE(accepted.ok());
    auto encoded = eve::climbing::encodeClimbingPredictionDecision(accepted.value());
    REQUIRE(encoded.ok());

    auto malformed = encoded.value();
    malformed.set("authoritativeSnapshot",
                  eve::Value::object({{"schemaId", "wrong"}, {"schemaVersion", std::int64_t(1)}}));
    auto malformedResult = eve::climbing::decodeClimbingPredictionDecision(malformed);
    CHECK(!malformedResult.ok());
    REQUIRE(malformedResult.error() != nullptr);
    CHECK_EQ(static_cast<int>(malformedResult.error()->code()), static_cast<int>(eve::DiagnosticCode::ParseError));

    auto future               = encoded.value();
    auto snapshot             = *future.find("authoritativeSnapshot")->getIf<eve::Value::Object>();
    snapshot["schemaVersion"] = eve::Value(eve::climbing::ClimbingRuntime::SnapshotSchemaVersion + 1);
    future.set("authoritativeSnapshot", eve::Value(std::move(snapshot)));
    auto futureResult = eve::climbing::decodeClimbingPredictionDecision(future);
    CHECK(!futureResult.ok());
    REQUIRE(futureResult.error() != nullptr);
    CHECK_EQ(static_cast<int>(futureResult.error()->code()), static_cast<int>(eve::DiagnosticCode::UnknownVersion));
}
}  // namespace test_climbing_prediction
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_climbing_reload)
#include "PrivateTestScope.h"
#include <memory>
#include "climbing/Climbing.h"
#include "climbing/ClimbingCodec.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// climbing_reload.cpp
namespace test_climbing_reload {
namespace {

struct ReloadWorld {
    ReloadWorld() {
        world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        body  = world->newBody("static", 0.f, 0.5f, 1.f);
        shape = body->newBoxShape(2.f, 1.f, 0.5f);
    }
    std::unique_ptr<eve::physics::World3D> world;
    eve::physics::Body3D*                  body  = nullptr;
    eve::physics::Shape3D*                 shape = nullptr;
};

eve::climbing::ClimbingProfileDefinition profile(float apexHeight) {
    eve::climbing::ClimbingActionDefinition action;
    action.id             = "mantle";
    action.minHeight      = 0.4f;
    action.maxHeight      = 1.2f;
    action.duration       = eve::Duration::fromNanoseconds(600000000);
    action.landingForward = 0.6f;
    action.apexHeight     = apexHeight;
    eve::climbing::ClimbingProfileDefinition result;
    result.actions.push_back(std::move(action));
    return result;
}

float pinnedApex(const eve::Value& snapshot) {
    const auto* execution = snapshot.find("execution");
    REQUIRE(execution != nullptr);
    const auto* executionObject = execution->getIf<eve::Value::Object>();
    REQUIRE(executionObject != nullptr);
    const auto* action = executionObject->at("action").getIf<eve::Value::Object>();
    REQUIRE(action != nullptr);
    return static_cast<float>(action->at("apexHeight").asDouble());
}

}  // namespace

TEST_CASE("climbing.reload.isAtomicAndPinsActiveDefinition") {
    ReloadWorld                    fixture;
    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.setProfile(profile(0.5f)).ok());
    const std::uint64_t oldGeneration = runtime.definitionGeneration();
    REQUIRE(
        runtime.tryBegin(*fixture.world, {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1}, eve::SimulationTick(10)).ok());

    REQUIRE(runtime.reloadProfile(profile(3.0f)).ok());
    CHECK(runtime.definitionGeneration() > oldGeneration);
    auto active = runtime.snapshot();
    REQUIRE(active.ok());
    CHECK(pinnedApex(active.value()) < 1.f);

    const std::uint64_t generationAfterReload = runtime.definitionGeneration();
    auto                invalid               = profile(2.f);
    invalid.capsuleRadius                     = -1.f;
    CHECK(!runtime.reloadProfile(std::move(invalid)).ok());
    CHECK_EQ(runtime.definitionGeneration(), generationAfterReload);
    auto afterFailure = runtime.snapshot();
    REQUIRE(afterFailure.ok());
    CHECK(afterFailure.value() == active.value());

    REQUIRE(runtime.cancel(eve::climbing::ClimbingCancelReason::PlayerRequest, eve::SimulationTick(10)).ok());
    REQUIRE(
        runtime.tryBegin(*fixture.world, {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1}, eve::SimulationTick(11)).ok());
    auto next = runtime.snapshot();
    REQUIRE(next.ok());
    CHECK(pinnedApex(next.value()) > 2.f);
}

TEST_CASE("climbing.reload.jsonProfileAndRuntimeRoundTrip") {
    ReloadWorld                    fixture;
    eve::climbing::ClimbingRuntime source;
    auto                           encodedProfile = eve::climbing::encodeClimbingProfileDefinition(profile(0.75f));
    REQUIRE(encodedProfile.ok());
    auto profileJson = encodedProfile.value().toJson();
    REQUIRE(profileJson.ok());
    REQUIRE(source.setProfileJson(profileJson.value()).ok());
    REQUIRE(source.tryBegin(*fixture.world, {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1}, eve::SimulationTick(20)).ok());
    auto json = source.snapshotJson();
    REQUIRE(json.ok());

    eve::climbing::ClimbingRuntime restored;
    REQUIRE(restored.restoreJson(json.value(), *fixture.world).ok());
    CHECK_EQ(restored.definitionGeneration(), source.definitionGeneration());
    auto restoredJson = restored.snapshotJson();
    REQUIRE(restoredJson.ok());
    CHECK_EQ(restoredJson.value(), json.value());

    const std::string before = restoredJson.value();
    CHECK(!restored.restoreJson("{\"schemaId\":", *fixture.world).ok());
    auto after = restored.snapshotJson();
    REQUIRE(after.ok());
    CHECK_EQ(after.value(), before);
}
}  // namespace test_climbing_reload
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_climbing_services)
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "PrivateTestScope.h"
#include "ScriptTest.h"
#include "climbing/ClimbingServices.h"
#include "common/Capability.h"
#include "common/ScriptProvider.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// climbing_services.cpp
namespace test_climbing_services {
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

TEST_CASE("climbing.services.requiredStaminaAbsenceIsObservableAndTransactional") {
    ServiceWorld                   fixture;
    eve::climbing::ClimbingRuntime runtime;
    auto                           profile = serviceProfile();
    REQUIRE(runtime.setProfile(profile).ok());
    eve::climbing::ClimbingIntent intent;
    REQUIRE(eve::climbing::ClimbingInputSystem::submit(intent, eve::climbing::ClimbingCommand::Jump,
                                                       eve::SimulationTick(4), profile)
                .ok());

    auto started = eve::climbing::ClimbingServiceSelectionSystem::tryStart(
        runtime, *fixture.world, pose(fixture), intent, eve::climbing::ClimbingCommand::Jump, eve::SimulationTick(4),
        eve::SimulationTick(4), eve::climbing::ClimbingServiceSubject(9));
    CHECK(!started.ok());
    CHECK_EQ(static_cast<int>(started.error()->code()), static_cast<int>(eve::DiagnosticCode::Unsupported));
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Idle));
    CHECK(eve::climbing::ClimbingInputSystem::peek(intent, eve::climbing::ClimbingCommand::Jump, eve::SimulationTick(4))
              .has_value());
}

TEST_CASE("climbing.services.presentStaminaCommitsExactlyOnceWithExecution") {
    ServiceWorld                   fixture;
    eve::climbing::ClimbingRuntime runtime;
    auto                           profile = serviceProfile();
    REQUIRE(runtime.setProfile(profile).ok());
    eve::climbing::ClimbingIntent intent;
    REQUIRE(eve::climbing::ClimbingInputSystem::submit(intent, eve::climbing::ClimbingCommand::Jump,
                                                       eve::SimulationTick(5), profile)
                .ok());
    Stamina stamina;
    eve::cap::provide<eve::climbing::IClimbingStaminaAuthority>(&stamina);

    auto started = eve::climbing::ClimbingServiceSelectionSystem::tryStart(
        runtime, *fixture.world, pose(fixture), intent, eve::climbing::ClimbingCommand::Jump, eve::SimulationTick(5),
        eve::SimulationTick(5), eve::climbing::ClimbingServiceSubject(11));
    eve::cap::revoke<eve::climbing::IClimbingStaminaAuthority>(&stamina);
    REQUIRE(started.ok());
    CHECK_EQ(static_cast<int>(started.value().stamina),
             static_cast<int>(eve::climbing::ClimbingOptionalServiceState::Applied));
    CHECK_EQ(stamina.prepareCount, 1);
    CHECK_EQ(stamina.commitCount, 1);
    CHECK_EQ(stamina.cancelCount, 0);
    CHECK(std::fabs(stamina.preparedCost - 12.f) < 0.0001f);
    CHECK(stamina.committedExecution == started.value().start.executionId);
}

TEST_CASE("climbing.services.staminaAdapterIdentityMismatchIsTransactional") {
    ServiceWorld                   fixture;
    eve::climbing::ClimbingRuntime runtime;
    auto                           profile = serviceProfile();
    REQUIRE(runtime.setProfile(profile).ok());
    eve::climbing::ClimbingIntent intent;
    REQUIRE(eve::climbing::ClimbingInputSystem::submit(intent, eve::climbing::ClimbingCommand::Jump,
                                                       eve::SimulationTick(6), profile)
                .ok());
    Stamina stamina;
    stamina.id = "test:other-stamina";
    eve::cap::provide<eve::climbing::IClimbingStaminaAuthority>(&stamina);
    auto rejected = eve::climbing::ClimbingServiceSelectionSystem::tryStart(
        runtime, *fixture.world, pose(fixture), intent, eve::climbing::ClimbingCommand::Jump, eve::SimulationTick(6),
        eve::SimulationTick(6), eve::climbing::ClimbingServiceSubject(12));
    eve::cap::revoke<eve::climbing::IClimbingStaminaAuthority>(&stamina);

    CHECK(!rejected.ok());
    CHECK_EQ(static_cast<int>(rejected.error()->code()), static_cast<int>(eve::DiagnosticCode::Unsupported));
    CHECK_EQ(stamina.prepareCount, 0);
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Idle));
    CHECK(eve::climbing::ClimbingInputSystem::peek(intent, eve::climbing::ClimbingCommand::Jump, eve::SimulationTick(6))
              .has_value());
}


TEST_CASE("climbing.services.conditionTagsRequireAuthorityAndGateCommit") {
    ServiceWorld fixture;
    auto         profile  = serviceProfile();
    profile.staminaPolicy = eve::climbing::ClimbingStaminaPolicy::Disabled;
    profile.staminaAdapter.clear();
    profile.actions.front().staminaCost           = 0.f;
    profile.actions.front().requiredConditionTags = {"state:not_stunned", "ability:parkour"};

    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.setProfile(profile).ok());
    eve::climbing::ClimbingIntent intent;
    REQUIRE(eve::climbing::ClimbingInputSystem::submit(intent, eve::climbing::ClimbingCommand::Jump,
                                                       eve::SimulationTick(30), profile)
                .ok());
    auto absent = eve::climbing::ClimbingServiceSelectionSystem::tryStart(
        runtime, *fixture.world, pose(fixture), intent, eve::climbing::ClimbingCommand::Jump, eve::SimulationTick(30),
        eve::SimulationTick(30), eve::climbing::ClimbingServiceSubject(31));
    CHECK(!absent.ok());
    CHECK_EQ(static_cast<int>(absent.error()->code()), static_cast<int>(eve::DiagnosticCode::Unsupported));
    CHECK(
        eve::climbing::ClimbingInputSystem::peek(intent, eve::climbing::ClimbingCommand::Jump, eve::SimulationTick(30))
            .has_value());

    Conditions conditions;
    conditions.decision = eve::climbing::ClimbingConditionDecision::Denied;
    eve::cap::provide<eve::climbing::IClimbingConditionAuthority>(&conditions);
    auto denied = eve::climbing::ClimbingServiceSelectionSystem::tryStart(
        runtime, *fixture.world, pose(fixture), intent, eve::climbing::ClimbingCommand::Jump, eve::SimulationTick(30),
        eve::SimulationTick(30), eve::climbing::ClimbingServiceSubject(31));
    CHECK(!denied.ok());
    CHECK_EQ(static_cast<int>(denied.error()->code()), static_cast<int>(eve::DiagnosticCode::PreconditionViolation));

    conditions.decision = eve::climbing::ClimbingConditionDecision::Allowed;
    auto allowed        = eve::climbing::ClimbingServiceSelectionSystem::tryStart(
        runtime, *fixture.world, pose(fixture), intent, eve::climbing::ClimbingCommand::Jump, eve::SimulationTick(30),
        eve::SimulationTick(30), eve::climbing::ClimbingServiceSubject(31));
    eve::cap::revoke<eve::climbing::IClimbingConditionAuthority>(&conditions);
    REQUIRE(allowed.ok());
    CHECK_EQ(conditions.seenCount, std::size_t{2});
    CHECK(conditions.seenSubject == eve::climbing::ClimbingServiceSubject(31));
}

// ---------------------------------------------------------------------------
// 决定③第二个落地的插座：`IClimbingEventSink` 可以由脚本提供。
//
// 与上面那条原生 sink 用例配对：同一条派发路径（`dispatchClimbingEvents`）在"没有 sink"时
// 报 `ProviderAbsent`、在脚本在位时变成 `Applied`，而脚本看到的事件记录与它在
// `eve.ClimbingRuntime` 上看到的形状完全一致（同一份 `climbingEventValue`）。
// ---------------------------------------------------------------------------

namespace {

const char* kScriptClimbingSinkContent = R"SQ(
// 观察用的根表槽位：先声明再在方法里赋值。
lastSubject <- 0
lastCount <- 0
lastKind <- ""
lastActionId <- ""
lastMetadataCount <- 0

class EventSink {
    function publishClimbingEvents(payload) {
        lastSubject = payload.subject
        lastCount = payload.events.len()
        if (lastCount > 0) {
            lastKind = payload.events[0].kind
            lastActionId = payload.events[0].actionId
            lastMetadataCount = payload.events[0].metadata.len()
        }
    }
}

class FailingEventSink {
    function publishClimbingEvents(payload) { throw "script refused the climbing event batch" }
}

eventSink <- EventSink()
failingEventSink <- FailingEventSink()
)SQ";

}  // namespace

/** 脚本提供方要一个真 VM；接缝由用例显式声明，所以不依赖 Climbing 模块是否被构造。 */
class ScriptClimbingSinkTest : public ScriptTest {
public:
    ScriptClimbingSinkTest() : ScriptTest(kScriptClimbingSinkContent) {
        auto& registry = eve::ScriptProviderRegistry::instance();
        registry.clearAll();
        declaredBefore_ = registry.capabilities();
        eve::cap::detail::clearAllRaw();
    }

    ~ScriptClimbingSinkTest() {
        eve::climbing::withdrawScriptClimbingEventSeam();
        eve::cap::detail::clearAllRaw();
        auto& registry = eve::ScriptProviderRegistry::instance();
        registry.clearAll();
        for (const std::string& capability : registry.capabilities()) {
            if (std::find(declaredBefore_.begin(), declaredBefore_.end(), capability) != declaredBefore_.end())
                continue;
            registry.removeCapability(capability).ignore("fixture teardown withdraws only this case's seams");
        }
    }

    static std::vector<eve::climbing::ClimbingEvent> batch() {
        std::vector<eve::climbing::ClimbingEvent> events(2);
        events[0].kind        = eve::climbing::ClimbingEventKind::Started;
        events[0].actionId    = "climb:event_mantle";
        events[0].tick        = eve::SimulationTick(11);
        events[0].executionId = eve::climbing::ClimbingExecutionId(3);
        events[0].metadata    = {"event:parkour", "surface:stone"};
        events[1].kind        = eve::climbing::ClimbingEventKind::Landed;
        events[1].actionId    = "climb:event_mantle";
        events[1].tick        = eve::SimulationTick(12);
        events[1].executionId = eve::climbing::ClimbingExecutionId(3);
        return events;
    }

private:
    std::vector<std::string> declaredBefore_;
};

TEST_CASE_FIXTURE(ScriptClimbingSinkTest, "climbing.services.scriptProviderAnswersTheEventSinkThroughTheSeam") {
    using namespace eve;
    const climbing::ClimbingServiceSubject subject(21);
    const auto                             events = batch();

    // ① 缺席：接缝没声明时发布直接被拒，派发报 ProviderAbsent（不是"绑着但不响"）。
    auto refused = ScriptProviderRegistry::instance().publishForCapability(
        climbing::IClimbingEventSink::capabilityName, "test.climbing-events", vm.find("eventSink"));
    REQUIRE(!refused.ok());
    auto absent = climbing::dispatchClimbingEvents(subject, events);
    REQUIRE(absent.ok());
    CHECK_EQ(static_cast<int>(absent.value()),
             static_cast<int>(climbing::ClimbingOptionalServiceState::ProviderAbsent));

    // ② 声明 + 发布：适配器进能力槽位，派发变成 Applied。
    climbing::declareScriptClimbingEventSeam();
    auto published = ScriptProviderRegistry::instance().publishForCapability(
        climbing::IClimbingEventSink::capabilityName, "test.climbing-events", vm.find("eventSink"));
    REQUIRE(published.ok());
    REQUIRE(cap::query<climbing::IClimbingEventSink>() != nullptr);

    auto applied = climbing::dispatchClimbingEvents(subject, events);
    REQUIRE(applied.ok());
    CHECK_EQ(static_cast<int>(applied.value()), static_cast<int>(climbing::ClimbingOptionalServiceState::Applied));
    REQUIRE_EQ(vm.find("lastSubject").toInt(), 21);
    REQUIRE_EQ(vm.find("lastCount").toInt(), 2);
    REQUIRE_EQ(vm.find("lastKind").toString(), std::string("started"));
    REQUIRE_EQ(vm.find("lastActionId").toString(), std::string("climb:event_mantle"));
    REQUIRE_EQ(vm.find("lastMetadataCount").toInt(), 2);

    // ③ 撤回后能力槽位重新为空，派发回到 ProviderAbsent。
    REQUIRE(ScriptProviderRegistry::instance().unload(published.value()).ok());
    REQUIRE(cap::query<climbing::IClimbingEventSink>() == nullptr);
    auto afterWithdrawal = climbing::dispatchClimbingEvents(subject, events);
    REQUIRE(afterWithdrawal.ok());
    CHECK_EQ(static_cast<int>(afterWithdrawal.value()),
             static_cast<int>(climbing::ClimbingOptionalServiceState::ProviderAbsent));
}

TEST_CASE_FIXTURE(ScriptClimbingSinkTest, "climbing.services.scriptEventSinkFailureIsReportedNotSwallowed") {
    using namespace eve;
    climbing::declareScriptClimbingEventSeam();
    auto published = ScriptProviderRegistry::instance().publishForCapability(
        climbing::IClimbingEventSink::capabilityName, "test.climbing-events", vm.find("failingEventSink"));
    REQUIRE(published.ok());

    // 脚本抛错 → 派发失败（消费方靠返回值判断，吞掉会让上层以为事件已经送达）。
    auto failed = climbing::dispatchClimbingEvents(climbing::ClimbingServiceSubject(21), batch());
    CHECK(!failed.ok());
    CHECK_EQ(static_cast<int>(failed.error()->code()), static_cast<int>(DiagnosticCode::Failed));

    // 提供方还能用：换回不抛错的那个脚本对象，派发恢复。
    REQUIRE(ScriptProviderRegistry::instance().unload(published.value()).ok());
    auto recovered = ScriptProviderRegistry::instance().publishForCapability(
        climbing::IClimbingEventSink::capabilityName, "test.climbing-events", vm.find("eventSink"));
    REQUIRE(recovered.ok());
    CHECK(climbing::dispatchClimbingEvents(climbing::ClimbingServiceSubject(21), batch()).ok());
}
}  // namespace test_climbing_services
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_climbing_snapshot)
#include "PrivateTestScope.h"
#include <memory>
#include "climbing/Climbing.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// climbing_snapshot.cpp
namespace test_climbing_snapshot {
namespace {

struct World {
    World() {
        world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        body  = world->newBody("static", 0.f, 0.5f, 1.f);
        shape = body->newBoxShape(2.f, 1.f, 0.5f);
    }
    std::unique_ptr<eve::physics::World3D> world;
    eve::physics::Body3D*                  body  = nullptr;
    eve::physics::Shape3D*                 shape = nullptr;
};

eve::climbing::ClimbingActionDefinition action(float apex = 0.7f) {
    eve::climbing::ClimbingActionDefinition result;
    result.id             = "climb:mantle";
    result.minHeight      = 0.4f;
    result.maxHeight      = 1.2f;
    result.duration       = eve::Duration::fromNanoseconds(600000000);
    result.landingForward = 0.6f;
    result.apexHeight     = apex;
    return result;
}

void begin(World& fixture, eve::climbing::ClimbingRuntime& runtime) {
    REQUIRE(runtime.upsertAction(action()).ok());
    auto begun = runtime.tryBegin(*fixture.world, {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1}, eve::SimulationTick(10));
    REQUIRE(begun.ok());
}

}  // namespace

TEST_CASE("climbing.snapshot.roundTripRestoresPinnedExecution") {
    World                          fixture;
    eve::climbing::ClimbingRuntime source;
    begin(fixture, source);
    auto snapshot = source.snapshot();
    REQUIRE(snapshot.ok());

    eve::climbing::ClimbingRuntime restored;
    REQUIRE(restored.restore(snapshot.value(), *fixture.world).ok());
    CHECK_EQ(static_cast<int>(restored.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Requested));
    CHECK(restored.executionId() == source.executionId());
    auto roundTrip = restored.snapshot();
    REQUIRE(roundTrip.ok());
    CHECK(roundTrip.value() == snapshot.value());
}

TEST_CASE("climbing.snapshot.supportsNMinusOneAndPinsActiveDefinitionAcrossReload") {
    World                          fixture;
    eve::climbing::ClimbingRuntime runtime;
    begin(fixture, runtime);
    REQUIRE(runtime.upsertAction(action(4.0f)).ok());

    auto snapshot = runtime.snapshot();
    REQUIRE(snapshot.ok());
    snapshot.value().set("schemaVersion",
                         eve::Value(std::int64_t(eve::climbing::ClimbingRuntime::SnapshotSchemaVersion - 1)));

    eve::climbing::ClimbingRuntime restored;
    REQUIRE(restored.restore(snapshot.value(), *fixture.world).ok());
    auto migrated = restored.snapshot();
    REQUIRE(migrated.ok());
    CHECK_EQ(migrated.value().find("schemaVersion")->asInt(), eve::climbing::ClimbingRuntime::SnapshotSchemaVersion);
    const auto* restoredExecution = migrated.value().find("execution")->getIf<eve::Value::Object>();
    REQUIRE(restoredExecution != nullptr);
    const auto* pinnedAction = restoredExecution->at("action").getIf<eve::Value::Object>();
    REQUIRE(pinnedAction != nullptr);
    CHECK(pinnedAction->at("apexHeight").asDouble() < 1.0);
}

TEST_CASE("climbing.snapshot.futureVersionAndStaleLinkFailuresAreAtomic") {
    World                          fixture;
    eve::climbing::ClimbingRuntime runtime;
    begin(fixture, runtime);
    auto before = runtime.snapshot();
    REQUIRE(before.ok());

    eve::Value future = before.value();
    future.set("schemaVersion", eve::Value(std::int64_t(eve::climbing::ClimbingRuntime::SnapshotSchemaVersion + 1)));
    auto unknown = runtime.restore(future, *fixture.world);
    CHECK(!unknown.ok());
    CHECK_EQ(static_cast<int>(unknown.error()->code()), static_cast<int>(eve::DiagnosticCode::UnknownVersion));
    auto afterUnknown = runtime.snapshot();
    REQUIRE(afterUnknown.ok());
    CHECK(afterUnknown.value() == before.value());

    fixture.world->destroyBody(fixture.body);
    fixture.body  = nullptr;
    fixture.shape = nullptr;
    auto stale    = runtime.restore(before.value(), *fixture.world);
    CHECK(!stale.ok());
    CHECK_EQ(static_cast<int>(stale.error()->code()), static_cast<int>(eve::DiagnosticCode::StaleHandle));
    auto afterStale = runtime.snapshot();
    REQUIRE(afterStale.ok());
    CHECK(afterStale.value() == before.value());
}
}  // namespace test_climbing_snapshot
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_container_adapters)
#include "PrivateTestScope.h"
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include "building/BuildingDef.h"
#include "building/ContainerAdapters.h"
#include "building/PlacementSystem.h"
#include "building/PlacementWorld.h"
#include "common/Container.h"
#include "game_event/GameEvent.h"
#include "vehicle/ContainerAdapters.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// container_adapters.cpp
namespace test_container_adapters {
namespace {

using eve::container::Capacity;
using eve::container::ContainerId;
using eve::container::MembershipId;
using eve::container::SlotIndex;

void registerTestBuilding(const char* id) {
    eve::building::BuildingRegistry::clear();
    eve::building::PlacementSystem::ensureBuiltins();
    eve::building::BuildingDefinition definition;
    definition.id         = id;
    definition.footprintW = 1;
    definition.footprintH = 1;
    eve::building::BuildingRegistry::registerBuilding(definition);
}

}  // namespace

TEST_CASE("container.adaptersShareParityAndCanonicalLifecycleEnvelope") {
    using namespace eve::container;

    static_assert(!std::is_convertible_v<Coordinate<ScreenSpace>, Coordinate<World2DSpace>>);
    static_assert(!std::is_convertible_v<Coordinate<World2DSpace>, Coordinate<World3DSpace>>);
    static_assert(!std::is_convertible_v<Coordinate<GridSpace>, Coordinate<ScreenSpace>>);

    auto zone = Zone<World2DSpace>::create(ContainerId("zone:world2d"), Rectangle<World2DSpace>{{0.f, 0.f}, 10.f, 10.f},
                                           Capacity::fixed(3));
    REQUIRE(zone.ok());
    CHECK_EQ(zone.value().id().value(), std::string("zone:world2d"));
    CHECK(zone.value().contains(Coordinate<World2DSpace>{5.f, 5.f}));

    ecs::Table       ecsWorld;
    ecs::ScopedTable guard(ecsWorld);
    auto*            vehicle = eve::vehicle::VehicleEntity::create();
    vehicle->identity()->id  = "vehicle:test";
    vehicle->seats()->list.resize(2);

    eve::vehicle::VehicleSeatContainerAdapter seats(ContainerId("vehicle:seat:test"), vehicle, AcceptedCondition{});
    registerTestBuilding("garrison.test");
    eve::building::PlacementWorld placement(4, 4, 1.f);
    placement.setId("world:test");
    const int buildingId = placement.placeAt("garrison.test", 0, 0);
    REQUIRE(buildingId > 0);
    eve::building::BuildingGarrisonContainerAdapter garrison(ContainerId("building:garrison:test"), &placement,
                                                             buildingId, Capacity::fixed(3));

    CHECK_EQ(static_cast<int>(seats.descriptor().ordering), static_cast<int>(Ordering::ExplicitSlots));
    CHECK_EQ(static_cast<int>(garrison.descriptor().ordering), static_cast<int>(Ordering::Insertion));
    CHECK_EQ(seats.descriptor().capacity.value(), std::size_t(2));
    CHECK_EQ(garrison.descriptor().capacity.value(), std::size_t(3));

    std::vector<std::string> eventTypes;
    auto                     sink = [&](const eve::game_event::GameEvent& event) {
        CHECK(!event.eventId.isNil());
        CHECK_EQ(event.schemaId.format(), std::string("container:event"));
        CHECK_EQ(event.schemaVersion.value(), 1u);
        CHECK_EQ(event.source, std::string("vehicle:seat:test"));
        CHECK_EQ(event.tick, eve::SimulationTick(17));
        eventTypes.push_back(event.type);
    };

    auto entered = seats.enter(SlotIndex(0), 7, sink, eve::SimulationTick(17));
    REQUIRE(entered.ok());
    CHECK_EQ(eventTypes.size(), std::size_t(2));
    CHECK_EQ(eventTypes[0], std::string("container.accepted"));
    CHECK_EQ(eventTypes[1], std::string("container.enter"));

    const auto revisionBeforeReject = seats.revision();
    auto       rejected             = seats.enter(SlotIndex(0), 8, sink, eve::SimulationTick(17));
    CHECK(!rejected.ok());
    CHECK_EQ(eventTypes.back(), std::string("container.rejected"));
    CHECK_EQ(seats.revision(), revisionBeforeReject);
    REQUIRE(seats.snapshot().ok());
    CHECK_EQ(seats.snapshot().value().entries.size(), std::size_t(1));

    auto callbackFailure = seats.enter(
        SlotIndex(1), 8, [](const eve::game_event::GameEvent&) { throw std::runtime_error("observer"); },
        eve::SimulationTick(18));
    REQUIRE(callbackFailure.ok());
    CHECK_EQ(static_cast<int>(callbackFailure.code()), static_cast<int>(eve::StatusCode::Applied));
    CHECK(callbackFailure.status().hasDiagnostics());
    CHECK(vehicle->seats()->list[1].occupied);

    const auto oldGarrison = garrison.snapshot();
    REQUIRE(oldGarrison.ok());
    auto garrisoned = garrison.enter("unit:one", "rts.unit", {"infantry"}, {}, eve::SimulationTick(19));
    REQUIRE(garrisoned.ok());
    CHECK_EQ(garrison.snapshot().value().entries.size(), std::size_t(1));
    auto stalePrepare = garrison.prepare(oldGarrison.value(), oldGarrison.value());
    CHECK(!stalePrepare.ok());
    CHECK_EQ(garrison.snapshot().value().entries.size(), std::size_t(1));

    std::vector<std::string> garrisonEventTypes;
    auto                     garrisonSink = [&](const eve::game_event::GameEvent& event) {
        CHECK_EQ(event.schemaId.format(), std::string("container:event"));
        CHECK_EQ(event.source, std::string("building:garrison:test"));
        CHECK_EQ(event.subject, std::string("unit:one"));
        CHECK_EQ(event.tick, eve::SimulationTick(20));
        garrisonEventTypes.push_back(event.type);
    };
    auto exited = garrison.exit(MembershipId("unit:one"), garrisonSink, eve::SimulationTick(20));
    REQUIRE(exited.ok());
    CHECK_EQ(garrisonEventTypes.size(), std::size_t(2));
    CHECK_EQ(garrisonEventTypes[0], std::string("container.accepted"));
    CHECK_EQ(garrisonEventTypes[1], std::string("container.exit"));

    // Cross-adapter transfer uses only common ContainerObject facts; neither
    // adapter needs to include the other's module or take ownership of it.
    auto movedToGarrison = eve::container::TransferService::transfer(
        TransferRequest{&seats, &garrison, MembershipId("vehicle:occupant:7"), SlotIndex(0), std::nullopt,
                        seats.revision(), garrison.revision()});
    REQUIRE(movedToGarrison.ok());
    CHECK(!vehicle->seats()->list[0].occupied);
    CHECK_EQ(garrison.snapshot().value().entries.size(), std::size_t(2));

    auto movedBack = eve::container::TransferService::transfer(
        TransferRequest{&garrison, &seats, MembershipId("vehicle:occupant:7"), std::nullopt, SlotIndex(0),
                        garrison.revision(), seats.revision()});
    REQUIRE(movedBack.ok());
    CHECK(vehicle->seats()->list[0].occupied);
    CHECK_EQ(vehicle->seats()->list[0].occupant, 7);

    eve::building::BuildingRegistry::clear();
}

TEST_CASE("container.adaptersFailureInjectionLeavesMembershipUnchanged") {
    registerTestBuilding("garrison.failure");
    eve::building::PlacementWorld placement(2, 2, 1.f);
    const int                     buildingId = placement.placeAt("garrison.failure", 0, 0);
    REQUIRE(buildingId > 0);
    eve::building::BuildingGarrisonContainerAdapter garrison(ContainerId("building:garrison:failure"), &placement,
                                                             buildingId, Capacity::fixed(1));
    REQUIRE(garrison.enter("unit:one").ok());
    const auto before = garrison.snapshot();
    REQUIRE(before.ok());

    auto full = garrison.enter("unit:two");
    CHECK(!full.ok());
    CHECK_EQ(garrison.revision(), before.value().revision);
    auto after = garrison.snapshot();
    REQUIRE(after.ok());
    CHECK_EQ(after.value().entries.size(), std::size_t(1));
    CHECK_EQ(after.value().entries.front().membership.object.value(), std::string("unit:one"));

    eve::building::BuildingRegistry::clear();
}
}  // namespace test_container_adapters
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fluids_volume_coupling)
#include "PrivateTestScope.h"
#include <cmath>
#include <memory>
#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidCoupling.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// fluids_volume_coupling.cpp
namespace test_fluids_volume_coupling {
using namespace eve::fluids;

TEST_CASE("fluids.volumeCoupling.attachmentReturnsGravityLoadAndTorque") {
    auto*                                  physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.f, 0.f, 0.f, false));
    auto*                                  body = world->newBody("dynamic", 0.f, 0.f, 0.f);
    body->newBoxShape(1.f, 1.f, 1.f, 10.f);
    VolumeFluidSettings settings;
    settings.gravity = {0.f, -10.f, 0.f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                fluid = std::move(created).takeValue();
    VolumeFluidSnapshot snapshot;
    snapshot.settings = settings;
    VolumeFluidParticle particle;
    particle.position       = {.5f, 1.f, 0.f};
    particle.material.phase = VolumeFluidPhase::Solid;
    snapshot.particles.push_back(particle);
    VolumeFluidCollider collider;
    collider.label    = 17;
    collider.radius   = .1f;
    collider.solidify = true;
    snapshot.colliders.push_back(collider);
    snapshot.attachments.push_back({0, 17, {.5f, 0.f, 0.f}});
    REQUIRE(fluid->restore(snapshot).ok());
    VolumeFluidCoupling bridge;
    REQUIRE(bridge.attach(*body, collider).ok());
    const auto stepped = bridge.step(*world, *fluid, 1.f / 120.f, 1);
    REQUIRE(stepped.ok());
    REQUIRE(stepped.value() == 1u);
    REQUIRE(std::abs(body->getLinearVelocityY() + 1.f / 120.f) < 1e-5f);
    REQUIRE(body->getAngularVelocityZ() < 0.f);
    REQUIRE(fluid->attachmentReactions().size() == 1);
    REQUIRE(fluid->contacts().empty());
}


TEST_CASE("fluids.volumeCoupling.extremeMassRatioUsesObservableVelocityLimits") {
    auto*                                  physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.f, 0.f, 0.f, false));
    auto*                                  body = world->newBody("dynamic", 0.f, 0.f, 0.f);
    body->newBoxShape(.1f, .1f, .1f, .1f);
    body->setAngularVelocity(0.f, 0.f, 10.f);
    VolumeFluidSettings settings;
    settings.gravity = {0.f, -100.f, 0.f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                fluid = std::move(created).takeValue();
    VolumeFluidSnapshot snapshot;
    snapshot.settings = settings;
    for (unsigned i = 0; i < 16; ++i) {
        VolumeFluidParticle p;
        p.position       = {-.3f + .04f * i, 1.f, 0.f};
        p.material.phase = VolumeFluidPhase::Solid;
        p.radii          = {.05f, .1f, .05f};
        snapshot.particles.push_back(p);
        snapshot.attachments.push_back({i, 9, p.position - glm::vec3(0, 1, 0)});
    }
    VolumeFluidCollider collider;
    collider.label  = 9;
    collider.radius = .01f;
    snapshot.colliders.push_back(collider);
    REQUIRE(fluid->restore(snapshot).ok());
    VolumeFluidCoupling bridge;
    REQUIRE(bridge.attach(*body, collider).ok());
    REQUIRE(bridge.setImpulseLimits(.5f, .25f).ok());
    REQUIRE(!bridge.setImpulseLimits(0.f, .25f).ok());
    REQUIRE(bridge.step(*world, *fluid, 1.f / 120.f, 1).ok());
    REQUIRE(std::abs(body->getLinearVelocityY()) <= .5001f);
    REQUIRE(std::abs(body->getAngularVelocityZ() - 10.f) <= .2501f);
    REQUIRE(bridge.lastClampedBodyCount() == 1u);
}

TEST_CASE("fluids.volumeCoupling.sdfObstacleTransfersImpulseToDynamicBody") {
    auto*                                  physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.f, 0.f, 0.f, false));
    // Body origin is the SDF frame; matches fluids.volume.ownedSdfColliderObstacle geometry.
    auto* body = world->newBody("dynamic", .2f, 0.f, 0.f);
    body->newSphereShape(.75f, 2.f);
    VolumeFluidSettings settings;
    settings.gravity    = {0.f, 0.f, 0.f};
    settings.iterations = 1;
    settings.minimum    = {-1.f, .2f, -.8f};
    settings.maximum    = {1.4f, 1.8f, .8f};
    auto created        = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto fluid = std::move(created).takeValue();

    VolumeFluidSdfCollider local;
    local.label    = 41;
    local.sdf      = MeshSdf::makeSphere({0.f, 1.f, 0.f}, .5f, {24, 24, 24});
    local.scale    = 1.5f;
    local.friction = 0.f;
    VolumeFluidCoupling bridge;
    REQUIRE(bridge.attachSdf(*body, local).ok());
    REQUIRE(!bridge.attachSdf(*body, local).ok());

    VolumeFluidParticle particle;
    particle.position           = {.9f, 1.5f, 0.f};
    particle.material.cohesion  = 0.f;
    particle.material.viscosity = 0.f;
    REQUIRE(fluid->emit(std::span(&particle, 1)).ok());

    const float beforeVx = body->getLinearVelocityX();
    const auto  stepped  = bridge.step(*world, *fluid, 1.f / 120.f, 1);
    REQUIRE(stepped.ok());
    REQUIRE(!fluid->contacts().empty());
    REQUIRE(fluid->contacts()[0].colliderLabel == 41u);
    REQUIRE(fluid->particles()[0].position.x > .98f);
    REQUIRE(body->getLinearVelocityX() != beforeVx);
    bridge.detach(41);
    REQUIRE(bridge.step(*world, *fluid, 1.f / 120.f, 1).ok());
}
}  // namespace test_fluids_volume_coupling
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_gameplay_control_climbing)
#include "PrivateTestScope.h"
#include <memory>
#include "climbing/ClimbingControl.h"
#include "common/Capability.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// gameplay_control_climbing.cpp
namespace test_gameplay_control_climbing {
namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::LogicalId action(const char* text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
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

eve::climbing::ClimbingActionDefinition mantle() {
    eve::climbing::ClimbingActionDefinition result;
    result.id             = "climb:gameplay-mantle";
    result.minHeight      = 0.4f;
    result.maxHeight      = 1.2f;
    result.duration       = eve::Duration::fromNanoseconds(600000000);
    result.landingForward = 0.6f;
    result.apexHeight     = 0.7f;
    return result;
}

eve::GameplayCommand command(const char* commandId, const char* actionId, eve::SubjectRef character,
                             const eve::GameplayObservation& observation) {
    eve::GameplayCommand result;
    result.id               = commandId;
    result.action           = action(actionId);
    result.subject          = character;
    result.observedTick     = observation.tick;
    result.expectedRevision = observation.revision;
    result.parameters       = eve::Value(eve::Value::Object{});
    return result;
}

}  // namespace

TEST_CASE("gameplay.control.climbingReprobesAuthorityForPlayerAndAutomation") {
    ClimbingWorld                  fixture;
    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.upsertAction(mantle()).ok());
    const eve::climbing::ClimbingPose pose{{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1};
    const auto                        instance  = subject("00000000-0000-7000-8000-000000000901");
    const auto                        character = subject("00000000-0000-7000-8000-000000000902");
    eve::climbing::ClimbingControl    control(instance, character, runtime, *fixture.world, pose);
    eve::GameplaySession              player{"player", eve::GameplayAccess::PlayerEquivalent, {character}};
    eve::GameplaySession              automation{"automation", eve::GameplayAccess::TestDriver, {character}};

    bool discovered = false;
    eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* provider) {
        if (provider == &control && provider->gameplayDomain() == "climbing") discovered = true;
    });
    CHECK(discovered);
    auto playerActions     = control.availableGameplayActions(player, instance, character);
    auto automationActions = control.availableGameplayActions(automation, instance, character);
    REQUIRE(playerActions.ok());
    REQUIRE(automationActions.ok());
    REQUIRE_EQ(playerActions.value().size(), std::size_t{1});
    CHECK_EQ(playerActions.value().front().id, automationActions.value().front().id);
    CHECK_EQ(playerActions.value().front().id, action("climbing:begin-best"));

    auto observed = control.observeGameplay(player, instance);
    REQUIRE(observed.ok());
    const auto before = std::move(observed).takeValue();
    auto       begun =
        control.submitGameplay(player, instance, command("climb-begin-1", "climbing:begin-best", character, before));
    REQUIRE(begun.ok());
    CHECK(!runtime.executionId().isZero());
    // 目录能力：该适配器服务的就是这一个实例，`instances` 因此能列出它。
    const auto catalogInstances = control.gameplayInstances();
    REQUIRE_EQ(catalogInstances.size(), std::size_t{1});
    CHECK_EQ(catalogInstances[0].format(), instance.format());
    eve::IGameplayInstanceCatalog* catalog = nullptr;
    eve::cap::forEach<eve::IGameplayInstanceCatalog>([&](auto* candidate) {
        if (candidate != nullptr && candidate->gameplayDomain() == "climbing") catalog = candidate;
    });
    REQUIRE(catalog != nullptr);
    CHECK_EQ(catalog->gameplayInstances().size(), std::size_t{1});

    auto events = control.gameplayEvents(player, instance, 0);
    REQUIRE(events.ok());
    REQUIRE_EQ(events.value().size(), std::size_t{1});
    CHECK_EQ(events.value().front().type, std::string("climbing.started"));
    CHECK_EQ(events.value().front().causationCommandId, std::string("climb-begin-1"));

    auto stale = control.submitGameplay(player, instance, command("climb-stale", "climbing:cancel", character, before));
    CHECK(!stale.ok());
    CHECK_EQ(stale.code(), eve::StatusCode::Conflict);
    CHECK(!runtime.executionId().isZero());
}

TEST_CASE("gameplay.control.climbingRejectsUnauthorizedCharacterWithoutProbeMutation") {
    ClimbingWorld                  fixture;
    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.upsertAction(mantle()).ok());
    const eve::climbing::ClimbingPose pose{{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1};
    const auto                        instance  = subject("00000000-0000-7000-8000-000000000911");
    const auto                        character = subject("00000000-0000-7000-8000-000000000912");
    const auto                        stranger  = subject("00000000-0000-7000-8000-000000000913");
    eve::climbing::ClimbingControl    control(instance, character, runtime, *fixture.world, pose);
    eve::GameplaySession              unauthorized{"other", eve::GameplayAccess::PlayerEquivalent, {stranger}};
    auto                              observed = control.observeGameplay(unauthorized, instance);
    CHECK(!observed.ok());
    CHECK(runtime.executionId().isZero());
    CHECK_EQ(runtime.pendingEventCount(), std::size_t{0});
}
}  // namespace test_gameplay_control_climbing
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_gts_terrain_collider)
#include "PrivateTestScope.h"
#include <zeroerr/unittest.h>
#include <memory>
#include "physics/Body3D.h"
#include "physics/World3D.h"
#include "procgen/GtsTerrainLod.h"
#include "procgen/physics/GtsTerrainColliderRuntime.h"
// gts_terrain_collider.cpp
namespace test_gts_terrain_collider {
using namespace eve;
namespace {
procgen::MeshBuild terrainQuad() {
    procgen::MeshBuild m;
    m.setActiveGroup("terrain");
    m.addVertex(0, 0, 0, 0, 1, 0, 0, 0);
    m.addVertex(2, 0, 0, 0, 1, 0, 1, 0);
    m.addVertex(2, 0, 2, 0, 1, 0, 1, 1);
    m.addVertex(0, 0, 2, 0, 1, 0, 0, 1);
    m.addTriangle(0, 1, 2);
    m.addTriangle(0, 2, 3);
    return m;
}
}  // namespace
TEST_CASE("procgen.physics.gtsTerrainCollider.replacesAtomicallyAndSurvivesWorldFirstTeardown") {
    auto lods = procgen::buildGtsTerrainLods(terrainQuad(), 1, 0, procgen::GtsMeshPivot::CenterXZ,
                                             procgen::defaultGtsTerrainLodLevels());
    REQUIRE(lods.ok());
    procgen_physics::GtsTerrainColliderRuntime runtime;
    auto                                       world    = std::make_unique<physics::World3D>(0.f, -9.8f, 0.f, true);
    auto                                       revision = runtime.replace(lods.value(), *world, 0, 10, 2, 20);
    REQUIRE(revision.ok());
    CHECK_EQ(revision.value(), uint64_t(1));
    CHECK_EQ(runtime.getTileCount(), 2);
    CHECK_EQ(world->getBodyCount(), 2);
    CHECK_EQ(world->getShapeCount(), 2);
    auto* first = runtime.getBody(0);
    REQUIRE(first);
    CHECK_EQ(first->getY(), 2.f);
    CHECK(first->getX() > 10.f);
    auto* preserved = runtime.getBody(1);
    REQUIRE(preserved);
    auto failed = runtime.replace(lods.value(), *world, 99);
    REQUIRE(!failed.ok());
    CHECK_EQ(runtime.getRevision(), uint64_t(1));
    CHECK_EQ(runtime.getBody(1), preserved);
    CHECK_EQ(world->getBodyCount(), 2);
    auto replaced = runtime.replace(lods.value(), *world, 1, -5, 1, -7);
    REQUIRE(replaced.ok());
    CHECK_EQ(replaced.value(), uint64_t(2));
    CHECK_EQ(world->getBodyCount(), 2);
    CHECK(runtime.getBody(1) != preserved);
    auto worldHandle = world->runtimeHandle();
    CHECK_EQ(physics::World3D::findWorld(worldHandle), world.get());
    world.reset();
    CHECK_EQ(physics::World3D::findWorld(worldHandle), nullptr);
    CHECK_EQ(runtime.getBody(0), nullptr);
    auto cleared = runtime.clear();
    REQUIRE(cleared.ok());
    CHECK_EQ(cleared.value(), 0);
    CHECK_EQ(runtime.getRevision(), uint64_t(3));
}
}  // namespace test_gts_terrain_collider
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_artifact_provider)
#include "PrivateTestScope.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include "common/Capability.h"
#include "physics/ArtifactProvider.h"
#include "physics/World3D.h"
#include "procgen/ArtifactPublish.h"
#include "procgen/GeneratedArtifact.h"
#include "procgen/Params.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// physics_artifact_provider.cpp
namespace test_physics_artifact_provider {
namespace {

eve::PersistentId id(const char* text) {
    const auto parsed = eve::PersistentId::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

struct ColliderPublication {
    std::array<float, 12>          vertices{-1.f, 0.f, -1.f, 1.f, 0.f, -1.f, 1.f, 0.f, 1.f, -1.f, 0.f, 1.f};
    std::array<std::uint32_t, 6>   indices{0, 2, 1, 0, 3, 2};
    eve::artifact::PartView        part;
    eve::artifact::PublicationView publication;

    explicit ColliderPublication(eve::PersistentId artifactId) {
        part.id            = artifactId;
        part.role          = "collider";
        part.kind          = eve::artifact::PartKind::Collider;
        part.schemaVersion = 1;
        part.buildKey      = "test.box3d.collider.v1";
        part.bounds        = {-1.f, 0.f, -1.f, 1.f, 0.f, 1.f, true};
        part.positions     = std::span<const float>(vertices);
        part.indices       = std::span<const std::uint32_t>(indices);

        publication.id            = artifactId;
        publication.schemaVersion = 1;
        publication.buildKey      = part.buildKey;
        publication.bounds        = part.bounds;
        publication.parts         = std::span<const eve::artifact::PartView>(&part, 1);
    }
};

}  // namespace

TEST_CASE("physics.artifact.stageUsesRealBox3DAndRayQuery") {
    eve::physics::PhysicsArtifactProvider provider;
    const auto                            artifactId = id("018f0b7e-6e50-7a10-8c22-2c8f8e3e0201");
    ColliderPublication                   input(artifactId);

    auto prepared = provider.prepare(input.publication);
    REQUIRE(prepared.ok());
    auto stage = std::move(prepared).takeValue();
    REQUIRE(static_cast<bool>(stage));
    CHECK_EQ(provider.size(), std::size_t(0));
    stage->commit();
    stage.reset();

    const auto* collider = provider.find(artifactId);
    REQUIRE(collider != nullptr);
    CHECK_EQ(provider.size(), std::size_t(1));
    CHECK_EQ(provider.backendName(artifactId), std::string("box3d"));
    CHECK(provider.isBox3DBacked(artifactId));
    CHECK(provider.isHandleLive(collider->handle));

    const auto hit = provider.rayCast(artifactId, 0.f, 2.f, 0.f, 0.f, -4.f, 0.f);
    CHECK(hit.hit);
    CHECK(hit.fraction >= 0.f);
    CHECK(hit.fraction <= 1.f);
    CHECK(std::fabs(hit.y) < 0.001f);
}

TEST_CASE("physics.artifact.rollbackAndValidationLeaveNoCollider") {
    eve::physics::PhysicsArtifactProvider provider;
    eve::physics::World3D                 sharedWorld(0.f, 0.f, 0.f, true);
    REQUIRE(provider.bindWorld(sharedWorld).ok());
    const auto          artifactId = id("018f0b7e-6e50-7a10-8c22-2c8f8e3e0202");
    ColliderPublication input(artifactId);

    auto prepared = provider.prepare(input.publication);
    REQUIRE(prepared.ok());
    auto stage = std::move(prepared).takeValue();
    CHECK_EQ(sharedWorld.getBodyCount(), 1);
    sharedWorld.rayCast(0.f, 2.f, 0.f, 0.f, -2.f, 0.f);
    CHECK(!sharedWorld.hasRayHit());
    stage->rollback();
    stage.reset();
    CHECK(provider.emptyState());
    CHECK_EQ(sharedWorld.getBodyCount(), 0);

    provider.setPrepareFailure(true);
    auto injected = provider.prepare(input.publication);
    CHECK(!injected.ok());
    CHECK(provider.emptyState());

    input.indices[0] = 99;
    provider.setPrepareFailure(false);
    auto invalid = provider.prepare(input.publication);
    CHECK(!invalid.ok());
    CHECK(provider.emptyState());
}

TEST_CASE("physics.artifact.restoreRebuildsBox3DAndInvalidatesOldHandle") {
    eve::physics::PhysicsArtifactProvider provider;
    const auto                            artifactId = id("018f0b7e-6e50-7a10-8c22-2c8f8e3e0203");
    ColliderPublication                   input(artifactId);

    auto prepared = provider.prepare(input.publication);
    REQUIRE(prepared.ok());
    auto stage = std::move(prepared).takeValue();
    stage->commit();
    stage.reset();
    const auto oldHandle = provider.find(artifactId)->handle;
    auto       snapshot  = provider.snapshotState();
    REQUIRE(snapshot.ok());
    auto encoded = std::move(snapshot).takeValue();

    // A failed candidate build must leave the live Box3D world untouched.
    auto  malformed       = encoded;
    auto* malformedObject = malformed.getIf<eve::Value::Object>();
    REQUIRE(malformedObject != nullptr);
    auto* encodedColliders = malformedObject->at("colliders").getIf<eve::Value::Array>();
    REQUIRE(encodedColliders != nullptr);
    auto* malformedCollider = encodedColliders->front().getIf<eve::Value::Object>();
    REQUIRE(malformedCollider != nullptr);
    auto* malformedIndices = malformedCollider->at("indices").getIf<eve::Value::Array>();
    REQUIRE(malformedIndices != nullptr);
    malformedIndices->front() = eve::Value(std::int64_t(99));
    auto rejected             = provider.restoreState(malformed);
    CHECK(!rejected.ok());
    CHECK_EQ(provider.size(), std::size_t(1));
    CHECK(provider.isHandleLive(oldHandle));
    CHECK(provider.rayCast(artifactId, 0.f, 2.f, 0.f, 0.f, -4.f, 0.f).hit);

    provider.clear();
    CHECK(!provider.isHandleLive(oldHandle));
    auto restored = provider.restoreState(encoded);
    REQUIRE(restored.ok());
    CHECK_EQ(provider.size(), std::size_t(1));
    const auto* rebuilt = provider.find(artifactId);
    REQUIRE(rebuilt != nullptr);
    CHECK(rebuilt->handle != oldHandle);
    CHECK(!provider.isHandleLive(oldHandle));
    CHECK(provider.isHandleLive(rebuilt->handle));
    CHECK(provider.isBox3DBacked(artifactId));
    CHECK(provider.rayCast(artifactId, 0.f, 2.f, 0.f, 0.f, -4.f, 0.f).hit);
}

TEST_CASE("physics.artifact.compositeColliderUsesPublicProcgenPublication") {
    eve::cap::detail::clearAllRaw();
    auto& provider = eve::physics::physicsArtifactProvider();
    provider.clear();
    eve::physics::World3D sharedWorld(0.f, 0.f, 0.f, true);
    REQUIRE(provider.bindWorld(sharedWorld).ok());
    eve::physics::registerPhysicsArtifactProvider();

    eve::procgen::Params params;
    params.setSeed(424242u);
    params.setInt("width", 8);
    params.setInt("height", 7);
    params.setFloat("radius", 1.f);
    const auto artifactId = eve::procgen::ArtifactId::parse("018f0b7e-6e50-7a10-8c22-2c8f8e3e0204");
    REQUIRE(artifactId.has_value());

    auto generated = eve::procgen::generateHexTerrainArtifact(params, *artifactId);
    REQUIRE(generated.ok());
    eve::procgen::ArtifactStore          store;
    eve::procgen::ArtifactPublisher      publisher(store);
    eve::procgen::ArtifactPublishOptions options;
    options.physics = true;
    auto published  = publisher.publish(std::move(generated).takeValue(), options);
    REQUIRE(published.ok());

    const auto persistentId = eve::PersistentId::fromUuid(*artifactId);
    CHECK_EQ(provider.size(), std::size_t(1));
    CHECK(provider.find(persistentId) != nullptr);
    CHECK(provider.isBox3DBacked(persistentId));
    CHECK(provider.isSharedWorldBacked(persistentId));
    CHECK_EQ(provider.boundWorld(), sharedWorld.runtimeHandle());
    CHECK_EQ(sharedWorld.getBodyCount(), 1);
    const auto* collider = provider.find(persistentId);
    REQUIRE(collider != nullptr);
    REQUIRE_GE(collider->indices.size(), std::size_t(3));
    const auto vertex = [collider](std::size_t corner, std::size_t axis) {
        return collider->vertices[std::size_t(collider->indices[corner]) * 3u + axis];
    };
    const float rayX = (vertex(0, 0) + vertex(1, 0) + vertex(2, 0)) / 3.f;
    const float rayZ = (vertex(0, 2) + vertex(1, 2) + vertex(2, 2)) / 3.f;
    const auto  hit  = provider.rayCast(persistentId, rayX, collider->bounds.maxY + 10.f, rayZ, 0.f,
                                        -((collider->bounds.maxY - collider->bounds.minY) + 20.f), 0.f);
    CHECK(hit.hit);

    sharedWorld.rayCast(rayX, collider->bounds.maxY + 10.f, rayZ, rayX, collider->bounds.minY - 10.f, rayZ);
    CHECK(sharedWorld.hasRayHit());
    provider.clear();
    CHECK_EQ(sharedWorld.getBodyCount(), 0);
    REQUIRE(provider.unbindWorld(sharedWorld.runtimeHandle()).ok());
}

TEST_CASE("physics.artifact.destroyedSharedWorldMakesPublicationStale") {
    eve::physics::PhysicsArtifactProvider provider;
    const auto                            artifactId = id("018f0b7e-6e50-7a10-8c22-2c8f8e3e0205");
    ColliderPublication                   input(artifactId);
    auto                                  world = std::make_unique<eve::physics::World3D>(0.f, 0.f, 0.f, true);
    REQUIRE(provider.bindWorld(*world).ok());
    auto prepared = provider.prepare(input.publication);
    REQUIRE(prepared.ok());
    auto stage = std::move(prepared).takeValue();
    stage->commit();
    const auto colliderHandle = provider.find(artifactId)->handle;

    world.reset();
    CHECK(provider.boundWorld().isInvalid());
    CHECK(!provider.isBox3DBacked(artifactId));
    CHECK(!provider.isSharedWorldBacked(artifactId));
    CHECK(!provider.isHandleLive(colliderHandle));
    CHECK(!provider.rayCast(artifactId, 0.f, 2.f, 0.f, 0.f, -4.f, 0.f).hit);
    provider.clear();
}

TEST_CASE("physics.artifact.preparedStageOutlivesProviderWithoutOwnerCallback") {
    eve::physics::World3D                               sharedWorld(0.f, 0.f, 0.f, true);
    std::unique_ptr<eve::artifact::PreparedPublication> stage;
    {
        auto provider = std::make_unique<eve::physics::PhysicsArtifactProvider>();
        REQUIRE(provider->bindWorld(sharedWorld).ok());
        ColliderPublication input(id("018f0b7e-6e50-7a10-8c22-2c8f8e3e0206"));
        auto                prepared = provider->prepare(input.publication);
        REQUIRE(prepared.ok());
        stage = std::move(prepared).takeValue();
        CHECK_EQ(sharedWorld.getBodyCount(), 1);
    }
    stage.reset();
    CHECK_EQ(sharedWorld.getBodyCount(), 0);
}
}  // namespace test_physics_artifact_provider
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_consumer_integration)
#include "PrivateTestScope.h"
#include <memory>
#include "physics/ArtifactProvider.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "pixelworld/PixelWorld.h"
#include "pixelworld/physics/PixelWorldPhysics.h"
#include "pixelworld/physics/PixelWorldPhysicsModule.h"
#include "scene/physics/ScenePhysics.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// physics_consumer_integration.cpp
namespace test_physics_consumer_integration {
TEST_CASE("physics.consumer.sceneRuntimeBindsGeneratedCollidersToGameplayWorld") {
    auto& provider = eve::physics::physicsArtifactProvider();
    provider.clear();
    eve::physics::World3D            world(0.f, -9.8f, 0.f, true);
    eve::scene_physics::ScenePhysics runtime;

    runtime.bindGeneratedColliders(world).expect("bind generated collider world");
    CHECK_EQ(runtime.boundWorld(), world.runtimeHandle());
    CHECK_EQ(provider.boundWorld(), world.runtimeHandle());
    runtime.unbindGeneratedColliders().expect("unbind generated collider world");
    CHECK(runtime.boundWorld().isInvalid());
}

TEST_CASE("physics.consumer.sceneRuntimeObservesDestroyedGameplayWorldAsStale") {
    auto& provider = eve::physics::physicsArtifactProvider();
    provider.clear();
    eve::scene_physics::ScenePhysics runtime;
    {
        auto world = std::make_unique<eve::physics::World3D>(0.f, 0.f, 0.f, true);
        runtime.bindGeneratedColliders(*world).expect("bind temporary world");
        CHECK(!runtime.boundWorld().isInvalid());
    }
    CHECK(runtime.boundWorld().isInvalid());
    provider.clear();
}

TEST_CASE("pixelworld_physics.moduleCreatesProductionTerrainProjection") {
    eve::pixelworld_physics::PixelWorldPhysics                           module;
    std::unique_ptr<eve::pixelworld_physics::PixelTerrainCollisionCache> cache(module.newTerrainCache());
    REQUIRE(cache != nullptr);
    eve::pixelworld::PixelWorld pixels(7001);
    for (int x = 0; x < 16; ++x) pixels.setMaterial(x, 12, "stone");
    eve::physics::World world(0.f, 100.f, true, 64.f);

    const auto receipt = cache->sync(world, pixels).expect("sync script-owned terrain cache");
    CHECK_GT(receipt.fixturesCreated, std::uint32_t(0));
    CHECK_EQ(cache->sourceRevision(), pixels.revision());
    CHECK_GT(cache->bodyCount(), std::size_t(0));
    cache->clearPhysics(world).expect("clear script-owned terrain cache");
}
}  // namespace test_physics_consumer_integration
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_contact_query)
#include "PrivateTestScope.h"
#include <memory>
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "zeroerr/unittest.h"
// physics_contact_query.cpp
namespace test_physics_contact_query {
using namespace eve::physics;

TEST_CASE("physics.contacts.ownedCoverageAndLifetime") {
    World3D                  world(0.f, -10.f, 0.f, false);
    std::unique_ptr<Body3D>  ground(world.newBody("static", 0.f, -0.5f, 0.f));
    std::unique_ptr<Shape3D> floor(ground->newBoxShape(10.f, 1.f, 10.f));
    std::unique_ptr<Body3D>  ball(world.newBody("dynamic", 0.f, 2.f, 0.f));
    std::unique_ptr<Shape3D> sphere(ball->newSphereShape(0.5f, 1.f, 0.4f, 0.f));
    for (int i = 0; i < 120; ++i) world.update(1.f / 60.f);

    auto full = world.queryBodyContactsOwned(ball->runtimeHandle(), 4096);
    REQUIRE(full.ok());
    REQUIRE_GT(full.value().points.size(), 0u);
    CHECK(full.value().coverage == ContactQueryCoverage3D::Complete);
    auto exact = world.queryBodyContactsOwned(ball->runtimeHandle(), static_cast<int>(full.value().points.size()));
    REQUIRE(exact.ok());
    CHECK(exact.value().coverage == ContactQueryCoverage3D::Complete);
    CHECK_LT(exact.value().points.front().normalY, -0.9f);

    REQUIRE_GT(world.queryBodyContacts(ground->getId(), 4096), 0);
    const int cachedShape = world.getContactPointShapeId(0);
    auto      independent = world.queryBodyContactsOwned(ball->runtimeHandle(), 1);
    REQUIRE(independent.ok());
    CHECK_EQ(world.getContactPointShapeId(0), cachedShape);
    const int ownShape = sphere->getId();
    world.destroy();
    CHECK_EQ(exact.value().points.front().shapeId, ownShape);
    auto stale = world.queryBodyContactsOwned(full.value().body, 1);
    CHECK(!stale.ok());
}

TEST_CASE("physics.contacts.truncationIsExplicit") {
    World3D                  world(0.f, -10.f, 0.f, false);
    std::unique_ptr<Body3D>  ground(world.newBody("static", 0.f, -0.5f, 0.f));
    std::unique_ptr<Shape3D> floor(ground->newBoxShape(10.f, 1.f, 10.f));
    std::unique_ptr<Body3D>  box(world.newBody("dynamic", 0.f, 1.f, 0.f));
    std::unique_ptr<Shape3D> shape(box->newBoxShape(1.f, 1.f, 1.f));
    for (int i = 0; i < 120; ++i) world.update(1.f / 60.f);
    auto full = world.queryBodyContactsOwned(box->runtimeHandle(), 4096);
    REQUIRE(full.ok());
    REQUIRE_GT(full.value().points.size(), 1u);
    auto limited = world.queryBodyContactsOwned(box->runtimeHandle(), 1);
    REQUIRE(limited.ok());
    CHECK_EQ(limited.value().points.size(), 1u);
    CHECK(limited.value().coverage == ContactQueryCoverage3D::Truncated);
    CHECK_EQ(limited.value().points.front().featureId, full.value().points.front().featureId);
}

TEST_CASE("physics.contacts.rejectsForeignStaleAndInvalidInputs") {
    World3D                 first(0.f, 0.f, 0.f, false);
    World3D                 second(0.f, 0.f, 0.f, false);
    std::unique_ptr<Body3D> body(first.newBody("dynamic", 0.f, 0.f, 0.f));
    const auto              handle  = body->runtimeHandle();
    auto                    foreign = second.queryBodyContactsOwned(handle, 1);
    CHECK(!foreign.ok());
    auto empty = first.queryBodyContactsOwned(handle, 1);
    REQUIRE(empty.ok());
    CHECK(empty.value().points.empty());
    CHECK(empty.value().coverage == ContactQueryCoverage3D::Complete);
    auto invalid = first.queryBodyContactsOwned(handle, 0);
    CHECK(!invalid.ok());
    auto excessive = first.queryBodyContactsOwned(handle, 4097);
    CHECK(!excessive.ok());
    body->destroy();
    auto stale = first.queryBodyContactsOwned(handle, 1);
    CHECK(!stale.ok());
}
}  // namespace test_physics_contact_query
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_core_boundary)
#include "PrivateTestScope.h"
#include <Box2D/Box2D.h>
#include <box3d/box3d.h>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include "common/Capability.h"
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/PhysicsLink.h"
#include "physics/Shape3D.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "physics/backend/SimulationBackend.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// physics_core_boundary.cpp
namespace test_physics_core_boundary {
using namespace eve::physics;

namespace {

class CapabilityReset {
public:
    CapabilityReset() { eve::cap::detail::clearAllRaw(); }
    ~CapabilityReset() { eve::cap::detail::clearAllRaw(); }
};

class MockAcceleratorProvider final : public IAcceleratorBackendProvider {
public:
    [[nodiscard]] bool supports(SimulationBackendDomain domain) const noexcept override {
        return domain == SimulationBackendDomain::World2D;
    }

    [[nodiscard]] eve::Result<std::unique_ptr<ISimulationBackend>> create(SimulationBackendDomain domain,
                                                                          void* /*state*/) override {
        if (!supports(domain)) {
            return eve::Result<std::unique_ptr<ISimulationBackend>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Unsupported, "Mock accelerator does not implement this domain"));
        }
        return eve::Result<std::unique_ptr<ISimulationBackend>>::success(detail::makeMockAcceleratorBackend());
    }
};

class FailingProvider final : public IAcceleratorBackendProvider {
public:
    explicit FailingProvider(bool throws) : throws_(throws) {}

    [[nodiscard]] bool supports(SimulationBackendDomain) const noexcept override { return true; }

    [[nodiscard]] eve::Result<std::unique_ptr<ISimulationBackend>> create(SimulationBackendDomain, void*) override {
        if (throws_) throw std::runtime_error("accelerator setup exploded");
        return eve::Result<std::unique_ptr<ISimulationBackend>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "accelerator rejected shared state", "provider.state"));
    }

private:
    bool throws_ = false;
};

class FailSecondStepBackend final : public ISimulationBackend {
public:
    explicit FailSecondStepBackend(b3WorldId* world)
        : delegate_(detail::makeCallbackSimulationBackend(world, &stepBox3D, SimulationBackendKind::MockAccelerator,
                                                          SimulationDeterminism::ToleranceBounded)) {}

    [[nodiscard]] eve::Result<void> step(const eve::SimulationStep& step, const SimulationSettings& settings) override {
        if (delegate_->observation().stepCount != 0) {
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "injected second-step failure", "physics.test.step"));
        }
        return delegate_->step(step, settings);
    }
    [[nodiscard]] SimulationObservation observation() const noexcept override { return delegate_->observation(); }
    [[nodiscard]] SimulationBackendKind kind() const noexcept override { return delegate_->kind(); }
    [[nodiscard]] SimulationDeterminism determinism() const noexcept override { return delegate_->determinism(); }
    [[nodiscard]] eve::Result<void>     restoreObservation(const SimulationObservation& value) override {
        return delegate_->restoreObservation(value);
    }

private:
    static void stepBox3D(void* context, const eve::SimulationStep& step, const SimulationSettings& settings) noexcept {
        b3World_Step(*static_cast<b3WorldId*>(context), static_cast<float>(step.delta.seconds()),
                     settings.subStepCount);
    }

    std::unique_ptr<ISimulationBackend> delegate_;
};

class FailSecondStepProvider final : public IAcceleratorBackendProvider {
public:
    [[nodiscard]] bool supports(SimulationBackendDomain domain) const noexcept override {
        return domain == SimulationBackendDomain::World3D;
    }
    [[nodiscard]] eve::Result<std::unique_ptr<ISimulationBackend>> create(SimulationBackendDomain,
                                                                          void* state) override {
        return eve::Result<std::unique_ptr<ISimulationBackend>>::success(
            std::make_unique<FailSecondStepBackend>(static_cast<b3WorldId*>(state)));
    }
};

[[maybe_unused]] void checkObservableContract(ISimulationBackend& backend) {
    const eve::SimulationStep step{eve::SimulationTick{1}, eve::Duration::fromNanoseconds(16666667)};
    auto                      applied   = backend.step(step, SimulationSettings{});
    const bool                appliedOk = applied.ok();
    CHECK(appliedOk);
    const SimulationObservation observation = backend.observation();
    CHECK_EQ(observation.stepCount, std::uint64_t(1));
    CHECK(observation.lastTick == step.tick);
    CHECK_EQ(observation.simulatedDuration, step.delta);

    auto       duplicate   = backend.step(step, SimulationSettings{});
    const bool duplicateOk = duplicate.ok();
    CHECK(!duplicateOk);
    CHECK_EQ(duplicate.code(), eve::StatusCode::Rejected);
}

}  // namespace

TEST_CASE("physics.core.backendContract.isHeadlessAndObservable") {
    auto*                  physics = Physics::create();
    std::unique_ptr<World> world(physics->newWorld(0.f, 900.f));
    REQUIRE(world.get() != nullptr);

    Body* body = world->newBody("dynamic", 0.f, 0.f);
    body->newCircleFixture(10.f);
    const float y0 = body->getY();

    auto backend = detail::makeBox2DSimulationBackend(world->raw());
    REQUIRE(backend.get() != nullptr);
    CHECK_EQ(backend->kind(), SimulationBackendKind::Cpu);
    CHECK_EQ(backend->determinism(), SimulationDeterminism::ToleranceBounded);
    CHECK_EQ(backend->observation().stepCount, std::uint64_t(0));

    auto       stepped = backend->step(SimulationStep{eve::SimulationTick{1}, eve::Duration::fromNanoseconds(16666667)},
                                       SimulationSettings{});
    const bool steppedOk = stepped.ok();
    REQUIRE(steppedOk);
    const SimulationObservation observation = backend->observation();
    CHECK_EQ(observation.stepCount, std::uint64_t(1));
    CHECK(observation.lastTick == eve::SimulationTick{1});
    CHECK(std::fabs(observation.simulatedSeconds - 0.016666667) < 1e-9);
    CHECK(std::fabs(observation.lastDeltaSeconds - 0.016666667f) < 1e-6f);
    CHECK_GT(body->getY(), y0);
}

TEST_CASE("physics.core.backendContract.cpuAndMockShareObservableRules") {
    CapabilityReset reset;
    b2World         rawWorld(b2Vec2_zero);

    auto cpu = detail::makeBox2DSimulationBackend(&rawWorld);
    REQUIRE(cpu.get() != nullptr);
    checkObservableContract(*cpu);

    auto mock = detail::makeMockAcceleratorBackend();
    REQUIRE(mock.get() != nullptr);
    CHECK_EQ(mock->kind(), SimulationBackendKind::MockAccelerator);
    checkObservableContract(*mock);
}

TEST_CASE("physics.core.backendFallbackIsStructuredAndObservable") {
    CapabilityReset reset;
    b2World         rawWorld(b2Vec2_zero);

    auto       absent   = detail::selectSimulationBackend(SimulationBackendDomain::World2D,
                                                          detail::makeBox2DSimulationBackend(&rawWorld), &rawWorld, true);
    const bool absentOk = absent.ok();
    REQUIRE(absentOk);
    CHECK_EQ(absent.status().code(), eve::StatusCode::Applied);
    REQUIRE_EQ(absent.diagnostics().size(), std::size_t(1));
    CHECK_EQ(absent.diagnostics().front().code(), eve::DiagnosticCode::Unsupported);
    auto absentSelection = std::move(absent).takeValue();
    CHECK(absentSelection.usedFallback);
    CHECK_EQ(absentSelection.actualKind, SimulationBackendKind::Cpu);

    MockAcceleratorProvider provider;
    eve::cap::provide<IAcceleratorBackendProvider>(&provider);
    auto       present   = detail::selectSimulationBackend(SimulationBackendDomain::World2D,
                                                           detail::makeBox2DSimulationBackend(&rawWorld), &rawWorld, true);
    const bool presentOk = present.ok();
    REQUIRE(presentOk);
    CHECK(present.diagnostics().empty());
    auto presentSelection = std::move(present).takeValue();
    CHECK(!presentSelection.usedFallback);
    CHECK_EQ(presentSelection.actualKind, SimulationBackendKind::MockAccelerator);
}

TEST_CASE("physics.core.backendFallbackPreservesProviderFailureDiagnostics") {
    CapabilityReset reset;
    b2World         rawWorld(b2Vec2_zero);

    FailingProvider provider(false);
    eve::cap::provide<IAcceleratorBackendProvider>(&provider);
    auto selected = detail::selectSimulationBackend(SimulationBackendDomain::World2D,
                                                    detail::makeBox2DSimulationBackend(&rawWorld), &rawWorld, true);
    REQUIRE(selected.ok());
    REQUIRE_EQ(selected.diagnostics().size(), std::size_t(2));
    CHECK_EQ(selected.diagnostics()[1].code(), eve::DiagnosticCode::Conflict);
    CHECK_EQ(selected.diagnostics()[1].message(), std::string("accelerator rejected shared state"));
    CHECK_EQ(selected.diagnostics()[1].path(), std::string("provider.state"));
}

TEST_CASE("physics.core.backendFallbackPreservesProviderExceptionMessage") {
    CapabilityReset reset;
    b2World         rawWorld(b2Vec2_zero);

    FailingProvider provider(true);
    eve::cap::provide<IAcceleratorBackendProvider>(&provider);
    auto selected = detail::selectSimulationBackend(SimulationBackendDomain::World2D,
                                                    detail::makeBox2DSimulationBackend(&rawWorld), &rawWorld, true);
    REQUIRE(selected.ok());
    REQUIRE_EQ(selected.diagnostics().size(), std::size_t(2));
    CHECK_EQ(selected.diagnostics()[1].code(), eve::DiagnosticCode::CallbackFailure);
    CHECK_EQ(selected.diagnostics()[1].message(), std::string("accelerator setup exploded"));
}

TEST_CASE("physics.core.world3dFailedStepPreservesPreviousContactEvents") {
    CapabilityReset        reset;
    FailSecondStepProvider provider;
    eve::cap::provide<IAcceleratorBackendProvider>(&provider);

    World3D                  world(0.f, -9.8f, 0.f, false);
    std::unique_ptr<Body3D>  bodyA(world.newBody("dynamic", 0.f, 0.f, 0.f));
    std::unique_ptr<Body3D>  bodyB(world.newBody("dynamic", 0.f, 0.f, 0.f));
    std::unique_ptr<Shape3D> shapeA(bodyA->newSphereShape(1.f));
    std::unique_ptr<Shape3D> shapeB(bodyB->newSphereShape(1.f));

    auto first = world.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(16666667)});
    REQUIRE(first.ok());
    REQUIRE_GT(world.getBeginContactCount(), 0);
    const int previousCount = world.getBeginContactCount();

    auto failed = world.step({eve::SimulationTick{2}, eve::Duration::fromNanoseconds(16666667)});
    CHECK(!failed.ok());
    CHECK_EQ(world.getBeginContactCount(), previousCount);
    CHECK_EQ(world.simulationTick(), eve::SimulationTick{1});
}

TEST_CASE("physics.core.worldUpdateNeedsNoGraphics") {
    auto*                  physics = Physics::create();
    std::unique_ptr<World> world(physics->newWorld(0.f, 0.f));
    REQUIRE(world.get() != nullptr);

    Body* body = world->newBody("dynamic", 20.f, 30.f);
    body->newCircleFixture(5.f);
    body->setLinearVelocity(60.f, 0.f);
    world->update(1.f / 60.f);

    CHECK(std::fabs(body->getX() - 21.f) < 0.05f);
    CHECK(std::fabs(body->getY() - 30.f) < 0.05f);
}

TEST_CASE("physics.core.worldAcceptsInjectedTickAndPhysicsLinkResolvesStale") {
    auto*                  physics = Physics::create();
    std::unique_ptr<World> world(physics->newWorld(0.f, 0.f));
    REQUIRE(world.get() != nullptr);
    Body* body = world->newBody("dynamic", 0.f, 0.f);
    body->newCircleFixture(5.f);

    const eve::SimulationStep step{eve::SimulationTick{1}, eve::Duration::fromNanoseconds(16666667)};
    auto                      stepped   = world->step(step);
    const bool                steppedOk = stepped.ok();
    REQUIRE(steppedOk);
    CHECK(world->simulationTick() == step.tick);
    CHECK_EQ(world->simulationObservation().lastTick, step.tick);
    CHECK(world->backendSelectionStatus().code() == eve::StatusCode::Applied);

    auto       linkResult = PhysicsLink::fromBody(*body);
    const bool linkOk     = linkResult.ok();
    REQUIRE(linkOk);
    const PhysicsLink link       = std::move(linkResult).takeValue();
    auto              resolved   = link.resolve(*world);
    const bool        resolvedOk = resolved.ok();
    REQUIRE(resolvedOk);
    CHECK(std::move(resolved).takeValue() == body);

    body->destroy();
    auto       stale   = link.resolve(*world);
    const bool staleOk = stale.ok();
    CHECK(!staleOk);
    CHECK_EQ(stale.code(), eve::StatusCode::Rejected);
}
}  // namespace test_physics_core_boundary
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_destruction)
#include "PrivateTestScope.h"
#include <memory>
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/PhysicsLink.h"
#include "physics/World3D.h"
#include "physics/destruction/Destruction.h"
#include "physics/destruction/DestructionField.h"
#include "physics/destruction/GeometryCollectionAsset.h"
#include "physics/destruction/GeometryCollectionInstance.h"
#include "schema/SchemaRegistry.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// physics_destruction.cpp
namespace test_physics_destruction {
using eve::physics::BoneRuntimeState;
using eve::physics::DestructionField;
using eve::physics::DestructionFieldFalloff;
using eve::physics::DestructionFieldKind;
using eve::physics::GeometryCollectionAsset;
using eve::physics::GeometryCollectionInstance;
using eve::physics::Physics;
using eve::physics::PhysicsLink;
using eve::physics::World3D;

namespace {

[[maybe_unused]] DestructionField strainAt(float x, float y, float z, float radius, float magnitude) {
    DestructionField field;
    field.kind      = DestructionFieldKind::Strain;
    field.falloff   = DestructionFieldFalloff::Linear;
    field.centerX   = x;
    field.centerY   = y;
    field.centerZ   = z;
    field.radius    = radius;
    field.magnitude = magnitude;
    return field;
}

[[maybe_unused]] eve::SimulationStep simStep(std::uint64_t tick, double seconds = 1.0 / 60.0) {
    return {eve::SimulationTick{tick}, eve::Duration::fromSeconds(seconds).expect("destruction dt")};
}

}  // namespace


TEST_CASE("physics_destruction.strainFieldBreaksEdgeAndDetachesBones") {
    auto*                    mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -9.8f, 0.f, true));
    auto                     asset = GeometryCollectionAsset::makeWeldedBoxesFixture(1.f);
    REQUIRE(asset.ok());
    auto instance = GeometryCollectionInstance::create(*world, asset.value(), 0.f, 0.f, 0.f);
    REQUIRE(instance.ok());
    REQUIRE_EQ(instance.value()->boneCount(), 2);
    REQUIRE_EQ(instance.value()->edgeCount(), 1);
    REQUIRE(!instance.value()->isEdgeBroken(0));

    auto applied = instance.value()->applyField(strainAt(0.f, 1.f, 0.f, 3.f, 1.5f));
    REQUIRE(applied.ok());
    REQUIRE(applied.value().edgesAffected >= 1);
    REQUIRE(instance.value()->edgeStrain(0) >= 1.f);

    auto stepped = instance.value()->step(simStep(1));
    REQUIRE(stepped.ok());
    REQUIRE(instance.value()->isEdgeBroken(0));
    REQUIRE_EQ(instance.value()->detachEventCount(), 1);
    REQUIRE(instance.value()->boneState(0) == BoneRuntimeState::Detached);
    REQUIRE(instance.value()->boneState(1) == BoneRuntimeState::Detached);

    auto link0 = instance.value()->boneLink(0);
    REQUIRE(link0.ok());
    auto body0 = link0.value().resolve(*world);
    REQUIRE(body0.ok());
    REQUIRE_EQ(body0.value()->getType(), std::string("dynamic"));
}

TEST_CASE("physics_destruction.instanceDestroyReleasesBodiesBeforeWorld") {
    auto*                    mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -9.8f, 0.f, true));
    auto                     asset = GeometryCollectionAsset::makeWeldedBoxesFixture(1.f);
    REQUIRE(asset.ok());
    auto created = GeometryCollectionInstance::create(*world, asset.value(), 0.f, 2.f, 0.f);
    REQUIRE(created.ok());
    auto instance = std::move(created.value());
    auto link     = instance->boneLink(0);
    REQUIRE(link.ok());
    instance.reset();
    auto resolved = link.value().resolve(*world);
    REQUIRE(!resolved.ok());
    REQUIRE_EQ(resolved.status().code(), eve::StatusCode::Rejected);
}

TEST_CASE("physics_destruction.worldDestroyFirstLeavesStaleLinks") {
    auto*                    mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -9.8f, 0.f, true));
    auto                     asset = GeometryCollectionAsset::makeWeldedBoxesFixture(1.f);
    REQUIRE(asset.ok());
    auto created = GeometryCollectionInstance::create(*world, asset.value(), 0.f, 2.f, 0.f);
    REQUIRE(created.ok());
    auto instance = std::move(created.value());
    auto link     = instance->boneLink(0);
    REQUIRE(link.ok());

    world->destroy();
    world.reset();

    REQUIRE(!instance->hasLiveWorld());
    auto stepped = instance->step(simStep(1));
    REQUIRE(!stepped.ok());
    REQUIRE_EQ(stepped.status().code(), eve::StatusCode::Rejected);

    // Resolving against a fresh world must still report stale owner identity.
    std::unique_ptr<World3D> other(mod->newWorld3D(0.f, -9.8f, 0.f, true));
    auto                     resolved = link.value().resolve(*other);
    REQUIRE(!resolved.ok());
    instance.reset();
}

TEST_CASE("physics_destruction.anchorAndSleepFields") {
    auto*                    mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    auto                     asset = GeometryCollectionAsset::makeWeldedBoxesFixture(0.5f);
    REQUIRE(asset.ok());
    auto instance = GeometryCollectionInstance::create(*world, asset.value(), 0.f, 0.f, 0.f);
    REQUIRE(instance.ok());

    DestructionField anchor;
    anchor.kind    = DestructionFieldKind::Anchor;
    anchor.falloff = DestructionFieldFalloff::None;
    anchor.centerX = -0.55f;
    anchor.centerY = 1.f;
    anchor.centerZ = 0.f;
    anchor.radius  = 0.25f;
    auto anchored  = instance.value()->applyField(anchor);
    REQUIRE(anchored.ok());
    REQUIRE(anchored.value().bonesAffected >= 1);

    REQUIRE(instance.value()->applyField(strainAt(0.f, 1.f, 0.f, 3.f, 2.f)).ok());
    REQUIRE(instance.value()->step(simStep(1)).ok());
    REQUIRE(instance.value()->isEdgeBroken(0));
    // Bone 0 was anchored so it stays Attached; bone 1 detaches.
    REQUIRE(instance.value()->boneState(0) == BoneRuntimeState::Attached);
    REQUIRE(instance.value()->boneState(1) == BoneRuntimeState::Detached);

    auto body1 = instance.value()->boneLink(1).value().resolve(*world);
    REQUIRE(body1.ok());
    body1.value()->setLinearVelocity(0.f, 0.f, 0.f);

    DestructionField sleep;
    sleep.kind    = DestructionFieldKind::Sleep;
    sleep.falloff = DestructionFieldFalloff::None;
    sleep.centerX = 0.55f;
    sleep.centerY = 1.f;
    sleep.centerZ = 0.f;
    sleep.radius  = 0.5f;
    auto slept    = instance.value()->applyField(sleep);
    REQUIRE(slept.ok());
    REQUIRE(slept.value().bonesAffected >= 1);
    REQUIRE(instance.value()->boneState(1) == BoneRuntimeState::Sleeping);
    REQUIRE_EQ(body1.value()->getType(), std::string("static"));
}
}  // namespace test_physics_destruction
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_destruction_p3)
#include "PrivateTestScope.h"
#include <memory>
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/World3D.h"
#include "physics/destruction/Destruction.h"
#include "physics/destruction/DestructionField.h"
#include "physics/destruction/GeometryCollectionAsset.h"
#include "physics/destruction/GeometryCollectionInstance.h"
#include "physics/destruction/GeometryCollectionSnapshot.h"
#include "schema/SchemaRegistry.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// physics_destruction_p3.cpp
namespace test_physics_destruction_p3 {
using eve::physics::BoneRuntimeState;
using eve::physics::Destruction;
using eve::physics::DestructionField;
using eve::physics::DestructionFieldFalloff;
using eve::physics::DestructionFieldKind;
using eve::physics::DestructionStepBudget;
using eve::physics::GeometryCollectionAsset;
using eve::physics::GeometryCollectionBone;
using eve::physics::GeometryCollectionEdge;
using eve::physics::GeometryCollectionInstance;
using eve::physics::GeometryCollectionInstanceSnapshot;
using eve::physics::Physics;
using eve::physics::World3D;

namespace {

[[maybe_unused]] DestructionField strainAt(float x, float y, float z, float radius, float magnitude) {
    DestructionField field;
    field.kind      = DestructionFieldKind::Strain;
    field.falloff   = DestructionFieldFalloff::Linear;
    field.centerX   = x;
    field.centerY   = y;
    field.centerZ   = z;
    field.radius    = radius;
    field.magnitude = magnitude;
    return field;
}

[[maybe_unused]] eve::SimulationStep simStep(std::uint64_t tick, double seconds = 1.0 / 60.0) {
    return {eve::SimulationTick{tick}, eve::Duration::fromSeconds(seconds).expect("destruction dt")};
}

[[maybe_unused]] eve::Result<GeometryCollectionAsset> makeThreeBoneChain() {
    GeometryCollectionAsset asset;
    GeometryCollectionBone  a;
    a.halfExtentX            = 0.4f;
    a.halfExtentY            = 0.4f;
    a.halfExtentZ            = 0.4f;
    a.localX                 = -1.0f;
    a.localY                 = 1.0f;
    a.clusterId              = 0;
    GeometryCollectionBone b = a;
    b.localX                 = 0.0f;
    b.clusterId              = 0;
    GeometryCollectionBone c = a;
    c.localX                 = 1.0f;
    c.clusterId              = 1;
    asset.bones              = {a, b, c};
    GeometryCollectionEdge e0;
    e0.boneA           = 0;
    e0.boneB           = 1;
    e0.strainThreshold = 1.f;
    GeometryCollectionEdge e1;
    e1.boneA           = 1;
    e1.boneB           = 2;
    e1.strainThreshold = 1.f;
    asset.edges        = {e0, e1};
    auto valid         = asset.validate();
    if (!valid) return eve::Result<GeometryCollectionAsset>::failure(valid.status());
    return eve::Result<GeometryCollectionAsset>::success(std::move(asset));
}

}  // namespace


TEST_CASE("physics_destruction_p3.edgeBreakBudgetDefersAndDrains") {
    auto*                    mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -9.8f, 0.f, true));
    auto                     asset = makeThreeBoneChain();
    REQUIRE(asset.ok());
    auto instance = GeometryCollectionInstance::create(*world, asset.value(), 0.f, 0.f, 0.f);
    REQUIRE(instance.ok());

    DestructionStepBudget budget;
    budget.maxEdgeBreaksPerStep = 1;
    instance.value()->setStepBudget(budget);

    REQUIRE(instance.value()->applyField(strainAt(0.f, 1.f, 0.f, 5.f, 2.f)).ok());
    auto step1 = instance.value()->step(simStep(1));
    REQUIRE(step1.ok());
    REQUIRE_EQ(step1.value().edgesBroken, 1);
    REQUIRE_EQ(step1.value().edgesDeferred, 1);
    REQUIRE_EQ(instance.value()->pendingEdgeBreakCount(), 1);
    REQUIRE(instance.value()->isEdgeBroken(0) != instance.value()->isEdgeBroken(1));

    auto step2 = instance.value()->step(simStep(2));
    REQUIRE(step2.ok());
    REQUIRE_EQ(step2.value().edgesBroken, 1);
    REQUIRE_EQ(step2.value().edgesDeferred, 0);
    REQUIRE_EQ(instance.value()->pendingEdgeBreakCount(), 0);
    REQUIRE(instance.value()->isEdgeBroken(0));
    REQUIRE(instance.value()->isEdgeBroken(1));
}

TEST_CASE("physics_destruction_p3.clusterBreakEventsAcrossMembership") {
    auto*                    mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    auto                     asset = makeThreeBoneChain();
    REQUIRE(asset.ok());
    auto instance = GeometryCollectionInstance::create(*world, asset.value(), 0.f, 0.f, 0.f);
    REQUIRE(instance.ok());
    REQUIRE_EQ(instance.value()->boneClusterId(0), 0);
    REQUIRE_EQ(instance.value()->boneClusterId(2), 1);

    REQUIRE(instance.value()->applyField(strainAt(0.f, 1.f, 0.f, 5.f, 2.f)).ok());
    auto stepped = instance.value()->step(simStep(1));
    REQUIRE(stepped.ok());
    REQUIRE(stepped.value().clusterBreaks >= 1);
    REQUIRE(instance.value()->clusterBreakEventCount() >= 1);
    auto       event = instance.value()->clusterBreakEventAt(0);
    const bool crossCluster =
        (event.clusterA == 0 && event.clusterB == 1) || (event.clusterA == 1 && event.clusterB == 0);
    REQUIRE(crossCluster);
}

TEST_CASE("physics_destruction_p3.sleepBudgetDefersRemainder") {
    auto*                    mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    auto                     asset = makeThreeBoneChain();
    REQUIRE(asset.ok());
    auto instance = GeometryCollectionInstance::create(*world, asset.value(), 0.f, 0.f, 0.f);
    REQUIRE(instance.ok());

    REQUIRE(instance.value()->applyField(strainAt(0.f, 1.f, 0.f, 5.f, 2.f)).ok());
    REQUIRE(instance.value()->step(simStep(1)).ok());
    for (int i = 0; i < 3; ++i) {
        if (instance.value()->boneState(i) != BoneRuntimeState::Detached) continue;
        auto body = instance.value()->boneLink(i).value().resolve(*world);
        REQUIRE(body.ok());
        body.value()->setLinearVelocity(0.f, 0.f, 0.f);
    }

    DestructionStepBudget budget;
    budget.maxSleepsPerStep = 1;
    instance.value()->setStepBudget(budget);

    DestructionField sleep;
    sleep.kind    = DestructionFieldKind::Sleep;
    sleep.falloff = DestructionFieldFalloff::None;
    sleep.centerX = 0.f;
    sleep.centerY = 1.f;
    sleep.centerZ = 0.f;
    sleep.radius  = 5.f;
    auto slept    = instance.value()->applyField(sleep);
    REQUIRE(slept.ok());
    REQUIRE_EQ(slept.value().bonesAffected, 1);
    REQUIRE(slept.value().sleepsDeferred >= 1);
}

TEST_CASE("physics_destruction_p3.instanceSnapshotRoundTripRestore") {
    auto*                    mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    auto                     asset = GeometryCollectionAsset::makeWeldedBoxesFixture(1.f);
    REQUIRE(asset.ok());
    asset.value().bones[0].clusterId = 2;
    asset.value().bones[1].clusterId = 4;
    auto instance                    = GeometryCollectionInstance::create(*world, asset.value(), 1.f, 2.f, 3.f);
    REQUIRE(instance.ok());

    REQUIRE(instance.value()->applyField(strainAt(1.f, 3.f, 3.f, 5.f, 2.f)).ok());
    REQUIRE(instance.value()->step(simStep(5)).ok());
    REQUIRE(instance.value()->isEdgeBroken(0));

    auto capture = instance.value()->captureSnapshot();
    REQUIRE(capture.ok());
    auto encoded = capture.value().toValue();
    REQUIRE(encoded.ok());
    auto decoded = GeometryCollectionInstanceSnapshot::fromValue(encoded.value());
    REQUIRE(decoded.ok());
    REQUIRE_EQ(decoded.value().bones[0].clusterId, 2);
    REQUIRE_EQ(decoded.value().bones[1].clusterId, 4);
    REQUIRE(decoded.value().edges[0].broken);

    // Mutate live state then restore.
    DestructionField sleep;
    sleep.kind    = DestructionFieldKind::Sleep;
    sleep.falloff = DestructionFieldFalloff::None;
    sleep.centerX = 1.f;
    sleep.centerY = 3.f;
    sleep.centerZ = 3.f;
    sleep.radius  = 5.f;
    for (int i = 0; i < 2; ++i) {
        auto body = instance.value()->boneLink(i).value().resolve(*world);
        REQUIRE(body.ok());
        body.value()->setLinearVelocity(0.f, 0.f, 0.f);
    }
    REQUIRE(instance.value()->applyField(sleep).ok());

    auto restored = instance.value()->restoreSnapshot(decoded.value());
    REQUIRE(restored.ok());
    REQUIRE(instance.value()->isEdgeBroken(0));
    REQUIRE(instance.value()->boneState(0) == BoneRuntimeState::Detached ||
            instance.value()->boneState(0) == BoneRuntimeState::Attached);
    REQUIRE_EQ(instance.value()->boneClusterId(0), 2);
    REQUIRE_EQ(instance.value()->boneClusterId(1), 4);
    REQUIRE_EQ(instance.value()->pendingEdgeBreakCount(), 0);

    auto registered = GeometryCollectionInstanceSnapshot::ensureSchemaRegistered();
    REQUIRE(registered.ok());
    REQUIRE(eve::schema::SchemaRegistry::resolve(std::string(GeometryCollectionInstanceSnapshot::SchemaId),
                                                 static_cast<int>(GeometryCollectionInstanceSnapshot::SchemaVersion)) !=
            nullptr);
    auto schemas = Destruction::create()->registerGeometryCollectionSchema();
    REQUIRE(schemas.ok());
}

TEST_CASE("physics_destruction_p3.intactMultiEdgeKeepsBonesAttached") {
    auto*                    mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -9.8f, 0.f, true));
    GeometryCollectionAsset  asset;
    for (int i = 0; i < 3; ++i) {
        GeometryCollectionBone bone;
        bone.halfExtentX = 0.3f;
        bone.halfExtentY = 0.3f;
        bone.halfExtentZ = 0.3f;
        bone.localX      = static_cast<float>(i) * 0.7f;
        bone.localY      = 1.f;
        asset.bones.push_back(bone);
    }
    auto add = [&](int a, int b) {
        GeometryCollectionEdge edge;
        edge.boneA           = a;
        edge.boneB           = b;
        edge.strainThreshold = 1.f;
        asset.edges.push_back(edge);
    };
    add(0, 1);
    add(1, 2);
    add(2, 0);
    REQUIRE(asset.validate().ok());
    auto instance = GeometryCollectionInstance::create(*world, asset, 0.f, 0.f, 0.f);
    REQUIRE(instance.ok());
    // Before break, Attached bones are static (do not drift under gravity).
    auto body0 = instance.value()->boneLink(0).value().resolve(*world);
    REQUIRE(body0.ok());
    REQUIRE_EQ(body0.value()->getType(), std::string("static"));
    world->update(1.f / 60.f);
    REQUIRE_EQ(instance.value()->boneState(0), BoneRuntimeState::Attached);

    DestructionStepBudget budget;
    budget.maxEdgeBreaksPerStep = 1;
    instance.value()->setStepBudget(budget);
    // Strain only the 0-1 midpoint so a single edge breaks.
    REQUIRE(instance.value()->applyField(strainAt(0.35f, 1.f, 0.f, 0.35f, 2.f)).ok());
    REQUIRE(instance.value()->step(simStep(1)).ok());
    // One broken edge still leaves a connected triangle path — stay Attached.
    int broken = 0;
    for (int i = 0; i < 3; ++i)
        if (instance.value()->isEdgeBroken(i)) ++broken;
    REQUIRE_EQ(broken, 1);
    REQUIRE_EQ(instance.value()->boneState(0), BoneRuntimeState::Attached);
    REQUIRE_EQ(instance.value()->boneState(1), BoneRuntimeState::Attached);
    REQUIRE_EQ(instance.value()->boneState(2), BoneRuntimeState::Attached);
}

TEST_CASE("physics_destruction_p3.impulseSkipsAnchoredAndAttached") {
    auto*                    mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    auto                     asset = GeometryCollectionAsset::makeClusterPillarFixture(0.8f);
    REQUIRE(asset.ok());
    auto instance = GeometryCollectionInstance::create(*world, asset.value(), 0.f, 0.f, 0.f);
    REQUIRE(instance.ok());
    DestructionField impulse;
    impulse.kind      = DestructionFieldKind::Impulse;
    impulse.falloff   = DestructionFieldFalloff::None;
    impulse.centerX   = 0.f;
    impulse.centerY   = 1.f;
    impulse.centerZ   = 0.f;
    impulse.radius    = 5.f;
    impulse.magnitude = 10.f;
    auto applied      = instance.value()->applyField(impulse);
    REQUIRE(applied.ok());
    REQUIRE_EQ(applied.value().bonesAffected, 0);
    REQUIRE_EQ(instance.value()->boneState(0), BoneRuntimeState::Attached);
    REQUIRE(instance.value()->boneClusterId(0) == 0);
    REQUIRE(instance.value()->boneClusterId(3) == 1);
}
}  // namespace test_physics_destruction_p3
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_joint_frames)
#include "PrivateTestScope.h"
#include <cmath>
#include <memory>
#include <vector>
#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/World3D.h"
#include "zeroerr/unittest.h"
// physics_joint_frames.cpp
namespace test_physics_joint_frames {
using namespace eve::physics;

TEST_CASE("physics.joints.localFramesAreOwnedAndBodyRelative") {
    World3D                 world(0.f, 0.f, 0.f, false);
    std::unique_ptr<Body3D> a(world.newBody("static", 1.f, 2.f, 3.f));
    std::unique_ptr<Body3D> b(world.newBody("dynamic", 4.f, 2.f, 3.f));
    const float             half = std::sqrt(0.5f);
    a->setRotation(0.f, 0.f, half, half);
    std::unique_ptr<Joint3D> joint(world.newRevoluteJoint(a.get(), b.get(), 1.f, 4.f, 3.f, 0.f, 0.f, 1.f));
    auto                     frames = joint->localFrames();
    REQUIRE(frames.ok());
    CHECK(std::fabs(frames.value().bodyA[0] - 2.f) < 0.0001f);
    CHECK(std::fabs(frames.value().bodyA[1]) < 0.0001f);
    CHECK(std::fabs(frames.value().bodyB[0] + 3.f) < 0.0001f);
    CHECK(std::fabs(frames.value().bodyB[1] - 2.f) < 0.0001f);
    joint->destroy();
    auto stale = joint->localFrames();
    CHECK(!stale.ok());
    CHECK(std::fabs(frames.value().bodyA[0] - 2.f) < 0.0001f);
}

TEST_CASE("physics.joints.rotatedFramesSurviveSnapshotRestore") {
    const eve::SnapshotHashProvider hash = [](std::string_view input) {
        eve::ContentId::Bytes bytes{};
        std::uint64_t         value = 14695981039346656037ull;
        for (unsigned char byte : input) value = (value ^ byte) * 1099511628211ull;
        for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(value >> ((i % 8) * 8));
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
    World3D                 world(0.f, 0.f, 0.f, false);
    std::unique_ptr<Body3D> a(world.newBody("static", 1.f, 2.f, 3.f));
    std::unique_ptr<Body3D> b(world.newBody("dynamic", 4.f, 5.f, 6.f));
    a->setRotation(0.1f, 0.2f, 0.3f, 0.9f);
    b->setRotation(0.3f, -0.2f, 0.1f, 0.9f);
    std::vector<std::unique_ptr<Joint3D>> joints;
    joints.emplace_back(world.newDistanceJoint(a.get(), b.get(), 2.f, 3.f, 4.f, 4.f, 5.f, 6.f, 3.f));
    joints.emplace_back(world.newRevoluteJoint(a.get(), b.get(), 2.f, 3.f, 4.f, 1.f, 2.f, 3.f));
    joints.emplace_back(world.newPrismaticJoint(a.get(), b.get(), 2.f, 3.f, 4.f, 1.f, 2.f, 3.f));
    joints.emplace_back(world.newSphericalJoint(a.get(), b.get(), 2.f, 3.f, 4.f, 1.f, 2.f, 3.f));
    joints.emplace_back(world.newWheelJoint(a.get(), b.get(), 2.f, 3.f, 4.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f));
    joints.emplace_back(world.newWeldJoint(a.get(), b.get(), 2.f, 3.f, 4.f));
    joints.emplace_back(world.newMotorJoint(a.get(), b.get()));
    joints.emplace_back(world.newParallelJoint(a.get(), b.get(), 1.f, 2.f, 3.f));
    joints.emplace_back(world.newFilterJoint(a.get(), b.get()));
    auto before = world.snapshot(hash);
    REQUIRE(before.ok());
    auto restored = world.restore(before.value(), hash);
    REQUIRE(restored.ok());
    auto after = world.snapshot(hash);
    REQUIRE(after.ok());
    const auto* left  = before.value().payload.find("joints");
    const auto* right = after.value().payload.find("joints");
    REQUIRE(left != nullptr);
    REQUIRE(right != nullptr);
    REQUIRE_EQ(left->arraySize(), 9u);
    REQUIRE_EQ(right->arraySize(), left->arraySize());
    for (std::size_t i = 0; i < left->arraySize(); ++i) {
        const auto* l = left->at(i).find("localFrames");
        const auto* r = right->at(i).find("localFrames");
        REQUIRE(l != nullptr);
        REQUIRE(r != nullptr);
        for (std::size_t component = 0; component < 14; ++component)
            CHECK(std::fabs(l->at(component).asDouble() - r->at(component).asDouble()) < 0.0001);
    }
}
}  // namespace test_physics_joint_frames
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_jolt_cooking)
#include "PrivateTestScope.h"
#include <cmath>
#include <limits>
#include <memory>
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "physics/box3d/Box3DWorldFactory.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#if EVENGINE_TEST_HAS_JOLT
#include "physics/jolt/JoltWorldFactory.h"
#endif
// physics_jolt_cooking.cpp
namespace test_physics_jolt_cooking {
#if EVENGINE_TEST_HAS_JOLT

using namespace eve::physics;

TEST_CASE("physics.jolt.deferredDensityParity") {
    for (auto kind : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        World3D owner(0, 0, 0, false);
        auto    made = kind == RigidBackend3D::Box3D ? box3d::makeWorld(owner, 0, 0, 0, false)
                                                     : jolt::makeWorld(owner, 0, 0, 0, false);
        REQUIRE(made.ok());
        auto world      = std::move(made).takeValue();
        auto bodyResult = world->createBody("dynamic", 0, 0, 0);
        REQUIRE(bodyResult.ok());
        auto            body = std::move(bodyResult).takeValue();
        ShapeGeometry3D geometry;
        geometry.a = geometry.b = geometry.c = .5f;
        geometry.localX                      = -1;
        auto leftResult                      = body->createShape(geometry, 1, .2f, 0);
        REQUIRE(leftResult.ok());
        auto left        = std::move(leftResult).takeValue();
        geometry.localX  = 1;
        auto rightResult = body->createShape(geometry, 1, .2f, 0);
        REQUIRE(rightResult.ok());
        auto       right   = std::move(rightResult).takeValue();
        const auto mass    = body->getMass().value();
        const auto inertia = body->getInertiaYY().value();
        const auto center  = body->getLocalCenterX().value();
        REQUIRE(right->setDensity(3, false).ok());
        CHECK(right->density().value() == 3);
        CHECK(body->getMass().value() == mass);
        CHECK(body->getInertiaYY().value() == inertia);
        CHECK(body->getLocalCenterX().value() == center);
        CHECK(right->testPoint(1, 0, 0).value());
        // Setting the same density does not flush pending mass in Box3D.
        REQUIRE(right->setDensity(3, true).ok());
        CHECK(body->getMass().value() == mass);
        REQUIRE(body->resetMassProperties().ok());
        CHECK(std::abs(body->getMass().value() - 4) < .001f);
        CHECK(std::abs(body->getLocalCenterX().value() - .5f) < .001f);
        CHECK(body->getInertiaYY().value() > inertia);
        CHECK(body->getX().value() == 0);
        REQUIRE(left->setDensity(2, false).ok());
        REQUIRE(right->setDensity(4, true).ok());
        CHECK(std::abs(body->getMass().value() - 6) < .001f);
        CHECK(std::abs(body->getLocalCenterX().value() - 1.f / 3.f) < .001f);
        if (kind == RigidBackend3D::Jolt) {
            auto invalid = right->setDensity(std::numeric_limits<float>::quiet_NaN(), false);
            CHECK(!invalid.ok());
            CHECK(right->density().value() == 4);
            CHECK(std::abs(body->getMass().value() - 6) < .001f);
        }
        REQUIRE(left->setDensity(4, false).ok());
        REQUIRE(body->setType("dynamic").ok());
        CHECK(std::abs(body->getMass().value() - 6) < .001f);
        REQUIRE(body->setType("kinematic").ok());
        REQUIRE(body->setType("dynamic").ok());
        CHECK(std::abs(body->getMass().value() - 8) < .001f);
        CHECK(std::abs(body->getLocalCenterX().value()) < .001f);
    }
}


#endif
}  // namespace test_physics_jolt_cooking
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_mechanism)
#include "PrivateTestScope.h"
#include <cmath>
#include <memory>
#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/Mechanism3D.h"
#include "physics/Physics.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// physics_mechanism.cpp
namespace test_physics_mechanism {
using eve::physics::Body3D;
using eve::physics::Joint3D;
using eve::physics::Mechanism3D;
using eve::physics::Physics;
using eve::physics::World3D;

TEST_CASE("physics.joint.weldMotorParallelAndFilterAreUsable") {
    auto*                    mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D*                  a = world->newBody("static", 0.f, 0.f, 0.f);
    Body3D*                  b = world->newBody("dynamic", 0.5f, 0.f, 0.f);
    b->newBoxShape(0.5f, 0.5f, 0.5f, 1.f);
    Body3D* c = world->newBody("dynamic", 0.f, 1.f, 0.f);
    c->newBoxShape(0.4f, 0.4f, 0.4f, 1.f);

    Joint3D* weld = world->newWeldJoint(a, b, 0.25f, 0.f, 0.f, false);
    REQUIRE(weld != nullptr);
    CHECK_EQ(weld->getKind(), std::string("weld"));
    weld->setWeldLinearSpring(0.f, 1.f);
    weld->setWeldAngularSpring(0.f, 1.f);
    CHECK(std::fabs(weld->getWeldLinearHertz()) < 0.001f);

    Joint3D* motor = world->newMotorJoint(a, c, false);
    REQUIRE(motor != nullptr);
    CHECK_EQ(motor->getKind(), std::string("motor"));
    motor->setMotorAngularVelocity(0.f, 0.f, 2.f);
    motor->setMotorVelocityLimits(0.f, 50.f);
    CHECK(std::fabs(motor->getMotorAngularVelocityZ() - 2.f) < 0.001f);

    Joint3D* parallel = world->newParallelJoint(a, c, 0.f, 1.f, 0.f, false);
    REQUIRE(parallel != nullptr);
    CHECK_EQ(parallel->getKind(), std::string("parallel"));
    parallel->setParallelSpring(8.f, 0.7f, 100.f);
    CHECK(std::fabs(parallel->getParallelHertz() - 8.f) < 0.001f);

    Joint3D* filter = world->newFilterJoint(b, c);
    REQUIRE(filter != nullptr);
    CHECK_EQ(filter->getKind(), std::string("filter"));

    for (int i = 0; i < 60; ++i) world->updateFull(1.f / 60.f, 4);
    CHECK(std::fabs(b->getY()) < 0.2f);
    CHECK_THROWS((world->newParallelJoint(a, c, 0.f, 0.f, 0.f, false), false));
    CHECK_THROWS((weld->setMotorLinearVelocity(1.f, 0.f, 0.f), false));
}

TEST_CASE("physics.mechanism.shaftDriveSpinsRotor") {
    auto*                    mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D*                  support = world->newBody("static", 0.f, 0.f, 0.f);
    Body3D*                  rotor   = world->newBody("dynamic", 0.f, 0.f, 0.f);
    rotor->newBoxShape(1.f, 0.2f, 0.2f, 1.f);
    Mechanism3D* shaft = world->newShaft(support, rotor, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, false);
    REQUIRE(shaft != nullptr);
    CHECK_EQ(shaft->getKind(), std::string("shaft"));
    shaft->setDrive(4.f, 80.f);
    for (int i = 0; i < 120; ++i) world->updateFull(1.f / 60.f, 4);
    CHECK_GT(std::fabs(shaft->getDriveAngle()), 1.f);
    CHECK_GT(std::fabs(shaft->getSpinSpeed()), 1.f);
    shaft->clearDrive();
    CHECK(!shaft->isDriveEnabled());
}

TEST_CASE("physics.mechanism.ratchetBlocksReverseSpin") {
    auto*                    mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D*                  frame = world->newBody("static", 0.f, 0.f, 0.f);
    Body3D*                  wheel = world->newBody("dynamic", 0.f, 0.f, 0.f);
    wheel->newBoxShape(1.f, 0.2f, 0.2f, 1.f);
    Mechanism3D* ratchet = world->newRatchet(frame, wheel, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1, 200.f, false);
    REQUIRE(ratchet != nullptr);
    CHECK_EQ(ratchet->getKind(), std::string("ratchet"));
    CHECK_EQ(ratchet->getRatchetDirection(), 1);

    // Freewheel forward under drive.
    ratchet->setDrive(3.f, 80.f);
    for (int i = 0; i < 90; ++i) world->updateFull(1.f / 60.f, 4);
    const float forwardAngle = ratchet->getDriveAngle();
    CHECK_GT(forwardAngle, 0.5f);

    // Attempt reverse: engagement should hold near the freewheel peak.
    ratchet->clearDrive();
    ratchet->setRatchetEngagementTorque(500.f);
    wheel->setAngularVelocity(0.f, 0.f, -20.f);
    for (int i = 0; i < 90; ++i) world->updateFull(1.f / 60.f, 4);
    CHECK_GT(ratchet->getDriveAngle(), forwardAngle - 0.35f);
    CHECK_THROWS((world->newRatchet(frame, wheel, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0, 10.f, false), false));
}

TEST_CASE("physics.mechanism.crankSliderConvertsRotationToStroke") {
    auto*                    mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D*                  frame = world->newBody("static", 0.f, 0.f, 0.f);
    Body3D*                  crank = world->newBody("dynamic", 0.25f, 0.f, 0.f);
    crank->newBoxShape(0.5f, 0.1f, 0.1f, 1.f);
    Body3D* rod = world->newBody("dynamic", 0.85f, 0.f, 0.f);
    rod->newBoxShape(0.7f, 0.08f, 0.08f, 0.5f);
    Body3D* slider = world->newBody("dynamic", 1.4f, 0.f, 0.f);
    slider->newBoxShape(0.3f, 0.3f, 0.3f, 1.f);

    Mechanism3D* cs = world->newCrankSlider(frame, crank, rod, slider, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.5f, 0.f, 0.f,
                                            1.2f, 0.f, 0.f, 1.f, 0.f, 0.f, false);
    REQUIRE(cs != nullptr);
    CHECK_EQ(cs->getKind(), std::string("crankSlider"));
    REQUIRE(cs->getSliderJoint() != nullptr);
    cs->setDrive(5.f, 120.f);

    float minT = 1e9f, maxT = -1e9f;
    for (int i = 0; i < 240; ++i) {
        world->updateFull(1.f / 60.f, 4);
        const float t = cs->getSliderTranslation();
        minT          = std::min(minT, t);
        maxT          = std::max(maxT, t);
    }
    CHECK_GT(maxT - minT, 0.35f);
    CHECK(std::fabs(slider->getY()) < 0.15f);
    CHECK(std::fabs(slider->getZ()) < 0.15f);
    CHECK_THROWS((world->newCrankSlider(frame, crank, rod, slider, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.5f, 0.f, 0.f, 1.2f,
                                        0.f, 0.f, 1.f, 0.f, 0.f, false),
                  false));
}
}  // namespace test_physics_mechanism
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_mechanism2d)
#include "PrivateTestScope.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include "physics/Body.h"
#include "physics/Joint2D.h"
#include "physics/Mechanism2D.h"
#include "physics/Physics.h"
#include "physics/World.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// physics_mechanism2d.cpp
namespace test_physics_mechanism2d {
using eve::physics::Body;
using eve::physics::Joint2D;
using eve::physics::Mechanism2D;
using eve::physics::Physics;
using eve::physics::World;

TEST_CASE("physics.joint2d.distanceRevoluteWeldMotorAndGearAreUsable") {
    auto*                  mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, false));
    world->setMeter(30.f);

    Body* anchor = world->newBody("static", 0.f, 0.f);
    Body* a      = world->newBody("dynamic", 60.f, 0.f);
    a->newRectangleFixture(30.f, 30.f, 1.f, 0.3f, 0.1f);
    Body* b = world->newBody("dynamic", 0.f, 60.f);
    b->newRectangleFixture(24.f, 24.f, 1.f, 0.3f, 0.1f);
    Body* c = world->newBody("dynamic", 120.f, 0.f);
    c->newRectangleFixture(20.f, 20.f, 1.f, 0.3f, 0.1f);

    Joint2D* distance = world->newDistanceJoint(anchor, a, 0.f, 0.f, 60.f, 0.f, 60.f, false);
    REQUIRE(distance != nullptr);
    CHECK_EQ(distance->getKind(), std::string("distance"));
    distance->setDistanceSpring(4.f, 0.7f);
    CHECK(std::fabs(distance->getDistanceFrequency() - 4.f) < 0.001f);

    Joint2D* revolute = world->newRevoluteJoint(anchor, b, 0.f, 0.f, false);
    REQUIRE(revolute != nullptr);
    CHECK_EQ(revolute->getKind(), std::string("revolute"));
    revolute->setRevoluteMotor(true, 2.f, 50.f);

    Joint2D* weld = world->newWeldJoint(a, c, 90.f, 0.f, false);
    REQUIRE(weld != nullptr);
    CHECK_EQ(weld->getKind(), std::string("weld"));
    weld->setWeldSpring(0.f, 1.f);

    Joint2D* motor = world->newMotorJoint(anchor, c, false);
    REQUIRE(motor != nullptr);
    CHECK_EQ(motor->getKind(), std::string("motor"));
    motor->setMotorAngularOffset(0.5f);
    motor->setMotorLimits(1000.f, 50.f);
    CHECK(std::fabs(motor->getMotorAngularOffset() - 0.5f) < 0.001f);

    Joint2D* prismatic = world->newPrismaticJoint(anchor, c, 120.f, 0.f, 1.f, 0.f, false);
    REQUIRE(prismatic != nullptr);
    Joint2D* gear = world->newGearJoint(revolute, prismatic, 1.f);
    REQUIRE(gear != nullptr);
    CHECK_EQ(gear->getKind(), std::string("gear"));
    gear->setGearRatio(2.f);
    CHECK(std::fabs(gear->getGearRatio() - 2.f) < 0.001f);

    for (int i = 0; i < 60; ++i) world->updateFull(1.f / 60.f, 8, 3);
    CHECK_GT(std::fabs(revolute->getRevoluteAngle()), 0.2f);
    CHECK_THROWS((world->newPrismaticJoint(anchor, a, 0.f, 0.f, 0.f, 0.f, false), false));
    CHECK_THROWS((distance->setRevoluteMotor(true, 1.f, 1.f), false));
}

TEST_CASE("physics.mechanism2d.shaftDriveSpinsRotor") {
    auto*                  mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, false));
    world->setMeter(30.f);
    Body* support = world->newBody("static", 0.f, 0.f);
    Body* rotor   = world->newBody("dynamic", 0.f, 0.f);
    rotor->newRectangleFixture(60.f, 12.f, 1.f, 0.3f, 0.1f);
    Mechanism2D* shaft = world->newShaft(support, rotor, 0.f, 0.f, false);
    REQUIRE(shaft != nullptr);
    CHECK_EQ(shaft->getKind(), std::string("shaft"));
    shaft->setDrive(4.f, 80.f);
    for (int i = 0; i < 120; ++i) world->updateFull(1.f / 60.f, 8, 3);
    CHECK_GT(std::fabs(shaft->getDriveAngle()), 1.f);
    CHECK_GT(std::fabs(shaft->getSpinSpeed()), 1.f);
    shaft->clearDrive();
    CHECK(!shaft->isDriveEnabled());
}

TEST_CASE("physics.mechanism2d.ratchetBlocksReverseSpin") {
    auto*                  mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, false));
    world->setMeter(30.f);
    Body* frame = world->newBody("static", 0.f, 0.f);
    Body* wheel = world->newBody("dynamic", 0.f, 0.f);
    wheel->newRectangleFixture(60.f, 12.f, 1.f, 0.3f, 0.1f);
    Mechanism2D* ratchet = world->newRatchet(frame, wheel, 0.f, 0.f, 1, 200.f, false);
    REQUIRE(ratchet != nullptr);
    CHECK_EQ(ratchet->getKind(), std::string("ratchet"));
    CHECK_EQ(ratchet->getRatchetDirection(), 1);

    ratchet->setDrive(3.f, 80.f);
    for (int i = 0; i < 90; ++i) world->updateFull(1.f / 60.f, 8, 3);
    const float forwardAngle = ratchet->getDriveAngle();
    CHECK_GT(forwardAngle, 0.5f);

    ratchet->clearDrive();
    ratchet->setRatchetEngagementTorque(500.f);
    wheel->setAngularVelocity(-20.f);
    for (int i = 0; i < 90; ++i) world->updateFull(1.f / 60.f, 8, 3);
    CHECK_GT(ratchet->getDriveAngle(), forwardAngle - 0.35f);
    CHECK_THROWS((world->newRatchet(frame, wheel, 0.f, 0.f, 0, 10.f, false), false));
}

TEST_CASE("physics.mechanism2d.crankSliderConvertsRotationToStroke") {
    auto*                  mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, false));
    world->setMeter(30.f);

    Body* frame = world->newBody("static", 0.f, 0.f);
    Body* crank = world->newBody("dynamic", 15.f, 0.f);
    crank->newRectangleFixture(30.f, 6.f, 1.f, 0.3f, 0.1f);
    Body* rod = world->newBody("dynamic", 50.f, 0.f);
    rod->newRectangleFixture(40.f, 5.f, 0.5f, 0.3f, 0.1f);
    Body* slider = world->newBody("dynamic", 85.f, 0.f);
    slider->newRectangleFixture(18.f, 18.f, 1.f, 0.3f, 0.1f);

    Mechanism2D* cs = world->newCrankSlider(frame, crank, rod, slider, 0.f, 0.f, 30.f, 0.f, 70.f, 0.f, 1.f, 0.f, false);
    REQUIRE(cs != nullptr);
    CHECK_EQ(cs->getKind(), std::string("crankSlider"));
    REQUIRE(cs->getSliderJoint() != nullptr);
    cs->setDrive(5.f, 120.f);

    float minT = 1e9f, maxT = -1e9f;
    for (int i = 0; i < 240; ++i) {
        world->updateFull(1.f / 60.f, 8, 3);
        const float t = cs->getSliderTranslation();
        minT          = std::min(minT, t);
        maxT          = std::max(maxT, t);
    }
    CHECK_GT(maxT - minT, 10.f);
    CHECK(std::fabs(slider->getY()) < 8.f);
    CHECK_THROWS(
        (world->newCrankSlider(frame, crank, rod, slider, 0.f, 0.f, 30.f, 0.f, 70.f, 0.f, 0.f, 0.f, false), false));
}
}  // namespace test_physics_mechanism2d
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_rigid_backend)
#include "PrivateTestScope.h"
#include <memory>
#include "common/Exception.h"
#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "zeroerr/unittest.h"
// physics_rigid_backend.cpp
namespace test_physics_rigid_backend {
using namespace eve::physics;
namespace {
struct ProbeState {
    float x         = 0.f;
    int   destroyed = 0;
    bool  alive     = true;
};
template <class T>
[[maybe_unused]] eve::Result<T> unsupported() {
    return eve::Result<T>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Unsupported,
                                                          "injected provider rejection", "physics.backend.probe"));
}
#define PROBE_UNSUPPORTED(type, name, arguments, qualifiers) \
    eve::Result<type> name arguments qualifiers override { return unsupported<type>(); }
class BodyProbe final : public IBodyBackend3D {
public:
    PROBE_UNSUPPORTED(std::unique_ptr<IShapeBackend3D>, createShape, (const ShapeGeometry3D&, float, float, float), )
    void bindOwner(Body3D&) noexcept override {}
    explicit BodyProbe(ProbeState& state) : state_(state) {}
    ~BodyProbe() override { destroy(); }
    bool isValid() const noexcept override { return state_.alive; }
    void destroy() noexcept override {
        if (state_.alive) ++state_.destroyed;
        state_.alive = false;
    }
    void              invalidate() noexcept override { state_.alive = false; }
    eve::Result<void> setPosition(float x, float, float) override {
        state_.x = x;
        return eve::Result<void>::success();
    }
    eve::Result<float> getX() const override { return eve::Result<float>::success(state_.x); }
    PROBE_UNSUPPORTED(float, getY, (), const)
    PROBE_UNSUPPORTED(float, getZ, (), const)
    PROBE_UNSUPPORTED(void, setRotation, (float, float, float, float), )
    PROBE_UNSUPPORTED(float, getRotX, (), const)
    PROBE_UNSUPPORTED(float, getRotY, (), const)
    PROBE_UNSUPPORTED(float, getRotZ, (), const)
    PROBE_UNSUPPORTED(float, getRotW, (), const)
    PROBE_UNSUPPORTED(void, setLinearVelocity, (float, float, float), )
    PROBE_UNSUPPORTED(float, getLinearVelocityX, (), const)
    PROBE_UNSUPPORTED(float, getLinearVelocityY, (), const)
    PROBE_UNSUPPORTED(float, getLinearVelocityZ, (), const)
    PROBE_UNSUPPORTED(float, getMass, (), const)
    PROBE_UNSUPPORTED(void, setAngularVelocity, (float, float, float), )
    PROBE_UNSUPPORTED(float, getAngularVelocityX, (), const)
    PROBE_UNSUPPORTED(float, getAngularVelocityY, (), const)
    PROBE_UNSUPPORTED(float, getAngularVelocityZ, (), const)
    PROBE_UNSUPPORTED(std::vector<float>, localToWorldPoint, (float, float, float), const)
    PROBE_UNSUPPORTED(PhysicsVector3D, localToWorldPointOwned, (float, float, float), const)
    PROBE_UNSUPPORTED(std::vector<float>, worldToLocalPoint, (float, float, float), const)
    PROBE_UNSUPPORTED(PhysicsVector3D, worldToLocalPointOwned, (float, float, float), const)
    PROBE_UNSUPPORTED(std::vector<float>, localToWorldVector, (float, float, float), const)
    PROBE_UNSUPPORTED(PhysicsVector3D, localToWorldVectorOwned, (float, float, float), const)
    PROBE_UNSUPPORTED(std::vector<float>, worldToLocalVector, (float, float, float), const)
    PROBE_UNSUPPORTED(std::vector<float>, getLocalPointVelocity, (float, float, float), const)
    PROBE_UNSUPPORTED(PhysicsVector3D, getLocalPointVelocityOwned, (float, float, float), const)
    PROBE_UNSUPPORTED(std::vector<float>, getWorldPointVelocity, (float, float, float), const)
    PROBE_UNSUPPORTED(void, applyForce, (float, float, float), )
    PROBE_UNSUPPORTED(void, applyForceAt, (float, float, float, float, float, float), )
    PROBE_UNSUPPORTED(void, applyTorque, (float, float, float), )
    PROBE_UNSUPPORTED(void, applyLinearImpulse, (float, float, float), )
    PROBE_UNSUPPORTED(void, applyLinearImpulseAt, (float, float, float, float, float, float), )
    PROBE_UNSUPPORTED(void, applyAngularImpulse, (float, float, float), )
    PROBE_UNSUPPORTED(void, applyLocalForce, (float, float, float, float, float, float), )
    PROBE_UNSUPPORTED(void, applyLocalForceToCenter, (float, float, float), )
    PROBE_UNSUPPORTED(void, applyLocalTorque, (float, float, float), )
    PROBE_UNSUPPORTED(void, applyLocalLinearImpulse, (float, float, float, float, float, float), )
    PROBE_UNSUPPORTED(void, applyLocalLinearImpulseToCenter, (float, float, float), )
    PROBE_UNSUPPORTED(void, applyLocalAngularImpulse, (float, float, float), )
    PROBE_UNSUPPORTED(void, setTargetTransform, (float, float, float, float, float, float, float, float), )
    PROBE_UNSUPPORTED(void, setLinearDamping, (float), )
    PROBE_UNSUPPORTED(float, getLinearDamping, (), const)
    PROBE_UNSUPPORTED(void, setAngularDamping, (float), )
    PROBE_UNSUPPORTED(float, getAngularDamping, (), const)
    PROBE_UNSUPPORTED(void, setGravityScale, (float), )
    PROBE_UNSUPPORTED(float, getGravityScale, (), const)
    PROBE_UNSUPPORTED(void, setSleepEnabled, (bool), )
    PROBE_UNSUPPORTED(bool, isSleepEnabled, (), const)
    PROBE_UNSUPPORTED(void, setSleepThreshold, (float), )
    PROBE_UNSUPPORTED(float, getSleepThreshold, (), const)
    PROBE_UNSUPPORTED(void, setMotionLocks, (bool, bool, bool, bool, bool, bool), )
    PROBE_UNSUPPORTED(bool, isLinearXLocked, (), const)
    PROBE_UNSUPPORTED(bool, isLinearYLocked, (), const)
    PROBE_UNSUPPORTED(bool, isLinearZLocked, (), const)
    PROBE_UNSUPPORTED(bool, isAngularXLocked, (), const)
    PROBE_UNSUPPORTED(bool, isAngularYLocked, (), const)
    PROBE_UNSUPPORTED(bool, isAngularZLocked, (), const)
    PROBE_UNSUPPORTED(void, setMassProperties, (float, float, float, float, float, float, float, float, float, float), )
    PROBE_UNSUPPORTED(void, resetMassProperties, (), )
    PROBE_UNSUPPORTED(float, getInertiaXX, (), const)
    PROBE_UNSUPPORTED(float, getInertiaYY, (), const)
    PROBE_UNSUPPORTED(float, getInertiaZZ, (), const)
    PROBE_UNSUPPORTED(float, getInertiaXY, (), const)
    PROBE_UNSUPPORTED(float, getInertiaXZ, (), const)
    PROBE_UNSUPPORTED(float, getInertiaYZ, (), const)
    PROBE_UNSUPPORTED(float, getLocalCenterX, (), const)
    PROBE_UNSUPPORTED(float, getLocalCenterY, (), const)
    PROBE_UNSUPPORTED(float, getLocalCenterZ, (), const)
    PROBE_UNSUPPORTED(float, getWorldCenterX, (), const)
    PROBE_UNSUPPORTED(float, getWorldCenterY, (), const)
    PROBE_UNSUPPORTED(float, getWorldCenterZ, (), const)
    PROBE_UNSUPPORTED(void, setType, (const std::string&), )
    PROBE_UNSUPPORTED(std::string, getType, (), const)
    PROBE_UNSUPPORTED(void, setFixedRotation, (bool), )
    PROBE_UNSUPPORTED(bool, isFixedRotation, (), const)
    PROBE_UNSUPPORTED(void, setActive, (bool), )
    PROBE_UNSUPPORTED(bool, isActive, (), const)
    PROBE_UNSUPPORTED(void, setBullet, (bool), )
    PROBE_UNSUPPORTED(bool, isBullet, (), const)
    PROBE_UNSUPPORTED(void, setAwake, (bool), )
    PROBE_UNSUPPORTED(bool, isAwake, (), const)
private:
    ProbeState& state_;
};
class JointProbe final : public IJointBackend3D {
public:
    void bindOwner(Joint3D&) noexcept override {}
    explicit JointProbe(ProbeState& state) : state_(state) {}
    ~JointProbe() override { destroy(); }
    bool isValid() const noexcept override { return state_.alive; }
    void destroy() noexcept override {
        if (state_.alive) ++state_.destroyed;
        state_.alive = false;
    }
    void                       invalidate() noexcept override { state_.alive = false; }
    eve::Result<JointFrames3D> localFrames() const override {
        JointFrames3D frames;
        frames.bodyA[0] = state_.x;
        return eve::Result<JointFrames3D>::success(frames);
    }
    PROBE_UNSUPPORTED(void, setCollideConnected, (bool), )
    PROBE_UNSUPPORTED(bool, getCollideConnected, (), const)
    PROBE_UNSUPPORTED(void, setForceThreshold, (float), )
    PROBE_UNSUPPORTED(float, getForceThreshold, (), const)
    PROBE_UNSUPPORTED(void, setTorqueThreshold, (float), )
    PROBE_UNSUPPORTED(float, getTorqueThreshold, (), const)
    PROBE_UNSUPPORTED(float, getConstraintForceX, (), const)
    PROBE_UNSUPPORTED(float, getConstraintForceY, (), const)
    PROBE_UNSUPPORTED(float, getConstraintForceZ, (), const)
    PROBE_UNSUPPORTED(float, getConstraintTorqueX, (), const)
    PROBE_UNSUPPORTED(float, getConstraintTorqueY, (), const)
    PROBE_UNSUPPORTED(float, getConstraintTorqueZ, (), const)
    PROBE_UNSUPPORTED(float, getLinearSeparation, (), const)
    PROBE_UNSUPPORTED(float, getAngularSeparation, (), const)
    PROBE_UNSUPPORTED(void, setDistanceLength, (float), )
    PROBE_UNSUPPORTED(float, getDistanceLength, (), const)
    PROBE_UNSUPPORTED(float, getDistanceCurrentLength, (), const)
    PROBE_UNSUPPORTED(void, setDistanceSpring, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setDistanceLimits, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setDistanceMotor, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setRevoluteSpring, (bool, float, float, float), )
    PROBE_UNSUPPORTED(void, setRevoluteLimits, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setRevoluteMotor, (bool, float, float), )
    PROBE_UNSUPPORTED(float, getRevoluteAngle, (), const)
    PROBE_UNSUPPORTED(float, getRevoluteMotorTorque, (), const)
    PROBE_UNSUPPORTED(void, setPrismaticSpring, (bool, float, float, float), )
    PROBE_UNSUPPORTED(void, setPrismaticLimits, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setPrismaticMotor, (bool, float, float), )
    PROBE_UNSUPPORTED(float, getPrismaticTranslation, (), const)
    PROBE_UNSUPPORTED(float, getPrismaticSpeed, (), const)
    PROBE_UNSUPPORTED(float, getPrismaticMotorForce, (), const)
    PROBE_UNSUPPORTED(void, setSphericalConeLimit, (bool, float), )
    PROBE_UNSUPPORTED(void, setSphericalTwistLimits, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setSphericalMotor, (bool, float, float, float, float), )
    PROBE_UNSUPPORTED(float, getSphericalConeAngle, (), const)
    PROBE_UNSUPPORTED(float, getSphericalTwistAngle, (), const)
    PROBE_UNSUPPORTED(void, setWheelSuspension, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setWheelSuspensionLimits, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setWheelSpinMotor, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setWheelSteering, (bool, float, float, float, float), )
    PROBE_UNSUPPORTED(void, setWheelSteeringLimits, (bool, float, float), )
    PROBE_UNSUPPORTED(float, getWheelSpinSpeed, (), const)
    PROBE_UNSUPPORTED(float, getWheelSpinTorque, (), const)
    PROBE_UNSUPPORTED(float, getWheelSteeringAngle, (), const)
    PROBE_UNSUPPORTED(float, getWheelSteeringTorque, (), const)
    PROBE_UNSUPPORTED(void, setWeldLinearSpring, (float, float), )
    PROBE_UNSUPPORTED(void, setWeldAngularSpring, (float, float), )
    PROBE_UNSUPPORTED(float, getWeldLinearHertz, (), const)
    PROBE_UNSUPPORTED(float, getWeldLinearDampingRatio, (), const)
    PROBE_UNSUPPORTED(float, getWeldAngularHertz, (), const)
    PROBE_UNSUPPORTED(float, getWeldAngularDampingRatio, (), const)
    PROBE_UNSUPPORTED(void, setMotorLinearVelocity, (float, float, float), )
    PROBE_UNSUPPORTED(void, setMotorAngularVelocity, (float, float, float), )
    PROBE_UNSUPPORTED(void, setMotorVelocityLimits, (float, float), )
    PROBE_UNSUPPORTED(void, setMotorLinearSpring, (float, float, float), )
    PROBE_UNSUPPORTED(void, setMotorAngularSpring, (float, float, float), )
    PROBE_UNSUPPORTED(float, getMotorLinearVelocityX, (), const)
    PROBE_UNSUPPORTED(float, getMotorLinearVelocityY, (), const)
    PROBE_UNSUPPORTED(float, getMotorLinearVelocityZ, (), const)
    PROBE_UNSUPPORTED(float, getMotorAngularVelocityX, (), const)
    PROBE_UNSUPPORTED(float, getMotorAngularVelocityY, (), const)
    PROBE_UNSUPPORTED(float, getMotorAngularVelocityZ, (), const)
    PROBE_UNSUPPORTED(void, setParallelSpring, (float, float, float), )
    PROBE_UNSUPPORTED(float, getParallelHertz, (), const)
    PROBE_UNSUPPORTED(float, getParallelDampingRatio, (), const)
    PROBE_UNSUPPORTED(float, getParallelMaxTorque, (), const)
private:
    ProbeState& state_;
};
class ShapeProbe final : public IShapeBackend3D {
public:
    bool              alive = true;
    bool              isValid() const noexcept override { return alive; }
    void              destroy() noexcept override { alive = false; }
    void              invalidate() noexcept override { alive = false; }
    void              bindOwner(Shape3D&) noexcept override {}
    eve::Result<bool> isSensor() const override { return eve::Result<bool>::success(false); }
    PROBE_UNSUPPORTED(ShapePlane3D, oneWayPlane, (const ShapeGeometry3D&, PhysicsVector3D, float), const)
    PROBE_UNSUPPORTED(void, rebuild, (const ShapeGeometry3D&, ShapeBehavior3D, std::span<const SurfaceMaterial3D>), )
    PROBE_UNSUPPORTED(float, density, (), const)
    PROBE_UNSUPPORTED(void, setDensity, (float, bool), )
    PROBE_UNSUPPORTED(float, friction, (), const)
    PROBE_UNSUPPORTED(void, setFriction, (float), )
    PROBE_UNSUPPORTED(float, restitution, (), const)
    PROBE_UNSUPPORTED(void, setRestitution, (float), )
    PROBE_UNSUPPORTED(SurfaceMaterial3D, material, (), const)
    PROBE_UNSUPPORTED(void, setMaterial, (SurfaceMaterial3D), )
    PROBE_UNSUPPORTED(ShapeFilter3D, filter, (), const)
    PROBE_UNSUPPORTED(void, setFilter, (ShapeFilter3D, bool), )
    PROBE_UNSUPPORTED(void, enableHitEvents, (bool), )
    PROBE_UNSUPPORTED(void, enablePreSolveEvents, (bool), )
    PROBE_UNSUPPORTED(int, meshMaterialCount, (), const)
    PROBE_UNSUPPORTED(SurfaceMaterial3D, meshMaterial, (int), const)
    PROBE_UNSUPPORTED(void, setMeshMaterial, (SurfaceMaterial3D, int), )
    PROBE_UNSUPPORTED(bool, testPoint, (float, float, float), const)
};

[[maybe_unused]] void checkPositionContract(Body3D& body) {
    for (float x : {7.f, -3.f, 0.f}) {
        body.setPosition(x, 0.f, 0.f);
        CHECK(body.getX() == x);
    }
    body.destroy();
    CHECK(!body.isValid());
    body.destroy();
    CHECK(!body.isValid());
}
#undef PROBE_UNSUPPORTED
}  // namespace

TEST_CASE("physics.backend.bodyDispatchDoesNotRequireNativeBody") {
    World3D    world(0.f, 0.f, 0.f, false);
    ProbeState state;
    {
        Body3D body(&world, std::make_unique<BodyProbe>(state), 123, PhysicsBodyHandle::invalid());
        body.setPosition(7.f, 8.f, 9.f);
        CHECK(body.getX() == 7.f);
        bool rejected = false;
        try {
            body.applyForce(1.f, 2.f, 3.f);
        } catch (const eve::Exception&) {
            rejected = true;
        }
        CHECK(rejected);
        CHECK(body.getX() == 7.f);
        auto result = body.localToWorldPointOwned(1.f, 2.f, 3.f);
        CHECK(!result.ok());
        REQUIRE(result.error() != nullptr);
        CHECK(result.error()->code() == eve::DiagnosticCode::Unsupported);
        body.destroy();
        CHECK(!body.isValid());
        CHECK(state.destroyed == 1);
        CHECK(body.getRotW() == 1.f);
    }
    CHECK(state.destroyed == 1);
}

TEST_CASE("physics.backend.jointDispatchDoesNotRequireNativeConstraint") {
    World3D    world(0.f, 0.f, 0.f, false);
    ProbeState state;
    state.x = 4.f;
    {
        Joint3D joint(&world, nullptr, nullptr, std::make_unique<JointProbe>(state), PhysicsJointHandle::invalid(),
                      Joint3D::Kind::Distance, 123);
        auto    frames = joint.localFrames();
        REQUIRE(frames.ok());
        CHECK(frames.value().bodyA[0] == 4.f);
        bool rejected = false;
        try {
            joint.setDistanceLength(2.f);
        } catch (const eve::Exception&) {
            rejected = true;
        }
        CHECK(rejected);
        joint.destroy();
        CHECK(!joint.isValid());
        CHECK(state.destroyed == 1);
    }
    CHECK(state.destroyed == 1);
}

TEST_CASE("physics.backend.parentDestructionInvalidatesBodyAndJoint") {
    auto                     world = std::make_unique<World3D>(0.f, 0.f, 0.f, false);
    std::unique_ptr<Body3D>  a(world->newBody("static", 0.f, 0.f, 0.f));
    std::unique_ptr<Body3D>  b(world->newBody("dynamic", 1.f, 0.f, 0.f));
    std::unique_ptr<Joint3D> joint(world->newDistanceJoint(a.get(), b.get(), 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f));
    world.reset();
    CHECK(!a->isValid());
    CHECK(!b->isValid());
    CHECK(!joint->isValid());
    auto position = b->localToWorldPointOwned(0.f, 0.f, 0.f);
    CHECK(!position.ok());
    REQUIRE(position.error() != nullptr);
    CHECK(position.error()->code() == eve::DiagnosticCode::StaleHandle);
    auto frames = joint->localFrames();
    CHECK(!frames.ok());
    REQUIRE(frames.error() != nullptr);
    CHECK(frames.error()->code() == eve::DiagnosticCode::StaleHandle);
}


TEST_CASE("physics.backend.sharedBodyPositionAndLifetimeContract") {
    World3D                 world(0.f, 0.f, 0.f, false);
    std::unique_ptr<Body3D> native(world.newBody("dynamic", 0.f, 0.f, 0.f));
    checkPositionContract(*native);
    ProbeState state;
    Body3D     probe(nullptr, std::make_unique<BodyProbe>(state), 1, PhysicsBodyHandle::invalid());
    checkPositionContract(probe);
    CHECK(state.destroyed == 1);
}


TEST_CASE("physics.backend.failedMeshCookPreservesGeometryAndMaterials") {
    World3D                  world(0.f, 0.f, 0.f, false);
    std::unique_ptr<Body3D>  body(world.newBody("static", 0.f, 0.f, 0.f));
    const std::vector<float> vertices{-1.f, 0.f, -1.f, 1.f, 0.f, -1.f, 1.f, 0.f, 1.f, -1.f, 0.f, 1.f};
    std::unique_ptr<Shape3D> shape(body->newTriangleMeshShape(vertices, {0, 1, 2, 0, 2, 3}));
    shape->setTriangleMeshMaterialIndices({0, 1});
    bool rejected = false;
    try {
        shape->setTriangleMeshData(vertices, {0, 1, 99});
    } catch (const eve::Exception&) {
        rejected = true;
    }
    CHECK(rejected);
    CHECK(shape->getTriangleMeshMaterialCount() == 2);
    CHECK(shape->getTriangleMeshMaterialIndex(1) == 1);
    CHECK(shape->isValid());
    shape->setTriangleMeshData(vertices, {0, 1, 2});
    CHECK(shape->getTriangleMeshMaterialCount() == 1);
    CHECK(shape->getTriangleMeshMaterialIndex(0) == 0);
}
}  // namespace test_physics_rigid_backend
#undef PROBE_UNSUPPORTED
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_rope)
#include "PrivateTestScope.h"
#include <cmath>
#include <limits>
#include <memory>
#include "physics/Body3D.h"
#include "physics/DistanceField3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "physics/rope/Rope.h"
#include "physics/rope/Rope3D.h"
#include "schema/SchemaRegistry.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// physics_rope.cpp
namespace test_physics_rope {
using namespace eve::physics;


TEST_CASE("physics.rope.continuousCollisionStopsAtTriangleMesh") {
    Physics                         physics;
    std::unique_ptr<World3D>        world(physics.newWorld3D(0.f, 0.f, 0.f, false));
    const std::vector<float>        vertices{-4.f, 0.f, -4.f, 4.f, 0.f, -4.f, 4.f, 0.f, 4.f, -4.f, 0.f, 4.f};
    const std::vector<std::int32_t> indices{0, 2, 1, 0, 3, 2};
    REQUIRE(world->newBody("static", 0.f, 0.f, 0.f)->newTriangleMeshShape(vertices, indices) != nullptr);

    Rope3D rope(3, -1.f, 1.f, 0.f, 1.f, 1.f, 0.f);
    rope.setCollideWorld(world.get());
    rope.setRadius(0.1f);
    rope.setGravity(0.f, -3000.f, 0.f);
    rope.update(0.05f);
    CHECK(rope.getParticleY(1) >= 0.099f);
}


TEST_CASE("physics.rope.dynamicRigidBodyReceivesReactionImpulse") {
    Physics                  physics;
    std::unique_ptr<World3D> world(physics.newWorld3D(0.f, 0.f, 0.f, false));
    Body3D*                  body = world->newBody("dynamic", 0.f, 0.f, 0.f);
    REQUIRE(body->newSphereShape(0.5f, 1.f) != nullptr);

    Rope3D rope(3, -0.5f, 1.f, 0.f, 0.5f, 1.f, 0.f);
    rope.setCollideWorld(world.get());
    rope.setRadius(0.1f);
    rope.setParticleMass(0.25f);
    rope.setGravity(0.f, -1000.f, 0.f);
    for (int i = 0; i < 4; ++i) rope.update(0.05f);
    CHECK(body->getLinearVelocityY() < -0.01f);
}
}  // namespace test_physics_rope
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_trajectory_collision)
#include "PrivateTestScope.h"
#include <memory>
#include <string>
#include <unordered_map>
#include "common/AttachmentPoint.h"
#include "common/Identity.h"
#include "common/SubjectRef.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "physics/trajectory/BoneCollider.h"
#include "physics/trajectory/TrajectoryCollision.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// physics_trajectory_collision.cpp
namespace test_physics_trajectory_collision {
namespace {

eve::SubjectRef subject(const char* value) {
    auto parsed = eve::PersistentId::parse(value);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

/** @brief Deterministic attachment map used as a stand-in for AnimPose / Avatar bones. */
class FixedBones final : public eve::IAttachmentPointSource {
public:
    std::unordered_map<std::string, eve::AttachmentPoint> bones;

    [[nodiscard]] eve::Result<eve::AttachmentPoint> sampleAttachmentPoint(
        std::string_view name, eve::AttachmentPoint localOffset = {}) const override {
        const auto found = bones.find(std::string(name));
        if (found == bones.end())
            return eve::Result<eve::AttachmentPoint>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::NotFound, "test attachment bone was not found", std::string(name)));
        // Treat the authored bone as a pure translation (bind-pose aligned), matching
        // Avatar's matrix transform for the identity-rotation case.
        return eve::Result<eve::AttachmentPoint>::success(
            {found->second.x + localOffset.x, found->second.y + localOffset.y, found->second.z + localOffset.z});
    }
};

}  // namespace

TEST_CASE("trajectoryCollision.sphereSweepHitsAndDedupsAgainstWorld3D") {
    auto*                                  physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.f, 0.f, 0.f, false));
    eve::physics::Body3D*                  wall = world->newBody("static", 2.f, 0.f, 0.f);
    wall->newBoxShape(1.f, 2.f, 2.f, 0.f, 0.5f, 0.f);

    const auto attacker = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    FixedBones bones;
    bones.bones["hand.R"] = {-1.f, 0.f, 0.f};

    eve::physics::trajectory::TrajectoryCollisionRuntime runtime(*world);
    eve::physics::trajectory::BoneColliderDefinition     hand;
    hand.colliderId = "hand.R";
    hand.boneName   = "hand.R";
    hand.kind       = eve::physics::trajectory::BoneColliderShapeKind::Sphere;
    hand.radius     = 0.25f;
    REQUIRE(runtime.registerCollider(hand).ok());
    REQUIRE(runtime.bindPoseSource(attacker, bones).ok());
    REQUIRE(runtime.arm(attacker, "hand.R").ok());

    // Arming frame establishes previous==current; no motion yet.
    auto primed = runtime.advance(eve::SimulationTick(1));
    REQUIRE(primed.ok());
    CHECK_EQ(primed.value().hits.size(), 0u);

    // Sweep the hand sphere through the static wall.
    bones.bones["hand.R"] = {3.f, 0.f, 0.f};
    auto hitFrame         = runtime.advance(eve::SimulationTick(2));
    REQUIRE(hitFrame.ok());
    REQUIRE_EQ(hitFrame.value().hits.size(), 1u);
    CHECK_EQ(hitFrame.value().hits.front().colliderId, std::string("hand.R"));
    CHECK_EQ(hitFrame.value().hits.front().bodyId, wall->getId());
    CHECK(hitFrame.value().hits.front().body.isValid());
    CHECK(hitFrame.value().hits.front().fraction >= 0.f);
    CHECK(hitFrame.value().hits.front().fraction <= 1.f);

    // Same contact is de-duplicated until disarm.
    auto again = runtime.advance(eve::SimulationTick(3));
    REQUIRE(again.ok());
    CHECK_EQ(again.value().hits.size(), 0u);

    REQUIRE(runtime.disarm(attacker, "hand.R").ok());
    bones.bones["hand.R"] = {-1.f, 0.f, 0.f};
    REQUIRE(runtime.arm(attacker, "hand.R").ok());
    bones.bones["hand.R"] = {3.f, 0.f, 0.f};
    auto rearmed          = runtime.advance(eve::SimulationTick(4));
    REQUIRE(rearmed.ok());
    CHECK_EQ(rearmed.value().hits.size(), 1u);
}

TEST_CASE("trajectoryCollision.dualSocketCapsuleSweepsWeaponBlade") {
    auto*                                  physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.f, 0.f, 0.f, false));
    eve::physics::Body3D*                  target = world->newBody("static", 0.f, 0.f, 2.f);
    target->newSphereShape(0.5f, 0.f, 0.5f, 0.f);

    const auto attacker = subject("01020304-0506-0708-890a-0b0c0d0e0f10");
    FixedBones bones;
    bones.bones["weapon.guard"] = {0.f, 0.f, -1.f};
    bones.bones["weapon.tip"]   = {0.f, 0.f, -0.2f};

    eve::physics::trajectory::TrajectoryCollisionRuntime runtime(*world);
    eve::physics::trajectory::BoneColliderDefinition     blade;
    blade.colliderId  = "weapon.blade";
    blade.boneName    = "weapon.guard";
    blade.endBoneName = "weapon.tip";
    blade.kind        = eve::physics::trajectory::BoneColliderShapeKind::Capsule;
    blade.radius      = 0.08f;
    REQUIRE(runtime.registerCollider(blade).ok());
    REQUIRE(runtime.bindPoseSource(attacker, bones).ok());
    REQUIRE(runtime.arm(attacker, "weapon.blade").ok());
    REQUIRE(runtime.advance(eve::SimulationTick(1)).ok());

    // Swing the blade forward so the capsule sweeps through the target sphere.
    bones.bones["weapon.guard"] = {0.f, 0.f, 1.5f};
    bones.bones["weapon.tip"]   = {0.f, 0.f, 2.5f};
    auto frame                  = runtime.advance(eve::SimulationTick(2));
    REQUIRE(frame.ok());
    REQUIRE_EQ(frame.value().hits.size(), 1u);
    CHECK_EQ(frame.value().hits.front().bodyId, target->getId());
    CHECK(frame.value().hits.front().shape.isValid());
}

TEST_CASE("trajectoryCollision.singleBoneCapsuleUsesLocalHalfHeight") {
    auto*                                  physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.f, 0.f, 0.f, false));
    eve::physics::Body3D*                  post = world->newBody("static", 0.f, 1.f, 2.f);
    post->newBoxShape(0.5f, 2.f, 0.5f, 0.f, 0.5f, 0.f);

    const auto attacker = subject("21222324-2526-2728-992a-2b2c2d2e2f30");
    FixedBones bones;
    bones.bones["forearm.L"] = {0.f, 1.f, -1.f};

    eve::physics::trajectory::TrajectoryCollisionRuntime runtime(*world);
    eve::physics::trajectory::BoneColliderDefinition     forearm;
    forearm.colliderId = "forearm.L";
    forearm.boneName   = "forearm.L";
    forearm.kind       = eve::physics::trajectory::BoneColliderShapeKind::Capsule;
    forearm.radius     = 0.12f;
    forearm.halfHeight = 0.35f;
    REQUIRE(runtime.registerCollider(forearm).ok());
    REQUIRE(runtime.bindPoseSource(attacker, bones).ok());
    REQUIRE(runtime.arm(attacker, "forearm.L").ok());
    REQUIRE(runtime.advance(eve::SimulationTick(1)).ok());

    bones.bones["forearm.L"] = {0.f, 1.f, 2.f};
    auto frame               = runtime.advance(eve::SimulationTick(2));
    REQUIRE(frame.ok());
    REQUIRE_EQ(frame.value().hits.size(), 1u);
    CHECK_EQ(frame.value().hits.front().colliderId, std::string("forearm.L"));
}

TEST_CASE("trajectoryCollision.rejectsInvalidCatalogAndSurvivesWorldDestroy") {
    eve::physics::trajectory::BoneColliderDefinition bad;
    bad.colliderId = "bad";
    bad.boneName   = "bone";
    bad.kind       = eve::physics::trajectory::BoneColliderShapeKind::Sphere;
    bad.radius     = -1.f;
    CHECK(!bad.validate().ok());

    auto*                                                physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D>               world(physics->newWorld3D(0.f, 0.f, 0.f, false));
    eve::physics::trajectory::TrajectoryCollisionRuntime runtime(*world);
    eve::physics::trajectory::BoneColliderDefinition     hand;
    hand.colliderId = "hand";
    hand.boneName   = "hand";
    hand.kind       = eve::physics::trajectory::BoneColliderShapeKind::Sphere;
    hand.radius     = 0.2f;
    REQUIRE(runtime.registerCollider(hand).ok());

    const auto attacker = subject("31323334-3536-3738-993a-3b3c3d3e3f40");
    FixedBones bones;
    bones.bones["hand"] = {0.f, 0.f, 0.f};
    REQUIRE(runtime.bindPoseSource(attacker, bones).ok());
    REQUIRE(runtime.arm(attacker, "hand").ok());

    world.reset();
    auto stale = runtime.advance(eve::SimulationTick(1));
    REQUIRE(!stale.ok());
    REQUIRE(stale.error() != nullptr);
    CHECK_EQ(stale.error()->code(), eve::DiagnosticCode::StaleHandle);
}

TEST_CASE("trajectoryCollision.filterBitsIgnoreNonMatchingCategory") {
    auto*                                  physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.f, 0.f, 0.f, false));
    eve::physics::Body3D*                  wall  = world->newBody("static", 2.f, 0.f, 0.f);
    eve::physics::Shape3D*                 shape = wall->newBoxShape(1.f, 2.f, 2.f, 0.f, 0.5f, 0.f);
    shape->setFilterBits(0x2u, 0xFFFFFFFFu);

    const auto attacker = subject("41424344-4546-4748-994a-4b4c4d4e4f50");
    FixedBones bones;
    bones.bones["fist"] = {-1.f, 0.f, 0.f};

    eve::physics::trajectory::TrajectoryCollisionRuntime runtime(*world);
    eve::physics::trajectory::BoneColliderDefinition     fist;
    fist.colliderId   = "fist";
    fist.boneName     = "fist";
    fist.kind         = eve::physics::trajectory::BoneColliderShapeKind::Sphere;
    fist.radius       = 0.25f;
    fist.categoryBits = 0x1u;
    fist.maskBits     = 0x1u;  // does not match wall category 0x2
    REQUIRE(runtime.registerCollider(fist).ok());
    REQUIRE(runtime.bindPoseSource(attacker, bones).ok());
    REQUIRE(runtime.arm(attacker, "fist").ok());
    REQUIRE(runtime.advance(eve::SimulationTick(1)).ok());

    bones.bones["fist"] = {3.f, 0.f, 0.f};
    auto miss           = runtime.advance(eve::SimulationTick(2));
    REQUIRE(miss.ok());
    CHECK_EQ(miss.value().hits.size(), 0u);

    fist.maskBits = 0x2u;
    REQUIRE(runtime.registerCollider(fist).ok());
    REQUIRE(runtime.disarm(attacker, "fist").ok());
    bones.bones["fist"] = {-1.f, 0.f, 0.f};
    REQUIRE(runtime.arm(attacker, "fist").ok());
    REQUIRE(runtime.advance(eve::SimulationTick(3)).ok());
    bones.bones["fist"] = {3.f, 0.f, 0.f};
    auto hit            = runtime.advance(eve::SimulationTick(4));
    REQUIRE(hit.ok());
    CHECK_EQ(hit.value().hits.size(), 1u);
}
}  // namespace test_physics_trajectory_collision
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_world_snapshot_topology)
#include "PrivateTestScope.h"
#include <cstdint>
#include <string_view>
#include <vector>
#include "common/Snapshot.h"
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/Shape3D.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// physics_world_snapshot_topology.cpp
namespace test_physics_world_snapshot_topology {
// physics_world_snapshot_topology.cpp
namespace {

eve::SnapshotHashProvider topologyHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        eve::ContentId::Bytes bytes{};
        std::uint64_t         hash = 14695981039346656037ull;
        for (unsigned char byte : input) hash = (hash ^ byte) * 1099511628211ull;
        for (std::size_t index = 0; index < bytes.size(); ++index)
            bytes[index] = static_cast<std::uint8_t>(hash >> ((index % 8) * 8));
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

}  // namespace

TEST_CASE("physics.snapshot_v2_persists_2d_fixture_geometry_and_rejects_unknown_fields_atomically") {
    const auto          hash = topologyHash();
    eve::physics::World world(0.f, 9.f, false, 32.f);
    auto*               body = world.newBody("dynamic", 2.f, 3.f);
    REQUIRE(body->newRectangleFixtureAt(4.f, 6.f, 1.f, -2.f) != nullptr);
    const auto handle   = body->runtimeHandle();
    auto       captured = world.snapshot(hash);
    REQUIRE(captured.ok());
    const eve::Value* bodies = captured.value().payload.find("bodies");
    REQUIRE(bodies != nullptr);
    REQUIRE_EQ(bodies->arraySize(), std::size_t{1});
    const eve::Value* fixtures = bodies->at(0).find("fixtures");
    REQUIRE(fixtures != nullptr);
    CHECK_EQ(fixtures->arraySize(), std::size_t{1});

    eve::Value invalidPayload = captured.value().payload;
    invalidPayload.set("unknownFutureField", eve::Value(true));
    auto invalid = eve::makeSnapshotEnvelope(
        captured.value().type, captured.value().schema, captured.value().schemaVersion, captured.value().instanceId,
        captured.value().revision, captured.value().tick, std::move(invalidPayload), hash);
    REQUIRE(invalid.ok());
    body->setPosition(11.f, 12.f);
    auto rejected = world.restore(invalid.value(), hash);
    CHECK(!rejected.ok());
    CHECK(world.findBody(handle) == body);
    CHECK_EQ(body->getX(), 11.f);
    CHECK_EQ(world.getGravityY(), 9.f);
}

TEST_CASE("physics.snapshot_v2_rebuilds_mesh_heightfield_and_joint_sources_from_detached_state") {
    const auto                      hash = topologyHash();
    eve::physics::World3D           world(0.f, -9.f, 0.f, false);
    auto*                           meshBody    = world.newBody("static", 0.f, 0.f, 0.f);
    auto*                           dynamicBody = world.newBody("dynamic", 0.f, 2.f, 0.f);
    const std::vector<float>        vertices{-2.f, 0.f, -2.f, 2.f, 0.f, -2.f, 0.f, 0.f, 2.f};
    const std::vector<std::int32_t> indices{0, 1, 2};
    auto*                           mesh = meshBody->newTriangleMeshShape(vertices, indices);
    REQUIRE(mesh != nullptr);
    const std::vector<float> heights(9, 0.f);
    REQUIRE(meshBody->newHeightFieldShape(3, 3, 1.f, 1.f, heights, -1.f, 1.f) != nullptr);
    REQUIRE(world.newDistanceJoint(meshBody, dynamicBody, 0.f, 0.f, 0.f, 0.f, 2.f, 0.f, 2.f) != nullptr);

    auto captured = world.snapshot(hash);
    REQUIRE(captured.ok());
    const eve::Value* shapes = captured.value().payload.find("shapes");
    const eve::Value* joints = captured.value().payload.find("joints");
    REQUIRE(shapes != nullptr);
    REQUIRE(joints != nullptr);
    CHECK_EQ(shapes->arraySize(), std::size_t{2});
    CHECK_EQ(joints->arraySize(), std::size_t{1});

    const std::vector<float> changed{-3.f, 0.f, -2.f, 3.f, 0.f, -2.f, 0.f, 0.f, 3.f};
    mesh->setTriangleMeshData(changed, indices);
    dynamicBody->setPosition(0.f, 8.f, 0.f);
    const auto handle   = dynamicBody->runtimeHandle();
    auto       restored = world.restore(captured.value(), hash);
    REQUIRE(restored.ok(), "{}", restored.status().describe());
    CHECK(world.findBody(handle) == nullptr);
    CHECK(!dynamicBody->isValid());
    CHECK_EQ(world.getGravityY(), -9.f);
    auto roundTrip = world.snapshot(hash);
    REQUIRE(roundTrip.ok());
    CHECK_EQ(roundTrip.value().payload.find("shapes")->arraySize(), std::size_t{2});
    CHECK_EQ(roundTrip.value().payload.find("joints")->arraySize(), std::size_t{1});
}

TEST_CASE("physics.snapshot_restored_world_keeps_live_step_driver") {
    const auto            hash = topologyHash();
    eve::physics::World3D world(0.f, -9.f, 0.f, false);
    auto*                 original = world.newBody("dynamic", 0.f, 10.f, 0.f);
    // Give the driver probe a physical mass; an unshaped zero-mass body does not fall.
    std::unique_ptr<eve::physics::Shape3D> shape(original->newBoxShape(1, 1, 1));
    const int             id       = original->getId();
    auto                  captured = world.snapshot(hash);
    REQUIRE(captured.ok());
    REQUIRE(world.restore(captured.value(), hash).ok());
    auto* restored = world.findBodyById(id);
    REQUIRE(restored != nullptr);
    CHECK(!original->isValid());
    world.update(0.02f);
    CHECK(restored->getY() < 10.f);
    CHECK(world.rigidBackendKind() == eve::physics::RigidBackend3D::Box3D);
}

namespace {

eve::PersistentId worldId(std::string_view text) {
    const auto parsed = eve::PersistentId::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

eve::SnapshotHashProvider testHashProvider() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        std::uint64_t left = 14695981039346656037ull, right = 1099511628211ull;
        for (const unsigned char byte : input) {
            left  = (left ^ byte) * 1099511628211ull;
            right = (right ^ (static_cast<std::uint64_t>(byte) + 0x9e3779b97f4a7c15ull)) * 14029467366897019727ull;
        }
        eve::ContentId::Bytes bytes{};
        for (int index = 0; index < 8; ++index) {
            bytes[static_cast<std::size_t>(index)]     = static_cast<std::uint8_t>(left >> (56 - index * 8));
            bytes[static_cast<std::size_t>(index + 8)] = static_cast<std::uint8_t>(right >> (56 - index * 8));
        }
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

}  // namespace

TEST_CASE("physics.snapshot_v2_has_stable_identity_and_rejects_foreign_world_atomically") {
    const auto          idA  = worldId("01234567-89ab-4cde-8fab-0123456789ab");
    const auto          idB  = worldId("11234567-89ab-4cde-8fab-0123456789ab");
    const auto          hash = testHashProvider();
    eve::physics::World source(0.f, 10.f, false, 30.f, idA);
    eve::physics::World foreign(0.f, 20.f, false, 30.f, idB);
    source.newBody("dynamic", 3.f, 4.f);
    foreign.newBody("dynamic", 8.f, 9.f);

    auto snapshot = source.snapshot(hash);
    REQUIRE(snapshot.ok());
    CHECK(!snapshot.value().instanceId.isNil());
    CHECK(snapshot.value().instanceId == idA);
    CHECK_EQ(snapshot.value().schemaVersion.value(), std::uint64_t{3});
    const float beforeGravity = foreign.getGravityY();
    auto        rejected      = foreign.restore(snapshot.value(), hash);
    CHECK(!rejected.ok());
    CHECK_EQ(static_cast<int>(rejected.error()->code()), static_cast<int>(eve::DiagnosticCode::Conflict));
    CHECK_EQ(foreign.getGravityY(), beforeGravity);
}

TEST_CASE("physics.snapshot_v2_explicit_identity_survives_world_recreation") {
    const auto          id   = worldId("51234567-89ab-4cde-8fab-0123456789ab");
    const auto          hash = testHashProvider();
    eve::physics::World source(0.f, 10.f, false, 30.f, id);
    eve::physics::World replacement(0.f, 20.f, false, 30.f, id);
    source.newBody("dynamic", 3.f, 4.f);
    auto* replacementBody = replacement.newBody("dynamic", 8.f, 9.f);

    auto snapshot = source.snapshot(hash);
    REQUIRE(snapshot.ok());
    REQUIRE(replacement.restore(snapshot.value(), hash).ok());
    CHECK_EQ(replacement.persistentId(), id);
    CHECK(!replacementBody->isValid());
    auto roundTrip = replacement.snapshot(hash);
    REQUIRE(roundTrip.ok());
    CHECK(roundTrip.value().payload == snapshot.value().payload);
}

TEST_CASE("physics.snapshot_v2_auto_identity_is_non_nil_and_world_local") {
    const auto          hash = testHashProvider();
    eve::physics::World first(0.f, 10.f, false, 30.f);
    eve::physics::World second(0.f, 10.f, false, 30.f);
    first.newBody("dynamic", 0.f, 0.f);
    second.newBody("dynamic", 0.f, 0.f);

    auto firstSnapshot  = first.snapshot(hash);
    auto secondSnapshot = second.snapshot(hash);
    REQUIRE(firstSnapshot.ok());
    REQUIRE(secondSnapshot.ok());
    CHECK(!firstSnapshot.value().instanceId.isNil());
    CHECK(!secondSnapshot.value().instanceId.isNil());
    CHECK(firstSnapshot.value().instanceId != secondSnapshot.value().instanceId);
}

TEST_CASE("physics.snapshot_v2_success_invalidates_2d_body_handles") {
    const auto          hash = testHashProvider();
    eve::physics::World world(0.f, 10.f, false, 30.f, worldId("21234567-89ab-4cde-8fab-0123456789ab"));
    auto*               body      = world.newBody("dynamic", 3.f, 4.f);
    const auto          oldHandle = body->runtimeHandle();
    auto                snapshot  = world.snapshot(hash);
    REQUIRE(snapshot.ok());
    body->setPosition(30.f, 40.f);
    auto restored = world.restore(snapshot.value(), hash);
    REQUIRE(restored.ok(), "{}", restored.status().describe());
    CHECK(world.findBody(oldHandle) == nullptr);
    CHECK(!body->isValid());
    CHECK(world.findBody(body->runtimeHandle()) == nullptr);
    auto roundTrip = world.snapshot(hash);
    REQUIRE(roundTrip.ok());
    CHECK(roundTrip.value().payload == snapshot.value().payload);
}

TEST_CASE("physics.snapshot_v2_success_invalidates_3d_body_shape_and_joint_handles") {
    const auto            hash = testHashProvider();
    eve::physics::World3D world(0.f, -10.f, 0.f, false, worldId("31234567-89ab-4cde-8fab-0123456789ab"));
    auto*                 bodyA = world.newBody("dynamic", 0.f, 0.f, 0.f);
    auto*                 bodyB = world.newBody("dynamic", 2.f, 0.f, 0.f);
    auto*                 shape = bodyA->newBoxShape(1.f, 1.f, 1.f);
    auto*                 joint = world.newDistanceJoint(bodyA, bodyB, 0.f, 0.f, 0.f, 2.f, 0.f, 0.f, 2.f);
    REQUIRE(shape != nullptr);
    REQUIRE(joint != nullptr);
    const auto oldBody  = bodyA->runtimeHandle();
    const auto oldShape = shape->runtimeHandle();
    const auto oldJoint = joint->runtimeHandle();
    auto       snapshot = world.snapshot(hash);
    REQUIRE(snapshot.ok());
    REQUIRE(world.restore(snapshot.value(), hash).ok());
    CHECK(world.findBody(oldBody) == nullptr);
    CHECK(world.findShape(oldShape) == nullptr);
    CHECK(world.findJoint(oldJoint) == nullptr);
    CHECK(!bodyA->isValid());
    CHECK(!bodyB->isValid());
    CHECK(!shape->isValid());
    CHECK(!joint->isValid());
    CHECK(world.findBody(bodyA->runtimeHandle()) == nullptr);
    CHECK(world.findShape(shape->runtimeHandle()) == nullptr);
    CHECK(world.findJoint(joint->runtimeHandle()) == nullptr);
    auto roundTrip = world.snapshot(hash);
    REQUIRE(roundTrip.ok());
    CHECK(roundTrip.value().payload == snapshot.value().payload);
}

TEST_CASE("physics.snapshot_v2_rejects_foreign_3d_world_without_invalidating_handles") {
    const auto            hash = testHashProvider();
    eve::physics::World3D source(0.f, -10.f, 0.f, false, worldId("61234567-89ab-4cde-8fab-0123456789ab"));
    eve::physics::World3D foreign(0.f, -20.f, 0.f, false, worldId("71234567-89ab-4cde-8fab-0123456789ab"));
    source.newBody("dynamic", 0.f, 1.f, 0.f);
    auto*      body   = foreign.newBody("dynamic", 0.f, 9.f, 0.f);
    const auto handle = body->runtimeHandle();

    auto snapshot = source.snapshot(hash);
    REQUIRE(snapshot.ok());
    auto rejected = foreign.restore(snapshot.value(), hash);
    CHECK(!rejected.ok());
    CHECK_EQ(static_cast<int>(rejected.error()->code()), static_cast<int>(eve::DiagnosticCode::Conflict));
    CHECK(foreign.findBody(handle) == body);
    CHECK_EQ(body->getY(), 9.f);
    CHECK_EQ(foreign.getGravityY(), -20.f);
}

TEST_CASE("physics.snapshot_v1_is_outside_2d_migration_window") {
    const auto          hash = testHashProvider();
    eve::physics::World world(0.f, 10.f, false, 30.f, worldId("41234567-89ab-4cde-8fab-0123456789ab"));
    auto*               body    = world.newBody("dynamic", 1.f, 2.f);
    auto                current = world.snapshot(hash);
    REQUIRE(current.ok());
    auto legacy = eve::makeSnapshotEnvelope(current.value().type, current.value().schema, eve::SchemaVersion(1),
                                            eve::PersistentId::nil(), current.value().revision, current.value().tick,
                                            current.value().payload, hash);
    REQUIRE(legacy.ok());
    const auto handle   = body->runtimeHandle();
    auto       restored = world.restore(legacy.value(), hash);
    REQUIRE(!restored.ok());
    CHECK(restored.error()->code() == eve::DiagnosticCode::UnknownVersion);
    CHECK(world.findBody(handle) == body);
    CHECK(body->isValid());
}

TEST_CASE("physics.snapshot_v2_roundtrips_closed_chain_geometry") {
    const auto          hash = topologyHash();
    eve::physics::World world(0.f, 0.f, false, 32.f);
    auto*               body = world.newBody("static", 2.f, 3.f);
    REQUIRE(body->newChainFixture({0.f, 0.f, 64.f, 0.f, 64.f, 64.f, 0.f, 64.f}, true) != nullptr);
    auto captured = world.snapshot(hash);
    REQUIRE(captured.ok());
    const auto handle   = body->runtimeHandle();
    auto       restored = world.restore(captured.value(), hash);
    REQUIRE(restored.ok(), "{}", restored.status().describe());
    CHECK(world.findBody(handle) == nullptr);
    CHECK(!body->isValid());
    auto roundTrip = world.snapshot(hash);
    REQUIRE(roundTrip.ok());
    CHECK(roundTrip.value().payload == captured.value().payload);
}

}  // namespace test_physics_world_snapshot_topology
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_pixelworld_physics)
#include "PrivateTestScope.h"
#include <cmath>
#include "physics/Body.h"
#include "physics/World.h"
#include "pixelworld/PixelWorld.h"
#include "pixelworld/physics/PixelWorldPhysics.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// pixelworld_physics.cpp
namespace test_pixelworld_physics {
using namespace eve::pixelworld;
using namespace eve::pixelworld_physics;

namespace {

PixelFragment detachL(PixelWorld& world) {
    world.setMaterial(0, 0, "wood");
    world.setMaterial(1, 0, "wood");
    world.setMaterial(0, 1, "wood");
    auto fragments = world.extractUnsupportedFragments({-1, -1, 2, 2}, 10).expect("detach L fragment");
    REQUIRE_EQ(fragments.size(), std::size_t(1));
    return std::move(fragments.front());
}

}  // namespace

TEST_CASE("pixelworld_physics.fragment_decomposition_and_sleep_settlement_are_transactional") {
    PixelWorld          pixels(501);
    PixelFragment       fragment = detachL(pixels);
    eve::physics::World physics(0.f, 100.f, true, 64.f);
    auto adapter = PixelFragmentBody::create(physics, std::move(fragment)).expect("create fragment body");

    REQUIRE_EQ(adapter->collisionRects().size(), std::size_t(2));
    auto body = adapter->physicsLink().resolve(physics).expect("resolve fragment body");
    body->setPosition(20.f, 20.f);
    body->setAngle(1.57079632679f);
    pixels.setMaterial(19, 19, "stone");
    body->setAwake(false);

    auto conflict = adapter->settleIfSleeping(physics, pixels);
    CHECK(!conflict.ok());
    CHECK(body->isValid());
    CHECK(!adapter->isRasterized());

    body->setPosition(40.f, 40.f);
    body->setAwake(false);
    const auto settled = adapter->settleIfSleeping(physics, pixels).expect("settle fragment");
    CHECK_EQ(int(settled.disposition), int(FragmentSettleDisposition::Rasterized));
    CHECK_EQ(settled.cellsRasterized, std::uint32_t(3));
    CHECK_EQ(settled.quarterTurns, 1);
    CHECK(adapter->isRasterized());
    CHECK(!body->isValid());
    CHECK_EQ(pixels.getMaterial(39, 39), int(MaterialId::Wood));
    CHECK_EQ(pixels.getMaterial(40, 39), int(MaterialId::Wood));
    CHECK_EQ(pixels.getMaterial(40, 40), int(MaterialId::Wood));
}

TEST_CASE("pixelworld_physics.both_destruction_orders_and_restore_staleness_are_safe") {
    PixelWorld          pixels(502);
    eve::physics::World firstPhysics(0.f, 0.f, true, 64.f);
    auto                first = PixelFragmentBody::create(firstPhysics, detachL(pixels)).expect("first fragment body");
    first->releasePhysics(firstPhysics).expect("body first release");
    CHECK(firstPhysics.isValid());

    pixels.clear();
    auto       secondFragment = detachL(pixels);
    const auto checkpoint     = pixels.saveSnapshot().expect("pixel checkpoint");
    auto second     = PixelFragmentBody::create(firstPhysics, std::move(secondFragment)).expect("second fragment body");
    auto secondBody = second->physicsLink().resolve(firstPhysics).expect("second body");
    secondBody->setAwake(false);
    pixels.restoreSnapshot(checkpoint).expect("restore invalidates pixel link");
    CHECK(!second->settleIfSleeping(firstPhysics, pixels).ok());
    CHECK(secondBody->isValid());

    auto thirdPhysics = std::make_unique<eve::physics::World>(0.f, 0.f, true, 64.f);
    pixels.clear();
    auto third = PixelFragmentBody::create(*thirdPhysics, detachL(pixels)).expect("third fragment body");
    thirdPhysics.reset();
    eve::physics::World foreignPhysics(0.f, 0.f, true, 64.f);
    CHECK(!third->releasePhysics(foreignPhysics).ok());
}

TEST_CASE("pixelworld_physics.terrain_cache_rebuilds_dirty_chunks_and_rolls_back_budget_failure") {
    PixelWorld pixels(503);
    for (int x = 0; x < 70; ++x) pixels.setMaterial(x, 20, "stone");
    pixels.setMaterial(5, 5, "water");
    eve::physics::World        physics(0.f, 100.f, true, 64.f);
    PixelTerrainCollisionCache cache;

    const auto initial = cache.sync(physics, pixels).expect("initial terrain collision sync");
    CHECK_EQ(initial.chunksRebuilt, std::uint32_t(2));
    CHECK_EQ(initial.fixturesCreated, std::uint32_t(4));
    CHECK_EQ(cache.bodyCount(), std::size_t(2));
    CHECK(physics.rayCast(10.f, 10.f, 10.f, 30.f) >= 0);
    CHECK(std::abs(physics.getRayHitY() - 20.f) < 0.01f);
    const auto represented = cache.sourceRevision();

    pixels.setMaterial(2, 19, "stone");
    const auto incremental = cache.sync(physics, pixels).expect("dirty chunk collision sync");
    CHECK_EQ(incremental.chunksRebuilt, std::uint32_t(2));
    CHECK_EQ(incremental.bodiesRemoved, std::uint32_t(2));
    CHECK(cache.sourceRevision() > represented);

    const auto beforeFailureRevision = cache.sourceRevision();
    const auto beforeFailureBodies   = cache.bodyCount();
    for (int x = 0; x < 16; x += 2) pixels.setMaterial(x, 4, "stone");
    auto rejected = cache.sync(physics, pixels, 1);
    CHECK(!rejected.ok());
    CHECK_EQ(cache.sourceRevision(), beforeFailureRevision);
    CHECK_EQ(cache.bodyCount(), beforeFailureBodies);

    cache.clearPhysics(physics).expect("clear terrain collision cache");
    CHECK_EQ(cache.bodyCount(), std::size_t(0));
    CHECK_EQ(cache.sourceRevision(), std::uint64_t(0));
}

TEST_CASE("pixelworld_physics.terrain_cache_rejects_live_world_rebind_without_orphaning_bodies") {
    PixelWorld pixels(506);
    for (int x = 0; x < 16; ++x) pixels.setMaterial(x, 12, "stone");
    eve::physics::World        first(0.f, 100.f, true, 64.f);
    eve::physics::World        second(0.f, 100.f, true, 64.f);
    PixelTerrainCollisionCache cache;

    cache.sync(first, pixels).expect("initial terrain projection");
    REQUIRE(first.rayCast(8.f, 0.f, 8.f, 20.f) >= 0);
    auto rejected = cache.sync(second, pixels);
    CHECK(!rejected.ok());
    CHECK_EQ(static_cast<int>(rejected.error()->code()), static_cast<int>(eve::DiagnosticCode::Conflict));
    CHECK(first.rayCast(8.f, 0.f, 8.f, 20.f) >= 0);
    CHECK(second.rayCast(8.f, 0.f, 8.f, 20.f) < 0);
    CHECK_EQ(cache.bodyCount(), std::size_t(1));

    cache.clearPhysics(first).expect("clear original projection before rebind");
    cache.sync(second, pixels).expect("rebind after explicit clear");
    CHECK(first.rayCast(8.f, 0.f, 8.f, 20.f) < 0);
    CHECK(second.rayCast(8.f, 0.f, 8.f, 20.f) >= 0);
    cache.clearPhysics(second).expect("clear replacement projection");
}

TEST_CASE("pixelworld_physics.terrain_contours_are_simplified_and_suppress_chunk_seams") {
    PixelWorld pixels(504);
    for (int y = 10; y < 12; ++y)
        for (int x = 10; x < 12; ++x) pixels.setMaterial(x, y, "stone");
    auto simple = extractTerrainContours(pixels, 0, 0).expect("simple terrain contour");
    REQUIRE_EQ(simple.size(), std::size_t(1));
    CHECK(simple.front().loop);
    CHECK_EQ(simple.front().vertices.size(), std::size_t(8));

    pixels.setMaterial(63, 20, "stone");
    pixels.setMaterial(64, 20, "stone");
    const auto left = extractTerrainContours(pixels, 0, 0).expect("left seam contour");
    for (const auto& contour : left) {
        const std::size_t count = contour.vertices.size() / 2;
        for (std::size_t index = 0; index + 1 < count; ++index) {
            const float x1 = contour.vertices[index * 2];
            const float y1 = contour.vertices[index * 2 + 1];
            const float x2 = contour.vertices[(index + 1) * 2];
            const float y2 = contour.vertices[(index + 1) * 2 + 1];
            CHECK(!(x1 == 64.f && x2 == 64.f && y1 >= 20.f && y2 >= 20.f));
        }
    }
    CHECK(!extractTerrainContours(pixels, 0, 0, 3).ok());
}

TEST_CASE("pixelworld_physics.character_probe_and_sweep_query_authoritative_material") {
    PixelWorld pixels(505);
    pixels.setMaterial(10, 10, "stone");
    pixels.setMaterial(8, 10, "water");

    const auto probe = probeTerrainCircle(pixels, 9.75f, 10.5f, 0.5f).expect("character terrain probe");
    REQUIRE(probe.hit);
    CHECK_EQ(int(probe.material), int(MaterialId::Stone));
    CHECK_EQ(probe.cellX, 10);
    CHECK(std::abs(probe.depth - 0.25f) < 0.0001f);
    CHECK_EQ(probe.normalX, -1.f);
    CHECK_EQ(probe.normalY, 0.f);

    const auto sweep = sweepTerrainCircle(pixels, 5.5f, 10.5f, 15.5f, 10.5f, 0.25f).expect("character terrain sweep");
    REQUIRE(sweep.hit);
    CHECK_EQ(int(sweep.material), int(MaterialId::Stone));
    CHECK_EQ(sweep.cellX, 10);
    CHECK(std::abs(sweep.fraction - 0.425f) < 0.0001f);
    CHECK_EQ(sweep.normalX, -1.f);

    const auto corner = sweepTerrainCircle(pixels, 8.f, 8.f, 10.f, 10.f, 0.5f).expect("rounded corner sweep");
    REQUIRE(corner.hit);
    CHECK(corner.normalX < -0.7f);
    CHECK(corner.normalY < -0.7f);
    const auto cornerMiss = sweepTerrainCircle(pixels, 8.f, 8.f, 9.6f, 9.6f, 0.5f).expect("rounded corner miss");
    CHECK(!cornerMiss.hit);

    const auto miss = sweepTerrainCircle(pixels, 1.f, 1.f, 2.f, 1.f, 0.25f).expect("empty terrain sweep");
    CHECK(!miss.hit);
    CHECK_EQ(miss.fraction, 1.f);
    CHECK(!sweepTerrainCircle(pixels, 0.f, 0.f, 100.f, 100.f, 1.f, 4).ok());
}
}  // namespace test_pixelworld_physics
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_targeting)
#include "PrivateTestScope.h"
#include <type_traits>
#include <utility>
#include "common/Capability.h"
#include "common/Identity.h"
#include "physics/TargetingLineOfSightAdapter.h"
#include "physics/World3D.h"
#include "sensing/Targeting.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// targeting.cpp
namespace test_targeting {
namespace {

using eve::LogicalId;
using eve::PersistentId;
using eve::physics::TargetingLineOfSightAdapter;
using eve::physics::World3D;
using eve::sensing::CoordinateSpace;
using eve::sensing::CountPolicy;
using eve::sensing::GridPoint;
using eve::sensing::ILineOfSightQuery;
using eve::sensing::ISensingCandidateProvider;
using eve::sensing::LineOfSightResult;
using eve::sensing::SensingCandidateProvider;
using eve::sensing::SensingWorld;
using eve::sensing::SensingWorldCandidateProvider;
using eve::sensing::SubjectRef;
using eve::sensing::TargetCandidate;
using eve::sensing::TargetDomain;
using eve::sensing::TargetingQuery;
using eve::sensing::TargetingResolver;
using eve::sensing::TargetingSpec;
using eve::sensing::TargetLocation;
using eve::sensing::WorldArea;
using eve::sensing::WorldPoint;

[[maybe_unused]] SubjectRef subject(const char* text) {
    auto id = PersistentId::parse(text);
    REQUIRE(id.has_value());
    return SubjectRef::fromPersistentId(*id);
}

[[maybe_unused]] WorldPoint world2D(float x, float y) {
    auto point = WorldPoint::world2D(x, y);
    return std::move(point).expect("test World2D point must be valid");
}

[[maybe_unused]] WorldPoint world3D(float x, float y, float z) {
    auto point = WorldPoint::world3D(x, y, z);
    return std::move(point).expect("test World3D point must be valid");
}

struct ResetCapabilities {
    ResetCapabilities() { eve::cap::detail::clearAllRaw(); }
    ~ResetCapabilities() { eve::cap::detail::clearAllRaw(); }
};

class VisibleLineOfSight final : public ILineOfSightQuery {
public:
    explicit VisibleLineOfSight(bool visible) : visible_(visible) {}

    eve::Result<LineOfSightResult> query(const TargetLocation& from, const TargetLocation& to) const override {
        if (from.index() != to.index())
            return eve::Result<LineOfSightResult>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "test LOS received mixed coordinate spaces"));
        if (from.index() == 1)
            return eve::Result<LineOfSightResult>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "test LOS does not support grid coordinates"));
        return eve::Result<LineOfSightResult>::success(LineOfSightResult{visible_, std::nullopt});
    }

private:
    bool visible_ = true;
};

}  // namespace


TEST_CASE("targeting.physicsAdapterRequiresOneActiveWorld") {
    TargetingLineOfSightAdapter adapter;
    const auto                  from = world3D(0.f, 0.f, 0.f);
    const auto                  to   = world3D(1.f, 0.f, 0.f);

    auto noWorld = adapter.query(TargetLocation{from}, TargetLocation{to});
    CHECK(!noWorld.ok());
    CHECK_EQ(noWorld.code(), eve::StatusCode::Unsupported);

    World3D first(0.f, 0.f, 0.f, false);
    auto    firstRegistration = adapter.addWorld(&first);
    REQUIRE(firstRegistration.ok());
    auto duplicateRegistration = adapter.addWorld(&first);
    REQUIRE(duplicateRegistration.ok());
    CHECK_EQ(duplicateRegistration.code(), eve::StatusCode::NoOp);

    World3D second(0.f, 0.f, 0.f, false);
    auto    conflictingRegistration = adapter.addWorld(&second);
    CHECK(!conflictingRegistration.ok());
    CHECK_EQ(conflictingRegistration.code(), eve::StatusCode::Conflict);

    auto present = adapter.query(TargetLocation{from}, TargetLocation{to});
    REQUIRE(present.ok());
    CHECK(present.value().visible);

    first.destroy();
    auto invalidWorld = adapter.query(TargetLocation{from}, TargetLocation{to});
    CHECK(!invalidWorld.ok());
    CHECK_EQ(invalidWorld.code(), eve::StatusCode::Unsupported);

    auto removed = adapter.removeWorld(&first);
    REQUIRE(removed.ok());
    auto afterRemove = adapter.query(TargetLocation{from}, TargetLocation{to});
    CHECK(!afterRemove.ok());
    CHECK_EQ(afterRemove.code(), eve::StatusCode::Unsupported);
}
}  // namespace test_targeting
#endif
