#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rts_script)
#include "PrivateTestScope.h"
#include <array>
#include "PathBesideSource.h"
#include "RtsCompositionFixtures.h"
#include "rts/RTS.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// rts_script.cpp
namespace test_rts_script {
// rts_script.cpp
TEST_CASE("rts.sandboxScriptCompilesThroughEveScriptFrontend") {
    const std::filesystem::path sourceRoot =
        std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path().parent_path();
    const std::filesystem::path scriptPath = sourceRoot / "examples" / "rts-sandbox" / "main.nut";
    std::ifstream               input(scriptPath, std::ios::binary);
    REQUIRE(input.is_open());
    std::ostringstream source;
    source << input.rdbuf();

    // The full engine module surface plus the comprehensive RTS sandbox needs
    // the same enlarged VM stack used by the game runner.
    eve::Runtime runtime(8192, ssq::Libs::ALL);
    bool         compiled = true;
    try {
        runtime.compileSource(source.str(), "examples/rts-sandbox/main.nut");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        compiled = false;
    } catch (...) {
        compiled = false;
    }
    REQUIRE(compiled);
}

TEST_CASE("rts.scriptFogQueriesResolveCreatedFactionIdentity") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);

    vm.run(vm.compileSource(R"(
        result <- "fail";
        local sim = eve.RTS();
        local configured = sim.configureScriptWorld(4, 4, 1.0, 0.0, 0.0);
        local created = sim.newFaction("00000000-0000-7000-8000-00000000fc01");
        if (configured.ok && created.ok) {
            local explored = sim.scriptCellExplored(created.value, 0, 0);
            local visible = sim.scriptCellVisible(created.value, 0, 0);
            if (explored.ok && visible.ok) result = "ok";
        }
    )"));

    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("rts.scriptFacadeKeepsFactionIdentityAcrossFrameCallbacks") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);

    vm.run(vm.compileSource(R"(
        persist sim = null;
        persist faction = null;
        result <- "fail";
        function init() {
            sim = eve.RTS();
            local configured = sim.configureScriptWorld(4, 4, 1.0, 0.0, 0.0);
            local created = sim.newFaction("00000000-0000-7000-8000-00000000fc02");
            if (configured.ok && created.ok) faction = created.value;
        }
        function update() {
            local stepped = sim.stepScript(1.0 / 30.0);
            if (!stepped.ok) result = stepped.status.summary;
        }
        function render() {
            local explored = sim.scriptCellExplored(faction, 0, 0);
            result = explored.ok ? "ok" : explored.status.summary;
        }
    )"));

    vm.callFunc(vm.findFunc("init"), vm);
    vm.callFunc(vm.findFunc("update"), vm);
    vm.callFunc(vm.findFunc("render"), vm);
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("rts.sandboxInitializesStepsAndRebuildsAcrossReload") {
    const std::filesystem::path sourceRoot =
        std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path().parent_path();
    auto readAll = [](const std::filesystem::path& path) {
        std::ifstream      input(path, std::ios::binary);
        std::ostringstream content;
        content << input.rdbuf();
        return content.str();
    };
    const auto        sandboxRoot = sourceRoot / "examples" / "rts-sandbox";
    const std::string source      = readAll(sandboxRoot / "main.nut");
    const std::string content     = readAll(sandboxRoot / "data" / "content.json");

    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(8192, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource("eve_init <- null; eve_update <- null; eve_render <- null;"));
    vm.run(vm.compileSource(source.c_str()));
    vm.set("sandboxContent", content);
    vm.run(vm.compileSource(R"(
        readTextFile = function(path) { return sandboxContent; };
        result <- "fail";
        stepSandbox <- function() {
            local stepped = sim.stepScript(1.0 / 30.0);
            if (!stepped.ok) throw stepped.status.summary;
        };
    )"));
    vm.callFunc(vm.findFunc("resetGame"), vm);
    const auto stepSandbox = vm.findFunc("stepSandbox");
    for (int frame = 0; frame < 180; ++frame) vm.callFunc(stepSandbox, vm);
    vm.run(vm.compileSource(R"(
        local explored = sim.scriptCellExplored(RTS_SANDBOX_BLUE_FACTION_ID, 0, 0);
        result = explored.ok ? "ok" : explored.status.summary;
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));

    // The game runner can reload main.nut while persisted script state survives.
    // The native RTS facade must not survive that boundary because its ECS roots
    // belong to the discarded script generation.
    vm.run(vm.compileSource(source.c_str()));
    vm.run(vm.compileSource(R"(
        if (sim != null) throw "RTS facade survived script reload";
    )"));
}

TEST_CASE("rts.legacySandboxContentMaterializesEveryArchetype") {
    const std::filesystem::path sourceRoot =
        std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path().parent_path();
    std::ifstream input(sourceRoot / "examples" / "rts-sandbox" / "data" / "content.json", std::ios::binary);
    REQUIRE(input.is_open());
    std::ostringstream json;
    json << input.rdbuf();

    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    REQUIRE(rts.configureScriptWorld(36, 22, 1.0f).ok());
    const auto loaded = rts.loadScriptContent(json.str());
    REQUIRE(loaded.ok());
    CHECK_EQ(loaded.value().inserted, std::size_t{18});

    auto*                            faction = rts.newFaction(subject("00000000-0000-7000-8000-00000000fa01")).value();
    const std::array<const char*, 7> unitTypes{"worker", "engineer", "marine", "apc", "jammer", "skyguard", "drone"};
    for (std::size_t index = 0; index < unitTypes.size(); ++index) {
        const auto id = eve::LogicalId::parse(std::string("unit:") + unitTypes[index]);
        REQUIRE(id.has_value());
        const std::string suffix     = std::to_string(100 + index);
        const auto        persistent = eve::PersistentId::parse("00000000-0000-7000-8000-000000000" + suffix);
        REQUIRE(persistent.has_value());
        auto created = rts.newFactionUnit(*faction, eve::SubjectRef::fromPersistentId(*persistent), *id);
        REQUIRE(created.ok());
        Unit* unit = created.value();
        CHECK(unit->durability()->state.maxHealth > 0.0);
        if (std::string_view(unitTypes[index]) == "marine") {
            CHECK_EQ(unit->morale()->capacity, 100.0f);
            CHECK(unit->morale()->retreatEnabled);
            CHECK_EQ(unit->veterancy()->eliteThreshold, 250.0f);
            CHECK(unit->command()->requiresCommand);
            CHECK_EQ(unit->supply()->capacity, 36.0f);
            CHECK(unit->weapon()->link.resolve() != nullptr);
        } else if (std::string_view(unitTypes[index]) == "apc") {
            CHECK_EQ(unit->containment()->capacity, std::size_t{4});
            CHECK_EQ(unit->supply()->capacity, 30.0f);
            CHECK(unit->supply()->autoDispatch);
            CHECK_EQ(unit->command()->capacity, 6);
            CHECK_EQ(unit->morale()->auraRange, 5.0f);
        } else if (std::string_view(unitTypes[index]) == "drone") {
            CHECK(unit->motion()->airborne);
            CHECK_EQ(unit->shield()->capacity, 30.0f);
        }
    }

    const std::array<const char*, 5> buildingTypes{"command_center", "barracks", "supply_depot", "turret",
                                                   "oil_derrick"};
    for (std::size_t index = 0; index < buildingTypes.size(); ++index) {
        const auto id = eve::LogicalId::parse(std::string("building:") + buildingTypes[index]);
        REQUIRE(id.has_value());
        const std::string suffix     = std::to_string(200 + index);
        const auto        persistent = eve::PersistentId::parse("00000000-0000-7000-8000-000000000" + suffix);
        REQUIRE(persistent.has_value());
        auto created = rts.newFactionBuilding(*faction, eve::SubjectRef::fromPersistentId(*persistent), *id);
        REQUIRE(created.ok());
        Building* building = created.value();
        CHECK(building->integrity()->state.maxHealth > 0.0);
        if (std::string_view(buildingTypes[index]) == "command_center") {
            CHECK_EQ(building->shield()->capacity, 100.0f);
            CHECK_EQ(building->infrastructure()->powerProduced, 20.0f);
            CHECK_EQ(building->command()->capacity, 12);
            CHECK(!building->dropoff()->acceptedResources.empty());
        } else if (std::string_view(buildingTypes[index]) == "supply_depot") {
            CHECK_EQ(building->supply()->capacity, 100.0f);
            CHECK_EQ(building->supply()->productionRate, 2.0f);
        } else if (std::string_view(buildingTypes[index]) == "turret") {
            CHECK_EQ(building->garrison()->capacity, std::size_t{3});
            CHECK_EQ(building->garrison()->damageBonusPerOccupant, 0.2f);
            CHECK(building->weapon()->link.resolve() != nullptr);
            CHECK_EQ(building->combat()->turnRateDegrees, 120.0f);
        }
    }
}

TEST_CASE("rts.scriptProductionUsesCanonicalEconomyActionAndProductionQueues") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    REQUIRE(rts.configureScriptWorld(12, 12, 1.0f).ok());
    REQUIRE(rts.loadScriptContent(R"({
        "units":[{"id":"marine","producer":"barracks","costResource":"minerals",
                  "cost":50,"buildTime":1.5,"health":80,"speed":3,"radius":0.3}],
        "buildings":[{"id":"barracks","health":500,"buildTime":2}]
    })")
                .ok());
    auto* faction = rts.newFaction(subject("00000000-0000-7000-8000-00000000fb01")).value();
    REQUIRE(rts.addScriptResource(*faction, "minerals", 100).ok());
    const auto barracksId = eve::LogicalId::parse("building:barracks");
    const auto marineId   = eve::LogicalId::parse("unit:marine");
    REQUIRE(barracksId.has_value());
    REQUIRE(marineId.has_value());
    auto* barracks =
        rts.newFactionBuilding(*faction, subject("00000000-0000-7000-8000-00000000fb02"), *barracksId).value();
    const auto marineSubject = subject("00000000-0000-7000-8000-00000000fb03");
    auto       queued        = rts.queueScriptUnit(*barracks, marineSubject, *marineId);
    REQUIRE(queued.ok());
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 50);
    CHECK(rts.findUnit(marineSubject) == nullptr);
    for (int index = 0; index < 4; ++index) {
        auto stepped = rts.stepScript(0.5);
        REQUIRE(stepped.ok());
    }
    Unit* marine = rts.findUnit(marineSubject);
    REQUIRE(marine != nullptr);
    CHECK_EQ(marine->definition()->id, *marineId);
    CHECK_EQ(marine->faction()->link.resolve(), faction);
    CHECK_EQ(marine->motion()->x, barracks->placement()->worldX);
}

TEST_CASE("rts.scriptConstructionAtomicallyPaysBuildsAndCompletesCanonicalOrder") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    REQUIRE(rts.configureScriptWorld(12, 12, 1.0f).ok());
    REQUIRE(rts.loadScriptContent(R"({
        "units":[{"id":"worker","role":"worker","health":60,"speed":3,"radius":0.3,
                  "buildRate":1}],
        "buildings":[{"id":"barracks","costResource":"minerals","cost":150,
                       "buildTime":2,"health":500}]
    })")
                .ok());
    auto* faction    = rts.newFaction(subject("00000000-0000-7000-8000-00000000fc01")).value();
    auto  workerId   = eve::LogicalId::parse("unit:worker");
    auto  barracksId = eve::LogicalId::parse("building:barracks");
    REQUIRE(workerId.has_value());
    REQUIRE(barracksId.has_value());
    Unit* builder = rts.newFactionUnit(*faction, subject("00000000-0000-7000-8000-00000000fc02"), *workerId).value();
    REQUIRE(rts.addScriptResource(*faction, "minerals", 200).ok());
    const auto buildingSubject = subject("00000000-0000-7000-8000-00000000fc03");
    auto       started = rts.startScriptConstruction(*faction, buildingSubject, *barracksId, {4.0f, 5.0f}, *builder);
    REQUIRE(started.ok());
    Building* barracks = started.value();
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 50);
    CHECK_EQ(barracks->construction()->progress, 0.0f);
    auto order = builder->orders()->values.current();
    REQUIRE(order.ok());
    CHECK_EQ(static_cast<int>(order.value().kind), static_cast<int>(OrderKind::Move));
    for (int index = 0; index < 8 && barracks->construction()->progress < 1.0f; ++index)
        REQUIRE(rts.stepScript(1.0).ok());
    CHECK_EQ(barracks->construction()->progress, 1.0f);
    CHECK(builder->orders()->values.empty());

    const std::size_t before = rts.buildingCount();
    auto rejected = rts.startScriptConstruction(*faction, subject("00000000-0000-7000-8000-00000000fc04"), *barracksId,
                                                {6.0f, 5.0f}, *builder);
    CHECK(!rejected.ok());
    CHECK_EQ(rts.buildingCount(), before);
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 50);
}

TEST_CASE("rts.scriptConstructionCancellationRefundsProgressAndRestoresThroughCheckpoint") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    REQUIRE(rts.configureScriptWorld(12, 12, 1.0f).ok());
    REQUIRE(rts.loadScriptContent(R"({
        "units":[{"id":"worker","role":"worker","health":60,"speed":3,"radius":0.3,
                  "buildRate":1}],
        "buildings":[{"id":"depot","costResource":"minerals","cost":100,
                       "buildTime":4,"health":400}]
    })")
                .ok());
    const auto factionSubject  = subject("00000000-0000-7000-8000-00000000fc11");
    const auto builderSubject  = subject("00000000-0000-7000-8000-00000000fc12");
    const auto buildingSubject = subject("00000000-0000-7000-8000-00000000fc13");
    auto*      faction         = rts.newFaction(factionSubject).value();
    const auto workerId        = eve::LogicalId::parse("unit:worker");
    const auto depotId         = eve::LogicalId::parse("building:depot");
    REQUIRE(workerId.has_value());
    REQUIRE(depotId.has_value());
    auto* builder = rts.newFactionUnit(*faction, builderSubject, *workerId).value();
    REQUIRE(rts.addScriptResource(*faction, "minerals", 200).ok());
    auto* building = rts.startScriptConstruction(*faction, buildingSubject, *depotId, {5.0f, 5.0f}, *builder).value();
    building->construction()->progress = 0.5f;
    REQUIRE(rts.captureScriptCheckpoint("half-built").ok());
    auto cancelled = rts.cancelScriptConstruction(*building);
    REQUIRE(cancelled.ok());
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 175);
    CHECK(rts.findBuilding(buildingSubject) == nullptr);
    CHECK(builder->orders()->values.empty());

    REQUIRE(rts.restoreScriptCheckpoint("half-built").ok());
    faction  = rts.findFaction(factionSubject);
    builder  = rts.findUnit(builderSubject);
    building = rts.findBuilding(buildingSubject);
    REQUIRE(faction != nullptr);
    REQUIRE(builder != nullptr);
    REQUIRE(building != nullptr);
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 100);
    REQUIRE(rts.cancelScriptConstruction(*building).ok());
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 175);
    CHECK(rts.findBuilding(buildingSubject) == nullptr);
}

TEST_CASE("rts.scriptBuildingSaleRefundsQueuesEvacuatesGarrisonAndRestoresThroughCheckpoint") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    REQUIRE(rts.configureScriptWorld(12, 12, 1.0f).ok());
    REQUIRE(rts.loadScriptContent(R"({
        "units":[{"id":"marine","producer":"barracks","costResource":"minerals",
                  "cost":50,"buildTime":2,"health":80,"speed":3,"radius":0.3}],
        "buildings":[{"id":"barracks","costResource":"minerals","cost":100,
                       "sellRefundRatio":0.5,"health":500,"buildTime":2}],
        "upgrades":[{"id":"weapons","producer":"barracks","targetUnit":"marine",
                     "costResource":"minerals","cost":100,"researchTime":3,
                     "attackMultiplier":1.2}]
    })")
                .ok());
    const auto factionSubject  = subject("00000000-0000-7000-8000-00000000fc21");
    const auto buildingSubject = subject("00000000-0000-7000-8000-00000000fc22");
    const auto occupantSubject = subject("00000000-0000-7000-8000-00000000fc23");
    const auto queuedSubject   = subject("00000000-0000-7000-8000-00000000fc24");
    const auto barracksId      = eve::LogicalId::parse("building:barracks");
    const auto marineId        = eve::LogicalId::parse("unit:marine");
    REQUIRE(barracksId.has_value());
    REQUIRE(marineId.has_value());
    auto* faction                  = rts.newFaction(factionSubject).value();
    auto* barracks                 = rts.newFactionBuilding(*faction, buildingSubject, *barracksId).value();
    auto* occupant                 = rts.newFactionUnit(*faction, occupantSubject, *marineId).value();
    barracks->garrison()->capacity = 1;
    auto container                 = eve::rts::ContainerLink::bind(ecs::handle_of(barracks));
    REQUIRE(container.ok());
    occupant->containment()->container = std::move(container).takeValue();
    barracks->garrison()->occupants.push_back(ecs::handle_of(occupant));
    REQUIRE(rts.addScriptResource(*faction, "minerals", 500).ok());
    REQUIRE(rts.queueScriptUnit(*barracks, queuedSubject, *marineId).ok());
    REQUIRE(rts.queueScriptResearch(*barracks, "weapons").ok());
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 350);
    REQUIRE(rts.captureScriptCheckpoint("before-sale").ok());

    REQUIRE(rts.sellScriptBuilding(*barracks).ok());
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 550);
    CHECK(rts.findBuilding(buildingSubject) == nullptr);
    CHECK(rts.findUnit(queuedSubject) == nullptr);
    CHECK(!occupant->containment()->container.isBound());

    REQUIRE(rts.restoreScriptCheckpoint("before-sale").ok());
    faction  = rts.findFaction(factionSubject);
    barracks = rts.findBuilding(buildingSubject);
    occupant = rts.findUnit(occupantSubject);
    REQUIRE(faction != nullptr);
    REQUIRE(barracks != nullptr);
    REQUIRE(occupant != nullptr);
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 350);
    REQUIRE(rts.sellScriptBuilding(*barracks).ok());
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 550);
    CHECK(!occupant->containment()->container.isBound());
}

TEST_CASE("rts.scriptReinforcementFacadeAndReplayResolveFallbackThroughPaidProduction") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    REQUIRE(rts.configureScriptWorld(12, 8, 1.0f).ok());
    REQUIRE(rts.loadScriptContent(R"({
        "units":[
          {"id":"tank","producer":"factory","costResource":"minerals","cost":100,
           "buildTime":2,"health":200,"speed":2,"radius":0.6},
          {"id":"marine","producer":"barracks","costResource":"minerals","cost":50,
           "buildTime":1,"health":80,"speed":3,"radius":0.3}],
        "buildings":[{"id":"barracks","health":500,"buildTime":2},
                     {"id":"factory","health":700,"buildTime":3}]
    })")
                .ok());
    auto*      faction    = rts.newFaction(subject("00000000-0000-7000-8000-00000000fc31")).value();
    const auto barracksId = eve::LogicalId::parse("building:barracks");
    const auto tankId     = eve::LogicalId::parse("unit:tank");
    REQUIRE(barracksId.has_value());
    REQUIRE(tankId.has_value());
    auto* barracks =
        rts.newFactionBuilding(*faction, subject("00000000-0000-7000-8000-00000000fc32"), *barracksId).value();
    CommandSpec rally;
    rally.kind   = OrderKind::AttackMove;
    rally.target = {8.0f, 4.0f};
    REQUIRE(rts.setBuildingRally(*barracks, rally, true).ok());
    REQUIRE(rts.setReinforcementFallback(*barracks, "tank", "marine").ok());
    REQUIRE(rts.addScriptResource(*faction, "minerals", 150).ok());

    const auto directSubject = subject("00000000-0000-7000-8000-00000000fc33");
    auto       direct        = rts.queueScriptReinforcement(*barracks, directSubject, *tankId);
    REQUIRE(direct.ok());
    CHECK_EQ(direct.value().requestedProduct, "tank");
    CHECK_EQ(direct.value().queuedProduct, "marine");
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 100);

    const auto                 replaySubject = subject("00000000-0000-7000-8000-00000000fc34");
    eve::rts::RTSReplayCommand replay;
    replay.tick          = eve::SimulationTick{1};
    replay.operation     = eve::rts::RTSReplayOperation::ReinforcementProduction;
    replay.producer      = barracks->identity()->subject;
    replay.resultSubject = replaySubject;
    replay.definition    = *tankId;
    REQUIRE(rts.queueScriptCommand(replay).ok());
    auto exported = rts.exportScriptCommandLog();
    REQUIRE(exported.ok());
    REQUIRE(rts.importScriptCommandLog(exported.value(), true).ok());
    REQUIRE(rts.stepScript(1.0).ok());
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 50);
    REQUIRE(rts.stepScript(1.0).ok());
    CHECK(rts.findUnit(directSubject) != nullptr);
    CHECK(rts.findUnit(replaySubject) != nullptr);
}

TEST_CASE("rts.scriptAIReplenishesWorkersBuildsArmyAndRestoresDeterministicIdentitySequence") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    REQUIRE(rts.configureScriptWorld(16, 8, 1.0f).ok());
    REQUIRE(rts.loadScriptContent(R"({
        "units":[
          {"id":"worker","role":"worker","producer":"barracks","costResource":"minerals",
           "cost":25,"buildTime":0.5,"health":60,"speed":3,"radius":0.3},
          {"id":"marine","producer":"barracks","costResource":"minerals","cost":50,
           "buildTime":0.5,"health":80,"speed":3,"radius":0.3}],
        "buildings":[
          {"id":"barracks","health":500,"buildTime":2},
          {"id":"headquarters","health":900,"buildTime":3}]
    })")
                .ok());
    const auto aiSubject      = subject("00000000-0000-7000-8000-00000000fc41");
    auto*      ai             = rts.newFaction(aiSubject).value();
    auto*      enemy          = rts.newFaction(subject("00000000-0000-7000-8000-00000000fc42")).value();
    const auto workerId       = eve::LogicalId::parse("unit:worker");
    const auto marineId       = eve::LogicalId::parse("unit:marine");
    const auto barracksId     = eve::LogicalId::parse("building:barracks");
    const auto headquartersId = eve::LogicalId::parse("building:headquarters");
    REQUIRE(workerId.has_value());
    REQUIRE(marineId.has_value());
    REQUIRE(barracksId.has_value());
    REQUIRE(headquartersId.has_value());
    auto* barracks = rts.newFactionBuilding(*ai, subject("00000000-0000-7000-8000-00000000fc43"), *barracksId).value();
    barracks->placement()->worldX = 2.0f;
    barracks->placement()->worldY = 3.0f;
    auto* headquarters =
        rts.newFactionBuilding(*enemy, subject("00000000-0000-7000-8000-00000000fc44"), *headquartersId).value();
    headquarters->placement()->worldX = 12.0f;
    headquarters->placement()->worldY = 3.0f;
    REQUIRE(rts.addScriptResource(*ai, "minerals", 300).ok());
    REQUIRE(rts.configureScriptAI(*ai, *workerId, *marineId, *headquartersId, 1, 2, 0.5f, 1.25f, true).ok());
    REQUIRE(rts.captureScriptCheckpoint("before-ai").ok());

    for (int tick = 0; tick < 5; ++tick) {
        auto stepped = rts.stepScript(0.5);
        REQUIRE(stepped.ok());
    }
    CHECK_EQ(rts.unitCount(), std::size_t{5});
    CHECK_EQ(rts.scriptResource(*ai, "minerals").value(), 75);
    auto firstState = rts.canonicalStateJson();
    REQUIRE(firstState.ok());

    REQUIRE(rts.restoreScriptCheckpoint("before-ai").ok());
    ai = rts.findFaction(aiSubject);
    REQUIRE(ai != nullptr);
    for (int tick = 0; tick < 5; ++tick) {
        auto stepped = rts.stepScript(0.5);
        REQUIRE(stepped.ok());
    }
    CHECK_EQ(rts.unitCount(), std::size_t{5});
    CHECK_EQ(rts.scriptResource(*ai, "minerals").value(), 75);
    auto secondState = rts.canonicalStateJson();
    REQUIRE(secondState.ok());
    CHECK_EQ(secondState.value(), firstState.value());
}

TEST_CASE("rts.scriptResearchPaysOnceAndUpgradesExistingAndFutureUnits") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    REQUIRE(rts.configureScriptWorld(12, 12, 1.0f).ok());
    REQUIRE(rts.loadScriptContent(R"({
        "units":[{"id":"marine","producer":"barracks","costResource":"minerals","cost":50,
                  "buildTime":1,"health":80,"speed":3,"radius":0.3}],
        "buildings":[{"id":"barracks","health":500,"buildTime":2}],
        "upgrades":[{"id":"infantry_weapons_1","producer":"barracks","targetUnit":"marine",
                     "costResource":"minerals","cost":100,"researchTime":2,"attackMultiplier":1.25}]
    })")
                .ok());
    auto* faction    = rts.newFaction(subject("00000000-0000-7000-8000-00000000fd01")).value();
    auto  marineId   = eve::LogicalId::parse("unit:marine");
    auto  barracksId = eve::LogicalId::parse("building:barracks");
    REQUIRE(marineId.has_value());
    REQUIRE(barracksId.has_value());
    Unit*     marine = rts.newFactionUnit(*faction, subject("00000000-0000-7000-8000-00000000fd02"), *marineId).value();
    Building* barracks =
        rts.newFactionBuilding(*faction, subject("00000000-0000-7000-8000-00000000fd03"), *barracksId).value();
    REQUIRE(rts.addScriptResource(*faction, "minerals", 150).ok());
    auto queued = rts.queueScriptResearch(*barracks, "infantry_weapons_1");
    REQUIRE(queued.ok());
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 50);
    CHECK(!rts.queueScriptResearch(*barracks, "infantry_weapons_1").ok());
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 50);
    REQUIRE(rts.stepScript(1.0).ok());
    REQUIRE(rts.stepScript(1.0).ok());
    CHECK_EQ(marine->combat()->upgradeDamageFactor, 1.25f);
    CHECK(std::binary_search(faction->technology()->unlocked.begin(), faction->technology()->unlocked.end(),
                             "infantry_weapons_1"));
    CHECK(!rts.queueScriptResearch(*barracks, "infantry_weapons_1").ok());

    Unit* reinforcement =
        rts.newFactionUnit(*faction, subject("00000000-0000-7000-8000-00000000fd04"), *marineId).value();
    CHECK_EQ(reinforcement->combat()->upgradeDamageFactor, 1.0f);
    REQUIRE(rts.stepScript(0.1).ok());
    CHECK_EQ(reinforcement->combat()->upgradeDamageFactor, 1.25f);
}

TEST_CASE("rts.scriptAbilityLoadsCanonicalDefinitionDebitsAndSettlesChannelDamage") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    REQUIRE(rts.configureScriptWorld(12, 12, 1.0f).ok());
    REQUIRE(rts.loadScriptContent(R"({
        "units":[{"id":"marine","health":80,"speed":3,"radius":0.3}],
        "abilities":[{"id":"frag_grenade","casterUnit":"marine","targetType":"point",
                      "range":6,"radius":1.8,"cooldown":6,"damage":28,"damageType":"normal",
                      "castTime":0.5,"interruptOnDamage":true,"resourceType":"energy","resourceCost":10}]
    })")
                .ok());
    auto* blue     = rts.newFaction(subject("00000000-0000-7000-8000-00000000fe01")).value();
    auto* red      = rts.newFaction(subject("00000000-0000-7000-8000-00000000fe02")).value();
    auto  marineId = eve::LogicalId::parse("unit:marine");
    REQUIRE(marineId.has_value());
    Unit* caster        = rts.newFactionUnit(*blue, subject("00000000-0000-7000-8000-00000000fe03"), *marineId).value();
    Unit* target        = rts.newFactionUnit(*red, subject("00000000-0000-7000-8000-00000000fe04"), *marineId).value();
    caster->motion()->x = 1.0f;
    caster->motion()->y = 1.0f;
    target->motion()->x = 4.0f;
    target->motion()->y = 1.0f;
    REQUIRE(rts.addScriptResource(*blue, "energy", 20).ok());
    REQUIRE(rts.castScriptAbility(*caster, "frag_grenade", {}, {4.0f, 1.0f}).ok());
    CHECK_EQ(rts.scriptResource(*blue, "energy").value(), 10);
    CHECK_EQ(target->durability()->state.health, 80.0);
    CHECK(!rts.castScriptAbility(*caster, "frag_grenade", {}, {4.0f, 1.0f}).ok());
    CHECK_EQ(rts.scriptResource(*blue, "energy").value(), 10);
    REQUIRE(rts.stepScript(0.5).ok());
    CHECK_EQ(target->durability()->state.health, 52.0);
    const auto  damageProjection = rts.inspectFrameEvents();
    const auto* damageEvents     = damageProjection.getIf<eve::Value::Array>();
    REQUIRE(damageEvents != nullptr);
    REQUIRE_EQ(damageEvents->size(), 3);
    const auto* damageEvent = (*damageEvents)[0].getIf<eve::Value::Object>();
    REQUIRE(damageEvent != nullptr);
    CHECK_EQ(*damageEvent->at("channel").getIf<std::string>(), "ability");
    CHECK_EQ(*damageEvent->at("damageType").getIf<std::string>(), "normal");
    CHECK_EQ(*damageEvent->at("appliedHealthDamage").getIf<double>(), 28.0);
    CHECK_EQ(*damageEvent->at("target").getIf<std::string>(), target->identity()->subject.format());
    const auto* castEvent      = (*damageEvents)[1].getIf<eve::Value::Object>();
    const auto* completedEvent = (*damageEvents)[2].getIf<eve::Value::Object>();
    REQUIRE(castEvent != nullptr);
    REQUIRE(completedEvent != nullptr);
    CHECK_EQ(*castEvent->at("type").getIf<std::string>(), "ability_cast");
    CHECK_EQ(*completedEvent->at("type").getIf<std::string>(), "ability_channel_completed");
    REQUIRE(rts.stepScript(0.1).ok());
    const auto  clearedProjection = rts.inspectFrameEvents();
    const auto* clearedEvents     = clearedProjection.getIf<eve::Value::Array>();
    REQUIRE(clearedEvents != nullptr);
    CHECK(clearedEvents->empty());
}

