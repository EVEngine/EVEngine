#include "RtsCompositionFixtures.h"

TEST_CASE("rts.crowd.arrivalPreservesResolvedSeparation") {
    ecs::Table        world;
    ecs::ScopedTable  guard(world);
    eve::crowd::Crowd crowd;
    crowd.setSeparationWeight(0.f);
    std::array<Unit*, 2> units{Unit::createUnit(), Unit::createUnit()};
    for (size_t i = 0; i < units.size(); ++i) {
        auto* unit                    = units[i];
        unit->crowd()->link           = eve::rts::CrowdLink::bind("arrival/" + std::to_string(i)).takeValue();
        unit->crowd()->radius         = 0.5f;
        unit->motion()->x             = i == 0 ? -0.4f : 0.4f;
        unit->motion()->speed         = 0.f;
        unit->motion()->arrivalRadius = 2.f;
        CommandSpec move;
        move.kind   = OrderKind::Move;
        move.target = {0.f, 0.f};
        REQUIRE(unit->orders()->values.enqueue(move).ok());
    }
    const eve::SimulationStep step{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.01).expect("test dt")};
    REQUIRE(eve::rts::CrowdMotionSystem::step(step, crowd).ok());
    CHECK(units[0]->motion()->arrived);
    CHECK(units[1]->motion()->arrived);
    CHECK(std::abs(units[0]->motion()->x - units[1]->motion()->x) >= 0.999f);
    for (auto* unit : units) {
        const auto state = crowd.getAgentState(crowd.getNamedAgentIndex(unit->crowd()->link.key()));
        CHECK_EQ(state.x, unit->motion()->x);
        unit->release();
    }
}

TEST_CASE("rts.crowd.holdOrderResistsPushAndReplacementReleasesIt") {
    ecs::Table        world;
    ecs::ScopedTable  guard(world);
    eve::crowd::Crowd crowd;
    crowd.setSeparationWeight(0.f);
    crowd.setArriveRadius(1.f);
    Unit* held              = Unit::createUnit();
    Unit* neighbor          = Unit::createUnit();
    held->crowd()->link     = eve::rts::CrowdLink::bind("held").takeValue();
    neighbor->crowd()->link = eve::rts::CrowdLink::bind("neighbor").takeValue();
    held->crowd()->radius = neighbor->crowd()->radius = 1.f;
    neighbor->motion()->x                             = 1.f;
    neighbor->navigation()->movementPriority          = 100;
    CommandSpec hold;
    hold.kind = OrderKind::HoldPosition;
    REQUIRE(held->orders()->values.enqueue(hold).ok());
    const eve::SimulationStep step{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.1).expect("test dt")};
    REQUIRE(eve::rts::CrowdMotionSystem::step(step, crowd).ok());
    CHECK_EQ(held->motion()->x, 0.f);
    CHECK(neighbor->motion()->x >= 1.999f);
    auto current = held->orders()->values.current();
    REQUIRE(current.ok());
    REQUIRE(held->orders()->values.complete(current.value().id).ok());
    CommandSpec move;
    move.kind   = OrderKind::Move;
    move.target = {-10.f, 0.f};
    REQUIRE(held->orders()->values.enqueue(move).ok());
    REQUIRE(eve::rts::CrowdMotionSystem::step(step, crowd).ok());
    CHECK(held->motion()->x < 0.f);
    auto policy = crowd.getAgentInteraction(crowd.getNamedAgentIndex("held"));
    REQUIRE(policy.ok());
    CHECK(!policy.value().holdPosition);
    held->release();
    neighbor->release();
}

TEST_CASE("rts.crowd.predictiveCrossingSurvivesEcsProjection") {
    ecs::Table        world;
    ecs::ScopedTable  guard(world);
    eve::crowd::Crowd crowd;
    crowd.setSeparationWeight(0.f);
    crowd.setResolveOverlaps(false);
    crowd.setArriveRadius(1.f);
    REQUIRE(crowd.configureAvoidance({true, 2.f, 0.1f, 32}).ok());
    std::array<Unit*, 2> units{Unit::createUnit(), Unit::createUnit()};
    for (size_t i = 0; i < units.size(); ++i) {
        auto* unit                    = units[i];
        unit->crowd()->link           = eve::rts::CrowdLink::bind("crossing/" + std::to_string(i)).takeValue();
        unit->crowd()->radius         = 0.5f;
        unit->motion()->x             = i == 0 ? -8.f : 8.f;
        unit->motion()->speed         = 4.f;
        unit->motion()->arrivalRadius = 0.2f;
        CommandSpec move;
        move.kind   = OrderKind::Move;
        move.target = {i == 0 ? 8.f : -8.f, 0.f};
        REQUIRE(unit->orders()->values.enqueue(move).ok());
    }
    float minimum = 100.f;
    for (std::uint64_t tick = 0; tick < 600; ++tick) {
        const eve::SimulationStep step{eve::SimulationTick{tick},
                                       eve::Duration::fromSeconds(1.0 / 60.0).expect("test dt")};
        REQUIRE(eve::rts::CrowdMotionSystem::step(step, crowd).ok());
        minimum = std::min(minimum, std::hypot(units[0]->motion()->x - units[1]->motion()->x,
                                               units[0]->motion()->y - units[1]->motion()->y));
    }
    CHECK(minimum >= 0.99f);
    CHECK(units[0]->motion()->x > 7.8f);
    CHECK(units[1]->motion()->x < -7.8f);
    for (auto* unit : units) unit->release();
}

