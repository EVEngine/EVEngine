#include "npc_ai/NpcAiECS.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::npc_ai;

namespace {
class NpcTestEntity final : public ecs::Entity {
public:
    ENTITY(NpcTestEntity, ecs::Entity)
    void release() override { ecs::DestroyEntity(this); }
    COMPONENT(NpcAgentState, npcAgent)
    COMPONENT(NpcAiProjection, npcProjection)
};

BehaviorDefinition idleBehavior() {
    BehaviorDefinition behavior;
    behavior.id           = "ecs-idle";
    behavior.initialState = "idle";
    behavior.states.push_back({"idle"});
    return behavior;
}
}  // namespace

TEST_CASE("npc_ai.ecsCompositionPublishesDerivedProjection") {
    NpcAiWorld aiWorld;
    REQUIRE(aiWorld.registerBehavior(idleBehavior()).ok());
    ecs::Table       ecsWorld;
    ecs::ScopedTable guard(ecsWorld);
    auto*            entity = NpcTestEntity::create();
    auto             state  = NpcAgentState::create(aiWorld, "ecs-idle");
    REQUIRE(state.ok());
    *entity->npcAgent() = state.value();

    auto stepped = NpcAiEcsSystem::step<NpcTestEntity>(aiWorld, {11, 0.016, 8, 4});
    REQUIRE(stepped.ok());
    CHECK_EQ(stepped.value().linkedEntities, 1u);
    CHECK_EQ(entity->npcProjection()->activeState, std::string("idle"));
    CHECK_EQ(entity->npcProjection()->lastTick, 11u);

    REQUIRE(entity->npcAgent()->releaseAgent(aiWorld).ok());
    entity->release();
}

TEST_CASE("npc_ai.ecsLinkDefinesAgentFirstAndEntityFirstCleanup") {
    NpcAiWorld aiWorld;
    REQUIRE(aiWorld.registerBehavior(idleBehavior()).ok());

    auto entityFirst = NpcAgentState::create(aiWorld, "ecs-idle");
    REQUIRE(entityFirst.ok());
    const NpcAgentState deferredCopy = entityFirst.value();
    REQUIRE(entityFirst.value().releaseAgent(aiWorld).ok());
    NpcAgentState staleCopy    = deferredCopy;
    auto          staleRelease = staleCopy.releaseAgent(aiWorld);
    REQUIRE(staleRelease.ok());
    CHECK(staleCopy.agent.isInvalid());

    auto agentFirst = NpcAgentState::create(aiWorld, "ecs-idle");
    REQUIRE(agentFirst.ok());
    REQUIRE(aiWorld.destroyAgent(agentFirst.value().agent).ok());
    auto cleanup = agentFirst.value().releaseAgent(aiWorld);
    REQUIRE(cleanup.ok());
    CHECK(agentFirst.value().agent.isInvalid());
}
