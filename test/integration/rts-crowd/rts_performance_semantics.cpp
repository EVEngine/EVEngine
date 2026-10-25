// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "module": "rts",
  "origins": [
    "rts_performance_semantics.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"
#include <array>
#include <set>
#include "RtsCompositionFixtures.h"
#include "map/Fov.h"
#include "rts/RTSTypes.h"
#include "sensing/Sensing.h"
#include "spatial/SpatialHash2D.h"
#include "zeroerr.h"

TEST_CASE("rts.performanceSpatialQueriesMatchBruteForceAcrossNegativeAndRepeatedBuckets") {
    eve::spatial::SpatialHash2D grid(2.f);
    const std::array            boxes{eve::spatial::makeAABB2(-8.f, -6.f, 6.f, 8.f),
                           eve::spatial::makeAABB2(-3.f, -2.f, -1.f, 0.f), eve::spatial::makeAABB2(0.f, 0.f, 0.f, 0.f),
                           eve::spatial::makeAABB2(1.f, -4.f, 3.f, -1.f)};
    for (int i = 0; i < 4; ++i) REQUIRE(grid.insert(i, boxes[i].minX, boxes[i].minY, boxes[i].maxX, boxes[i].maxY));
    for (int y = -9; y <= 9; ++y)
        for (int x = -9; x <= 9; ++x) {
            grid.queryCircle(float(x), float(y), 2.5f);
            std::set<int> expected, actual;
            for (int i = 0; i < 4; ++i)
                if (boxes[i].intersectsCircle(float(x), float(y), 2.5f)) expected.insert(i);
            for (int i = 0; i < grid.getResultCount(); ++i) REQUIRE(actual.insert(grid.getResultId(i)).second);
            CHECK(actual == expected);
        }
    REQUIRE(grid.update(0, 12.f, 12.f, 13.f, 13.f));
    grid.queryPoint(0.f, 0.f);
    CHECK_EQ(grid.getResultCount(), 1);
    CHECK_EQ(grid.getResultId(0), 2);
    eve::spatial::SpatialHash2D reference(2.f);
    grid.clear();
    for (int id = 0; id < 4; ++id) {
        REQUIRE(grid.insert(id, -1.f, -1.f, 3.f, 3.f));
        REQUIRE(reference.insert(id, -1.f, -1.f, 3.f, 3.f));
    }
    for (int id : {2, 0, 3, 1, 2}) {
        REQUIRE(grid.update(id, -0.5f, -0.5f, 2.5f, 2.5f));
        REQUIRE(reference.insert(id, -0.5f, -0.5f, 2.5f, 2.5f));
        for (float x : {-1.f, 0.f, 2.5f, 3.f}) {
            grid.queryCircle(x, 0.f, 3.f);
            reference.queryCircle(x, 0.f, 3.f);
            CHECK_EQ(grid.getResultCount(), reference.getResultCount());
            for (int index = 0; index < grid.getResultCount(); ++index)
                CHECK_EQ(grid.getResultId(index), reference.getResultId(index));
        }
    }
}

TEST_CASE("rts.performanceFovIndexSurvivesOrderedRemovalMoveAndClear") {
    eve::map::Fov fov(40, 20);
    const int first = fov.addRevealer(2, 2, 1), middle = fov.addRevealer(10, 2, 1), last = fov.addRevealer(20, 2, 1);
    fov.removeRevealer(middle);
    fov.setRevealerPosition(last, 30, 2);
    fov.compute();
    REQUIRE(fov.isVisible(2, 2));
    REQUIRE(fov.isVisible(30, 2));
    REQUIRE(!fov.isVisible(10, 2));
    eve::map::Fov moved(std::move(fov));
    moved.removeRevealer(first);
    moved.setRevealerRadius(last, 2);
    moved.compute();
    REQUIRE(!moved.isVisible(2, 2));
    REQUIRE(moved.isVisible(31, 2));
    moved.clearRevealers();
    moved.setRevealerPosition(last, 1, 1);
    const int fresh = moved.addRevealer(5, 5, 1);
    REQUIRE(fresh != last);
    moved.compute();
    REQUIRE(moved.isVisible(5, 5));
    REQUIRE(!moved.isVisible(30, 2));
}


TEST_CASE("rts.performanceOrderProjectionInvalidatesOnLifecycleAndSameIdRestore") {
    eve::rts::OrderComponent orders;
    eve::rts::CommandSpec    first;
    first.kind   = eve::rts::OrderKind::AttackMove;
    first.target = {3.f, 4.f};
    REQUIRE(orders.enqueue(first, 5).ok());
    for (int i = 0; i < 3; ++i) {
        auto current = orders.current();
        REQUIRE(current.ok());
        CHECK_EQ(current.value().target.x, 3.f);
        CHECK_EQ(current.value().formationSlot, 5);
    }
    eve::rts::OrderComponent restored;
    first.target = {7.f, 8.f};
    REQUIRE(restored.enqueue(first, 9).ok());
    auto snapshot = restored.snapshotState();
    REQUIRE(snapshot.ok());
    REQUIRE(orders.restoreState(snapshot.value()).ok());
    auto current = orders.current();
    REQUIRE(current.ok());
    CHECK_EQ(current.value().target.x, 7.f);
    CHECK_EQ(current.value().formationSlot, 9);
    REQUIRE(orders.restore(snapshot.value().queueJson).ok());
    current = orders.current();
    REQUIRE(current.ok());
    CHECK_EQ(current.value().target.x, 7.f);
    eve::rts::OrderComponent copied(orders);
    first.target = {11.f, 12.f};
    REQUIRE(copied.enqueue(first, 2).ok());
    REQUIRE(copied.complete(copied.current().expect("active").id).ok());
    current = copied.current();
    REQUIRE(current.ok());
    CHECK_EQ(current.value().target.x, 11.f);
    copied.clear();
    REQUIRE(copied.enqueue(first, 3).ok());
    const auto afterClear = copied.current().expect("after clear");
    CHECK_EQ(afterClear.formationSlot, 3);
    REQUIRE(copied.replace(first, 4).ok());
    const auto afterReplace = copied.current().expect("after replace");
    CHECK_EQ(afterReplace.formationSlot, 4);
}

TEST_CASE("rts.performanceSensingNodeIndexSurvivesUpsertEraseRestoreAndMove") {
    eve::sensing::SensingWorld world;
    REQUIRE(world.setSpatialIndexEnabled(true, 2.f).ok());
    REQUIRE(world.upsert("a", 1.f, 1.f, "blue", "unit", "").ok());
    REQUIRE(world.upsert("b", 4.f, 1.f, "red", "unit", "").ok());
    eve::sensing::QuerySpec spec;
    spec.shape       = eve::sensing::QueryCircle{0.f, 0.f, 10.f};
    spec.maxRange    = 10.f;
    spec.maxCount    = 10;
    spec.countPolicy = eve::sensing::CountPolicy::TruncateToMax;
    auto result      = world.query({0.f, 0.f, std::nullopt}, spec);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().size(), 2);
    REQUIRE(world.upsert("b", 4.f, 1.f, "blue", "building", "").ok());
    spec.requiredTags = {"unit"};
    result            = world.query({0.f, 0.f, std::nullopt}, spec);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().size(), 1);
    spec.requiredTags.clear();
    REQUIRE(world.upsert("b", 30.f, 1.f, "red", "unit", "").ok());
    result = world.query({0.f, 0.f, std::nullopt}, spec);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().size(), 1);
    const auto snapshot = world.snapshotJson();
    REQUIRE(world.remove("a").ok());
    result = world.query({0.f, 0.f, std::nullopt}, spec);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().size(), 0);
    REQUIRE(world.restoreJson(snapshot).ok());
    eve::sensing::SensingWorld moved(std::move(world));
    result = moved.query({0.f, 0.f, std::nullopt}, spec);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().size(), 1);
    CHECK_EQ(result.value().ranked()[0].id, "a");
    REQUIRE(moved.setSpatialIndexEnabled(false, 2.f).ok());
    REQUIRE(moved.setSpatialIndexEnabled(true, 3.f).ok());
    result = moved.query({0.f, 0.f, std::nullopt}, spec);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().size(), 1);
}