TEST_CASE("rts.crowd.stepBudgetFailureReturnsResult") {
    ecs::Table        world;
    ecs::ScopedTable  guard(world);
    eve::crowd::Crowd crowd;
    Unit*             unit = Unit::createUnit();
    unit->crowd()->link    = eve::rts::CrowdLink::bind("budget").takeValue();
    const eve::SimulationStep step{eve::SimulationTick{1}, eve::Duration::fromSeconds(10000.0).expect("test dt")};
    auto                      result = eve::rts::CrowdMotionSystem::step(step, crowd);
    CHECK(!result.ok());
    unit->release();
}

TEST_CASE("rts.crowd.containmentRemovesFootprintAndDisembarkRebuildsIt") {
    ecs::Table        world;
    ecs::ScopedTable  guard(world);
    eve::crowd::Crowd crowd;
    Unit*             passenger = Unit::createUnit();
    Unit*             transport = Unit::createUnit();
    passenger->crowd()->link    = eve::rts::CrowdLink::bind("passenger").takeValue();
    passenger->motion()->x      = 2.f;
    passenger->motion()->y      = 3.f;
    const eve::SimulationStep step{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.01).expect("test dt")};
    auto                      projected = eve::rts::CrowdMotionSystem::step(step, crowd);
    REQUIRE(projected.ok());
    CHECK(crowd.getNamedAgentIndex("passenger") >= 0);
    passenger->containment()->container = eve::rts::ContainerLink::bind(ecs::handle_of(transport)).takeValue();
    projected                           = eve::rts::CrowdMotionSystem::step(step, crowd);
    REQUIRE(projected.ok());
    CHECK_EQ(crowd.getNamedAgentIndex("passenger"), -1);
    passenger->containment()->container = {};
    passenger->motion()->x              = 7.f;
    passenger->motion()->y              = 8.f;
    projected                           = eve::rts::CrowdMotionSystem::step(step, crowd);
    REQUIRE(projected.ok());
    const auto index = crowd.getNamedAgentIndex("passenger");
    REQUIRE(index >= 0);
    const auto state = crowd.getAgentState(index);
    CHECK_EQ(state.x, 7.f);
    CHECK_EQ(state.y, 8.f);
    passenger->release();
    transport->release();
}

TEST_CASE("rts.crowd.convoyWaitDefersArrivalAndResumesExistingOrder") {
    ecs::Table        world;
    ecs::ScopedTable  guard(world);
    eve::crowd::Crowd crowd;
    crowd.setSeparationWeight(0.f);
    Unit* unit                    = Unit::createUnit();
    unit->crowd()->link           = eve::rts::CrowdLink::bind("waiting-convoy").takeValue();
    unit->motion()->speed         = 3.f;
    unit->motion()->arrivalRadius = 20.f;
    unit->supply()->convoyWaiting = true;
    CommandSpec move;
    move.kind    = OrderKind::Move;
    move.target  = {10.f, 0.f};
    auto ordered = unit->orders()->values.replace(move);
    REQUIRE(ordered.ok());
    const auto                orderId = ordered.value();
    const eve::SimulationStep step{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.1).expect("test dt")};
    auto                      advanced = eve::rts::CrowdMotionSystem::step(step, crowd);
    REQUIRE(advanced.ok());
    CHECK_EQ(unit->motion()->x, 0.f);
    CHECK(!unit->motion()->arrived);
    unit->supply()->convoyWaiting = false;
    unit->motion()->arrivalRadius = 0.1f;
    advanced                      = eve::rts::CrowdMotionSystem::step(step, crowd);
    REQUIRE(advanced.ok());
    CHECK(unit->motion()->x > 0.f);
    CHECK_EQ(unit->orders()->values.current().value().id, orderId);
    unit->release();
}
