#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "climbing/ClimbingECS.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/World3D.h"

#include <cmath>
#include <memory>
#include <vector>

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

eve::climbing::ClimbingActionDefinition vaultAction() {
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

TEST_CASE("climbing.ecs.composesWithExistingRootAcrossFixedPhases") {
    ecs::Table         table;
    ecs::ScopedTable   tableScope(table);
    EcsClimbingFixture fixture;

    TestClimber* entity = TestClimber::create();
    auto         state  = eve::climbing::ClimbingState::create();
    REQUIRE(state.ok());
    *entity->climbingState() = std::move(state).takeValue();

    auto bodyLink = eve::physics::PhysicsLink::fromBody(*fixture.character);
    REQUIRE(bodyLink.ok());
    entity->climbingLinks()->physicsBody = std::move(bodyLink).takeValue();

    auto runtime = eve::climbing::Climbing::resolve(entity->climbingState()->runtime);
    REQUIRE(runtime.isBound());
    REQUIRE(runtime->upsertAction(vaultAction()).ok());

    auto body              = entity->climbingBody();
    body->feet             = {0.f, 0.f, 0.f};
    body->lastStableFeet   = body->feet;
    body->forward          = {0.f, 0.f, 1.f};
    body->velocity         = {0.f, 0.f, 1.f};
    body->grounded         = true;
    body->lastGroundedTick = eve::SimulationTick(10);
    REQUIRE(eve::climbing::ClimbingInputSystem::submit(*entity->climbingIntent(), eve::climbing::ClimbingCommand::Climb,
                                                       eve::SimulationTick(10), 4)
                .ok());

    auto probed = eve::climbing::ClimbingProbeSystem::step<TestClimber>(*fixture.world, eve::SimulationTick(10));
    REQUIRE(probed.ok());
    CHECK_EQ(probed.value(), std::size_t{1});
    CHECK(!entity->climbingCandidates()->values().empty());
    CHECK(entity->climbingCandidates()->tick() == eve::SimulationTick(10));

    auto selected = eve::climbing::ClimbingEcsSelectionSystem::step<TestClimber>(
        *fixture.world, eve::climbing::ClimbingCommand::Climb, eve::SimulationTick(10));
    REQUIRE(selected.ok());
    CHECK_EQ(selected.value(), std::size_t{1});
    CHECK(!runtime->executionId().isZero());
    CHECK(!entity->climbingIntent()->commands.front().consumedExecutionId.isZero());

    const eve::SimulationStep step{eve::SimulationTick(11), eve::Duration::fromNanoseconds(16666667)};
    auto                      advanced = eve::climbing::ClimbingMotionSystem::step<TestClimber>(*fixture.world, step);
    REQUIRE(advanced.ok());
    CHECK_EQ(advanced.value(), std::size_t{1});
    CHECK(entity->climbingState()->lastAdvanceTick == step.tick);
    CHECK(std::fabs(fixture.character->getY() - (body->feet.y + body->capsuleHeight * 0.5f)) < 1e-4f);

    auto posed = eve::climbing::ClimbingPoseSystem::step<TestClimber>(step.tick);
    REQUIRE(posed.ok());
    CHECK_EQ(posed.value(), std::size_t{1});
    CHECK(entity->climbingPose()->executionId == runtime->executionId());
    REQUIRE(!entity->climbingEvents()->values().empty());
    CHECK_EQ(static_cast<int>(entity->climbingEvents()->values().front().kind),
             static_cast<int>(eve::climbing::ClimbingEventKind::Started));

    REQUIRE(entity->climbingState()->releaseRuntime().ok());
    CHECK(!eve::climbing::Climbing::resolve(entity->climbingState()->runtime).isBound());
    entity->release();
}

TEST_CASE("climbing.ecs.rejectsStalePhysicsLinkWithoutMutatingDerivedCandidates") {
    ecs::Table         table;
    ecs::ScopedTable   tableScope(table);
    EcsClimbingFixture fixture;

    TestClimber* entity = TestClimber::create();
    auto         state  = eve::climbing::ClimbingState::create();
    REQUIRE(state.ok());
    *entity->climbingState() = std::move(state).takeValue();
    auto runtime             = eve::climbing::Climbing::resolve(entity->climbingState()->runtime);
    REQUIRE(runtime.isBound());
    REQUIRE(runtime->upsertAction(vaultAction()).ok());

    auto link = eve::physics::PhysicsLink::fromBody(*fixture.character);
    REQUIRE(link.ok());
    entity->climbingLinks()->physicsBody = std::move(link).takeValue();

    eve::climbing::ClimbingCandidate sentinel;
    sentinel.actionId = "test:sentinel";
    const std::vector<eve::climbing::ClimbingCandidate> oldValues{sentinel};
    REQUIRE(entity->climbingCandidates()->replace(oldValues, eve::SimulationTick(7)).ok());
    fixture.world->destroyBody(fixture.character);
    fixture.character = nullptr;

    auto probed = eve::climbing::ClimbingProbeSystem::step<TestClimber>(*fixture.world, eve::SimulationTick(8));
    CHECK(!probed.ok());
    CHECK_EQ(entity->climbingCandidates()->values().size(), std::size_t{1});
    CHECK_EQ(entity->climbingCandidates()->values().front().actionId, std::string("test:sentinel"));
    CHECK(entity->climbingCandidates()->tick() == eve::SimulationTick(7));

    REQUIRE(entity->climbingState()->releaseRuntime().ok());
    entity->release();
}

TEST_CASE("climbing.ecs.profileDrivesCollisionConstrainedGroundAndAirLocomotion") {
    ecs::Table         table;
    ecs::ScopedTable   tableScope(table);
    EcsClimbingFixture fixture;
    auto*              floor = fixture.world->newBody("static", 0.f, -0.1f, 0.f);
    floor->newBoxShape(20.f, 0.2f, 20.f);

    TestClimber* entity = TestClimber::create();
    auto         state  = eve::climbing::ClimbingState::create();
    REQUIRE(state.ok());
    *entity->climbingState() = std::move(state).takeValue();
    auto link                = eve::physics::PhysicsLink::fromBody(*fixture.character);
    REQUIRE(link.ok());
    entity->climbingLinks()->physicsBody = std::move(link).takeValue();
    auto runtime                         = eve::climbing::Climbing::resolve(entity->climbingState()->runtime);
    REQUIRE(runtime.isBound());

    eve::climbing::ClimbingProfileDefinition profile;
    profile.groundAcceleration = 12.f;
    profile.groundBraking      = 24.f;
    profile.airControl         = 0.25f;
    profile.gravity            = 18.f;
    profile.jumpSpeed          = 6.f;
    REQUIRE(runtime->setProfile(profile).ok());

    auto body                      = entity->climbingBody();
    body->feet                     = {0.f, 0.f, -2.f};
    body->lastStableFeet           = body->feet;
    body->lastGroundedTick         = eve::SimulationTick(1);
    entity->climbingIntent()->move = {3.f, 0.f, 0.f};
    const eve::Duration fixedDelta = eve::Duration::fromNanoseconds(16666667);
    auto                grounded =
        eve::climbing::ClimbingMotionSystem::step<TestClimber>(*fixture.world, {eve::SimulationTick(2), fixedDelta});
    REQUIRE(grounded.ok());
    CHECK_EQ(grounded.value(), std::size_t{1});
    CHECK(body->feet.x > 0.f);
    CHECK(body->velocity.x > 0.f);
    CHECK(body->velocity.x < 3.f);
    CHECK(body->grounded);
    CHECK_EQ(runtime->counters().queryCount, std::uint32_t{1});
    CHECK_EQ(static_cast<int>(runtime->counters().workload),
             static_cast<int>(eve::climbing::ClimbingWorkload::Ordinary));

    REQUIRE(eve::climbing::ClimbingInputSystem::submit(*entity->climbingIntent(), eve::climbing::ClimbingCommand::Jump,
                                                       eve::SimulationTick(3), profile)
                .ok());
    auto jumped =
        eve::climbing::ClimbingMotionSystem::step<TestClimber>(*fixture.world, {eve::SimulationTick(3), fixedDelta});
    REQUIRE(jumped.ok());
    CHECK(!body->grounded);
    CHECK(body->velocity.y > 0.f);
    const float launchedVelocity = body->velocity.y;

    auto airborne =
        eve::climbing::ClimbingMotionSystem::step<TestClimber>(*fixture.world, {eve::SimulationTick(4), fixedDelta});
    REQUIRE(airborne.ok());
    CHECK(body->velocity.y < launchedVelocity);
    const std::uint64_t lastJumpTick = body->lastOrdinaryJumpPressedTick.value();
    CHECK_EQ(lastJumpTick, std::uint64_t{3});

    REQUIRE(entity->climbingState()->releaseRuntime().ok());
    entity->release();
}

TEST_CASE("climbing.ecs.stepHeightUsesBoundedOwningMoverQueries") {
    ecs::Table         table;
    ecs::ScopedTable   tableScope(table);
    EcsClimbingFixture fixture;
    auto*              floor = fixture.world->newBody("static", 0.f, -0.1f, -2.f);
    floor->newBoxShape(8.f, 0.2f, 4.f);
    auto* stepBody = fixture.world->newBody("static", 0.f, 0.1f, -2.f);
    stepBody->newBoxShape(0.5f, 0.2f, 2.f);

    TestClimber* entity = TestClimber::create();
    auto         state  = eve::climbing::ClimbingState::create();
    REQUIRE(state.ok());
    *entity->climbingState() = std::move(state).takeValue();
    auto link                = eve::physics::PhysicsLink::fromBody(*fixture.character);
    REQUIRE(link.ok());
    entity->climbingLinks()->physicsBody = std::move(link).takeValue();
    auto runtime                         = eve::climbing::Climbing::resolve(entity->climbingState()->runtime);
    REQUIRE(runtime.isBound());

    auto body  = entity->climbingBody();
    auto reset = [&] {
        body->feet             = {-0.8f, 0.f, -2.f};
        body->velocity         = {2.f, 0.f, 0.f};
        body->grounded         = true;
        body->lastGroundedTick = eve::SimulationTick(10);
        fixture.character->setPosition(-0.8f, body->capsuleHeight * 0.5f, -2.f);
    };
    entity->climbingIntent()->move = {2.f, 0.f, 0.f};
    const eve::Duration halfSecond = eve::Duration::fromNanoseconds(500000000);

    reset();
    body->stepHeight = 0.f;
    REQUIRE(
        eve::climbing::ClimbingMotionSystem::step<TestClimber>(*fixture.world, {eve::SimulationTick(11), halfSecond})
            .ok());
    const float blockedX = body->feet.x;

    reset();
    body->stepHeight = 0.35f;
    REQUIRE(
        eve::climbing::ClimbingMotionSystem::step<TestClimber>(*fixture.world, {eve::SimulationTick(12), halfSecond})
            .ok());
    CHECK(body->feet.x > blockedX + 0.2f);
    CHECK(body->feet.y > 0.15f);
    CHECK(body->grounded);
    CHECK_EQ(runtime->counters().queryCount, eve::climbing::ClimbingQueryBudgets::Ordinary);
    CHECK(runtime->counters().moverIterations > 0);
    const auto samples = runtime->telemetry().samples();
    REQUIRE(!samples.empty());
    CHECK(samples.back().elapsedNanoseconds > 0);

    REQUIRE(entity->climbingState()->releaseRuntime().ok());
    entity->release();
}

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
