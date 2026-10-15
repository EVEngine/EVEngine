#include "RtsCompositionFixtures.h"

TEST_CASE("rts.crowd.providerDestructionIsObservableAndSafeInBothOrders") {
    ecs::Table         world;
    ecs::ScopedTable   guard(world);
    PendingRTSExecutor executor;
    {
        eve::rts::RTS rts;
        auto          provider = std::make_unique<eve::crowd::Crowd>();
        rts.setCrowdProvider(provider.get());
        auto* unit          = rts.newUnit(subject("00000000-0000-7000-8000-000000000972")).takeValue();
        unit->crowd()->link = eve::rts::CrowdLink::bind("lifetime").takeValue();
        REQUIRE(rts.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}, executor).ok());
        CHECK(unit->crowd()->runtimeOwner != 0);
        provider.reset();
        REQUIRE(!rts.step({eve::SimulationTick{2}, eve::Duration::fromNanoseconds(10000000)}, executor).ok());
        CHECK_EQ(unit->crowd()->runtimeOwner, 0u);
        REQUIRE(!rts.relocateUnits({{{unit->identity()->subject, {3.f, 4.f}}}, 1000}).ok());
        rts.setCrowdProvider(nullptr);
    }
    {
        eve::crowd::Crowd provider;
        auto              rts = std::make_unique<eve::rts::RTS>();
        rts->setCrowdProvider(&provider);
        auto* unit          = rts->newUnit(subject("00000000-0000-7000-8000-000000000973")).takeValue();
        unit->crowd()->link = eve::rts::CrowdLink::bind("lifetime-other-order").takeValue();
        REQUIRE(rts->step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}, executor).ok());
        rts.reset();
        CHECK_EQ(provider.getAgentCount(), 0);
    }
}

TEST_CASE("rts.crowd.invalidClockRejectsBeforeBindingOrIntentMutation") {
    ecs::Table        world;
    ecs::ScopedTable  guard(world);
    eve::crowd::Crowd crowd;
    auto*             unit = Unit::createUnit();
    unit->crowd()->link    = eve::rts::CrowdLink::bind("clock-unit").takeValue();
    unit->motion()->speed  = 0.f;
    eve::SimulationStep step{eve::SimulationTick{2}, eve::Duration::fromNanoseconds(10000000)};
    REQUIRE(!eve::rts::CrowdMotionSystem::step(step, crowd).ok());
    CHECK_EQ(crowd.getAgentCount(), 0);
    CHECK_EQ(unit->crowd()->runtimeOwner, 0u);
    step.tick = eve::SimulationTick{1};
    REQUIRE(eve::rts::CrowdMotionSystem::step(step, crowd).ok());
    const eve::crowd::AgentRef ref{unit->crowd()->runtimeAgent, unit->crowd()->runtimeOwner};
    const auto                 before = crowd.readAgent(ref).takeValue();
    unit->motion()->speed             = 10.f;
    unit->crowd()->link               = eve::rts::CrowdLink::bind("clock-rebound").takeValue();
    REQUIRE(!eve::rts::CrowdMotionSystem::step(step, crowd).ok());
    CHECK(crowd.isAgentAlive(ref));
    CHECK(!crowd.hasNamedAgent("clock-rebound"));
    CHECK_EQ(crowd.readAgent(ref).value().x, before.x);
    step.tick = eve::SimulationTick{2};
    REQUIRE(eve::rts::CrowdMotionSystem::step(step, crowd).ok());
    CHECK(!crowd.isAgentAlive(ref));
    unit->release();
}