TEST_CASE("rts.scriptStatusEffectsDriveMovementDamageIntakeRegenerationAndExpiry") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    REQUIRE(rts.configureScriptWorld(20, 12, 1.0f).ok());
    REQUIRE(rts.loadScriptContent(R"({
        "units":[{"id":"marine","health":100,"speed":2,"radius":0.3,"sightRange":8}],
        "statusEffects":[
          {"id":"stimmed","duration":2,"speedMultiplier":2,"damageMultiplier":1.5},
          {"id":"marked","duration":2,"incomingDamageMultiplier":1.5},
          {"id":"regenerating","duration":2,"healingPerSecond":5}
        ],
        "abilities":[
          {"id":"stim","casterUnit":"marine","targetType":"self","cooldown":0.1,
           "statusEffect":"stimmed"},
          {"id":"mark","casterUnit":"marine","targetType":"enemy","range":8,"cooldown":0.1,
           "statusEffect":"marked"},
          {"id":"regenerate","casterUnit":"marine","targetType":"self","cooldown":0.1,
           "statusEffect":"regenerating"},
          {"id":"blast","casterUnit":"marine","targetType":"point","range":8,"radius":1,
           "cooldown":0.1,"damage":20}
        ]
    })")
                .ok());
    auto*      blue     = rts.newFaction(subject("00000000-0000-7000-8000-00000000ff01")).value();
    auto*      red      = rts.newFaction(subject("00000000-0000-7000-8000-00000000ff02")).value();
    const auto marineId = eve::LogicalId::parse("unit:marine");
    REQUIRE(marineId.has_value());
    Unit* caster        = rts.newFactionUnit(*blue, subject("00000000-0000-7000-8000-00000000ff03"), *marineId).value();
    Unit* target        = rts.newFactionUnit(*red, subject("00000000-0000-7000-8000-00000000ff04"), *marineId).value();
    caster->motion()->x = 1.0f;
    caster->motion()->y = 1.0f;
    target->motion()->x = 4.0f;
    target->motion()->y = 1.0f;
    REQUIRE(rts.stepScript(0.01).ok());

    REQUIRE(rts.castScriptAbility(*caster, "stim", caster->identity()->subject, {}).ok());
    CHECK_EQ(caster->effects()->values.multiplier("speedMultiplier"), 2.0);
    CHECK_EQ(caster->effects()->values.multiplier("damageMultiplier"), 1.5);
    REQUIRE(rts.castScriptAbility(*caster, "mark", target->identity()->subject, {}).ok());
    CHECK_EQ(target->effects()->values.multiplier("incomingDamageMultiplier"), 1.5);
    REQUIRE(rts.castScriptAbility(*caster, "blast", {}, {4.0f, 1.0f}).ok());
    CHECK_EQ(target->durability()->state.health, 70.0);

    caster->durability()->state.health = 70.0;
    REQUIRE(rts.castScriptAbility(*caster, "regenerate", caster->identity()->subject, {}).ok());
    REQUIRE(rts.stepScript(1.0).ok());
    CHECK_EQ(caster->durability()->state.health, 75.0);
    REQUIRE(rts.stepScript(1.1).ok());
    CHECK_EQ(caster->effects()->values.multiplier("speedMultiplier"), 1.0);
    CHECK_EQ(caster->effects()->values.multiplier("damageMultiplier"), 1.0);
    CHECK_EQ(target->effects()->values.multiplier("incomingDamageMultiplier"), 1.0);
    const auto  expiredProjection = rts.inspectFrameEvents();
    const auto* expiredEvents     = expiredProjection.getIf<eve::Value::Array>();
    REQUIRE(expiredEvents != nullptr);
    int expiredCount = 0;
    for (const auto& event : *expiredEvents) {
        const auto* object = event.getIf<eve::Value::Object>();
        if (object != nullptr && *object->at("type").getIf<std::string>() == "status_expired") ++expiredCount;
    }
    CHECK_EQ(expiredCount, 3);
}

TEST_CASE("rts.scriptCommandLogSchedulesStableCommandsAtExactFixedTicks") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    REQUIRE(rts.configureScriptWorld(16, 8, 1.0f).ok());
    Faction* faction      = rts.newFaction(subject("00000000-0000-7000-8000-00000000ed01")).value();
    Unit*    unit         = rts.newFactionUnit(*faction, subject("00000000-0000-7000-8000-00000000ed02")).value();
    unit->motion()->speed = 1.0f;
    unit->motion()->arrivalRadius = 0.01f;

    eve::rts::RTSReplayCommand replay;
    replay.tick           = eve::SimulationTick{3};
    replay.units          = {unit->identity()->subject};
    replay.command.kind   = OrderKind::Move;
    replay.command.target = {6.0f, 0.0f};
    REQUIRE(rts.queueScriptCommand(replay).ok());
    auto exported = rts.exportScriptCommandLog();
    REQUIRE(exported.ok());
    CHECK(exported.value().find("EVERTS_COMMANDS 1") == 0);
    REQUIRE(rts.importScriptCommandLog(exported.value(), true).ok());

    REQUIRE(rts.stepScript(1.0).ok());
    CHECK_EQ(rts.scriptTick(), std::uint64_t{1});
    CHECK(unit->orders()->values.empty());
    REQUIRE(rts.stepScript(1.0).ok());
    CHECK_EQ(rts.scriptTick(), std::uint64_t{2});
    CHECK(unit->orders()->values.empty());
    REQUIRE(rts.stepScript(1.0).ok());
    CHECK_EQ(rts.scriptTick(), std::uint64_t{3});
    CHECK(unit->motion()->x > 0.0f);

    eve::rts::RTSReplayCommand past = replay;
    past.tick                       = eve::SimulationTick{2};
    CHECK(!rts.queueScriptCommand(std::move(past)).ok());
    CHECK(!rts.importScriptCommandLog(exported.value(), true).ok());
}

TEST_CASE("rts.scriptCommandLogReplaysConstructionCancellationAndBuildingSale") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    REQUIRE(rts.configureScriptWorld(12, 8, 1.0f).ok());
    REQUIRE(rts.loadScriptContent(R"({
        "units":[{"id":"worker","role":"worker","health":60,"speed":3,"radius":0.3,
                  "buildRate":1}],
        "buildings":[{"id":"depot","costResource":"minerals","cost":100,
                       "sellRefundRatio":0.5,"buildTime":4,"health":400}]
    })")
                .ok());
    auto*      faction  = rts.newFaction(subject("00000000-0000-7000-8000-00000000ed11")).value();
    const auto workerId = eve::LogicalId::parse("unit:worker");
    const auto depotId  = eve::LogicalId::parse("building:depot");
    REQUIRE(workerId.has_value());
    REQUIRE(depotId.has_value());
    auto* builder = rts.newFactionUnit(*faction, subject("00000000-0000-7000-8000-00000000ed12"), *workerId).value();
    const auto unfinishedSubject = subject("00000000-0000-7000-8000-00000000ed13");
    const auto completedSubject  = subject("00000000-0000-7000-8000-00000000ed14");
    REQUIRE(rts.addScriptResource(*faction, "minerals", 200).ok());
    auto* unfinished =
        rts.startScriptConstruction(*faction, unfinishedSubject, *depotId, {3.0f, 3.0f}, *builder).value();
    unfinished->construction()->progress = 0.5f;
    REQUIRE(rts.newFactionBuilding(*faction, completedSubject, *depotId).ok());

    eve::rts::RTSReplayCommand cancel;
    cancel.tick      = eve::SimulationTick{1};
    cancel.operation = eve::rts::RTSReplayOperation::CancelConstruction;
    cancel.producer  = unfinishedSubject;
    REQUIRE(rts.queueScriptCommand(cancel).ok());
    eve::rts::RTSReplayCommand sell;
    sell.tick      = eve::SimulationTick{1};
    sell.operation = eve::rts::RTSReplayOperation::SellBuilding;
    sell.producer  = completedSubject;
    REQUIRE(rts.queueScriptCommand(sell).ok());
    const auto exported = rts.exportScriptCommandLog();
    REQUIRE(exported.ok());
    CHECK(exported.value().find("EVERTS_COMMANDS 2") == 0);
    REQUIRE(rts.importScriptCommandLog(exported.value(), true).ok());
    REQUIRE(rts.stepScript(1.0).ok());
    CHECK(rts.findBuilding(unfinishedSubject) == nullptr);
    CHECK(rts.findBuilding(completedSubject) == nullptr);
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 225);
}

TEST_CASE("rts.scriptCheckpointRestoresRootsEconomyAndTimelineTogether") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    REQUIRE(rts.configureScriptWorld(10, 8, 1.0f).ok());
    Faction* faction  = rts.newFaction(subject("00000000-0000-7000-8000-00000000eb01")).value();
    Unit*    unit     = rts.newFactionUnit(*faction, subject("00000000-0000-7000-8000-00000000eb02")).value();
    unit->motion()->x = 2.0f;
    unit->motion()->y = 3.0f;
    REQUIRE(rts.addScriptResource(*faction, "minerals", 50).ok());
    REQUIRE(rts.stepScript(0.1).ok());
    REQUIRE(rts.captureScriptCheckpoint("slot-1").ok());

    unit->motion()->x = 9.0f;
    REQUIRE(rts.addScriptResource(*faction, "minerals", 25).ok());
    REQUIRE(rts.stepScript(0.1).ok());
    CHECK_EQ(rts.scriptTick(), std::uint64_t{2});
    REQUIRE_EQ(rts.scriptResource(*faction, "minerals").value(), std::int64_t{75});

    REQUIRE(rts.restoreScriptCheckpoint("slot-1").ok());
    CHECK_EQ(rts.scriptTick(), std::uint64_t{1});
    CHECK_EQ(unit->motion()->x, 2.0f);
    CHECK_EQ(unit->motion()->y, 3.0f);
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), std::int64_t{50});
    REQUIRE(rts.removeScriptCheckpoint("slot-1").ok());
    CHECK(!rts.restoreScriptCheckpoint("slot-1").ok());
}

TEST_CASE("rts.scriptCheckpointRebuildsChangedRootTopology") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    REQUIRE(rts.configureScriptWorld(10, 8, 1.0f).ok());
    const auto factionId  = subject("00000000-0000-7000-8000-00000000ea01");
    const auto originalId = subject("00000000-0000-7000-8000-00000000ea02");
    const auto laterId    = subject("00000000-0000-7000-8000-00000000ea03");
    Faction*   faction    = rts.newFaction(factionId).value();
    Unit*      original   = rts.newFactionUnit(*faction, originalId).value();
    original->motion()->x = 4.0f;
    REQUIRE(rts.addScriptResource(*faction, "minerals", 30).ok());
    REQUIRE(rts.captureScriptCheckpoint("before-production").ok());

    REQUIRE(rts.newFactionUnit(*faction, laterId).ok());
    REQUIRE_EQ(rts.unitCount(), std::size_t{2});
    REQUIRE(rts.addScriptResource(*faction, "minerals", 20).ok());

    REQUIRE(rts.restoreScriptCheckpoint("before-production").ok());
    CHECK_EQ(rts.unitCount(), std::size_t{1});
    CHECK(rts.findUnit(laterId) == nullptr);
    Unit* restored = rts.findUnit(originalId);
    REQUIRE(restored != nullptr);
    CHECK_EQ(restored->motion()->x, 4.0f);
    Faction* restoredFaction = rts.findFaction(factionId);
    REQUIRE(restoredFaction != nullptr);
    CHECK_EQ(rts.scriptResource(*restoredFaction, "minerals").value(), std::int64_t{30});
    CHECK(restored->crowd()->link.isBound());
    CHECK(restored->sensing()->link.isBound());
}

TEST_CASE("rts.scriptFogUsesCanonicalFovAndProjectsStableLastKnownContacts") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    REQUIRE(rts.configureScriptWorld(12, 8, 1.0f).ok());
    Faction* blue               = rts.newFaction(subject("00000000-0000-7000-8000-00000000ec01")).value();
    Faction* red                = rts.newFaction(subject("00000000-0000-7000-8000-00000000ec02")).value();
    Unit*    scout              = rts.newFactionUnit(*blue, subject("00000000-0000-7000-8000-00000000ec03")).value();
    Unit*    enemy              = rts.newFactionUnit(*red, subject("00000000-0000-7000-8000-00000000ec04")).value();
    scout->motion()->x          = 2.0f;
    scout->motion()->y          = 2.0f;
    scout->vision()->sightRange = 4.0f;
    enemy->motion()->x          = 4.0f;
    enemy->motion()->y          = 2.0f;
    REQUIRE(rts.stepScript(0.1).ok());
    CHECK(rts.scriptCellVisible(*blue, 4, 2).value());
    CHECK(rts.scriptCellExplored(*blue, 4, 2).value());
    auto visible = rts.scriptContact(*blue, enemy->identity()->subject);
    REQUIRE(visible.ok());
    const auto* visibleObject = visible.value().getIf<eve::Value::Object>();
    REQUIRE(visibleObject != nullptr);
    CHECK(visibleObject->at("visible").getIf<bool>() != nullptr);
    CHECK(*visibleObject->at("visible").getIf<bool>());

    enemy->motion()->x = 10.0f;
    REQUIRE(rts.stepScript(0.5).ok());
    auto remembered = rts.scriptContact(*blue, enemy->identity()->subject);
    REQUIRE(remembered.ok());
    const auto* rememberedObject = remembered.value().getIf<eve::Value::Object>();
    REQUIRE(rememberedObject != nullptr);
    CHECK(!*rememberedObject->at("visible").getIf<bool>());
    CHECK(*rememberedObject->at("age").getIf<double>() >= 0.5);
    CHECK(rts.scriptCellExplored(*blue, 4, 2).value());
    CHECK(!rts.scriptCellVisible(*blue, 10, 2).value());
    CHECK(!rts.scriptCellVisible(*red, -1, 2).ok());
}

TEST_CASE("rts.scriptMatchFacadeRunsResourceVictoryAndStableSurrenderLifecycle") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    REQUIRE(rts.configureScriptWorld(8, 8, 1.0f).ok());
    Faction* blue         = rts.newFaction(subject("00000000-0000-7000-8000-00000000eb01")).value();
    Faction* red          = rts.newFaction(subject("00000000-0000-7000-8000-00000000eb02")).value();
    Match*   economyMatch = rts.newMatch(subject("00000000-0000-7000-8000-00000000eb03")).value();
    REQUIRE(rts.configureMatch(*economyMatch, eve::rts::VictoryRule::ResourceTarget, "minerals", 100.0).ok());
    REQUIRE(rts.addMatchParticipant(*economyMatch, *blue, 1).ok());
    REQUIRE(rts.addMatchParticipant(*economyMatch, *red, 2).ok());
    REQUIRE(rts.startMatch(*economyMatch).ok());
    CHECK(!rts.configureMatch(*economyMatch, eve::rts::VictoryRule::Annihilation).ok());
    REQUIRE(rts.addScriptResource(*blue, "minerals", 100).ok());
    REQUIRE(rts.stepScript(0.1).ok());
    CHECK_EQ(static_cast<int>(economyMatch->state()->phase), static_cast<int>(eve::rts::MatchPhase::Finished));
    CHECK_EQ(economyMatch->state()->winningTeam, 1);
    auto projected = rts.inspectMatch(*economyMatch);
    REQUIRE(projected.ok());
    const auto* matchObject = projected.value().getIf<eve::Value::Object>();
    REQUIRE(matchObject != nullptr);
    CHECK_EQ(*matchObject->at("phase").getIf<std::string>(), "finished");
    CHECK_EQ(*matchObject->at("winningTeam").getIf<std::int64_t>(), std::int64_t{1});

    Match* surrender = rts.newMatch(subject("00000000-0000-7000-8000-00000000eb04")).value();
    REQUIRE(rts.addMatchParticipant(*surrender, *blue, 1).ok());
    REQUIRE(rts.addMatchParticipant(*surrender, *red, 2).ok());
    REQUIRE(rts.startMatch(*surrender).ok());
    REQUIRE(rts.surrenderMatch(*surrender, *red).ok());
    CHECK_EQ(static_cast<int>(surrender->state()->phase), static_cast<int>(eve::rts::MatchPhase::Finished));
    CHECK_EQ(surrender->state()->winningTeam, 1);

    Match* foreign = Match::createMatch(subject("00000000-0000-7000-8000-00000000eb05"));
    CHECK(!rts.startMatch(*foreign).ok());
    foreign->release();
}

// rts_script_2.cpp
TEST_CASE("rts.scriptProfileMaterializesCanonicalContentAndAutomaticFire") {
    eve::rts::RTS module;
    REQUIRE(module.configureScriptWorld(20, 20, 1.0f).ok());
    auto loaded = module.loadScriptContent(R"JSON({
      "weapons":[{"id":"rifle","damageType":"normal","damage":8,"range":6,"cooldown":0.1,
        "projectileSpeed":0,"magazineSize":8,"reloadTime":1}],
      "units":[
        {"id":"marine","role":"soldier","weaponType":"rifle","health":80,"speed":3,
         "radius":0.35,"attackRange":6,"sightRange":8,"targetTags":["biological"]},
        {"id":"worker","role":"worker","health":50,"speed":2,"radius":0.3,"sightRange":5}],
      "buildings":[{"id":"command_center","health":900,"radius":1.2,"dropoff":true,
        "powerProvided":10,"buildInfluenceRadius":8}]
    })JSON");
    REQUIRE(loaded.ok());

    auto unitId     = eve::LogicalId::parse("unit:marine");
    auto workerId   = eve::LogicalId::parse("unit:worker");
    auto buildingId = eve::LogicalId::parse("building:command_center");
    REQUIRE(unitId.has_value());
    REQUIRE(workerId.has_value());
    REQUIRE(buildingId.has_value());
    auto firstFaction  = module.newFaction(subject("00000000-0000-7000-8000-000000000074"));
    auto secondFaction = module.newFaction(subject("00000000-0000-7000-8000-000000000075"));
    REQUIRE(firstFaction.ok());
    REQUIRE(secondFaction.ok());
    Faction* first          = std::move(firstFaction).takeValue();
    Faction* second         = std::move(secondFaction).takeValue();
    auto     attackerResult = module.newFactionUnit(*first, subject("00000000-0000-7000-8000-000000000076"), *unitId);
    auto     targetResult   = module.newFactionUnit(*second, subject("00000000-0000-7000-8000-000000000077"), *unitId);
    auto     workerResult   = module.newFactionUnit(*first, subject("00000000-0000-7000-8000-000000000078"), *workerId);
    auto     buildingResult =
        module.newFactionBuilding(*first, subject("00000000-0000-7000-8000-000000000079"), *buildingId);
    REQUIRE(attackerResult.ok());
    REQUIRE(targetResult.ok());
    REQUIRE(workerResult.ok());
    REQUIRE(buildingResult.ok());
    Unit*     attacker    = std::move(attackerResult).takeValue();
    Unit*     target      = std::move(targetResult).takeValue();
    Unit*     worker      = std::move(workerResult).takeValue();
    Building* building    = std::move(buildingResult).takeValue();
    attacker->motion()->x = 2.0f;
    attacker->motion()->y = 2.0f;
    target->motion()->x   = 5.0f;
    target->motion()->y   = 2.0f;

    CHECK_EQ(attacker->durability()->state.maxHealth, 80.0);
    CHECK_EQ(attacker->motion()->speed, 3.0f);
    CHECK(attacker->weapon()->link.isBound());
    CHECK(worker->worker()->autoAssign);
    CHECK_EQ(worker->worker()->capacity, 10.0f);
    CHECK_EQ(building->integrity()->state.maxHealth, 900.0);
    CHECK_EQ(building->infrastructure()->powerProduced, 10.0f);
    REQUIRE_EQ(building->dropoff()->acceptedResources.size(), 1u);
    CHECK_EQ(building->dropoff()->acceptedResources.front(), "minerals");

    const double before = target->durability()->state.health;
    for (int index = 0; index < 4; ++index) REQUIRE(module.stepScript(0.1).ok());
    CHECK(target->durability()->state.health < before);
    const auto  frameProjection = module.inspectFrameEvents();
    const auto* events          = frameProjection.getIf<eve::Value::Array>();
    REQUIRE(events != nullptr);
    bool observedFire   = false;
    bool observedDamage = false;
    for (const auto& event : *events) {
        const auto* object = event.getIf<eve::Value::Object>();
        if (object == nullptr) continue;
        const auto  found = object->find("type");
        const auto* type  = found == object->end() ? nullptr : found->second.getIf<std::string>();
        if (type != nullptr && *type == "weapon_fired") observedFire = true;
        if (type != nullptr && *type == "damage") observedDamage = true;
    }
    CHECK(observedFire);
    CHECK(observedDamage);
}

TEST_CASE("rts.scriptGridBlocksHitscanAndNewObstaclesInterceptProjectiles") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    module;
    REQUIRE(module.configureScriptWorld(12, 6, 1.0f).ok());
    REQUIRE(module
                .loadScriptContent(R"JSON({
      "weapons":[
        {"id":"beam","damageType":"normal","damage":10,"range":10,"cooldown":0.1,
         "projectileSpeed":0,"blockedByObstacles":true},
        {"id":"shell","damageType":"normal","damage":10,"range":10,"cooldown":10,
         "projectileSpeed":4,"blockedByObstacles":true}],
      "units":[
        {"id":"beam_unit","weaponType":"beam","health":50,"speed":0,"radius":0.3,
         "attackRange":10,"sightRange":12},
        {"id":"shell_unit","weaponType":"shell","health":50,"speed":0,"radius":0.3,
         "attackRange":10,"sightRange":12},
        {"id":"target","health":50,"speed":0,"radius":0.3,"sightRange":1}]
    })JSON")
                .ok());
    auto beamType   = eve::LogicalId::parse("unit:beam_unit");
    auto shellType  = eve::LogicalId::parse("unit:shell_unit");
    auto targetType = eve::LogicalId::parse("unit:target");
    REQUIRE(beamType.has_value());
    REQUIRE(shellType.has_value());
    REQUIRE(targetType.has_value());
    Faction* blue = module.newFaction(subject("00000000-0000-7000-8000-00000000e901")).value();
    Faction* red  = module.newFaction(subject("00000000-0000-7000-8000-00000000e902")).value();
    Unit*    beam = module.newFactionUnit(*blue, subject("00000000-0000-7000-8000-00000000e903"), *beamType).value();
    Unit*    beamTarget =
        module.newFactionUnit(*red, subject("00000000-0000-7000-8000-00000000e904"), *targetType).value();
    beam->motion()->x       = 1.5f;
    beam->motion()->y       = 1.5f;
    beamTarget->motion()->x = 7.5f;
    beamTarget->motion()->y = 1.5f;
    REQUIRE(module.setScriptNavigationBlocked(4, 1, true).ok());
    REQUIRE(module.stepScript(0.2).ok());
    CHECK_EQ(beamTarget->durability()->state.health, 50.0);
    REQUIRE(module.setScriptNavigationBlocked(4, 1, false).ok());
    REQUIRE(module.stepScript(0.2).ok());
    CHECK(beamTarget->durability()->state.health < 50.0);

    Unit* shell = module.newFactionUnit(*blue, subject("00000000-0000-7000-8000-00000000e905"), *shellType).value();
    Unit* shellTarget =
        module.newFactionUnit(*red, subject("00000000-0000-7000-8000-00000000e906"), *targetType).value();
    shell->motion()->x       = 1.5f;
    shell->motion()->y       = 4.5f;
    shellTarget->motion()->x = 7.5f;
    shellTarget->motion()->y = 4.5f;
    REQUIRE(module.stepScript(0.1).ok());
    REQUIRE(module.setScriptTerrainElevation(4, 4, 2.0f).ok());
    REQUIRE(module.stepScript(1.0).ok());
    REQUIRE(module.stepScript(1.0).ok());
    CHECK_EQ(shellTarget->durability()->state.health, 50.0);
}

TEST_CASE("rts.scriptTerrainHeightDrivesFogFireLinesAndCheckpoints") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    module;
    REQUIRE(module.configureScriptWorld(12, 5, 1.0f).ok());
    REQUIRE(module
                .loadScriptContent(R"JSON({
      "weapons":[{"id":"rifle","damageType":"normal","damage":10,"range":10,"cooldown":0.1,
        "projectileSpeed":0,"blockedByObstacles":true}],
      "units":[
        {"id":"shooter","weaponType":"rifle","health":50,"speed":0,"radius":0.3,
         "attackRange":10,"sightRange":12,"firingHeight":1,"targetHeight":1},
        {"id":"target","health":50,"speed":0,"radius":0.3,"sightRange":1,"targetHeight":1}]
    })JSON")
                .ok());
    const auto shooterType = eve::LogicalId::parse("unit:shooter");
    const auto targetType  = eve::LogicalId::parse("unit:target");
    REQUIRE(shooterType.has_value());
    REQUIRE(targetType.has_value());
    Faction* blue = module.newFaction(subject("00000000-0000-7000-8000-00000000e801")).value();
    Faction* red  = module.newFaction(subject("00000000-0000-7000-8000-00000000e802")).value();
    Unit* shooter = module.newFactionUnit(*blue, subject("00000000-0000-7000-8000-00000000e803"), *shooterType).value();
    Unit* target  = module.newFactionUnit(*red, subject("00000000-0000-7000-8000-00000000e804"), *targetType).value();
    shooter->motion()->x = 1.5f;
    shooter->motion()->y = 2.5f;
    target->motion()->x  = 7.5f;
    target->motion()->y  = 2.5f;
    CHECK_EQ(shooter->combat()->firingHeight, 1.0f);
    CHECK_EQ(target->combat()->targetHeight, 1.0f);
    REQUIRE(module.setScriptTerrainElevation(4, 2, 2.0f).ok());
    REQUIRE(module.captureScriptCheckpoint("ridge").ok());
    REQUIRE(module.stepScript(0.2).ok());
    CHECK_EQ(target->durability()->state.health, 50.0);
    CHECK(!module.scriptCellVisible(*blue, 7, 2).value());

    REQUIRE(module.setScriptTerrainElevation(1, 2, 3.0f).ok());
    REQUIRE(module.stepScript(0.2).ok());
    CHECK(module.scriptCellVisible(*blue, 7, 2).value());
    CHECK(target->durability()->state.health < 50.0);

    REQUIRE(module.setScriptTerrainElevation(4, 2, 0.0f).ok());
    REQUIRE(module.restoreScriptCheckpoint("ridge").ok());
    CHECK_EQ(module.scriptTerrainElevation(4, 2).value(), 2.0f);
    CHECK_EQ(module.scriptTerrainElevation(1, 2).value(), 0.0f);
}

// rts_settlement.cpp
namespace {

eve::SubjectRef subject(std::uint8_t value) {
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = value;
    return eve::SubjectRef::fromPersistentId(eve::PersistentId(bytes));
}

}  // namespace

