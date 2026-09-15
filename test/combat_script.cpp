#include "common/Module.h"
#include "common/SquirrelBinding.h"
#include "combat/Combat.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>


TEST_CASE("combatScript.runtimeOwnsMovementAndDamageState") {
    ssq::VM vm(1024, ssq::Libs::STRING | ssq::Libs::MATH);
    auto eve = vm.addTable("eve");
    eve::script::exposeResultBindings(eve);
    eve::combat::Combat::expose(eve);
    vm.run(vm.compileSource(R"(
        combat <- eve.Combat();
        created <- combat.newRuntime();
        arena <- created.value;
        player <- "01020304-0506-0708-890a-0b0c0d0e0f10";
        enemy <- "11121314-1516-1718-991a-1b1c1d1e1f20";
        registeredPlayer <- arena.registerFighter(player, "fighter:player", 0.0, 0.0, 100.0, 40.0, 4.0, 20.0);
        registeredEnemy <- arena.registerFighter(enemy, "fighter:enemy", 3.0, 0.0, 80.0, 30.0, 3.0, 15.0);
        moved <- arena.setMoveIntent(player, 1.0, 0.0, 1.0);
        advanced <- arena.advance(1, 0.25);
        playerState <- arena.state(player);
        damaged <- arena.applyDamage(player, enemy, "Damage.Physical.Slash", 18.0, 10.0, 0.0, 0.0, 0.0);
        timelineDamage <- arena.applyTimelineDamage(player, enemy,
            "{\"damageType\":\"Damage.Physical.Slash\",\"amount\":7,\"poiseAmount\":2}");
        enemyState <- arena.state(enemy);
        invalid <- arena.applyDamage("bad", enemy, "Damage.Physical.Slash", 1.0, 0.0, 0.0, 0.0, 0.0);
        grantedAbility <- arena.grantAbility("fighter:player", "combat-ability:light-attack");
        grantId <- grantedAbility.value.grantId;
        activatedAbility <- arena.activateAbility(grantId, 2);
        blockedAbility <- arena.activateAbility(grantId, 2);
        abilityStep <- arena.advanceAbilities(3, 0.10);
        coolingGrant <- arena.abilityGrant(grantId);
        abilityFinished <- arena.advanceAbilities(4, 0.40);
        readyGrant <- arena.abilityGrant(grantId);
        reactivatedAbility <- arena.activateAbility(grantId, 5);
        invalidAbility <- arena.grantAbility("fighter:player", "combat-ability:missing");
        coolingSeconds <- coolingGrant.value.cooldownSeconds;
        finishedActiveCount <- abilityFinished.value.activeCount;
        readySeconds <- readyGrant.value.cooldownSeconds;
        playerX <- playerState.value.position.x;
        enemyHealth <- enemyState.value.health;
        damageReaction <- damaged.value.reaction;
    )"));
    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("arena").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(vm.find("registeredPlayer").toTable().get<bool>("ok"));
    CHECK(vm.find("registeredEnemy").toTable().get<bool>("ok"));
    CHECK(vm.find("moved").toTable().get<bool>("ok"));
    CHECK(vm.find("advanced").toTable().get<bool>("ok"));
    CHECK(vm.find("playerX").toFloat() > 0.0f);
    CHECK_EQ(vm.find("enemyHealth").toFloat(), 55.0f);
    CHECK(vm.find("timelineDamage").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("damageReaction").toString(), std::string("flinch"));
    CHECK(!vm.find("invalid").toTable().get<bool>("ok"));
    CHECK(vm.find("grantedAbility").toTable().get<bool>("ok"));
    CHECK(vm.find("activatedAbility").toTable().get<bool>("ok"));
    CHECK(!vm.find("blockedAbility").toTable().get<bool>("ok"));
    CHECK(vm.find("abilityStep").toTable().get<bool>("ok"));
    CHECK(vm.find("coolingSeconds").toFloat() > 0.0f);
    CHECK_EQ(vm.find("finishedActiveCount").toInt(), 0);
    CHECK_EQ(vm.find("readySeconds").toFloat(), 0.0f);
    CHECK(vm.find("reactivatedAbility").toTable().get<bool>("ok"));
    CHECK(!vm.find("invalidAbility").toTable().get<bool>("ok"));
}
