// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "module": "rts",
  "origins": [
    "rts_crowd.cpp",
    "rts_crowd_production.cpp",
    "rts_formation.cpp",
    "rts_groups.cpp",
    "rts_orders.cpp",
    "rts_production.cpp",
    "rts_restore.cpp"
  ],
  "cases": {
    "script-binding": [
      "rts.formation.scriptCommandsReachCanonicalOrdersAndReplay",
      "rts.groups.scriptAndReplayUseCanonicalAdmission"
    ]
  }
}
*/
// clang-format on

// Related integration contracts share one translation unit to amortize header parsing.

// Contract section: rts crowd
#include "PrivateTestScope.h"
#include "zeroerr/assert.h"

#include "RtsCompositionFixtures.h"
// rts_crowd.cpp
namespace test_rts_crowd {
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
    eve::SimulationStep step{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.1).expect("test dt")};
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
    step.tick = eve::SimulationTick{2};
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
        const eve::SimulationStep step{eve::SimulationTick{tick + 1},
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
    eve::SimulationStep       step{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.01).expect("test dt")};
    auto                      projected = eve::rts::CrowdMotionSystem::step(step, crowd);
    REQUIRE(projected.ok());
    CHECK(crowd.getNamedAgentIndex("passenger") >= 0);
    passenger->containment()->container = eve::rts::ContainerLink::bind(ecs::handle_of(transport)).takeValue();
    step.tick                           = eve::SimulationTick{2};
    projected                           = eve::rts::CrowdMotionSystem::step(step, crowd);
    REQUIRE(projected.ok());
    CHECK_EQ(crowd.getNamedAgentIndex("passenger"), -1);
    passenger->containment()->container = {};
    passenger->motion()->x              = 7.f;
    passenger->motion()->y              = 8.f;
    step.tick                           = eve::SimulationTick{3};
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
    eve::SimulationStep       step{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.1).expect("test dt")};
    auto                      advanced = eve::rts::CrowdMotionSystem::step(step, crowd);
    REQUIRE(advanced.ok());
    CHECK_EQ(unit->motion()->x, 0.f);
    CHECK(!unit->motion()->arrived);
    unit->supply()->convoyWaiting = false;
    unit->motion()->arrivalRadius = 0.1f;
    step.tick                     = eve::SimulationTick{2};
    advanced                      = eve::rts::CrowdMotionSystem::step(step, crowd);
    REQUIRE(advanced.ok());
    CHECK(unit->motion()->x > 0.f);
    CHECK_EQ(unit->orders()->values.current().value().id, orderId);
    unit->release();
}
}  // namespace test_rts_crowd

// Contract section: rts crowd production
#include "PrivateTestScope.h"

// rts_crowd_production.cpp
namespace test_rts_crowd_production {
namespace {
void loadProduction(eve::rts::RTS& rts) {
    auto configured = rts.configureScriptWorld(32, 32, 1.f);
    REQUIRE(configured.ok());
    auto loaded = rts.loadScriptContent(R"({
        "units":[{"id":"marine","producer":"barracks","costResource":"minerals",
                  "cost":50,"buildTime":0.1,"health":80,"speed":0,"radius":0.5}],
        "buildings":[{"id":"barracks","health":500,"buildTime":1}]
    })");
    REQUIRE(loaded.ok());
}
}  // namespace

TEST_CASE("rts.crowd.productionPushPublishesPeerMotion") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    loadProduction(rts);
    auto* faction  = rts.newFaction(subject("00000000-0000-7000-8000-000000000801")).takeValue();
    auto* producer = rts.newFactionBuilding(*faction, subject("00000000-0000-7000-8000-000000000802"),
                                            *eve::LogicalId::parse("building:barracks"))
                         .takeValue();
    producer->placement()->worldX = 10.f;
    producer->placement()->worldY = 10.f;
    const auto definition         = *eve::LogicalId::parse("unit:marine");
    auto*      neighbor =
        rts.newFactionUnit(*faction, subject("00000000-0000-7000-8000-000000000803"), definition).takeValue();
    neighbor->motion()->x = 10.f;
    neighbor->motion()->y = 10.f;
    const auto born       = subject("00000000-0000-7000-8000-000000000804");
    auto       credit     = rts.addScriptResource(*faction, "minerals", 100);
    REQUIRE(credit.ok());
    auto queued = rts.queueScriptUnit(*producer, born, definition);
    REQUIRE(queued.ok());
    auto stepped = rts.stepScript(0.1);
    REQUIRE(stepped.ok());
    Unit* created = rts.findUnit(born);
    REQUIRE(created != nullptr);
    if (!created) return;
    CHECK_EQ(created->motion()->x, 10.f);
    CHECK_EQ(created->motion()->y, 10.f);
    CHECK(std::hypot(neighbor->motion()->x - 10.f, neighbor->motion()->y - 10.f) >= 0.999f);
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 50);
    stepped = rts.stepScript(0.1);
    REQUIRE(stepped.ok());
    CHECK(std::hypot(neighbor->motion()->x - created->motion()->x, neighbor->motion()->y - created->motion()->y) >=
          0.999f);
    CHECK_EQ(rts.unitCount(), 2u);
}

TEST_CASE("rts.crowd.blockedProductionRetainsPaymentAndIdentityForRetry") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    loadProduction(rts);
    auto* faction  = rts.newFaction(subject("00000000-0000-7000-8000-000000000811")).takeValue();
    auto* producer = rts.newFactionBuilding(*faction, subject("00000000-0000-7000-8000-000000000812"),
                                            *eve::LogicalId::parse("building:barracks"))
                         .takeValue();
    producer->placement()->worldX = 10.f;
    producer->placement()->worldY = 10.f;
    const auto definition         = *eve::LogicalId::parse("unit:marine");
    auto* held = rts.newFactionUnit(*faction, subject("00000000-0000-7000-8000-000000000813"), definition).takeValue();
    held->motion()->x = 10.f;
    held->motion()->y = 10.f;
    CommandSpec hold;
    hold.kind    = OrderKind::HoldPosition;
    auto ordered = held->orders()->values.replace(hold);
    REQUIRE(ordered.ok());
    const auto born   = subject("00000000-0000-7000-8000-000000000814");
    auto       credit = rts.addScriptResource(*faction, "minerals", 100);
    REQUIRE(credit.ok());
    auto queued = rts.queueScriptUnit(*producer, born, definition);
    REQUIRE(queued.ok());
    for (int i = 0; i < 3; ++i) {
        auto stepped = rts.stepScript(0.1);
        REQUIRE(stepped.ok());
        CHECK(rts.findUnit(born) == nullptr);
        CHECK_EQ(rts.unitCount(), 1u);
        CHECK_EQ(faction->members()->units.size(), 1u);
        CHECK(producer->rally()->productionSpawnBlocked);
        CHECK(producer->rally()->settledProductionTasks.empty());
        auto task = producer->production()->values.taskAt(0);
        REQUIRE(task.has_value());
        CHECK(task->get().settlementRequired);
        CHECK(task->get().state == eve::production::TaskState::ReadyToSettle);
        CHECK(task->get().settlement.settlementId.empty());
        CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 50);
        CHECK_EQ(held->motion()->x, 10.f);
        CHECK_EQ(held->motion()->y, 10.f);
    }
    auto duplicate = rts.newUnit(born, definition);
    CHECK(!duplicate.ok());
    auto duplicateBuilding = rts.newBuilding(born);
    CHECK(!duplicateBuilding.ok());
    auto duplicateQueue = rts.queueScriptUnit(*producer, born, definition);
    CHECK(!duplicateQueue.ok());
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 50);
    const auto factionSubject  = faction->identity()->subject;
    const auto producerSubject = producer->identity()->subject;
    const auto heldSubject     = held->identity()->subject;
    auto       checkpoint      = rts.captureScriptCheckpoint("blocked-spawn");
    REQUIRE(checkpoint.ok());
    REQUIRE(rts.relocateUnits({{{heldSubject, {20.f, 10.f}}}, 1000}).ok());
    auto stepped      = rts.stepScript(0.1);
    REQUIRE(stepped.ok());
    CHECK(rts.findUnit(born) != nullptr);
    CHECK(!producer->rally()->productionSpawnBlocked);
    CHECK_EQ(producer->rally()->settledProductionTasks.size(), 1u);
    {
        auto task = producer->production()->values.taskAt(0);
        REQUIRE(task.has_value());
        CHECK(task->get().state == eve::production::TaskState::Completed);
        CHECK_EQ(task->get().settlement.settlementId, "rts.unit:" + task->get().id);
    }
    CHECK_EQ(rts.unitCount(), 2u);
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 50);
    auto restored = rts.restoreScriptCheckpoint("blocked-spawn");
    REQUIRE(restored.ok());
    // Restore rebuilds ECS roots; old pointers must not cross this boundary.
    faction  = rts.findFaction(factionSubject);
    producer = rts.findBuilding(producerSubject);
    held     = rts.findUnit(heldSubject);
    REQUIRE(faction != nullptr);
    REQUIRE(producer != nullptr);
    REQUIRE(held != nullptr);
    CHECK(rts.findUnit(born) == nullptr);
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 50);
    stepped = rts.stepScript(0.1);
    REQUIRE(stepped.ok());
    CHECK(producer->rally()->productionSpawnBlocked);
    CHECK_EQ(rts.unitCount(), 1u);
    auto stillReserved = rts.newUnit(born, definition);
    CHECK(!stillReserved.ok());
    REQUIRE(rts.relocateUnits({{{heldSubject, {20.f, 10.f}}}, 1000}).ok());
    stepped = rts.stepScript(0.1);
    REQUIRE(stepped.ok());
    CHECK(rts.findUnit(born) != nullptr);
    CHECK_EQ(producer->rally()->settledProductionTasks.size(), 1u);
    CHECK_EQ(rts.unitCount(), 2u);
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 50);
}

TEST_CASE("rts.crowd.scriptProfileCrossingReachesBothDestinations") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    auto             configured = rts.configureScriptWorld(32, 32, 1.f);
    REQUIRE(configured.ok());
    auto loaded = rts.loadScriptContent(R"({"units":[{"id":"walker","health":100,"speed":2,"radius":0.4}]})");
    REQUIRE(loaded.ok());
    auto       faction    = rts.newFaction(subject("00000000-0000-7000-8000-000000000831")).takeValue();
    const auto definition = *eve::LogicalId::parse("unit:walker");
    Unit* a = rts.newFactionUnit(*faction, subject("00000000-0000-7000-8000-000000000832"), definition).takeValue();
    Unit* b = rts.newFactionUnit(*faction, subject("00000000-0000-7000-8000-000000000833"), definition).takeValue();
    a->motion()->x = 5.5f;
    b->motion()->x = 15.5f;
    a->motion()->y = b->motion()->y = 10.5f;
    a->motion()->arrivalRadius = b->motion()->arrivalRadius = 0.3f;
    CommandSpec move;
    move.kind   = OrderKind::Move;
    move.target = {15.5f, 10.5f};
    auto issued = a->orders()->values.replace(move);
    REQUIRE(issued.ok());
    move.target = {5.5f, 10.5f};
    issued      = b->orders()->values.replace(move);
    REQUIRE(issued.ok());
    float minimumSeparation = 100.f;
    for (int tick = 0; tick < 240; ++tick) {
        auto stepped = rts.stepScript(0.05);
        REQUIRE(stepped.ok());
        minimumSeparation =
            std::min(minimumSeparation, std::hypot(a->motion()->x - b->motion()->x, a->motion()->y - b->motion()->y));
    }
    CHECK(minimumSeparation >= 0.799f);
    CHECK(std::hypot(a->motion()->x - 15.5f, a->motion()->y - 10.5f) < 0.5f);
    CHECK(std::hypot(b->motion()->x - 5.5f, b->motion()->y - 10.5f) < 0.5f);
}
}  // namespace test_rts_crowd_production

// Contract section: rts formation
#include "PrivateTestScope.h"

#include <limits>
#include <numbers>
#include <simplesquirrel/simplesquirrel.hpp>
#include "rts/RTSReplay.h"
// rts_formation.cpp
namespace test_rts_formation {
TEST_CASE("rts.formation.mixedRadiiFitAndSelectionOrderIsStable") {
    ecs::Table                 world;
    ecs::ScopedTable           guard(world);
    std::array<Unit*, 4>       units{Unit::createUnit(), Unit::createUnit(), Unit::createUnit(), Unit::createUnit()};
    const std::array<float, 4> radii{1.5f, 0.3f, 2.f, 0.6f};
    std::vector<ecs::EntityHandle> handles;
    for (size_t i = 0; i < units.size(); ++i) {
        units[i]->crowd()->radius = radii[i];
        units[i]->motion()->x     = static_cast<float>(i) * 3.f;
        handles.push_back(ecs::handle_of(units[i]));
    }
    CommandSpec command;
    command.kind   = OrderKind::Move;
    command.target = {10.f, 10.f};
    for (auto kind : {FormationKind::Line, FormationKind::Grid, FormationKind::Wedge, FormationKind::Column,
                      FormationKind::Dispersed}) {
        FormationSpec formation{kind, 0.1f, 2};
        auto          assigned = eve::rts::CommandFanOutSystem::fanOut(handles, command, formation);
        REQUIRE(assigned.ok());
        std::array<eve::rts::WorldPosition, 4> positions;
        for (size_t i = 0; i < units.size(); ++i) positions[i] = units[i]->orders()->values.current().value().target;
        for (size_t i = 0; i < units.size(); ++i)
            for (size_t j = i + 1; j < units.size(); ++j)
                CHECK(std::hypot(positions[i].x - positions[j].x, positions[i].y - positions[j].y) + 0.0001f >=
                      radii[i] + radii[j]);
        std::reverse(handles.begin(), handles.end());
        assigned = eve::rts::CommandFanOutSystem::fanOut(handles, command, formation);
        REQUIRE(assigned.ok());
        for (size_t i = 0; i < units.size(); ++i) {
            const auto current = units[i]->orders()->values.current().value();
            CHECK_EQ(current.target.x, positions[i].x);
            CHECK_EQ(current.target.y, positions[i].y);
        }
    }
    for (auto* unit : units) unit->release();
}

TEST_CASE("rts.formation.invalidSelectionLeavesCommandsUntouched") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Unit*            unit = Unit::createUnit();
    CommandSpec      command;
    command.kind   = OrderKind::Move;
    command.target = {2.f, 3.f};
    auto initial   = unit->orders()->values.enqueue(command);
    REQUIRE(initial.ok());
    auto                                   before = unit->orders()->values.snapshotState().takeValue();
    const auto                             handle = ecs::handle_of(unit);
    const std::array<ecs::EntityHandle, 2> duplicate{handle, handle};
    auto                                   rejected = eve::rts::CommandFanOutSystem::fanOut(duplicate, command, {});
    CHECK(!rejected.ok());
    CHECK_EQ(unit->orders()->values.snapshotState().value().queueJson, before.queueJson);
    unit->crowd()->radius = std::numeric_limits<float>::quiet_NaN();
    rejected              = eve::rts::CommandFanOutSystem::fanOut(std::span(&handle, 1), command, {});
    CHECK(!rejected.ok());
    CHECK_EQ(unit->orders()->values.snapshotState().value().queueJson, before.queueJson);
    unit->release();
}

TEST_CASE("rts.formation.rotationAndColumnPreserveAnchorAndSpacing") {
    FormationSpec spec{FormationKind::Line, 2.f, 0, std::numbers::pi_v<float> / 2.f};
    auto          planned = eve::rts::FormationPlanner::plan(3, {10.f, 20.f}, spec);
    REQUIRE(planned.ok());
    for (const auto& point : planned.value()) CHECK(std::abs(point.x - 10.f) < 0.0001f);
    CHECK(std::abs(planned.value()[0].y - 18.f) < 0.0001f);
    CHECK(std::abs(planned.value()[2].y - 22.f) < 0.0001f);
    spec.kind = FormationKind::Column;
    planned   = eve::rts::FormationPlanner::plan(3, {10.f, 20.f}, spec);
    REQUIRE(planned.ok());
    for (const auto& point : planned.value()) CHECK(std::abs(point.y - 20.f) < 0.0001f);
    CHECK(std::abs(planned.value()[0].x - 12.f) < 0.0001f);
    CHECK(std::abs(planned.value()[2].x - 8.f) < 0.0001f);
    spec.rotationRadians = std::numeric_limits<float>::infinity();
    CHECK(!spec.validate().ok());
}

TEST_CASE("rts.formation.replayVersionFourRoundTripsAndRejectsMalformedAtomically") {
    ecs::Table                 world;
    ecs::ScopedTable           guard(world);
    eve::rts::RTS              replayWorld;
    eve::rts::RTSReplayCommand command;
    command.tick  = eve::SimulationTick{2};
    command.units = {subject("00000000-0000-7000-8000-000000000901"), subject("00000000-0000-7000-8000-000000000903")};
    command.command.kind = OrderKind::Move;
    command.formation    = {FormationKind::Column, 2.f, 0, 0.75f};
    eve::rts::RTSCommandLog log;
    auto                    queued = log.queue(command);
    REQUIRE(queued.ok());
    const auto text = log.exportText();
    CHECK(text.starts_with("EVERTS_COMMANDS 4\n"));
    eve::rts::RTSCommandLog restored;
    auto                    imported = restored.importText(text);
    REQUIRE(imported.ok());
    CHECK_EQ(restored.exportText(), text);
    auto       malformed = text;
    const auto angle     = malformed.find("0.75");
    REQUIRE(angle != std::string::npos);
    malformed.replace(angle, 4, "nan");
    imported = restored.importText(malformed);
    CHECK(!imported.ok());
    CHECK_EQ(restored.exportText(), text);
    for (auto id : command.units) {
        auto created = replayWorld.newUnit(id);
        REQUIRE(created.ok());
    }
    auto applied = restored.apply(eve::SimulationTick{2}, replayWorld);
    REQUIRE(applied.ok());
    CHECK_EQ(applied.value(), 2u);
    const auto first  = replayWorld.findUnit(command.units[0])->orders()->values.current().value().target;
    const auto second = replayWorld.findUnit(command.units[1])->orders()->values.current().value().target;
    CHECK(std::abs(std::abs(first.x - second.x) - 2.f * std::sin(0.75f)) < 0.0001f);
    CHECK(std::abs(std::abs(first.y - second.y) - 2.f * std::cos(0.75f)) < 0.0001f);
    for (unsigned version = 1; version <= 3; ++version) {
        command.formation = {FormationKind::Line, 2.f, 0};
        command.operation = version == 1   ? eve::rts::RTSReplayOperation::UnitCommand
                            : version == 2 ? eve::rts::RTSReplayOperation::CancelProduction
                                           : eve::rts::RTSReplayOperation::CancelFireSupport;
        command.producer  = subject("00000000-0000-7000-8000-000000000902");
        eve::rts::RTSCommandLog legacy;
        queued = legacy.queue(command);
        REQUIRE(queued.ok());
        const auto oldText = legacy.exportText();
        CHECK(oldText.starts_with("EVERTS_COMMANDS " + std::to_string(version) + "\n"));
        imported = restored.importText(oldText);
        REQUIRE(imported.ok());
        CHECK_EQ(restored.exportText(), oldText);
    }
}

