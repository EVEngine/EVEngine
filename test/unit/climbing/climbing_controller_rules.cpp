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
