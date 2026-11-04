// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "concurrency",
  "cases": {
    "script-binding": [
      "rts.thread.scriptCrowdExecutionRequiresConfiguredWorldAndScheduler"
    ]
  },
  "module": "rts",
  "origins": [
    "CrowdMotion.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"
#include <stdexcept>
#include "../../RtsCompositionFixtures.h"
#include "crowd/CrowdWorkScheduler.h"
#include "crowd/thread/CrowdThread.h"
#include "rts/RTSCrowdFlowNavigationInternal.h"
#include "thread/JobSystem.h"
using namespace eve;
using namespace eve::crowd;
TEST_CASE("rts.thread.scriptCrowdExecutionRequiresConfiguredWorldAndScheduler") {
    ecs::Table       world;
    ecs::ScopedTable scope(world);
    rts::RTS         sim;
    REQUIRE(!sim.configureScriptCrowdExecution(CrowdExecution::Parallel, {}).ok());
    REQUIRE(sim.configureScriptWorld(16, 16, 1.f).ok());
    REQUIRE(!sim.configureScriptCrowdExecution(CrowdExecution::Parallel, {}).ok());
    auto jobs = std::shared_ptr<thread::JobSystem>(thread::createJobSystem(4));
    auto created = crowd_thread::makeJobSystemCrowdScheduler(jobs);
    REQUIRE(created.ok());
    REQUIRE(sim.configureScriptCrowdExecution(CrowdExecution::Parallel, created.value()).ok());
    REQUIRE(sim.stepScript(0.05).ok());
}
namespace {
struct Observation {
    AgentState state;
    float      x, y, heading;
    bool       arrived;
};
std::vector<Unit*> units() {
    std::vector<Unit*> result;
    for (int i = 0; i < 300; ++i) {
        auto* unit                    = Unit::createUnit();
        unit->crowd()->link           = rts::CrowdLink::bind("parallel-unit/" + std::to_string(i)).takeValue();
        unit->crowd()->radius         = 0.4f;
        unit->motion()->x             = float(i % 30) * 0.5f;
        unit->motion()->y             = float(i / 30) * 0.5f;
        unit->motion()->speed         = 4.f;
        unit->motion()->arrivalRadius = 0.2f;
        CommandSpec order;
        order.kind   = i % 17 == 0 ? OrderKind::HoldPosition : i % 2 ? OrderKind::Move : OrderKind::AttackMove;
        order.target = {i % 2 ? -20.f : 50.f, float(i / 30)};
        REQUIRE(unit->orders()->values.enqueue(order).ok());
        result.push_back(unit);
    }
    return result;
}
void configure(Crowd& crowd) {
    crowd.setClampToField(false);
    crowd.setSeparationRadius(2.f);
    crowd.setPerceptionRadius(3.f);
    REQUIRE(crowd.configureAvoidance({true, 2.f, 0.1f, 16}).ok());
}
Observation observe(Unit& unit, Crowd& crowd) {
    auto state = crowd.readAgent({unit.crowd()->runtimeAgent, unit.crowd()->runtimeOwner});
    REQUIRE(state.ok());
    CHECK_EQ(state.value().x, unit.motion()->x);
    CHECK_EQ(state.value().y, unit.motion()->y);
    CHECK_EQ(state.value().heading, unit.crowd()->heading);
    return {state.value(), unit.motion()->x, unit.motion()->y, unit.crowd()->heading, unit.motion()->arrived};
}
void equal(const Observation& a, const Observation& b) {
    CHECK_EQ(a.x, b.x);
    CHECK_EQ(a.y, b.y);
    CHECK_EQ(a.heading, b.heading);
    CHECK_EQ(a.arrived, b.arrived);
    CHECK_EQ(a.state.vx, b.state.vx);
    CHECK_EQ(a.state.vy, b.state.vy);
    CHECK_EQ(a.state.speed, b.state.speed);
    CHECK_EQ(a.state.action, b.state.action);
}
std::vector<Observation> run(std::shared_ptr<CrowdWorkScheduler> scheduler) {
    ecs::Table       world;
    ecs::ScopedTable scope(world);
    Crowd            crowd;
    configure(crowd);
    if (scheduler) REQUIRE(crowd.setWorkScheduler(CrowdExecution::Parallel, std::move(scheduler)).ok());
    auto                     actors = units();
    std::vector<Observation> result;
    for (int tick = 1; tick <= 3; ++tick) {
        const SimulationStep step{SimulationTick(tick), Duration::fromNanoseconds(10000000)};
        auto                 moved = rts::CrowdMotionSystem::step(step, crowd);
        REQUIRE(moved.ok());
        CHECK_EQ(moved.value(), actors.size());
        for (auto* actor : actors) result.push_back(observe(*actor, crowd));
    }
    for (auto* actor : actors) actor->release();
    return result;
}
class FailGather final : public CrowdWorkScheduler {
public:
    explicit FailGather(std::shared_ptr<CrowdWorkScheduler> inner) : inner_(std::move(inner)) {}
    Result<void> execute(int partitions, const std::function<void(int)>& body) override {
        ++calls;
        return inner_->execute(partitions, [&](int index) {
            body(index);
            if (calls == 4 && index == 0) throw std::runtime_error("RTS gather injection");
        });
    }
    int calls = 0;

private:
    std::shared_ptr<CrowdWorkScheduler> inner_;
};
}  // namespace
TEST_CASE("rts.crowd.parallelConsumerMatchesSerialMotionAndArrival") {
    auto jobs    = std::shared_ptr<thread::JobSystem>(thread::createJobSystem(3));
    auto created = crowd_thread::makeJobSystemCrowdScheduler(jobs);
    REQUIRE(created.ok());
    auto serial = run({}), parallel = run(created.value());
    REQUIRE(serial.size() == parallel.size());
    for (size_t i = 0; i < serial.size(); ++i) equal(serial[i], parallel[i]);
}
TEST_CASE("rts.crowd.parallelConsumerRejectsExpiredProviderWithoutPublishingMotion") {
    ecs::Table       world;
    ecs::ScopedTable scope(world);
    Crowd            crowd;
    configure(crowd);
    auto                 actors = units();
    const SimulationStep first{SimulationTick(1), Duration::fromNanoseconds(10000000)};
    REQUIRE(rts::CrowdMotionSystem::step(first, crowd).ok());
    std::vector<Observation> before;
    for (auto* actor : actors) before.push_back(observe(*actor, crowd));
    auto jobs    = std::shared_ptr<thread::JobSystem>(thread::createJobSystem(3));
    auto created = crowd_thread::makeJobSystemCrowdScheduler(jobs);
    REQUIRE(created.ok());
    REQUIRE(crowd.setWorkScheduler(CrowdExecution::Parallel, created.value()).ok());
    jobs.reset();
    const SimulationStep second{SimulationTick(2), Duration::fromNanoseconds(10000000)};
    REQUIRE(!rts::CrowdMotionSystem::step(second, crowd).ok());
    for (size_t i = 0; i < actors.size(); ++i) equal(before[i], observe(*actors[i], crowd));
    REQUIRE(crowd.setWorkScheduler(CrowdExecution::Serial, {}).ok());
    REQUIRE(rts::CrowdMotionSystem::step(second, crowd).ok());
    for (auto* actor : actors) actor->release();
}
TEST_CASE("rts.crowd.partialParallelGatherRollsBackBothConsumerPoses") {
    ecs::Table       world;
    ecs::ScopedTable scope(world);
    Crowd            crowd;
    configure(crowd);
    crowd.setResolveOverlaps(false);
    auto actors = units();
    REQUIRE(rts::CrowdMotionSystem::step({SimulationTick(1), Duration::fromNanoseconds(10000000)}, crowd).ok());
    std::vector<Observation> before;
    for (auto* actor : actors) before.push_back(observe(*actor, crowd));
    CHECK(std::hypot(actors[0]->motion()->x - actors[1]->motion()->x, actors[0]->motion()->y - actors[1]->motion()->y) <
          0.7f);
    crowd.setResolveOverlaps(true);
    auto jobs    = std::shared_ptr<thread::JobSystem>(thread::createJobSystem(3));
    auto created = crowd_thread::makeJobSystemCrowdScheduler(jobs);
    REQUIRE(created.ok());
    auto injected = std::make_shared<FailGather>(created.value());
    REQUIRE(crowd.setWorkScheduler(CrowdExecution::Parallel, injected).ok());
    const SimulationStep second{SimulationTick(2), Duration::fromNanoseconds(10000000)};
    REQUIRE(!rts::CrowdMotionSystem::step(second, crowd).ok());
    CHECK_EQ(injected->calls, 4);
    for (size_t i = 0; i < actors.size(); ++i) equal(before[i], observe(*actors[i], crowd));
    REQUIRE(crowd.setWorkScheduler(CrowdExecution::Parallel, created.value()).ok());
    REQUIRE(rts::CrowdMotionSystem::step(second, crowd).ok());
    for (auto* actor : actors) {
        observe(*actor, crowd);
        actor->release();
    }
}

TEST_CASE("rts.crowd.pendingSharedFlowFailurePreservesArrivalUntilPoseCommit") {
    ecs::Table                world;
    ecs::ScopedTable          scope(world);
    Crowd                     crowd;
    NavigationSurfaceSnapshot input;
    input.stamp    = {NavigationSurfaceId(1), MovementClassId(1), 1, 1, 1, 0.25f};
    input.width    = 5;
    input.height   = 3;
    input.cellSize = 1.f;
    input.terrainCosts.assign(15, 1.f);
    input.navigationCosts.assign(15, 1.f);
    REQUIRE(crowd.commitNavigationSurface(crowd.prepareNavigationSurface(input).takeValue()).ok());
    rts::CrowdFlowNavigation navigation({1, 1});
    auto*                    actor = Unit::createUnit();
    actor->crowd()->link           = rts::CrowdLink::bind("pending-failure").takeValue();
    actor->crowd()->radius         = 0.25f;
    actor->motion()->x             = 0.5f;
    actor->motion()->y             = 1.5f;
    actor->motion()->speed         = 1.f;
    actor->motion()->arrived       = true;
    CommandSpec move;
    move.kind   = OrderKind::Move;
    move.target = {4.5f, 1.5f};
    REQUIRE(actor->orders()->values.enqueue(move).ok());
    auto jobs    = std::shared_ptr<thread::JobSystem>(thread::createJobSystem(2));
    auto created = crowd_thread::makeJobSystemCrowdScheduler(jobs);
    REQUIRE(created.ok());
    REQUIRE(crowd.setWorkScheduler(CrowdExecution::Parallel, created.value()).ok());
    jobs.reset();
    const SimulationStep step{SimulationTick(1), Duration::fromNanoseconds(10000000)};
    REQUIRE(!rts::CrowdMotionSystem::stepSharedFlow(step, crowd, navigation).ok());
    CHECK(actor->motion()->arrived);
    CHECK_EQ(actor->motion()->x, 0.5f);
    CHECK_EQ(actor->motion()->y, 1.5f);
    REQUIRE(crowd.setWorkScheduler(CrowdExecution::Serial, {}).ok());
    REQUIRE(rts::CrowdMotionSystem::stepSharedFlow(step, crowd, navigation).ok());
    CHECK(!actor->motion()->arrived);
    actor->release();
}

TEST_CASE("rts.crowd.dynamicArrivalObservesCompleteNewPoseSnapshot") {
    auto jobs      = std::shared_ptr<thread::JobSystem>(thread::createJobSystem(3));
    auto scheduler = crowd_thread::makeJobSystemCrowdScheduler(jobs);
    REQUIRE(scheduler.ok());
    for (bool leaderFirst : {false, true}) {
        ecs::Table       world;
        ecs::ScopedTable scope(world);
        Crowd            crowd;
        crowd.setClampToField(false);
        crowd.setResolveOverlaps(false);
        crowd.setSeparationWeight(0.f);
        crowd.setArriveRadius(1.f);
        REQUIRE(crowd.setWorkScheduler(CrowdExecution::Parallel, scheduler.value()).ok());
        auto* first             = Unit::createUnit();
        auto* second            = Unit::createUnit();
        auto* leader            = leaderFirst ? first : second;
        auto* follower          = leaderFirst ? second : first;
        leader->crowd()->link   = rts::CrowdLink::bind("leader").takeValue();
        follower->crowd()->link = rts::CrowdLink::bind("follower").takeValue();
        for (auto* actor : {leader, follower}) {
            actor->motion()->speed = 4.f;
            actor->crowd()->radius = 0.01f;
        }
        leader->motion()->x                 = 0.1f;
        follower->motion()->x               = 0.f;
        follower->motion()->arrivalRadius   = 0.1f;
        follower->combat()->engagementRange = 0.f;
        CommandSpec move;
        move.kind   = OrderKind::Move;
        move.target = {10.f, 0.f};
        REQUIRE(leader->orders()->values.enqueue(move).ok());
        CommandSpec attack;
        attack.kind         = OrderKind::Attack;
        attack.targetEntity = leader->identity()->self;
        REQUIRE(follower->orders()->values.enqueue(attack).ok());
        REQUIRE(rts::CrowdMotionSystem::step({SimulationTick(1), Duration::fromNanoseconds(10000000)}, crowd).ok());
        CHECK(leader->motion()->x - follower->motion()->x > 0.1f);
        CHECK(!follower->motion()->arrived);
        observe(*leader, crowd);
        observe(*follower, crowd);
        leader->release();
        follower->release();
    }
}