TEST_CASE("rts.formation.scriptCommandsReachCanonicalOrdersAndReplay") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    const auto first  = subject("00000000-0000-7000-8000-000000000921");
    const auto second = subject("00000000-0000-7000-8000-000000000922");
    vm.addFunc("makeFormationUnits", [first, second](eve::rts::RTS* sim) {
        auto a = sim->newUnit(first);
        auto b = sim->newUnit(second);
        if (!a || !b) return false;
        a.value()->motion()->speed = b.value()->motion()->speed = 0.f;
        return true;
    });
    vm.addFunc("hasVerticalTargets", [first, second](eve::rts::RTS* sim, float x, float y) {
        auto* a = sim->findUnit(first);
        auto* b = sim->findUnit(second);
        if (!a || !b) return false;
        auto aa = a->orders()->values.current();
        auto bb = b->orders()->values.current();
        if (!aa || !bb) return false;
        const auto pa = aa.value().target, pb = bb.value().target;
        return std::abs(pa.x - x) < 0.0001f && std::abs(pb.x - x) < 0.0001f &&
               std::abs((pa.y + pb.y) * 0.5f - y) < 0.0001f && std::abs(std::abs(pa.y - pb.y) - 2.f) < 0.0001f;
    });
    vm.run(vm.compileSource(R"(
        local sim = eve.RTS();
        assert(sim.configureScriptWorld(32,32,1.0,0.0,0.0).ok);
        assert(makeFormationUnits(sim));
        local units = ["00000000-0000-7000-8000-000000000921", "00000000-0000-7000-8000-000000000922"];
        local issued = sim.moveFormationUnits(units,10.0,20.0,false,"line",2.0,0,1.57079632679);
        assert(issued.ok && issued.hasValue && issued.value.accepted == 2);
        assert(hasVerticalTargets(sim,10.0,20.0));
        assert(!sim.moveFormationUnits(units,0.0,0.0,false,"invalid",2.0,0,0.0).ok);
        assert(!sim.moveFormationUnits(units,0.0,0.0,false,"grid",-1.0,0,0.0).ok);
        assert(hasVerticalTargets(sim,10.0,20.0));
        assert(!sim.moveFormationUnits([42],0.0,0.0,false,"line",2.0,0,0.0).ok);
        assert(hasVerticalTargets(sim,10.0,20.0));
        assert(sim.queueScriptFormationMove(2,units,20.0,20.0,false,"column",2.0,0,0.0).ok);
        local log = sim.exportScriptCommandLog();
        assert(log.ok && log.value.find("EVERTS_COMMANDS 4") == 0);
        assert(sim.stepScript(0.1).ok);
        assert(hasVerticalTargets(sim,10.0,20.0));
        assert(sim.stepScript(0.1).ok);
        assert(hasVerticalTargets(sim,20.0,20.0));
        assert(sim.moveUnits(units,20.0,20.0,false,2.0).ok);
        assert(sim.attackMoveUnits(units,20.0,20.0,false,2.0).ok);
        assert(!sim.queueScriptFormationMove(-1,units,20.0,20.0,false,"column",2.0,0,0.0).ok);
    )"));
}

TEST_CASE("rts.formation.slotRelaxationAvoidsStealingNearbyDestination") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Unit*            distant = Unit::createUnit();
    Unit*            nearby  = Unit::createUnit();
    distant->motion()->x     = 0.f;
    distant->motion()->y     = 2.f;
    nearby->motion()->x      = -0.9f;
    nearby->motion()->y      = 0.f;
    const std::array handles{ecs::handle_of(distant), ecs::handle_of(nearby)};
    CommandSpec      command;
    command.kind   = OrderKind::Move;
    command.target = {0.f, 0.f};
    FormationSpec formation{FormationKind::Line, 2.f, 0};
    auto          assigned = eve::rts::CommandFanOutSystem::fanOut(handles, command, formation);
    REQUIRE(assigned.ok());
    const auto distantOrder = distant->orders()->values.current().value();
    const auto nearbyOrder  = nearby->orders()->values.current().value();
    CHECK_EQ(distantOrder.target.x, 1.f);
    CHECK_EQ(nearbyOrder.target.x, -1.f);
    CHECK_EQ(distantOrder.formationSlot, 1);
    CHECK_EQ(nearbyOrder.formationSlot, 0);
    const double distance = std::hypot(distantOrder.target.x, distantOrder.target.y - 2.f) +
                            std::hypot(nearbyOrder.target.x + 0.9f, nearbyOrder.target.y);
    CHECK(distance < 2.34);
    distant->release();
    nearby->release();
}
}  // namespace test_rts_formation

// Contract section: rts groups
#include "PrivateTestScope.h"

