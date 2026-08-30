#include "npc_ai/NpcAi.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <map>

using namespace eve::npc_ai;

namespace {
class CountingTask final : public ITaskService {
public:
    eve::Result<void> start(const TaskContext&, const TaskSpec& spec, std::string&) override {
        ++starts;
        ++startsById[spec.id];
        return eve::Result<void>::success();
    }
    eve::Result<TaskStatus> tick(const TaskContext&, const TaskSpec& spec, std::string&) override {
        ++ticks;
        return eve::Result<TaskStatus>::success(TaskStatus::Running);
    }
    void stop(const TaskContext& context, const TaskSpec& spec, StopReason, std::string_view) noexcept override {
        ++stops;
        ++stopsById[spec.id];
        stopStates.push_back(std::string(context.stateId));
    }
    int                        starts = 0;
    int                        ticks  = 0;
    int                        stops  = 0;
    std::map<std::string, int> startsById;
    std::map<std::string, int> stopsById;
    std::vector<std::string>   stopStates;
};

class FailingStartTask final : public ITaskService {
public:
    eve::Result<void> start(const TaskContext&, const TaskSpec&, std::string&) override {
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::CallbackFailure,
                                                                 "injected task start failure", {}, {}, "npc_ai.test"));
    }
    eve::Result<TaskStatus> tick(const TaskContext&, const TaskSpec&, std::string&) override {
        return eve::Result<TaskStatus>::success(TaskStatus::Running);
    }
    void stop(const TaskContext&, const TaskSpec&, StopReason, std::string_view) noexcept override {}
};

BehaviorDefinition patrolBehavior() {
    BehaviorDefinition result{"guard", 1, "idle", {}};
    StateDefinition    idle;
    idle.id = "idle";
    idle.tasks.push_back({"wait", "count"});
    idle.transitions.push_back({"alert", "enemy_seen", {}, 10});
    StateDefinition alert;
    alert.id = "alert";
    alert.enterConditions.push_back({"enemy", CompareOp::Exists, std::nullopt});
    result.states = {idle, alert};
    return result;
}
}  // namespace

TEST_CASE("npc_ai.definitionValidationIsTransactional") {
    NpcAiWorld world;
    auto       invalid   = patrolBehavior();
    invalid.states[1].id = "idle";
    auto rejected        = world.registerBehavior(std::move(invalid));
    CHECK(!rejected.ok());
    auto missing = world.createAgent("guard");
    CHECK(!missing.ok());
    auto accepted = world.registerBehavior(patrolBehavior());
    REQUIRE(accepted.ok());
}

TEST_CASE("npc_ai.signalTransitionAndTaskLifecycle") {
    NpcAiWorld world;
    auto       task     = std::make_unique<CountingTask>();
    auto*      observed = task.get();
    REQUIRE(world.registerTaskService("count", std::move(task)).ok());
    REQUIRE(world.registerBehavior(patrolBehavior()).ok());
    auto created = world.createAgent("guard");
    REQUIRE(created.ok());
    const auto agent = created.value();
    REQUIRE(world.setBlackboard(agent, "enemy", std::string("player")).ok());
    auto first = world.tick({1, 1.0 / 60.0, 1, 8});
    REQUIRE(first.ok());
    CHECK_EQ(observed->starts, 1);
    CHECK_EQ(observed->ticks, 1);
    REQUIRE(world.signal(agent, "enemy_seen").ok());
    auto second = world.tick({2, 1.0 / 60.0, 1, 8});
    REQUIRE(second.ok());
    auto snapshot = world.snapshot(agent);
    REQUIRE(snapshot.ok());
    CHECK_EQ(snapshot.value().activeState, std::string("alert"));
    CHECK_EQ(observed->stops, 1);
    REQUIRE(world.destroyAgent(agent).ok());
    auto stale = world.snapshot(agent);
    CHECK(!stale.ok());
}

