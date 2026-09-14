#include "climbing/ClimbingControl.h"
#include "common/Capability.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <memory>

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
    eve::physics::Body3D* obstacle = nullptr;
};

eve::climbing::ClimbingActionDefinition mantle() {
    eve::climbing::ClimbingActionDefinition result;
    result.id = "climb:gameplay-mantle";
    result.minHeight = 0.4f;
    result.maxHeight = 1.2f;
    result.duration = eve::Duration::fromNanoseconds(600000000);
    result.landingForward = 0.6f;
    result.apexHeight = 0.7f;
    return result;
}

eve::GameplayCommand command(const char* commandId, const char* actionId,
                             eve::SubjectRef character,
                             const eve::GameplayObservation& observation) {
    eve::GameplayCommand result;
    result.id = commandId;
    result.action = action(actionId);
    result.subject = character;
    result.observedTick = observation.tick;
    result.expectedRevision = observation.revision;
    result.parameters = eve::Value(eve::Value::Object{});
    return result;
}

}  // namespace

TEST_CASE("gameplay.control.climbingReprobesAuthorityForPlayerAndAutomation") {
    ClimbingWorld fixture;
    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.upsertAction(mantle()).ok());
    const eve::climbing::ClimbingPose pose{{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1};
    const auto instance = subject("00000000-0000-7000-8000-000000000901");
    const auto character = subject("00000000-0000-7000-8000-000000000902");
    eve::climbing::ClimbingControl control(instance, character, runtime, *fixture.world, pose);
    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {character}};
    eve::GameplaySession automation{"automation", eve::GameplayAccess::TestDriver, {character}};

    bool discovered = false;
    eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* provider) {
        if (provider == &control && provider->gameplayDomain() == "climbing") discovered = true;
    });
    CHECK(discovered);
    auto playerActions = control.availableGameplayActions(player, instance, character);
    auto automationActions = control.availableGameplayActions(automation, instance, character);
    REQUIRE(playerActions.ok());
    REQUIRE(automationActions.ok());
    REQUIRE_EQ(playerActions.value().size(), std::size_t{1});
    CHECK_EQ(playerActions.value().front().id, automationActions.value().front().id);
    CHECK_EQ(playerActions.value().front().id, action("climbing:begin-best"));

    auto observed = control.observeGameplay(player, instance);
    REQUIRE(observed.ok());
    const auto before = std::move(observed).takeValue();
    auto begun = control.submitGameplay(
        player, instance, command("climb-begin-1", "climbing:begin-best", character, before));
    REQUIRE(begun.ok());
    CHECK(!runtime.executionId().isZero());
    auto events = control.gameplayEvents(player, instance, 0);
    REQUIRE(events.ok());
    REQUIRE_EQ(events.value().size(), std::size_t{1});
    CHECK_EQ(events.value().front().type, std::string("climbing.started"));
    CHECK_EQ(events.value().front().causationCommandId, std::string("climb-begin-1"));

    auto stale = control.submitGameplay(
        player, instance, command("climb-stale", "climbing:cancel", character, before));
    CHECK(!stale.ok());
    CHECK_EQ(stale.code(), eve::StatusCode::Conflict);
    CHECK(!runtime.executionId().isZero());
}

TEST_CASE("gameplay.control.climbingRejectsUnauthorizedCharacterWithoutProbeMutation") {
    ClimbingWorld fixture;
    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.upsertAction(mantle()).ok());
    const eve::climbing::ClimbingPose pose{{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1};
    const auto instance = subject("00000000-0000-7000-8000-000000000911");
    const auto character = subject("00000000-0000-7000-8000-000000000912");
    const auto stranger = subject("00000000-0000-7000-8000-000000000913");
    eve::climbing::ClimbingControl control(instance, character, runtime, *fixture.world, pose);
    eve::GameplaySession unauthorized{"other", eve::GameplayAccess::PlayerEquivalent, {stranger}};
    auto observed = control.observeGameplay(unauthorized, instance);
    CHECK(!observed.ok());
    CHECK(runtime.executionId().isZero());
    CHECK_EQ(runtime.pendingEventCount(), std::size_t{0});
}
