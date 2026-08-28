#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "climbing/ClimbingServices.h"
#include "common/Capability.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/World3D.h"

#include <cmath>
#include <memory>
#include <vector>

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

eve::climbing::ClimbingProfileDefinition serviceProfile() {
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

eve::climbing::ClimbingPose pose(const ServiceWorld& fixture) {
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
