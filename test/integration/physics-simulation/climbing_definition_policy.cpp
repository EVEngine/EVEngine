#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "climbing/Climbing.h"
#include "climbing/ClimbingInput.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/World3D.h"

#include <memory>

namespace {

struct PolicyWorld {
    PolicyWorld() {
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

eve::climbing::ClimbingActionDefinition vault(std::string id) {
    eve::climbing::ClimbingActionDefinition action;
    action.id             = std::move(id);
    action.kind           = eve::climbing::ClimbingActionKind::Vault;
    action.minHeight      = 0.4f;
    action.maxHeight      = 0.8f;
    action.landingForward = 0.75f;
    action.apexHeight     = 0.4f;
    action.tags           = {"parkour"};
    return action;
}

eve::climbing::ClimbingPose groundedPose(const PolicyWorld& fixture) {
    return {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 4.f, fixture.player->getId(), 0.f, true};
}

}  // namespace

TEST_CASE("climbing.definitionPolicy.enforcesSourceTagsAndCandidateBudget") {
    PolicyWorld                              fixture;
    eve::climbing::ClimbingRuntime           runtime;
    eve::climbing::ClimbingProfileDefinition profile;
    profile.maxCandidates     = 1;
    profile.allowedActionTags = {"parkour"};
    auto airborne             = vault("parkour:airborne_first");
    airborne.sourceModes      = eve::climbing::ClimbingSourceMode::Airborne;
    auto grounded             = vault("parkour:grounded_second");
    grounded.sourceModes      = eve::climbing::ClimbingSourceMode::Grounded;
    auto denied               = vault("parkour:denied");
    denied.tags               = {"parkour", "disabled"};
    profile.deniedActionTags  = {"disabled"};
    profile.actions           = {airborne, grounded, denied};
    REQUIRE(runtime.setProfile(profile).ok());

    auto candidates = runtime.probe(*fixture.world, groundedPose(fixture));
    REQUIRE(candidates.ok());
    REQUIRE_EQ(candidates.value().size(), std::size_t{1});
    CHECK_EQ(candidates.value().front().actionId, std::string("parkour:grounded_second"));
}

TEST_CASE("climbing.definitionPolicy.commandIsTransactionalAndCameraCueIsOwningOutput") {
    PolicyWorld                              fixture;
    eve::climbing::ClimbingRuntime           runtime;
    eve::climbing::ClimbingProfileDefinition profile;
    profile.cameraCueProfile = "camera:climbing";
    auto action              = vault("parkour:vault_commanded");
    action.requiredCommand   = eve::climbing::ClimbingCommandRequirement::Jump;
    action.cameraCue         = "camera:vault_low";
    profile.actions.push_back(action);
    REQUIRE(runtime.setProfile(profile).ok());

    eve::climbing::ClimbingIntent intent;
    REQUIRE(eve::climbing::ClimbingInputSystem::submit(intent, eve::climbing::ClimbingCommand::Climb,
                                                       eve::SimulationTick(10), profile)
                .ok());
    auto mismatch = eve::climbing::ClimbingSelectionSystem::tryStart(runtime, *fixture.world, groundedPose(fixture),
                                                                     intent, eve::climbing::ClimbingCommand::Climb,
                                                                     eve::SimulationTick(10), eve::SimulationTick(10));
    CHECK(!mismatch.ok());
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Idle));
    REQUIRE(
        eve::climbing::ClimbingInputSystem::peek(intent, eve::climbing::ClimbingCommand::Climb, eve::SimulationTick(10))
            .has_value());

    REQUIRE(eve::climbing::ClimbingInputSystem::submit(intent, eve::climbing::ClimbingCommand::Jump,
                                                       eve::SimulationTick(11), profile)
                .ok());
    auto started = eve::climbing::ClimbingSelectionSystem::tryStart(runtime, *fixture.world, groundedPose(fixture),
                                                                    intent, eve::climbing::ClimbingCommand::Jump,
                                                                    eve::SimulationTick(11), eve::SimulationTick(11));
    REQUIRE(started.ok());
    auto advanced =
        runtime.advance(*fixture.world, {eve::SimulationTick(12), eve::Duration::fromNanoseconds(16666667)});
    REQUIRE(advanced.ok());
    CHECK_EQ(advanced.value().cameraCueProfile, std::string("camera:climbing"));
    CHECK_EQ(advanced.value().cameraCue, std::string("camera:vault_low"));
}