TEST_CASE("npc_ai.schedulerHonorsAgentBudget") {
    NpcAiWorld         world;
    BehaviorDefinition behavior;
    behavior.id           = "idle";
    behavior.initialState = "root";
    behavior.states.push_back({"root"});
    REQUIRE(world.registerBehavior(std::move(behavior)).ok());
    REQUIRE(world.createAgent("idle").ok());
    REQUIRE(world.createAgent("idle").ok());
    auto report = world.tick({7, 0.1, 1, 2});
    REQUIRE(report.ok());
    CHECK_EQ(report.value().agentsUpdated, 1u);
    CHECK_EQ(report.value().agentsDeferred, 1u);
}

TEST_CASE("npc_ai.hierarchicalPathKeepsParentTaskAcrossSiblingTransition") {
    NpcAiWorld world;
    auto       task     = std::make_unique<CountingTask>();
    auto*      observed = task.get();
    REQUIRE(world.registerTaskService("count", std::move(task)).ok());

    BehaviorDefinition behavior;
    behavior.id           = "hierarchy";
    behavior.initialState = "patrol";
    StateDefinition root;
    root.id = "root";
    root.tasks.push_back({"awareness", "count"});
    StateDefinition patrol;
    patrol.id     = "patrol";
    patrol.parent = "root";
    patrol.tasks.push_back({"move", "count"});
    patrol.transitions.push_back({"combat", "enemy_seen", {}, 1});
    StateDefinition combat;
    combat.id     = "combat";
    combat.parent = "root";
    combat.tasks.push_back({"aim", "count"});
    behavior.states = {root, patrol, combat};
    REQUIRE(world.registerBehavior(std::move(behavior)).ok());
    auto created = world.createAgent("hierarchy");
    REQUIRE(created.ok());

    REQUIRE(world.tick({1, 0.016, 1, 4}).ok());
    REQUIRE(world.signal(created.value(), "enemy_seen").ok());
    REQUIRE(world.tick({2, 0.016, 1, 4}).ok());
    auto snapshot = world.snapshot(created.value());
    REQUIRE(snapshot.ok());
    CHECK_EQ(snapshot.value().activePath.size(), 2u);
    CHECK_EQ(snapshot.value().activePath[0], std::string("root"));
    CHECK_EQ(snapshot.value().activePath[1], std::string("combat"));
    CHECK_EQ(observed->startsById["awareness"], 1);
    CHECK_EQ(observed->stopsById["awareness"], 0);
    CHECK_EQ(observed->stopsById["move"], 1);
    CHECK_EQ(observed->startsById["aim"], 1);
}

TEST_CASE("npc_ai.failedEnterConditionDoesNotPartiallyTransition") {
    NpcAiWorld world;
    auto       task     = std::make_unique<CountingTask>();
    auto*      observed = task.get();
    REQUIRE(world.registerTaskService("count", std::move(task)).ok());
    REQUIRE(world.registerBehavior(patrolBehavior()).ok());
    auto created = world.createAgent("guard");
    REQUIRE(created.ok());
    REQUIRE(world.tick({1, 0.016, 1, 4}).ok());
    REQUIRE(world.signal(created.value(), "enemy_seen").ok());
    REQUIRE(world.tick({2, 0.016, 1, 4}).ok());
    auto snapshot = world.snapshot(created.value());
    REQUIRE(snapshot.ok());
    CHECK_EQ(snapshot.value().activeState, std::string("idle"));
    CHECK_EQ(observed->stops, 0);
}

