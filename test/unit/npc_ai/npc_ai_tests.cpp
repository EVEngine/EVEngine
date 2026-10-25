// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "npc_ai.cpp",
    "npc_ai_ecs.cpp",
    "npc_ai_smart_object.cpp",
    "decision_context.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_npc_ai)
#include "PrivateTestScope.h"
#include "npc_ai/NpcAi.h"
#include "npc_ai/SensingPerception.h"
#include "sensing/Sensing.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace test_npc_ai {
using namespace eve::npc_ai;
namespace {
class TaskProbe final : public ITaskService {
public:
    eve::Result<void> start(const TaskContext&, const TaskSpec& spec, std::string&) override {
        ++starts[spec.id];
        return eve::Result<void>::success();
    }
    eve::Result<TaskStatus> tick(const TaskContext&, const TaskSpec& spec, std::string&) override {
        ++ticks[spec.id];
        return eve::Result<TaskStatus>::success(TaskStatus::Running);
    }
    void stop(const TaskContext& context, const TaskSpec& spec, StopReason, std::string_view) noexcept override {
        ++stops[spec.id];
        stoppedIn.push_back(std::string(context.stateId));
    }
    std::map<std::string, int> starts, ticks, stops;
    std::vector<std::string>   stoppedIn;
};
class RejectStart final : public ITaskService {
public:
    eve::Result<void> start(const TaskContext&, const TaskSpec&, std::string&) override {
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::CallbackFailure,
                                                                 "injected start failure", {}, {}, "npc_ai.test"));
    }
    eve::Result<TaskStatus> tick(const TaskContext&, const TaskSpec&, std::string&) override {
        return eve::Result<TaskStatus>::success(TaskStatus::Running);
    }
    void stop(const TaskContext&, const TaskSpec&, StopReason, std::string_view) noexcept override {}
};
BehaviorDefinition guard() {
    BehaviorDefinition definition{"guard", 1, "idle", {}};
    StateDefinition    idle;
    idle.id = "idle";
    idle.tasks.push_back({"wait", "count"});
    idle.transitions.push_back({"alert", "enemy_seen", {}, 10});
    StateDefinition alert;
    alert.id = "alert";
    alert.enterConditions.push_back({"enemy", CompareOp::Exists, std::nullopt});
    definition.states = {idle, alert};
    return definition;
}
BehaviorDefinition idle(std::string id) {
    BehaviorDefinition definition;
    definition.id           = std::move(id);
    definition.initialState = "idle";
    definition.states.push_back({"idle"});
    return definition;
}
}  // namespace

TEST_CASE("npc_ai.spec.definitionAndBoundedScheduler") {
    SUB_CASE("duplicate state rejects registration without publishing a partial behavior") {
        NpcAiWorld world;
        auto       invalid   = guard();
        invalid.states[1].id = "idle";
        REQUIRE(!world.registerBehavior(std::move(invalid)).ok());
        REQUIRE(!world.createAgent("guard").ok());
        REQUIRE(world.registerBehavior(guard()).ok());
        REQUIRE(world.createAgent("guard").ok());
    };
    SUB_CASE("two slots update only two of eight agents per tick and eventually visit all") {
        NpcAiWorld world({16, 0});
        REQUIRE(world.registerBehavior(idle("many")).ok());
        std::vector<AgentHandle> agents;
        for (int i = 0; i < 8; ++i) {
            auto created = world.createAgent("many");
            REQUIRE(created.ok());
            agents.push_back(created.value());
        }
        for (std::uint64_t tick = 1; tick <= 4; ++tick) {
            auto report = world.tick({tick, .1, 2, 2});
            REQUIRE(report.ok());
            REQUIRE(report.value().agentsUpdated == 2);
            REQUIRE(report.value().agentsDeferred == 6);
        }
        for (auto agent : agents) {
            auto state = world.snapshot(agent);
            REQUIRE(state.ok());
            REQUIRE(state.value().lastTick > 0);
        }
    };
}

