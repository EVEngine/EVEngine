#pragma once
#include "RTSSpawnRestoreFixture.h"
#include "rts/RTSAttributeCapture.h"
#include "rts/RTSWeaponCapture.h"
#include "weapon/WeaponAttributes.h"
#include "weapon/WeaponSystem.h"
namespace test_rts_spawn {
struct SavedRTSWeaponScope {
    SavedRTSSpawn                                            session;
    std::shared_ptr<const eve::rts::RTSWeaponCapture>        weapons;
    std::shared_ptr<const eve::rts::RTSUnitAttributeCapture> attributes;
    eve::SubjectRef                                          first, second, building, unarmed;
    std::weak_ptr<ecs::Table>                                sourceWorld;
};
inline SavedRTSWeaponScope freezeWeaponScope(bool distinctPools = false) {
    Fixture source;
    source.sourceWorld->advance({eve::SimulationTick(1), eve::Duration(100000000)}).expect("actual source spawning");
    SavedRTSWeaponScope saved;
    saved.sourceWorld = source.table;
    saved.first       = source.request.units[0].subject;
    saved.second      = source.request.units[1].subject;
    const auto epoch  = source.request.source.owner.child("body-weapon-graph");
    saved.building    = eve::SubjectRef::fromPersistentId(epoch.child("building"));
    saved.unarmed     = eve::SubjectRef::fromPersistentId(epoch.child("unarmed"));
    ecs::ScopedTable scope(*source.table);
    auto*            building = source.domain->newBuilding(saved.building).expect("native non-generated building");
    source.domain->newUnit(saved.unarmed).expect("native unarmed root");
    auto* pool                 = eve::weapon::AmmoPoolEntity::createPool();
    pool->identity()->id       = "same-name";
    pool->identity()->ammoType = "round";
    pool->state()->count       = 9;
    pool->state()->max         = 50;
    for (std::size_t i = 0; i < 2; ++i) {
        auto* unit     = source.domain->findUnit(source.request.units[i].subject);
        auto* gun      = dynamic_cast<eve::weapon::WeaponEntity*>(unit->weapon()->link.resolve());
        auto* selected = pool;
        if (distinctPools && i == 1) {
            selected                       = eve::weapon::AmmoPoolEntity::createPool();
            selected->identity()->id       = "same-name";
            selected->identity()->ammoType = "round";
            selected->state()->count       = 13;
            selected->state()->max         = 50;
        }
        gun->state()->ammoPool       = eve::weapon::AmmoPoolLink::bind(*gun, *selected).expect("source alias");
        gun->state()->resource.value = 0;
        eve::weapon::WeaponSystem::startReload(*gun);
        eve::weapon::WeaponSystem::update(*gun, i == 0 ? .25f : .5f);
        eve::weapon::WeaponAttributeAdapter::setBase(*gun, "mana", 3 + double(i)).expect("native weapon attributes");
        unit->attributes()->values.setBase("mana", 17 + double(i)).expect("full native Unit attributes");
        if (i == 0) {
            building->weapon()->link               = unit->weapon()->link;
            gun->state()->lastRequest.targetHandle = ecs::handle_of(building);
            gun->state()->cooldown                 = .75f;
        }
    }
    saved.session = freezeSpawn(source, epoch);
    saved.weapons = std::make_shared<const eve::rts::RTSWeaponCapture>(
        source.domain->captureWeapons(epoch).expect("whole body weapon scope"));
    saved.attributes = std::make_shared<const eve::rts::RTSUnitAttributeCapture>(
        source.domain->captureUnitAttributes(epoch).expect("full Unit attributes"));
    return saved;
}
}  // namespace test_rts_spawn