#include "rts/RTSSystemMovementInternal.h"
// rts_groups.cpp
namespace test_rts_groups {
TEST_CASE("rts.groups.pacingWorksWithAndWithoutCrowd") {
    for (bool withCrowd : {false, true}) {
        ecs::Table        table;
        ecs::ScopedTable  scope(table);
        eve::crowd::Crowd crowd;
        crowd.resizeField(64, 32, 1.f, 0.f, 0.f);
        crowd.setArriveRadius(1.f);
        crowd.setSeparationWeight(0.f);
        eve::rts::RTS rts;
        auto          a    = subject("00000000-0000-7000-8000-00000000f101");
        auto          b    = subject("00000000-0000-7000-8000-00000000f102");
        auto*         fast = rts.newUnit(a).takeValue();
        auto*         slow = rts.newUnit(b).takeValue();
        fast->motion()->x = slow->motion()->x = 5.f;
        fast->motion()->y                     = 9.f;
        slow->motion()->y                     = 11.f;
        fast->motion()->speed                 = 4.f;
        slow->motion()->speed                 = 1.f;
        fast->motion()->arrivalRadius = slow->motion()->arrivalRadius = 0.1f;
        if (withCrowd) {
            fast->crowd()->link = eve::rts::CrowdLink::bind("group/fast").takeValue();
            slow->crowd()->link = eve::rts::CrowdLink::bind("group/slow").takeValue();
            rts.setCrowdProvider(&crowd);
        }
        eve::rts::MovementGroupBatch batch;
        batch.units        = {a, b};
        batch.target       = {25.f, 10.f};
        batch.formation    = {FormationKind::Column, 2.f, 0};
        batch.leadDistance = 1.f;
        auto issued        = rts.submitMovementGroup(batch);
        REQUIRE(issued.ok());
        PendingRTSExecutor executor;
        for (std::uint64_t tick = 1; tick <= 100; ++tick) {
            const auto before = fast->motion()->x;
            auto stepped = rts.step({eve::SimulationTick{tick}, eve::Duration::fromSeconds(0.05).value()}, executor);
            REQUIRE(stepped.ok());
            CHECK(fast->motion()->x - slow->motion()->x < 2.1f);
            CHECK(fast->motion()->x - before <= 0.201f);
        }
        CHECK(slow->motion()->x > 9.f);
        if (withCrowd) CHECK(crowd.hasNamedAgent("group/fast"));
        CHECK(fast->navigation()->formationSpeedFactor < 1.f);
        // A lagging traffic loser must let the leading winner clear the passage.
        // Waiting is temporary and must not detach the member from its group.
        slow->navigation()->trafficWaiting = true;
        const float beforeYield            = fast->motion()->x;
        auto        yielded = rts.step({eve::SimulationTick{101}, eve::Duration::fromSeconds(0.05).value()}, executor);
        REQUIRE(yielded.ok());
        CHECK_EQ(fast->navigation()->formationSpeedFactor, 1.f);
        CHECK(fast->motion()->x > beforeYield);
        CHECK_EQ(rts.snapshotState().value().movementGroups.size(), 1u);
        slow->navigation()->trafficWaiting = false;
        auto resumed = rts.step({eve::SimulationTick{102}, eve::Duration::fromSeconds(0.05).value()}, executor);
        REQUIRE(resumed.ok());
        CHECK(fast->navigation()->formationSpeedFactor < 1.f);
        auto saved = rts.snapshotState();
        REQUIRE(saved.ok());
        CHECK_EQ(saved.value().movementGroups.size(), 1u);
        for (std::uint64_t tick = 103; tick <= 560; ++tick) {
            auto stepped = rts.step({eve::SimulationTick{tick}, eve::Duration::fromSeconds(0.05).value()}, executor);
            REQUIRE(stepped.ok());
        }
        CHECK(fast->orders()->values.empty());
        CHECK(slow->orders()->values.empty());
        CHECK(rts.snapshotState().value().movementGroups.empty());
    }
}

TEST_CASE("rts.groups.movingSlotsAssembleBeforeArrival") {
    for (bool withCrowd : {false, true}) {
        ecs::Table        table;
        ecs::ScopedTable  scope(table);
        eve::crowd::Crowd crowd;
        eve::rts::RTS     rts;
        crowd.resizeField(128, 64, 1.f, 0.f, 0.f);
        crowd.setArriveRadius(1.f);
        crowd.setSeparationWeight(0.f);
        auto  a            = subject("00000000-0000-7000-8000-00000000f141");
        auto  b            = subject("00000000-0000-7000-8000-00000000f142");
        auto* first        = rts.newUnit(a).takeValue();
        auto* second       = rts.newUnit(b).takeValue();
        first->motion()->x = second->motion()->x = 10.f;
        first->motion()->y                       = 10.f;
        second->motion()->y                      = 30.f;
        first->motion()->speed = second->motion()->speed = 4.f;
        first->motion()->arrivalRadius = second->motion()->arrivalRadius = 0.1f;
        if (withCrowd) {
            first->crowd()->link  = eve::rts::CrowdLink::bind("assembling/first").takeValue();
            second->crowd()->link = eve::rts::CrowdLink::bind("assembling/second").takeValue();
            rts.setCrowdProvider(&crowd);
        }
        eve::rts::MovementGroupBatch batch;
        batch.units        = {a, b};
        batch.target       = {100.f, 20.f};
        batch.formation    = {FormationKind::Column, 2.f, 0};
        batch.leadDistance = 2.f;
        REQUIRE(rts.submitMovementGroup(batch).ok());
        PendingRTSExecutor executor;
        for (std::uint64_t tick = 1; tick <= 100; ++tick) {
            REQUIRE(rts.step({eve::SimulationTick{tick}, eve::Duration::fromSeconds(0.05).value()}, executor).ok());
            CHECK(!first->orders()->values.empty());
            CHECK(!second->orders()->values.empty());
        }
        CHECK(std::abs(first->motion()->y - second->motion()->y) < 3.f);
        CHECK(first->motion()->x < 40.f);
        REQUIRE(first->navigation()->formationTarget.has_value());
        auto saved = rts.snapshotState().takeValue();
        for (const auto& unit : saved.units) CHECK(!unit.navigation.formationTarget.has_value());
        REQUIRE(rts.step({eve::SimulationTick{100}, eve::Duration::zero()}, executor).ok());
        const auto before = *first->navigation()->formationTarget;
        REQUIRE(rts.restoreState(saved).ok());
        CHECK(!first->navigation()->formationTarget.has_value());
        REQUIRE(rts.step({eve::SimulationTick{100}, eve::Duration::zero()}, executor).ok());
        REQUIRE(first->navigation()->formationTarget.has_value());
        CHECK(std::isfinite(first->navigation()->formationTarget->x));
        CHECK(std::abs(first->navigation()->formationTarget->x - before.x) < 1e-5f);
        CHECK(std::abs(first->navigation()->formationTarget->y - before.y) < 1e-5f);
        eve::map::Pathfinder pathfinder(128, 64);
        pathfinder.setBlocked(50, 19, true);
        pathfinder.setBlocked(50, 21, true);
        rts.setNavigationProvider(&pathfinder);
        REQUIRE(rts.step({eve::SimulationTick{100}, eve::Duration::zero()}, executor).ok());
        CHECK(!first->navigation()->formationTarget.has_value());
        CHECK(!second->navigation()->formationTarget.has_value());
        CHECK(!first->navigation()->waypoints.empty());
        pathfinder.setBlocked(50, 19, false);
        pathfinder.setBlocked(50, 21, false);
        REQUIRE(rts.step({eve::SimulationTick{100}, eve::Duration::zero()}, executor).ok());
        CHECK(first->navigation()->formationTarget.has_value());
        CHECK(second->navigation()->formationTarget.has_value());
        rts.setNavigationProvider(nullptr);
        CommandSpec replacement;
        replacement.target = {10.f, 40.f};
        REQUIRE(first->orders()->values.replace(replacement).ok());
        REQUIRE(rts.step({eve::SimulationTick{100}, eve::Duration::zero()}, executor).ok());
        CHECK(!first->navigation()->formationTarget.has_value());
        CHECK(!second->navigation()->formationTarget.has_value());
    }
}

TEST_CASE("rts.groups.passageReleasesAndRestoresFormation") {
    for (bool withCrowd : {false, true}) {
        ecs::Table           table;
        ecs::ScopedTable     scope(table);
        eve::map::Pathfinder pathfinder(24, 24);
        pathfinder.setDiagonal(false);
        eve::crowd::Crowd crowd;
        crowd.resizeField(24, 24, 1.f, -0.5f, -0.5f);
        crowd.setArriveRadius(1.f);
        crowd.setSeparationWeight(0.f);
        for (int y = 0; y < 24; ++y) {
            if (y == 10) continue;
            pathfinder.setBlocked(10, y, true);
            crowd.setBlocked(10, y, true);
        }
        eve::rts::RTS rts;
        rts.setNavigationProvider(&pathfinder);
        if (withCrowd) rts.setCrowdProvider(&crowd);
        const auto a       = subject("00000000-0000-7000-8000-00000000f151");
        const auto b       = subject("00000000-0000-7000-8000-00000000f152");
        auto*      first   = rts.newUnit(a).takeValue();
        auto*      second  = rts.newUnit(b).takeValue();
        first->motion()->x = second->motion()->x = 2.f;
        first->motion()->y                       = 8.f;
        second->motion()->y                      = 12.f;
        for (auto* unit : {first, second}) {
            unit->motion()->speed         = 3.f;
            unit->motion()->arrivalRadius = 0.1f;
            unit->crowd()->radius         = 0.2f;
            if (withCrowd)
                unit->crowd()->link = eve::rts::CrowdLink::bind(unit->identity()->subject.format()).takeValue();
        }
        eve::rts::MovementGroupBatch batch;
        batch.units     = {a, b};
        batch.target    = {18.f, 10.f};
        batch.formation = {FormationKind::Column, 4.f, 0};
        REQUIRE(rts.submitMovementGroup(batch).ok());
        PendingRTSExecutor executor;
        bool               released = false, recovered = false;
        for (std::uint64_t tick = 1; tick <= 900; ++tick) {
            const WorldPosition firstBefore{first->motion()->x, first->motion()->y};
            const WorldPosition secondBefore{second->motion()->x, second->motion()->y};
            REQUIRE(rts.step({eve::SimulationTick{tick}, eve::Duration::fromSeconds(0.05).value()}, executor).ok());
            if (!first->navigation()->formationTarget && first->motion()->x < 10.f) released = true;
            if (released && first->navigation()->formationTarget && second->navigation()->formationTarget)
                recovered = true;
            for (auto* unit : {first, second}) {
                WorldPosition position{unit->motion()->x, unit->motion()->y};
                CHECK(eve::rts::systems_internal::isFormationSegmentClear(
                    pathfinder, {}, unit == first ? firstBefore : secondBefore, position, 0.2f));
            }
            if (first->orders()->values.empty() && second->orders()->values.empty()) break;
        }
        CHECK(released);
        CHECK(recovered);
        CHECK(first->orders()->values.empty());
        CHECK(second->orders()->values.empty());
        CHECK(first->motion()->x > 17.f);
        CHECK(second->motion()->x > 17.f);
    }
}

TEST_CASE("rts.groups.mixedSpeedSquadClearsSingleCellPassage") {
    for (bool withCrowd : {false, true}) {
        ecs::Table           table;
        ecs::ScopedTable     scope(table);
        eve::map::Pathfinder pathfinder(24, 24);
        pathfinder.setDiagonal(false);
        eve::crowd::Crowd crowd;
        crowd.resizeField(24, 24, 1.f, -0.5f, -0.5f);
        crowd.setArriveRadius(1.f);
        crowd.setSeparationWeight(0.f);
        for (int y = 0; y < 24; ++y) {
            if (y == 10) continue;
            pathfinder.setBlocked(10, y, true);
            crowd.setBlocked(10, y, true);
        }
        eve::rts::RTS rts;
        rts.setNavigationProvider(&pathfinder);
        if (withCrowd) rts.setCrowdProvider(&crowd);
        eve::rts::MovementGroupBatch batch;
        batch.target    = {18.f, 10.f};
        batch.formation = {FormationKind::Grid, 2.f, 2};
        std::vector<Unit*> units;
        for (int i = 0; i < 6; ++i) {
            const auto id   = subject(("00000000-0000-7000-8000-00000000f16" + std::to_string(i)).c_str());
            auto*      unit = rts.newUnit(id).takeValue();
            batch.units.push_back(id);
            units.push_back(unit);
            unit->motion()->x             = 2.f + static_cast<float>(i % 2) * 2.f;
            unit->motion()->y             = 7.f + static_cast<float>(i / 2) * 3.f;
            unit->motion()->speed         = 2.f + static_cast<float>(i) * 0.25f;
            unit->motion()->arrivalRadius = 0.1f;
            unit->crowd()->radius         = 0.3f;
            if (withCrowd) unit->crowd()->link = eve::rts::CrowdLink::bind(id.format()).takeValue();
        }
        REQUIRE(rts.submitMovementGroup(batch).ok());
        PendingRTSExecutor executor;
        for (std::uint64_t tick = 1; tick <= 1200; ++tick) {
            REQUIRE(rts.step({eve::SimulationTick{tick}, eve::Duration::fromSeconds(0.05).value()}, executor).ok());
            bool finished = true;
            for (auto* unit : units) finished = finished && unit->orders()->values.empty();
            if (finished) break;
        }
        for (auto* unit : units) {
            CHECK(unit->orders()->values.empty());
            CHECK(unit->motion()->x > 16.f);
        }
    }
}

TEST_CASE("rts.groups.opposingSquadsReserveWholeCorridor") {
    for (bool withCrowd : {false, true}) {
        ecs::Table           table;
        ecs::ScopedTable     scope(table);
        eve::map::Pathfinder pathfinder(24, 24);
        pathfinder.setDiagonal(false);
        eve::crowd::Crowd crowd;
        crowd.resizeField(24, 24, 1.f, -0.5f, -0.5f);
        crowd.setArriveRadius(1.f);
        crowd.setSeparationWeight(0.f);
        for (int x = 8; x <= 15; ++x) {
            for (int y = 0; y < 24; ++y) {
                if (y == 10) continue;
                pathfinder.setBlocked(x, y, true);
                crowd.setBlocked(x, y, true);
            }
        }
        eve::rts::RTS rts;
        rts.setNavigationProvider(&pathfinder);
        if (withCrowd) rts.setCrowdProvider(&crowd);
        std::vector<Unit*> units;
        for (int side = 0; side < 2; ++side) {
            eve::rts::MovementGroupBatch batch;
            batch.target    = {side == 0 ? 21.f : 2.f, 10.f};
            batch.formation = {FormationKind::Column, 1.2f, 0};
            for (int member = 0; member < 2; ++member) {
                const auto id =
                    subject(("00000000-0000-7000-8000-00000000f17" + std::to_string(side * 2 + member)).c_str());
                auto* unit = rts.newUnit(id).takeValue();
                units.push_back(unit);
                batch.units.push_back(id);
                unit->motion()->x             = side == 0 ? 2.f : 21.f;
                unit->motion()->y             = member == 0 ? 9.f : 11.f;
                unit->motion()->speed         = 3.f;
                unit->motion()->arrivalRadius = 0.1f;
                unit->crowd()->radius         = 0.25f;
                if (withCrowd) unit->crowd()->link = eve::rts::CrowdLink::bind(id.format()).takeValue();
            }
            REQUIRE(rts.submitMovementGroup(batch).ok());
        }
        PendingRTSExecutor executor;
        bool               waited = false;
        for (std::uint64_t tick = 1; tick <= 1500; ++tick) {
            REQUIRE(rts.step({eve::SimulationTick{tick}, eve::Duration::fromSeconds(0.05).value()}, executor).ok());
            bool finished = true, leftInside = false, rightInside = false;
            for (std::size_t i = 0; i < units.size(); ++i) {
                auto* unit        = units[i];
                finished          = finished && unit->orders()->values.empty();
                waited            = waited || unit->navigation()->trafficWaiting;
                const bool inside = unit->motion()->x >= 7.5f && unit->motion()->x <= 15.5f;
                if (i < 2)
                    leftInside = leftInside || inside;
                else
                    rightInside = rightInside || inside;
            }
            CHECK(!(leftInside && rightInside));
            if (finished) break;
        }
        CHECK(waited);
        for (auto* unit : units) CHECK(unit->orders()->values.empty());
    }
}

TEST_CASE("rts.groups.trappedOpponentsEvacuateAndResumeOrders") {
    for (bool withCrowd : {false, true}) {
        ecs::Table           table;
        ecs::ScopedTable     scope(table);
        eve::map::Pathfinder pathfinder(24, 24);
        pathfinder.setDiagonal(false);
        eve::crowd::Crowd crowd;
        crowd.resizeField(24, 24, 1.f, -0.5f, -0.5f);
        crowd.setArriveRadius(1.f);
        crowd.setSeparationWeight(0.f);
        for (int x = 8; x <= 15; ++x) {
            for (int y = 0; y < 24; ++y) {
                if (y == 10) continue;
                pathfinder.setBlocked(x, y, true);
                crowd.setBlocked(x, y, true);
            }
        }
        eve::rts::RTS rts;
        rts.setNavigationProvider(&pathfinder);
        if (withCrowd) rts.setCrowdProvider(&crowd);
        std::vector<Unit*> units;
        for (int side = 0; side < 2; ++side) {
            eve::rts::MovementGroupBatch batch;
            batch.target    = {side == 0 ? 21.f : 2.f, 10.f};
            batch.formation = {FormationKind::Column, 1.2f, 0};
            for (int member = 0; member < 2; ++member) {
                const auto id =
                    subject(("00000000-0000-7000-8000-00000000f17" + std::to_string(side * 2 + member)).c_str());
                auto* unit = rts.newUnit(id).takeValue();
                units.push_back(unit);
                batch.units.push_back(id);
                unit->motion()->x = side == 0 ? 9.f + static_cast<float>(member) : 13.f + static_cast<float>(member);
                unit->motion()->y = 10.f;
                unit->motion()->speed         = 3.f;
                unit->motion()->arrivalRadius = 0.1f;
                unit->crowd()->radius         = 0.25f;
                if (withCrowd) unit->crowd()->link = eve::rts::CrowdLink::bind(id.format()).takeValue();
            }
            REQUIRE(rts.submitMovementGroup(batch).ok());
        }
        PendingRTSExecutor executor;
        bool               waited    = false;
        bool               recovered = false, restored = false;
        for (std::uint64_t tick = 1; tick <= 1500; ++tick) {
            std::vector<WorldPosition> before;
            before.reserve(units.size());
            for (auto* unit : units) before.push_back({unit->motion()->x, unit->motion()->y});
            REQUIRE(rts.step({eve::SimulationTick{tick}, eve::Duration::fromSeconds(0.05).value()}, executor).ok());
            bool finished = true, leftInside = false, rightInside = false;
            for (std::size_t i = 0; i < units.size(); ++i) {
                auto* unit = units[i];
                finished   = finished && unit->orders()->values.empty();
                waited     = waited || unit->navigation()->trafficWaiting;
                CHECK(eve::rts::systems_internal::isFormationSegmentClear(
                    pathfinder, {}, before[i], {unit->motion()->x, unit->motion()->y}, unit->crowd()->radius));
                const bool inside = unit->motion()->x >= 7.5f && unit->motion()->x <= 15.5f;
                if (i < 2)
                    leftInside = leftInside || inside;
                else
                    rightInside = rightInside || inside;
            }
            (void)leftInside;
            (void)rightInside;
            for (auto* unit : units) recovered = recovered || unit->navigation()->trafficRecoveryTarget.has_value();
            if (recovered && !restored) {
                auto saved = rts.snapshotState().takeValue();
                for (const auto& unit : saved.units) CHECK(!unit.navigation.trafficRecoveryTarget.has_value());
                REQUIRE(rts.restoreState(saved).ok());
                restored = true;
            }
            if (finished) break;
        }
        CHECK(waited);
        CHECK(recovered);
        CHECK(restored);
        for (auto* unit : units) CHECK(unit->orders()->values.empty());
    }
}

TEST_CASE("rts.groups.navigationProviderClearsTemporaryEvacuation") {
    ecs::Table           table;
    ecs::ScopedTable     scope(table);
    eve::map::Pathfinder pathfinder(24, 24);
    pathfinder.setDiagonal(false);
    for (int x = 8; x <= 15; ++x) {
        for (int y = 0; y < 24; ++y) {
            if (y == 10) continue;
            pathfinder.setBlocked(x, y, true);
        }
    }
    eve::rts::RTS rts;
    rts.setNavigationProvider(&pathfinder);
    std::vector<Unit*> units;
    for (int side = 0; side < 2; ++side) {
        eve::rts::MovementGroupBatch batch;
        batch.target    = {side == 0 ? 21.f : 2.f, 10.f};
        batch.formation = {FormationKind::Column, 1.2f, 0};
        for (int member = 0; member < 2; ++member) {
            const auto id =
                subject(("00000000-0000-7000-8000-00000000f19" + std::to_string(side * 2 + member)).c_str());
            auto* unit = rts.newUnit(id).takeValue();
            units.push_back(unit);
            batch.units.push_back(id);
            unit->motion()->x     = side == 0 ? 9.f + static_cast<float>(member) : 13.f + static_cast<float>(member);
            unit->motion()->y     = 10.f;
            unit->motion()->speed = 3.f;
            unit->motion()->arrivalRadius = 0.1f;
            unit->crowd()->radius         = 0.25f;
        }
        REQUIRE(rts.submitMovementGroup(batch).ok());
    }
    PendingRTSExecutor executor;
    bool               recovered = false;
    for (std::uint64_t tick = 1; tick <= 120; ++tick) {
        REQUIRE(rts.step({eve::SimulationTick{tick}, eve::Duration::fromSeconds(0.05).value()}, executor).ok());
        for (auto* unit : units) recovered = recovered || unit->navigation()->trafficRecoveryTarget.has_value();
        if (recovered) break;
    }
    REQUIRE(recovered);
    std::vector<Unit*> recoveringUnits;
    for (auto* unit : units) {
        if (unit->navigation()->trafficRecoveryTarget) recoveringUnits.push_back(unit);
    }
    REQUIRE(!recoveringUnits.empty());
    rts.setNavigationProvider(nullptr);
    for (auto* unit : units) {
        CHECK(!unit->navigation()->trafficRecoveryTarget.has_value());
        CHECK(!unit->navigation()->trafficWaiting);
    }
    for (auto* unit : recoveringUnits) {
        CHECK(unit->navigation()->plannedOrderId.empty());
    }
}

TEST_CASE("rts.groups.oversizedCorridorReportsBudgetFailure") {
    ecs::Table           table;
    ecs::ScopedTable     scope(table);
    eve::map::Pathfinder pathfinder(1100, 1);
    eve::rts::RTS        rts;
    auto*                unit = rts.newUnit(subject("00000000-0000-7000-8000-00000000f180")).takeValue();
    CommandSpec          move;
    move.target = {1099.f, 0.f};
    REQUIRE(unit->orders()->values.enqueue(move).ok());
    auto result = eve::rts::TrafficReservationSystem::step(pathfinder, {});
    CHECK(!result.ok());
    REQUIRE(!result.diagnostics().empty());
    CHECK_EQ(result.diagnostics().front().code(), eve::DiagnosticCode::Unsupported);
    CHECK(!unit->orders()->values.empty());
}

TEST_CASE("rts.groups.clearanceAccountsForRadiusAndGridOrigin") {
    eve::map::Pathfinder pathfinder(12, 12);
    pathfinder.setBlocked(5, 5, true);
    eve::rts::NavigationGrid grid;
    grid.cellSize = 2.f;
    grid.originX  = -10.f;
    grid.originY  = 20.f;
    using eve::rts::systems_internal::isFormationSegmentClear;
    CHECK(isFormationSegmentClear(pathfinder, grid, {-6.f, 32.f}, {6.f, 32.f}, 0.5f));
    CHECK(!isFormationSegmentClear(pathfinder, grid, {-6.f, 32.f}, {6.f, 32.f}, 1.1f));
    CHECK(!isFormationSegmentClear(pathfinder, grid, {-6.f, 30.f}, {6.f, 30.f}, 0.2f));
    CHECK(!isFormationSegmentClear(pathfinder, grid, {-1000000.f, 30.f}, {1000000.f, 30.f}, 0.2f));
    CHECK(!isFormationSegmentClear(pathfinder, grid, {-10.f, 20.f}, {-10.f, 20.f}, 1.1f));
}

TEST_CASE("rts.groups.restoreValidatesMembershipAndReplacementDetaches") {
    ecs::Table                   table;
    ecs::ScopedTable             scope(table);
    eve::rts::RTS                rts;
    auto                         a      = subject("00000000-0000-7000-8000-00000000f111");
    auto                         b      = subject("00000000-0000-7000-8000-00000000f112");
    auto*                        first  = rts.newUnit(a).takeValue();
    auto*                        second = rts.newUnit(b).takeValue();
    eve::rts::MovementGroupBatch batch;
    batch.units             = {a, b};
    batch.target            = {20.f, 10.f};
    batch.formation.spacing = 2.f;
    auto issued             = rts.submitMovementGroup(batch);
    REQUIRE(issued.ok());
    auto saved = rts.snapshotState();
    REQUIRE(saved.ok());
    REQUIRE_EQ(saved.value().movementGroups.size(), 1u);
    auto before                            = rts.canonicalStateJson().takeValue();
    auto malformed                         = saved.value();
    malformed.movementGroups[0].members[1] = malformed.movementGroups[0].members[0];
    auto rejected                          = rts.restoreState(malformed);
    CHECK(!rejected.ok());
    CHECK_EQ(rts.canonicalStateJson().value(), before);
    CommandSpec replacement;
    replacement.target = {-5.f, 0.f};
    auto replaced      = first->orders()->values.replace(replacement);
    REQUIRE(replaced.ok());
    PendingRTSExecutor executor;
    auto               stepped = rts.step({eve::SimulationTick{1}, eve::Duration::fromSeconds(0.05).value()}, executor);
    REQUIRE(stepped.ok());
    CHECK_EQ(second->navigation()->formationSpeedFactor, 1.f);
    CHECK(rts.snapshotState().value().movementGroups.empty());
    auto restored = rts.restoreState(saved.value());
    REQUIRE(restored.ok());
    auto afterRestore = rts.snapshotState().takeValue();
    REQUIRE_EQ(afterRestore.movementGroups.size(), 1u);
    CHECK_EQ(afterRestore.movementGroups[0].leadDistance, saved.value().movementGroups[0].leadDistance);
    CHECK_EQ(first->orders()->values.snapshotState().value().queueJson, saved.value().units[0].orders.queueJson);
    CHECK_EQ(second->orders()->values.snapshotState().value().queueJson, saved.value().units[1].orders.queueJson);
    CHECK_EQ(first->motion()->x, saved.value().units[0].motion.x);
    std::vector<Unit*> additionalUnits;
    for (int i = 0; i < 32; ++i) additionalUnits.push_back(Unit::createUnit());
    CHECK_EQ(rts.snapshotState().value().movementGroups.size(), 1u);
    for (auto* unit : additionalUnits) unit->release();
    // Queue clear reuses textual order ids; runtime membership must not revive.
    first->orders()->values.clear();
    auto reused = first->orders()->values.replace(saved.value().units[0].orders.extended.begin()->second);
    REQUIRE(reused.ok());
    CHECK_EQ(reused.value(), saved.value().movementGroups[0].members[0].orderId);
    CHECK(rts.snapshotState().value().movementGroups.empty());
    restored = rts.restoreState(saved.value());
    REQUIRE(restored.ok());
    auto removed = rts.remove(a);
    REQUIRE(removed.ok());
    stepped = rts.step({eve::SimulationTick{2}, eve::Duration::fromSeconds(0.05).value()}, executor);
    REQUIRE(stepped.ok());
    CHECK_EQ(second->navigation()->formationSpeedFactor, 1.f);
    CHECK(rts.snapshotState().value().movementGroups.empty());
}

TEST_CASE("rts.groups.rebuildAndLegacySnapshotMigration") {
    eve::rts::RTSStateSnapshot saved;
    {
        ecs::Table       table;
        ecs::ScopedTable scope(table);
        eve::rts::RTS    rts;
        auto             a = subject("00000000-0000-7000-8000-00000000f121");
        auto             b = subject("00000000-0000-7000-8000-00000000f122");
        REQUIRE(rts.newUnit(a).ok());
        REQUIRE(rts.newUnit(b).ok());
        eve::rts::MovementGroupBatch batch;
        batch.units  = {a, b};
        batch.target = {20.f, 10.f};
        auto issued  = rts.submitMovementGroup(batch);
        REQUIRE(issued.ok());
        saved = rts.snapshotState().takeValue();
    }
    ecs::Table       table;
    ecs::ScopedTable scope(table);
    eve::rts::RTS    rts;
    auto             rebuilt = rts.rebuildState(saved);
    REQUIRE(rebuilt.ok());
    auto rebuiltState = rts.snapshotState().takeValue();
    REQUIRE_EQ(rebuiltState.movementGroups.size(), 1u);
    CHECK_EQ(rebuiltState.movementGroups[0].members[0].orderId, saved.movementGroups[0].members[0].orderId);
    CHECK(rebuiltState.movementGroups[0].members[0].subject == saved.movementGroups[0].members[0].subject);
    CHECK_EQ(rebuiltState.units[0].orders.queueJson, saved.units[0].orders.queueJson);
    CHECK_EQ(rebuiltState.units[1].orders.queueJson, saved.units[1].orders.queueJson);
    // Attribute restore intentionally rebases provider revisions; compare the
    // rebound command/group state above, then use this state for atomicity.
    const auto canonical = rts.canonicalStateJson().takeValue();
    saved.version        = 1;
    CHECK(!rts.restoreState(saved).ok());
    CHECK_EQ(rts.canonicalStateJson().value(), canonical);
    saved.movementGroups.clear();
    auto migrated = rts.restoreState(saved);
    REQUIRE(migrated.ok());
    CHECK(rts.snapshotState().value().movementGroups.empty());
}

TEST_CASE("rts.groups.scriptAndReplayUseCanonicalAdmission") {
    ecs::Table       table;
    ecs::ScopedTable scope(table);
    ssq::VM          vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    const auto a = subject("00000000-0000-7000-8000-00000000f131");
    const auto b = subject("00000000-0000-7000-8000-00000000f132");
    vm.addFunc("makeGroupUnits", [a, b](eve::rts::RTS* sim) {
        auto first  = sim->newUnit(a);
        auto second = sim->newUnit(b);
        if (!first || !second) return false;
        first.value()->motion()->x = second.value()->motion()->x = 5.f;
        first.value()->motion()->y                               = 8.f;
        second.value()->motion()->y                              = 12.f;
        first.value()->motion()->speed = second.value()->motion()->speed = 0.f;
        return true;
    });
    vm.addFunc("groupLead", [](eve::rts::RTS* sim) {
        auto state = sim->snapshotState();
        return state && state.value().movementGroups.size() == 1 ? state.value().movementGroups[0].leadDistance : -1.f;
    });
    vm.run(vm.compileSource(R"(
        local sim = eve.RTS();
        assert(sim.configureScriptWorld(32,32,1.0,0.0,0.0).ok);
        assert(makeGroupUnits(sim));
        local units = ["00000000-0000-7000-8000-00000000f131", "00000000-0000-7000-8000-00000000f132"];
        assert(sim.moveGroupUnits(units,20.0,10.0,"column",2.0,0,0.0,1.5).ok);
        assert(groupLead(sim) == 1.5);
        assert(!sim.moveGroupUnits(units,20.0,10.0,"column",2.0,0,0.0,0.0).ok);
        assert(groupLead(sim) == 1.5);
        assert(sim.queueScriptGroupMove(2,units,20.0,10.0,"column",2.0,0,0.0,3.0).ok);
        local log = sim.exportScriptCommandLog();
        assert(log.ok && log.value.find("EVERTS_COMMANDS 5") == 0);
        assert(sim.importScriptCommandLog(log.value,true).ok);
        assert(sim.stepScript(0.05).ok);
        assert(groupLead(sim) == 1.5);
        assert(sim.stepScript(0.05).ok);
        assert(groupLead(sim) == 3.0);
    )"));
    eve::rts::RTSCommandLog    log;
    eve::rts::RTSReplayCommand group;
    group.operation         = eve::rts::RTSReplayOperation::MovementGroup;
    group.units             = {a, b};
    group.command.target    = {20.f, 10.f};
    group.groupLeadDistance = 3.f;
    auto queued             = log.queue(group);
    REQUIRE(queued.ok());
    const auto              text = log.exportText();
    eve::rts::RTSCommandLog restored;
    auto                    imported = restored.importText(text);
    REQUIRE(imported.ok());
    CHECK_EQ(restored.exportText(), text);
    auto wrongVersion = text;
    wrongVersion.replace(0, 17, "EVERTS_COMMANDS 4\n");
    auto rejected = restored.importText(wrongVersion);
    CHECK(!rejected.ok());
    CHECK_EQ(restored.exportText(), text);
    group.groupLeadDistance = 0.f;
    CHECK(!restored.queue(group).ok());
    CHECK_EQ(restored.exportText(), text);
}
}  // namespace test_rts_groups

// Contract section: rts orders
#include "PrivateTestScope.h"

// rts_orders.cpp
namespace test_rts_orders {
TEST_CASE("rts.facadeCommandsContainmentAndCloakUseOwnedStableRoots") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    Faction*         faction   = rts.newFaction(subject("00000000-0000-7000-8000-00000000ee01")).value();
    Unit*            transport = rts.newFactionUnit(*faction, subject("00000000-0000-7000-8000-00000000ee02")).value();
    Unit*            passenger = rts.newFactionUnit(*faction, subject("00000000-0000-7000-8000-00000000ee03")).value();
    Building*        bunker = rts.newFactionBuilding(*faction, subject("00000000-0000-7000-8000-00000000ee04")).value();
    transport->containment()->capacity = 2;
    transport->motion()->x             = 3.0f;
    transport->motion()->y             = 4.0f;
    passenger->motion()->x             = 3.0f;
    passenger->motion()->y             = 4.0f;
    passenger->motion()->arrived       = true;

    CommandSpec board;
    board.kind         = OrderKind::BoardTransport;
    board.target       = {3.0f, 4.0f};
    board.targetEntity = ecs::handle_of(transport);
    const std::array passengerSelection{passenger->identity()->subject};
    REQUIRE(rts.commandUnits(passengerSelection, board).ok());
    REQUIRE(eve::rts::ContainmentSystem::step().ok());
    CHECK(passenger->containment()->container.resolve() == transport);
    auto unloaded = rts.unloadTransport(*transport, {8.0f, 5.0f});
    REQUIRE(unloaded.ok());
    CHECK_EQ(unloaded.value(), std::size_t{1});
    CHECK(!passenger->containment()->container.isBound());

    auto bunkerLink = eve::rts::ContainerLink::bind(ecs::handle_of(bunker));
    REQUIRE(bunkerLink.ok());
    passenger->containment()->container = std::move(bunkerLink).takeValue();
    bunker->garrison()->occupants.push_back(ecs::handle_of(passenger));
    bunker->capture()->blockedByGarrison = true;
    auto evacuated                       = rts.evacuateBuilding(*bunker, {10.0f, 6.0f});
    REQUIRE(evacuated.ok());
    CHECK_EQ(evacuated.value(), std::size_t{1});
    CHECK(!bunker->capture()->blockedByGarrison);