TEST_CASE("npc_ai.typedBlackboardAndPerceptionMemoryAreBounded") {
    NpcAiWorld         world({32, 2});
    BehaviorDefinition behavior;
    behavior.id           = "perception";
    behavior.initialState = "idle";
    behavior.blackboardSchema.push_back({"alert", BlackboardType::Boolean, true, false});
    behavior.states.push_back({"idle"});
    REQUIRE(world.registerBehavior(std::move(behavior)).ok());
    auto created = world.createAgent("perception");
    REQUIRE(created.ok());
    auto badType = world.setBlackboard(created.value(), "alert", std::string("yes"));
    CHECK(!badType.ok());
    REQUIRE(world.remember(created.value(), {"enemy-a", "sight", 0.8, 1, 10, "{}"}).ok());
    REQUIRE(world.remember(created.value(), {"enemy-b", "hearing", 0.5, 2, 2, "{}"}).ok());
    REQUIRE(world.remember(created.value(), {"enemy-c", "damage", 1.0, 3, 20, "{}"}).ok());
    auto bounded = world.snapshot(created.value());
    REQUIRE(bounded.ok());
    CHECK_EQ(bounded.value().perception.size(), 2u);
    REQUIRE(world.tick({4, 0.016, 1, 4}).ok());
    auto expired = world.snapshot(created.value());
    REQUIRE(expired.ok());
    CHECK_EQ(expired.value().perception.size(), 1u);
    auto trace = world.trace(created.value());
    REQUIRE(trace.ok());
    CHECK(trace.value().size() >= 5u);
}

TEST_CASE("npc_ai.taskAdmissionPreflightsAndRollsBackFailedStarts") {
    BehaviorDefinition behavior;
    behavior.id           = "admission";
    behavior.initialState = "root";
    StateDefinition root;
    root.id         = "root";
    root.tasks      = {{"first", "count"}, {"second", "fail"}};
    behavior.states = {root};

    NpcAiWorld missingProviderWorld;
    auto       missingCounter  = std::make_unique<CountingTask>();
    auto*      missingObserved = missingCounter.get();
    REQUIRE(missingProviderWorld.registerTaskService("count", std::move(missingCounter)).ok());
    REQUIRE(missingProviderWorld.registerBehavior(behavior).ok());
    auto missingAgent = missingProviderWorld.createAgent("admission");
    REQUIRE(missingAgent.ok());
    auto missingTick = missingProviderWorld.tick({1, 0.016, 1, 4});
    CHECK(!missingTick.ok());
    CHECK_EQ(missingObserved->starts, 0);

    NpcAiWorld rollbackWorld;
    auto       rollbackCounter  = std::make_unique<CountingTask>();
    auto*      rollbackObserved = rollbackCounter.get();
    REQUIRE(rollbackWorld.registerTaskService("count", std::move(rollbackCounter)).ok());
    REQUIRE(rollbackWorld.registerTaskService("fail", std::make_unique<FailingStartTask>()).ok());
    REQUIRE(rollbackWorld.registerBehavior(std::move(behavior)).ok());
    auto rollbackAgent = rollbackWorld.createAgent("admission");
    REQUIRE(rollbackAgent.ok());
    auto rollbackTick = rollbackWorld.tick({1, 0.016, 1, 4});
    CHECK(!rollbackTick.ok());
    CHECK_EQ(rollbackObserved->starts, 1);
    CHECK_EQ(rollbackObserved->stops, 1);
}

TEST_CASE("npc_ai.archiveRestoreIsVersionedAtomicAndRestartsProviders") {
    NpcAiWorld world;
    auto       task     = std::make_unique<CountingTask>();
    auto*      observed = task.get();
    REQUIRE(world.registerTaskService("count", std::move(task)).ok());
    REQUIRE(world.registerBehavior(patrolBehavior()).ok());
    auto created = world.createAgent("guard");
    REQUIRE(created.ok());
    REQUIRE(world.setBlackboard(created.value(), "enemy", std::string("player")).ok());
    REQUIRE(world.tick({11, 0.016, 1, 4}).ok());
    CHECK_EQ(observed->starts, 1);

    auto saved = world.archive(created.value());
    REQUIRE(saved.ok());
    auto incompatible          = saved.value();
    incompatible.schemaVersion = 99;
    auto rejected              = world.restoreAgent(incompatible);
    CHECK(!rejected.ok());
    CHECK(!world.isStale(created.value()));

    auto restored = world.restoreAgent(saved.value());
    REQUIRE(restored.ok());
    CHECK_NE(restored.value(), created.value());
    auto restoredSnapshot = world.snapshot(restored.value());
    REQUIRE(restoredSnapshot.ok());
    CHECK_EQ(restoredSnapshot.value().lastTick, 11u);
    CHECK_EQ(restoredSnapshot.value().blackboard.at("enemy"), BlackboardValue(std::string("player")));
    REQUIRE(world.tick({12, 0.016, 2, 4}).ok());
    CHECK_EQ(observed->starts, 2);
}

