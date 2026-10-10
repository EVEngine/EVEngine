#include <limits>
#include <numbers>
#include <simplesquirrel/simplesquirrel.hpp>
#include "RtsCompositionFixtures.h"
#include "rts/RTSReplay.h"

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
