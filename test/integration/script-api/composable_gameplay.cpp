#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <fstream>
#include <sstream>
#include <string>

TEST_CASE("composableGameplay.scriptBuildsRebellionWithoutDomainCpp") {
    const std::string path = std::string(EVENGINE_SOURCE_DIR) + "/examples/composable-rebellion/simulation.nut";
    std::ifstream     input(path);
    REQUIRE(input.good());
    std::ostringstream source;
    source << input.rdbuf();
    const std::string script = source.str();

    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(script.c_str()));
    vm.run(vm.compileSource(R"(
        scenarioResult <- run_rebellion_scenario();
        scenarioOwner <- demoSocial.ownerOf(demoState["baseId"]);
        scenarioProduction <- demoState.baseStats.getFinal("production_speed", 0.0);
        scenarioOrder <- demoState.commandQueue.current().getKind();
        scenarioCanCommand <- demoTags.hasCapability(demoState.general, "command_army");
        scenarioEffect <- demoState.statusEffects.effectAt(0).getType();
        scenarioEvent <- demoState.events.at(0).getType();
        scenarioTransaction <- demoState.transactions.at(0).getState();
        scenarioStateOwner <- demoState.authoritativeState.get(demoState.baseId, "owner");
        scenarioRankResult <- demoState.definitions.resolve("rank", "rank.general");
        scenarioRank <- scenarioRankResult.ok ? scenarioRankResult.value.json : "";
        scenarioAuthority <- demoState.authority.can(demoState.general, demoState.baseId, "govern_base");
        scenarioPolicy <- demoState.policies.select("administration").getName();
        scenarioProductionA <- demoState.production.taskAt(0).getState() + ":" +
                               demoState.production.taskAt(0).getKind();
        scenarioProductionB <- demoState.production.taskAt(1).getState() + ":" +
                               demoState.production.taskAt(1).getKind();
        scenarioAiTarget <- demoState.aiTarget;
        scenarioAiState <- demoState.decisions.state("defense.ai");
        scenarioAiAction <- demoState.aiAction;
        scenarioAiVelocity <- demoState.aiVelocity;
        scenarioAiThreat <- demoState.aiThreat;
    )"));

    CHECK(vm.find("scenarioResult").toBool());
    CHECK_EQ(vm.find("scenarioOwner").toString(), std::string("faction.frontier"));
    CHECK_EQ(vm.find("scenarioProduction").toFloat(), 1.4F);
    CHECK_EQ(vm.find("scenarioOrder").toString(), std::string("secure_assets"));
    CHECK(vm.find("scenarioCanCommand").toBool());
    CHECK_EQ(vm.find("scenarioEffect").toString(), std::string("salary_unpaid"));
    CHECK_EQ(vm.find("scenarioEvent").toString(), std::string("rebellion_started"));
    CHECK_EQ(vm.find("scenarioTransaction").toString(), std::string("committed"));
    CHECK_EQ(vm.find("scenarioStateOwner").toString(), std::string("\"faction.frontier\""));
    CHECK_EQ(vm.find("scenarioRank").toString(), std::string("{\"authority\":[\"govern_base\",\"command_army\"],"
                                                             "\"commandCapacity\":8}"));
    CHECK(vm.find("scenarioAuthority").toBool());
    CHECK_EQ(vm.find("scenarioPolicy").toString(), std::string("governor_bonus"));
    CHECK_EQ(vm.find("scenarioProductionA").toString(), std::string("completed:build_unit"));
    CHECK_EQ(vm.find("scenarioProductionB").toString(), std::string("completed:issue_decree"));
    CHECK_EQ(vm.find("scenarioAiTarget").toString(), std::string("army.raiders"));
    CHECK_EQ(vm.find("scenarioAiState").toString(), std::string("engage"));
    CHECK_EQ(vm.find("scenarioAiAction").toString(), std::string("attack"));
    CHECK(vm.find("scenarioAiVelocity").toString().find("\"x\"") != std::string::npos);
    CHECK_EQ(vm.find("scenarioAiThreat").toFloat(), 0.8F);

    vm.run(vm.compileSource(R"(
        reset_demo();
        demoState.authority.revokeBySource("rank.general", "rank_removed");
        refresh_governor_bonus();
        deniedProduction <- demoState.baseStats.getFinal("production_speed", 0.0);
        deniedReason <- demoState.authority.explain(
            demoState.general, demoState.baseId, "govern_base").getReason();
    )"));
    CHECK_EQ(vm.find("deniedProduction").toFloat(), 1.0F);
    CHECK_EQ(vm.find("deniedReason").toString(), std::string("no_matching_rule"));

    vm.run(vm.compileSource(R"(
        reset_demo();
        apply_unpaid_salary();
        conflictResult <- demoState.authoritativeState.newBatch();
        local conflict = conflictResult.ok ? conflictResult.value : null;
        if (conflict != null) {
            conflict.setExpected(demoState.baseId, "owner", "\"faction.usurper\"", "\"faction.crown\"");
            demoState.authoritativeState.commit(conflict);
        }
        conflictResult <- evaluate_rebellion();
        conflictGeneralOwner <- demoState.authoritativeState.get(demoState.general, "owner");
        conflictSocialOwner <- demoSocial.ownerOf(demoState.general);
        conflictTransaction <- demoState.transactions.at(0).getState();
        conflictEvents <- demoState.events.size();
    )"));

    CHECK(!vm.find("conflictResult").toBool());
    CHECK_EQ(vm.find("conflictGeneralOwner").toString(), std::string("\"faction.crown\""));
    CHECK_EQ(vm.find("conflictSocialOwner").toString(), std::string("faction.crown"));
    CHECK_EQ(vm.find("conflictTransaction").toString(), std::string("failed"));
    CHECK_EQ(vm.find("conflictEvents").toInt(), int64_t{0});
}

TEST_CASE("composableGameplay.cardAndRtsAdaptersShareDefinitionsTerrainAndIdentity") {
    const std::string path = std::string(EVENGINE_SOURCE_DIR) + "/examples/composable-editor/gameplay_components.nut";
    std::ifstream     input(path);
    REQUIRE(input.good());
    std::ostringstream source;
    source << input.rdbuf();

    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(source.str().c_str()));
    vm.run(vm.compileSource(R"(
        gameplay <- GameplayEditorComponents();
        heightmapResult <- eve.Procgen().newHeightmap(8, 8);
        heightmap <- heightmapResult.ok ? heightmapResult.value : null;
        for (local y = 0; y < 8; ++y)
            for (local x = 0; x < 8; ++x) heightmap.setHeight(x, y, (x + y).tofloat() / 16.0);
        gameplay.bindTerrain(heightmap, 1.0);
        authoredCard <- gameplay.createCardInstance();
        fakeEntity <- { position={ x=0.0, z=0.0 } };
        authoredUnit <- gameplay.createRtsUnit("unit.editor", fakeEntity, 0.5, 0.5);
        gameplay.update(0.1);
        authoredCardDefinition <- authoredCard.getDefinitionId();
        authoredCardCount <- gameplay.cards.getCardDefinitionCount();
        authoredUnitStable <- gameplay.crowdSim.getAgentStableId(
            gameplay.crowdSim.getNamedAgentIndex("unit.editor"));
        authoredDefinitionCount <- gameplay.definitions.size();
        authoredTerrainCost <- gameplay.crowdSim.getCellCost(7, 7);
    )"));

    CHECK(vm.find("authoredUnit").toBool());
    CHECK_EQ(vm.find("authoredCardDefinition").toString(), std::string("card.scout"));
    CHECK_EQ(vm.find("authoredCardCount").toInt(), 1);
    CHECK_EQ(vm.find("authoredUnitStable").toString(), std::string("unit.editor"));
    CHECK_EQ(vm.find("authoredDefinitionCount").toInt(), 2);
    CHECK(vm.find("authoredTerrainCost").toFloat() > 1.f);
}