TEST_CASE("npc_ai.largePopulationMakesBoundedRoundRobinProgress") {
    constexpr std::uint32_t Population = 2048;
    constexpr std::uint32_t Budget     = 128;
    NpcAiWorld              world({64, 0});
    BehaviorDefinition      behavior;
    behavior.id           = "crowd";
    behavior.initialState = "idle";
    behavior.states.push_back({"idle"});
    REQUIRE(world.registerBehavior(std::move(behavior)).ok());
    std::vector<AgentHandle> agents;
    agents.reserve(Population);
    for (std::uint32_t index = 0; index < Population; ++index) {
        auto created = world.createAgent("crowd");
        REQUIRE(created.ok());
        agents.push_back(created.value());
    }
    for (std::uint32_t batch = 0; batch < Population / Budget; ++batch) {
        auto report = world.tick({batch + 1, 1.0 / 30.0, Budget, 2});
        REQUIRE(report.ok());
        CHECK_EQ(report.value().agentsUpdated, Budget);
        CHECK_EQ(report.value().agentsDeferred, Population - Budget);
    }
    for (const auto agent : agents) {
        auto state = world.snapshot(agent);
        REQUIRE(state.ok());
        CHECK(state.value().lastTick > 0);
    }
}

TEST_CASE("npc_ai.transitionPriorityAppliesAcrossTheActiveHierarchy") {
    NpcAiWorld         world;
    BehaviorDefinition behavior;
    behavior.id           = "priority";
    behavior.initialState = "leaf";
    StateDefinition root;
    root.id = "root";
    root.transitions.push_back({"high", "choose", {}, 10});
    StateDefinition leaf;
    leaf.id     = "leaf";
    leaf.parent = "root";
    leaf.transitions.push_back({"low", "choose", {}, 1});
    behavior.states = {root, leaf, {"low"}, {"high"}};
    REQUIRE(world.registerBehavior(std::move(behavior)).ok());
    auto agent = world.createAgent("priority");
    REQUIRE(agent.ok());
    REQUIRE(world.signal(agent.value(), "choose").ok());
    REQUIRE(world.tick({1, 0.016, 1, 4}).ok());
    auto state = world.snapshot(agent.value());
    REQUIRE(state.ok());
    CHECK_EQ(state.value().activeState, std::string("high"));
}

TEST_CASE("npc_ai.startupRollbackUsesEachTaskStateContext") {
    NpcAiWorld world;
    auto       task     = std::make_unique<CountingTask>();
    auto*      observed = task.get();
    REQUIRE(world.registerTaskService("count", std::move(task)).ok());
    REQUIRE(world.registerTaskService("fail", std::make_unique<FailingStartTask>()).ok());
    BehaviorDefinition behavior;
    behavior.id           = "rollback-context";
    behavior.initialState = "leaf";
    StateDefinition root;
    root.id = "root";
    root.tasks.push_back({"parent-task", "count"});
    StateDefinition leaf;
    leaf.id     = "leaf";
    leaf.parent = "root";
    leaf.tasks.push_back({"leaf-task", "fail"});
    behavior.states = {root, leaf};
    REQUIRE(world.registerBehavior(std::move(behavior)).ok());
    auto agent = world.createAgent("rollback-context");
    REQUIRE(agent.ok());
    auto ticked = world.tick({1, 0.016, 1, 4});
    CHECK(!ticked.ok());
    REQUIRE_EQ(observed->stopStates.size(), 1u);
    CHECK_EQ(observed->stopStates.front(), std::string("root"));
}
