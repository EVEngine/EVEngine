#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "card/CardEffects.h"
#include "rts/RTSEffects.h"
#include "vehicle/VehicleEffects.h"
#include "weapon/WeaponEffects.h"

#include <array>
#include <cstdint>
#include <string>
#include <utility>

namespace {

eve::SubjectRef subject() {
    eve::PersistentId::Bytes bytes{};
    bytes[0] = 0x42;
    bytes[15] = 0x17;
    return eve::SubjectRef::fromPersistentId(eve::PersistentId(bytes));
}

eve::SimulationStep step(std::uint64_t tick, double seconds) {
    auto delta = eve::Duration::fromSeconds(seconds);
    REQUIRE(delta.ok());
    return {eve::SimulationTick(tick), std::move(delta).takeValue()};
}

eve::effects::EffectPolicy oncePolicy() {
    eve::effects::EffectPolicy policy;
    policy.stackMode = eve::effects::StackMode::NewInstance;
    policy.stackCount = eve::effects::StackCountPolicy::Keep;
    policy.maxStacks = 1;
    return policy;
}

}  // namespace

TEST_CASE("effects.domainAdapters.shareLifecycleAndPeriodicParity") {
    eve::card::CardEffectAdapter card;
    eve::rts::RTSEffectAdapter rts;
    eve::vehicle::VehicleEffectAdapter vehicle;
    eve::weapon::WeaponEffectAdapter weapon;

    eve::card::CardEffectDefinition cardDefinition;
    cardDefinition.id = "periodic.damage";
    cardDefinition.source = "spell.card";
    cardDefinition.duration = 3.0;
    cardDefinition.period = 1.0;
    cardDefinition.magnitude = 5.0;
    cardDefinition.policy = oncePolicy();
    auto cardHandle = card.apply(cardDefinition, subject());
    REQUIRE(cardHandle.ok());

    eve::rts::RTSEffectDefinition rtsDefinition;
    rtsDefinition.id = cardDefinition.id;
    rtsDefinition.source = cardDefinition.source;
    rtsDefinition.duration = cardDefinition.duration;
    rtsDefinition.period = cardDefinition.period;
    rtsDefinition.magnitude = cardDefinition.magnitude;
    rtsDefinition.policy = cardDefinition.policy;
    auto rtsHandle = rts.apply(rtsDefinition, subject());
    REQUIRE(rtsHandle.ok());

    eve::vehicle::VehicleEffectDefinition vehicleDefinition;
    vehicleDefinition.id = cardDefinition.id;
    vehicleDefinition.source = cardDefinition.source;
    vehicleDefinition.duration = cardDefinition.duration;
    vehicleDefinition.period = cardDefinition.period;
    vehicleDefinition.magnitude = cardDefinition.magnitude;
    vehicleDefinition.policy = cardDefinition.policy;
    vehicleDefinition.armorZone = "side";
    auto vehicleHandle = vehicle.apply(vehicleDefinition, subject());
    REQUIRE(vehicleHandle.ok());

    eve::weapon::WeaponEffectDefinition weaponDefinition;
    weaponDefinition.id = cardDefinition.id;
    weaponDefinition.source = cardDefinition.source;
    weaponDefinition.duration = cardDefinition.duration;
    weaponDefinition.period = cardDefinition.period;
    weaponDefinition.magnitude = cardDefinition.magnitude;
    weaponDefinition.policy = cardDefinition.policy;
    auto weaponHandle = weapon.apply(weaponDefinition, subject());
    REQUIRE(weaponHandle.ok());

    const auto checkCommon = [&](const auto& adapter, const auto& handle) {
        auto resolved = adapter.resolve(handle);
        REQUIRE(resolved.ok());
        REQUIRE(resolved.value() != nullptr);
        CHECK_EQ(resolved.value()->subject, subject().format());
        CHECK_EQ(resolved.value()->source, std::string("spell.card"));
        CHECK_EQ(resolved.value()->period, 1.0);
        CHECK_EQ(resolved.value()->duration, 3.0);
        CHECK_EQ(resolved.value()->stackCount, 1u);
    };
    checkCommon(card, std::move(cardHandle).takeValue());
    checkCommon(rts, std::move(rtsHandle).takeValue());
    checkCommon(vehicle, std::move(vehicleHandle).takeValue());
    checkCommon(weapon, std::move(weaponHandle).takeValue());

    auto cardUpdate = card.advance(step(1, 1.0));
    auto rtsUpdate = rts.advance(step(1, 1.0));
    auto vehicleUpdate = vehicle.advance(step(1, 1.0));
    auto weaponUpdate = weapon.advance(step(1, 1.0));
    REQUIRE(cardUpdate.ok());
    REQUIRE(rtsUpdate.ok());
    REQUIRE(vehicleUpdate.ok());
    REQUIRE(weaponUpdate.ok());
    CHECK_EQ(std::move(cardUpdate).takeValue().settled, 1u);
    CHECK_EQ(std::move(rtsUpdate).takeValue().settled, 1u);
    CHECK_EQ(std::move(vehicleUpdate).takeValue().settled, 1u);
    CHECK_EQ(std::move(weaponUpdate).takeValue().settled, 1u);
    CHECK_EQ(card.target().health, 95);
    CHECK_EQ(rts.target().morale, 95.0);
    CHECK_EQ(vehicle.target().hull, 95.0);
    CHECK_EQ(weapon.target().heat, 5.0);
}