    REQUIRE(rts.setUnitCloaked(*passenger, true).ok());
    CHECK(passenger->vision()->cloaked);
    Unit* foreign = Unit::createUnit(subject("00000000-0000-7000-8000-00000000ee05"));
    CHECK(!rts.setUnitCloaked(*foreign, true).ok());
    CHECK(!rts.unloadTransport(*foreign, {}).ok());
    foreign->release();
}

TEST_CASE("rts.formationPlannerIsDeterministic") {
    const FormationSpec spec{FormationKind::Grid, 10.0f, 2};
    auto                planned = eve::rts::FormationPlanner::plan(3, {100.0f, 200.0f}, spec);
    REQUIRE(planned.ok());
    const auto positions = std::move(planned).takeValue();
    REQUIRE_EQ(positions.size(), 3u);
    CHECK(std::abs(positions[0].x - 95.0f) < 1e-5f);
    CHECK(std::abs(positions[0].y - 195.0f) < 1e-5f);
    CHECK(std::abs(positions[1].x - 105.0f) < 1e-5f);
    CHECK(std::abs(positions[1].y - 195.0f) < 1e-5f);
    CHECK(std::abs(positions[2].x - 95.0f) < 1e-5f);
    CHECK(std::abs(positions[2].y - 205.0f) < 1e-5f);
}

TEST_CASE("rts.commandFanOutUsesGenericOrdersAndFormation") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    std::vector<Unit*>             units;
    std::vector<ecs::EntityHandle> handles;
    for (int index = 0; index < 3; ++index) {
        Unit* unit            = Unit::createUnit();
        unit->motion()->speed = 100.0f;
        units.push_back(unit);
        handles.push_back(ecs::handle_of(unit));
    }

    const CommandSpec   command{OrderKind::Move, {100.0f, 200.0f}, {}, 0, 0.0};
    const FormationSpec formation{FormationKind::Grid, 10.0f, 2};
    auto                receiptResult = eve::rts::CommandFanOutSystem::fanOut(handles, command, formation);
    REQUIRE(receiptResult.ok());
    const auto receipt = std::move(receiptResult).takeValue();
    CHECK_EQ(receipt.requested, 3u);
    CHECK_EQ(receipt.accepted, 3u);
    CHECK_EQ(receipt.orderIds.size(), 3u);

    for (std::size_t index = 0; index < units.size(); ++index) {
        auto current = units[index]->orders()->values.current();
        REQUIRE(current.ok());
        const auto order = std::move(current).takeValue();
        CHECK_EQ(static_cast<int>(order.kind), static_cast<int>(OrderKind::Move));
        CHECK_EQ(order.formationSlot, static_cast<int>(index));
        CHECK_EQ(order.id, receipt.orderIds[index]);
    }

    const std::size_t beforeRejectedFanOut = units[0]->orders()->values.orderCount();
    units[2]->release();
    auto staleResult = eve::rts::CommandFanOutSystem::fanOut(handles, command, formation);
    CHECK(!staleResult.ok());
    staleResult.ignore("expected stale selection in fan-out test");
    CHECK_EQ(units[0]->orders()->values.orderCount(), beforeRejectedFanOut);

    units[0]->release();
    units[1]->release();
}

TEST_CASE("rts.commandFanOutReplacesDirectCommandsAndAppendsQueuedWaypoints") {
    ecs::Table                       world;
    ecs::ScopedTable                 guard(world);
    Unit*                            unit = Unit::createUnit();
    std::array<ecs::EntityHandle, 1> selection{ecs::handle_of(unit)};
    FormationSpec                    formation{FormationKind::Line, 1.0f, 0};

    CommandSpec oldMove;
    oldMove.kind   = OrderKind::Move;
    oldMove.target = {1.0f, 0.0f};
    REQUIRE(unit->orders()->values.enqueue(oldMove).ok());
    CommandSpec direct = oldMove;
    direct.target      = {5.0f, 0.0f};
    auto replaced      = eve::rts::CommandFanOutSystem::fanOut(selection, direct, formation);
    REQUIRE(replaced.ok());
    // Canonical queue retains the cancelled command in history.
    CHECK_EQ(unit->orders()->values.orderCount(), 2u);
    auto current = unit->orders()->values.current();
    REQUIRE(current.ok());
    CHECK(std::abs(current.value().target.x - 5.0f) < 1e-5f);

    CommandSpec queued = direct;
    queued.target      = {9.0f, 0.0f};
    queued.append      = true;
    auto appended      = eve::rts::CommandFanOutSystem::fanOut(selection, queued, formation);
    REQUIRE(appended.ok());
    CHECK_EQ(unit->orders()->values.orderCount(), 3u);
    current = unit->orders()->values.current();
    REQUIRE(current.ok());
    CHECK(std::abs(current.value().target.x - 5.0f) < 1e-5f);
    auto completed = unit->orders()->values.complete(current.value().id);
    REQUIRE(completed.ok());
    current = unit->orders()->values.current();
    REQUIRE(current.ok());
    CHECK_EQ(current.value().target.x, 9.0f);
    completed = unit->orders()->values.complete(current.value().id);
    REQUIRE(completed.ok());
    CHECK(!unit->orders()->values.current().ok());
    unit->release();
}

TEST_CASE("rts.extendedOrdersPreserveEntityAndAreaPayload") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    Unit*       unit   = Unit::createUnit();
    Unit*       target = Unit::createUnit();
    CommandSpec suppress;
    suppress.kind            = OrderKind::SuppressArea;
    suppress.target          = {12.0f, 8.0f};
    suppress.secondaryTarget = {20.0f, 14.0f};
    suppress.targetEntity    = ecs::handle_of(target);
    suppress.radius          = 6.0f;
    suppress.append          = true;

    auto queued = unit->orders()->values.enqueue(suppress);
    REQUIRE(queued.ok());
    std::move(queued).takeValue();
    auto current = unit->orders()->values.current();
    REQUIRE(current.ok());
    const auto order = std::move(current).takeValue();
    CHECK_EQ(static_cast<int>(order.kind), static_cast<int>(OrderKind::SuppressArea));
    CHECK(std::abs(order.secondaryTarget.x - 20.0f) < 1e-5f);
    CHECK(std::abs(order.radius - 6.0f) < 1e-5f);
    CHECK(order.append);
    CHECK(order.targetEntity.id == ecs::handle_of(target).id);
    CHECK(std::abs(unit->worker()->cargo) < 1e-5f);
    CHECK(!unit->combat()->holdPosition);

    unit->release();
    target->release();
}

TEST_CASE("rts.crowdProviderOwnsLinkedMovementAndOverlapResolution") {
    ecs::Table        world;
    ecs::ScopedTable  guard(world);
    eve::crowd::Crowd crowd;
    crowd.setResolveOverlaps(true);
    crowd.setSeparationRadius(3.0f);
    crowd.setSeparationWeight(2.0f);

    Unit* first      = Unit::createUnit();
    Unit* second     = Unit::createUnit();
    auto  firstLink  = eve::rts::CrowdLink::bind("rts/unit/first");
    auto  secondLink = eve::rts::CrowdLink::bind("rts/unit/second");
    REQUIRE(firstLink.ok());
    REQUIRE(secondLink.ok());
    first->crowd()->link    = std::move(firstLink).takeValue();
    second->crowd()->link   = std::move(secondLink).takeValue();
    first->crowd()->radius  = 0.75f;
    second->crowd()->radius = 0.75f;
    first->motion()->speed  = 4.0f;
    second->motion()->speed = 4.0f;
    CommandSpec move;
    move.kind        = OrderKind::Move;
    move.target      = {10.0f, 0.0f};
    auto firstOrder  = first->orders()->values.enqueue(move);
    auto secondOrder = second->orders()->values.enqueue(move);
    REQUIRE(firstOrder.ok());
    REQUIRE(secondOrder.ok());
    std::move(firstOrder).takeValue();
    std::move(secondOrder).takeValue();

    const eve::SimulationStep step{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.25).expect("crowd dt")};
    auto                      moved = eve::rts::CrowdMotionSystem::step(step, crowd);
    REQUIRE(moved.ok());
    CHECK_EQ(moved.value(), 2u);
    CHECK(crowd.hasNamedAgent("rts/unit/first"));
    CHECK(crowd.hasNamedAgent("rts/unit/second"));
    const float dx = first->motion()->x - second->motion()->x;
    const float dy = first->motion()->y - second->motion()->y;
    CHECK(std::hypot(dx, dy) > 0.0f);

    first->release();
    second->release();
}

TEST_CASE("rts.crowdProjectsMovementPriorityAndUnlocksExactOverlap") {
    ecs::Table        world;
    ecs::ScopedTable  guard(world);
    eve::crowd::Crowd crowd;
    crowd.setClampToField(false);
    crowd.setResolveOverlaps(true);
    crowd.setSeparationRadius(3.0f);
    crowd.setSeparationWeight(0.0f);
    Unit* high     = Unit::createUnit(subject("00000000-0000-7000-8000-000000000401"));
    Unit* low      = Unit::createUnit(subject("00000000-0000-7000-8000-000000000402"));
    auto  highLink = eve::rts::CrowdLink::bind("rts/priority/high");
    auto  lowLink  = eve::rts::CrowdLink::bind("rts/priority/low");
    REQUIRE(highLink.ok());
    REQUIRE(lowLink.ok());
    high->crowd()->link   = std::move(highLink).takeValue();
    low->crowd()->link    = std::move(lowLink).takeValue();
    high->crowd()->radius = low->crowd()->radius = 1.0f;
    high->navigation()->movementPriority         = 10;
    low->navigation()->movementPriority          = -10;
    CommandSpec move;
    move.kind   = OrderKind::Move;
    move.target = {10.0f, 0.0f};
    REQUIRE(high->orders()->values.enqueue(move).ok());
    REQUIRE(low->orders()->values.enqueue(move).ok());
    const eve::SimulationStep step{eve::SimulationTick{1},
                                   eve::Duration::fromSeconds(0.1).expect("priority avoidance dt")};
    REQUIRE(eve::rts::CrowdMotionSystem::step(step, crowd).ok());
    const int highAgent = crowd.getNamedAgentIndex("rts/priority/high");
    const int lowAgent  = crowd.getNamedAgentIndex("rts/priority/low");
    CHECK_EQ(crowd.getAgentAvoidancePriority(highAgent), 10);
    CHECK_EQ(crowd.getAgentAvoidancePriority(lowAgent), -10);
    CHECK(std::hypot(low->motion()->x, low->motion()->y) > std::hypot(high->motion()->x, high->motion()->y));
    high->release();
    low->release();
}

TEST_CASE("rts.navigationUsesCanonicalMapPathAndReportsUnreachableOnce") {
    ecs::Table           world;
    ecs::ScopedTable     guard(world);
    eve::map::Pathfinder pathfinder(5, 3);
    pathfinder.setDiagonal(false);
    pathfinder.setBlocked(1, 1, true);

    Unit* unit            = Unit::createUnit();
    unit->motion()->x     = 0.0f;
    unit->motion()->y     = 1.0f;
    unit->motion()->speed = 1.0f;
    CommandSpec move;
    move.kind   = OrderKind::Move;
    move.target = {4.0f, 1.0f};
    auto queued = unit->orders()->values.enqueue(move);
    REQUIRE(queued.ok());
    std::move(queued).takeValue();

    int  unreachableCount = 0;
    auto planned = eve::rts::NavigationSystem::step(pathfinder, {}, [&](Unit& reported, const eve::rts::OrderRecord&) {
        CHECK(&reported == unit);
        ++unreachableCount;
    });
    REQUIRE(planned.ok());
    CHECK(!unit->navigation()->unreachable);
    REQUIRE(!unit->navigation()->waypoints.empty());
    CHECK(std::abs(unit->navigation()->waypoints.front().y - 1.0f) > 0.5f);

    const eve::SimulationStep tick{eve::SimulationTick{1}, eve::Duration::fromSeconds(1.0).expect("navigation dt")};
    auto                      moved = eve::rts::MotionSystem::step(tick);
    REQUIRE(moved.ok());
    CHECK(std::abs(unit->motion()->y - 1.0f) > 0.5f);

    pathfinder.setBlocked(3, 1, true);
    pathfinder.setBlocked(4, 0, true);
    pathfinder.setBlocked(4, 2, true);
    auto blockedQueue = unit->orders()->values.replace(move);
    REQUIRE(blockedQueue.ok());
    std::move(blockedQueue).takeValue();
    auto blocked = eve::rts::NavigationSystem::step(pathfinder, {},
                                                    [&](Unit&, const eve::rts::OrderRecord&) { ++unreachableCount; });
    REQUIRE(blocked.ok());
    CHECK(unit->navigation()->unreachable);
    CHECK_EQ(unreachableCount, 1);
    auto repeated = eve::rts::NavigationSystem::step(pathfinder, {},
                                                     [&](Unit&, const eve::rts::OrderRecord&) { ++unreachableCount; });
    REQUIRE(repeated.ok());
    CHECK_EQ(unreachableCount, 1);

    unit->release();
}

TEST_CASE("rts.trafficReservationsHonorPriorityAndRecoverAtNarrowCells") {
    ecs::Table           world;
    ecs::ScopedTable     guard(world);
    eve::map::Pathfinder pathfinder(3, 1);
    pathfinder.setDiagonal(false);

    Unit* low                            = Unit::createUnit(subject("00000000-0000-7000-8000-000000000202"));
    Unit* high                           = Unit::createUnit(subject("00000000-0000-7000-8000-000000000201"));
    low->motion()->x                     = 0.0f;
    high->motion()->x                    = 2.0f;
    low->navigation()->movementPriority  = 1;
    high->navigation()->movementPriority = 9;
    CommandSpec lowMove;
    lowMove.kind         = OrderKind::Move;
    lowMove.target       = {2.0f, 0.0f};
    CommandSpec highMove = lowMove;
    highMove.target      = {0.0f, 0.0f};
    REQUIRE(low->orders()->values.enqueue(lowMove).ok());
    REQUIRE(high->orders()->values.enqueue(highMove).ok());
    REQUIRE(eve::rts::NavigationSystem::step(pathfinder, {}).ok());

    auto reserved = eve::rts::TrafficReservationSystem::step(pathfinder, {});
    REQUIRE(reserved.ok());
    CHECK(low->navigation()->trafficWaiting);
    CHECK(!high->navigation()->trafficWaiting);

    high->motion()->x = 1.0f;
    REQUIRE(eve::rts::NavigationSystem::step(pathfinder, {}).ok());
    REQUIRE(eve::rts::TrafficReservationSystem::step(pathfinder, {}).ok());
    CHECK(low->navigation()->trafficWaiting);
    high->release();
    REQUIRE(eve::rts::TrafficReservationSystem::step(pathfinder, {}).ok());
    CHECK(!low->navigation()->trafficWaiting);

    low->release();
}

TEST_CASE("rts.arrivedMovementOrdersAdvanceTheCanonicalMixedQueue") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Unit*            unit = Unit::createUnit();
    unit->motion()->speed = 2.0f;

    CommandSpec first;
    first.kind   = OrderKind::Move;
    first.target = {1.0f, 0.0f};
    CommandSpec second;
    second.kind   = OrderKind::AttackMove;
    second.target = {3.0f, 0.0f};
    CommandSpec third;
    third.kind   = OrderKind::Move;
    third.target = {4.0f, 0.0f};
    REQUIRE(unit->orders()->values.enqueue(first).ok());
    REQUIRE(unit->orders()->values.enqueue(second).ok());
    REQUIRE(unit->orders()->values.enqueue(third).ok());

    const eve::SimulationStep tick{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.5).expect("movement queue dt")};
    REQUIRE(eve::rts::MotionSystem::step(tick).ok());
    CHECK(unit->motion()->arrived);
    auto settled = eve::rts::MovementOrderSystem::step();
    REQUIRE(settled.ok());
    CHECK_EQ(settled.value(), 1u);
    auto current = unit->orders()->values.current();
    REQUIRE(current.ok());
    CHECK_EQ(static_cast<int>(current.value().kind), static_cast<int>(OrderKind::AttackMove));

    REQUIRE(eve::rts::MotionSystem::step(tick).ok());
    CHECK(!unit->motion()->arrived);
    REQUIRE(eve::rts::MotionSystem::step(tick).ok());
    CHECK(unit->motion()->arrived);
    REQUIRE(eve::rts::MovementOrderSystem::step().ok());
    current = unit->orders()->values.current();
    REQUIRE(current.ok());
    CHECK_EQ(static_cast<int>(current.value().kind), static_cast<int>(OrderKind::Move));
    unit->release();
}

TEST_CASE("rts.patrolPersistsAndAlternatesBetweenCommandOriginAndTarget") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Unit*            unit = Unit::createUnit();
    unit->motion()->x     = 1.0f;
    unit->motion()->speed = 2.0f;
    CommandSpec patrol;
    patrol.kind   = OrderKind::Patrol;
    patrol.target = {3.0f, 0.0f};
    REQUIRE(unit->orders()->values.enqueue(patrol).ok());
    REQUIRE(eve::rts::PatrolSystem::step().ok());
    CHECK(unit->navigation()->patrolInitialized);
    CHECK(unit->navigation()->patrolTowardTarget);
    CHECK(std::abs(unit->navigation()->patrolOrigin.x - 1.0f) < 1e-5f);

    const eve::SimulationStep tick{eve::SimulationTick{1}, eve::Duration::fromSeconds(1.0).expect("patrol dt")};
    REQUIRE(eve::rts::MotionSystem::step(tick).ok());
    CHECK(std::abs(unit->motion()->x - 3.0f) < 1e-5f);
    REQUIRE(eve::rts::PatrolSystem::step().ok());
    CHECK(!unit->navigation()->patrolTowardTarget);
    REQUIRE(eve::rts::MotionSystem::step(tick).ok());
    CHECK(std::abs(unit->motion()->x - 1.0f) < 1e-5f);
    CHECK(!unit->orders()->values.empty());
    unit->release();
}

TEST_CASE("rts.radarCreatesQuantizedUntargetableContactsAndJammingStopsRefresh") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue       = Faction::createFaction(subject("00000000-0000-7000-8000-000000000106"));
    Faction*         red        = Faction::createFaction(subject("00000000-0000-7000-8000-000000000107"));
    Unit*            radar      = Unit::createUnit(subject("00000000-0000-7000-8000-000000000108"));
    Unit*            target     = Unit::createUnit(subject("00000000-0000-7000-8000-000000000109"));
    Unit*            jammer     = Unit::createUnit(subject("00000000-0000-7000-8000-000000000110"));
    auto             blueLink   = eve::rts::FactionLink::bind(ecs::handle_of(blue));
    auto             targetLink = eve::rts::FactionLink::bind(ecs::handle_of(red));
    auto             jammerLink = eve::rts::FactionLink::bind(ecs::handle_of(red));
    REQUIRE(blueLink.ok());
    REQUIRE(targetLink.ok());
    REQUIRE(jammerLink.ok());
    radar->faction()->link           = std::move(blueLink).takeValue();
    target->faction()->link          = std::move(targetLink).takeValue();
    jammer->faction()->link          = std::move(jammerLink).takeValue();
    radar->vision()->radarRange      = 20.0f;
    radar->vision()->radarResolution = 2.0f;
    target->motion()->x              = 5.2f;
    target->motion()->y              = 3.1f;
    jammer->motion()->x              = 30.0f;

    eve::map::Fov                   blueFov(40, 40), redFov(40, 40);
    eve::rts::FogOfWarSystem::State fog;
    const auto                      provider = [&](Faction& faction) { return &faction == blue ? &blueFov : &redFov; };
    const eve::SimulationStep       first{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.5).expect("radar dt")};
    REQUIRE(eve::rts::FogOfWarSystem::step(first, {}, fog, provider).ok());
    const auto* contact = eve::rts::FogOfWarSystem::contact(*blue, target->identity()->subject);
    REQUIRE(contact != nullptr);
    CHECK_EQ(contact->kind, "radar_unit");
    CHECK(!contact->visible);
    CHECK(!contact->detected);
    CHECK(std::abs(contact->position.x - 5.0f) < 1e-5f);
    CHECK(std::abs(contact->position.y - 3.0f) < 1e-5f);

    // Keep the moved target inside the jammer radius for the blocked refresh.
    jammer->motion()->x            = 8.0f;
    jammer->motion()->y            = 3.1f;
    jammer->vision()->jammingRange = 3.0f;
    target->motion()->x            = 9.0f;
    const eve::SimulationStep second{eve::SimulationTick{2}, eve::Duration::fromSeconds(1.0).expect("jam dt")};
    REQUIRE(eve::rts::FogOfWarSystem::step(second, {}, fog, provider).ok());
    contact = eve::rts::FogOfWarSystem::contact(*blue, target->identity()->subject);
    REQUIRE(contact != nullptr);
    CHECK(std::abs(contact->position.x - 5.0f) < 1e-5f);
    CHECK(std::abs(contact->ageSeconds - 1.0) < 1e-6);

    target->motion()->x = 13.0f;
    const eve::SimulationStep third{eve::SimulationTick{3}, second.delta};
    auto                      refreshed = eve::rts::FogOfWarSystem::step(third, {}, fog, provider);
    REQUIRE(refreshed.ok());
    contact = eve::rts::FogOfWarSystem::contact(*blue, target->identity()->subject);
    REQUIRE(contact != nullptr);
    CHECK_EQ(contact->position.x, 13.0f);
    CHECK_EQ(contact->ageSeconds, 0.0);


    eve::rts::FogOfWarSystem::clear(fog);
    jammer->release();
    target->release();
    radar->release();
    red->release();
    blue->release();
}