TEST_CASE("rts.settlement.configurationUsesAttachedCombatProvider") {
    eve::rts::RTS                      rts;
    eve::settlement::SettlementRuleSet emptyRules;
    REQUIRE(emptyRules.configure({}).ok());
    CHECK(!rts.configureSettlementRules(emptyRules).ok());

    eve::combat::DamageRuntime damage;
    rts.setCombatProviders(nullptr, &damage);
    CHECK(rts.configureSettlementRules(emptyRules).ok());
}

TEST_CASE("rts.settlement.rulesReadCanonicalCombatContext") {
    eve::settlement::SettlementRule exposed;
    exposed.id        = "rts.exposed";
    exposed.stage     = eve::settlement::StageKind::TargetMitigation;
    exposed.operation = eve::settlement::RuleOperation::Multiply;
    exposed.value     = 2.0;
    exposed.when      = "context.incoming_damage_multiplier > 1.2 && context.target_health_ratio == 1";
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({exposed}).ok());

    eve::combat::DamageRuntime damage;
    eve::rts::RTS              rts;
    rts.setCombatProviders(nullptr, &damage);
    REQUIRE(rts.configureSettlementRules(rules).ok());

    eve::combat::CombatState target;
    target.subject = subject(2);
    target.health = target.maxHealth = 100.0;
    target.poise = target.maxPoise = 10.0;
    eve::combat::DamageRequest request;
    request.source                   = subject(1);
    request.target                   = target.subject;
    request.damageType               = "damage.physical";
    request.healthDamage             = 10.0;
    request.incomingDamageMultiplier = 1.5;
    auto outcome                     = damage.apply(target, request);
    REQUIRE(outcome.ok());
    CHECK_EQ(outcome.value().appliedHealthDamage, 30.0);
    CHECK_EQ(target.health, 70.0);
}

TEST_CASE("rts.settlement.healingUsesTheConfiguredCombatPipeline") {
    eve::settlement::SettlementRule amplified;
    amplified.id           = "rts.healing.amplified";
    amplified.stage        = eve::settlement::StageKind::SourceModifiers;
    amplified.operation    = eve::settlement::RuleOperation::Multiply;
    amplified.value        = 2.0;
    amplified.filter.kinds = {"heal"};
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({amplified}).ok());

    eve::combat::DamageRuntime settlement;
    REQUIRE(settlement.configureSettlementRules(rules).ok());
    eve::combat::CombatState target;
    target.subject   = subject(2);
    target.health    = 50.0;
    target.maxHealth = 100.0;

    auto restored = settlement.heal(target, subject(1), 10.0);
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value().requested, 10.0);
    CHECK_EQ(restored.value().applied, 20.0);
    CHECK_EQ(target.health, 70.0);
}
}  // namespace test_rts_script
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_scene_authoring)
#include "PrivateTestScope.h"
#include <cmath>
#include <limits>
#include "scene/editing/SceneAuthoring.h"
#include "scene/editing/SceneAuthoringDocument.h"
#include "scene/editor/SceneEditorSession.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// scene_authoring.cpp
namespace test_scene_authoring {
namespace {
using eve::editing::Value;
using eve::scene_editor::SceneEditorSession;
std::unique_ptr<SceneEditorSession> authoring() {
    return std::make_unique<SceneEditorSession>(
        std::make_unique<eve::scene_editing::SceneAuthoringDocumentTarget>("authoring-test"));
}
Value bounds() { return Value::Array{-0.5, -0.5, -0.5, 0.5, 0.5, 0.5}; }
Value camera() {
    return Value::Object{{"eye", Value::Array{0.0, 0.0, 10.0}},
                         {"target", Value::Array{0.0, 0.0, 0.0}},
                         {"up", Value::Array{0.0, 1.0, 0.0}},
                         {"fov", 60.0},
                         {"near", 0.1},
                         {"far", 100.0},
                         {"exposure", 0.0},
                         {"ambient", Value::Array{0.2, 0.2, 0.2}}};
}
eve::scene_editing::SceneAuthoringDocumentTarget copied(const SceneEditorSession& session) {
    eve::scene_editing::SceneAuthoringDocumentTarget target("inspect");
    auto                                             operation = target.makeRestore(session.snapshot());
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    return target;
}
double component(const Value& value, const char* key, std::size_t index) {
    return *value.getIf<Value::Object>()->at(key).getIf<Value::Array>()->at(index).getIf<double>();
}
}  // namespace

TEST_CASE("scene.authoring.document_atomic_spawn_presentation_roundtrip_history") {
    auto       session = authoring();
    const auto empty   = session->saveJson();
    REQUIRE(
        session
            ->execute("scene.authoring.spawn.v1", Value::Object{{"object", "crate"},
                                                                {"source", Value::Object{{"kind", "box"}, {"seed", 7}}},
                                                                {"position", Value::Array{0.0, 3.0, 0.0}}})
            .ok());
    const auto spawned = session->saveJson();
    REQUIRE(spawned != empty);
    REQUIRE(session->revision() > 0);
    REQUIRE(session->undo().ok());
    REQUIRE_EQ(session->saveJson(), empty);
    REQUIRE(session->redo().ok());
    REQUIRE_EQ(session->saveJson(), spawned);
    REQUIRE(session->execute("scene.authoring.camera.set.v1", Value::Object{{"value", camera()}}).ok());
    const auto cameraJson = session->saveJson();
    REQUIRE(session
                ->execute("scene.authoring.lighting.set.v1",
                          Value::Object{{"value", Value::Object{{"background", Value::Array{0.1, 0.2, 0.3}},
                                                                {"direction", Value::Array{-0.5, -1.0, -0.4}},
                                                                {"color", Value::Array{1.0, 1.0, 1.0}}}}})
                .ok());
    const auto complete = session->saveJson();
    REQUIRE(session->undo().ok());
    REQUIRE_EQ(session->saveJson(), cameraJson);
    REQUIRE(session->redo().ok());
    REQUIRE_EQ(session->saveJson(), complete);
    auto restored = authoring();
    REQUIRE(restored->restoreJson(complete).ok());
    REQUIRE_EQ(restored->saveJson(), complete);
    REQUIRE(restored->undo().ok());
    REQUIRE_EQ(restored->saveJson(), empty);
}

TEST_CASE("scene.authoring.rejected_source_schema_camera_and_parent_preserve_state") {
    auto       session = authoring();
    const auto empty   = session->saveJson();
    REQUIRE(!session
                 ->execute("scene.authoring.spawn.v1",
                           Value::Object{{"object", "broken"},
                                         {"source", Value::Object{{"kind", "asset"}, {"path", "missing.glb"}}}})
                 .ok());
    REQUIRE_EQ(session->saveJson(), empty);
    REQUIRE(!session
                 ->execute("scene.authoring.spawn.v1",
                           Value::Object{
                               {"object", "broken"}, {"parent", "missing"}, {"source", Value::Object{{"kind", "box"}}}})
                 .ok());
    REQUIRE_EQ(session->saveJson(), empty);
    auto badCamera                            = camera();
    (*badCamera.getIf<Value::Object>())["up"] = Value::Array{0.0, 0.0, 1.0};
    REQUIRE(!session->execute("scene.authoring.camera.set.v1", Value::Object{{"value", badCamera}}).ok());
    REQUIRE_EQ(session->saveJson(), empty);
    auto document                                       = session->snapshot();
    (*document.getIf<Value::Object>())["schemaVersion"] = 99;
    REQUIRE(!session->execute("scene.snapshot.restore.v1", document).ok());
    REQUIRE_EQ(session->saveJson(), empty);
    (*document.getIf<Value::Object>())["schemaVersion"] = 2;
    (*document.getIf<Value::Object>())["unknown"]       = true;
    REQUIRE(!session->execute("scene.snapshot.restore.v1", document).ok());
    REQUIRE_EQ(session->saveJson(), empty);
}

TEST_CASE("scene.authoring.ground_and_gap_solve_world_geometry_under_reflected_parent") {
    auto session = authoring();
    REQUIRE(session
                ->execute("scene.object.create.v1", Value::Object{{"object", "parent"},
                                                                  {"position", Value::Array{3.0, 2.0, -1.0}},
                                                                  {"rotation", Value::Array{0.2, 0.7, 0.1}},
                                                                  {"scale", Value::Array{-2.0, 3.0, 0.5}}})
                .ok());
    REQUIRE(session
                ->execute(
                    "scene.object.create.v1",
                    Value::Object{{"object", "crate"}, {"parent", "parent"}, {"rotation", Value::Array{0.4, 0.5, 0.2}}})
                .ok());
    REQUIRE(session
                ->execute("scene.object.create.v1",
                          Value::Object{{"object", "reference"}, {"position", Value::Array{7.0, 0.0, 0.0}}})
                .ok());
    const auto before = session->saveJson();
    REQUIRE(session
                ->execute("scene.placement.ground.v1",
                          Value::Object{{"object", "crate"}, {"bounds", bounds()}, {"height", 0.0}})
                .ok());
    auto target   = copied(*session);
    auto geometry = eve::scene_editing::inspectAuthoringObject(target, eve::editing::ObjectId("crate"),
                                                               {-0.5, -0.5, -0.5, 0.5, 0.5, 0.5});
    REQUIRE(geometry.ok());
    REQUIRE(std::abs(component(geometry.value(), "worldMinimum", 1)) < 1e-9);
    REQUIRE(session->undo().ok());
    REQUIRE_EQ(session->saveJson(), before);
    REQUIRE(session
                ->execute("scene.placement.align.v1", Value::Object{{"object", "crate"},
                                                                    {"bounds", bounds()},
                                                                    {"reference", "reference"},
                                                                    {"referenceBounds", bounds()},
                                                                    {"axis", "x"},
                                                                    {"gap", 0.25}})
                .ok());
    target   = copied(*session);
    geometry = eve::scene_editing::inspectAuthoringObject(target, eve::editing::ObjectId("crate"),
                                                          {-0.5, -0.5, -0.5, 0.5, 0.5, 0.5});
    REQUIRE(geometry.ok());
    REQUIRE(std::abs(component(geometry.value(), "worldMinimum", 0) - 7.75) < 1e-9);
    const auto aligned = session->saveJson();
    REQUIRE(!session
                 ->execute("scene.placement.align.v1", Value::Object{{"object", "parent"},
                                                                     {"bounds", bounds()},
                                                                     {"reference", "crate"},
                                                                     {"referenceBounds", bounds()},
                                                                     {"axis", "x"},
                                                                     {"gap", 0.0}})
                 .ok());
    REQUIRE_EQ(session->saveJson(), aligned);
}

TEST_CASE("scene.authoring.screen_anchor_preserves_depth_and_rejects_singular_parent") {
    auto session = authoring();
    REQUIRE(session->execute("scene.object.create.v1", Value::Object{{"object", "crate"}}).ok());
    const auto before  = session->saveJson();
    Value      payload = Value::Object{{"object", "crate"},
                                       {"anchor", Value::Array{0.0, 0.0, 0.0}},
                                       {"eye", Value::Array{0.0, 0.0, 10.0}},
                                       {"target", Value::Array{0.0, 0.0, 0.0}},
                                       {"up", Value::Array{0.0, 1.0, 0.0}},
                                       {"fov", 60.0},
                                       {"aspect", 2.0},
                                       {"screen", Value::Array{0.75, 0.25}}};
    REQUIRE(session->execute("scene.placement.screen.v1", payload).ok());
    auto target    = copied(*session);
    auto transform = target.readTransform(eve::editing::ObjectId("crate"));
    REQUIRE(transform.ok());
    REQUIRE(std::abs(transform.value().x - 10.0 * std::tan(3.141592653589793 / 6.0)) < 1e-9);
    REQUIRE(std::abs(transform.value().y - 5.0 * std::tan(3.141592653589793 / 6.0)) < 1e-9);
    REQUIRE(std::abs(transform.value().z) < 1e-9);
    REQUIRE(session->undo().ok());
    REQUIRE_EQ(session->saveJson(), before);
    REQUIRE(session
                ->execute("scene.object.create.v1",
                          Value::Object{{"object", "flat"}, {"scale", Value::Array{0.000001, 0.000001, 0.000001}}})
                .ok());
    REQUIRE(session->execute("scene.object.reparent.v1", Value::Object{{"object", "crate"}, {"parent", "flat"}}).ok());
    const auto singular = session->saveJson();
    REQUIRE(!session->execute("scene.placement.screen.v1", payload).ok());
    REQUIRE_EQ(session->saveJson(), singular);
}
}  // namespace test_scene_authoring
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_scene_editing_commands)
#include "PrivateTestScope.h"
#include <map>
#include "editing/EditingValueJson.h"
#include "scene/editing/SceneTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// scene_editing_commands.cpp
namespace test_scene_editing_commands {
namespace {
using namespace eve::editing;
using namespace eve::scene_editing;

struct Commands final : IEditingCommandRegistry {
    std::map<std::string, EditingCommandPlanner> planners;
    Result<void> registerPlannedCommand(EditingCommandDescriptor descriptor, EditingCommandPlanner planner) override {
        planners.emplace(descriptor.id.value(), std::move(planner));
        return applied<void>();
    }
    Result<std::size_t> unregisterOwner(const std::string&) override {
        const auto count = planners.size();
        planners.clear();
        return applied<std::size_t>(count);
    }
    Result<CommandPlan> plan(IEditableTarget& target, const std::string& command, Value payload) {
        CommandRequest request;
        request.payload = std::move(payload);
        return planners.at(command)(target, request);
    }
};

TEST_CASE("scene.editing.commands_hierarchy_plans_are_reversible_and_backend_neutral") {
    Commands commands;
    REQUIRE(registerEditingCommands(commands).ok());
    for (const auto* id :
         {"scene.object.create.v1", "scene.object.delete.v1", "scene.object.rename.v1", "scene.object.reparent.v1"})
        REQUIRE(commands.planners.contains(id));
    SceneDocumentTarget document("document");
    RuntimeWorldTarget  runtime("runtime");
    for (SceneTargetBase* target :
         {static_cast<SceneTargetBase*>(&document), static_cast<SceneTargetBase*>(&runtime)}) {
        auto create =
            commands.plan(*target, "scene.object.create.v1",
                          Value::Object{{"object", "cube"}, {"name", "Cube"}, {"position", Value::Array{1, 2, 3}}});
        REQUIRE(create.ok());
        REQUIRE(!target->sceneObject(ObjectId("cube")).ok());
        REQUIRE_EQ(create.value().operations.size(), std::size_t(1));
        REQUIRE(target->applyDomainOperation(create.value().operations.front()).ok());
        REQUIRE_EQ(target->readTransform(ObjectId("cube")).value().y, 2.0);
        auto rename =
            commands.plan(*target, "scene.object.rename.v1", Value::Object{{"object", "cube"}, {"name", "Renamed"}});
        REQUIRE(rename.ok());
        auto op = rename.value().operations.front();
        REQUIRE(target->applyDomainOperation(op).ok());
        if (!op.inverseType.empty()) op.type = op.inverseType;
        op.payload = op.inverse;
        REQUIRE(target->applyDomainOperation(op).ok());
        REQUIRE_EQ(target->sceneObject(ObjectId("cube")).value().name, std::string("Cube"));
        auto cycle =
            commands.plan(*target, "scene.object.reparent.v1", Value::Object{{"object", "cube"}, {"parent", "cube"}});
        REQUIRE(!cycle.ok());
        auto remove = commands.plan(*target, "scene.object.delete.v1", Value::Object{{"object", "cube"}});
        REQUIRE(remove.ok());
        REQUIRE(target->applyDomainOperation(remove.value().operations.front()).ok());
        REQUIRE(target->sceneChildren({}).empty());
    }
}

TEST_CASE("scene.editing.snapshot_restore_is_validated_atomic_and_reversible") {
    SceneDocumentTarget target("scene");
    auto                create = target.makeCreate({ObjectId("root"), {}, "Root", {}});
    REQUIRE(create.ok());
    REQUIRE(target.applyDomainOperation(create.value()).ok());
    const auto before  = target.snapshotValue();
    auto       decoded = editorValueFromJson(editorValueToJson(before));
    REQUIRE(decoded.ok());
    auto restore = target.makeRestore(decoded.value());
    REQUIRE(restore.ok());
    REQUIRE(target.applyDomainOperation(restore.value()).ok());
    REQUIRE_EQ(editorValueToJson(before), editorValueToJson(target.snapshotValue()));
    auto invalid                                       = decoded.value();
    (*invalid.getIf<Value::Object>())["schemaVersion"] = 99;
    const auto revision                                = target.revision();
    REQUIRE(!target.makeRestore(invalid).ok());
    REQUIRE_EQ(target.revision(), revision);
    invalid       = decoded.value();
    auto& objects = *invalid.getIf<Value::Object>()->at("objects").getIf<Value::Array>();
    (*objects.front().getIf<Value::Object>())["parent"] = "root";
    REQUIRE(!target.makeRestore(invalid).ok());
    REQUIRE_EQ(editorValueToJson(before), editorValueToJson(target.snapshotValue()));
}
}  // namespace
}  // namespace test_scene_editing_commands
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_scene_editor_live_session)
#include "PrivateTestScope.h"
#include "editing/EditingValueJson.h"
#include "scene/SceneHost.h"
#include "scene/TransformSystem.h"
#include "scene/editing/SceneTarget.h"
#include "scene/editor/SceneEditorSession.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// scene_editor_live_session.cpp
namespace test_scene_editor_live_session {
TEST_CASE("scene.editor.live_session_preserves_links_rejects_divergence_and_stale_host") {
    using namespace eve::editing;
    auto created = eve::scene::SceneHost::createHost("scene-session-live");
    REQUIRE(created.ok());
    auto* host = created.value();
    host->setTree(eve::scene::node("root", {eve::scene::node("child")}));
    int external = 42;
    host->findById("child").value()->links.push_back({777, &external, 0});
    eve::scene_editor::SceneEditorSession session(
        std::make_unique<eve::scene_editing::SceneHostEditorTarget>("live", host));
    REQUIRE(session
                .execute("scene.object.update.v1",
                         Value::Object{{"object", "child"}, {"name", "Changed"}, {"position", Value::Array{2, 3, 4}}})
                .ok());
    REQUIRE_EQ(host->findById("child").value()->x, 2.f);
    REQUIRE_EQ(host->findById("child").value()->links.at(0).target, &external);
    REQUIRE(!session.execute("scene.object.delete.v1", Value::Object{{"object", "child"}}).ok());
    REQUIRE(host->hasNode("child"));
    REQUIRE(session.undo().ok());
    REQUIRE_EQ(host->findById("child").value()->x, 0.f);
    REQUIRE_EQ(host->findById("child").value()->name, std::string("child"));
    const auto before = session.saveJson();
    REQUIRE(host->appendNode(eve::scene::SceneNode{.id = "outside", .name = "Outside"}, "root") ==
            eve::scene::SceneMutationStatus::Applied);
    REQUIRE(!session.execute("scene.object.rename.v1", Value::Object{{"object", "child"}, {"name", "Refused"}}).ok());
    REQUIRE_EQ(session.saveJson(), before);
    ecs::DestroyEntity(host);
    REQUIRE(!session.execute("scene.object.rename.v1", Value::Object{{"object", "child"}, {"name", "Stale"}}).ok());
}

TEST_CASE("scene.editor.live_restore_reorders_hierarchy_atomically_and_can_reopen") {
    using namespace eve::editing;
    auto created = eve::scene::SceneHost::createHost("scene-session-restore");
    REQUIRE(created.ok());
    auto* host = created.value();
    host->setTree(eve::scene::node("z-root", {eve::scene::node("a-child")}));
    eve::scene_editor::SceneEditorSession session(
        std::make_unique<eve::scene_editing::SceneHostEditorTarget>("live", host));
    const auto before = session.saveJson();
    REQUIRE(session.restoreJson(before).ok());
    // Published arena is sorted by id, so the importer must not assume parent-first storage.
    eve::scene_editor::SceneEditorSession reopened(
        std::make_unique<eve::scene_editing::SceneHostEditorTarget>("live-again", host));
    REQUIRE_EQ(reopened.saveJson(), before);
    auto  changed = session.snapshot();
    auto& objects = *changed.getIf<Value::Object>()->at("objects").getIf<Value::Array>();
    for (auto& entry : objects) {
        auto& object     = *entry.getIf<Value::Object>();
        object["parent"] = *object.at("id").getIf<std::string>() == "a-child" ? "" : "a-child";
    }
    REQUIRE(session.restoreJson(editorValueToJson(changed)).ok());
    REQUIRE_EQ(host->getParentById("z-root")->id, std::string("a-child"));
    REQUIRE(session.undo().ok());
    REQUIRE_EQ(session.saveJson(), before);
}

TEST_CASE("scene.editor.detached_root_keeps_world_transform") {
    auto created = eve::scene::SceneHost::createHost("scene-detached-root");
    REQUIRE(created.ok());
    auto* host = created.value();
    host->setTree(eve::scene::node("root", {eve::scene::node("child")}));
    eve::scene_editor::SceneEditorSession session(
        std::make_unique<eve::scene_editing::SceneHostEditorTarget>("live", host));
    REQUIRE(session
                .execute("scene.object.update.v1",
                         eve::editing::Value::Object{
                             {"object", "child"}, {"parent", ""}, {"position", eve::editing::Value::Array{3, 4, 5}}})
                .ok());
    eve::scene::TransformSystem::updateHost(host);
    REQUIRE_EQ(host->findById("child").value()->world[3][0], 3.f);
    REQUIRE_EQ(host->findById("child").value()->world[3][1], 4.f);
}
}  // namespace test_scene_editor_live_session
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_scene_editor_session)
#include "PrivateTestScope.h"
#include <cmath>
#include "scene/editor/SceneEditorSession.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// scene_editor_session.cpp
namespace test_scene_editor_session {
TEST_CASE("scene.editor.session_create_restore_and_undo_share_one_history") {
    using namespace eve::editing;
    eve::scene_editor::SceneEditorSession session("level");
    const auto                            empty = session.saveJson();
    REQUIRE(session.execute("scene.object.create.v1", Value::Object{{"object", "box"}}).ok());
    const auto created = session.saveJson();
    REQUIRE(created != empty);
    REQUIRE(session.undo().ok());
    REQUIRE_EQ(session.saveJson(), empty);
    REQUIRE(session.redo().ok());
    REQUIRE_EQ(session.saveJson(), created);
    REQUIRE(session.restoreJson(empty).ok());
    REQUIRE_EQ(session.saveJson(), empty);
    REQUIRE(session.undo().ok());
    REQUIRE_EQ(session.saveJson(), created);
    REQUIRE(!session.restoreJson("{broken").ok());
    REQUIRE_EQ(session.saveJson(), created);
    REQUIRE(session.execute("scene.object.rename.v1", Value::Object{{"object", "box"}, {"name", "Crate"}}).ok());
    const auto renamed = session.saveJson();
    REQUIRE(!session
                 .execute("scene.object.update.v1", Value::Object{{"object", "box"},
                                                                  {"name", "Partial"},
                                                                  {"parent", "missing"},
                                                                  {"position", Value::Array{3, 4, 5}}})
                 .ok());
    REQUIRE_EQ(session.saveJson(), renamed);
}

TEST_CASE("scene.editor.physics_placement_commits_selected_objects_as_one_undo_step") {
    using namespace eve::editing;
    using namespace eve::scene_editor;
    SceneEditorSession session("physics-placement");
    REQUIRE(session
                .execute("scene.object.create.v1",
                         Value::Object{{"object", "wall"}, {"position", Value::Array{2.0, 0.5, 0.0}}})
                .ok());
    REQUIRE(session.execute("scene.object.create.v1", Value::Object{{"object", "crate"}}).ok());
    const auto              before = session.saveJson();
    PhysicsPlacementRequest request;
    request.objects.push_back({ObjectId("wall"), {}, 0.5, 2.0, 2.0, false});
    request.objects.push_back({ObjectId("crate"), {}, 0.5, 0.5, 0.5, true});
    REQUIRE(session.beginPhysicsPlacement(std::move(request)).ok());
    REQUIRE(session.physicsPlacementActive());
    for (int step = 0; step < 90; ++step) REQUIRE(session.updatePhysicsPlacement(4.0, 0.5, 0.0).ok());
    REQUIRE_EQ(session.saveJson(), before);
    REQUIRE(session.commitPhysicsPlacement().ok());
    REQUIRE(!session.physicsPlacementActive());
    REQUIRE(session.saveJson() != before);
    REQUIRE(session.undo().ok());
    REQUIRE_EQ(session.saveJson(), before);
}

TEST_CASE("scene.editor.physics_placement_cancel_is_non_destructive") {
    using namespace eve::editing;
    using namespace eve::scene_editor;
    SceneEditorSession session("physics-placement-cancel");
    REQUIRE(session.execute("scene.object.create.v1", Value::Object{{"object", "crate"}}).ok());
    const auto              before = session.saveJson();
    PhysicsPlacementRequest request;
    request.objects.push_back({ObjectId("crate"), {}, 0.5, 0.5, 0.5, true});
    REQUIRE(session.beginPhysicsPlacement(std::move(request)).ok());
    REQUIRE(session.updatePhysicsPlacement(3.0, 0.0, 0.0).ok());
    REQUIRE(session.cancelPhysicsPlacement().ok());
    REQUIRE_EQ(session.saveJson(), before);
}

TEST_CASE("scene.editor.physics_placement_transform_commits_scale_and_undoes_atomically") {
    using namespace eve::editing;
    using namespace eve::scene_editor;
    SceneEditorSession session("physics-placement-scale");
    REQUIRE(session.execute("scene.object.create.v1", Value::Object{{"object", "crate"}}).ok());
    const auto              before = session.saveJson();
    PhysicsPlacementRequest request;
    request.primaryObject = ObjectId("crate");
    request.objects.push_back({ObjectId("crate"), {}, 0.5, 0.5, 0.5, true});
    REQUIRE(session.beginPhysicsPlacement(std::move(request)).ok());
    auto frame = session.updatePhysicsPlacementTransform(1.0, 0.0, 0.0, 0.0, 0.0, 0.25, 2.0, 1.5, 0.75);
    REQUIRE(frame.ok());
    CHECK(std::abs(frame.value().objects[0].transform.scaleX - 2.0) < 0.001);
    REQUIRE(session.commitPhysicsPlacement().ok());
    REQUIRE(session.saveJson() != before);
    REQUIRE(session.undo().ok());
    REQUIRE_EQ(session.saveJson(), before);
}

TEST_CASE("scene.editor.physics_placement_auto_hull_cache_is_explicit_and_session_owned") {
    using namespace eve::editing;
    using namespace eve::scene_editor;
    SceneEditorSession session("physics-placement-auto-hull");
    REQUIRE(session.execute("scene.object.create.v1", Value::Object{{"object", "mesh"}}).ok());
    PhysicsPlacementRequest missing;
    auto                    object = PhysicsPlacementObject{ObjectId("mesh"), {}, 0.5, 0.5, 0.5, true};
    object.shape                   = PhysicsPlacementShape::Auto;
    missing.objects.push_back(object);
    const auto missingResult = session.beginPhysicsPlacement(std::move(missing));
    REQUIRE(missingResult.code() == Status::NotFound);
    const std::vector<float> cube = {-0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f,
                                     -0.5f, -0.5f, 0.5f,  0.5f, -0.5f, 0.5f,  -0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f};
    REQUIRE(session.cachePhysicsPlacementHull(ObjectId("mesh"), cube, 32).ok());
    const auto invalidCache = session.cachePhysicsPlacementHull(
        ObjectId("mesh"), {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f}, 32);
    REQUIRE(invalidCache.code() == Status::Rejected);
    PhysicsPlacementRequest cached;
    cached.objects.push_back(object);
    REQUIRE(session.beginPhysicsPlacement(std::move(cached)).ok());
    REQUIRE(session.cancelPhysicsPlacement().ok());
    REQUIRE(session.removePhysicsPlacementHull(ObjectId("mesh")).ok());
    const auto missingRemoval = session.removePhysicsPlacementHull(ObjectId("mesh"));
    REQUIRE(missingRemoval.code() == Status::NoOp);
}

TEST_CASE("scene.editor.physics_placement_compound_cache_round_trips_and_detects_stale_resources") {
    using namespace eve::editing;
    using namespace eve::scene_editor;
    SceneEditorSession       source("physics-placement-compound-cache-source");
    PhysicsPlacementCollider left;
    left.halfExtentX               = 0.25;
    left.halfExtentY               = 1.0;
    left.halfExtentZ               = 0.5;
    left.localX                    = -0.75;
    PhysicsPlacementCollider right = left;
    right.localX                   = 0.75;
    REQUIRE(source.cachePhysicsPlacementCompound(ObjectId("arch"), "mesh-hash-1", {left, right}).ok());
    const auto json = source.savePhysicsPlacementColliderCacheJson();

    SceneEditorSession restored("physics-placement-compound-cache-restored");
    REQUIRE(restored.execute("scene.object.create.v1", Value::Object{{"object", "arch"}}).ok());
    REQUIRE(restored.restorePhysicsPlacementColliderCacheJson(json).ok());
    PhysicsPlacementObject object{ObjectId("arch"), {}, 1.0, 1.0, 0.5, true};
    object.shape               = PhysicsPlacementShape::Auto;
    object.colliderResourceKey = "mesh-hash-1";
    PhysicsPlacementRequest request;
    request.objects.push_back(object);
    REQUIRE(restored.beginPhysicsPlacement(std::move(request)).ok());
    REQUIRE(restored.cancelPhysicsPlacement().ok());

    object.colliderResourceKey = "mesh-hash-2";
    PhysicsPlacementRequest stale;
    stale.objects.push_back(object);
    CHECK_EQ(static_cast<int>(restored.beginPhysicsPlacement(std::move(stale)).code()),
             static_cast<int>(Status::Conflict));
    const auto before = restored.savePhysicsPlacementColliderCacheJson();
    CHECK_EQ(static_cast<int>(restored.restorePhysicsPlacementColliderCacheJson("{}").code()),
             static_cast<int>(Status::Rejected));
    REQUIRE_EQ(restored.savePhysicsPlacementColliderCacheJson(), before);
}

TEST_CASE("scene.editor.physics_placement_detects_scene_changes_before_commit") {
    using namespace eve::editing;
    using namespace eve::scene_editor;
    SceneEditorSession session("physics-placement-conflict");
    REQUIRE(session.execute("scene.object.create.v1", Value::Object{{"object", "crate"}}).ok());
    PhysicsPlacementRequest request;
    request.objects.push_back({ObjectId("crate"), {}, 0.5, 0.5, 0.5, true});
    REQUIRE(session.beginPhysicsPlacement(std::move(request)).ok());
    REQUIRE(session.execute("scene.object.rename.v1", Value::Object{{"object", "crate"}, {"name", "Changed"}}).ok());
    const auto committed = session.commitPhysicsPlacement();
    REQUIRE_EQ(static_cast<int>(committed.code()), static_cast<int>(Status::Conflict));
    REQUIRE(session.physicsPlacementActive());
    REQUIRE(session.cancelPhysicsPlacement().ok());
}

TEST_CASE("scene.editor.physics_placement_converts_mixed_parent_world_preview_back_to_local_trs") {
    using namespace eve::editing;
    using namespace eve::scene_editor;
    SceneEditorSession session("physics-placement-parents");
    REQUIRE(session
                .execute("scene.object.create.v1",
                         Value::Object{{"object", "left-parent"}, {"position", Value::Array{10.0, 0.0, 0.0}}})
                .ok());
    REQUIRE(session
                .execute("scene.object.create.v1",
                         Value::Object{{"object", "right-parent"}, {"position", Value::Array{-10.0, 0.0, 0.0}}})
                .ok());
    REQUIRE(session
                .execute("scene.object.create.v1",
                         Value::Object{
                             {"object", "left"}, {"parent", "left-parent"}, {"position", Value::Array{1.0, 0.0, 0.0}}})
                .ok());
    REQUIRE(session
                .execute("scene.object.create.v1", Value::Object{{"object", "right"},
                                                                 {"parent", "right-parent"},
                                                                 {"position", Value::Array{2.0, 0.0, 0.0}}})
                .ok());
    PhysicsPlacementRequest request;
    request.primaryObject = ObjectId("left");
    request.objects.push_back({ObjectId("left"), {}, 0.5, 0.5, 0.5, true});
    request.objects.push_back({ObjectId("right"), {}, 0.5, 0.5, 0.5, true});
    REQUIRE(session.beginPhysicsPlacement(std::move(request)).ok());
    for (int step = 0; step < 30; ++step) REQUIRE(session.updatePhysicsPlacement(3.5, 0.0, 0.0).ok());
    REQUIRE(session.commitPhysicsPlacement().ok());
    const auto  snapshot = session.snapshot();
    const auto* root     = snapshot.getIf<Value::Object>();
    REQUIRE(root != nullptr);
    const auto* objects = root->at("objects").getIf<Value::Array>();
    REQUIRE(objects != nullptr);
    const auto localX = [&](const std::string& id) {
        for (const auto& entry : *objects) {
            const auto* fields = entry.getIf<Value::Object>();
            if (!fields) continue;
            const auto* objectId = fields->at("id").getIf<std::string>();
            if (objectId && *objectId == id) {
                const auto* transform = fields->at("transform").getIf<Value::Object>();
                return *transform->at("x").getIf<double>();
            }
        }
        return -1000.0;
    };
    CHECK(std::abs(localX("left") - 3.0) < 0.08);
    CHECK(std::abs(localX("right") - 4.0) < 0.08);
}

TEST_CASE("scene.editor.physics_placement_selected_parent_uses_its_final_world_for_child_local_trs") {
    using namespace eve::editing;
    using namespace eve::scene_editor;
    SceneEditorSession session("physics-placement-selected-parent");
    REQUIRE(session
                .execute("scene.object.create.v1",
                         Value::Object{{"object", "parent"}, {"position", Value::Array{5.0, 0.0, 0.0}}})
                .ok());
    REQUIRE(session
                .execute(
                    "scene.object.create.v1",
                    Value::Object{{"object", "child"}, {"parent", "parent"}, {"position", Value::Array{1.0, 0.0, 0.0}}})
                .ok());
    PhysicsPlacementRequest request;
    request.primaryObject = ObjectId("parent");
    request.objects.push_back({ObjectId("parent"), {}, 0.4, 0.4, 0.4, true});
    request.objects.push_back({ObjectId("child"), {}, 0.4, 0.4, 0.4, true});
    REQUIRE(session.beginPhysicsPlacement(std::move(request)).ok());
    for (int step = 0; step < 30; ++step) REQUIRE(session.updatePhysicsPlacement(7.5, 0.0, 0.0).ok());
    REQUIRE(session.commitPhysicsPlacement().ok());
    const auto  snapshot = session.snapshot();
    const auto* root     = snapshot.getIf<Value::Object>();
    REQUIRE(root != nullptr);
    const auto* objects = root->at("objects").getIf<Value::Array>();
    REQUIRE(objects != nullptr);
    const auto localX = [&](const std::string& id) {
        for (const auto& entry : *objects) {
            const auto* fields = entry.getIf<Value::Object>();
            if (!fields) continue;
            const auto* objectId = fields->at("id").getIf<std::string>();
            if (objectId && *objectId == id) {
                const auto* transform = fields->at("transform").getIf<Value::Object>();
                return *transform->at("x").getIf<double>();
            }
        }
        return -1000.0;
    };
    CHECK(std::abs(localX("parent") - 7.0) < 0.08);
    CHECK(std::abs(localX("child") - 1.0) < 0.08);
}
}  // namespace test_scene_editor_session
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_scene_follow_player)
#include "PrivateTestScope.h"
#include <zeroerr/unittest.h>
#include <cmath>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelBinding.h"
#include "scene/FollowPlayer.h"
#include "scene/LocationProfile.h"
#include "scene/LocationProfileBindings.h"
#include "scene/editor/ScenePhysicsPlacement.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// scene_follow_player.cpp
namespace test_scene_follow_player {
// scene_follow_player.cpp
using namespace eve::scene;

TEST_CASE("scene.followPlayer.matchesPcgOffsetsAndWaterThreshold") {
    FollowPlayerSettings settings;
    FollowPlayerInput    input;
    FollowPlayerOutput   output;
    input.hasPlayer = true;
    input.playerX   = 10.f;
    input.playerY   = 0.5f;
    input.playerZ   = -20.f;
    REQUIRE(static_cast<bool>(evaluateFollowPlayer(&output, &settings, &input)));
    CHECK(output.applyPosition);
    CHECK_EQ(output.x, 10.f);
    CHECK_EQ(output.y, 0.5f);
    CHECK_EQ(output.z, -20.f);

    settings.useOffset = true;
    settings.offsetX   = 1250.f;
    settings.offsetY   = 200.f;
    settings.offsetZ   = 300.f;
    REQUIRE(static_cast<bool>(evaluateFollowPlayer(&output, &settings, &input)));
    CHECK_EQ(output.x, 1260.f);
    CHECK_EQ(output.y, -199.5f);
    CHECK_EQ(output.z, -320.f);

    settings.waterObject = true;
    REQUIRE(static_cast<bool>(evaluateFollowPlayer(&output, &settings, &input)));
    CHECK_EQ(output.y, -129.5f);
    input.playerY = 1.f;
    REQUIRE(static_cast<bool>(evaluateFollowPlayer(&output, &settings, &input)));
    CHECK_EQ(output.y, -189.f);
}

TEST_CASE("scene.followPlayer.scaleNoPlayerAndAtomicValidation") {
    FollowPlayerSettings settings;
    settings.followPlayer = false;
    settings.useScale     = true;
    settings.scaleX       = 2.f;
    settings.scaleY       = 3.f;
    settings.scaleZ       = 4.f;
    FollowPlayerInput  input;
    FollowPlayerOutput output;
    output.x = 77.f;
    REQUIRE(static_cast<bool>(evaluateFollowPlayer(&output, &settings, &input)));
    CHECK(!output.applyPosition);
    CHECK(output.applyScale);
    CHECK_EQ(output.scaleX, 2.f);
    CHECK_EQ(output.scaleY, 3.f);
    CHECK_EQ(output.scaleZ, 4.f);

    const auto unchanged = output;
    settings.offsetX     = std::numeric_limits<float>::quiet_NaN();
    CHECK(!evaluateFollowPlayer(&output, &settings, &input));
    CHECK_EQ(output.applyScale, unchanged.applyScale);
    CHECK_EQ(output.scaleX, unchanged.scaleX);
    CHECK(!evaluateFollowPlayer(nullptr, &settings, &input));
}

// scene_location_profile.cpp
namespace {
eve::scene::LocationPose pose(float x) {
    eve::scene::LocationPose value;
    value.x = x;
    return value;
}

eve::scene::LocationBookmark bookmark(std::string name, float x) {
    eve::scene::LocationBookmark value;
    value.name       = std::move(name);
    value.camera     = pose(x);
    value.player     = pose(x + 10);
    value.controller = "FlyingCamera";
    value.scene      = "World";
    return value;
}
}  // namespace

TEST_CASE("scene.locationProfile.oneShotLocationAndFailureAtomicity") {
    eve::scene::LocationProfile profile;
    REQUIRE(profile.saveLocation(pose(2), pose(12)).ok());
    CHECK(profile.hasSavedLocation());
    auto loaded = profile.loadLocation();
    REQUIRE(loaded.ok());
    CHECK(loaded.value().camera.x == 2);
    CHECK(loaded.value().player->x == 12);
    CHECK(!profile.hasSavedLocation());
    CHECK(!profile.loadLocation().ok());

    auto invalid = pose(3);
    invalid.qw   = std::numeric_limits<float>::quiet_NaN();
    CHECK(!profile.saveLocation(invalid).ok());
    CHECK(!profile.hasSavedLocation());
}

TEST_CASE("scene.locationProfile.bookmarksMatchPcgSelectionRules") {
    eve::scene::LocationProfile profile;
    REQUIRE(profile.addBookmark(bookmark("A", 1)).ok());
    REQUIRE(profile.addBookmark(bookmark("B", 2)).ok());
    REQUIRE(profile.addBookmark(bookmark("C", 3)).ok());
    CHECK(!profile.addBookmark(bookmark("B", 9)).ok());
    CHECK(profile.getBookmarkCount() == 3);
    CHECK(profile.previousBookmark(0).value() == 2);
    CHECK(profile.nextBookmark(2).value() == 0);

    auto snapshot = profile.loadBookmark(1);
    REQUIRE(snapshot.ok());
    snapshot.value().camera.x = 22;
    REQUIRE(profile.overrideBookmark(1, snapshot.value(), "FirstPerson", "Second").ok());
    CHECK(profile.bookmarkAt(1)->name == "B");
    CHECK(profile.bookmarkAt(1)->camera.x == 22);
    CHECK(profile.bookmarkAt(1)->controller == "FirstPerson");
    CHECK(profile.getBookmarkName(1).value() == "B");
    CHECK(profile.getBookmarkController(1).value() == "FirstPerson");
    CHECK(profile.getBookmarkScene(1).value() == "Second");
    CHECK(!profile.getBookmarkName(3).ok());

    auto selected = profile.removeBookmark(2, 2);
    REQUIRE(selected.ok());
    CHECK(selected.value() == 1);
    CHECK(profile.getBookmarkCount() == 2);
    selected = profile.removeBookmark(0, 1);
    REQUIRE(selected.ok());
    CHECK(selected.value() == 0);
    profile.clearBookmarks();
    CHECK(profile.getBookmarkCount() == 0);
    CHECK(!profile.nextBookmark(0).ok());
}

TEST_CASE("scene.locationProfile.squirrelContract") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    eve::script::exposeResultBindings(table);
    eve::scene::exposeLocationProfileBindings(table);
    vm.run(vm.compileSource(R"(
        local profile = eve.LocationProfile();
        local camera = eve.LocationPose(); camera.x = 4.5;
        local player = eve.LocationPose(); player.x = 8.0;
        assert(profile.saveLocationWithPlayer(camera, player).ok);
        local saved = profile.loadLocation();
        assert(saved.ok && saved.value.hasPlayer && saved.value.camera.x == 4.5);
        local mark = eve.LocationBookmark(); mark.name = "Vista"; mark.controller = "Fly";
        mark.scene = "World"; mark.setCamera(camera); mark.setPlayer(player);
        assert(profile.addBookmark(mark).value == 0);
        assert(profile.getBookmarkName(0).value == "Vista");
        camera.x = 9.0; mark.setCamera(camera); mark.controller = "FirstPerson";
        assert(profile.overrideBookmark(0, mark).ok);
        assert(profile.loadBookmark(0).value.camera.x == 9);
        assert(profile.getBookmarkController(0).value == "FirstPerson");
        assert(profile.removeBookmark(0, 0).value == -1);
    )"));
}

TEST_CASE("scene.locationProfile.versionedJsonRoundTripAndAtomicRestore") {
    eve::scene::LocationProfile source;
    REQUIRE(source.saveLocation(pose(5), pose(15)).ok());
    REQUIRE(source.addBookmark(bookmark("Summit", 7)).ok());
    auto json = source.serializeJson();
    REQUIRE(json.ok());
    CHECK(json.value().find("eve.scene.location-profile") != std::string::npos);

    eve::scene::LocationProfile restored;
    REQUIRE(restored.restoreJson(json.value()).ok());
    CHECK(restored.hasSavedLocation());
    CHECK(restored.getBookmarkCount() == 1);
    CHECK(restored.getBookmarkName(0).value() == "Summit");
    CHECK(restored.loadBookmark(0).value().player->x == 17);

    CHECK(!restored
               .restoreJson(R"({"schemaId":"eve.scene.location-profile","schemaVersion":1,
        "saved":null,"bookmarks":[],"future":true})")
               .ok());
    CHECK(restored.getBookmarkCount() == 1);
    CHECK(!restored
               .restoreJson(R"({"schemaId":"eve.scene.location-profile","schemaVersion":2,
        "saved":null,"bookmarks":[]})")
               .ok());
    CHECK(restored.getBookmarkName(0).value() == "Summit");
}

