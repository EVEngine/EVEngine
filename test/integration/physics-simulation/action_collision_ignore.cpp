#include "action/ActionBlockRuntime.h"
#include "action/ActionNotifyRegistry.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/PhysicsLink.h"
#include "physics/World3D.h"
#include "physics/action/ActionCollisionIgnoreState.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <memory>
#include <string_view>

namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return *parsed;
}

eve::action::ActionActiveBlock active(const eve::action::ActionTimelineEvent& event) {
    return {event.trackId, event.itemId, event.type, eve::Duration::zero(),
            eve::Duration::fromSeconds(0.5).takeValue(), event.payload};
}

}  // namespace

TEST_CASE("actionCollisionIgnore.overlapRestoresPhysicsAuthorityAndSurvivesStaleEntity") {
    auto* physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.f, 0.f, 0.f, false));
    auto* firstBody = world->newBody("dynamic", 0.f, 0.f, 0.f);
    auto* secondBody = world->newBody("dynamic", 1.f, 0.f, 0.f);
    auto firstLink = eve::physics::PhysicsLink::fromBody(*firstBody);
    auto secondLink = eve::physics::PhysicsLink::fromBody(*secondBody);
    REQUIRE(firstLink.ok());
    REQUIRE(secondLink.ok());

    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    const eve::action::ActionExecutionId execution(701);
    ecs::EntityHandle sourceHandle{nullptr, typeid(void), 11, 3};
    bool entityAlive = true;
    eve::physics::action_adapter::ActionCollisionIgnoreState ignores(
        *world, [&](ecs::EntityHandle handle, std::string_view channel)
                    -> eve::Result<eve::physics::action_adapter::ActionCollisionPair> {
            if (!entityAlive || handle.id != sourceHandle.id || channel != "self-vs-owner")
                return eve::Result<eve::physics::action_adapter::ActionCollisionPair>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                           "collision subject is stale", "entity"));
            return eve::Result<eve::physics::action_adapter::ActionCollisionPair>::success(
                {firstLink.value(), secondLink.value()});
        });

    eve::action::ActionTimelineEvent first{
        eve::action::ActionTimelineEventKind::StateEnter, id("collision-track:ignore"),
        id("collision-window:first"), id("collision:ignore-window"), eve::Duration::zero(),
        {{"channel", eve::Value("self-vs-owner")}}};
    auto second = first;
    second.itemId = id("collision-window:second");
    eve::action::ActionNotifyContext context;
    context.executionId = execution;
    context.source = sourceHandle;
    CHECK(!registry.value().dispatch(first, context).ok());

    ignores.setEnabled(true);
    eve::action::ActionBlockRuntime runtime(registry.value());
    eve::action::ActionAdvance entered;
    entered.id = execution;
    entered.phase = eve::action::ActionPhase::Active;
    entered.timelineEvents = {first, second};
    entered.activeBlocks = {active(first), active(second)};
    REQUIRE(runtime.apply(entered, context).ok());
    CHECK_EQ(ignores.activeCount(), 2U);
    CHECK_EQ(ignores.pairCount(), 1U);
    CHECK(!world->isBodyPairCollisionEnabled(firstBody, secondBody));

    entityAlive = false;
    REQUIRE(runtime.interrupt(context).ok());
    CHECK_EQ(ignores.activeCount(), 0U);
    CHECK_EQ(ignores.pairCount(), 0U);
    CHECK(world->isBodyPairCollisionEnabled(firstBody, secondBody));
    REQUIRE(ignores.shutdown().ok());
}