TEST_CASE("effects.domainAdapters.failureIsAtomicAndRestoreMakesHandlesStale") {
    eve::card::CardEffectAdapter card;
    eve::rts::RTSEffectAdapter rts;
    eve::vehicle::VehicleEffectAdapter vehicle;
    eve::weapon::WeaponEffectAdapter weapon;

    eve::card::CardEffectDefinition cardDefinition;
    cardDefinition.id = "failure.damage";
    cardDefinition.period = 1.0;
    cardDefinition.magnitude = -2.0;
    cardDefinition.policy = oncePolicy();
    auto cardHandle = card.apply(cardDefinition, subject());
    REQUIRE(cardHandle.ok());
    const auto cardReference = std::move(cardHandle).takeValue();
    const auto cardHealth = card.target().health;
    auto cardFailed = card.advance(step(1, 1.0));
    CHECK(!cardFailed.ok());
    CHECK_EQ(card.target().health, cardHealth);
    CHECK(card.resolve(cardReference).ok());

    eve::rts::RTSEffectDefinition rtsDefinition;
    rtsDefinition.id = "failure.morale";
    rtsDefinition.period = 1.0;
    rtsDefinition.magnitude = -2.0;
    rtsDefinition.policy = oncePolicy();
    auto rtsHandle = rts.apply(rtsDefinition, subject());
    REQUIRE(rtsHandle.ok());
    const auto rtsReference = std::move(rtsHandle).takeValue();
    const auto rtsMorale = rts.target().morale;
    auto rtsFailed = rts.advance(step(1, 1.0));
    CHECK(!rtsFailed.ok());
    CHECK_EQ(rts.target().morale, rtsMorale);
    CHECK(rts.resolve(rtsReference).ok());

    eve::vehicle::VehicleEffectDefinition vehicleDefinition;
    vehicleDefinition.id = "failure.hull";
    vehicleDefinition.period = 1.0;
    vehicleDefinition.magnitude = -2.0;
    vehicleDefinition.policy = oncePolicy();
    auto vehicleHandle = vehicle.apply(vehicleDefinition, subject());
    REQUIRE(vehicleHandle.ok());
    const auto vehicleReference = std::move(vehicleHandle).takeValue();
    const auto vehicleHull = vehicle.target().hull;
    auto vehicleFailed = vehicle.advance(step(1, 1.0));
    CHECK(!vehicleFailed.ok());
    CHECK_EQ(vehicle.target().hull, vehicleHull);
    CHECK(vehicle.resolve(vehicleReference).ok());

    eve::weapon::WeaponEffectDefinition weaponDefinition;
    weaponDefinition.id = "failure.heat";
    weaponDefinition.period = 1.0;
    weaponDefinition.magnitude = -2.0;
    weaponDefinition.policy = oncePolicy();
    auto weaponHandle = weapon.apply(weaponDefinition, subject());
    REQUIRE(weaponHandle.ok());
    const auto weaponReference = std::move(weaponHandle).takeValue();
    const auto weaponHeat = weapon.target().heat;
    auto weaponFailed = weapon.advance(step(1, 1.0));
    CHECK(!weaponFailed.ok());
    CHECK_EQ(weapon.target().heat, weaponHeat);
    CHECK(weapon.resolve(weaponReference).ok());

    const auto snapshot = weapon.snapshot();
    auto extra = weapon.apply(weaponDefinition, subject());
    REQUIRE(extra.ok());
    const auto stale = std::move(extra).takeValue();
    REQUIRE(weapon.restore(snapshot).ok());
    CHECK(!weapon.resolve(stale).ok());
    CHECK_EQ(static_cast<int>(weapon.resolve(stale).code()),
             static_cast<int>(eve::StatusCode::Rejected));
    CHECK(!weapon.resolve(weaponReference).ok());
}

TEST_CASE("effects.domainAdapters.keepDomainStrategies") {
    eve::card::CardEffectAdapter card;
    eve::card::CardEffectDefinition shield;
    shield.id = "card.shield";
    shield.magnitude = 8.0;
    shield.kind = eve::card::CardEffectKind::Shield;
    shield.policy = oncePolicy();
    REQUIRE(card.apply(shield, subject()).ok());
    CHECK_EQ(card.target().barrier, 8);

    eve::rts::RTSEffectAdapter rts;
    eve::rts::RTSEffectDefinition lock;
    lock.id = "rts.production-lock";
    lock.kind = eve::rts::RTSEffectKind::ProductionLock;
    lock.policy = oncePolicy();
    REQUIRE(rts.apply(lock, subject()).ok());
    CHECK(rts.target().productionLocked);

    eve::vehicle::VehicleEffectAdapter vehicle;
    eve::vehicle::VehicleEffectDefinition hit;
    hit.id = "vehicle.rear-hit";
    hit.armorZone = "rear";
    hit.period = 1.0;
    hit.magnitude = 10.0;
    hit.policy = oncePolicy();
    REQUIRE(vehicle.apply(hit, subject()).ok());
    REQUIRE(vehicle.advance(step(1, 1.0)).ok());
    CHECK_EQ(vehicle.target().hull, 87.5);
    CHECK_EQ(vehicle.target().criticalHits, 0u);

    eve::weapon::WeaponEffectAdapter weapon;
    eve::weapon::WeaponEffectDefinition heat;
    heat.id = "weapon.overheat";
    heat.period = 1.0;
    heat.duration = 2.0;
    heat.magnitude = 60.0;
    heat.policy = oncePolicy();
    REQUIRE(weapon.apply(heat, subject()).ok());
    REQUIRE(weapon.advance(step(1, 1.0)).ok());
    REQUIRE(weapon.advance(step(2, 1.0)).ok());
    CHECK(weapon.target().jammed);
    CHECK_EQ(weapon.target().blockedShots, 1u);
}