TEST_CASE("rts.commandNetworkUsesRelaysCapacityPriorityAndHostileJamming") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue        = Faction::createFaction(subject("00000000-0000-7000-8000-000000000221"));
    Faction*         red         = Faction::createFaction(subject("00000000-0000-7000-8000-000000000222"));
    Building*        hq          = Building::createBuilding(subject("00000000-0000-7000-8000-000000000223"));
    Unit*            relay       = Unit::createUnit(subject("00000000-0000-7000-8000-000000000224"));
    Unit*            high        = Unit::createUnit(subject("00000000-0000-7000-8000-000000000225"));
    Unit*            low         = Unit::createUnit(subject("00000000-0000-7000-8000-000000000226"));
    Unit*            jammer      = Unit::createUnit(subject("00000000-0000-7000-8000-000000000227"));
    auto             bindFaction = [&](auto& entity, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        entity.faction()->link = std::move(link).takeValue();
    };
    bindFaction(*hq, *blue);
    bindFaction(*relay, *blue);
    bindFaction(*high, *blue);
    bindFaction(*low, *blue);
    bindFaction(*jammer, *red);
    hq->integrity()->state.health = hq->integrity()->state.maxHealth = 100.0;
    hq->command()->range                                             = 10.0f;
    hq->command()->capacity                                          = 1;
    relay->motion()->x                                               = 8.0f;
    relay->command()->range                                          = 12.0f;
    relay->command()->capacity                                       = 1;
    relay->command()->cost                                           = 1;
    relay->command()->priority                                       = 100;
    relay->command()->relayRequiresUplink                            = true;
    relay->command()->requiresCommand                                = true;
    high->motion()->x                                                = 16.0f;
    high->command()->requiresCommand                                 = true;
    high->command()->priority                                        = 10;
    low->motion()->x                                                 = 17.0f;
    low->motion()->speed                                             = 2.0f;
    low->command()->requiresCommand                                  = true;
    low->command()->priority                                         = 1;
    low->command()->outOfCommandSpeedFactor                          = 0.5f;
    jammer->motion()->x                                              = 40.0f;
    jammer->command()->jammingRange                                  = 5.0f;

    auto connected = eve::rts::CommandNetworkSystem::step();
    REQUIRE(connected.ok());
    CHECK(relay->command()->relayActive);
    CHECK(high->command()->inCommand);
    CHECK(!low->command()->inCommand);
    CHECK_EQ(relay->command()->load, 1);

    jammer->motion()->x = 8.0f;
    auto jammed         = eve::rts::CommandNetworkSystem::step();
    REQUIRE(jammed.ok());
    CHECK(relay->command()->jammed);
    CHECK(!relay->command()->relayActive);
    CHECK(!high->command()->inCommand);
    CommandSpec move;
    move.kind   = OrderKind::Move;
    move.target = {27.0f, 0.0f};
    REQUIRE(low->orders()->values.replace(move).ok());
    auto moved = eve::rts::MotionSystem::step(
        {eve::SimulationTick{1}, eve::Duration::fromSeconds(1.0).expect("command motion dt")});
    REQUIRE(moved.ok());
    CHECK(std::abs(low->motion()->x - 18.0f) < 1e-5f);

    jammer->release();
    low->release();
    high->release();
    relay->release();
    hq->release();
    red->release();
    blue->release();
}
}  // namespace test_rts_orders

// Contract section: rts production
#include "PrivateTestScope.h"

// rts_production.cpp
namespace test_rts_production {
TEST_CASE("rts.buildingTouchesConstructionDropoffAndRally") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    Building* building = Building::createBuilding();
    CHECK(std::abs(building->construction()->progress - 1.0f) < 1e-5f);
    CHECK(building->dropoff()->acceptedResources.empty());
    CHECK(!building->rally()->enabled);
    building->release();
}

TEST_CASE("rts.aiRequestsProductionAndLaunchesAttackMove") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue     = Faction::createFaction();
    Faction*         red      = Faction::createFaction();
    Building*        producer = Building::createBuilding();
    Building*        target   = Building::createBuilding();
    const auto       workerId = eve::LogicalId::parse("rts:worker");
    const auto       armyId   = eve::LogicalId::parse("rts:marine");
    const auto       baseId   = eve::LogicalId::parse("rts:command-center");
    REQUIRE(workerId.has_value());
    REQUIRE(armyId.has_value());
    REQUIRE(baseId.has_value());
    Unit* first    = Unit::createUnit({}, *armyId);
    Unit* second   = Unit::createUnit({}, *armyId);
    auto  bindUnit = [&](Unit& unit, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        unit.faction()->link = std::move(link).takeValue();
    };
    auto bindBuilding = [&](Building& building, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        building.faction()->link = std::move(link).takeValue();
    };
    bindUnit(*first, *blue);
    bindUnit(*second, *blue);
    bindBuilding(*producer, *blue);
    bindBuilding(*target, *red);
    target->definition()->id                   = *baseId;
    target->placement()->worldX                = 20.0f;
    target->placement()->worldY                = 4.0f;
    blue->strategy()->enabled                  = true;
    blue->strategy()->workerDefinition         = *workerId;
    blue->strategy()->armyDefinition           = *armyId;
    blue->strategy()->targetBuildingDefinition = *baseId;
    blue->strategy()->desiredWorkers           = 0;
    blue->strategy()->attackThreshold          = 2;
    blue->strategy()->thinkInterval            = 0.5f;

    int                       productionRequests = 0;
    eve::LogicalId            requestedDefinition;
    const eve::SimulationStep step{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.5).expect("AI dt")};
    auto                      result =
        eve::rts::AISystem::step(step, [&](Faction& faction, Building& building, const eve::LogicalId& definition) {
            CHECK(&faction == blue);
            CHECK(&building == producer);
            ++productionRequests;
            requestedDefinition = definition;
            return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
        });
    REQUIRE(result.ok());
    CHECK_EQ(productionRequests, 1);
    CHECK(requestedDefinition == *armyId);
    CHECK(!first->orders()->values.empty());
    CHECK(!second->orders()->values.empty());
    CHECK_EQ(first->tactics()->combatGroup, second->tactics()->combatGroup);
    CHECK(first->tactics()->combatGroup != 0);

    first->release();
    second->release();
    producer->release();
    target->release();
    blue->release();
    red->release();
}

TEST_CASE("rts.transportBoardingUsesGenerationCheckedContainment") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    Faction* faction          = Faction::createFaction();
    Unit*    transport        = Unit::createUnit();
    Unit*    passenger        = Unit::createUnit();
    auto     transportFaction = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    auto     passengerFaction = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    REQUIRE(transportFaction.ok());
    REQUIRE(passengerFaction.ok());
    transport->faction()->link         = std::move(transportFaction).takeValue();
    passenger->faction()->link         = std::move(passengerFaction).takeValue();
    transport->containment()->capacity = 2;
    transport->motion()->x             = 4.0f;
    transport->motion()->y             = 7.0f;
    passenger->motion()->arrived       = true;

    CommandSpec board;
    board.kind         = OrderKind::BoardTransport;
    board.target       = {4.0f, 7.0f};
    board.targetEntity = ecs::handle_of(transport);
    auto queued        = passenger->orders()->values.enqueue(board);
    REQUIRE(queued.ok());
    std::move(queued).takeValue();
    auto boarded = eve::rts::ContainmentSystem::step();
    REQUIRE(boarded.ok());
    CHECK_EQ(boarded.value(), 1u);
    CHECK(passenger->containment()->container.resolve() == transport);
    CHECK_EQ(transport->containment()->occupants.size(), 1u);
    CHECK(passenger->orders()->values.empty());

    transport->motion()->x = 9.0f;
    auto synchronized      = eve::rts::ContainmentSystem::step();
    REQUIRE(synchronized.ok());
    CHECK(std::abs(passenger->motion()->x - 9.0f) < 1e-5f);

    auto unloaded = eve::rts::ContainmentSystem::unload(*transport, {12.0f, 3.0f});
    REQUIRE(unloaded.ok());
    CHECK_EQ(unloaded.value(), 1u);
    CHECK(transport->containment()->occupants.empty());
    CHECK(!passenger->containment()->container.isBound());
    CHECK(passenger->motion()->x > 12.0f);

    Building* bunker        = Building::createBuilding();
    auto      bunkerFaction = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    REQUIRE(bunkerFaction.ok());
    bunker->faction()->link = std::move(bunkerFaction).takeValue();
    auto bunkerLink         = eve::rts::ContainerLink::bind(ecs::handle_of(bunker));
    REQUIRE(bunkerLink.ok());
    passenger->containment()->container = std::move(bunkerLink).takeValue();
    bunker->garrison()->occupants.push_back(ecs::handle_of(passenger));
    bunker->capture()->blockedByGarrison = true;
    auto evacuated                       = eve::rts::ContainmentSystem::evacuate(*bunker, {15.0f, 5.0f});
    REQUIRE(evacuated.ok());
    CHECK_EQ(evacuated.value(), 1u);
    CHECK(bunker->garrison()->occupants.empty());
    CHECK(!bunker->capture()->blockedByGarrison);
    CHECK(!passenger->containment()->container.isBound());

    passenger->release();
    transport->release();
    bunker->release();
    faction->release();
}

TEST_CASE("rts.buildingProductionAndModuleFactoryCompose") {
    eve::rts::RTS module;
    auto          unitResult = module.newUnit(subject("00000000-0000-7000-8000-000000000011"));
    REQUIRE(unitResult.ok());
    Unit* unit = std::move(unitResult).takeValue();
    CHECK_EQ(module.unitCount(), 1u);

    auto buildingResult = module.newBuilding(subject("00000000-0000-7000-8000-000000000012"));
    REQUIRE(buildingResult.ok());
    Building* building = std::move(buildingResult).takeValue();
    CHECK_EQ(module.buildingCount(), 1u);

    auto duration = eve::Duration::fromSeconds(1.0);
    REQUIRE(duration.ok());
    auto task = building->production()->values.enqueue("faction", "train", "worker", std::move(duration).takeValue());
    REQUIRE(task.ok());
    std::move(task).takeValue();

    eve::action::ActionRuntime runtime;
    eve::rts::ActionAdapter    adapter(runtime);
    const eve::SimulationStep  step{eve::SimulationTick{1}, eve::Duration::fromNanoseconds(500000000)};
    auto                       processed = module.step(step, adapter);
    REQUIRE(processed.ok());
    CHECK(processed.value() >= 2u);
    CHECK_EQ(building->production()->values.taskCount(), 1u);

    (void)unit;
}

TEST_CASE("rts.completedProductionBoardsDispatchesAndUnloadsTransportReinforcements") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         faction          = Faction::createFaction();
    Building*        factory          = Building::createBuilding();
    Unit*            transport        = Unit::createUnit();
    auto             factoryFaction   = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    auto             transportFaction = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    REQUIRE(factoryFaction.ok());
    REQUIRE(transportFaction.ok());
    factory->faction()->link               = std::move(factoryFaction).takeValue();
    transport->faction()->link             = std::move(transportFaction).takeValue();
    transport->containment()->capacity     = 2;
    factory->rally()->enabled              = true;
    factory->rally()->command.kind         = OrderKind::AttackMove;
    factory->rally()->command.target       = {20.0f, 4.0f};
    factory->rally()->combatGroup          = 77;
    factory->rally()->transport            = ecs::handle_of(transport);
    const auto transportHandle             = ecs::handle_of(transport);
    factory->rally()->minimumTransportLoad = 2;
    const auto duration                    = eve::Duration::fromSeconds(1.0).expect("reinforcement duration");
    REQUIRE(factory->production()->values.enqueue("faction", "unit", "marine", duration).ok());
    REQUIRE(factory->production()->values.enqueue("faction", "unit", "marine", duration).ok());

    std::vector<ecs::EntityHandle> spawned;
    eve::rts::ProductionSpawn      spawn = [&](Building&, const eve::production::ProductionTask&,
                                          eve::rts::WorldPosition requested) {
        Unit* unit        = Unit::createUnit();
        unit->motion()->x = requested.x;
        unit->motion()->y = requested.y;
        auto link         = eve::rts::FactionLink::bind(ecs::handle_of(faction));
        if (!link) return eve::Result<eve::rts::ProductionSpawnOutcome>::failure(link.status());
        unit->faction()->link = std::move(link).takeValue();
        spawned.push_back(ecs::handle_of(unit));
        return eve::Result<eve::rts::ProductionSpawnOutcome>::success({eve::rts::ProductionSpawnState::Created, unit});
    };
    auto first = eve::rts::BuildingProductionSystem::step({eve::SimulationTick{1}, duration}, spawn);
    REQUIRE(first.ok());
    auto second = eve::rts::BuildingProductionSystem::step({eve::SimulationTick{2}, duration}, spawn);
    REQUIRE(second.ok());
    REQUIRE_EQ(spawned.size(), 2u);
    transport = dynamic_cast<Unit*>(ecs::try_get(transportHandle));
    REQUIRE(transport != nullptr);
    CHECK_EQ(transport->containment()->occupants.size(), 2u);

    auto dispatched = eve::rts::ReinforcementSystem::step();
    REQUIRE(dispatched.ok());
    CHECK(factory->rally()->transportActive);
    transport->motion()->arrived = false;
    auto travelling              = eve::rts::ReinforcementSystem::step();
    REQUIRE(travelling.ok());
    CHECK_EQ(transport->containment()->occupants.size(), 2u);
    transport->motion()->x       = 20.0f;
    transport->motion()->y       = 4.0f;
    transport->motion()->arrived = true;
    auto arrived                 = eve::rts::ReinforcementSystem::step();
    REQUIRE(arrived.ok());
    CHECK(!factory->rally()->transportActive);
    CHECK(transport->containment()->occupants.empty());
    for (const auto& handle : spawned) {
        Unit* unit = dynamic_cast<Unit*>(ecs::try_get(handle));
        REQUIRE(unit != nullptr);
        CHECK(!unit->containment()->container.isBound());
        CHECK_EQ(unit->tactics()->combatGroup, 77u);
        CHECK_EQ(unit->motion()->x, 20.0f);
        auto order = unit->orders()->values.current();
        REQUIRE(order.ok());
        CHECK_EQ(static_cast<int>(order.value().kind), static_cast<int>(OrderKind::AttackMove));
    }
}

TEST_CASE("rts.factionResourceFloorsProtectHighPriorityProductionAcrossFactories") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    auto             factionResult  = rts.newFaction(subject("00000000-0000-7000-8000-0000000004a1"));
    auto             barracksResult = rts.newBuilding(subject("00000000-0000-7000-8000-0000000004a2"));
    auto             factoryResult  = rts.newBuilding(subject("00000000-0000-7000-8000-0000000004a3"));
    REQUIRE(factionResult.ok());
    REQUIRE(barracksResult.ok());
    REQUIRE(factoryResult.ok());
    Faction*  faction  = std::move(factionResult).takeValue();
    Building* barracks = std::move(barracksResult).takeValue();
    Building* factory  = std::move(factoryResult).takeValue();
    for (Building* building : {barracks, factory}) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(faction));
        REQUIRE(link.ok());
        building->faction()->link = std::move(link).takeValue();
    }
    REQUIRE(rts.setProductionResourceReserve(*faction, "minerals", 100, 10).ok());
    REQUIRE(rts.setProductionResourceReserve(*faction, "gas", 25, 10).ok());
    eve::economy::EconomyLedger ledger;
    const auto                  creditedMinerals = ledger.credit("minerals", 150);
    const auto                  creditedGas      = ledger.credit("gas", 25);
    REQUIRE_EQ(creditedMinerals, 150);
    REQUIRE_EQ(creditedGas, 25);
    eve::rts::RTSEconomyAdapter economy(ledger);
    eve::action::ActionRuntime  action;
    const auto                  duration = eve::Duration::fromSeconds(1.0).expect("resource floor production duration");

    auto marineCost = eve::resource::CostSpec::single("minerals", 50);
    REQUIRE(marineCost.ok());
    auto firstMarine = rts.build(*barracks, action, economy.account(), std::move(marineCost).takeValue(), "marine",
                                 duration, "unit", 1);
    REQUIRE(firstMarine.ok());
    CHECK_EQ(ledger.get("minerals"), 100);
    marineCost = eve::resource::CostSpec::single("minerals", 50);
    REQUIRE(marineCost.ok());
    auto blockedMarine = rts.build(*barracks, action, economy.account(), std::move(marineCost).takeValue(), "marine",
                                   duration, "unit", 1);
    CHECK(!blockedMarine.ok());
    CHECK_EQ(ledger.get("minerals"), 100);

    auto tankCost = eve::resource::CostSpec::from({{"minerals", 100}, {"gas", 25}});
    REQUIRE(tankCost.ok());
    auto tank =
        rts.build(*factory, action, economy.account(), std::move(tankCost).takeValue(), "tank", duration, "unit", 10);
    REQUIRE(tank.ok());
    CHECK_EQ(ledger.get("minerals"), 0);
    CHECK_EQ(ledger.get("gas"), 0);
    auto snapshot = rts.snapshotState();
    REQUIRE(snapshot.ok());
    REQUIRE_EQ(snapshot.value().factions.size(), 1u);
    CHECK_EQ(snapshot.value().factions[0].productionPolicy.resourceReserves.at("minerals").amount, 100);
    auto canonical = rts.canonicalStateJson();
    REQUIRE(canonical.ok());
    CHECK(canonical.value().find("productionPolicy") != std::string::npos);

    REQUIRE(rts.setProductionResourceReserve(*faction, "minerals", 0, 10).ok());
    CHECK(!faction->productionPolicy()->resourceReserves.contains("minerals"));
}