// scene_physics_placement.cpp
using namespace eve::scene_editor;

namespace {
PhysicsPlacementObject placementBox(const char* id, double x, double y, double z, double hx, double hy, double hz,
                                    bool selected) {
    PhysicsPlacementObject object;
    object.object      = eve::editing::ObjectId(id);
    object.transform.x = x;
    object.transform.y = y;
    object.transform.z = z;
    object.halfExtentX = hx;
    object.halfExtentY = hy;
    object.halfExtentZ = hz;
    object.selected    = selected;
    return object;
}
}  // namespace

TEST_CASE("scene.physicsPlacement.rejects_invalid_admission_without_a_world") {
    PhysicsPlacementRequest request;
    request.objects.push_back(placementBox("wall", 0, 0, 0, 1, 1, 1, false));
    CHECK_EQ(static_cast<int>(ScenePhysicsPlacementBackend::create(std::move(request)).code()),
             static_cast<int>(eve::editing::Status::Rejected));
}

TEST_CASE("scene.physicsPlacement.drives_a_private_box3d_world_without_mutating_admission") {
    PhysicsPlacementRequest request;
    request.sourceRevision = 17;
    request.objects.push_back(placementBox("floor", 0, -0.5, 0, 5, 0.5, 5, false));
    request.objects.push_back(placementBox("crate", 0, 0.5, 0, 0.5, 0.5, 0.5, true));
    auto created = ScenePhysicsPlacementBackend::create(request);
    REQUIRE(created.ok());
    auto placement = std::move(created.value());
    REQUIRE_EQ(placement->sourceRevision(), 17U);
    REQUIRE(placement->setHandlePosition(3.0, 0.5, 0.0).ok());
    for (std::uint64_t tick = 1; tick <= 30; ++tick) REQUIRE(placement->step(tick, 1.0 / 60.0).ok());
    auto frame = placement->placementFrame();
    REQUIRE(frame.ok());
    REQUIRE_EQ(frame.value().objects.size(), 2U);
    CHECK(frame.value().objects[1].transform.x > 2.0);
    CHECK_EQ(request.objects[1].transform.x, 0.0);
    auto samples = placement->capture();
    REQUIRE(samples.ok());
    REQUIRE_EQ(samples.value().size(), 1U);
    CHECK_EQ(samples.value()[0].object, std::string("crate"));
}

TEST_CASE("scene.physicsPlacement.fast_drag_reaches_the_handle_in_one_preview_step") {
    PhysicsPlacementRequest request;
    request.objects.push_back(placementBox("crate", 0.0, 0.5, 0.0, 0.5, 0.5, 0.5, true));
    auto created = ScenePhysicsPlacementBackend::create(std::move(request));
    REQUIRE(created.ok());
    auto placement = std::move(created.value());
    REQUIRE(placement->setHandlePosition(20.0, 0.5, 0.0).ok());
    REQUIRE(placement->step(1, 1.0 / 60.0).ok());
    auto frame = placement->placementFrame();
    REQUIRE(frame.ok());
    CHECK(frame.value().objects[0].transform.x > 19.5);
}

TEST_CASE("scene.physicsPlacement.resolves_collision_and_clones_independently") {
    PhysicsPlacementRequest request;
    request.objects.push_back(placementBox("wall", 2.0, 0.5, 0, 0.5, 2.0, 2.0, false));
    request.objects.push_back(placementBox("crate", 0.0, 0.5, 0, 0.5, 0.5, 0.5, true));
    auto created = ScenePhysicsPlacementBackend::create(std::move(request));
    REQUIRE(created.ok());
    auto placement = std::move(created.value());
    REQUIRE(placement->setHandlePosition(4.0, 0.5, 0.0).ok());
    for (std::uint64_t tick = 1; tick <= 90; ++tick) REQUIRE(placement->step(tick, 1.0 / 60.0).ok());
    auto frame = placement->placementFrame();
    REQUIRE(frame.ok());
    CHECK(frame.value().objects[1].transform.x < 1.05);
    auto clone = placement->cloneForPreview();
    REQUIRE(clone != nullptr);
    auto clonedFrame = clone->capture();
    REQUIRE(clonedFrame.ok());
    CHECK(std::abs(clonedFrame.value()[0].positionX) < 0.001);
}

TEST_CASE("scene.physicsPlacement.drop_uses_gravity_and_reports_settled_contact") {
    PhysicsPlacementRequest request;
    request.settings.mode = PhysicsPlacementMode::Drop;
    request.objects.push_back(placementBox("floor", 0, -0.5, 0, 5, 0.5, 5, false));
    request.objects.push_back(placementBox("crate", 0, 3.0, 0, 0.5, 0.5, 0.5, true));
    auto created = ScenePhysicsPlacementBackend::create(std::move(request));
    REQUIRE(created.ok());
    auto                  placement = std::move(created.value());
    PhysicsPlacementFrame frame;
    for (std::uint64_t tick = 1; tick <= 240; ++tick) {
        REQUIRE(placement->step(tick, 1.0 / 60.0).ok());
        auto captured = placement->placementFrame();
        REQUIRE(captured.ok());
        frame = std::move(captured.value());
        if (frame.settled) break;
    }
    REQUIRE(frame.settled);
    REQUIRE(frame.colliding);
    CHECK(std::abs(frame.objects[1].transform.y - 0.5) < 0.06);
}

TEST_CASE("scene.physicsPlacement.rotates_a_multi_selection_as_one_rigid_layout") {
    PhysicsPlacementRequest request;
    request.objects.push_back(placementBox("left", -1.0, 0.5, 0, 0.25, 0.25, 0.25, true));
    request.objects.push_back(placementBox("right", 1.0, 0.5, 0, 0.25, 0.25, 0.25, true));
    auto created = ScenePhysicsPlacementBackend::create(std::move(request));
    REQUIRE(created.ok());
    auto placement = std::move(created.value());
    REQUIRE(placement->setHandleRotation(0.0, 0.0, 1.57079632679).ok());
    for (std::uint64_t tick = 1; tick <= 30; ++tick) REQUIRE(placement->step(tick, 1.0 / 60.0).ok());
    auto frame = placement->placementFrame();
    REQUIRE(frame.ok());
    CHECK(std::abs(frame.value().objects[0].transform.x) < 0.08);
    CHECK(std::abs(frame.value().objects[1].transform.x) < 0.08);
    CHECK(frame.value().objects[0].transform.y < -0.35);
    CHECK(frame.value().objects[1].transform.y > 1.35);
    CHECK(std::abs(frame.value().objects[0].transform.rotationZ - 1.57079632679) < 0.08);
}

TEST_CASE("scene.physicsPlacement.scales_layout_and_live_collision_shapes_from_the_primary_handle") {
    PhysicsPlacementRequest request;
    request.primaryObject  = eve::editing::ObjectId("right");
    auto left              = placementBox("left", -1.0, 0.5, 0, 0.25, 0.25, 0.25, true);
    auto right             = placementBox("right", 1.0, 0.5, 0, 0.25, 0.25, 0.25, true);
    right.transform.scaleX = right.transform.scaleY = right.transform.scaleZ = 2.0;
    request.objects                                                          = {left, right};
    auto created = ScenePhysicsPlacementBackend::create(std::move(request));
    REQUIRE(created.ok());
    auto placement = std::move(created.value());
    REQUIRE(placement->setHandleScale(4.0, 4.0, 4.0).ok());
    for (std::uint64_t tick = 1; tick <= 30; ++tick) REQUIRE(placement->step(tick, 1.0 / 60.0).ok());
    auto frame = placement->placementFrame();
    REQUIRE(frame.ok());
    CHECK(frame.value().objects[0].transform.x < -1.85);
    CHECK(frame.value().objects[1].transform.x > 1.85);
    CHECK(std::abs(frame.value().objects[0].transform.scaleX - 2.0) < 0.001);
    CHECK(std::abs(frame.value().objects[1].transform.scaleX - 4.0) < 0.001);
    CHECK_EQ(static_cast<int>(placement->setHandleScale(0.0, 1.0, 1.0).code()),
             static_cast<int>(eve::editing::Status::Rejected));
}

TEST_CASE("scene.physicsPlacement.supports_primitive_and_convex_preview_shapes") {
    PhysicsPlacementRequest request;
    auto                    sphere = placementBox("sphere", -3.0, 1.0, 0, 0.5, 0.5, 0.5, true);
    sphere.shape                   = PhysicsPlacementShape::Sphere;
    auto capsule                   = placementBox("capsule", 0.0, 1.0, 0, 0.4, 1.0, 0.4, false);
    capsule.shape                  = PhysicsPlacementShape::Capsule;
    auto hull                      = placementBox("hull", 3.0, 1.0, 0, 0.5, 0.5, 0.5, false);
    hull.shape                     = PhysicsPlacementShape::ConvexHull;
    hull.convexVertices            = {-0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.0f, 0.5f};
    request.objects                = {sphere, capsule, hull};
    auto created                   = ScenePhysicsPlacementBackend::create(std::move(request));
    REQUIRE(created.ok());
    REQUIRE(created.value()->step(1, 1.0 / 60.0).ok());
    auto frame = created.value()->placementFrame();
    REQUIRE(frame.ok());
    REQUIRE_EQ(frame.value().objects.size(), 3U);
}

TEST_CASE("scene.physicsPlacement.rejects_malformed_convex_preview_shape") {
    PhysicsPlacementRequest request;
    auto                    hull = placementBox("hull", 0.0, 0.0, 0.0, 0.5, 0.5, 0.5, true);
    hull.shape                   = PhysicsPlacementShape::ConvexHull;
    hull.convexVertices          = {0.0f, 0.0f, 0.0f};
    request.objects.push_back(std::move(hull));
    CHECK_EQ(static_cast<int>(ScenePhysicsPlacementBackend::create(std::move(request)).code()),
             static_cast<int>(eve::editing::Status::Rejected));
}

TEST_CASE("scene.physicsPlacement.aligns_selection_up_and_support_to_external_surface") {
    PhysicsPlacementRequest request;
    request.objects.push_back(placementBox("wall", 2.0, 0.5, 0.0, 0.5, 3.0, 3.0, false));
    request.objects.push_back(placementBox("crate", 0.0, 0.5, 0.0, 0.5, 0.5, 0.5, true));
    auto created = ScenePhysicsPlacementBackend::create(std::move(request));
    REQUIRE(created.ok());
    auto placement = std::move(created.value());
    REQUIRE(placement->alignHandleToSurface(5.0, 0.5, 0.0, -5.0, 0.5, 0.0, 0.02).ok());
    for (std::uint64_t tick = 1; tick <= 30; ++tick) REQUIRE(placement->step(tick, 1.0 / 60.0).ok());
    auto frame = placement->placementFrame();
    REQUIRE(frame.ok());
    REQUIRE(frame.value().surfaceAligned);
    CHECK(frame.value().surfaceNormalX > 0.99);
    CHECK(std::abs(frame.value().surfaceNormalY) < 0.01);
    CHECK(frame.value().objects[1].transform.x > 2.95);
    CHECK(std::abs(std::abs(frame.value().objects[1].transform.rotationZ) - 1.57079632679) < 0.08);
}

TEST_CASE("scene.physicsPlacement.builds_compound_colliders_and_honors_source_policy") {
    PhysicsPlacementRequest  request;
    auto                     object = placementBox("arch", 0.0, 0.0, 0.0, 1.5, 1.0, 0.5, true);
    PhysicsPlacementCollider left;
    left.source                        = PhysicsPlacementColliderSource::Existing;
    left.halfExtentX                   = 0.25;
    left.halfExtentY                   = 1.0;
    left.halfExtentZ                   = 0.5;
    left.localX                        = -1.0;
    PhysicsPlacementCollider right     = left;
    right.localX                       = 1.0;
    PhysicsPlacementCollider generated = left;
    generated.source                   = PhysicsPlacementColliderSource::Generated;
    generated.localX                   = 0.0;
    object.colliders                   = {left, right, generated};
    request.settings.colliderPolicy    = PhysicsPlacementColliderPolicy::ExistingOnly;
    request.objects.push_back(std::move(object));
    auto created = ScenePhysicsPlacementBackend::create(std::move(request));
    REQUIRE(created.ok());
    REQUIRE(created.value()->setHandleScale(2.0, 1.0, 1.0).ok());
    REQUIRE(created.value()->step(1, 1.0 / 60.0).ok());

    PhysicsPlacementRequest rejected;
    rejected.settings.colliderPolicy = PhysicsPlacementColliderPolicy::ExistingOnly;
    rejected.objects.push_back(placementBox("generated", 0, 0, 0, 1, 1, 1, true));
    CHECK_EQ(static_cast<int>(ScenePhysicsPlacementBackend::create(std::move(rejected)).code()),
             static_cast<int>(eve::editing::Status::Rejected));
}

TEST_CASE("scene.physicsPlacement.filters_background_but_rejects_filtered_selection") {
    PhysicsPlacementRequest request;
    auto                    ignored = placementBox("ignored", 5, 0, 0, 1, 1, 1, false);
    ignored.layerBits               = 2;
    request.objects.push_back(ignored);
    request.objects.push_back(placementBox("selected", 0, 0, 0, 1, 1, 1, true));
    request.admission.includedLayerBits = 1;
    auto created                        = ScenePhysicsPlacementBackend::create(request);
    REQUIRE(created.ok());
    auto frame = created.value()->placementFrame();
    REQUIRE(frame.ok());
    REQUIRE_EQ(frame.value().objects.size(), 1U);
    request.objects[1].locked = true;
    CHECK_EQ(static_cast<int>(ScenePhysicsPlacementBackend::create(std::move(request)).code()),
             static_cast<int>(eve::editing::Status::Rejected));
}

