#include "common/Module.h"
#include "common/SquirrelBinding.h"
#include "weapon/Weapon.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>

TEST_CASE("combatCarrierScript.castAtTargetReportsHitEvents") {
    ssq::VM vm(1024, ssq::Libs::STRING | ssq::Libs::MATH);
    auto    eve = vm.addTable("eve");
    eve::script::exposeResultBindings(eve);
    eve::weapon::Weapon::expose(eve);
    vm.run(vm.compileSource(R"(
        weapon <- eve.Weapon();
        created <- weapon.newCarrierRuntime(16);
        pool <- created.value;
        pool.setTargetPosition(7, 1, 5.0, 0.0, 0.0);
        pool.addHitTarget(7, 1, 5.0, 0.0, 0.0, 0.6);
        casted <- pool.castAtTarget([
            { kind = "homing", turnRate = 90 },
            { kind = "pierce", count = 1 },
            { kind = "projectile", id = "spell:bolt", speed = 10, damage = 12, lifetime = 2, element = "fire" }
        ], 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 7, 1);
        missingTarget <- pool.cast([
            { kind = "homing", turnRate = 90 },
            { kind = "projectile", id = "spell:bolt", speed = 10, damage = 1, lifetime = 2 }
        ], 0.0, 0.0, 0.0, 1.0, 0.0, 0.0);
        frame <- pool.update(0.5);
        eventCount <- frame.value.eventCount;
        firstImpact <- frame.value.events[0].impact;
        firstTrigger <- frame.value.events[0].trigger;
        preset <- pool.registerRecipe({
            id = "spell:shell",
            lifetime = 1.5,
            speed = 8.0,
            damage = 4.0,
            motion = "linear"
        });
        spawned <- pool.spawn("spell:shell", 0.0, 0.0, 0.0, 0.0, 1.0, 0.0);
        // Preset OnHit recipes must advance even with an empty hit list.
        emptyHitFrame <- pool.update(0.1);
        invalid <- pool.cast([
            { kind = "projectile", id = "spell:a", speed = 1, damage = 1, lifetime = 1 },
            { kind = "homing", turnRate = 10 }
        ], 0.0, 0.0, 0.0, 1.0, 0.0, 0.0);
        oversized <- pool.addHitTarget(4294967296, 1, 0.0, 0.0, 0.0, 0.5);
        avoidBody <- pool.registerRecipe({
            id = "spell:avoid",
            lifetime = 1.0,
            speed = 5.0,
            motion = [ { kind = "avoidBody", avoidLookAhead = 2.0, maxTurnRateDegrees = 90.0 }, "linear" ],
            triggers = [ "onExpire" ],
            impacts = [ { kind = "release", on = "onExpire" } ]
        });
    )"));
    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK(vm.find("casted").toTable().get<bool>("ok"));
    CHECK(!vm.find("missingTarget").toTable().get<bool>("ok"));
    CHECK(vm.find("frame").toTable().get<bool>("ok"));
    CHECK(vm.find("eventCount").toInt() >= 1);
    CHECK_EQ(vm.find("firstImpact").toString(), std::string("emitHit"));
    CHECK_EQ(vm.find("firstTrigger").toString(), std::string("onHit"));
    CHECK(vm.find("preset").toTable().get<bool>("ok"));
    CHECK(vm.find("spawned").toTable().get<bool>("ok"));
    CHECK(vm.find("emptyHitFrame").toTable().get<bool>("ok"));
    CHECK(!vm.find("invalid").toTable().get<bool>("ok"));
    CHECK(!vm.find("oversized").toTable().get<bool>("ok"));
    CHECK(!vm.find("avoidBody").toTable().get<bool>("ok"));
}