TEST_CASE("rts.sharedReinforcementCapReservesLastSlotByTypePriority") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         faction         = Faction::createFaction();
    Building*        infantryFactory = Building::createBuilding(subject("00000000-0000-7000-8000-0000000003f1"));
    Building*        armorFactory    = Building::createBuilding(subject("00000000-0000-7000-8000-0000000003f2"));
    for (Building* factory : {infantryFactory, armorFactory}) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(faction));
        REQUIRE(link.ok());
        factory->faction()->link                                    = std::move(link).takeValue();
        factory->rally()->enabled                                   = true;
        factory->rally()->combatGroup                               = 91;
        factory->rally()->reinforcementLimit                        = 1;
        factory->rally()->reinforcementTypePriorities["rts:marine"] = 1;
        factory->rally()->reinforcementTypePriorities["rts:tank"]   = 10;
    }
    const auto duration   = eve::Duration::fromSeconds(5.0).expect("reinforcement policy duration");
    auto       marineTask = infantryFactory->production()->values.enqueue("faction", "unit", "rts:marine", duration);
    auto       tankTask   = armorFactory->production()->values.enqueue("faction", "unit", "rts:tank", duration);
    REQUIRE(marineTask.ok());
    REQUIRE(tankTask.ok());
    const std::string marineId = marineTask.value();
    const std::string tankId   = tankTask.value();

    REQUIRE(eve::rts::ReinforcementProductionPolicySystem::step().ok());
    REQUIRE(infantryFactory->production()->values.find(marineId));
    REQUIRE(armorFactory->production()->values.find(tankId));
    CHECK_EQ(static_cast<int>(infantryFactory->production()->values.find(marineId)->get().state),
             static_cast<int>(eve::production::TaskState::Paused));
    CHECK_NE(static_cast<int>(armorFactory->production()->values.find(tankId)->get().state),
             static_cast<int>(eve::production::TaskState::Paused));
    CHECK(infantryFactory->rally()->reinforcementCapped);

    const auto tankDefinition = eve::LogicalId::parse("rts:tank");
    REQUIRE(tankDefinition.has_value());
    Unit* existing        = Unit::createUnit({}, *tankDefinition);
    auto  existingFaction = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    REQUIRE(existingFaction.ok());
    existing->faction()->link        = std::move(existingFaction).takeValue();
    existing->tactics()->combatGroup = 91;
    REQUIRE(eve::rts::ReinforcementProductionPolicySystem::step().ok());
    CHECK_EQ(static_cast<int>(armorFactory->production()->values.find(tankId)->get().state),
             static_cast<int>(eve::production::TaskState::Paused));

    existing->durability()->alive = false;
    REQUIRE(eve::rts::ReinforcementProductionPolicySystem::step().ok());
    CHECK_NE(static_cast<int>(armorFactory->production()->values.find(tankId)->get().state),
             static_cast<int>(eve::production::TaskState::Paused));
    CHECK(armorFactory->rally()->reinforcementPolicyPausedTask.empty());

    existing->release();
    armorFactory->release();
    infantryFactory->release();
    faction->release();
}

TEST_CASE("rts.reinforcementRequestUsesDeterministicFallbackChain") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Building*        factory                             = Building::createBuilding();
    factory->rally()->reinforcementFallbacks["rts:tank"] = "rts:marine";
    std::vector<std::string>       attempts;
    eve::rts::ReinforcementEnqueue enqueue = [&](Building& producer, std::string_view product) {
        CHECK_EQ(&producer, factory);
        attempts.emplace_back(product);
        if (product == "rts:tank")
            return eve::Result<std::string>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "tank unavailable", "product"));
        return producer.production()->values.enqueue("faction", "unit", product,
                                                     eve::Duration::fromSeconds(1.0).expect("fallback duration"));
    };
    auto requested = eve::rts::ReinforcementProductionPolicySystem::request(*factory, "rts:tank", enqueue);
    REQUIRE(requested.ok());
    CHECK_EQ(requested.value().requestedProduct, "rts:tank");
    CHECK_EQ(requested.value().queuedProduct, "rts:marine");
    REQUIRE_EQ(attempts.size(), 2u);
    CHECK_EQ(attempts[0], "rts:tank");
    CHECK_EQ(attempts[1], "rts:marine");

    factory->rally()->reinforcementFallbacks["rts:marine"] = "rts:tank";
    auto cyclic =
        eve::rts::ReinforcementProductionPolicySystem::request(*factory, "rts:tank", [&](Building&, std::string_view) {
            return eve::Result<std::string>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "unavailable", "product"));
        });
    CHECK(!cyclic.ok());
    CHECK_EQ(cyclic.code(), eve::StatusCode::Rejected);
    factory->release();
}

TEST_CASE("rts.cappedReinforcementUsesInjectedAtomicCancelRefundBoundary") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         faction         = Faction::createFaction();
    Building*        factory         = Building::createBuilding();
    Unit*            existing        = Unit::createUnit();
    auto             buildingFaction = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    auto             unitFaction     = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    REQUIRE(buildingFaction.ok());
    REQUIRE(unitFaction.ok());
    factory->faction()->link                       = std::move(buildingFaction).takeValue();
    existing->faction()->link                      = std::move(unitFaction).takeValue();
    factory->rally()->enabled                      = true;
    factory->rally()->combatGroup                  = 92;
    factory->rally()->reinforcementLimit           = 1;
    factory->rally()->reinforcementAutoCancelDelay = 1.0f;
    existing->tactics()->combatGroup               = 92;
    auto task = factory->production()->values.enqueue("faction", "unit", "rts:marine",
                                                      eve::Duration::fromSeconds(5.0).expect("auto cancel duration"));
    REQUIRE(task.ok());
    const std::string             taskId   = task.value();
    bool                          refunded = false;
    eve::rts::ReinforcementCancel cancel   = [&](Building& producer, std::string_view id) {
        CHECK_EQ(&producer, factory);
        CHECK_EQ(id, taskId);
        auto cancelled = producer.production()->values.cancel(id, "reinforcement capped");
        if (cancelled) refunded = true;
        return cancelled;
    };
    const eve::SimulationStep half{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.5).expect("half capped delay")};
    REQUIRE(eve::rts::ReinforcementProductionPolicySystem::step(half, cancel).ok());
    CHECK(!refunded);
    const eve::SimulationStep secondHalf{eve::SimulationTick{2}, half.delta};
    REQUIRE(eve::rts::ReinforcementProductionPolicySystem::step(secondHalf, cancel).ok());
    CHECK(refunded);
    REQUIRE(factory->production()->values.find(taskId));
    CHECK_EQ(static_cast<int>(factory->production()->values.find(taskId)->get().state),
             static_cast<int>(eve::production::TaskState::Cancelled));
    CHECK(!factory->rally()->reinforcementCapped);

    existing->release();
    factory->release();
    faction->release();
}

TEST_CASE("rts.productionBlockedExitWaitsAndSpawnsAtFirstAvailablePosition") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Building*        factory     = Building::createBuilding();
    factory->placement()->worldX = 4.0f;
    factory->placement()->worldY = 5.0f;
    const auto duration          = eve::Duration::fromSeconds(1.0).expect("blocked exit duration");
    auto       queued            = factory->production()->values.enqueue("faction", "unit", "marine", duration);
    REQUIRE(queued.ok());
    const std::string                 taskId   = std::move(queued).takeValue();
    int                               probes   = 0;
    eve::rts::ProductionSpawnPosition position = [&](Building&, const eve::production::ProductionTask&) {
        ++probes;
        if (probes == 1) return eve::Result<std::optional<eve::rts::WorldPosition>>::success(std::nullopt);
        return eve::Result<std::optional<eve::rts::WorldPosition>>::success(eve::rts::WorldPosition{8.0f, 9.0f});
    };
    std::vector<ecs::EntityHandle> spawned;
    eve::rts::ProductionSpawn      spawn = [&](Building&, const eve::production::ProductionTask&,
                                          eve::rts::WorldPosition requested) {
        Unit* unit        = Unit::createUnit();
        unit->motion()->x = requested.x;
        unit->motion()->y = requested.y;
        spawned.push_back(ecs::handle_of(unit));
        return eve::Result<eve::rts::ProductionSpawnOutcome>::success({eve::rts::ProductionSpawnState::Created, unit});
    };

    std::vector<eve::rts::LifecycleEvent> productionEvents;
    auto collectProduction = [&](const eve::rts::LifecycleEvent& event, eve::SimulationTick) {
        productionEvents.push_back(event);
    };
    auto blocked = eve::rts::BuildingProductionSystem::step({eve::SimulationTick{1}, duration}, spawn, position,
                                                            collectProduction);
    REQUIRE(blocked.ok());
    CHECK(spawned.empty());
    CHECK(factory->rally()->productionSpawnBlocked);
    CHECK_EQ(factory->rally()->blockedProductionTask, taskId);
    CHECK(factory->rally()->settledProductionTasks.empty());
    REQUIRE_EQ(productionEvents.size(), 1u);
    CHECK_EQ(static_cast<int>(productionEvents[0].kind),
             static_cast<int>(eve::rts::LifecycleEventKind::ProductionSpawnBlocked));

    auto cleared = eve::rts::BuildingProductionSystem::step({eve::SimulationTick{2}, duration}, spawn, position,
                                                            collectProduction);
    REQUIRE(cleared.ok());
    REQUIRE_EQ(spawned.size(), 1u);
    auto* unit = dynamic_cast<Unit*>(ecs::try_get(spawned.front()));
    REQUIRE(unit != nullptr);
    CHECK_EQ(unit->motion()->x, 8.0f);
    CHECK_EQ(unit->motion()->y, 9.0f);
    CHECK(!factory->rally()->productionSpawnBlocked);
    CHECK(factory->rally()->blockedProductionTask.empty());
    CHECK_EQ(factory->rally()->settledProductionTasks.size(), 1u);
    REQUIRE_EQ(productionEvents.size(), 3u);
    CHECK_EQ(static_cast<int>(productionEvents[1].kind),
             static_cast<int>(eve::rts::LifecycleEventKind::ProductionSpawnCleared));
    CHECK_EQ(static_cast<int>(productionEvents[2].kind), static_cast<int>(eve::rts::LifecycleEventKind::UnitProduced));
}

TEST_CASE("rts.rallyFacadeConfiguresLinkedReinforcementPolicyAndExclusiveTransport") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    REQUIRE(rts.configureScriptWorld(16, 16, 1.0f).ok());
    REQUIRE(rts.loadScriptContent(R"({
        "units":[{"id":"marine","health":80,"speed":3,"radius":0.3},
                 {"id":"medic","health":60,"speed":3,"radius":0.3},
                 {"id":"apc","health":200,"speed":4,"radius":0.8}],
        "buildings":[{"id":"barracks","health":500,"buildTime":2}]
    })")
                .ok());
    auto*      faction    = rts.newFaction(subject("00000000-0000-7000-8000-0000000002a1")).value();
    const auto barracksId = eve::LogicalId::parse("building:barracks");
    const auto apcId      = eve::LogicalId::parse("unit:apc");
    REQUIRE(barracksId.has_value());
    REQUIRE(apcId.has_value());
    auto* first =
        rts.newFactionBuilding(*faction, subject("00000000-0000-7000-8000-0000000002a2"), *barracksId).value();
    auto* second =
        rts.newFactionBuilding(*faction, subject("00000000-0000-7000-8000-0000000002a3"), *barracksId).value();
    auto* third =
        rts.newFactionBuilding(*faction, subject("00000000-0000-7000-8000-0000000002a4"), *barracksId).value();
    auto* transport = rts.newFactionUnit(*faction, subject("00000000-0000-7000-8000-0000000002a5"), *apcId).value();
    transport->containment()->capacity = 4;

    CommandSpec rally;
    rally.kind   = OrderKind::AttackMove;
    rally.target = {12.0f, 6.0f};
    REQUIRE(rts.setBuildingRally(*first, rally, true).ok());
    REQUIRE(rts.linkBuildingRally(*second, *first).ok());
    CHECK(first->rally()->combatGroup != 0);
    CHECK_EQ(second->rally()->combatGroup, first->rally()->combatGroup);
    REQUIRE(rts.setReinforcementLimit(*first, 8).ok());
    REQUIRE(rts.setReinforcementTypeLimit(*first, "marine", 5).ok());
    REQUIRE(rts.setReinforcementTypePriority(*second, "medic", 3).ok());
    REQUIRE(rts.setReinforcementFallback(*first, "medic", "marine").ok());
    CHECK(!rts.setReinforcementFallback(*first, "marine", "medic").ok());
    REQUIRE(rts.setReinforcementAutoCancel(*first, 2.5f).ok());
    for (auto* building : {first, second}) {
        CHECK_EQ(building->rally()->reinforcementLimit, std::size_t{8});
        CHECK_EQ(building->rally()->reinforcementTypeLimits.at("marine"), std::size_t{5});
        CHECK_EQ(building->rally()->reinforcementTypePriorities.at("medic"), 3);
        CHECK_EQ(building->rally()->reinforcementFallbacks.at("medic"), "marine");
        CHECK_EQ(building->rally()->reinforcementAutoCancelDelay, 2.5f);
    }
    REQUIRE(rts.setReinforcementTransport(*first, transport, 2).ok());
    CHECK(!rts.setReinforcementTransport(*third, transport, 2).ok());
    REQUIRE(rts.setReinforcementTransport(*first, nullptr).ok());
    REQUIRE(rts.setReinforcementTransport(*third, transport, 2).ok());
    REQUIRE(rts.clearBuildingRally(*second).ok());
    CHECK(!second->rally()->enabled);
    CHECK_EQ(second->rally()->combatGroup, std::uint64_t{0});
}

TEST_CASE("rts.productionPreservesFactoryResolvedPlacement") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Building*        factory     = Building::createBuilding();
    factory->placement()->worldX = 3.0f;
    factory->placement()->worldY = 4.0f;
    const auto duration          = eve::Duration::fromSeconds(1.0).expect("production duration");
    auto       queued            = factory->production()->values.enqueue("faction", "unit", "marine", duration);
    REQUIRE(queued.ok());
    Unit*                     produced = nullptr;
    eve::rts::ProductionSpawn spawn    = [&](Building&, const eve::production::ProductionTask&,
                                          eve::rts::WorldPosition requested) {
        CHECK_EQ(requested.x, 3.0f);
        CHECK_EQ(requested.y, 4.0f);
        produced              = Unit::createUnit();
        produced->motion()->x = requested.x + 2.0f;
        produced->motion()->y = requested.y - 1.0f;
        return eve::Result<eve::rts::ProductionSpawnOutcome>::success(
            {eve::rts::ProductionSpawnState::Created, produced});
    };
    auto settled = eve::rts::BuildingProductionSystem::step({eve::SimulationTick{1}, duration}, spawn);
    REQUIRE(settled.ok());
    REQUIRE(produced != nullptr);
    if (produced == nullptr) {
        factory->release();
        return;
    }
    CHECK_EQ(produced->motion()->x, 5.0f);
    CHECK_EQ(produced->motion()->y, 3.0f);
    CHECK_EQ(factory->rally()->settledProductionTasks.size(), 1u);
    produced->release();
    factory->release();
}
}  // namespace test_rts_production

// Contract section: rts restore
#include "PrivateTestScope.h"

// rts_restore.cpp
namespace test_rts_restore {
TEST_CASE("rts.buildingAmmoProductionPurchasesFiniteStockAndSnapshotsFractionalProgress") {
    eve::rts::RTS module;
    auto          buildingResult = module.newBuilding(subject("00000000-0000-7000-8000-000000000010"));
    REQUIRE(buildingResult.ok());
    Building* building                         = std::move(buildingResult).takeValue();
    building->supply()->capacity               = 3.0f;
    building->supply()->productionResource     = "minerals";
    building->supply()->productionCostPerRound = 2;
    building->supply()->productionRate         = 2.0f;
    std::int64_t                     bank      = 5;
    eve::rts::AmmoProductionPurchase purchase  = [&](Building& producer, std::string_view resource,
                                                    std::int64_t unitCost,
                                                    std::size_t  requested) -> eve::Result<std::size_t> {
        CHECK_EQ(&producer, building);
        CHECK_EQ(resource, "minerals");
        const auto affordable = static_cast<std::size_t>(bank / unitCost);
        const auto bought     = std::min(requested, affordable);
        bank -= static_cast<std::int64_t>(bought) * unitCost;
        return eve::Result<std::size_t>::success(bought);
    };

    auto partial = eve::rts::SupplySystem::step(
        {eve::SimulationTick{1}, eve::Duration::fromSeconds(0.25).expect("ammo partial")}, purchase);
    REQUIRE(partial.ok());
    CHECK_EQ(building->supply()->stock, 0.0f);
    CHECK_EQ(building->supply()->productionProgress, 0.5f);
    auto snapshot = module.snapshotState();
    REQUIRE(snapshot.ok());
    building->supply()->productionProgress = 0.0f;
    REQUIRE(module.restoreState(snapshot.value()).ok());
    CHECK_EQ(building->supply()->productionProgress, 0.5f);

    std::vector<eve::rts::LifecycleEvent> events;
    auto                                  produced = eve::rts::SupplySystem::step(
        {eve::SimulationTick{2}, eve::Duration::fromSeconds(1.0).expect("ammo production")}, purchase, nullptr, {},
        [&](const eve::rts::LifecycleEvent& event, eve::SimulationTick tick) {
            CHECK_EQ(tick.value(), 2u);
            events.push_back(event);
        });
    REQUIRE(produced.ok());
    CHECK_EQ(produced.value(), 2u);
    CHECK_EQ(building->supply()->stock, 2.0f);
    CHECK_EQ(building->supply()->productionProgress, 0.5f);
    CHECK_EQ(bank, 1);
    REQUIRE_EQ(events.size(), 1u);
    CHECK_EQ(static_cast<int>(events[0].kind), static_cast<int>(eve::rts::LifecycleEventKind::AmmoProduced));
    CHECK_EQ(events[0].source, building->identity()->subject);
    CHECK_EQ(events[0].detail, "minerals");
    CHECK_EQ(events[0].value, 2.0);

    auto unaffordable = eve::rts::SupplySystem::step(
        {eve::SimulationTick{3}, eve::Duration::fromSeconds(1.0).expect("ammo insufficient")}, purchase);
    REQUIRE(unaffordable.ok());
    CHECK_EQ(building->supply()->stock, 2.0f);
    CHECK_EQ(building->supply()->productionProgress, 1.0f);
    CHECK_EQ(bank, 1);
}

TEST_CASE("rts.orderAndProductionAdaptersRoundTripCanonicalQueueSnapshots") {
    ecs::Table               world;
    ecs::ScopedTable         guard(world);
    Unit*                    target = Unit::createUnit();
    eve::rts::OrderComponent orders;
    CommandSpec              move;
    move.kind            = OrderKind::SuppressArea;
    move.target          = {7.0f, 9.0f};
    move.secondaryTarget = {11.0f, 13.0f};
    move.radius          = 2.5f;
    move.append          = true;
    move.targetEntity    = ecs::handle_of(target);
    REQUIRE(orders.enqueue(move).ok());
    auto orderState = orders.snapshotState();
    REQUIRE(orderState.ok());
    eve::rts::OrderComponent restoredOrders;
    REQUIRE(restoredOrders.restoreState(orderState.value()).ok());
    auto current = restoredOrders.current();
    REQUIRE(current.ok());
    CHECK_EQ(static_cast<int>(current.value().kind), static_cast<int>(OrderKind::SuppressArea));
    CHECK_EQ(current.value().target.x, 7.0f);
    CHECK_EQ(current.value().target.y, 9.0f);
    CHECK_EQ(current.value().secondaryTarget.x, 11.0f);
    CHECK_EQ(current.value().secondaryTarget.y, 13.0f);
    CHECK_EQ(current.value().radius, 2.5f);
    CHECK(current.value().append);
    CHECK_EQ(ecs::try_get(current.value().targetEntity), target);

    eve::rts::ProductionComponent production;
    const auto                    duration = eve::Duration::fromSeconds(2.0).expect("snapshot production duration");
    REQUIRE(production.enqueue("faction", "unit", "marine", duration).ok());
    auto productionJson = production.snapshot();
    REQUIRE(productionJson.ok());
    eve::rts::ProductionComponent restoredProduction;
    REQUIRE(restoredProduction.restore(productionJson.value()).ok());
    CHECK_EQ(restoredProduction.taskCount(), 1u);

    const auto before = restoredProduction.snapshot();
    REQUIRE(before.ok());
    auto rejected = restoredProduction.restore("{not-json");
    CHECK(!rejected.ok());
    auto after = restoredProduction.snapshot();
    REQUIRE(after.ok());
    CHECK_EQ(after.value(), before.value());
    target->release();
}

TEST_CASE("rts.rootSnapshotRestoresStableRelationshipsAndRejectsTopologyMismatch") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    module;
    auto             factionResult  = module.newFaction(subject("00000000-0000-7000-8000-000000000081"));
    auto             unitResult     = module.newUnit(subject("00000000-0000-7000-8000-000000000082"));
    auto             buildingResult = module.newBuilding(subject("00000000-0000-7000-8000-000000000083"));
    auto             nodeResult =
        module.newResourceNode(subject("00000000-0000-7000-8000-000000000084"), "minerals", 500.0f, {8.0f, 6.0f}, 2);
    auto playerResult = module.newPlayer(subject("00000000-0000-7000-8000-000000000085"));
    auto matchResult  = module.newMatch(subject("00000000-0000-7000-8000-000000000086"));
    REQUIRE(factionResult.ok());
    REQUIRE(unitResult.ok());
    REQUIRE(buildingResult.ok());
    REQUIRE(nodeResult.ok());
    REQUIRE(playerResult.ok());
    REQUIRE(matchResult.ok());
    Faction*      faction         = factionResult.value();
    Unit*         unit            = unitResult.value();
    Building*     building        = buildingResult.value();
    ResourceNode* node            = nodeResult.value();
    Player*       player          = playerResult.value();
    Match*        match           = matchResult.value();
    auto          unitFaction     = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    auto          buildingFaction = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    auto          nodeLink        = eve::rts::ResourceNodeLink::bind(ecs::handle_of(node));
    auto          dropoffLink     = eve::rts::BuildingLink::bind(ecs::handle_of(building));
    REQUIRE(unitFaction.ok());
    REQUIRE(buildingFaction.ok());
    REQUIRE(nodeLink.ok());
    REQUIRE(dropoffLink.ok());
    unit->faction()->link                      = std::move(unitFaction).takeValue();
    building->faction()->link                  = std::move(buildingFaction).takeValue();
    unit->worker()->resourceNode               = std::move(nodeLink).takeValue();
    unit->worker()->dropoff                    = std::move(dropoffLink).takeValue();
    unit->worker()->cargo                      = 17.0f;
    unit->motion()->x                          = 3.0f;
    unit->motion()->y                          = 4.0f;
    unit->tactics()->coordinatedVolleyInterval = 0.75f;
    unit->tactics()->volleyReleaseRemaining    = 0.25f;
    unit->tactics()->volleyHolding             = true;
    unit->artillery()->observedFireSpotter     = ecs::handle_of(unit);
    unit->artillery()->usingObservedFire       = true;
    REQUIRE(module.setUnitAttribute(*unit, "armor", 7.0).ok());
    REQUIRE(unit->tags()->values.add("unit.worker").ok());
    REQUIRE(building->tags()->values.add("building.dropoff").ok());
    eve::rts::RTSEffectDefinition effect;
    effect.id       = "snapshot-morale";
    effect.source   = "test";
    effect.duration = 5.0;
    REQUIRE(module.applyEffect(*unit, effect).ok());
    building->construction()->builders.push_back(ecs::handle_of(unit));
    building->rally()->enabled                 = true;
    building->rally()->productionSpawnBlocked  = true;
    building->rally()->blockedProductionTask   = "task-7";
    building->combat()->airDefenseNetworkRange = 9.0f;
    building->combat()->airDefenseNetworkRoot  = ecs::handle_of(building);
    building->combat()->airDefenseNetworkSize  = 3;
    building->rally()->command.kind            = OrderKind::Escort;
    building->rally()->command.targetEntity    = ecs::handle_of(unit);
    node->harvest()->workers.push_back(ecs::handle_of(unit));
    faction->members()->units.push_back(ecs::handle_of(unit));
    faction->members()->buildings.push_back(ecs::handle_of(building));
    faction->workforce()->autoConstruction = true;
    faction->intel()->enabled              = true;
    player->selection()->units.push_back(ecs::handle_of(unit));
    player->selection()->buildings.push_back(ecs::handle_of(building));
    Match::Participants::Entry participant;
    auto                       participantLink = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    REQUIRE(participantLink.ok());
    participant.faction = std::move(participantLink).takeValue();
    participant.team    = 3;
    match->participants()->entries.push_back(std::move(participant));
    CommandSpec attack;
    attack.kind         = OrderKind::Attack;
    attack.target       = {8.0f, 6.0f};
    attack.targetEntity = ecs::handle_of(building);
    REQUIRE(unit->orders()->values.enqueue(attack).ok());
    auto duration = eve::Duration::fromSeconds(3.0).expect("snapshot queue duration");
    REQUIRE(building->production()->values.enqueue("faction", "unit", "marine", duration).ok());

