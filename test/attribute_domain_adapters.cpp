#include "zeroerr/unittest.h"

#include "attributes/AttributeProjection.h"
#include "card/CardAttributes.h"
#include "card/CardTypes.h"
#include "common/ECS.h"
#include "rts/RTSAttributes.h"
#include "rts/RTSTypes.h"
#include "vehicle/VehicleAttributes.h"
#include "vehicle/VehicleTypes.h"
#include "weapon/WeaponAttributes.h"
#include "weapon/WeaponTypes.h"

#include <string>

namespace {

using eve::attributes::AttributeModifier;
using eve::attributes::AttributeOperation;
using eve::attributes::ModifierPriority;

void checkSharedModifierContract(const AttributeModifier* modifier) {
    REQUIRE(modifier != nullptr);
    CHECK_EQ(modifier->source, std::string(eve::attributes::source::equipment));
    CHECK_EQ(modifier->priority, static_cast<ModifierPriority>(eve::attributes::priority::equipment));
    CHECK_EQ(static_cast<int>(modifier->operation), static_cast<int>(AttributeOperation::Add));
    CHECK_GT(modifier->sequence, 0u);
}

}  // namespace

TEST_CASE("attributes.domainAdapters.projectSelectedStats") {
    ecs::Table world;
    ecs::ScopedTable guard(world);

    auto* card = eve::card::CardData::createCard();
    REQUIRE(card != nullptr);
    card->stats()->attack = 3;
    card->stats()->health = 5;
    auto cardEnsure = eve::card::CardAttributeAdapter::ensure(*card);
    REQUIRE(cardEnsure.ok());
    auto cardModifier = eve::card::CardAttributeAdapter::addModifier(
        *card, "card.equipment.attack", eve::card::CardAttributeAdapter::attackAttribute,
        std::string(eve::attributes::source::equipment), AttributeOperation::Add, 2.0,
        eve::attributes::priority::equipment);
    REQUIRE(cardModifier.ok());
    checkSharedModifierContract(card->attributes()->values.modifierAt(0));
    CHECK_EQ(card->stats()->attack, 3);
    auto cardProject = eve::card::CardAttributeAdapter::project(*card);
    REQUIRE(cardProject.ok());
    CHECK_EQ(card->stats()->attack, 5);

    auto cardSnapshot = eve::card::CardAttributeAdapter::snapshot(*card);
    REQUIRE(cardSnapshot.ok());
    auto snapshot = std::move(cardSnapshot).takeValue();
    auto cardSet = eve::card::CardAttributeAdapter::setBase(
        *card, eve::card::CardAttributeAdapter::attackAttribute, 9.0);
    REQUIRE(cardSet.ok());
    CHECK_EQ(card->stats()->attack, 9);
    auto cardRestore = eve::card::CardAttributeAdapter::restore(
        *card, snapshot, card->attributes()->values.revision());
    REQUIRE(cardRestore.ok());
    CHECK_EQ(card->stats()->attack, 5);
    snapshot.owner.generation += 1;
    auto cardStale = eve::card::CardAttributeAdapter::restore(
        *card, snapshot, card->attributes()->values.revision());
    CHECK(!cardStale.ok());
    cardStale.ignore("stale-owner assertion");
    card->release();

    auto* vehicle = eve::vehicle::VehicleEntity::createVehicle();
    REQUIRE(vehicle != nullptr);
    vehicle->health()->hp = 80.0f;
    vehicle->health()->maxHp = 100.0f;
    auto vehicleEnsure = eve::vehicle::VehicleAttributeAdapter::ensure(*vehicle);
    REQUIRE(vehicleEnsure.ok());
    auto vehicleModifier = eve::vehicle::VehicleAttributeAdapter::addModifier(
        *vehicle, "vehicle.equipment.armor", eve::vehicle::VehicleAttributeAdapter::armorAttribute,
        std::string(eve::attributes::source::equipment), AttributeOperation::Add, 25.0,
        eve::attributes::priority::equipment);
    REQUIRE(vehicleModifier.ok());
    checkSharedModifierContract(vehicle->attributes()->values.modifierAt(0));
    auto vehicleProject = eve::vehicle::VehicleAttributeAdapter::project(*vehicle);
    REQUIRE(vehicleProject.ok());
    CHECK_EQ(vehicle->health()->hp, 80.0f);
    auto vehicleArmor = eve::vehicle::VehicleAttributeAdapter::read(
        *vehicle, eve::vehicle::VehicleAttributeAdapter::armorAttribute);
    REQUIRE(vehicleArmor.ok());
    CHECK_EQ(vehicleArmor.value(), 25.0);
    vehicle->release();

    auto* weapon = eve::weapon::WeaponEntity::createWeapon();
    REQUIRE(weapon != nullptr);
    weapon->state()->resource.kind = eve::weapon::ResourceKind::Mana;
    weapon->state()->resource.value = 10.0f;
    weapon->state()->resource.max = 20.0f;
    weapon->state()->resource.cost = 3.0f;
    auto weaponEnsure = eve::weapon::WeaponAttributeAdapter::ensure(*weapon);
    REQUIRE(weaponEnsure.ok());
    auto weaponModifier = eve::weapon::WeaponAttributeAdapter::addModifier(
        *weapon, "weapon.equipment.mana", eve::weapon::WeaponAttributeAdapter::manaAttribute,
        std::string(eve::attributes::source::equipment), AttributeOperation::Add, 5.0,
        eve::attributes::priority::equipment);
    REQUIRE(weaponModifier.ok());
    checkSharedModifierContract(weapon->attributes()->values.modifierAt(0));
    auto weaponProject = eve::weapon::WeaponAttributeAdapter::project(*weapon);
    REQUIRE(weaponProject.ok());
    CHECK_EQ(weapon->state()->resource.value, 15.0f);
    auto consume = eve::weapon::WeaponAttributeAdapter::consumeTriggerResource(*weapon);
    REQUIRE(consume.ok());
    CHECK_EQ(weapon->state()->resource.value, 12.0f);
    weapon->release();

    auto* unit = eve::rts::Unit::createUnit();
    REQUIRE(unit != nullptr);
    unit->motion()->speed = 7.0f;
    auto unitEnsure = eve::rts::RTSUnitAttributeAdapter::ensure(*unit);
    REQUIRE(unitEnsure.ok());
    auto unitModifier = eve::rts::RTSUnitAttributeAdapter::addModifier(
        *unit, "rts.equipment.attack", eve::rts::RTSUnitAttributeAdapter::attackAttribute,
        std::string(eve::attributes::source::equipment), AttributeOperation::Add, 4.0,
        eve::attributes::priority::equipment);
    REQUIRE(unitModifier.ok());
    checkSharedModifierContract(unit->attributes()->values.modifierAt(0));
    auto unitAttack = eve::rts::RTSUnitAttributeAdapter::read(
        *unit, eve::rts::RTSUnitAttributeAdapter::attackAttribute);
    REQUIRE(unitAttack.ok());
    CHECK_EQ(unitAttack.value(), 4.0);
    auto unsupported = eve::rts::RTSUnitAttributeAdapter::read(*unit, "speed");
    CHECK(!unsupported.ok());
    unsupported.ignore("motion remains domain-owned");
    CHECK_EQ(unit->motion()->speed, 7.0f);
    unit->release();
}
