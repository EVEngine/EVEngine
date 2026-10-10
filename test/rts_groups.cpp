#include <simplesquirrel/simplesquirrel.hpp>
#include "RtsCompositionFixtures.h"
#include "rts/RTSSystemMovementInternal.h"

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
        REQUIRE(rts.step({eve::SimulationTick{101}, eve::Duration::zero()}, executor).ok());
        const auto before = *first->navigation()->formationTarget;
        REQUIRE(rts.restoreState(saved).ok());
        CHECK(!first->navigation()->formationTarget.has_value());
        REQUIRE(rts.step({eve::SimulationTick{101}, eve::Duration::zero()}, executor).ok());
        REQUIRE(first->navigation()->formationTarget.has_value());
        CHECK(std::isfinite(first->navigation()->formationTarget->x));
        CHECK(std::abs(first->navigation()->formationTarget->x - before.x) < 1e-5f);
        CHECK(std::abs(first->navigation()->formationTarget->y - before.y) < 1e-5f);
        eve::map::Pathfinder pathfinder(128, 64);
        pathfinder.setBlocked(50, 19, true);
        pathfinder.setBlocked(50, 21, true);
        rts.setNavigationProvider(&pathfinder);
        REQUIRE(rts.step({eve::SimulationTick{102}, eve::Duration::zero()}, executor).ok());
        CHECK(!first->navigation()->formationTarget.has_value());
        CHECK(!second->navigation()->formationTarget.has_value());
        CHECK(!first->navigation()->waypoints.empty());
        pathfinder.setBlocked(50, 19, false);
        pathfinder.setBlocked(50, 21, false);
        REQUIRE(rts.step({eve::SimulationTick{103}, eve::Duration::zero()}, executor).ok());
        CHECK(first->navigation()->formationTarget.has_value());
        CHECK(second->navigation()->formationTarget.has_value());
        rts.setNavigationProvider(nullptr);
        CommandSpec replacement;
        replacement.target = {10.f, 40.f};
        REQUIRE(first->orders()->values.replace(replacement).ok());
        REQUIRE(rts.step({eve::SimulationTick{104}, eve::Duration::zero()}, executor).ok());
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