TEST_CASE("actionCollisionIgnore.preservesExternalDisableAndCleansUpDestroyedBodies") {
    auto* physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.f, 0.f, 0.f, false));
    auto* firstBody = world->newBody("dynamic", 0.f, 0.f, 0.f);
    auto* secondBody = world->newBody("dynamic", 1.f, 0.f, 0.f);
    auto firstLink = eve::physics::PhysicsLink::fromBody(*firstBody);
    auto secondLink = eve::physics::PhysicsLink::fromBody(*secondBody);
    REQUIRE(firstLink.ok());
    REQUIRE(secondLink.ok());
    world->setBodyPairCollisionEnabled(firstBody, secondBody, false);

    eve::physics::action_adapter::ActionCollisionIgnoreState ignores(
        *world, [&](ecs::EntityHandle, std::string_view)
                    -> eve::Result<eve::physics::action_adapter::ActionCollisionPair> {
            return eve::Result<eve::physics::action_adapter::ActionCollisionPair>::success(
                {firstLink.value(), secondLink.value()});
        });
    eve::action::ActionStateWindowBinding binding;
    binding.kind = eve::action::ActionStateWindowKind::CollisionIgnore;
    binding.resource = "external-disable";
    eve::action::ActionTimelineEvent event{
        eve::action::ActionTimelineEventKind::StateEnter, id("collision-track:ignore"),
        id("collision-window:external"), id("collision:ignore-window"), eve::Duration::zero(), {}};
    eve::action::ActionNotifyContext context;
    context.executionId = eve::action::ActionExecutionId(702);
    context.source = ecs::EntityHandle{nullptr, typeid(void), 12, 1};
    REQUIRE(ignores.enter(binding, event, context).ok());
    REQUIRE(ignores.exit(binding, event, context).ok());
    CHECK(!world->isBodyPairCollisionEnabled(firstBody, secondBody));

    REQUIRE(ignores.enter(binding, event, context).ok());
    secondBody->destroy();
    REQUIRE(ignores.exit(binding, event, context).ok());
    CHECK_EQ(ignores.activeCount(), 0U);
    CHECK_EQ(ignores.pairCount(), 0U);
    REQUIRE(ignores.shutdown().ok());
}

TEST_CASE("actionCollisionIgnore.destructorRestoresAndWorldMayDieFirst") {
    auto* physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.f, 0.f, 0.f, false));
    auto* firstBody = world->newBody("dynamic", 0.f, 0.f, 0.f);
    auto* secondBody = world->newBody("dynamic", 1.f, 0.f, 0.f);
    auto firstLink = eve::physics::PhysicsLink::fromBody(*firstBody);
    auto secondLink = eve::physics::PhysicsLink::fromBody(*secondBody);
    REQUIRE(firstLink.ok());
    REQUIRE(secondLink.ok());
    eve::action::ActionStateWindowBinding binding;
    binding.kind = eve::action::ActionStateWindowKind::CollisionIgnore;
    binding.resource = "lifetime";
    eve::action::ActionTimelineEvent event{
        eve::action::ActionTimelineEventKind::StateEnter, id("collision-track:ignore"),
        id("collision-window:lifetime"), id("collision:ignore-window"), eve::Duration::zero(), {}};
    eve::action::ActionNotifyContext context;
    context.executionId = eve::action::ActionExecutionId(703);
    context.source = ecs::EntityHandle{nullptr, typeid(void), 13, 1};

    {
        eve::physics::action_adapter::ActionCollisionIgnoreState ignores(
            *world, [&](ecs::EntityHandle, std::string_view)
                        -> eve::Result<eve::physics::action_adapter::ActionCollisionPair> {
                return eve::Result<eve::physics::action_adapter::ActionCollisionPair>::success(
                    {firstLink.value(), secondLink.value()});
            });
        REQUIRE(ignores.enter(binding, event, context).ok());
        CHECK(!world->isBodyPairCollisionEnabled(firstBody, secondBody));
    }
    CHECK(world->isBodyPairCollisionEnabled(firstBody, secondBody));

    auto ignores = std::make_unique<eve::physics::action_adapter::ActionCollisionIgnoreState>(
        *world, [&](ecs::EntityHandle, std::string_view)
                    -> eve::Result<eve::physics::action_adapter::ActionCollisionPair> {
            return eve::Result<eve::physics::action_adapter::ActionCollisionPair>::success(
                {firstLink.value(), secondLink.value()});
        });
    REQUIRE(ignores->enter(binding, event, context).ok());
    world->destroy();
    REQUIRE(ignores->shutdown().ok());
    ignores.reset();
}