TEST_CASE("rts.performanceOpenHeightmapMatchesHeightRayPathAndInvalidatesOnEdits") {
    eve::map::Fov fast(50, 50), reference(50, 50);
    for (auto* fov : {&fast, &reference}) {
        fov->setMode("heightmap");
        fov->setEyeOffset(1.f);
        fov->setCliffBlock(0.f);
        fov->addRevealer(10, 10, 8);
        fov->addRevealer(15, 14, 6);
    }
    // Outside every revealer's radius; forces the reference through ordinary height rays.
    reference.setOpaque(49, 49, true);
    for (int stage = 0; stage < 3; ++stage) {
        if (stage == 1)
            for (auto* fov : {&fast, &reference}) fov->setElevation(12, 10, 4.f);
        if (stage == 2)
            for (auto* fov : {&fast, &reference}) fov->setOpaque(10, 12, true);
        fast.compute();
        reference.compute();
        for (int y = 0; y < 50; ++y)
            for (int x = 0; x < 50; ++x) REQUIRE(fast.isVisible(x, y) == reference.isVisible(x, y));
    }
}

TEST_CASE("rts.performanceCasualtyBatchRepairsOwnedReferencesWithoutCrowdProvider") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    sim;
    auto*            faction = sim.newFaction(subject("00000000-0000-7000-8000-00000000ba01")).expect("faction");
    auto* dead     = sim.newFactionUnit(*faction, subject("00000000-0000-7000-8000-00000000ba02")).expect("dead");
    auto* survivor = sim.newFactionUnit(*faction, subject("00000000-0000-7000-8000-00000000ba03")).expect("survivor");
    auto* building =
        sim.newFactionBuilding(*faction, subject("00000000-0000-7000-8000-00000000ba04")).expect("building");
    auto*      player     = sim.newPlayer(subject("00000000-0000-7000-8000-00000000ba05")).expect("player");
    const auto deadHandle = ecs::handle_of(dead), aliveHandle = ecs::handle_of(survivor);
    survivor->combat()->target         = deadHandle;
    survivor->tactics()->escortTarget  = deadHandle;
    survivor->supply()->assignedTarget = deadHandle;
    survivor->supply()->convoyLeader   = deadHandle;
    building->garrison()->occupants    = {deadHandle};
    building->garrison()->capacity     = 1;
    dead->containment()->container     = eve::rts::ContainerLink::bind(ecs::handle_of(building)).expect("container");
    building->construction()->builders = {deadHandle, aliveHandle};
    building->rally()->reinforcements  = {deadHandle, aliveHandle};
    building->rally()->transport       = deadHandle;
    building->rally()->transportActive = true;
    building->capture()->blockedByGarrison = true;
    player->selection()->units             = {deadHandle, aliveHandle};
    dead->durability()->alive              = false;
    dead->durability()->state.health       = 0;
    const auto cleanupCount                = sim.cleanupDestroyed().expect("cleanup");
    REQUIRE_EQ(cleanupCount, 1);
    REQUIRE(ecs::try_get(deadHandle) == nullptr);
    REQUIRE(ecs::try_get(aliveHandle) == survivor);
    REQUIRE(survivor->combat()->target.table == nullptr);
    REQUIRE(survivor->tactics()->escortTarget.table == nullptr);
    REQUIRE(survivor->supply()->assignedTarget.table == nullptr);
    REQUIRE(survivor->supply()->convoyLeader.table == nullptr);
    REQUIRE(building->garrison()->occupants.empty());
    REQUIRE(!building->capture()->blockedByGarrison);
    REQUIRE(!building->rally()->transportActive);
    REQUIRE_EQ(building->construction()->builders.size(), 1);
    REQUIRE_EQ(building->rally()->reinforcements.size(), 1);
    REQUIRE_EQ(player->selection()->units.size(), 1);
    REQUIRE_EQ(faction->members()->units.size(), 1);
    const auto emptycleanupCount = sim.cleanupDestroyed().expect("empty cleanup");
    REQUIRE_EQ(emptycleanupCount, 0);
}