    auto snapshot = module.snapshotState();
    REQUIRE(snapshot.ok());
    unit->motion()->x                          = 99.0f;
    unit->worker()->cargo                      = 0.0f;
    unit->tactics()->coordinatedVolleyInterval = 0.0f;
    unit->tactics()->volleyReleaseRemaining    = 0.0f;
    unit->tactics()->volleyHolding             = false;
    unit->artillery()->observedFireSpotter     = {};
    unit->artillery()->usingObservedFire       = false;
    REQUIRE(module.setUnitAttribute(*unit, "armor", 1.0).ok());
    REQUIRE(unit->tags()->values.remove("unit.worker").ok());
    REQUIRE(building->tags()->values.remove("building.dropoff").ok());
    building->construction()->builders.clear();
    node->stock()->remaining                  = 1.0f;
    building->rally()->command.targetEntity   = {};
    building->rally()->productionSpawnBlocked = false;
    building->rally()->blockedProductionTask.clear();
    building->combat()->airDefenseNetworkRange = 0.0f;
    building->combat()->airDefenseNetworkRoot  = {};
    building->combat()->airDefenseNetworkSize  = 0;
    faction->members()->units.clear();
    faction->workforce()->autoConstruction = false;
    faction->intel()->enabled              = false;
    player->selection()->units.clear();
    match->participants()->entries.clear();
    REQUIRE(module.restoreState(snapshot.value()).ok());
    CHECK_EQ(unit->motion()->x, 3.0f);
    CHECK_EQ(unit->worker()->cargo, 17.0f);
    CHECK_EQ(unit->tactics()->coordinatedVolleyInterval, 0.75f);
    CHECK_EQ(unit->tactics()->volleyReleaseRemaining, 0.25f);
    CHECK(unit->tactics()->volleyHolding);
    CHECK(faction->intel()->enabled);
    CHECK_EQ(ecs::try_get(unit->artillery()->observedFireSpotter), unit);
    CHECK(unit->artillery()->usingObservedFire);
    auto armor = module.readUnitAttribute(*unit, "armor");
    REQUIRE(armor.ok());
    CHECK_EQ(armor.value(), 7.0);
    CHECK(unit->tags()->values.contains("unit.worker"));
    CHECK(building->tags()->values.contains("building.dropoff"));
    CHECK_EQ(unit->effects()->values.count(), 1u);
    CHECK_EQ(unit->worker()->resourceNode.resolve(), node);
    CHECK_EQ(building->construction()->builders.size(), 1u);
    CHECK_EQ(ecs::try_get(building->rally()->command.targetEntity), unit);
    CHECK(building->rally()->productionSpawnBlocked);
    CHECK_EQ(building->rally()->blockedProductionTask, "task-7");
    CHECK_EQ(building->combat()->airDefenseNetworkRange, 9.0f);
    CHECK_EQ(ecs::try_get(building->combat()->airDefenseNetworkRoot), building);
    CHECK_EQ(building->combat()->airDefenseNetworkSize, 3u);
    CHECK_EQ(node->stock()->remaining, 500.0f);
    auto restoredOrder = unit->orders()->values.current();
    REQUIRE(restoredOrder.ok());
    CHECK_EQ(ecs::try_get(restoredOrder.value().targetEntity), building);
    CHECK_EQ(building->production()->values.taskCount(), 1u);
    CHECK_EQ(faction->members()->units.size(), 1u);
    CHECK(faction->workforce()->autoConstruction);
    CHECK_EQ(player->selection()->units.size(), 1u);
    REQUIRE_EQ(match->participants()->entries.size(), 1u);
    CHECK_EQ(match->participants()->entries.front().faction.resolve(), faction);
    CHECK_EQ(match->participants()->entries.front().team, 3);

    auto invalid = snapshot.value();
    invalid.resourceNodes.clear();
    unit->motion()->x = 42.0f;
    auto rejected     = module.restoreState(invalid);
    CHECK(!rejected.ok());
    CHECK_EQ(unit->motion()->x, 42.0f);

    invalid                                   = snapshot.value();
    invalid.units.front().worker.resourceNode = building->identity()->subject;
    rejected                                  = module.restoreState(invalid);
    CHECK(!rejected.ok());
    CHECK_EQ(unit->motion()->x, 42.0f);
}

TEST_CASE("rts.ballisticProjectileUsesAbsoluteHeightsAndSnapshotsThreeDimensionalFlight") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         faction     = Faction::createFaction(subject("00000000-0000-7000-8000-00000000e701"));
    Unit*            source      = Unit::createUnit(subject("00000000-0000-7000-8000-00000000e702"));
    auto             factionLink = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    REQUIRE(factionLink.ok());
    source->faction()->link = std::move(factionLink).takeValue();

    eve::weapon::WeaponDefinition definition;
    definition.id                 = "height-mortar";
    definition.damage             = 1.0f;
    definition.range              = 10.0f;
    definition.projectile.speed   = 10.0f;
    definition.projectile.gravity = 4.0f;
    definition.blockedByObstacles = true;
    eve::rts::RTSProjectileSystem projectiles;
    REQUIRE(projectiles
                .launch(source->identity()->subject, source->faction()->link.handle(), {}, {}, {6.0f, 0.0f}, definition,
                        1.0, {}, 1.0f, 1.0f)
                .ok());
    auto launched = projectiles.snapshot();
    REQUIRE(!launched.runtime.slots.empty());
    REQUIRE(launched.runtime.slots[0].state.has_value());
    CHECK_EQ(launched.runtime.slots[0].state->position.y, 1.0);
    CHECK(launched.runtime.slots[0].state->velocity.y > 0.0);

    eve::combat::DamageRuntime damage;
    bool                       sawThreeDimensionalSweep = false;
    auto                       advanced                 = projectiles.step(
        {eve::SimulationTick{1}, eve::Duration::fromSeconds(0.2).expect("ballistic height dt")}, damage,
        [&](eve::rts::WorldPosition, float fromHeight, eve::rts::WorldPosition, float toHeight, eve::SubjectRef,
            ecs::EntityHandle) {
            sawThreeDimensionalSweep = true;
            CHECK(fromHeight >= 1.0f);
            CHECK(toHeight > fromHeight);
            return eve::Result<std::optional<eve::rts::ProjectileCollision>>::success(std::nullopt);
        });
    REQUIRE(advanced.ok());
    CHECK(sawThreeDimensionalSweep);
    const auto inFlight = projectiles.snapshot();
    REQUIRE(inFlight.runtime.slots[0].state.has_value());
    CHECK(inFlight.runtime.slots[0].state->position.y > 1.0);

    eve::rts::RTSProjectileSystem restored;
    REQUIRE(restored
                .restore(inFlight,
                         [&](eve::SubjectRef stable) -> ecs::Entity* {
                             if (stable == source->identity()->subject) return source;
                             if (stable == faction->identity()->subject) return faction;
                             return nullptr;
                         })
                .ok());
    CHECK(restored.snapshot().runtime.slots[0].state->position == inFlight.runtime.slots[0].state->position);
    // The ballistic backend uses discrete integration: keep the restored
    // flight at a simulation-sized step instead of one 500 ms approximation.
    std::size_t impacts = 0;
    for (std::uint64_t tick = 2; tick <= 61 && restored.activeCount() != 0; ++tick) {
        auto completed = restored.step(
            {eve::SimulationTick{tick}, eve::Duration::fromSeconds(0.01).expect("ballistic impact dt")}, damage);
        REQUIRE(completed.ok());
        impacts += completed.value();
    }
    CHECK_EQ(impacts, std::size_t{1});
    CHECK_EQ(restored.activeCount(), std::size_t{0});
    source->release();
    faction->release();
}

TEST_CASE("rts.rebuildSnapshotMaterializesTopologyAndRollsBackInvalidRelationships") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    source;
    auto             factionResult = source.newFaction(subject("00000000-0000-7000-8000-000000000261"));
    auto             unitResult    = source.newUnit(subject("00000000-0000-7000-8000-000000000262"));
    auto             nodeResult =
        source.newResourceNode(subject("00000000-0000-7000-8000-000000000263"), "ore", 80.0f, {4.0f, 5.0f}, 2);
    REQUIRE(factionResult.ok());
    REQUIRE(unitResult.ok());
    REQUIRE(nodeResult.ok());
    auto* faction                 = factionResult.value();
    auto* unit                    = unitResult.value();
    auto* node                    = nodeResult.value();
    unit->identity()->displayName = "Harvester";
    unit->motion()->x             = 2.0f;
    auto factionLink              = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    auto nodeLink                 = eve::rts::ResourceNodeLink::bind(ecs::handle_of(node));
    REQUIRE(factionLink.ok());
    REQUIRE(nodeLink.ok());
    unit->faction()->link        = std::move(factionLink).takeValue();
    unit->worker()->resourceNode = std::move(nodeLink).takeValue();
    faction->members()->units.push_back(ecs::handle_of(unit));
    node->harvest()->workers.push_back(ecs::handle_of(unit));

    auto captured = source.snapshotState();
    REQUIRE(captured.ok());
    eve::rts::RTS rebuilt;
    REQUIRE(rebuilt.rebuildState(captured.value()).ok());
    CHECK_EQ(rebuilt.unitCount(), 1u);
    CHECK_EQ(rebuilt.factionCount(), 1u);
    CHECK_EQ(rebuilt.resourceNodeCount(), 1u);
    auto* restoredUnit = rebuilt.findUnit(unit->identity()->subject);
    REQUIRE(restoredUnit != nullptr);
    CHECK_EQ(restoredUnit->identity()->displayName, "Harvester");
    CHECK(std::abs(restoredUnit->motion()->x - 2.0f) < 1e-5f);
    CHECK(restoredUnit->faction()->link.resolve() != nullptr);
    CHECK(restoredUnit->worker()->resourceNode.resolve() != nullptr);

    eve::rts::RTS invalidTarget;
    auto          invalid         = captured.value();
    invalid.units.front().faction = subject("00000000-0000-7000-8000-000000000269");
    auto rejected                 = invalidTarget.rebuildState(invalid);
    CHECK(!rejected.ok());
    CHECK_EQ(invalidTarget.unitCount(), 0u);
    CHECK_EQ(invalidTarget.factionCount(), 0u);
    CHECK_EQ(invalidTarget.resourceNodeCount(), 0u);
}

TEST_CASE("rts.commandLogRoundTripsStableSubjectsAndAppliesAtExactTick") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    const auto       firstSubject  = subject("00000000-0000-7000-8000-000000000271");
    const auto       secondSubject = subject("00000000-0000-7000-8000-000000000272");
    const auto       targetSubject = subject("00000000-0000-7000-8000-000000000273");
    eve::rts::RTS    source;
    REQUIRE(source.newUnit(firstSubject).ok());
    REQUIRE(source.newUnit(secondSubject).ok());
    REQUIRE(source.newBuilding(targetSubject).ok());

    eve::rts::RTSReplayCommand command;
    command.tick                 = eve::SimulationTick{7};
    command.units                = {secondSubject, firstSubject, secondSubject};
    command.command.kind         = OrderKind::Attack;
    command.command.target       = {9.0f, 4.0f};
    command.command.targetEntity = ecs::handle_of(source.findBuilding(targetSubject));
    command.targetEntity         = targetSubject;
    command.formation            = {eve::rts::FormationKind::Line, 2.0f, 0};
    eve::rts::RTSCommandLog recorded;
    REQUIRE(recorded.queue(command, eve::SimulationTick{3}).ok());
    CHECK_EQ(recorded.size(), 1u);
    const std::string text = recorded.exportText();
    CHECK(text.starts_with("EVERTS_COMMANDS 1\n"));

    eve::rts::RTS replay;
    REQUIRE(replay.newUnit(firstSubject).ok());
    REQUIRE(replay.newUnit(secondSubject).ok());
    REQUIRE(replay.newBuilding(targetSubject).ok());
    eve::rts::RTSCommandLog imported;
    REQUIRE(imported.importText(text, eve::SimulationTick{3}).ok());
    CHECK_EQ(imported.exportText(), text);
    auto early = imported.apply(eve::SimulationTick{6}, replay);
    REQUIRE(early.ok());
    CHECK_EQ(early.value(), 0u);
    auto applied = imported.apply(eve::SimulationTick{7}, replay);
    REQUIRE(applied.ok());
    CHECK_EQ(applied.value(), 2u);
    for (eve::SubjectRef unitSubject : {firstSubject, secondSubject}) {
        auto current = replay.findUnit(unitSubject)->orders()->values.current();
        REQUIRE(current.ok());
        CHECK_EQ(static_cast<int>(current.value().kind), static_cast<int>(OrderKind::Attack));
        CHECK(ecs::try_get(current.value().targetEntity) == replay.findBuilding(targetSubject));
    }
    CHECK(!imported.importText("EVERTS_COMMANDS 99\n", eve::SimulationTick{}).ok());
    CHECK(!imported.importText(text, eve::SimulationTick{8}).ok());
    CHECK_EQ(imported.size(), 1u);
}