TEST_CASE("scene.physicsPlacement.rotate_and_point_are_distinct_tool_policies") {
    PhysicsPlacementRequest rotate;
    rotate.settings.mode = PhysicsPlacementMode::Rotate;
    rotate.objects.push_back(placementBox("left", -1, 0, 0, 0.25, 0.25, 0.25, true));
    rotate.objects.push_back(placementBox("right", 1, 0, 0, 0.25, 0.25, 0.25, true));
    auto rotated = ScenePhysicsPlacementBackend::create(std::move(rotate));
    REQUIRE(rotated.ok());
    REQUIRE(rotated.value()->setHandlePosition(100, 100, 100).ok());
    REQUIRE(rotated.value()->setHandleRotation(0, 0, 1.57079632679).ok());
    REQUIRE(rotated.value()->step(1, 1.0 / 60.0).ok());
    auto rotateFrame = rotated.value()->placementFrame();
    REQUIRE(rotateFrame.ok());
    CHECK(std::abs(rotateFrame.value().objects[0].transform.x) < 0.1);

    PhysicsPlacementRequest point;
    point.settings.mode = PhysicsPlacementMode::Point;
    point.objects.push_back(placementBox("pointer", 0, 0, 0, 0.25, 0.25, 0.25, true));
    auto pointed = ScenePhysicsPlacementBackend::create(std::move(point));
    REQUIRE(pointed.ok());
    REQUIRE(pointed.value()->setHandlePosition(5, 0, 0).ok());
    REQUIRE(pointed.value()->step(1, 1.0 / 60.0).ok());
    auto pointFrame = pointed.value()->placementFrame();
    REQUIRE(pointFrame.ok());
    CHECK(std::abs(pointFrame.value().objects[0].transform.x) < 0.01);
    CHECK(std::abs(pointFrame.value().objects[0].transform.rotationY) > 1.0);
}

TEST_CASE("scene.physicsPlacement_supports_vector_gravity_motion_locks_and_teleport_threshold") {
    PhysicsPlacementRequest falling;
    falling.settings.mode            = PhysicsPlacementMode::Fall;
    falling.settings.gravityX        = 4.0;
    falling.settings.gravityY        = 0.0;
    falling.settings.freezePositionX = true;
    falling.objects.push_back(placementBox("locked", 0, 0, 0, 0.25, 0.25, 0.25, true));
    auto locked = ScenePhysicsPlacementBackend::create(std::move(falling));
    REQUIRE(locked.ok());
    for (std::uint64_t tick = 1; tick <= 30; ++tick) REQUIRE(locked.value()->step(tick, 1.0 / 60.0).ok());
    auto lockedFrame = locked.value()->placementFrame();
    REQUIRE(lockedFrame.ok());
    CHECK(std::abs(lockedFrame.value().objects[0].transform.x) < 0.001);

    PhysicsPlacementRequest teleport;
    teleport.settings.teleportDistance = 2.0;
    teleport.objects.push_back(placementBox("crate", 0, 0, 0, 0.5, 0.5, 0.5, true));
    auto teleported = ScenePhysicsPlacementBackend::create(std::move(teleport));
    REQUIRE(teleported.ok());
    REQUIRE(teleported.value()->setHandlePosition(10, 0, 0).ok());
    REQUIRE(teleported.value()->step(1, 1.0 / 60.0).ok());
    auto teleportFrame = teleported.value()->placementFrame();
    REQUIRE(teleportFrame.ok());
    CHECK(std::abs(teleportFrame.value().objects[0].transform.x - 10.0) < 0.001);
}
}  // namespace test_scene_follow_player
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_scene_projected_overlap)
#include "PrivateTestScope.h"
#include <memory>
#include "common/Capability.h"
#include "common/SceneVisibilityEvidence.h"
#include "editing/EditingProperty.h"
#include "scene/editing/SceneAuthoringDocument.h"
#include "scene/editing/SceneRegions.h"
#include "scene/editor/SceneEditorSession.h"
#include "scene/editor/SceneVisualRelationEvidence.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// scene_projected_overlap.cpp
namespace test_scene_projected_overlap {
namespace {
using namespace eve::editing;
std::unique_ptr<eve::scene_editor::SceneEditorSession> makeOverlapSession() {
    auto target = std::make_unique<eve::scene_editor::SceneEditorSession>(
        std::make_unique<eve::scene_editing::SceneAuthoringDocumentTarget>("overlap"));
    for (const auto* id : {"a", "b"})
        REQUIRE(target
                    ->execute("scene.authoring.spawn.v1",
                              Value::Object{{"object", id}, {"source", Value::Object{{"kind", "box"}}}})
                    .ok());
    return target;
}
Value intent() {
    return Value::Object{{"id", "compose"}, {"type", "projected_overlap"},
                         {"subject", "a"},  {"reference", "b"},
                         {"minimum", 0.4},  {"maximum", 0.6}};
}
Value evidence(double fraction) {
    Value pair   = Value::Object{{"object", "a"},          {"status", "Measured"},
                                 {"fraction", fraction},   {"intersectionPixels", std::int64_t(fraction * 100)},
                                 {"referencePixels", 100}, {"subjectPixels", 100}};
    Value target = Value::Object{{"object", "b"},
                                 {"status", "AmbiguousDepth"},
                                 {"visibleFraction", 0.0},
                                 {"occluders", Value::Array{}},
                                 {"projectedOverlaps", Value::Array{pair}}};
    return Value::Object{{"targets", Value::Array{target}}};
}
}  // namespace
TEST_CASE("scene.projected_overlap.schema_v4_migration_roundtrip_delete_and_invalid_restore") {
    auto target                                       = makeOverlapSession();
    auto legacy                                       = target->snapshot();
    (*legacy.getIf<Value::Object>())["schemaVersion"] = 3;
    REQUIRE(target->execute("scene.snapshot.restore.v1", legacy).ok());
    REQUIRE_EQ(*target->snapshot().getIf<Value::Object>()->at("schemaVersion").getIf<std::int64_t>(), 4);
    REQUIRE(target->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{intent()}}}).ok());
    const auto saved = target->saveJson();
    REQUIRE(target->restoreJson(saved).ok());
    REQUIRE_EQ(target->saveJson(), saved);
    for (int version : {1, 2, 3}) {
        auto bad                                       = target->snapshot();
        (*bad.getIf<Value::Object>())["schemaVersion"] = version;
        REQUIRE(!target->execute("scene.snapshot.restore.v1", bad).ok());
        REQUIRE_EQ(target->saveJson(), saved);
    }
    auto bad                                   = intent();
    (*bad.getIf<Value::Object>())["tolerance"] = 0.1;
    REQUIRE(!target->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{bad}}}).ok());
    REQUIRE_EQ(target->saveJson(), saved);
    REQUIRE(target->execute("scene.object.delete.v1", Value::Object{{"object", "a"}}).ok());
    REQUIRE(target->snapshot().getIf<Value::Object>()->at("relations").getIf<Value::Array>()->empty());
    REQUIRE(target->undo().ok());
    REQUIRE_EQ(target->saveJson(), saved);
}
TEST_CASE("scene.projected_overlap.target_planning_includes_partner_and_reference_region") {
    auto target   = makeOverlapSession();
    auto revision = target->geometryRevision(ObjectId("b"));
    REQUIRE(revision.ok());
    auto relation                                = intent();
    (*relation.getIf<Value::Object>())["region"] = Value::Object{{"schemaId", "eve.scene.local-region"},
                                                                 {"schemaVersion", 1},
                                                                 {"kind", "local_box"},
                                                                 {"id", "lower"},
                                                                 {"geometryRevision", revision.value()},
                                                                 {"minimum", Value::Array{-0.5, -0.5, -0.6}},
                                                                 {"maximum", Value::Array{0.5, 0, 0.6}}};
    REQUIRE(target->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{relation}}}).ok());
    auto                                    snapshot = target->snapshot();
    eve::scene_editing::SceneDocumentTarget hierarchy("parse");
    auto restore = hierarchy.makeRestore(snapshot.getIf<Value::Object>()->at("hierarchy"));
    REQUIRE(restore.ok());
    REQUIRE(hierarchy.applyDomainOperation(restore.value()).ok());
    auto parsed = eve::scene_editing::parseSceneRelations(snapshot.getIf<Value::Object>()->at("relations"), hierarchy);
    REQUIRE(parsed.ok());
    auto planned = eve::scene_editing::sceneVisibilityTargets(parsed.value(), snapshot);
    REQUIRE(planned.ok());
    REQUIRE_EQ(planned.value().size(), size_t(2));
    REQUIRE_EQ(*planned.value()[0].getIf<std::string>(), std::string("a"));
    REQUIRE_EQ(*planned.value()[1].getIf<Value::Object>()->at("object").getIf<std::string>(), std::string("b"));
}
TEST_CASE("scene.projected_overlap_uses_pair_fraction_independent_of_visibility_and_rejects_bad_evidence") {
    eve::scene_editing::SceneRelation relation;
    relation.id        = "compose";
    relation.kind      = eve::scene_editing::SceneRelationKind::ProjectedOverlap;
    relation.subject   = ObjectId("a");
    relation.reference = ObjectId("b");
    relation.minimum   = 0.4;
    relation.maximum   = 0.6;
    eve::scene_editor::PhysicsPlacementFrame  frame;
    eve::scene_editor::PhysicsPlacementObject selected;
    selected.object   = ObjectId("unrelated");
    selected.selected = true;
    frame.objects     = {selected};
    auto physical     = eve::scene_editor::evaluateSceneRelations({relation}, frame);
    REQUIRE(physical.ok());
    for (double fraction : {0.0, 0.5, 1.0}) {
        auto report =
            eve::scene_editor::evaluateSceneVisualRelations({relation}, frame, evidence(fraction), physical.value());
        REQUIRE(report.ok());
        REQUIRE_EQ(*report.value().getIf<Value::Object>()->at("hardSatisfied").getIf<bool>(), fraction == 0.5);
    }
    auto bad = evidence(1.2);
    REQUIRE(!eve::scene_editor::evaluateSceneVisualRelations({relation}, frame, bad, physical.value()).ok());
    bad         = evidence(0.5);
    auto& pairs = *bad.getIf<Value::Object>()
                       ->at("targets")
                       .getIf<Value::Array>()
                       ->front()
                       .getIf<Value::Object>()
                       ->at("projectedOverlaps")
                       .getIf<Value::Array>();
    (*pairs.front().getIf<Value::Object>())["referencePixels"] = 0;
    REQUIRE(!eve::scene_editor::evaluateSceneVisualRelations({relation}, frame, bad, physical.value()).ok());
    (*pairs.front().getIf<Value::Object>())["referencePixels"] = 100;
    pairs.push_back(pairs.front());
    REQUIRE(!eve::scene_editor::evaluateSceneVisualRelations({relation}, frame, bad, physical.value()).ok());
    pairs.clear();
    auto missing = eve::scene_editor::evaluateSceneVisualRelations({relation}, frame, bad, physical.value());
    REQUIRE(missing.ok());
    REQUIRE(!*missing.value().getIf<Value::Object>()->at("hardSatisfied").getIf<bool>());
    relation.hard = false;
    auto soft     = eve::scene_editor::evaluateSceneVisualRelations({relation}, frame, bad, physical.value());
    REQUIRE(soft.ok());
    REQUIRE(*soft.value().getIf<Value::Object>()->at("hardSatisfied").getIf<bool>());
}

TEST_CASE("scene.projected_overlap.provider_absent_rejects_measure_and_commit_without_document_mutation") {
    auto* previous = eve::cap::query<eve::ISceneVisibilityEvidence>();
    if (previous) eve::cap::revoke<eve::ISceneVisibilityEvidence>(previous);
    auto target = makeOverlapSession();
    REQUIRE(target->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{intent()}}}).ok());
    eve::scene_editor::PhysicsPlacementRequest request;
    eve::scene_editor::PhysicsPlacementObject  a, b;
    a.object                  = ObjectId("a");
    a.selected                = true;
    b.object                  = ObjectId("b");
    request.objects           = {a, b};
    request.settings.gravityY = 0;
    REQUIRE(target->beginRelationPlacement(request).ok());
    const auto saved   = target->saveJson();
    auto       missing = target->prepareRelationVisibility("host", Value::Object{});
    REQUIRE(!missing.ok());
    REQUIRE_EQ(static_cast<int>(missing.code()), static_cast<int>(Status::Unsupported));
    REQUIRE(!target->checkRelationPlacement().ok());
    REQUIRE(!target->commitPhysicsPlacement().ok());
    REQUIRE_EQ(target->saveJson(), saved);
    REQUIRE(target->cancelPhysicsPlacement().ok());
    if (previous) eve::cap::provide<eve::ISceneVisibilityEvidence>(previous);
}
}  // namespace test_scene_projected_overlap
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_scene_regions)
#include "PrivateTestScope.h"
#include <memory>
#include "editing/EditingProperty.h"
#include "scene/editing/SceneAuthoringDocument.h"
#include "scene/editing/SceneRegions.h"
#include "scene/editor/SceneEditorSession.h"
#include "scene/editor/SceneVisualRelationEvidence.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// scene_regions.cpp
namespace test_scene_regions {
namespace {
using namespace eve::editing;
std::unique_ptr<eve::scene_editor::SceneEditorSession> makeRegionSession() {
    auto result = std::make_unique<eve::scene_editor::SceneEditorSession>(
        std::make_unique<eve::scene_editing::SceneAuthoringDocumentTarget>("regions"));
    REQUIRE(
        result
            ->execute("scene.authoring.spawn.v1",
                      Value::Object{
                          {"object", "house"},
                          {"source", Value::Object{{"kind", "box"}, {"seed", 7}, {"mesh_params", Value::Object{}}}}})
            .ok());
    REQUIRE(result
                ->execute("scene.authoring.spawn.v1",
                          Value::Object{{"object", "tree"}, {"source", Value::Object{{"kind", "box"}}}})
                .ok());
    return result;
}
Value region(const std::string& revision) {
    return Value::Object{{"schemaId", "eve.scene.local-region"},
                         {"schemaVersion", 1},
                         {"kind", "local_box"},
                         {"id", "window"},
                         {"geometryRevision", revision},
                         {"minimum", Value::Array{-0.4, 0.0, -0.6}},
                         {"maximum", Value::Array{0.4, 0.4, 0.6}}};
}
Value intent(const std::string& revision) {
    return Value::Object{{"id", "keep-window"},
                         {"type", "visible_fraction"},
                         {"subject", "house"},
                         {"minimum", 0.95},
                         {"region", region(revision)}};
}
}  // namespace
TEST_CASE("scene.regions.geometry_revision_excludes_trs_and_material_but_invalidates_recipe") {
    auto target   = makeRegionSession();
    auto revision = target->geometryRevision(ObjectId("house"));
    REQUIRE(revision.ok());
    auto source = target->snapshot().getIf<Value::Object>()->at("sources").getIf<Value::Object>()->at("house");
    (*source.getIf<Value::Object>())["tint"] = Value::Array{0.1, 0.2, 0.3};
    REQUIRE(
        target->execute("scene.authoring.sources.set.v1", Value::Object{{"object", "house"}, {"value", source}}).ok());
    REQUIRE(
        target
            ->execute("scene.transform.set.v1", Value::Object{{"object", "house"}, {"position", Value::Array{2, 3, 4}}})
            .ok());
    auto same = target->geometryRevision(ObjectId("house"));
    REQUIRE(same.ok());
    REQUIRE_EQ(same.value(), revision.value());
    const auto relation = intent(revision.value());
    REQUIRE(target->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{relation}}}).ok());
    auto hierarchy = eve::scene_editing::SceneDocumentTarget("check");
    auto snap      = target->snapshot();
    auto restore   = hierarchy.makeRestore(snap.getIf<Value::Object>()->at("hierarchy"));
    REQUIRE(restore.ok());
    REQUIRE(hierarchy.applyDomainOperation(restore.value()).ok());
    auto relations = eve::scene_editing::parseSceneRelations(snap.getIf<Value::Object>()->at("relations"), hierarchy);
    REQUIRE(relations.ok());
    REQUIRE(eve::scene_editing::sceneVisibilityTargets(relations.value(), snap).ok());
    (*source.getIf<Value::Object>())["seed"] = 8;
    REQUIRE(
        target->execute("scene.authoring.sources.set.v1", Value::Object{{"object", "house"}, {"value", source}}).ok());
    auto stale = eve::scene_editing::sceneVisibilityTargets(relations.value(), target->snapshot());
    REQUIRE(!stale.ok());
    REQUIRE_EQ(static_cast<int>(stale.code()), static_cast<int>(Status::Conflict));
    REQUIRE(target->undo().ok());
    REQUIRE(eve::scene_editing::sceneVisibilityTargets(relations.value(), target->snapshot()).ok());
}
TEST_CASE("scene.regions.roundtrip_delete_undo_and_v2_migration_preserve_intent") {
    auto target   = makeRegionSession();
    auto revision = target->geometryRevision(ObjectId("house"));
    REQUIRE(revision.ok());
    REQUIRE(target
                ->execute("scene.authoring.relations.set.v1",
                          Value::Object{{"value", Value::Array{intent(revision.value())}}})
                .ok());
    const auto saved = target->saveJson();
    REQUIRE(target->restoreJson(saved).ok());
    REQUIRE_EQ(target->saveJson(), saved);
    REQUIRE(target->execute("scene.object.delete.v1", Value::Object{{"object", "house"}}).ok());
    REQUIRE(target->snapshot().getIf<Value::Object>()->at("relations").getIf<Value::Array>()->empty());
    REQUIRE(target->undo().ok());
    REQUIRE_EQ(target->saveJson(), saved);
    auto legacy                                       = target->snapshot();
    (*legacy.getIf<Value::Object>())["schemaVersion"] = 2;
    REQUIRE(!target->execute("scene.snapshot.restore.v1", legacy).ok());
    REQUIRE_EQ(target->saveJson(), saved);
    auto& relation =
        *legacy.getIf<Value::Object>()->at("relations").getIf<Value::Array>()->front().getIf<Value::Object>();
    relation.erase("region");
    REQUIRE(target->execute("scene.snapshot.restore.v1", legacy).ok());
    REQUIRE_EQ(*target->snapshot().getIf<Value::Object>()->at("schemaVersion").getIf<std::int64_t>(), 4);
    REQUIRE(target->snapshot().getIf<Value::Object>()->at("relations") ==
            legacy.getIf<Value::Object>()->at("relations"));
}
TEST_CASE("scene.regions.invalid_bounds_unknown_fields_and_conflicting_identity_are_atomic") {
    auto target   = makeRegionSession();
    auto revision = target->geometryRevision(ObjectId("house"));
    REQUIRE(revision.ok());
    const auto saved = target->saveJson();
    for (auto bounds : {Value::Object{{"minimum", Value::Array{0, 0, 0}}, {"maximum", Value::Array{0, 1, 1}}},
                        Value::Object{{"kind", "screen_rectangle"}}, Value::Object{{"unknown", 1}},
                        Value::Object{{"schemaVersion", 99}}, Value::Object{{"geometryRevision", ""}}}) {
        auto  bad = intent(revision.value());
        auto& r   = *bad.getIf<Value::Object>()->at("region").getIf<Value::Object>();
        for (const auto& [key, value] : bounds) r[key] = value;
        REQUIRE(!target->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{bad}}}).ok());
        REQUIRE_EQ(target->saveJson(), saved);
    }
    auto a = intent(revision.value()), b = a;
    (*b.getIf<Value::Object>())["id"]                                           = "another";
    (*b.getIf<Value::Object>()->at("region").getIf<Value::Object>())["maximum"] = Value::Array{0.3, 0.4, 0.6};
    REQUIRE(!target->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{a, b}}}).ok());
    REQUIRE_EQ(target->saveJson(), saved);
}
TEST_CASE("scene.regions.evidence_is_matched_by_object_region_and_definition") {
    eve::scene_editing::SceneRelation relation;
    relation.id      = "window";
    relation.kind    = eve::scene_editing::SceneRelationKind::VisibleFraction;
    relation.subject = ObjectId("house");
    relation.minimum = 0.95;
    auto parsed      = eve::SceneLocalRegion::fromValue(toPresentationValue(region("revision")));
    REQUIRE(parsed.ok());
    relation.region = parsed.value();
    eve::scene_editor::PhysicsPlacementFrame  frame;
    eve::scene_editor::PhysicsPlacementObject tree;
    tree.object   = ObjectId("tree");
    tree.selected = true;
    frame.objects = {tree};
    auto physical = eve::scene_editor::evaluateSceneRelations({relation}, frame);
    REQUIRE(physical.ok());
    Value whole = Value::Object{
        {"object", "house"}, {"status", "Measured"}, {"visibleFraction", 1.0}, {"occluders", Value::Array{}}};
    Value window                                        = whole;
    (*window.getIf<Value::Object>())["region"]          = region("revision");
    (*window.getIf<Value::Object>())["visibleFraction"] = 0.2;
    Value evidence                                      = Value::Object{{"targets", Value::Array{whole, window}}};
    auto  measured = eve::scene_editor::evaluateSceneVisualRelations({relation}, frame, evidence, physical.value());
    REQUIRE(measured.ok());
    REQUIRE(!*measured.value().getIf<Value::Object>()->at("hardSatisfied").getIf<bool>());
    (*window.getIf<Value::Object>()->at("region").getIf<Value::Object>())["id"] = "other";
    evidence   = Value::Object{{"targets", Value::Array{whole, window}}};
    auto wrong = eve::scene_editor::evaluateSceneVisualRelations({relation}, frame, evidence, physical.value());
    REQUIRE(wrong.ok());
    REQUIRE_EQ(*wrong.value()
                    .getIf<Value::Object>()
                    ->at("relations")
                    .getIf<Value::Array>()
                    ->front()
                    .getIf<Value::Object>()
                    ->at("status")
                    .getIf<std::string>(),
               std::string("Unmeasurable"));
}
}  // namespace test_scene_regions
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_scene_relations)
#include "PrivateTestScope.h"
#include <memory>
#include "common/Capability.h"
#include "common/SceneVisibilityEvidence.h"
#include "scene/editing/SceneAuthoringDocument.h"
#include "scene/editor/SceneEditorSession.h"
#include "scene/editor/SceneRelationEvidence.h"
#include "scene/editor/SceneVisualRelationEvidence.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// scene_relations.cpp
namespace test_scene_relations {
namespace {
using namespace eve::editing;
using namespace eve::scene_editor;
std::unique_ptr<SceneEditorSession> relationSession() {
    auto session = std::make_unique<SceneEditorSession>(
        std::make_unique<eve::scene_editing::SceneAuthoringDocumentTarget>("relations"));
    for (const auto* id : {"floor", "crate", "wall"}) {
        auto created =
            session->execute("scene.object.create.v1",
                             Value::Object{{"object", id},
                                           {"position", Value::Array{std::string(id) == "wall" ? 10.0 : 0.0,
                                                                     std::string(id) == "floor" ? -0.5 : 3.0, 0.0}}});
        REQUIRE(created.ok());
    }
    return session;
}
Value relation(const char* id, const char* type, const char* reference) {
    return Value::Object{{"id", id},     {"type", type},     {"subject", "crate"}, {"reference", reference},
                         {"hard", true}, {"tolerance", 0.06}};
}
PhysicsPlacementRequest request() {
    PhysicsPlacementRequest request;
    request.settings.mode            = PhysicsPlacementMode::Fall;
    request.settings.freezeRotationX = request.settings.freezeRotationY = request.settings.freezeRotationZ = true;
    PhysicsPlacementObject floor, crate;
    floor.object      = ObjectId("floor");
    floor.locked      = true;
    floor.halfExtentX = floor.halfExtentZ = 5;
    floor.halfExtentY                     = 0.5;
    crate.object                          = ObjectId("crate");
    crate.selected                        = true;
    request.objects                       = {floor, crate};
    return request;
}
bool satisfied(const Value& report) {
    const auto& fields = *report.getIf<Value::Object>();
    REQUIRE_EQ(*fields.at("schemaId").getIf<std::string>(), std::string("eve.scene.relation-report"));
    REQUIRE_EQ(*fields.at("schemaVersion").getIf<std::int64_t>(), 1);
    return *fields.at("hardSatisfied").getIf<bool>();
}
const Value::Array& relations(const Value& snapshot) {
    return *snapshot.getIf<Value::Object>()->at("relations").getIf<Value::Array>();
}
}  // namespace

TEST_CASE("scene.relations.roundtrip_delete_undo_and_v1_migration_are_atomic") {
    auto session = relationSession();
    REQUIRE(session
                ->execute("scene.authoring.relations.set.v1",
                          Value::Object{{"value", Value::Array{relation("on-floor", "supported_on", "floor")}}})
                .ok());
    const auto saved = session->saveJson();
    REQUIRE(session->execute("scene.object.delete.v1", Value::Object{{"object", "crate"}}).ok());
    REQUIRE(relations(session->snapshot()).empty());
    REQUIRE(session->undo().ok());
    REQUIRE_EQ(session->saveJson(), saved);
    REQUIRE(session->redo().ok());
    REQUIRE(relations(session->snapshot()).empty());
    REQUIRE(session->restoreJson(saved).ok());
    REQUIRE_EQ(session->saveJson(), saved);
    auto  legacy = session->snapshot();
    auto& root   = *legacy.getIf<Value::Object>();
    root.erase("relations");
    root["schemaVersion"] = 1;
    REQUIRE(session->execute("scene.snapshot.restore.v1", legacy).ok());
    REQUIRE(relations(session->snapshot()).empty());
    REQUIRE_EQ(*session->snapshot().getIf<Value::Object>()->at("schemaVersion").getIf<std::int64_t>(), 4);
    REQUIRE(session->undo().ok());
    REQUIRE_EQ(session->saveJson(), saved);
}

TEST_CASE("scene.relations.reject_unknown_missing_and_duplicate_without_mutation") {
    auto       session = relationSession();
    const auto saved   = session->saveJson();
    for (auto bad : {relation("r", "unknown-type", "floor"), relation("r", "supported_on", "missing")}) {
        REQUIRE(
            !session->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{bad}}}).ok());
        REQUIRE_EQ(session->saveJson(), saved);
    }
    auto r = relation("r", "supported_on", "floor");
    REQUIRE(!session->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{r, r}}}).ok());
    (*r.getIf<Value::Object>())["tolerance"] = -1.0;
    REQUIRE(!session->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{r}}}).ok());
    REQUIRE_EQ(session->saveJson(), saved);
    SceneEditorSession plain("plain");
    REQUIRE_EQ(static_cast<int>(
                   plain.execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{}}}).code()),
               static_cast<int>(Status::Unsupported));
}

TEST_CASE("scene.relations.contact_evidence_distinguishes_support_penetration_and_unmeasurable") {
    eve::scene_editing::SceneRelation r;
    r.id        = "r";
    r.subject   = ObjectId("crate");
    r.reference = ObjectId("floor");
    PhysicsPlacementFrame frame;
    frame.tick             = 3;
    frame.contactsComplete = true;
    PhysicsPlacementObject crate, floor;
    crate.object   = r.subject;
    crate.selected = true;
    floor.object   = r.reference;
    frame.objects  = {crate, floor};
    frame.contacts.push_back({r.subject, r.reference, 0, 0, 0, 0, -1, 0, -0.005});
    auto good = evaluateSceneRelations({r}, frame);
    REQUIRE(good.ok());
    REQUIRE(satisfied(good.value()));
    frame.contacts[0].separation = -0.2;
    auto penetrating             = evaluateSceneRelations({r}, frame);
    REQUIRE(penetrating.ok());
    REQUIRE(!satisfied(penetrating.value()));
    frame.contacts[0].separation = 0;
    frame.contacts[0].normalY    = 0;
    auto side                    = evaluateSceneRelations({r}, frame);
    REQUIRE(side.ok());
    REQUIRE(!satisfied(side.value()));
    frame.contacts.clear();
    r.kind         = eve::scene_editing::SceneRelationKind::Nonpenetrating;
    auto separated = evaluateSceneRelations({r}, frame);
    REQUIRE(separated.ok());
    REQUIRE(satisfied(separated.value()));
    frame.contactsComplete = false;
    auto incomplete        = evaluateSceneRelations({r}, frame);
    REQUIRE(incomplete.ok());
    REQUIRE(!satisfied(incomplete.value()));
    frame.contactsComplete    = true;
    frame.objects[1].selected = true;
    auto disabledPair         = evaluateSceneRelations({r}, frame);
    REQUIRE(disabledPair.ok());
    REQUIRE(!satisfied(disabledPair.value()));
}

