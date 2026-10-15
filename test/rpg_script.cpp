#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

TEST_CASE("rpg.script.beginCastSkillAcceptsNullAndOmittedTarget") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        rpg <- eve.RPG();
        rpg.clearSkillDefinitions();
        rpg.registerSkillsFromJson("[{\"id\":\"script.cast.self\",\"castTime\":0,\"cooldown\":0}]");
        actor <- rpg.newActor();
        actor.learnSkill("script.cast.self");
        omitted <- actor.beginCastSkill("script.cast.self");
        actor.setSkillCooldown("script.cast.self", 0.0);
        withNull <- actor.beginCastSkill("script.cast.self", null);
        other <- rpg.newActor();
        actor.setSkillCooldown("script.cast.self", 0.0);
        withTarget <- actor.beginCastSkill("script.cast.self", other);
    )"));
    CHECK(vm.find("omitted").toBool());
    CHECK(vm.find("withNull").toBool());
    CHECK(vm.find("withTarget").toBool());
}