TEST_CASE("npc_ai.spec.transitionsAndTaskLifecycle") {
    SUB_CASE("signal enters eligible state and stops old task; destroyed handle becomes stale") {
        NpcAiWorld world;
        auto       service = std::make_unique<TaskProbe>();
        auto*      probe   = service.get();
        REQUIRE(world.registerTaskService("count", std::move(service)).ok());
        REQUIRE(world.registerBehavior(guard()).ok());
        auto agent = world.createAgent("guard");
        REQUIRE(agent.ok());
        REQUIRE(world.setBlackboard(agent.value(), "enemy", std::string("player")).ok());
        REQUIRE(world.tick({1, .016, 1, 8}).ok());
        REQUIRE(probe->starts["wait"] == 1);
        REQUIRE(probe->ticks["wait"] == 1);
        REQUIRE(world.signal(agent.value(), "enemy_seen").ok());
        REQUIRE(world.tick({2, .016, 1, 8}).ok());
        auto state = world.snapshot(agent.value());
        REQUIRE(state.ok());
        REQUIRE(state.value().activeState == "alert");
        REQUIRE(probe->stops["wait"] == 1);
        REQUIRE(world.destroyAgent(agent.value()).ok());
        REQUIRE(!world.snapshot(agent.value()).ok());
    };
    SUB_CASE("failed enter condition leaves the source state and task running") {
        NpcAiWorld world;
        auto       service = std::make_unique<TaskProbe>();
        auto*      probe   = service.get();
        REQUIRE(world.registerTaskService("count", std::move(service)).ok());
        REQUIRE(world.registerBehavior(guard()).ok());
        auto agent = world.createAgent("guard");
        REQUIRE(agent.ok());
        REQUIRE(world.tick({1, .016, 1, 4}).ok());
        REQUIRE(world.signal(agent.value(), "enemy_seen").ok());
        REQUIRE(world.tick({2, .016, 1, 4}).ok());
        auto state = world.snapshot(agent.value());
        REQUIRE(state.ok());
        REQUIRE(state.value().activeState == "idle");
        REQUIRE(probe->stops["wait"] == 0);
    };
    SUB_CASE("sibling transition retains ancestor task and replaces the child task") {
        NpcAiWorld world;
        auto       service = std::make_unique<TaskProbe>();
        auto*      probe   = service.get();
        REQUIRE(world.registerTaskService("count", std::move(service)).ok());
        BehaviorDefinition behavior;
        behavior.id           = "hierarchy";
        behavior.initialState = "patrol";
        StateDefinition root, patrol, combat;
        root.id = "root";
        root.tasks.push_back({"awareness", "count"});
        patrol.id     = "patrol";
        patrol.parent = "root";
        patrol.tasks.push_back({"move", "count"});
        patrol.transitions.push_back({"combat", "enemy_seen", {}, 1});
        combat.id     = "combat";
        combat.parent = "root";
        combat.tasks.push_back({"aim", "count"});
        behavior.states = {root, patrol, combat};
        REQUIRE(world.registerBehavior(std::move(behavior)).ok());
        auto agent = world.createAgent("hierarchy");
        REQUIRE(agent.ok());
        REQUIRE(world.tick({1, .016, 1, 4}).ok());
        REQUIRE(world.signal(agent.value(), "enemy_seen").ok());
        REQUIRE(world.tick({2, .016, 1, 4}).ok());
        auto state = world.snapshot(agent.value());
        REQUIRE(state.ok());
        REQUIRE(state.value().activePath.size() == 2);
        REQUIRE(state.value().activePath[0] == "root");
        REQUIRE(state.value().activePath[1] == "combat");
        REQUIRE(probe->starts["awareness"] == 1);
        REQUIRE(probe->stops["awareness"] == 0);
        REQUIRE(probe->stops["move"] == 1);
        REQUIRE(probe->starts["aim"] == 1);
    };
    SUB_CASE("higher-priority ancestor transition wins over a leaf transition") {
        NpcAiWorld         world;
        BehaviorDefinition behavior;
        behavior.id           = "priority";
        behavior.initialState = "leaf";
        StateDefinition root, leaf;
        root.id = "root";
        root.transitions.push_back({"high", "choose", {}, 10});
        leaf.id     = "leaf";
        leaf.parent = "root";
        leaf.transitions.push_back({"low", "choose", {}, 1});
        behavior.states = {root, leaf, {"low"}, {"high"}};
        REQUIRE(world.registerBehavior(std::move(behavior)).ok());
        auto agent = world.createAgent("priority");
        REQUIRE(agent.ok());
        REQUIRE(world.signal(agent.value(), "choose").ok());
        REQUIRE(world.tick({1, .016, 1, 4}).ok());
        auto state = world.snapshot(agent.value());
        REQUIRE(state.ok());
        REQUIRE(state.value().activeState == "high");
    };
}