TEST_CASE("scene.relations.native_drop_preserves_preview_and_commits_with_locked_floor") {
    auto session = relationSession();
    REQUIRE(session
                ->execute("scene.authoring.relations.set.v1",
                          Value::Object{{"value", Value::Array{relation("on-floor", "supported_on", "floor")}}})
                .ok());
    const auto before = session->saveJson();
    REQUIRE(session->beginRelationPlacement(request()).ok());
    auto unstepped = session->checkRelationPlacement();
    REQUIRE(unstepped.ok());
    REQUIRE(!satisfied(unstepped.value()));
    REQUIRE(!session->commitPhysicsPlacement().ok());
    REQUIRE_EQ(session->saveJson(), before);
    for (int i = 0; i < 240; ++i) REQUIRE(session->updatePhysicsPlacement(0, 3, 0).ok());
    auto report = session->checkRelationPlacement();
    REQUIRE(report.ok());
    REQUIRE(satisfied(report.value()));
    REQUIRE_EQ(session->saveJson(), before);
    REQUIRE(session->commitPhysicsPlacement().ok());
    REQUIRE(session->saveJson() != before);
    REQUIRE(session->undo().ok());
    REQUIRE_EQ(session->saveJson(), before);
    REQUIRE(session->redo().ok());
}

TEST_CASE("scene.relations.reject_wrong_support_missing_participant_locked_selection_and_stale_preview") {
    auto session = relationSession();
    REQUIRE(session
                ->execute("scene.authoring.relations.set.v1",
                          Value::Object{{"value", Value::Array{relation("wrong-support", "supported_on", "wall")}}})
                .ok());
    REQUIRE(!session->beginRelationPlacement(request()).ok());
    auto                   candidate = request();
    PhysicsPlacementObject wall;
    wall.object = ObjectId("wall");
    wall.locked = true;
    candidate.objects.push_back(wall);
    candidate.objects[1].locked = true;
    REQUIRE(!session->beginRelationPlacement(candidate).ok());
    candidate.objects[1].locked = false;
    REQUIRE(session->beginRelationPlacement(candidate).ok());
    for (int i = 0; i < 200; ++i) REQUIRE(session->updatePhysicsPlacement(0, 3, 0).ok());
    auto report = session->checkRelationPlacement();
    REQUIRE(report.ok());
    REQUIRE(!satisfied(report.value()));
    REQUIRE(!session->commitPhysicsPlacement().ok());
    REQUIRE(session->cancelPhysicsPlacement().ok());
    REQUIRE(session->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{}}}).ok());
    REQUIRE(session->beginRelationPlacement(request()).ok());
    REQUIRE(session
                ->execute("scene.transform.set.v1",
                          Value::Object{{"object", "floor"}, {"position", Value::Array{0.0, -1.0, 0.0}}})
                .ok());
    REQUIRE_EQ(static_cast<int>(session->checkRelationPlacement().code()), static_cast<int>(Status::Conflict));
    REQUIRE_EQ(static_cast<int>(session->commitPhysicsPlacement().code()), static_cast<int>(Status::Conflict));
    REQUIRE(session->cancelPhysicsPlacement().ok());
}

TEST_CASE("scene.relations.physics_roundtrip_preserves_scene_yxz_rotation") {
    auto session = relationSession();
    REQUIRE(session
                ->execute("scene.transform.set.v1",
                          Value::Object{{"object", "crate"}, {"rotation", Value::Array{0.31, 0.52, -0.23}}})
                .ok());
    REQUIRE(session->beginRelationPlacement(request()).ok());
    auto frame = session->updatePhysicsPlacement(0, 3, 0);
    REQUIRE(frame.ok());
    const auto& rotation = frame.value().objects[1].transform;
    CHECK(std::abs(rotation.rotationX - 0.31) < 1e-5);
    CHECK(std::abs(rotation.rotationY - 0.52) < 1e-5);
    CHECK(std::abs(rotation.rotationZ + 0.23) < 1e-5);
    REQUIRE(session->commitPhysicsPlacement().ok());
    const auto  snapshot  = session->snapshot();
    const auto& hierarchy = *snapshot.getIf<Value::Object>()->at("hierarchy").getIf<Value::Object>();
    for (const auto& object : *hierarchy.at("objects").getIf<Value::Array>()) {
        const auto& fields = *object.getIf<Value::Object>();
        if (*fields.at("id").getIf<std::string>() != "crate") continue;
        const auto& t = *fields.at("transform").getIf<Value::Object>();
        CHECK(std::abs(*t.at("rotationX").getIf<double>() - 0.31) < 1e-5);
        CHECK(std::abs(*t.at("rotationY").getIf<double>() - 0.52) < 1e-5);
        CHECK(std::abs(*t.at("rotationZ").getIf<double>() + 0.23) < 1e-5);
    }
}

TEST_CASE("scene.relations.visual_roundtrip_delete_and_invalid_intervals_are_atomic") {
    auto  session = relationSession();
    Value visible =
        Value::Object{{"id", "keep-visible"}, {"type", "visible_fraction"}, {"subject", "crate"}, {"minimum", 0.75}};
    Value occludes = Value::Object{{"id", "cover"},       {"type", "occludes"}, {"subject", "crate"},
                                   {"reference", "wall"}, {"minimum", 0.2},     {"maximum", 0.5}};
    REQUIRE(
        session->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{visible, occludes}}})
            .ok());
    const auto saved = session->saveJson();
    REQUIRE(session->restoreJson(saved).ok());
    REQUIRE_EQ(session->saveJson(), saved);
    REQUIRE(session->execute("scene.object.delete.v1", Value::Object{{"object", "crate"}}).ok());
    REQUIRE(relations(session->snapshot()).empty());
    REQUIRE(session->undo().ok());
    REQUIRE_EQ(session->saveJson(), saved);
    for (const auto& fields :
         {Value::Object{{"minimum", 0.8}, {"maximum", 0.4}}, Value::Object{{"minimum", -0.1}},
          Value::Object{{"minimum", 1.1}}, Value::Object{{"reference", "floor"}}, Value::Object{{"tolerance", 0.01}}}) {
        auto bad = visible;
        for (const auto& [key, value] : fields) (*bad.getIf<Value::Object>())[key] = value;
        REQUIRE(
            !session->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{bad}}}).ok());
        REQUIRE_EQ(session->saveJson(), saved);
    }
}
TEST_CASE("scene.relations.visual_evidence_uses_directional_occluder_and_rejects_ambiguity") {
    eve::scene_editing::SceneRelation visible, cover;
    visible.id      = "visible";
    visible.kind    = eve::scene_editing::SceneRelationKind::VisibleFraction;
    visible.subject = ObjectId("crate");
    visible.minimum = 0.75;
    cover.id        = "cover";
    cover.kind      = eve::scene_editing::SceneRelationKind::Occludes;
    cover.subject   = ObjectId("crate");
    cover.reference = ObjectId("wall");
    cover.minimum   = 0.2;
    cover.maximum   = 0.3;
    PhysicsPlacementFrame  frame;
    PhysicsPlacementObject crate;
    crate.object   = visible.subject;
    crate.selected = true;
    frame.objects  = {crate};
    Value::Array targets;
    targets.emplace_back(Value::Object{
        {"object", "crate"}, {"status", "Measured"}, {"visibleFraction", 0.8}, {"occluders", Value::Array{}}});
    targets.emplace_back(
        Value::Object{{"object", "wall"},
                      {"status", "Measured"},
                      {"visibleFraction", 0.5},
                      {"occluders", Value::Array{Value::Object{{"object", "crate"}, {"fraction", 0.25}}}}});
    Value                                                evidence = Value::Object{{"targets", std::move(targets)}};
    const std::vector<eve::scene_editing::SceneRelation> intents{visible, cover};
    auto                                                 physical = evaluateSceneRelations(intents, frame);
    REQUIRE(physical.ok());
    auto measured = evaluateSceneVisualRelations(intents, frame, evidence, physical.value());
    REQUIRE(measured.ok());
    REQUIRE(satisfied(measured.value()));
    auto& target = *(*evidence.getIf<Value::Object>()->at("targets").getIf<Value::Array>())[1].getIf<Value::Object>();
    target["occluders"] = Value::Array{Value::Object{{"object", "floor"}, {"fraction", 0.25}}};
    auto unrelated      = evaluateSceneVisualRelations(intents, frame, evidence, physical.value());
    REQUIRE(unrelated.ok());
    REQUIRE(!satisfied(unrelated.value()));
    target["status"] = "AmbiguousDepth";
    auto ambiguous   = evaluateSceneVisualRelations(intents, frame, evidence, physical.value());
    REQUIRE(ambiguous.ok());
    REQUIRE(!satisfied(ambiguous.value()));
}
TEST_CASE("scene.relations.visual_provider_absent_cannot_commit_but_physics_still_works") {
    auto* previous = eve::cap::query<eve::ISceneVisibilityEvidence>();
    if (previous) eve::cap::revoke<eve::ISceneVisibilityEvidence>(previous);
    auto session = relationSession();
    REQUIRE(session
                ->execute("scene.authoring.relations.set.v1",
                          Value::Object{{"value", Value::Array{Value::Object{{"id", "visible"},
                                                                             {"type", "visible_fraction"},
                                                                             {"subject", "crate"},
                                                                             {"minimum", 0.5}}}}})
                .ok());
    REQUIRE(session->beginRelationPlacement(request()).ok());
    REQUIRE(!session->prepareRelationVisibility("host", Value::Object{}).ok());
    const auto saved = session->saveJson();
    REQUIRE(!session->commitPhysicsPlacement().ok());
    REQUIRE_EQ(session->saveJson(), saved);
    REQUIRE(session->physicsPlacementActive());
    REQUIRE(session->cancelPhysicsPlacement().ok());
    REQUIRE(session->execute("scene.authoring.relations.set.v1", Value::Object{{"value", Value::Array{}}}).ok());
    REQUIRE(session->beginRelationPlacement(request()).ok());
    auto report = session->checkRelationPlacement();
    REQUIRE(report.ok());
    REQUIRE(satisfied(report.value()));
    REQUIRE(session->cancelPhysicsPlacement().ok());
    if (previous) eve::cap::provide<eve::ISceneVisibilityEvidence>(previous);
}

TEST_CASE("scene.relations.moving_unrelated_occluder_still_checks_all_visual_intent") {
    eve::scene_editing::SceneRelation visible;
    visible.id      = "visible";
    visible.kind    = eve::scene_editing::SceneRelationKind::VisibleFraction;
    visible.subject = ObjectId("target");
    visible.minimum = 0.8;
    PhysicsPlacementFrame  frame;
    PhysicsPlacementObject blocker;
    blocker.object   = ObjectId("blocker");
    blocker.selected = true;
    frame.objects    = {blocker};
    Value evidence   = Value::Object{
          {"targets",
           Value::Array{Value::Object{
               {"object", "target"}, {"status", "Measured"}, {"visibleFraction", 0.2}, {"occluders", Value::Array{}}}}}};
    auto physical = evaluateSceneRelations({visible}, frame);
    REQUIRE(physical.ok());
    auto evaluated = evaluateSceneVisualRelations({visible}, frame, evidence, physical.value());
    REQUIRE(evaluated.ok());
    REQUIRE(!satisfied(evaluated.value()));
    visible.hard = false;
    auto soft    = evaluateSceneVisualRelations({visible}, frame, evidence, physical.value());
    REQUIRE(soft.ok());
    REQUIRE(satisfied(soft.value()));
    const auto& item =
        *soft.value().getIf<Value::Object>()->at("relations").getIf<Value::Array>()->front().getIf<Value::Object>();
    REQUIRE_EQ(*item.at("status").getIf<std::string>(), std::string("Violated"));
}

TEST_CASE("scene.relations.malformed_visual_provider_evidence_returns_failure") {
    PhysicsPlacementFrame  frame;
    PhysicsPlacementObject object;
    object.object   = ObjectId("target");
    object.selected = true;
    frame.objects   = {object};
    eve::scene_editing::SceneRelation visible;
    visible.kind    = eve::scene_editing::SceneRelationKind::VisibleFraction;
    visible.subject = object.object;
    auto report     = evaluateSceneRelations({visible}, frame);
    REQUIRE(report.ok());
    for (const auto& broken :
         {Value::Object{{"object", "target"}},
          Value::Object{
              {"object", "target"}, {"status", "Measured"}, {"visibleFraction", 2.0}, {"occluders", Value::Array{}}},
          Value::Object{{"object", "target"},
                        {"status", "Measured"},
                        {"visibleFraction", 1.0},
                        {"occluders", Value::Array{Value::Object{{"object", "blocker"}}}}}}) {
        auto bad = evaluateSceneVisualRelations({visible}, frame, Value::Object{{"targets", Value::Array{broken}}},
                                                report.value());
        REQUIRE(!bad.ok());
    }
}
}  // namespace test_scene_relations
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_schema_production_integration)
#include "PrivateTestScope.h"
#include "definitions/Definitions.h"
#include "policyregistry/PolicyRegistry.h"
#include "schema/SchemaRegistry.h"
#include "zeroerr/unittest.h"
// schema_production_integration.cpp
namespace test_schema_production_integration {
namespace {

eve::schema::SchemaDefinition objectSchema(std::string id) {
    eve::schema::SchemaDefinition schema;
    schema.id                   = std::move(id);
    schema.version              = 1;
    schema.additionalProperties = false;
    eve::schema::FieldDefinition enabled;
    enabled.name     = "enabled";
    enabled.type     = eve::schema::ValueType::Boolean;
    enabled.required = true;
    schema.fields.push_back(std::move(enabled));
    return schema;
}

}  // namespace

TEST_CASE("schema.productionBoundary.validatesDefinitionsBeforeMutation") {
    eve::schema::SchemaRegistry::clear();
    auto schemaRegistration = eve::schema::SchemaRegistry::registerVersioned(objectSchema("ability"));
    REQUIRE(schemaRegistration.ok());
    CHECK_EQ(schemaRegistration.value(), eve::schema::SchemaRegistrationStatus::Registered);

    eve::definitions::DefinitionRegistry registry;
    auto                                 invalid = registry.insert("ability", "dash", 1, R"({"enabled":"yes"})");
    CHECK(!invalid.ok());
    CHECK_EQ(registry.size(), 0);
    invalid.ignore("expected schema rejection");

    auto valid = registry.insert("ability", "dash", 1, R"({"enabled":true})");
    REQUIRE(valid.ok());
    CHECK_EQ(registry.size(), 1);
}

TEST_CASE("schema.productionBoundary.validatesPolicyMetadataBeforeMutation") {
    eve::schema::SchemaRegistry::clear();
    auto schemaRegistration = eve::schema::SchemaRegistry::registerVersioned(objectSchema("policy:movement"));
    REQUIRE(schemaRegistration.ok());
    CHECK_EQ(schemaRegistration.value(), eve::schema::SchemaRegistrationStatus::Registered);

    eve::policyregistry::PolicyRegistry registry;
    auto invalid = registry.insert("movement", "ground", 1, 0, true, "builtin", "policy:movement", R"({"enabled":1})");
    CHECK(!invalid.ok());
    CHECK_EQ(registry.size(), 0);
    invalid.ignore("expected schema rejection");

    auto valid = registry.insert("movement", "ground", 1, 0, true, "builtin", "policy:movement", R"({"enabled":true})");
    REQUIRE(valid.ok());
    CHECK_EQ(registry.size(), 1);
}
}  // namespace test_schema_production_integration
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_settlement_replay_composition)
#include "PrivateTestScope.h"
#include <array>
#include <string_view>
#include "card/CardEffects.h"
#include "combat/Damage.h"
#include "rpg/RPGActor.h"
#include "rpg/SettlementAdapter.h"
#include "settlement/SettlementRules.h"
#include "tactics/TacticsSettlement.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// settlement_replay_composition.cpp
namespace test_settlement_replay_composition {
namespace {

eve::SubjectRef subject(std::uint8_t suffix) {
    eve::PersistentId::Bytes bytes{};
    bytes[15] = suffix;
    return eve::SubjectRef::fromPersistentId(eve::PersistentId(bytes));
}

eve::SnapshotHashProvider replayHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        eve::ContentId::Bytes bytes{};
        std::uint64_t         hash = 14695981039346656037ull;
        for (const unsigned char byte : input) {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        for (std::size_t index = 0; index < bytes.size(); ++index)
            bytes[index] = static_cast<std::uint8_t>(hash >> ((index % 8) * 8));
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

eve::rpg::RPGActor* fighter(double health) {
    auto* actor = eve::rpg::RPGActor::createActor();
    actor->setBaseAttribute("hp", 100.0);
    actor->setCurrent("hp", health);
    return actor;
}

eve::SimulationStep step(std::uint64_t tick, double seconds) {
    auto delta = eve::Duration::fromSeconds(seconds);
    REQUIRE(delta.ok());
    return {eve::SimulationTick(tick), std::move(delta).takeValue()};
}

eve::effects::EffectPolicy oncePolicy() {
    eve::effects::EffectPolicy policy;
    policy.stackMode  = eve::effects::StackMode::NewInstance;
    policy.stackCount = eve::effects::StackCountPolicy::Keep;
    policy.maxStacks  = 1;
    return policy;
}

class TacticalHealthPolicy final : public eve::settlement::ISettlementPolicy {
public:
    explicit TacticalHealthPolicy(double& health, bool failCommit = false) : health_(health), failCommit_(failCommit) {}
    eve::Result<void> validate(eve::settlement::SettlementContext&) override { return eve::Result<void>::success(); }
    eve::Result<void> sourceModifiers(eve::settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> targetMitigation(eve::settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> armorShield(eve::settlement::SettlementContext&) override { return eve::Result<void>::success(); }
    eve::Result<void> clamp(eve::settlement::SettlementContext& context) override {
        return context.setClampMax(health_);
    }
    eve::Result<eve::settlement::PreparedApply> prepareApply(
        const eve::settlement::SettlementContext& context) override {
        const double before = health_;
        const double after  = before - context.magnitude();
        return eve::Result<eve::settlement::PreparedApply>::success(eve::settlement::PreparedApply(
            [this, after]() {
                health_ = after;
                if (failCommit_)
                    return eve::Result<void>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected tactics commit failure"));
                return eve::Result<void>::success();
            },
            [this, before]() { health_ = before; }));
    }

private:
    double& health_;
    bool    failCommit_ = false;
};

eve::settlement::SettlementRuleSet lifestealRules() {
    eve::settlement::SettlementRule rule;
    rule.id           = "rpg.replay.lifesteal";
    rule.source       = "test:vampiric";
    rule.stage        = eve::settlement::StageKind::Trigger;
    rule.operation    = eve::settlement::RuleOperation::Lifesteal;
    rule.value        = 0.5;
    rule.filter.kinds = {"damage"};
    eve::settlement::SettlementRuleSet rules;
    rules.configure({std::move(rule)}).ignore("test fixture rule configuration");
    return rules;
}

}  // namespace

TEST_CASE("settlement.replay.rpgLifestealChainReplaysThroughDomainAdapter") {
    const auto attackerRef = subject(41);
    const auto targetRef   = subject(42);
    auto       rules       = lifestealRules();
    auto       ruleDigest  = rules.digest(replayHash());
    REQUIRE(ruleDigest.ok());

    eve::settlement::SettlementPipeline pipeline;
    REQUIRE(rules.install(pipeline).ok());
    auto*      attacker = fighter(50.0);
    auto*      target   = fighter(100.0);
    const auto execute  = [&](const eve::settlement::SettlementRequest& request) {
        auto*                          targetActor = request.target == attackerRef ? attacker : target;
        auto*                          sourceActor = request.source == attackerRef ? attacker : target;
        eve::rpg::RPGSettlementAdapter policy(*targetActor, request.target, sourceActor, request.source);
        return pipeline.settle(request, policy);
    };

    eve::settlement::SettlementRequest root;
    root.source    = attackerRef;
    root.target    = targetRef;
    root.kind      = "damage";
    root.resource  = "hp";
    root.magnitude = 20.0;
    root.tick      = eve::SimulationTick(9);
    auto original  = pipeline.settleChain(root, execute, 4, 8);
    REQUIRE(original.ok());
    REQUIRE_EQ(original.value().size(), 2u);
    REQUIRE(original.value()[0].result.has_value());
    REQUIRE(original.value()[1].result.has_value());
    CHECK_EQ(target->getCurrent("hp"), 80.0);
    CHECK_EQ(attacker->getCurrent("hp"), 60.0);

    std::array<std::string, 2> records;
    for (std::size_t index = 0; index < records.size(); ++index) {
        auto record = eve::settlement::createSettlementReplayRecord(original.value()[index].request, ruleDigest.value(),
                                                                    *original.value()[index].result, replayHash());
        REQUIRE(record.ok());
        records[index] = std::move(record).takeValue();
    }
    attacker->release();
    target->release();

    attacker        = fighter(50.0);
    target          = fighter(100.0);
    auto replayRoot = eve::settlement::settlementReplayRequest(records[0]);
    REQUIRE(replayRoot.ok());
    auto replayed = pipeline.settleChain(replayRoot.value(), execute, 4, 8);
    REQUIRE(replayed.ok());
    REQUIRE_EQ(replayed.value().size(), 2u);
    for (std::size_t index = 0; index < records.size(); ++index) {
        REQUIRE(replayed.value()[index].result.has_value());
        auto verified = eve::settlement::verifySettlementReplayRecord(records[index], ruleDigest.value(),
                                                                      *replayed.value()[index].result, replayHash());
        REQUIRE(verified.ok());
        CHECK(verified.value().empty());
    }
    CHECK_EQ(target->getCurrent("hp"), 80.0);
    CHECK_EQ(attacker->getCurrent("hp"), 60.0);
    attacker->release();
    target->release();
}

TEST_CASE("settlement.replay.rtsDamageBatchRetainsAndVerifiesCanonicalAudits") {
    eve::settlement::SettlementRule fortified;
    fortified.id           = "rts.replay.fortified";
    fortified.source       = "test:fortification";
    fortified.stage        = eve::settlement::StageKind::TargetMitigation;
    fortified.operation    = eve::settlement::RuleOperation::ResistPercent;
    fortified.value        = 0.25;
    fortified.filter.kinds = {"damage"};
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({fortified}).ok());
    auto ruleDigest = rules.digest(replayHash());
    REQUIRE(ruleDigest.ok());

    eve::combat::DamageRuntime runtime;
    REQUIRE(runtime.configureSettlementRules(rules).ok());
    std::array<eve::combat::CombatState, 2>   targets;
    std::array<eve::combat::DamageRequest, 2> requests;
    for (std::size_t index = 0; index < targets.size(); ++index) {
        targets[index].subject = subject(static_cast<std::uint8_t>(50 + index));
        targets[index].health = targets[index].maxHealth = 100.0;
        targets[index].poise = targets[index].maxPoise = 20.0;
        requests[index].source                         = subject(49);
        requests[index].target                         = targets[index].subject;
        requests[index].damageType                     = "damage.physical";
        requests[index].healthDamage                   = 20.0 + static_cast<double>(index) * 4.0;
    }

    std::array<std::string, 2> records;
    for (std::size_t index = 0; index < targets.size(); ++index) {
        auto outcome = runtime.apply(targets[index], requests[index]);
        REQUIRE(outcome.ok());
        CHECK_EQ(outcome.value().settlementRequest.target, targets[index].subject);
        CHECK_EQ(outcome.value().settlementResult.applied, requests[index].healthDamage * 0.75);
        auto record = eve::settlement::createSettlementReplayRecord(
            outcome.value().settlementRequest, ruleDigest.value(), outcome.value().settlementResult, replayHash());
        REQUIRE(record.ok());
        records[index] = std::move(record).takeValue();
    }

    std::array<eve::combat::CombatState, 2> replayTargets;
    for (std::size_t index = 0; index < replayTargets.size(); ++index) {
        replayTargets[index].subject = targets[index].subject;
        replayTargets[index].health = replayTargets[index].maxHealth = 100.0;
        replayTargets[index].poise = replayTargets[index].maxPoise = 20.0;
        auto restoredRequest = eve::settlement::settlementReplayRequest(records[index]);
        REQUIRE(restoredRequest.ok());
        CHECK_EQ(restoredRequest.value().magnitude, requests[index].healthDamage);
        auto replayed = runtime.apply(replayTargets[index], requests[index]);
        REQUIRE(replayed.ok());
        auto verified = eve::settlement::verifySettlementReplayRecord(records[index], ruleDigest.value(),
                                                                      replayed.value().settlementResult, replayHash());
        REQUIRE(verified.ok());
        CHECK(verified.value().empty());
    }
}

TEST_CASE("settlement.replay.cardPeriodicRestoreRetainsAndVerifiesCanonicalAudit") {
    eve::settlement::SettlementRule resistance;
    resistance.id           = "card.replay.resistance";
    resistance.source       = "test:ward";
    resistance.stage        = eve::settlement::StageKind::TargetMitigation;
    resistance.operation    = eve::settlement::RuleOperation::ResistPercent;
    resistance.value        = 0.2;
    resistance.filter.kinds = {"damage"};
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({resistance}).ok());
    auto ruleDigest = rules.digest(replayHash());
    REQUIRE(ruleDigest.ok());

    eve::card::CardEffectAdapter card;
    REQUIRE(card.configureSettlementRules(rules).ok());
    eve::card::CardEffectDefinition damage;
    damage.id        = "card.replay.periodic";
    damage.source    = "test:spell";
    damage.duration  = 2.0;
    damage.period    = 1.0;
    damage.magnitude = 10.0;
    damage.policy    = oncePolicy();
    REQUIRE(card.apply(damage, subject(60)).ok());
    const auto before = card.snapshot();

    auto original = card.advance(step(1, 1.0));
    REQUIRE(original.ok());
    REQUIRE_EQ(original.value().settlements.size(), 1u);
    REQUIRE(original.value().settlements[0].result.has_value());
    CHECK_EQ(card.target().health, 92);
    auto record =
        eve::settlement::createSettlementReplayRecord(original.value().settlements[0].request, ruleDigest.value(),
                                                      *original.value().settlements[0].result, replayHash());
    REQUIRE(record.ok());

    REQUIRE(card.restore(before).ok());
    auto replayed = card.advance(step(1, 1.0));
    REQUIRE(replayed.ok());
    REQUIRE_EQ(replayed.value().settlements.size(), 1u);
    REQUIRE(replayed.value().settlements[0].result.has_value());
    auto verified = eve::settlement::verifySettlementReplayRecord(
        record.value(), ruleDigest.value(), *replayed.value().settlements[0].result, replayHash());
    REQUIRE(verified.ok());
    CHECK(verified.value().empty());
    CHECK_EQ(card.target().health, 92);
}

TEST_CASE("settlement.replay.cardProjectionFailureDoesNotCommitLifecycleCandidate") {
    eve::card::CardEffectAdapter    card;
    eve::card::CardEffectDefinition damage;
    damage.id        = "card.failure.periodic";
    damage.duration  = 3.0;
    damage.period    = 1.0;
    damage.magnitude = 10.0;
    damage.policy    = oncePolicy();
    REQUIRE(card.apply(damage, subject(63)).ok());
    const auto clean = card.snapshot();

    auto                           malformed = clean;
    eve::effects::EffectDefinition invalidRule;
    invalidRule.id       = "card.failure.invalid-rule";
    invalidRule.stackKey = invalidRule.id;
    invalidRule.duration = 3.0;
    invalidRule.policy   = oncePolicy();
    REQUIRE(invalidRule.payload.setJson("settlement.rule", "42").ok());
    REQUIRE(malformed.effects.apply(invalidRule, subject(63).format()).ok());
    REQUIRE(card.restore(malformed).ok());

    const auto before = card.snapshot();
    auto       failed = card.advance(step(1, 1.0));
    CHECK(!failed.ok());
    const auto after = card.snapshot();
    CHECK_EQ(after.target.health, before.target.health);
    CHECK_EQ(after.effects.effectCount(), before.effects.effectCount());
    for (int index = 0; index < before.effects.effectCount(); ++index) {
        REQUIRE(before.effects.effectAt(index) != nullptr);
        REQUIRE(after.effects.effectAt(index) != nullptr);
        CHECK_EQ(after.effects.effectAt(index)->id, before.effects.effectAt(index)->id);
        CHECK_EQ(after.effects.effectAt(index)->remaining, before.effects.effectAt(index)->remaining);
        CHECK_EQ(after.effects.effectAt(index)->periodElapsed, before.effects.effectAt(index)->periodElapsed);
    }

    REQUIRE(card.restore(clean).ok());
    auto retried = card.advance(step(1, 1.0));
    REQUIRE(retried.ok());
    CHECK_EQ(card.target().health, 90);
}

TEST_CASE("settlement.replay.tacticsCanonicalProjectionReplaysThroughDomainAdapter") {
    eve::settlement::SettlementRule highGround;
    highGround.id                  = "tactics.replay.high-ground";
    highGround.source              = "test:board";
    highGround.stage               = eve::settlement::StageKind::SourceModifiers;
    highGround.operation           = eve::settlement::RuleOperation::Multiply;
    highGround.value               = 1.5;
    highGround.filter.kinds        = {"damage"};
    highGround.filter.requiredTags = {"tactics:ability"};
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({highGround}).ok());
    auto ruleDigest = rules.digest(replayHash());
    REQUIRE(ruleDigest.ok());

