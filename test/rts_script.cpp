#include "RtsCompositionFixtures.h"

TEST_CASE("rts.sandboxScriptCompilesThroughEveScriptFrontend") {
    const std::filesystem::path sourceRoot = std::filesystem::path(__FILE__).parent_path().parent_path();
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
    const std::filesystem::path sourceRoot = std::filesystem::path(__FILE__).parent_path().parent_path();
    auto                        readAll    = [](const std::filesystem::path& path) {
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
    const std::filesystem::path sourceRoot = std::filesystem::path(__FILE__).parent_path().parent_path();
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