TEST_CASE("rts.genericReplayCoversQueuedMovementGroundFireAndControlOrdersAtExactTicks") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    module;
    const auto       unitSubject      = subject("00000000-0000-7000-8000-00000000d191");
    const auto       buildingSubject  = subject("00000000-0000-7000-8000-00000000d192");
    const auto       transportSubject = subject("00000000-0000-7000-8000-00000000d193");
    auto*            unit             = module.newUnit(unitSubject).value();
    module.newBuilding(buildingSubject).value();
    module.newUnit(transportSubject).value();
    eve::rts::RTSCommandLog recorded;
    const auto queue = [&](std::uint64_t tick, OrderKind kind, eve::rts::WorldPosition point = {}, bool append = false,
                           eve::SubjectRef target = {}, float spacing = 0.0f) {
        eve::rts::RTSReplayCommand replay;
        replay.tick           = eve::SimulationTick{tick};
        replay.units          = {unitSubject};
        replay.command.kind   = kind;
        replay.command.target = point;
        replay.command.append = append;
        replay.targetEntity   = target;
        if (spacing > 0.0f) {
            replay.formation.kind    = FormationKind::Grid;
            replay.formation.spacing = spacing;
        }
        return recorded.queue(std::move(replay));
    };
    REQUIRE(queue(2, OrderKind::Move, {3.0f, 4.0f}, false, {}, 1.25f).ok());
    REQUIRE(queue(2, OrderKind::AttackMove, {6.0f, 7.0f}, true, {}, 1.5f).ok());
    REQUIRE(queue(3, OrderKind::AttackGround, {8.0f, 9.0f}, true).ok());
    REQUIRE(queue(4, OrderKind::Stop).ok());
    REQUIRE(queue(5, OrderKind::HoldPosition).ok());
    REQUIRE(queue(6, OrderKind::Patrol, {2.0f, 5.0f}).ok());
    REQUIRE(queue(7, OrderKind::Repair, {}, false, buildingSubject).ok());
    REQUIRE(queue(8, OrderKind::Capture, {}, false, buildingSubject).ok());
    REQUIRE(queue(9, OrderKind::Garrison, {}, false, buildingSubject).ok());
    REQUIRE(queue(10, OrderKind::BoardTransport, {}, false, transportSubject).ok());
    const std::string text = recorded.exportText();
    CHECK(text.starts_with("EVERTS_COMMANDS 1\n"));
    eve::rts::RTSCommandLog replay;
    REQUIRE(replay.importText(text).ok());
    CHECK_EQ(replay.exportText(), text);
    auto first = replay.apply(eve::SimulationTick{2}, module);
    REQUIRE(first.ok());
    CHECK_EQ(first.value(), std::size_t{2});
    CHECK_EQ(unit->orders()->values.orderCount(), std::size_t{2});
    auto ground = replay.apply(eve::SimulationTick{3}, module);
    REQUIRE(ground.ok());
    CHECK_EQ(unit->orders()->values.orderCount(), std::size_t{3});
    for (std::uint64_t tick = 4; tick <= 10; ++tick) {
        auto applied = replay.apply(eve::SimulationTick{tick}, module);
        REQUIRE(applied.ok());
        CHECK_EQ(applied.value(), std::size_t{1});
        // Replaced orders remain as terminal history in the canonical queue.
        CHECK_EQ(unit->orders()->values.orderCount(), static_cast<std::size_t>(tick));
        eve::rts::OrderComponent copy   = unit->orders()->values;
        auto                     active = copy.current();
        REQUIRE(active.ok());
        auto completed = copy.complete(active.value().id);
        REQUIRE(completed.ok());
        CHECK(copy.empty());
    }
    auto current = unit->orders()->values.current();
    REQUIRE(current.ok());
    CHECK_EQ(static_cast<int>(current.value().kind), static_cast<int>(OrderKind::BoardTransport));
    CHECK(ecs::try_get(current.value().targetEntity) == module.findUnit(transportSubject));
}

TEST_CASE("rts.fireSupportFacadeRoundTripsAndReplaysRequestAndCancellationAtExactTicks") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    module;
    const auto       factionSubject   = subject("00000000-0000-7000-8000-00000000d201");
    const auto       requesterSubject = subject("00000000-0000-7000-8000-00000000d202");
    const auto       batterySubject   = subject("00000000-0000-7000-8000-00000000d203");
    auto             faction          = module.newFaction(factionSubject);
    REQUIRE(faction.ok());
    auto requester = module.newFactionUnit(*faction.value(), requesterSubject);
    auto battery   = module.newFactionUnit(*faction.value(), batterySubject);
    REQUIRE(requester.ok());
    REQUIRE(battery.ok());
    requester.value()->motion()->x       = 0.0f;
    battery.value()->motion()->x         = 2.0f;
    auto*                         weapon = eve::weapon::WeaponEntity::createWeapon();
    eve::weapon::WeaponDefinition indirect;
    indirect.id                 = "replay-howitzer";
    indirect.range              = 30.0f;
    indirect.projectile.speed   = 12.0f;
    indirect.projectile.gravity = 1.0f;
    weapon->definition()->owned = std::make_shared<const eve::weapon::WeaponDefinition>(indirect);
    weapon->definition()->def   = weapon->definition()->owned.get();
    auto weaponLink             = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
    REQUIRE(weaponLink.ok());
    battery.value()->weapon()->link = std::move(weaponLink).takeValue();

    eve::rts::RTSReplayCommand request;
    request.tick           = eve::SimulationTick{4};
    request.operation      = eve::rts::RTSReplayOperation::RequestFireSupport;
    request.producer       = requesterSubject;
    request.point          = {12.0f, 1.0f};
    request.command.radius = 2.5f;
    request.priority       = 4;
    request.limit          = 1;
    eve::rts::RTSReplayCommand cancel;
    cancel.tick      = eve::SimulationTick{5};
    cancel.operation = eve::rts::RTSReplayOperation::CancelFireSupport;
    cancel.producer  = requesterSubject;
    eve::rts::RTSCommandLog recorded;
    REQUIRE(recorded.queue(request).ok());
    REQUIRE(recorded.queue(cancel).ok());
    const std::string text = recorded.exportText();
    CHECK(text.starts_with("EVERTS_COMMANDS 3\n"));
    eve::rts::RTSCommandLog replay;
    REQUIRE(replay.importText(text).ok());
    CHECK_EQ(replay.exportText(), text);
    auto early = replay.apply(eve::SimulationTick{3}, module);
    REQUIRE(early.ok());
    CHECK_EQ(early.value(), 0u);
    auto assigned = replay.apply(eve::SimulationTick{4}, module);
    REQUIRE(assigned.ok());
    CHECK_EQ(assigned.value(), 1u);
    auto order = battery.value()->orders()->values.current();
    REQUIRE(order.ok());
    CHECK_EQ(static_cast<int>(order.value().kind), static_cast<int>(OrderKind::SuppressArea));
    CHECK_EQ(battery.value()->artillery()->suppressionShotsRemaining, 4);
    CHECK_EQ(ecs::try_get(battery.value()->artillery()->fireSupportRequester), requester.value());
    auto cancelled = replay.apply(eve::SimulationTick{5}, module);
    REQUIRE(cancelled.ok());
    CHECK_EQ(cancelled.value(), 1u);
    CHECK(ecs::try_get(battery.value()->artillery()->fireSupportRequester) == nullptr);
    weapon->release();
}

TEST_CASE("rts.commandLogReplaysConstructionProductionResearchAndAbilitiesThroughCanonicalFacades") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    REQUIRE(rts.configureScriptWorld(16, 16, 1.0f).ok());
    REQUIRE(rts.loadScriptContent(R"({
        "units":[
          {"id":"worker","role":"worker","health":60,"speed":3,"radius":0.3,"buildRate":1},
          {"id":"marine","producer":"barracks","costResource":"minerals","cost":50,
           "buildTime":1,"health":80,"speed":3,"radius":0.3}],
        "buildings":[
          {"id":"barracks","costResource":"minerals","cost":150,"buildTime":2,"health":500}],
        "upgrades":[
          {"id":"weapons_1","producer":"barracks","targetUnit":"marine",
           "costResource":"minerals","cost":100,"researchTime":2,"attackMultiplier":1.2}],
        "abilities":[
          {"id":"grenade","casterUnit":"marine","targetType":"point","range":6,"radius":1,
           "cooldown":2,"damage":10,"castTime":0.5,"resourceType":"minerals","resourceCost":10}]
    })")
                .ok());
    const auto factionSubject  = subject("00000000-0000-7000-8000-000000000291");
    const auto builderSubject  = subject("00000000-0000-7000-8000-000000000292");
    const auto casterSubject   = subject("00000000-0000-7000-8000-000000000293");
    const auto producerSubject = subject("00000000-0000-7000-8000-000000000294");
    const auto buildingSubject = subject("00000000-0000-7000-8000-000000000295");
    const auto producedSubject = subject("00000000-0000-7000-8000-000000000296");
    const auto workerId        = eve::LogicalId::parse("unit:worker");
    const auto marineId        = eve::LogicalId::parse("unit:marine");
    const auto barracksId      = eve::LogicalId::parse("building:barracks");
    REQUIRE(workerId.has_value());
    REQUIRE(marineId.has_value());
    REQUIRE(barracksId.has_value());
    auto* faction = rts.newFaction(factionSubject).value();
    REQUIRE(rts.newFactionUnit(*faction, builderSubject, *workerId).ok());
    auto* caster        = rts.newFactionUnit(*faction, casterSubject, *marineId).value();
    caster->motion()->x = 1.0f;
    caster->motion()->y = 1.0f;
    REQUIRE(rts.newFactionBuilding(*faction, producerSubject, *barracksId).ok());
    REQUIRE(rts.addScriptResource(*faction, "minerals", 500).ok());

    eve::rts::RTSCommandLog    recorded;
    eve::rts::RTSReplayCommand construction;
    construction.tick          = eve::SimulationTick{2};
    construction.operation     = eve::rts::RTSReplayOperation::Construction;
    construction.units         = {builderSubject};
    construction.faction       = factionSubject;
    construction.resultSubject = buildingSubject;
    construction.definition    = *barracksId;
    construction.point         = {4.0f, 5.0f};
    REQUIRE(recorded.queue(construction).ok());

    eve::rts::RTSReplayCommand production;
    production.tick          = eve::SimulationTick{2};
    production.operation     = eve::rts::RTSReplayOperation::Production;
    production.producer      = producerSubject;
    production.resultSubject = producedSubject;
    production.definition    = *marineId;
    production.priority      = 3;
    REQUIRE(recorded.queue(production).ok());

    eve::rts::RTSReplayCommand research;
    research.tick      = eve::SimulationTick{2};
    research.operation = eve::rts::RTSReplayOperation::Research;
    research.producer  = producerSubject;
    research.value     = "weapons_1";
    REQUIRE(recorded.queue(research).ok());

    eve::rts::RTSReplayCommand ability;
    ability.tick      = eve::SimulationTick{2};
    ability.operation = eve::rts::RTSReplayOperation::Ability;
    ability.units     = {casterSubject};
    ability.value     = "grenade";
    ability.point     = {2.0f, 1.0f};
    REQUIRE(recorded.queue(ability).ok());

    eve::rts::RTSReplayCommand cancelProduction;
    cancelProduction.tick      = eve::SimulationTick{3};
    cancelProduction.operation = eve::rts::RTSReplayOperation::CancelProduction;
    cancelProduction.producer  = producerSubject;
    cancelProduction.priority  = -1;
    REQUIRE(recorded.queue(cancelProduction).ok());

    eve::rts::RTSReplayCommand cancelAbility;
    cancelAbility.tick      = eve::SimulationTick{3};
    cancelAbility.operation = eve::rts::RTSReplayOperation::CancelAbility;
    cancelAbility.units     = {casterSubject};
    REQUIRE(recorded.queue(cancelAbility).ok());

    const std::string text = recorded.exportText();
    CHECK(text.starts_with("EVERTS_COMMANDS 2\n"));
    eve::rts::RTSCommandLog imported;
    REQUIRE(imported.importText(text).ok());
    CHECK_EQ(imported.exportText(), text);
    REQUIRE(imported.apply(eve::SimulationTick{1}, rts).ok());
    auto applied = imported.apply(eve::SimulationTick{2}, rts);
    REQUIRE(applied.ok());
    CHECK_EQ(applied.value(), std::size_t{4});
    CHECK(rts.findBuilding(buildingSubject) != nullptr);
    CHECK_EQ(rts.findBuilding(producerSubject)->production()->values.taskCount(), std::size_t{2});
    CHECK(caster->abilities()->channel.has_value());
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 190);
    auto cancelled = imported.apply(eve::SimulationTick{3}, rts);
    REQUIRE(cancelled.ok());
    CHECK_EQ(cancelled.value(), std::size_t{2});
    CHECK(!caster->abilities()->channel.has_value());
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 290);

    std::string replayCanonical;
    {
        ecs::Table       replayTable;
        ecs::ScopedTable replayGuard(replayTable);
        eve::rts::RTS    replayWorld;
        REQUIRE(replayWorld.configureScriptWorld(16, 16, 1.0f).ok());
        REQUIRE(replayWorld
                    .loadScriptContent(R"({
            "units":[
              {"id":"worker","role":"worker","health":60,"speed":3,"radius":0.3,"buildRate":1},
              {"id":"marine","producer":"barracks","costResource":"minerals","cost":50,
               "buildTime":1,"health":80,"speed":3,"radius":0.3}],
            "buildings":[
              {"id":"barracks","costResource":"minerals","cost":150,"buildTime":2,"health":500}],
            "upgrades":[
              {"id":"weapons_1","producer":"barracks","targetUnit":"marine",
               "costResource":"minerals","cost":100,"researchTime":2,"attackMultiplier":1.2}],
            "abilities":[
              {"id":"grenade","casterUnit":"marine","targetType":"point","range":6,"radius":1,
               "cooldown":2,"damage":10,"castTime":0.5,"resourceType":"minerals","resourceCost":10}]
        })")
                    .ok());
        auto* replayFaction = replayWorld.newFaction(factionSubject).value();
        REQUIRE(replayWorld.newFactionUnit(*replayFaction, builderSubject, *workerId).ok());
        auto* replayCaster        = replayWorld.newFactionUnit(*replayFaction, casterSubject, *marineId).value();
        replayCaster->motion()->x = 1.0f;
        replayCaster->motion()->y = 1.0f;
        REQUIRE(replayWorld.newFactionBuilding(*replayFaction, producerSubject, *barracksId).ok());
        REQUIRE(replayWorld.addScriptResource(*replayFaction, "minerals", 500).ok());
        eve::rts::RTSCommandLog replayLog;
        REQUIRE(replayLog.importText(text).ok());
        REQUIRE(replayLog.apply(eve::SimulationTick{2}, replayWorld).ok());
        REQUIRE(replayLog.apply(eve::SimulationTick{3}, replayWorld).ok());
        CHECK_EQ(replayWorld.scriptResource(*replayFaction, "minerals").value(), 290);
        auto canonical = replayWorld.canonicalStateJson();
        REQUIRE(canonical.ok());
        replayCanonical = canonical.value();
    }
    auto sourceCanonical = rts.canonicalStateJson();
    REQUIRE(sourceCanonical.ok());
    CHECK_EQ(replayCanonical, sourceCanonical.value());
}

TEST_CASE("rts.lockstepAppliesCommandsBeforeExactFixedSimulationTicks") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    const auto       unitSubject = subject("00000000-0000-7000-8000-000000000281");
    eve::rts::RTS    simulation;
    REQUIRE(simulation.newUnit(unitSubject).ok());
    eve::rts::RTSReplayCommand command;
    command.tick              = eve::SimulationTick{2};
    command.units             = {unitSubject};
    command.command.kind      = OrderKind::Move;
    command.command.target    = {6.0f, 3.0f};
    command.formation.spacing = 1.0f;
    eve::rts::RTSLockstep lockstep;
    auto                  tenth = eve::Duration::fromSeconds(0.1);
    REQUIRE(tenth.ok());
    REQUIRE(lockstep.setFixedStep(tenth.value()).ok());
    REQUIRE(lockstep.queue(command).ok());
    PendingRTSExecutor executor;
    auto               first = lockstep.step(simulation, executor);
    REQUIRE(first.ok());
    CHECK_EQ(lockstep.currentTick().value(), 1u);
    CHECK(!simulation.findUnit(unitSubject)->orders()->values.current().ok());
    auto second = lockstep.step(simulation, executor);
    REQUIRE(second.ok());
    CHECK_EQ(lockstep.currentTick().value(), 2u);
    auto current = simulation.findUnit(unitSubject)->orders()->values.current();
    REQUIRE(current.ok());
    CHECK_EQ(static_cast<int>(current.value().kind), static_cast<int>(OrderKind::Move));
    CHECK(std::abs(current.value().target.x - 6.0f) < 1e-5f);
    CHECK(std::abs(current.value().target.y - 3.0f) < 1e-5f);
    CHECK(!lockstep.setFixedStep(eve::Duration::zero()).ok());
    lockstep.reset(eve::SimulationTick{9});
    CHECK_EQ(lockstep.currentTick().value(), 9u);
    CHECK_EQ(lockstep.commands().size(), 0u);
}

TEST_CASE("rts.canonicalStateAndInjectedHashIgnoreCreationOrderAndDetectMutation") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    const auto       first  = subject("00000000-0000-7000-8000-000000000291");
    const auto       second = subject("00000000-0000-7000-8000-000000000292");
    const auto       base   = subject("00000000-0000-7000-8000-000000000293");
    eve::rts::RTS    left;
    eve::rts::RTS    right;
    REQUIRE(left.newUnit(first).ok());
    REQUIRE(left.newUnit(second).ok());
    REQUIRE(left.newBuilding(base).ok());
    REQUIRE(right.newBuilding(base).ok());
    REQUIRE(right.newUnit(second).ok());
    REQUIRE(right.newUnit(first).ok());
    left.findBuilding(base)->infrastructure()->powerProduced  = 5.0f;
    right.findBuilding(base)->infrastructure()->powerProduced = 5.0f;
    left.findUnit(first)->motion()->x                         = 3.0f;
    right.findUnit(first)->motion()->x                        = 3.0f;
    REQUIRE(left.findUnit(first)->tags()->values.add("unit.scout").ok());
    REQUIRE(right.findUnit(first)->tags()->values.add("unit.scout").ok());
    auto leftJson  = left.canonicalStateJson();
    auto rightJson = right.canonicalStateJson();
    REQUIRE(leftJson.ok());
    REQUIRE(rightJson.ok());
    CHECK_EQ(leftJson.value(), rightJson.value());
    CHECK(eve::Value::fromJson(leftJson.value()).ok());

    std::string hashedInput;
    auto        digest = eve::ContentId::parse("00000000-0000-7000-8000-000000000299");
    REQUIRE(digest.has_value());
    eve::SnapshotHashProvider provider = [&](std::string_view input) {
        hashedInput = input;
        return eve::Result<eve::ContentId>::success(*digest);
    };
    auto hash = left.stateHash(provider);
    REQUIRE(hash.ok());
    CHECK_EQ(hash.value(), *digest);
    CHECK_EQ(hashedInput, leftJson.value());
    right.findUnit(first)->worker()->cargo                    = 1.0f;
    right.findBuilding(base)->infrastructure()->powerProduced = 6.0f;
    auto changed                                              = right.canonicalStateJson();
    REQUIRE(changed.ok());
    CHECK_NE(changed.value(), leftJson.value());
    CHECK(!left.stateHash({}).ok());
}

TEST_CASE("rts.facadeMigratesCombatStanceAndMovementPriorityIntoSnapshots") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    const auto       firstSubject  = subject("00000000-0000-7000-8000-0000000002c1");
    const auto       secondSubject = subject("00000000-0000-7000-8000-0000000002c2");
    auto             firstCreated  = rts.newUnit(firstSubject);
    auto             secondCreated = rts.newUnit(secondSubject);
    REQUIRE(firstCreated.ok());
    REQUIRE(secondCreated.ok());
    auto* first         = firstCreated.value();
    auto* second        = secondCreated.value();
    first->motion()->x  = 3.0f;
    first->motion()->y  = 4.0f;
    second->motion()->x = 7.0f;
    second->motion()->y = 8.0f;
    const std::vector<eve::SubjectRef> selection{firstSubject, secondSubject};

    REQUIRE(rts.setUnitStance(selection, eve::rts::CombatStance::Passive, 6.0f).ok());
    REQUIRE(rts.setUnitMovementPriority(selection, 250).ok());
    CHECK_EQ(static_cast<int>(first->combat()->stance), static_cast<int>(eve::rts::CombatStance::Passive));
    CHECK_EQ(first->combat()->leashRange, 6.0f);
    CHECK_EQ(first->combat()->guardX, 3.0f);
    CHECK_EQ(first->combat()->guardY, 4.0f);
    CHECK_EQ(second->navigation()->movementPriority, 100);

    const std::vector<eve::SubjectRef> invalid{firstSubject, subject("00000000-0000-7000-8000-0000000002cf")};
    auto                               rejected = rts.setUnitMovementPriority(invalid, -50);
    CHECK(!rejected.ok());
    CHECK_EQ(first->navigation()->movementPriority, 100);

    auto snapshot = rts.snapshotState();
    REQUIRE(snapshot.ok());
    CHECK_EQ(static_cast<int>(snapshot.value().units.front().combat.stance),
             static_cast<int>(eve::rts::CombatStance::Passive));
    auto canonical = rts.canonicalStateJson();
    REQUIRE(canonical.ok());
    CHECK(canonical.value().find("\"stance\":\"passive\"") != std::string::npos);
}
}  // namespace test_rts_restore