    eve::tactics::TacticsSettlementRuntime runtime;
    REQUIRE(runtime.configureSettlementRules(rules).ok());
    eve::tactics::AbilitySettlementRequest request;
    request.ability.actor      = subject(61);
    request.ability.targetUnit = subject(62);
    request.kind               = "damage";
    request.magnitude          = 20.0;
    request.tick               = eve::SimulationTick(12);
    request.context            = eve::Value(eve::Value::Object{{"range", 2.0}});
    auto canonical             = eve::tactics::makeSettlementRequest(request);
    REQUIRE(canonical.ok());

    double               health = 100.0;
    TacticalHealthPolicy policy(health);
    auto                 original = runtime.settle(request, policy);
    REQUIRE(original.ok());
    CHECK_EQ(health, 70.0);
    auto record = eve::settlement::createSettlementReplayRecord(canonical.value(), ruleDigest.value(), original.value(),
                                                                replayHash());
    REQUIRE(record.ok());
    auto restoredRequest = eve::settlement::settlementReplayRequest(record.value());
    REQUIRE(restoredRequest.ok());
    CHECK_EQ(restoredRequest.value().source, canonical.value().source);
    CHECK_EQ(restoredRequest.value().target, canonical.value().target);
    CHECK_EQ(restoredRequest.value().tags, canonical.value().tags);

    health        = 100.0;
    auto replayed = runtime.settle(request, policy);
    REQUIRE(replayed.ok());
    auto verified = eve::settlement::verifySettlementReplayRecord(record.value(), ruleDigest.value(), replayed.value(),
                                                                  replayHash());
    REQUIRE(verified.ok());
    CHECK(verified.value().empty());
    CHECK_EQ(health, 70.0);
}

TEST_CASE("settlement.replay.tacticsCommitFailureRollsBackDomainState") {
    eve::tactics::TacticsSettlementRuntime runtime;
    eve::tactics::AbilitySettlementRequest request;
    request.ability.actor      = subject(64);
    request.ability.targetUnit = subject(65);
    request.kind               = "damage";
    request.magnitude          = 25.0;
    request.tick               = eve::SimulationTick(13);

    double               health = 100.0;
    TacticalHealthPolicy policy(health, true);
    auto                 failed = runtime.settle(request, policy);
    CHECK(!failed.ok());
    CHECK_EQ(health, 100.0);
}
}  // namespace test_settlement_replay_composition
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_snapshot_consumers)
#include "PrivateTestScope.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include "GameEventTestSupport.h"
#include "StatePatchTestSupport.h"
#include "authority/Authority.h"
#include "common/Snapshot.h"
#include "definitions/Definitions.h"
#include "game_event/GameEvent.h"
#include "production/Production.h"
#include "statepatch/StatePatch.h"
#include "transaction/Transaction.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// snapshot_consumers.cpp
namespace test_snapshot_consumers {
namespace {

using eve::ContentId;
using eve::DiagnosticCode;
using eve::LogicalId;
using eve::PersistentId;
using eve::Result;
using eve::SchemaVersion;
using eve::SimulationTick;
using eve::SnapshotEnvelope;
using eve::SnapshotHashProvider;
using eve::Value;

PersistentId persistentId(std::string_view text) {
    const auto parsed = PersistentId::parse(text);
    return parsed ? *parsed : PersistentId::nil();
}

LogicalId logicalId(std::string_view text) {
    const auto parsed = LogicalId::parse(text);
    return parsed ? *parsed : LogicalId{};
}

SnapshotHashProvider testHashProvider() {
    return [](std::string_view input) -> Result<ContentId> {
        std::uint64_t left  = 14695981039346656037ull;
        std::uint64_t right = 1099511628211ull;
        for (const unsigned char byte : input) {
            left ^= byte;
            left *= 1099511628211ull;
            right ^= static_cast<std::uint64_t>(byte) + 0x9e3779b97f4a7c15ull;
            right *= 14029467366897019727ull;
        }
        ContentId::Bytes bytes{};
        for (int index = 0; index < 8; ++index) {
            bytes[static_cast<std::size_t>(index)]     = static_cast<std::uint8_t>(left >> (56 - index * 8));
            bytes[static_cast<std::size_t>(index + 8)] = static_cast<std::uint8_t>(right >> (56 - index * 8));
        }
        return Result<ContentId>::success(ContentId(bytes));
    };
}

SnapshotEnvelope reseal(const SnapshotEnvelope& source, SchemaVersion version, Value payload,
                        const SnapshotHashProvider& hash) {
    auto result = eve::makeSnapshotEnvelope(source.type, source.schema, version, source.instanceId, source.revision,
                                            source.tick, std::move(payload), hash);
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}

Value withPayloadMetadata(const Value& payload, std::string field, std::uint64_t value) {
    Value::Object object;
    if (const auto* existing = payload.getIf<Value::Object>()) object = *existing;
    object[std::move(field)] = Value(std::to_string(value));
    return Value(std::move(object));
}

}  // namespace

TEST_CASE("snapshot.consumers.eventStreamEnvelopeRoundTripAndGeneratorSurvives") {
    const auto                    hash = testHashProvider();
    const auto                    id   = persistentId("11111111-2222-4333-8444-555555555555");
    eve::game_event::GameEventLog stream(id);
    auto                          appended = stream.append(
        eve::game_event::test::envelope(1, "test.event", "test", "subject", "", "", 5, 1, "{\"value\":1}"));
    REQUIRE(appended.ok());
    CHECK_EQ(appended.value().value(), uint64_t{1});

    auto captured = stream.snapshot(hash);
    REQUIRE(captured.ok());
    CHECK_EQ(captured.value().type, std::string("game_event.stream"));
    CHECK_EQ(captured.value().instanceId, id);
    CHECK_EQ(captured.value().revision, eve::Revision(1));

    eve::game_event::GameEventLog restored(id);
    REQUIRE(restored.restoreSnapshot(captured.value(), hash).ok());
    CHECK_EQ(restored.snapshotJson(), stream.snapshotJson());

    auto                          old = reseal(captured.value(), SchemaVersion(0), captured.value().payload, hash);
    eve::game_event::GameEventLog migrated(id);
    REQUIRE(migrated.restoreSnapshot(old, hash).ok());
    CHECK_EQ(migrated.snapshotJson(), stream.snapshotJson());

    auto              newer   = reseal(captured.value(), SchemaVersion(2), captured.value().payload, hash);
    const std::string before  = restored.snapshotJson();
    auto              unknown = restored.restoreSnapshot(newer, hash);
    CHECK(!unknown.ok());
    CHECK_EQ(static_cast<int>(unknown.error()->code()), static_cast<int>(DiagnosticCode::UnknownVersion));
    CHECK_EQ(restored.snapshotJson(), before);

    auto mismatched        = captured.value();
    mismatched.contentHash = ContentId::nil();
    auto mismatch          = restored.restoreSnapshot(mismatched, hash);
    CHECK(!mismatch.ok());
    CHECK_EQ(static_cast<int>(mismatch.error()->code()), static_cast<int>(DiagnosticCode::HashMismatch));
    CHECK_EQ(restored.snapshotJson(), before);

    auto duplicateRevision =
        reseal(captured.value(), SchemaVersion(1),
               withPayloadMetadata(captured.value().payload, "revision", captured.value().revision.value() + 1), hash);
    auto duplicateFailure = restored.restoreSnapshot(duplicateRevision, hash);
    CHECK(!duplicateFailure.ok());
    CHECK_EQ(static_cast<int>(duplicateFailure.error()->code()), static_cast<int>(DiagnosticCode::Conflict));
    CHECK_EQ(restored.snapshotJson(), before);
}

TEST_CASE("snapshot.consumers.transactionLedgerEnvelopeMigratesAtomically") {
    const auto               hash = testHashProvider();
    const auto               id   = persistentId("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
    eve::transaction::Ledger ledger(id);
    auto                     created =
        ledger.create("chain", "command", *eve::TransactionId::parse("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee01"));
    REQUIRE(created.ok());
    auto* plan = created.value();
    auto  staged =
        plan->stage("set", "target", "{\"value\":1}", *eve::OperationId::parse("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee02"));
    REQUIRE(staged.ok());
    const auto operation = staged.value();
    REQUIRE(plan->markValid(operation).ok());
    REQUIRE(plan->validate().ok());
    REQUIRE(plan->commit().ok());

    auto captured = ledger.snapshot(hash);
    REQUIRE(captured.ok());
    eve::transaction::Ledger restored(id);
    REQUIRE(restored.restoreSnapshot(captured.value(), hash).ok());
    CHECK_EQ(restored.snapshotJson(), ledger.snapshotJson());

    auto                     old = reseal(captured.value(), SchemaVersion(0), captured.value().payload, hash);
    eve::transaction::Ledger migrated(id);
    REQUIRE(migrated.restoreSnapshot(old, hash).ok());
    CHECK_EQ(migrated.snapshotJson(), ledger.snapshotJson());

    const std::string before = restored.snapshotJson();
    Value::Object     malformedObject;
    malformedObject.emplace("version", Value(std::int64_t(1)));
    auto malformed = reseal(captured.value(), SchemaVersion(1), Value(std::move(malformedObject)), hash);
    auto failure   = restored.restoreSnapshot(malformed, hash);
    CHECK(!failure.ok());
    CHECK_EQ(restored.snapshotJson(), before);

    auto duplicateTick =
        reseal(captured.value(), SchemaVersion(1),
               withPayloadMetadata(captured.value().payload, "tick", captured.value().tick.value() + 1), hash);
    auto duplicateFailure = restored.restoreSnapshot(duplicateTick, hash);
    CHECK(!duplicateFailure.ok());
    CHECK_EQ(static_cast<int>(duplicateFailure.error()->code()), static_cast<int>(DiagnosticCode::Conflict));
    CHECK_EQ(restored.snapshotJson(), before);
}

TEST_CASE("snapshot.consumers.statePatchEnvelopeRejectsHashAndRevisionMismatch") {
    const auto             hash = testHashProvider();
    const auto             id   = persistentId("12345678-1234-4123-8123-123456789012");
    eve::statepatch::Store store(id);
    auto                   batch = eve::test_support::openStatePatchBatch(store);
    REQUIRE(batch.view.isBound());
    REQUIRE(batch.view->set("actor", "value", "1"));
    REQUIRE(store.commit(batch.view.get()).ok());

    auto captured = store.snapshot(hash);
    REQUIRE(captured.ok());
    eve::statepatch::Store restored(id);
    REQUIRE(restored.restoreSnapshot(captured.value(), hash).ok());
    CHECK_EQ(restored.snapshotJson(), store.snapshotJson());

    auto                   old = reseal(captured.value(), SchemaVersion(0), captured.value().payload, hash);
    eve::statepatch::Store migrated(id);
    REQUIRE(migrated.restoreSnapshot(old, hash).ok());
    CHECK_EQ(migrated.snapshotJson(), store.snapshotJson());

    const std::string before   = restored.snapshotJson();
    auto              mismatch = captured.value();
    mismatch.contentHash       = ContentId::nil();
    auto hashFailure           = restored.restoreSnapshot(mismatch, hash);
    CHECK(!hashFailure.ok());
    CHECK_EQ(static_cast<int>(hashFailure.error()->code()), static_cast<int>(DiagnosticCode::HashMismatch));
    CHECK_EQ(restored.snapshotJson(), before);

    auto badRevision     = captured.value();
    badRevision.revision = eve::Revision(captured.value().revision.value() + 1);
    auto badRevisionHash = eve::makeSnapshotEnvelope(badRevision.type, badRevision.schema, badRevision.schemaVersion,
                                                     badRevision.instanceId, badRevision.revision, badRevision.tick,
                                                     badRevision.payload, hash);
    REQUIRE(badRevisionHash.ok());
    auto revisionFailure = restored.restoreSnapshot(badRevisionHash.value(), hash);
    CHECK(!revisionFailure.ok());
    CHECK_EQ(restored.snapshotJson(), before);

    auto duplicateTick =
        reseal(captured.value(), SchemaVersion(1),
               withPayloadMetadata(captured.value().payload, "tick", captured.value().tick.value() + 1), hash);
    auto duplicateFailure = restored.restoreSnapshot(duplicateTick, hash);
    CHECK(!duplicateFailure.ok());
    CHECK_EQ(static_cast<int>(duplicateFailure.error()->code()), static_cast<int>(DiagnosticCode::Conflict));
    CHECK_EQ(restored.snapshotJson(), before);
}

TEST_CASE("snapshot.consumers.productionEnvelopeRoundTripAndUnknownVersionIsAtomic") {
    const auto hash         = testHashProvider();
    const auto id           = persistentId("99999999-8888-4777-8666-555555555555");
    auto       snapshotJson = [](const eve::production::WorkQueue& queue) {
        auto result = queue.snapshot();
        return std::move(result).expect("production snapshot must serialize");
    };
    eve::production::WorkQueue queue(id);
    auto                       taskIdResult =
        queue.enqueue("factory", "smelt", "iron", eve::Value(eve::Value::Object{{"ore", eve::Value(1)}}), 2.0);
    REQUIRE(taskIdResult.ok());

    auto captured = queue.snapshot(hash);
    REQUIRE(captured.ok());
    eve::production::WorkQueue restored(id);
    REQUIRE(restored.restoreSnapshot(captured.value(), hash).ok());
    CHECK_EQ(snapshotJson(restored), snapshotJson(queue));

    auto                       old = reseal(captured.value(), SchemaVersion(0), captured.value().payload, hash);
    eve::production::WorkQueue migrated(id);
    REQUIRE(migrated.restoreSnapshot(old, hash).ok());
    CHECK_EQ(snapshotJson(migrated), snapshotJson(queue));

    auto              newer   = reseal(captured.value(), SchemaVersion(8), captured.value().payload, hash);
    const std::string before  = snapshotJson(restored);
    auto              unknown = restored.restoreSnapshot(newer, hash);
    CHECK(!unknown.ok());
    CHECK_EQ(static_cast<int>(unknown.error()->code()), static_cast<int>(DiagnosticCode::UnknownVersion));
    CHECK_EQ(snapshotJson(restored), before);

    auto duplicateRevision =
        reseal(captured.value(), SchemaVersion(7),
               withPayloadMetadata(captured.value().payload, "revision", captured.value().revision.value() + 1), hash);
    auto duplicateFailure = restored.restoreSnapshot(duplicateRevision, hash);
    CHECK(!duplicateFailure.ok());
    CHECK_EQ(static_cast<int>(duplicateFailure.error()->code()), static_cast<int>(DiagnosticCode::Conflict));
    CHECK_EQ(snapshotJson(restored), before);
}

TEST_CASE("snapshot.consumers.authorityAndDefinitionsRejectDuplicateMetadataAtomically") {
    const auto hash = testHashProvider();

    const auto            authorityId = persistentId("11111111-2222-4333-8444-555555555555");
    eve::authority::Store authority(authorityId);
    REQUIRE(authority.grant("actor", "scope", "read", "test").ok());
    auto authoritySnapshot = authority.snapshot(hash);
    REQUIRE(authoritySnapshot.ok());
    eve::authority::Store authorityRestored(authorityId);
    REQUIRE(authorityRestored.restoreSnapshot(authoritySnapshot.value(), hash).ok());
    const std::string authorityBefore   = authorityRestored.snapshotJson();
    auto              authorityConflict = reseal(authoritySnapshot.value(), SchemaVersion(1),
                                                 withPayloadMetadata(authoritySnapshot.value().payload, "revision",
                                                                     authoritySnapshot.value().revision.value() + 1),
                                                 hash);
    auto              authorityFailure  = authorityRestored.restoreSnapshot(authorityConflict, hash);
    CHECK(!authorityFailure.ok());
    CHECK_EQ(static_cast<int>(authorityFailure.error()->code()), static_cast<int>(DiagnosticCode::Conflict));
    CHECK_EQ(authorityRestored.snapshotJson(), authorityBefore);

    const auto                           definitionsId = persistentId("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
    eve::definitions::DefinitionRegistry definitions(definitionsId);
    REQUIRE(definitions.insert("unit", "tank", 1, "{\"speed\":4}").ok());
    auto definitionsSnapshot = definitions.snapshot(hash);
    REQUIRE(definitionsSnapshot.ok());
    eve::definitions::DefinitionRegistry definitionsRestored(definitionsId);
    REQUIRE(definitionsRestored.restoreSnapshot(definitionsSnapshot.value(), hash).ok());
    const std::string definitionsBefore   = definitionsRestored.snapshotJson();
    auto              definitionsConflict = reseal(
        definitionsSnapshot.value(), SchemaVersion(1),
        withPayloadMetadata(definitionsSnapshot.value().payload, "tick", definitionsSnapshot.value().tick.value() + 1),
        hash);
    auto definitionsFailure = definitionsRestored.restoreSnapshot(definitionsConflict, hash);
    CHECK(!definitionsFailure.ok());
    CHECK_EQ(static_cast<int>(definitionsFailure.error()->code()), static_cast<int>(DiagnosticCode::Conflict));
    CHECK_EQ(definitionsRestored.snapshotJson(), definitionsBefore);
}
}  // namespace test_snapshot_consumers
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_snapshot_envelope)
#include "PrivateTestScope.h"
#include <cstdint>
#include <string_view>
#include <utility>
#include "authority/Authority.h"
#include "common/Snapshot.h"
#include "definitions/Definitions.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// snapshot_envelope.cpp
namespace test_snapshot_envelope {
using eve::ContentId;
using eve::DiagnosticCode;
using eve::LogicalId;
using eve::PersistentId;
using eve::Result;
using eve::SchemaVersion;
using eve::SimulationTick;
using eve::SnapshotEnvelope;
using eve::SnapshotHashProvider;
using eve::SnapshotMigrationChain;
using eve::Value;

namespace {

PersistentId persistentId(std::string_view text) {
    const auto parsed = PersistentId::parse(text);
    return parsed ? *parsed : PersistentId::nil();
}

LogicalId logicalId(std::string_view text) {
    const auto parsed = LogicalId::parse(text);
    return parsed ? *parsed : LogicalId{};
}

/** Test-only deterministic non-cryptographic digest provider. */
SnapshotHashProvider testHashProvider() {
    return [](std::string_view input) -> Result<ContentId> {
        std::uint64_t left  = 14695981039346656037ull;
        std::uint64_t right = 1099511628211ull;
        for (const unsigned char byte : input) {
            left ^= byte;
            left *= 1099511628211ull;
            right ^= static_cast<std::uint64_t>(byte) + 0x9e3779b97f4a7c15ull;
            right *= 14029467366897019727ull;
        }
        ContentId::Bytes bytes{};
        for (int index = 0; index < 8; ++index) {
            bytes[static_cast<std::size_t>(index)]     = static_cast<std::uint8_t>(left >> (56 - index * 8));
            bytes[static_cast<std::size_t>(index + 8)] = static_cast<std::uint8_t>(right >> (56 - index * 8));
        }
        return Result<ContentId>::success(ContentId(bytes));
    };
}

SnapshotEnvelope makeEnvelope(std::string type, LogicalId schema, SchemaVersion version, PersistentId instanceId,
                              Value payload, const SnapshotHashProvider& hashProvider) {
    auto result = eve::makeSnapshotEnvelope(std::move(type), std::move(schema), version, instanceId, eve::Revision(7),
                                            SimulationTick(11), std::move(payload), hashProvider);
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}

}  // namespace

TEST_CASE("snapshot.envelope.canonicalHashRoundTripAndNoWallClock") {
    const auto    hash = testHashProvider();
    Value::Object payloadObject;
    payloadObject.emplace("z", Value(std::int64_t(2)));
    payloadObject.emplace("a", Value("stable"));
    const auto envelope =
        makeEnvelope("test.snapshot", logicalId("test:payload"), SchemaVersion(1),
                     persistentId("01234567-89ab-4cde-8fab-0123456789ab"), Value(std::move(payloadObject)), hash);

    auto hashInput = eve::snapshotHashInput(envelope);
    REQUIRE(hashInput.ok());
    CHECK(hashInput.value().find("createdAt") == std::string::npos);
    CHECK(hashInput.value().find("wallClock") == std::string::npos);
    CHECK(hashInput.value().find("\"a\"") < hashInput.value().find("\"z\""));

    auto serialized = eve::serializeSnapshotEnvelope(envelope);
    REQUIRE(serialized.ok());
    auto parsed = eve::parseSnapshotEnvelope(std::string_view(serialized.value()), hash);
    REQUIRE(parsed.ok());
    CHECK_EQ(parsed.value().type, envelope.type);
    CHECK_EQ(parsed.value().schema, envelope.schema);
    CHECK_EQ(parsed.value().schemaVersion, envelope.schemaVersion);
    CHECK_EQ(parsed.value().instanceId, envelope.instanceId);
    CHECK_EQ(parsed.value().revision, envelope.revision);
    CHECK_EQ(parsed.value().tick, envelope.tick);
    CHECK_EQ(parsed.value().contentHash, envelope.contentHash);
    CHECK_EQ(parsed.value().payload, envelope.payload);
}

TEST_CASE("snapshot.migration.rejectsUnknownNewVersionAndHashMismatch") {
    const auto             hash   = testHashProvider();
    const auto             schema = logicalId("test:payload");
    SnapshotMigrationChain chain;
    auto                   registration =
        chain.add(schema, SchemaVersion(0), SchemaVersion(1), [](const Value& payload) -> Result<Value> {
            const auto* object = payload.getIf<Value::Object>();
            if (!object)
                return Result<Value>::failure(
                    eve::Diagnostic::error(DiagnosticCode::ParseError, "payload must be an object"));
            Value::Object migrated = *object;
            migrated["version"]    = Value(std::int64_t(1));
            return Result<Value>::success(Value(std::move(migrated)));
        });
    REQUIRE(registration.ok());

    Value::Object oldPayload;
    oldPayload.emplace("version", Value(std::int64_t(0)));
    auto old      = makeEnvelope("test.snapshot", schema, SchemaVersion(0), PersistentId::nil(),
                                 Value(std::move(oldPayload)), hash);
    auto migrated = chain.migrate(std::move(old), SchemaVersion(1), hash);
    REQUIRE(migrated.ok());
    CHECK_EQ(migrated.value().schemaVersion, SchemaVersion(1));
    CHECK_EQ(migrated.value().payload.getIf<Value::Object>()->at("version"), Value(std::int64_t(1)));

    Value::Object newPayload;
    newPayload.emplace("version", Value(std::int64_t(2)));
    auto newer    = makeEnvelope("test.snapshot", schema, SchemaVersion(2), PersistentId::nil(),
                                 Value(std::move(newPayload)), hash);
    auto rejected = chain.migrate(std::move(newer), SchemaVersion(1), hash);
    CHECK(!rejected.ok());
    CHECK_EQ(static_cast<int>(rejected.code()), static_cast<int>(eve::StatusCode::Failed));
    CHECK_EQ(static_cast<int>(rejected.error()->code()), static_cast<int>(DiagnosticCode::UnknownVersion));

    auto valid =
        makeEnvelope("test.snapshot", schema, SchemaVersion(1), PersistentId::nil(), Value(Value::Object{}), hash);
    valid.contentHash = ContentId::nil();
    auto mismatch     = eve::verifySnapshotEnvelope(valid, hash);
    CHECK(!mismatch.ok());
    CHECK_EQ(static_cast<int>(mismatch.error()->code()), static_cast<int>(DiagnosticCode::HashMismatch));
}

TEST_CASE("snapshot.consumers.authorityAndDefinitionsRoundTripAndMetadata") {
    const auto            hash        = testHashProvider();
    const auto            authorityId = persistentId("11111111-2222-4333-8444-555555555555");
    eve::authority::Store authority(authorityId);
    auto                  grantResult = authority.grant("actor", "scope", "read", "test", 3);
    REQUIRE(grantResult.ok());
    const auto ruleId = std::move(grantResult).takeValue();
    REQUIRE(!ruleId.empty());
    auto authoritySnapshot = authority.snapshot(hash);
    REQUIRE(authoritySnapshot.ok());
    CHECK_EQ(authoritySnapshot.value().instanceId, authorityId);
    CHECK_EQ(authoritySnapshot.value().revision, eve::Revision(1));

    eve::authority::Store authorityCopy(authorityId);
    auto                  authorityRestored = authorityCopy.restoreSnapshot(authoritySnapshot.value(), hash);
    REQUIRE(authorityRestored.ok());
    CHECK(authorityCopy.can("actor", "scope", "read"));
    CHECK_EQ(authorityCopy.snapshot(hash).value().contentHash, authoritySnapshot.value().contentHash);

    const auto                           definitionsId = persistentId("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
    eve::definitions::DefinitionRegistry definitions(definitionsId);
    REQUIRE(definitions.insert("unit", "tank", 1, "{\"speed\":4}").ok());
    auto definitionsSnapshot = definitions.snapshot(hash);
    REQUIRE(definitionsSnapshot.ok());
    CHECK_EQ(definitionsSnapshot.value().instanceId, definitionsId);

    eve::definitions::DefinitionRegistry definitionsCopy(definitionsId);
    auto definitionsRestored = definitionsCopy.restoreSnapshot(definitionsSnapshot.value(), hash);
    REQUIRE(definitionsRestored.ok());
    auto tank = definitionsCopy.resolve("unit", "tank");
    REQUIRE(tank.ok());
    CHECK_EQ(tank.value().get().json, std::string("{\"speed\":4}"));
}

TEST_CASE("snapshot.consumers.malformedPayloadAndUnknownVersionLeaveStateUnchanged") {
    const auto            hash        = testHashProvider();
    const auto            authorityId = persistentId("99999999-8888-4777-8666-555555555555");
    eve::authority::Store authority(authorityId);
    REQUIRE(authority.grant("actor", "scope", "read", "before").ok());
    const std::string authorityBefore = authority.snapshotJson();

    Value::Object malformedAuthority;
    malformedAuthority.emplace("version", Value(std::int64_t(1)));
    auto malformedAuthorityEnvelope = makeEnvelope("authority.store", logicalId("authority:store"), SchemaVersion(1),
                                                   authorityId, Value(std::move(malformedAuthority)), hash);
    auto authorityFailure           = authority.restoreSnapshot(malformedAuthorityEnvelope, hash);
    CHECK(!authorityFailure.ok());
    CHECK_EQ(authority.snapshotJson(), authorityBefore);

    Value::Object newerAuthority;
    newerAuthority.emplace("version", Value(std::int64_t(2)));
    auto newerAuthorityEnvelope = makeEnvelope("authority.store", logicalId("authority:store"), SchemaVersion(2),
                                               authorityId, Value(std::move(newerAuthority)), hash);
    auto unknownAuthority       = authority.restoreSnapshot(newerAuthorityEnvelope, hash);
    CHECK(!unknownAuthority.ok());
    CHECK_EQ(static_cast<int>(unknownAuthority.error()->code()), static_cast<int>(DiagnosticCode::UnknownVersion));
    CHECK_EQ(authority.snapshotJson(), authorityBefore);

    const auto                           definitionsId = persistentId("12345678-1234-4123-8123-123456789012");
    eve::definitions::DefinitionRegistry definitions(definitionsId);
    REQUIRE(definitions.insert("unit", "before", 1, "{}").ok());
    const std::string definitionsBefore = definitions.snapshotJson();
    Value::Object     malformedDefinitions;
    malformedDefinitions.emplace("version", Value(std::int64_t(1)));
    auto malformedDefinitionsEnvelope =
        makeEnvelope("definitions.registry", logicalId("definitions:registry"), SchemaVersion(1), definitionsId,
                     Value(std::move(malformedDefinitions)), hash);
    auto definitionsFailure = definitions.restoreSnapshot(malformedDefinitionsEnvelope, hash);
    CHECK(!definitionsFailure.ok());
    CHECK_EQ(definitions.snapshotJson(), definitionsBefore);
}
}  // namespace test_snapshot_envelope
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_softbody_volume)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <cmath>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include "asset/CanonicalMesh.h"
#include "graphics/Camera.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem3D.h"
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "physics/softbody/SoftBody3D.h"
#include "physics/softbody/SoftBody3DDefinition.h"
#include "physics/softbody/SoftBodyCollision.h"
#include "physics/softbody/SoftBodyModelDefinition.h"
#include "physics/softbody/cook/SoftBodyModelCooker.h"
#include "physics/softbody/editing/SoftBody3DSchema.h"
#include "physics/softbody/graphics/SoftBody3DRenderer.h"
#include "schema/SchemaRegistry.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// softbody_volume.cpp
namespace test_softbody_volume {
using eve::physics::SoftBody3D;

namespace {

class PlaneCollisionProvider final : public eve::physics::softbody::ISoftBodyCollisionWorld {
public:
    [[nodiscard]] bool softBodyCollisionAvailable() const noexcept override { return available; }