TEST_CASE("rts.crowd.scriptRelocationUsesCheckedOwnerPlacement") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.addFunc("createRelocationUnit", [](eve::rts::RTS* rts) {
        auto created = rts->newUnit(subject("00000000-0000-7000-8000-000000000971"));
        if (!created) return false;
        created.value()->motion()->x     = 2.f;
        created.value()->motion()->y     = 2.f;
        created.value()->motion()->speed = 0.f;
        return true;
    });
    vm.addFunc("relocationX", [](eve::rts::RTS* rts) {
        return rts->findUnit(subject("00000000-0000-7000-8000-000000000971"))->motion()->x;
    });
    vm.run(vm.compileSource(R"(
        local sim = eve.RTS();
        assert(sim.configureScriptWorld(32,32,1.0,0.0,0.0).ok);
        assert(createRelocationUnit(sim));
        assert(sim.stepScript(0.01).ok);
        local id = "00000000-0000-7000-8000-000000000971";
        assert(sim.relocateUnits([{subject=id,x=8.0,y=8.0}],10000).ok);
        assert(relocationX(sim) == 8.0);
        assert(!sim.relocateUnits([{subject=id,x=9.0,y=8.0},{subject=id,x=10.0,y=8.0}],10000).ok);
        assert(!sim.relocateUnits([{subject="invalid",x=9.0,y=8.0}],10000).ok);
        assert(relocationX(sim) == 8.0);
    )"));
}

TEST_CASE("rts.crowd.cachedProjectionUsesCanonicalStateAfterSwapPop") {
    ecs::Table        world;
    ecs::ScopedTable  guard(world);
    eve::crowd::Crowd crowd;
    const int         unrelated = crowd.addNamedAgent("unrelated", 100.f, 0.f, 0.f, 1.f);
    auto*             unit      = Unit::createUnit();
    unit->crowd()->link         = eve::rts::CrowdLink::bind("cached").takeValue();
    unit->motion()->speed       = 0.f;
    const eve::SimulationStep step{eve::SimulationTick{0}, eve::Duration{}};
    REQUIRE(eve::rts::CrowdMotionSystem::step(step, crowd).ok());
    const eve::crowd::AgentRef reference{unit->crowd()->runtimeAgent, unit->crowd()->runtimeOwner};
    REQUIRE(crowd.removeAgent(unrelated));
    REQUIRE(crowd.setAgentPosition(crowd.getNamedAgentIndex("cached"), 7.f, 3.f));
    unit->motion()->x = 99.f;  // An old ECS projection must not overwrite the movement owner.
    REQUIRE(eve::rts::CrowdMotionSystem::step(step, crowd).ok());
    CHECK_EQ(unit->crowd()->runtimeAgent, reference.handle);
    CHECK_EQ(unit->motion()->x, 7.f);
    CHECK_EQ(unit->motion()->y, 3.f);
    unit->release();
}

TEST_CASE("rts.crowd.rebindingAndUnlinkInvalidateOldProjection") {
    ecs::Table        world;
    ecs::ScopedTable  guard(world);
    eve::crowd::Crowd crowd;
    auto*             unit = Unit::createUnit();
    unit->crowd()->link    = eve::rts::CrowdLink::bind("old").takeValue();
    const eve::SimulationStep step{eve::SimulationTick{0}, eve::Duration{}};
    REQUIRE(eve::rts::CrowdMotionSystem::step(step, crowd).ok());
    const eve::crowd::AgentRef old{unit->crowd()->runtimeAgent, unit->crowd()->runtimeOwner};
    unit->crowd()->link = eve::rts::CrowdLink::bind("new").takeValue();
    REQUIRE(eve::rts::CrowdMotionSystem::step(step, crowd).ok());
    CHECK(!crowd.isAgentAlive(old));
    CHECK(!crowd.hasNamedAgent("old"));
    CHECK(crowd.hasNamedAgent("new"));
    unit->crowd()->link = {};
    REQUIRE(eve::rts::CrowdMotionSystem::step(step, crowd).ok());
    CHECK_EQ(crowd.getAgentCount(), 0);
    CHECK_EQ(unit->crowd()->runtimeOwner, 0u);
    unit->release();
}

TEST_CASE("rts.crowd.duplicateBindingNamesRejectBeforeCreation") {
    ecs::Table        world;
    ecs::ScopedTable  guard(world);
    eve::crowd::Crowd crowd;
    auto*             a = Unit::createUnit();
    auto*             b = Unit::createUnit();
    a->crowd()->link    = eve::rts::CrowdLink::bind("duplicate").takeValue();
    b->crowd()->link    = eve::rts::CrowdLink::bind("duplicate").takeValue();
    const eve::SimulationStep step{eve::SimulationTick{0}, eve::Duration{}};
    REQUIRE(!eve::rts::CrowdMotionSystem::step(step, crowd).ok());
    CHECK_EQ(crowd.getAgentCount(), 0);
    a->release();
    b->release();
}