TEST_CASE("rts.performanceCasualtyBatchCrowdFailureLeavesReferencesAndRootsIntact") {
    ecs::Table        world;
    ecs::ScopedTable  guard(world);
    eve::crowd::Crowd provider;
    eve::rts::RTS     sim;
    sim.setCrowdProvider(&provider);
    auto* first           = sim.newUnit(subject("00000000-0000-7000-8000-00000000bb01")).expect("first");
    auto* second          = sim.newUnit(subject("00000000-0000-7000-8000-00000000bb02")).expect("second");
    first->crowd()->link  = eve::rts::CrowdLink::bind("first").expect("link");
    second->crowd()->link = eve::rts::CrowdLink::bind("second").expect("link");
    second->motion()->x   = 4.f;
    PendingRTSExecutor executor;
    REQUIRE(sim.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(50000000)}, executor).ok());
    const eve::crowd::AgentRef original{second->crowd()->runtimeAgent, second->crowd()->runtimeOwner};
    const auto                 target = ecs::handle_of(first);
    second->combat()->target          = target;
    first->durability()->alive = second->durability()->alive = false;
    first->durability()->state.health = second->durability()->state.health = 0;
    second->crowd()->runtimeAgent                                          = first->crowd()->runtimeAgent;
    auto rejected                                                          = sim.cleanupDestroyed();
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.code(), eve::StatusCode::Conflict);
    CHECK_EQ(sim.unitCount(), 2);
    CHECK_EQ(provider.getAgentCount(), 2);
    CHECK_EQ(second->combat()->target.table, target.table);
    CHECK_EQ(second->combat()->target.id, target.id);
    CHECK_EQ(second->combat()->target.generation, target.generation);
    second->crowd()->runtimeAgent = original.handle;
    const auto validbatchCount    = sim.cleanupDestroyed().expect("valid batch");
    CHECK_EQ(validbatchCount, 2);
    CHECK_EQ(provider.getAgentCount(), 0);
    CHECK(!provider.isAgentAlive(original));
}

TEST_CASE("rts.performanceNestedPassengerCleanupKeepsRecursiveLifecycle") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    sim;
    auto*            carrier         = sim.newUnit(subject("00000000-0000-7000-8000-00000000bc01")).expect("carrier");
    auto*            passenger       = sim.newUnit(subject("00000000-0000-7000-8000-00000000bc02")).expect("passenger");
    carrier->containment()->capacity = 1;
    carrier->containment()->occupants   = {ecs::handle_of(passenger)};
    passenger->containment()->container = eve::rts::ContainerLink::bind(ecs::handle_of(carrier)).expect("container");
    const auto carrierHandle = ecs::handle_of(carrier), passengerHandle = ecs::handle_of(passenger);
    carrier->durability()->alive        = false;
    carrier->durability()->state.health = 0;
    const auto recursivecleanupCount    = sim.cleanupDestroyed().expect("recursive cleanup");
    REQUIRE_EQ(recursivecleanupCount, 2);
    REQUIRE(ecs::try_get(carrierHandle) == nullptr);
    REQUIRE(ecs::try_get(passengerHandle) == nullptr);
    REQUIRE_EQ(sim.unitCount(), 0);
}