    [[nodiscard]] eve::physics::softbody::SoftBodyContactState probeSoftBodyParticle(
        float, float y, float, float radius, eve::physics::softbody::SoftBodyContact& contact) const override {
        ++probeCount;
        contact           = {};
        const float depth = planeY + radius - y;
        if (depth <= 0.f) return eve::physics::softbody::SoftBodyContactState::None;
        contact.hit         = true;
        contact.ny          = 1.f;
        contact.depth       = depth;
        contact.bodyId      = 17;
        contact.dynamicBody = true;
        return eve::physics::softbody::SoftBodyContactState::Hit;
    }

    void applySoftBodyImpulse(int bodyId, float, float y, float) override {
        if (bodyId == 17) {
            ++impulseCount;
            accumulatedImpulseY += y;
        }
    }

    bool        available           = true;
    float       planeY              = 0.f;
    mutable int probeCount          = 0;
    int         impulseCount        = 0;
    float       accumulatedImpulseY = 0.f;
};

}  // namespace

TEST_CASE("softbody.volume.creationRejectsInvalidLattice") {
    auto invalid = SoftBody3D::create(1, 3, 3, 0.5f, 0.f, 0.f, 0.f);
    REQUIRE(!invalid);
    REQUIRE_EQ(invalid.status().code(), eve::StatusCode::Rejected);
}

TEST_CASE("softbody.volume.definitionRoundTripsAndCreatesRuntime") {
    eve::physics::SoftBody3DDefinition definition;
    definition.cols                  = 3;
    definition.rows                  = 4;
    definition.layers                = 5;
    definition.gravityY              = -4.5f;
    definition.deformationResistance = 0.67f;
    definition.iterations            = 7;
    definition.selfCollision         = true;
    auto       encoded               = definition.toValue();
    const bool encodedOk             = encoded.ok();
    REQUIRE(encodedOk);
    auto       decoded   = eve::physics::SoftBody3DDefinition::fromValue(encoded.value());
    const bool decodedOk = decoded.ok();
    REQUIRE(decodedOk);
    REQUIRE_EQ(decoded.value().cols, 3);
    REQUIRE_EQ(decoded.value().layers, 5);
    REQUIRE_EQ(decoded.value().iterations, 7);
    REQUIRE(decoded.value().selfCollision);
    auto       created   = SoftBody3D::create(decoded.value());
    const bool createdOk = created.ok();
    REQUIRE(createdOk);
    REQUIRE_EQ(created.value()->getParticleCount(), 60);
    REQUIRE_EQ(created.value()->getIterations(), 7);
    REQUIRE(created.value()->getSelfCollision());
    const auto* registered =
        eve::schema::SchemaRegistry::resolve(std::string(eve::physics::SoftBody3DDefinition::SchemaId),
                                             static_cast<int>(eve::physics::SoftBody3DDefinition::SchemaVersion));
    REQUIRE(registered != nullptr);
    auto       json   = encoded.value().toJson();
    const bool jsonOk = json.ok();
    REQUIRE(jsonOk);
    REQUIRE(eve::schema::SchemaRegistry::validate(registered->id, registered->version, json.value()).empty());
}

TEST_CASE("softbody.volume.definitionRejectsUnknownFieldsAndVersions") {
    eve::physics::SoftBody3DDefinition definition;
    auto                               encoded   = definition.toValue();
    const bool                         encodedOk = encoded.ok();
    REQUIRE(encodedOk);
    auto unknown                                          = encoded.value();
    (*unknown.getIf<eve::Value::Object>())["futureField"] = true;
    REQUIRE(!eve::physics::SoftBody3DDefinition::fromValue(unknown));
    auto future                                            = encoded.value();
    (*future.getIf<eve::Value::Object>())["schemaVersion"] = std::int64_t(2);
    REQUIRE(!eve::physics::SoftBody3DDefinition::fromValue(future));
}

TEST_CASE("softbody.volume.modelDefinitionRoundTripsMeshSamplingRecipe") {
    eve::physics::SoftBodyModelDefinition model;
    model.sourceMesh       = "asset://characters/slime.glb#mesh0";
    model.surfaceSampling  = eve::physics::SoftBodySurfaceSampling::Vertices;
    model.volumeSampling   = eve::physics::SoftBodyVolumeSampling::Voxels;
    model.volumeResolution = 24;
    model.shapeResolution  = 64;
    model.maxAnisotropy    = 2.5f;
    auto encoded           = model.toValue();
    REQUIRE(encoded.ok());
    auto decoded = eve::physics::SoftBodyModelDefinition::fromValue(encoded.value());
    REQUIRE(decoded.ok());
    REQUIRE_EQ(decoded.value().sourceMesh, model.sourceMesh);
    REQUIRE_EQ(decoded.value().volumeResolution, 24);
    REQUIRE_EQ(decoded.value().shapeResolution, 64);
    REQUIRE(std::fabs(decoded.value().maxAnisotropy - 2.5f) < 0.0001f);
    auto registered = eve::physics::SoftBodyModelDefinition::ensureSchemaRegistered();
    REQUIRE(registered.ok());
    REQUIRE(eve::schema::SchemaRegistry::resolve("physics:softbody-model", 1) != nullptr);

    auto unknown                                          = encoded.value();
    (*unknown.getIf<eve::Value::Object>())["futureField"] = true;
    REQUIRE(!eve::physics::SoftBodyModelDefinition::fromValue(unknown));
}

TEST_CASE("softbody.volume.cooksVertexSampledModelFromCanonicalMesh") {
    eve::asset::CanonicalMeshData mesh;
    mesh.positions = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f};
    mesh.indices   = {0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3};
    eve::physics::SoftBodyModelDefinition definition;
    definition.sourceMesh      = "asset://tetrahedron.evmesh";
    definition.surfaceSampling = eve::physics::SoftBodySurfaceSampling::Vertices;
    definition.volumeSampling  = eve::physics::SoftBodyVolumeSampling::None;
    auto cooked                = eve::physics::softbody_cook::cookSoftBodyModel(mesh, definition);
    REQUIRE(cooked.ok());
    REQUIRE_EQ(cooked.value().particles.size(), std::size_t(4));
    REQUIRE_EQ(cooked.value().surfaceBindings.size(), std::size_t(4));
    REQUIRE_EQ(cooked.value().surfaceIndices.size(), std::size_t(12));
    REQUIRE_EQ(cooked.value().clusters.size(), std::size_t(4));
    REQUIRE(cooked.value().validate().ok());
}

TEST_CASE("softbody.volume.cookerRejectsUnsupportedVoxelRecipeTransactionally") {
    eve::asset::CanonicalMeshData mesh;
    mesh.positions = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
    mesh.indices   = {0, 1, 2};
    eve::physics::SoftBodyModelDefinition definition;
    definition.sourceMesh      = "asset://triangle.evmesh";
    definition.surfaceSampling = eve::physics::SoftBodySurfaceSampling::Voxels;
    auto cooked                = eve::physics::softbody_cook::cookSoftBodyModel(mesh, definition);
    REQUIRE(!cooked.ok());
    REQUIRE_EQ(cooked.status().code(), eve::StatusCode::Unsupported);
}

TEST_CASE("softbody.volume.creationSchemaMatchesDefinitionDefaults") {
    const auto schema = eve::physics_editing::softBody3DDefinitionSchema();
    REQUIRE_EQ(schema.typeId, std::string(eve::physics::SoftBody3DDefinition::SchemaId));
    REQUIRE_EQ(schema.version, eve::physics::SoftBody3DDefinition::SchemaVersion);
    REQUIRE_EQ(schema.properties.size(), std::size_t(20));
    auto iterations = schema.find(eve::editing::PropertyPath("iterations"));
    REQUIRE(iterations);
    REQUIRE_EQ(iterations->numeric.minimum.value(), 1.0);
    REQUIRE_EQ(iterations->numeric.maximum.value(), 32.0);
    REQUIRE_EQ(*iterations->defaultValue.getIf<std::int64_t>(), std::int64_t(5));
    auto selfCollision = schema.find(eve::editing::PropertyPath("selfCollision"));
    REQUIRE(selfCollision);
    REQUIRE_EQ(*selfCollision->defaultValue.getIf<bool>(), false);
}

TEST_CASE("softbody.volume.shapeMatchingRecoversAfterCompression") {
    auto       created   = SoftBody3D::create(4, 4, 4, 0.4f, -0.6f, 2.f, -0.6f);
    const bool createdOk = created.ok();
    REQUIRE(createdOk);
    auto body = std::move(created).takeValue();
    body->setGravity(0.f, 0.f, 0.f);
    body->setDeformationResistance(0.92f);
    body->setIterations(8);

    for (int z = 0; z < body->getLayers(); ++z) {
        for (int y = 0; y < body->getRows(); ++y) {
            for (int x = 0; x < body->getCols(); ++x) {
                const int index = (z * body->getRows() + y) * body->getCols() + x;
                body->setParticlePosition(index, body->getParticleX(index) * 0.55f, body->getParticleY(index),
                                          body->getParticleZ(index));
            }
        }
    }
    const float compressed = body->getVolumeRatio();
    REQUIRE_LT(compressed, 0.65f);
    for (int frame = 0; frame < 90; ++frame) body->update(1.f / 60.f);
    REQUIRE_GT(body->getVolumeRatio(), compressed + 0.2f);
    REQUIRE_GT(body->getVolumeRatio(), 0.85f);
}

TEST_CASE("softbody.volume.pinsAndWorldCollision") {
    auto       created   = SoftBody3D::create(3, 3, 3, 0.35f, -0.35f, 2.f, -0.35f);
    const bool createdOk = created.ok();
    REQUIRE(createdOk);
    auto                  body = std::move(created).takeValue();
    eve::physics::World3D world(0.f, -9.8f, 0.f, true);
    auto*                 ground = world.newBody("static", 0.f, -0.5f, 0.f);
    REQUIRE(ground != nullptr);
    ground->newBoxShape(6.f, 1.f, 6.f);
    body->setCollideWorld(&world);
    body->setParticleRadius(0.12f);
    body->setDeformationResistance(0.8f);
    body->pin(24);
    const float pinnedY = body->getParticleY(24);

    for (int frame = 0; frame < 180; ++frame) {
        world.update(1.f / 60.f);
        body->update(1.f / 60.f);
    }
    REQUIRE(std::fabs(body->getParticleY(24) - pinnedY) < 0.001f);
    for (int i = 0; i < body->getParticleCount(); ++i) {
        REQUIRE(std::isfinite(body->getParticleX(i)));
        REQUIRE(std::isfinite(body->getParticleY(i)));
        REQUIRE(std::isfinite(body->getParticleZ(i)));
        REQUIRE_GE(body->getParticleY(i), -0.2f);
    }
}

TEST_CASE("softbody.volume.coreAcceptsBackendNeutralCollisionProvider") {
    auto       created   = SoftBody3D::create(2, 2, 2, 0.25f, 0.f, 0.2f, 0.f);
    const bool createdOk = created.ok();
    REQUIRE(createdOk);
    auto body = std::move(created).takeValue();
    body->setParticleRadius(0.1f);

    PlaneCollisionProvider provider;
    provider.available = false;
    body->setCollisionWorld(&provider);
    body->update(1.f / 60.f);
    REQUIRE_EQ(provider.probeCount, 0);

    provider.available = true;
    for (int frame = 0; frame < 90; ++frame) body->update(1.f / 60.f);
    REQUIRE_GT(provider.probeCount, 0);
    REQUIRE_GT(provider.impulseCount, 0);
    REQUIRE_LT(provider.accumulatedImpulseY, 0.f);
    for (int i = 0; i < body->getParticleCount(); ++i) REQUIRE_GE(body->getParticleY(i), 0.099f);

    body->setCollisionWorld(nullptr);
}

TEST_CASE("softbody.volume.checkedStepTracksInjectedTime") {
    auto       created   = SoftBody3D::create(2, 2, 2, 0.5f, 0.f, 1.f, 0.f);
    const bool createdOk = created.ok();
    REQUIRE(createdOk);
    auto                             body = std::move(created).takeValue();
    eve::physics::SimulationSettings settings;
    settings.subStepCount = 3;
    auto stepped =
        body->step(eve::SimulationStep{eve::SimulationTick{1}, eve::Duration::fromNanoseconds(16666667)}, settings);
    const bool steppedOk = stepped.ok();
    REQUIRE(steppedOk);
    REQUIRE_EQ(body->observation().stepCount, 1u);
    REQUIRE(body->observation().lastTick == eve::SimulationTick{1});
}

TEST_CASE("softbody.volume.renderPreview") {
    auto* window   = eve::window::Window::create();
    auto* graphics = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(graphics != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 640;
    settings.height   = 420;
    settings.centered = true;
    REQUIRE(window->setWindowSettings(settings));
    graphics->setScreenReadbackEnabled(true);
    auto* camera = eve::graphics::Camera3D::createCamera();
    camera->setEye(2.7f, 2.4f, 4.f);
    camera->setTarget(0.f, 1.f, 0.f);
    camera->setAmbient(0.4f, 0.36f, 0.34f);
    camera->setActive(true);
    eve::graphics::RenderSystem3D::setDirectionalLight(-1.f, -1.f, -1.f, 1.4f, 1.2f, 1.f);

    auto       created   = SoftBody3D::create(4, 4, 4, 0.35f, -0.55f, 0.7f, -0.55f);
    const bool createdOk = created.ok();
    REQUIRE(createdOk);
    auto body = std::move(created).takeValue();
    body->setGravity(0.f, -3.f, 0.f);
    body->setDeformationResistance(0.82f);
    eve::physics::SoftBody3DRenderer renderer(body.get());
    renderer.setColor(0.96f, 0.34f, 0.18f, 1.f);
    graphics->setBackgroundColorRGBA(0.04f, 0.06f, 0.1f, 1.f);
    const glm::mat4 view =
        glm::lookAtRH(glm::vec3(2.7f, 2.4f, 4.f), glm::vec3(0.f, 1.f, 0.f), glm::vec3(0.f, 1.f, 0.f));
    glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(45.f), 640.f / 420.f, 0.1f, 100.f);
    projection[1][1] *= -1.f;
    for (int frame = 0; frame < 45; ++frame) {
        if (frame < 18) body->applyForce(0.08f, 0.f, 0.f);
        body->update(1.f / 60.f);
        graphics->begin3DFrame();
        graphics->setMesh3DViewProj(projection * view);
        graphics->setMesh3DView(view);
        graphics->setMesh3DClip(0.1f, 100.f);
        graphics->setMesh3DCameraPos(glm::vec3(2.7f, 2.4f, 4.f));
        renderer.draw(graphics);
        graphics->present();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
        }
        SDL_Delay(4);
    }
    const std::string output = std::string(EVENGINE_TEST_BINARY_DIR) + "/softbody_volume_preview.png";
    REQUIRE(graphics->saveFramePng(output));
    REQUIRE(std::filesystem::exists(output));
    REQUIRE(std::filesystem::file_size(output) > std::uintmax_t(1024));
    window->close();
}
}  // namespace test_softbody_volume
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_standard_combat_abilities)
#include "PrivateTestScope.h"
#include <set>
#include <string>
#include "action/AbilityAsset.h"
#include "action/AbilityController.h"
#include "combat/StandardAbilities.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// standard_combat_abilities.cpp
namespace test_standard_combat_abilities {
TEST_CASE("standardCombatAbilities.providesTwentyUniqueValidatedArchetypes") {
    auto catalog = eve::combat::standardCombatAbilities();
    REQUIRE(catalog.ok());
    REQUIRE_EQ(catalog.value().size(), 20u);
    std::set<std::string> ids;
    for (const auto& definition : catalog.value()) {
        REQUIRE(definition.validate().ok());
        ids.insert(definition.id.format());
        CHECK(definition.action.metadata.contains("combat.adapter"));
    }
    CHECK_EQ(ids.size(), 20u);
    CHECK(ids.contains("combat-ability:light-attack"));
    CHECK(ids.contains("combat-ability:death"));
}

TEST_CASE("standardCombatAbilities.registerGrantAndActivateThroughSharedController") {
    auto catalog = eve::combat::standardCombatAbilities();
    REQUIRE(catalog.ok());
    eve::action::ActionRuntime  actions;
    eve::action::AbilityRuntime abilities(actions);
    for (const auto& definition : catalog.value()) REQUIRE(abilities.registerDefinition(definition).ok());

    auto grant = abilities.grant("fighter", catalog.value()[0].id);
    REQUIRE(grant.ok());
    eve::action::PlayerAbilityIntentQueue player;
    eve::action::AbilityIntent            intent;
    intent.grantId          = grant.value();
    intent.request.actionId = catalog.value()[0].action.id;
    REQUIRE(player.enqueue(std::move(intent)).ok());
    eve::action::AbilityControllerRuntime controller(abilities);
    auto                                  activation = controller.processNext(player, eve::SimulationTick(1));
    REQUIRE(activation.ok());
    REQUIRE(activation.value().has_value());
    CHECK(actions.find(activation.value()->executionId) != nullptr);
}

TEST_CASE("standardCombatAbilities.roundTripEveryArchetypeThroughVersionedJsonAssets") {
    auto catalog = eve::combat::standardCombatAbilities();
    REQUIRE(catalog.ok());
    eve::action::ActionRuntime  actions;
    eve::action::AbilityRuntime abilities(actions);
    for (const auto& definition : catalog.value()) {
        auto encoded = eve::action::encodeAbilityAsset(definition);
        REQUIRE(encoded.ok());
        auto json = encoded.value().toJson();
        REQUIRE(json.ok());
        auto parsed = eve::Value::fromJson(json.value());
        REQUIRE(parsed.ok());
        auto decoded = eve::action::decodeAbilityAsset(parsed.value());
        REQUIRE(decoded.ok());
        CHECK_EQ(decoded.value().id, definition.id);
        CHECK_EQ(decoded.value().action.metadata.at("combat.adapter").asString(),
                 definition.action.metadata.at("combat.adapter").asString());
        REQUIRE(abilities.registerDefinition(std::move(decoded).takeValue()).ok());
    }
}
}  // namespace test_standard_combat_abilities
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_stylize_action_attack_vfx)
#include "PrivateTestScope.h"
#include <optional>
#include <string>
#include <string_view>
#include "action/ActionAttackVfxBlock.h"
#include "action/ActionCameraBlock.h"
#include "action/ActionNotifyRegistry.h"
#include "common/Capability.h"
#include "stylize/AttackVfxLayerExecutor.h"
#include "stylize/AttackVfxRecipe.h"
#include "stylize/AttackVfxRuntime.h"
#include "stylize/action/StylizeAction.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// stylize_action_attack_vfx.cpp
namespace test_stylize_action_attack_vfx {
using namespace eve::stylize;
using namespace eve::stylize_action;

namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

const char* kMinimalRecipe = R"({
  "schema":"eve.stylize.attack-vfx","schemaVersion":1,"id":"attackvfx:pulse",
  "phases":[{"kind":"release","durationSeconds":0.5,"layers":[
    {"role":"camera","floatParams":{"posAmp":0.1,"duration":0.08}},
    {"role":"particles","uri":"particles://missing"}
  ]}]
})";

class FakeCameraSink final : public eve::action::IActionCameraCueSink {
public:
    bool              supports(const eve::LogicalId&) const noexcept override { return true; }
    eve::Result<void> trigger(const eve::action::ActionCameraCueBinding& binding,
                              const eve::action::ActionNotifyContext&) override {
        ++calls;
        last = binding;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    int                                                calls = 0;
    std::optional<eve::action::ActionCameraCueBinding> last;
};

IAttackVfxLayerExecutor* cameraExecutor() {
    IAttackVfxLayerExecutor* found = nullptr;
    eve::cap::forEach<IAttackVfxLayerExecutor>([&](IAttackVfxLayerExecutor* executor) {
        if (!found && executor && executor->role() == AttackVfxLayerRole::Camera) found = executor;
    });
    return found;
}

}  // namespace

TEST_CASE("stylize.action.attack_vfx binding validates recipe xor uri") {
    auto both = eve::action::ActionAttackVfxBinding::fromPayload(
        {{"recipeId", "attackvfx:pulse"}, {"uri", "json:{}"}, {"lifetimeSeconds", 0.2}},
        eve::action::ActionAttackVfxShape::Instant);
    REQUIRE(!both.ok());

    auto neither = eve::action::ActionAttackVfxBinding::fromPayload({{"lifetimeSeconds", 0.2}},
                                                                    eve::action::ActionAttackVfxShape::Instant);
    REQUIRE(!neither.ok());

    auto ok = eve::action::ActionAttackVfxBinding::fromPayload(
        {{"uri", std::string("json:") + kMinimalRecipe}, {"lifetimeSeconds", 0.4}},
        eve::action::ActionAttackVfxShape::Instant);
    REQUIRE(ok.ok());
    REQUIRE(!ok.value().recipeId.isValid());
    REQUIRE(!ok.value().uri.empty());
}

TEST_CASE("stylize.action.attack_vfx camera executor routes to optional sink") {
    FakeCameraSink sink;
    eve::cap::addListener<eve::action::IActionCameraCueSink>(&sink);
    auto* module = StylizeAction::create();
    REQUIRE(module != nullptr);
    auto* executor = cameraExecutor();
    REQUIRE(executor != nullptr);

    AttackVfxLayer layer;
    layer.role                    = AttackVfxLayerRole::Camera;
    layer.floatParams["posAmp"]   = 0.2f;
    layer.floatParams["rotAmp"]   = 1.0f;
    layer.floatParams["duration"] = 0.15f;

    AttackVfxRequest playRequest;
    playRequest.sourceId = 7;
    playRequest.targetId = 9;

    AttackVfxLayerStartRequest request;
    request.layer       = &layer;
    request.playRequest = &playRequest;

    auto started = executor->start(request);
    REQUIRE(started.ok());
    REQUIRE_EQ(sink.calls, 1);
    REQUIRE(sink.last.has_value());
    CHECK_EQ(sink.last->positionAmplitude, 0.2);
    CHECK_EQ(sink.last->duration.seconds(), 0.15);
    REQUIRE(started.value().valid());
    CHECK(executor->stop(started.value(), AttackVfxStopBehavior::ClearImmediately).ok());

    eve::cap::removeListener<eve::action::IActionCameraCueSink>(&sink);
}

TEST_CASE("stylize.action.attack_vfx runtime plays with soft-skipped missing backends") {
    FakeCameraSink sink;
    eve::cap::addListener<eve::action::IActionCameraCueSink>(&sink);
    auto* module = StylizeAction::create();
    REQUIRE(module != nullptr);
    REQUIRE(module->registerRecipeJson(kMinimalRecipe).ok());

    // Camera executor is present; particles executor is not (Particles module unloaded).
    // Play must succeed: missing particles role soft-skips, camera impulse fires.
    auto played = module->runtime().play(id("attackvfx:pulse"), {});
    REQUIRE(played.ok());
    REQUIRE_EQ(sink.calls, 1);
    REQUIRE_EQ(module->runtime().activeCount(), 1u);

    auto advanced = module->runtime().advance(1.0);
    REQUIRE(advanced.ok());
    REQUIRE_EQ(module->runtime().activeCount(), 0u);

    eve::cap::removeListener<eve::action::IActionCameraCueSink>(&sink);
}

TEST_CASE("stylize.action.attack_vfx action notify plays registered recipe") {
    FakeCameraSink sink;
    eve::cap::addListener<eve::action::IActionCameraCueSink>(&sink);
    auto* module = StylizeAction::create();
    REQUIRE(module != nullptr);
    REQUIRE(module->registerRecipeJson(kMinimalRecipe).ok());

    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    REQUIRE(registry.value().hasHandler("presentation:attack-vfx"));

    eve::action::ActionTimelineEvent event;
    event.kind    = eve::action::ActionTimelineEventKind::Notify;
    event.type    = id("presentation:attack-vfx");
    event.itemId  = id("item:pulse");
    event.payload = {{"recipeId", "attackvfx:pulse"}, {"lifetimeSeconds", 0.25}};

    eve::action::ActionNotifyContext context;
    context.executionId = eve::action::ActionExecutionId{1};
    context.time        = eve::Duration::fromSeconds(0.0).value();

    auto handled = registry.value().dispatch(event, context);
    REQUIRE(handled.ok());
    REQUIRE_EQ(module->runtime().activeCount(), 1u);
    REQUIRE_EQ(sink.calls, 1);

    context.time  = eve::Duration::fromSeconds(0.3).value();
    auto advanced = registry.value().advanceHandlers(context);
    REQUIRE(advanced.ok());
    REQUIRE_EQ(module->runtime().activeCount(), 0u);

    // Second advance after the instant recipe already completed must not fail on stale stop.
    context.time = eve::Duration::fromSeconds(0.6).value();
    advanced     = registry.value().advanceHandlers(context);
    REQUIRE(advanced.ok());

    eve::cap::removeListener<eve::action::IActionCameraCueSink>(&sink);
}

TEST_CASE("stylize.action.attack_vfx cues are sorted by offset") {
    auto binding = eve::action::ActionAttackVfxBinding::fromPayload(
        {{"uri", std::string("json:") + kMinimalRecipe},
         {"lifetimeSeconds", 1.0},
         {"cues",
          eve::Value::Array{
              eve::Value::Object{{"offsetSeconds", 0.5}, {"cue", "late"}},
              eve::Value::Object{{"offsetSeconds", 0.1}, {"cue", "early"}},
          }}},
        eve::action::ActionAttackVfxShape::Instant);
    REQUIRE(binding.ok());
    REQUIRE_EQ(binding.value().cues.size(), 2u);
    CHECK_EQ(binding.value().cues[0].cue, "early");
    CHECK_EQ(binding.value().cues[1].cue, "late");
}

TEST_CASE("stylize.action.attack_vfx concurrent executions use isolated clocks") {
    FakeCameraSink sink;
    eve::cap::addListener<eve::action::IActionCameraCueSink>(&sink);
    auto* module = StylizeAction::create();
    REQUIRE(module != nullptr);
    REQUIRE(module->registerRecipeJson(kMinimalRecipe).ok());

    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());

    eve::action::ActionTimelineEvent event;
    event.kind    = eve::action::ActionTimelineEventKind::Notify;
    event.type    = id("presentation:attack-vfx");
    event.itemId  = id("item:pulse");
    event.payload = {{"recipeId", "attackvfx:pulse"}, {"lifetimeSeconds", 0.5}};

    eve::action::ActionNotifyContext a;
    a.executionId = eve::action::ActionExecutionId{10};
    a.time        = eve::Duration::fromSeconds(1.0).value();
    REQUIRE(registry.value().dispatch(event, a).ok());

    eve::action::ActionNotifyContext b;
    b.executionId = eve::action::ActionExecutionId{11};
    b.time        = eve::Duration::fromSeconds(0.0).value();
    REQUIRE(registry.value().dispatch(event, b).ok());
    REQUIRE_EQ(module->runtime().activeCount(), 2u);

    // Advancing B at t=0 must not apply A's large clock jump as dt.
    b.time = eve::Duration::fromSeconds(0.05).value();
    REQUIRE(registry.value().advanceHandlers(b).ok());
    REQUIRE_EQ(module->runtime().activeCount(), 2u);

    eve::cap::removeListener<eve::action::IActionCameraCueSink>(&sink);
}
}  // namespace test_stylize_action_attack_vfx
#endif