TEST_CASE("npc_ai.spec.admissionMemoryAndRestore") {
    SUB_CASE("missing task provider and failed start do not leave a partially started state") {
        BehaviorDefinition behavior;
        behavior.id           = "admission";
        behavior.initialState = "root";
        StateDefinition root;
        root.id         = "root";
        root.tasks      = {{"first", "count"}, {"second", "fail"}};
        behavior.states = {root};
        NpcAiWorld missing;
        auto       missingService = std::make_unique<TaskProbe>();
        auto*      missingProbe   = missingService.get();
        REQUIRE(missing.registerTaskService("count", std::move(missingService)).ok());
        REQUIRE(missing.registerBehavior(behavior).ok());
        REQUIRE(missing.createAgent("admission").ok());
        REQUIRE(!missing.tick({1, .016, 1, 4}).ok());
        REQUIRE(missingProbe->starts["first"] == 0);
        NpcAiWorld failed;
        auto       service = std::make_unique<TaskProbe>();
        auto*      probe   = service.get();
        REQUIRE(failed.registerTaskService("count", std::move(service)).ok());
        REQUIRE(failed.registerTaskService("fail", std::make_unique<RejectStart>()).ok());
        REQUIRE(failed.registerBehavior(std::move(behavior)).ok());
        REQUIRE(failed.createAgent("admission").ok());
        REQUIRE(!failed.tick({1, .016, 1, 4}).ok());
        REQUIRE(probe->starts["first"] == 1);
        REQUIRE(probe->stops["first"] == 1);
        REQUIRE(probe->stoppedIn.front() == "root");
    };
    SUB_CASE("typed blackboard and bounded perception reject bad values and expire old observations") {
        NpcAiWorld world({32, 2});
        auto       behavior = idle("memory");
        behavior.blackboardSchema.push_back({"alert", BlackboardType::Boolean, true, false});
        REQUIRE(world.registerBehavior(std::move(behavior)).ok());
        auto agent = world.createAgent("memory");
        REQUIRE(agent.ok());
        REQUIRE(!world.setBlackboard(agent.value(), "alert", std::string("yes")).ok());
        REQUIRE(world.remember(agent.value(), {"a", "sight", .8, 1, 10, "{}"}).ok());
        REQUIRE(world.remember(agent.value(), {"b", "hearing", .5, 2, 2, "{}"}).ok());
        REQUIRE(world.remember(agent.value(), {"c", "damage", 1., 3, 20, "{}"}).ok());
        auto bounded = world.snapshot(agent.value());
        REQUIRE(bounded.ok());
        REQUIRE(bounded.value().perception.size() == 2);
        REQUIRE(world.tick({4, .016, 1, 4}).ok());
        auto expired = world.snapshot(agent.value());
        REQUIRE(expired.ok());
        REQUIRE(expired.value().perception.size() == 1);
    };
    SUB_CASE("archive rejects unknown schema atomically and restores a new agent with provider restart") {
        NpcAiWorld world;
        auto       service = std::make_unique<TaskProbe>();
        auto*      probe   = service.get();
        REQUIRE(world.registerTaskService("count", std::move(service)).ok());
        REQUIRE(world.registerBehavior(guard()).ok());
        auto agent = world.createAgent("guard");
        REQUIRE(agent.ok());
        REQUIRE(world.setBlackboard(agent.value(), "enemy", std::string("player")).ok());
        REQUIRE(world.tick({11, .016, 1, 4}).ok());
        auto archive = world.archive(agent.value());
        REQUIRE(archive.ok());
        auto incompatible          = archive.value();
        incompatible.schemaVersion = 99;
        REQUIRE(!world.restoreAgent(incompatible).ok());
        REQUIRE(!world.isStale(agent.value()));
        auto restored = world.restoreAgent(archive.value());
        REQUIRE(restored.ok());
        REQUIRE(restored.value() != agent.value());
        auto state = world.snapshot(restored.value());
        REQUIRE(state.ok());
        REQUIRE(state.value().lastTick == 11);
        REQUIRE(state.value().blackboard.at("enemy") == BlackboardValue(std::string("player")));
        REQUIRE(world.tick({12, .016, 2, 4}).ok());
        REQUIRE(probe->starts["wait"] == 2);
    };
}

