#include "common/ECS.h"
#include "weapon/Weapon.h"
#include "weapon/WeaponSystem.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("weapon.infiniteReserveReloadsAcrossMultipleMagazines") {
    ecs::Table          world;
    ecs::ScopedTable    guard(world);
    eve::weapon::Weapon weapons;
    REQUIRE_EQ(weapons.registerWeaponsFromJson(
                   R"([{"id":"infinite","logic":"hitscan","cooldown":0,
                        "ammo":{"mag":1,"reserve":-1,"reload":0.1}}])"),
               1);
    auto* weapon = weapons.newWeapon("infinite");
    REQUIRE(weapon != nullptr);
    eve::weapon::AttackRequest request;
    request.hasTarget = true;
    request.targetX   = 1.0f;
    for (int magazine = 0; magazine < 4; ++magazine) {
        REQUIRE(eve::weapon::WeaponSystem::tryFire(*weapon, request));
        CHECK_EQ(weapon->state()->resource.value, 0.0f);
        eve::weapon::WeaponSystem::update(*weapon, 0.11f);
        CHECK_EQ(weapon->state()->resource.value, 1.0f);
        CHECK_EQ(weapon->state()->resource.reserve, 0);
        CHECK(!weapon->state()->resource.reloading);
    }
}
