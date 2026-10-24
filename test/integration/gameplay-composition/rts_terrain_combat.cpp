#include "RtsCompositionFixtures.h"

TEST_CASE("rts.terrainRidgeStopsFireAndTerrainEditsReopenFireLine") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    module;
    REQUIRE(module.configureScriptWorld(4, 4, 1.f).ok());
    REQUIRE(module
                .loadScriptContent(R"JSON({
      "weapons":[{"id":"beam","damageType":"normal","damage":10,"range":8,
        "cooldown":0.1,"projectileSpeed":0,"blockedByObstacles":true}],
      "units":[{"id":"shooter","weaponType":"beam","health":50,"speed":0,
        "radius":0.1,"attackRange":8,"sightRange":8},
        {"id":"target","health":50,"speed":0,"radius":0.1,"sightRange":1}]
    })JSON")
                .ok());
    const auto shooterType = eve::LogicalId::parse("unit:shooter");
    const auto targetType  = eve::LogicalId::parse("unit:target");
    REQUIRE(shooterType.has_value());
    REQUIRE(targetType.has_value());
    auto blue = module.newFaction(subject("00000000-0000-7000-8000-00000000f901"));
    auto red  = module.newFaction(subject("00000000-0000-7000-8000-00000000f902"));
    REQUIRE(blue.ok());
    REQUIRE(red.ok());
    auto shooter = module.newFactionUnit(*blue.value(), subject("00000000-0000-7000-8000-00000000f903"), *shooterType);
    auto target  = module.newFactionUnit(*red.value(), subject("00000000-0000-7000-8000-00000000f904"), *targetType);
    REQUIRE(shooter.ok());
    REQUIRE(target.ok());
    shooter.value()->motion()->x = 0.1f;
    shooter.value()->motion()->y = 0.1f;
    target.value()->motion()->x  = 2.9f;
    target.value()->motion()->y  = 2.88f;
    REQUIRE(module.setScriptTerrainElevation(1, 0, 3.f).ok());
    REQUIRE(module.stepScript(0.2).ok());
    CHECK_EQ(target.value()->durability()->state.health, 50.0);
    REQUIRE(module.setScriptTerrainElevation(1, 0, 0.f).ok());
    REQUIRE(module.stepScript(0.2).ok());
    CHECK(target.value()->durability()->state.health < 50.0);
}