TEST_CASE("npc_ai.spec.sensingBridge") {
    eve::sensing::SensingWorld sensing;
    REQUIRE(sensing.upsert("00000000-0000-7000-8000-000000000031", 1.f, 0.f, "red", "unit", "").ok());
    REQUIRE(sensing.upsert("00000000-0000-7000-8000-000000000032", 4.f, 0.f, "red", "unit", "").ok());
    eve::sensing::QuerySpec spec;
    spec.requiredTags = {"unit"};
    spec.maxRange     = 10.f;
    spec.maxCount     = 8;
    spec.countPolicy  = eve::sensing::CountPolicy::TruncateToMax;
    spec.sortKey      = eve::sensing::SortKey::DistanceAscending;
    auto ranked       = sensing.query(eve::sensing::QueryOrigin{0.f, 0.f, std::nullopt}, spec);
    REQUIRE(ranked.ok());
    const auto facts = eve::sensing::perceptionFactsFrom(ranked.value());
    REQUIRE(facts.size() == 2);
    auto memories = perceptionMemoriesFrom(facts, 10, 20);
    REQUIRE(memories.size() == 2);
    REQUIRE(memories[0].subject == facts[0].subjectId);
    REQUIRE(memories[0].sense == "sensing.query");
    REQUIRE(memories[0].confidence > memories[1].confidence);
    NpcAiWorld world({32, 8});
    REQUIRE(world.registerBehavior(idle("bridge")).ok());
    auto agent = world.createAgent("bridge");
    REQUIRE(agent.ok());
    for (const auto& memory : memories) REQUIRE(world.remember(agent.value(), memory).ok());
    auto state = world.snapshot(agent.value());
    REQUIRE(state.ok());
    REQUIRE(state.value().perception.size() == 2);
}
}  // namespace test_npc_ai
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_npc_ai_ecs)
#include "PrivateTestScope.h"
#include "npc_ai/NpcAiECS.h"
namespace test_npc_ai_ecs {
using namespace eve::npc_ai;
namespace {
class NpcTestEntity final : public ecs::Entity {
public:
    ENTITY(NpcTestEntity, ecs::Entity)
    void release() override { ecs::DestroyEntity(this); }
    COMPONENT(NpcAgentState, npcAgent)
    COMPONENT(NpcAiProjection, npcProjection)
};
BehaviorDefinition idle() {
    BehaviorDefinition result;
    result.id           = "ecs-idle";
    result.initialState = "idle";
    result.states.push_back({"idle"});
    return result;
}
}  // namespace
TEST_CASE("npc_ai.ecs.spec.projectionAndBothDestructionOrders") {
    NpcAiWorld ai;
    REQUIRE(ai.registerBehavior(idle()).ok());
    auto* aiView = &ai;
    SUB_CASE("step writes the derived projection from the linked agent") {
        ecs::Table       table;
        ecs::ScopedTable guard(table);
        auto*            entity = NpcTestEntity::create();
        auto             link   = NpcAgentState::create(*aiView, "ecs-idle");
        REQUIRE(link.ok());
        *entity->npcAgent() = link.value();
        auto report         = NpcAiEcsSystem::step<NpcTestEntity>(*aiView, {11, .016, 8, 4});
        REQUIRE(report.ok());
        REQUIRE(report.value().linkedEntities == 1);
        REQUIRE(entity->npcProjection()->activeState == "idle");
        REQUIRE(entity->npcProjection()->lastTick == 11);
        REQUIRE(entity->npcAgent()->releaseAgent(*aiView).ok());
        entity->release();
    };
    SUB_CASE("release after entity and after agent destruction is idempotent") {
        auto link = NpcAgentState::create(*aiView, "ecs-idle");
        REQUIRE(link.ok());
        auto copy = link.value();
        REQUIRE(link.value().releaseAgent(*aiView).ok());
        REQUIRE(copy.releaseAgent(*aiView).ok());
        REQUIRE(copy.agent.isInvalid());
        auto reverse = NpcAgentState::create(*aiView, "ecs-idle");
        REQUIRE(reverse.ok());
        REQUIRE(aiView->destroyAgent(reverse.value().agent).ok());
        REQUIRE(reverse.value().releaseAgent(*aiView).ok());
        REQUIRE(reverse.value().agent.isInvalid());
    };
}
}  // namespace test_npc_ai_ecs
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_npc_ai_smart_object)
#include "PrivateTestScope.h"
#include "npc_ai/SmartObject.h"
namespace test_npc_ai_smart_object {
using namespace eve::npc_ai;
namespace {
SmartObjectDefinition cover(std::string id, double x) {
    return {std::move(id), {{"left", "cover", {"high", "outdoor"}, {x, 0., 0.}}}};
}
}  // namespace
TEST_CASE("npc_ai.smartObject.spec.queryLeaseExpiryAndCleanup") {
    SmartObjectWorld world;
    auto             far  = world.registerObject(cover("cover-b", 8.));
    auto             near = world.registerObject(cover("cover-a", 2.));
    REQUIRE(far.ok());
    REQUIRE(near.ok());
    SmartObjectQuery query{"cover", {"high"}, {0., 0., 0.}, 10.};
    auto             candidates = world.query(query);
    REQUIRE(candidates.ok());
    REQUIRE(candidates.value().size() == 2);
    REQUIRE(candidates.value()[0].logicalId == "cover-a");
    const AgentHandle agent(4, 1);
    auto              lease = world.claim(agent, near.value(), "left", 10, 5);
    REQUIRE(lease.ok());
    REQUIRE(world.query(query).value().size() == 1);
    REQUIRE(!world.claim(AgentHandle(5, 1), near.value(), "left", 10, 5).ok());
    REQUIRE(world.renew(lease.value(), agent, 12, 10).ok());
    auto state = world.snapshot(lease.value());
    REQUIRE(state.ok());
    REQUIRE(state.value().expiresAtTick == 22);
    auto expired = world.expire(22);
    REQUIRE(expired.ok());
    REQUIRE(expired.value().claimsExpired == 1);
    REQUIRE(!world.snapshot(lease.value()).ok());
    auto next = world.claim(agent, near.value(), "left", 23, 100);
    REQUIRE(next.ok());
    REQUIRE(!world.removeObject(near.value(), SmartObjectRemoval::RejectClaimed).ok());
    REQUIRE(world.removeObject(near.value(), SmartObjectRemoval::CancelClaims).ok());
    REQUIRE(!world.snapshot(next.value()).ok());
    auto other = world.claim(agent, far.value(), "left", 24, 100);
    REQUIRE(other.ok());
    auto removed = world.releaseAgentClaims(agent);
    REQUIRE(removed.ok());
    REQUIRE(removed.value() == 1);
    REQUIRE(world.removeObject(far.value(), SmartObjectRemoval::RejectClaimed).ok());
}
}  // namespace test_npc_ai_smart_object
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_decision_context)
#include <cmath>
#include "PrivateTestScope.h"
#include "npc_ai/Decision.h"
namespace test_decision_context {
using namespace eve::npc_ai;
TEST_CASE("decision.spec.blackboardFsmUtilityInfluenceAndRestore") {
    DecisionContext context;
    REQUIRE(context.set("ai", "enemy", "\"tank-1\"").ok());
    REQUIRE(context.get("ai", "enemy", "null") == "\"tank-1\"");
    REQUIRE(context.setState("unit", "idle").ok());
    REQUIRE(context.addTransition("unit", "idle", "enemy_seen", "attack").ok());
    auto event = context.trigger("unit", "enemy_seen");
    REQUIRE(event.ok());
    REQUIRE(event.value());
    REQUIRE(context.state("unit") == "attack");
    REQUIRE(std::abs(DecisionContext::utility("1:2,0:1") - .6666667f) < .001f);
    REQUIRE(DecisionContext::choose("retreat=0.8:1;attack=0.8:1") == "attack");
    REQUIRE(context.newGrid("threat", 2, 2, 10, 0, 0).ok());
    REQUIRE(context.setCell("threat", 1, 0, 2).ok());
    REQUIRE(context.addCell("threat", 1, 0, 3).ok());
    REQUIRE(context.sample("threat", 15, 5, -1) == 5.f);
    const auto      snapshot = context.snapshotJson();
    DecisionContext restored;
    REQUIRE(restored.restoreJson(snapshot).ok());
    REQUIRE(restored.snapshotJson() == snapshot);
    REQUIRE(!restored.restoreJson("{}").ok());
    REQUIRE(restored.snapshotJson() == snapshot);
}
}  // namespace test_decision_context
#endif
