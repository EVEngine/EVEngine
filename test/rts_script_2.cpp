#include "RtsCompositionFixtures.h"

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
