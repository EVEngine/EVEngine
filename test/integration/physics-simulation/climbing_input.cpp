#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "climbing/ClimbingInput.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/World3D.h"

#include <limits>
#include <memory>

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


TEST_CASE("climbing.selection.commitsExecutionAndConsumesInputExactlyOnce") {
    ClimbingWorld                            fixture;
    eve::climbing::ClimbingRuntime           runtime;
    eve::climbing::ClimbingIntent            intent;
    eve::climbing::ClimbingProfileDefinition profile;
    profile.actions.push_back(mantleAction());
    profile.inputBufferTicks = 5;
    REQUIRE(runtime.setProfile(profile).ok());
    REQUIRE(eve::climbing::ClimbingInputSystem::submit(intent, eve::climbing::ClimbingCommand::Climb,
                                                       eve::SimulationTick(10), profile)
                .ok());

    const eve::climbing::ClimbingPose pose{{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1};
    auto started = eve::climbing::ClimbingSelectionSystem::tryStart(runtime, *fixture.world, pose, intent,
                                                                    eve::climbing::ClimbingCommand::Climb,
                                                                    eve::SimulationTick(12), eve::SimulationTick(12));
    REQUIRE(started.ok());
    CHECK(!started.value().executionId.isZero());
    CHECK(started.value().executionId == runtime.executionId());
    REQUIRE_EQ(intent.commands.size(), std::size_t(1));
    CHECK(intent.commands.front().consumedExecutionId == started.value().executionId);

    auto repeated = eve::climbing::ClimbingSelectionSystem::tryStart(runtime, *fixture.world, pose, intent,
                                                                     eve::climbing::ClimbingCommand::Climb,
                                                                     eve::SimulationTick(12), eve::SimulationTick(12));
    CHECK(!repeated.ok());
    CHECK_EQ(static_cast<int>(repeated.error()->code()), static_cast<int>(eve::DiagnosticCode::NotFound));
}

TEST_CASE("climbing.selection.failedPrepareLeavesInputAndAuthorityUnchanged") {
    ClimbingWorld                  fixture;
    eve::climbing::ClimbingRuntime runtime;
    eve::climbing::ClimbingIntent  intent;
    REQUIRE(eve::climbing::ClimbingInputSystem::submit(intent, eve::climbing::ClimbingCommand::Climb,
                                                       eve::SimulationTick(20), 4)
                .ok());
    const eve::climbing::ClimbingIntent before   = intent;
    auto                                rejected = eve::climbing::ClimbingSelectionSystem::tryStart(
        runtime, *fixture.world, {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1}, intent,
        eve::climbing::ClimbingCommand::Climb, eve::SimulationTick(21), eve::SimulationTick(21));
    CHECK(!rejected.ok());
    CHECK(intent.commands == before.commands);
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Idle));
    CHECK(runtime.executionId().isZero());
}

TEST_CASE("climbing.selection.appliesProfileCoyoteTicksAtTheBoundary") {
    ClimbingWorld                     fixture;
    const eve::climbing::ClimbingPose airborne{{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1, -0.1f, false};

    auto attempt = [&](std::uint64_t tick) {
        eve::climbing::ClimbingRuntime           runtime;
        eve::climbing::ClimbingIntent            intent;
        eve::climbing::ClimbingProfileDefinition profile;
        profile.actions.push_back(mantleAction());
        profile.coyoteTicks = 6;
        REQUIRE(runtime.setProfile(profile).ok());
        REQUIRE(eve::climbing::ClimbingInputSystem::submit(intent, eve::climbing::ClimbingCommand::Climb,
                                                           eve::SimulationTick(tick), profile)
                    .ok());
        auto result = eve::climbing::ClimbingSelectionSystem::tryStart(
            runtime, *fixture.world, airborne, intent, eve::climbing::ClimbingCommand::Climb, eve::SimulationTick(tick),
            eve::SimulationTick(100));
        return std::pair(std::move(result), std::move(intent));
    };

    auto [inside, consumed] = attempt(106);
    REQUIRE(inside.ok());
    CHECK(!consumed.commands.front().consumedExecutionId.isZero());

    auto [outside, retained] = attempt(107);
    CHECK(!outside.ok());
    CHECK(retained.commands.front().consumedExecutionId.isZero());
}
