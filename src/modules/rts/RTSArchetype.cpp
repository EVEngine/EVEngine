#include "rts/RTSArchetype.h"

#include "common/Value.h"
#include "weapon/WeaponTypes.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace eve::rts {
namespace {

template <typename T>
Result<T> invalid(std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message),
                                                 std::move(path), {}, "rts.archetype"));
}

const Value* field(const Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

Result<Value::Object> definitionObject(definitions::DefinitionRegistry& registry, std::string_view type,
                                       const LogicalId& id) {
    if (!id.isValid() || id.name().empty())
        return invalid<Value::Object>("RTS root requires a logical definition", std::string(type) + ".definition");
    auto resolved = registry.resolve(std::string(type), std::string(id.name()));
    if (!resolved) return Result<Value::Object>::failure(resolved.status());
    auto parsed = Value::fromJson(resolved.value().get().json);
    if (!parsed) return Result<Value::Object>::failure(parsed.status());
    auto value = std::move(parsed).takeValue();
    const auto* object = value.getIf<Value::Object>();
    if (object == nullptr)
        return invalid<Value::Object>("RTS definition must be a JSON object", std::string(type) + ".json");
    return Result<Value::Object>::success(*object);
}

Result<float> number(const Value::Object& object, std::string_view name, float fallback,
                     float minimum = -std::numeric_limits<float>::max()) {
    const Value* source = field(object, name);
    double value = fallback;
    if (source != nullptr) {
        if (const auto* real = source->getIf<double>()) value = *real;
        else if (const auto* integer = source->getIf<std::int64_t>()) value = static_cast<double>(*integer);
        else return invalid<float>("RTS archetype numeric field must be a number", std::string(name));
    }
    if (!std::isfinite(value) || value < minimum || value > std::numeric_limits<float>::max())
        return invalid<float>("RTS archetype numeric field is outside its valid range", std::string(name));
    return Result<float>::success(static_cast<float>(value));
}

Result<int> integer(const Value::Object& object, std::string_view name, int fallback, int minimum = 0) {
    const Value* source = field(object, name);
    std::int64_t value = fallback;
    if (source != nullptr) {
        if (const auto* exact = source->getIf<std::int64_t>()) value = *exact;
        else return invalid<int>("RTS archetype integer field must be an integer", std::string(name));
    }
    if (value < minimum || value > std::numeric_limits<int>::max())
        return invalid<int>("RTS archetype integer field is outside its valid range", std::string(name));
    return Result<int>::success(static_cast<int>(value));
}

Result<bool> boolean(const Value::Object& object, std::string_view name, bool fallback) {
    const Value* source = field(object, name);
    if (source == nullptr) return Result<bool>::success(fallback);
    const auto* value = source->getIf<bool>();
    if (value == nullptr) return invalid<bool>("RTS archetype boolean field must be boolean", std::string(name));
    return Result<bool>::success(*value);
}

Result<std::string> text(const Value::Object& object, std::string_view name, std::string fallback = {}) {
    const Value* source = field(object, name);
    if (source == nullptr) return Result<std::string>::success(std::move(fallback));
    const auto* value = source->getIf<std::string>();
    if (value == nullptr) return invalid<std::string>("RTS archetype text field must be a string", std::string(name));
    return Result<std::string>::success(*value);
}

Result<TagSet> tags(const Value::Object& object, std::string_view name) {
    TagSet result;
    const Value* source = field(object, name);
    if (source == nullptr) return Result<TagSet>::success(std::move(result));
    const auto* array = source->getIf<Value::Array>();
    if (array == nullptr) return invalid<TagSet>("RTS archetype tags must be an array", std::string(name));
    for (std::size_t index = 0; index < array->size(); ++index) {
        const auto* value = (*array)[index].getIf<std::string>();
        if (value == nullptr)
            return invalid<TagSet>("RTS archetype tag must be a string",
                                   std::string(name) + "[" + std::to_string(index) + "]");
        auto added = result.add(*value);
        if (!added) return Result<TagSet>::failure(added.status());
    }
    return Result<TagSet>::success(std::move(result));
}

}  // namespace

Result<void> RTSArchetypeMaterializer::apply(definitions::DefinitionRegistry& registry, Unit& unit,
                                               const ArchetypeWeaponFactory& weaponFactory) {
    auto objectResult = definitionObject(registry, "unit", unit.definition()->id);
    if (!objectResult) return Result<void>::failure(objectResult.status());
    const auto object = std::move(objectResult).takeValue();

    auto health = number(object, "health", 1.0f, 0.0f);
    auto speed = number(object, "speed", 1.0f, 0.0f);
    auto radius = number(object, "radius", 0.5f, 0.0f);
    auto sight = number(object, "sightRange", 0.0f, 0.0f);
    auto detection = number(object, "detectionRange", 0.0f, 0.0f);
    auto radar = number(object, "radarRange", 0.0f, 0.0f);
    auto jamming = number(object, "jammingRange", 0.0f, 0.0f);
    auto range = number(object, "attackRange", 0.0f, 0.0f);
    auto role = text(object, "role");
    auto weaponType = text(object, "weaponType");
    auto targetTags = tags(object, "targetTags");
    if (!health || !speed || !radius || !sight || !detection || !radar || !jamming || !range || !role ||
        !weaponType || !targetTags)
        return invalid<void>("RTS unit definition contains an invalid core field", "unit");

    auto capacity = number(object, "cargoCapacity", role.value() == "worker" ? 10.0f : 0.0f, 0.0f);
    auto gather = number(object, "gatherRate", role.value() == "worker" ? 5.0f : 0.0f, 0.0f);
    auto buildRate = number(object, "buildRate", 1.0f, 0.0f);
    auto repairRate = number(object, "repairRate", 0.0f, 0.0f);
    auto captureRate = number(object, "captureRate", 0.0f, 0.0f);
    auto shield = number(object, "shieldCapacity", 0.0f, 0.0f);
    auto shieldRegen = number(object, "shieldRegenRate", 0.0f, 0.0f);
    auto shieldDelay = number(object, "shieldRegenDelay", 0.0f, 0.0f);
    auto transport = integer(object, "transportCapacity", 0);
    auto airborne = text(object, "targetDomain");
    auto suppression = number(object, "suppressionCapacity", 0.0f, 0.0f);
    auto suppressionRecovery = number(object, "suppressionRecoveryRate", 0.0f, 0.0f);
    auto suppressedSpeed = number(object, "suppressedSpeedFactor", 1.0f, 0.0f);
    auto suppressedDamage = number(object, "suppressedDamageFactor", 1.0f, 0.0f);
    auto retreatEnabled = boolean(object, "suppressionRetreatEnabled", false);
    auto retreatThreshold = number(object, "suppressionRetreatThreshold", 0.8f, 0.0f);
    auto retreatDistance = number(object, "suppressionRetreatDistance", 6.0f, 0.0f);
    auto moraleRange = number(object, "moraleAuraRange", 0.0f, 0.0f);
    auto moraleFactor = number(object, "moraleAuraSuppressionFactor", 1.0f, 0.0f);
    auto moraleRecovery = number(object, "moraleAuraRecoveryBonus", 0.0f, 0.0f);
    auto veteranThreshold = number(object, "veteranThreshold", 0.0f, 0.0f);
    auto eliteThreshold = number(object, "eliteThreshold", 0.0f, 0.0f);
    auto veteranDamage = number(object, "veteranDamageFactor", 1.0f, 0.0f);
    auto eliteDamage = number(object, "eliteDamageFactor", 1.0f, 0.0f);
    auto veteranHealth = number(object, "veteranHealthFactor", 1.0f, 0.0f);
    auto eliteHealth = number(object, "eliteHealthFactor", 1.0f, 0.0f);
    auto commandRange = number(object, "commandRange", 0.0f, 0.0f);
    auto commandCapacity = integer(object, "commandCapacity", 0);
    auto commandCost = integer(object, "commandCost", 1);
    auto commandPriority = integer(object, "commandPriority", 0);
    auto commandJamming = number(object, "commandJammingRange", 0.0f, 0.0f);
    auto commandRequired = boolean(object, "requiresCommand", false);
    auto relayUplink = boolean(object, "commandRelayRequiresUplink", false);
    auto outOfCommandSpeed = number(object, "outOfCommandSpeedFactor", 1.0f, 0.0f);
    auto outOfCommandDamage = number(object, "outOfCommandDamageFactor", 1.0f, 0.0f);
    auto ammoCapacity = number(object, "ammoSupplyCapacity", 0.0f, 0.0f);
    auto ammoRange = number(object, "ammoSupplyRange", 0.0f, 0.0f);
    auto ammoRate = number(object, "ammoSupplyRate", 0.0f, 0.0f);
    auto ammoRelay = boolean(object, "ammoRelayEnabled", false);
    auto autoResupply = boolean(object, "autoResupplyEnabled", false);
    auto autoThreshold = number(object, "autoResupplyThreshold", 0.5f, 0.0f);
    auto reserveAmmo = number(object, "reserveAmmoCapacity", 0.0f, 0.0f);
    auto supplyPriority = number(object, "supplyPriority", 1.0f, 0.0f);
    auto firingTurnRate = number(object, "turretTurnRate", 0.0f, 0.0f);
    auto firingArc = number(object, "firingArc", 0.01f, 0.0f);
    auto firingHeight = number(object, "firingHeight", 1.0f, 0.000001f);
    auto targetHeight = number(object, "targetHeight", 1.0f, 0.000001f);
    if (!capacity || !gather || !buildRate || !repairRate || !captureRate || !shield || !shieldRegen ||
        !shieldDelay || !transport || !airborne || !suppression || !suppressionRecovery ||
        !suppressedSpeed || !suppressedDamage || !retreatEnabled || !retreatThreshold || !retreatDistance ||
        !moraleRange || !moraleFactor || !moraleRecovery || !veteranThreshold || !eliteThreshold ||
        !veteranDamage || !eliteDamage || !veteranHealth || !eliteHealth || !commandRange ||
        !commandCapacity || !commandCost || !commandPriority || !commandJamming || !commandRequired ||
        !relayUplink || !outOfCommandSpeed || !outOfCommandDamage || !ammoCapacity || !ammoRange ||
        !ammoRate || !ammoRelay || !autoResupply || !autoThreshold || !reserveAmmo || !supplyPriority ||
        !firingTurnRate || !firingArc || !firingHeight || !targetHeight)
        return invalid<void>("RTS unit definition contains an invalid capability field", "unit");

    weapon::WeaponEntity* weapon = nullptr;
    if (!weaponType.value().empty()) {
        if (!weaponFactory)
            return invalid<void>("RTS unit definition requires a weapon factory", "unit.weaponType");
        auto created = weaponFactory(weaponType.value(), unit.identity()->subject.persistentId());
        if (!created) return Result<void>::failure(created.status());
        weapon = std::move(created).takeValue();
    }

    unit.motion()->speed = speed.value();
    unit.motion()->airborne = airborne.value() == "air";
    unit.crowd()->radius = radius.value();
    unit.vision()->sightRange = sight.value();
    unit.vision()->detectionRange = detection.value();
    unit.vision()->radarRange = radar.value();
    unit.vision()->jammingRange = jamming.value();
    unit.combat()->acquisitionRange = std::max(sight.value(), range.value());
    unit.combat()->engagementRange = range.value();
    unit.combat()->leashRange = std::max(sight.value(), range.value()) * 1.5f;
    unit.durability()->state.subject = unit.identity()->subject;
    unit.durability()->state.health = health.value();
    unit.durability()->state.maxHealth = health.value();
    unit.durability()->alive = health.value() > 0.0f;
    unit.worker()->capacity = capacity.value();
    unit.worker()->gatherRate = gather.value();
    unit.worker()->buildRate = buildRate.value();
    unit.worker()->repairRate = repairRate.value();
    unit.worker()->autoAssign = role.value() == "worker";
    unit.capture()->rate = captureRate.value();
    unit.shield()->capacity = shield.value();
    unit.shield()->value = shield.value();
    unit.shield()->regenRate = shieldRegen.value();
    unit.shield()->regenDelay = shieldDelay.value();
    unit.containment()->capacity = static_cast<std::size_t>(transport.value());
    unit.morale()->capacity = suppression.value();
    unit.morale()->recoveryRate = suppressionRecovery.value();
    unit.morale()->suppressedSpeedFactor = suppressedSpeed.value();
    unit.morale()->suppressedDamageFactor = suppressedDamage.value();
    unit.morale()->retreatEnabled = retreatEnabled.value();
    unit.morale()->retreatThreshold = retreatThreshold.value();
    unit.morale()->retreatDistance = retreatDistance.value();
    unit.morale()->auraRange = moraleRange.value();
    unit.morale()->auraSuppressionFactor = moraleFactor.value();
    unit.morale()->auraRecoveryBonus = moraleRecovery.value();
    unit.veterancy()->veteranThreshold = veteranThreshold.value();
    unit.veterancy()->eliteThreshold = eliteThreshold.value();
    unit.veterancy()->veteranDamageFactor = veteranDamage.value();
    unit.veterancy()->eliteDamageFactor = eliteDamage.value();
    unit.veterancy()->veteranHealthFactor = veteranHealth.value();
    unit.veterancy()->eliteHealthFactor = eliteHealth.value();
    unit.command()->range = commandRange.value();
    unit.command()->capacity = commandCapacity.value();
    unit.command()->cost = commandCost.value();
    unit.command()->priority = commandPriority.value();
    unit.command()->jammingRange = commandJamming.value();
    unit.command()->requiresCommand = commandRequired.value();
    unit.command()->relayRequiresUplink = relayUplink.value();
    unit.command()->outOfCommandSpeedFactor = outOfCommandSpeed.value();
    unit.command()->outOfCommandDamageFactor = outOfCommandDamage.value();
    unit.supply()->capacity = std::max(ammoCapacity.value(), reserveAmmo.value());
    unit.supply()->stock = ammoCapacity.value();
    unit.supply()->range = ammoRange.value();
    unit.supply()->transferRate = ammoRate.value();
    unit.supply()->relayEnabled = ammoRelay.value();
    unit.supply()->autoDispatch = autoResupply.value();
    unit.supply()->autoThreshold = autoThreshold.value();
    unit.supply()->priority = supplyPriority.value();
    unit.combat()->turnRateDegrees = firingTurnRate.value();
    unit.combat()->aimToleranceDegrees = firingArc.value();
    unit.combat()->firingHeight = firingHeight.value();
    unit.combat()->targetHeight = targetHeight.value();
    unit.tags()->values = std::move(targetTags).takeValue();
    if (weapon != nullptr) {
        auto link = WeaponLink::bind(ecs::handle_of(weapon));
        if (!link) return Result<void>::failure(link.status());
        unit.weapon()->link = std::move(link).takeValue();
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> RTSArchetypeMaterializer::apply(definitions::DefinitionRegistry& registry, Building& building,
                                               const ArchetypeWeaponFactory& weaponFactory) {
    auto objectResult = definitionObject(registry, "building", building.definition()->id);
    if (!objectResult) return Result<void>::failure(objectResult.status());
    const auto object = std::move(objectResult).takeValue();
    auto health = number(object, "health", 1.0f, 0.0f);
    auto radius = number(object, "radius", 0.5f, 0.0f);
    auto buildTime = number(object, "buildTime", 0.0f, 0.0f);
    auto sight = number(object, "sightRange", 0.0f, 0.0f);
    auto weaponType = text(object, "weaponType");
    auto garrison = integer(object, "garrisonCapacity", 0);
    auto capturable = boolean(object, "capturable", false);
    auto captureTime = number(object, "captureTime", 5.0f, 0.000001f);
    auto powerProduced = number(object, "powerProvided", 0.0f, 0.0f);
    auto powerUsed = number(object, "powerUsed", 0.0f, 0.0f);
    auto powerPriority = integer(object, "powerPriority", 0);
    auto influence = number(object, "buildInfluenceRadius", 0.0f, 0.0f);
    auto incomeResource = text(object, "incomeResource");
    auto incomeRate = number(object, "incomeRate", 0.0f, 0.0f);
    auto repairCost = number(object, "repairCostPerHealth", 0.0f, 0.0f);
    auto repairResource = text(object, "repairResource", "minerals");
    auto shield = number(object, "shieldCapacity", 0.0f, 0.0f);
    auto shieldRegen = number(object, "shieldRegenRate", 0.0f, 0.0f);
    auto shieldDelay = number(object, "shieldRegenDelay", 0.0f, 0.0f);
    auto garrisonBonus = number(object, "garrisonDamageBonus", 0.0f, 0.0f);
    auto ammoCapacity = number(object, "ammoSupplyCapacity", 0.0f, 0.0f);
    auto ammoRange = number(object, "ammoSupplyRange", 0.0f, 0.0f);
    auto ammoRate = number(object, "ammoSupplyRate", 0.0f, 0.0f);
    auto ammoResource = text(object, "ammoProductionResource");
    auto ammoCost = number(object, "ammoProductionCost", 0.0f, 0.0f);
    auto ammoProduction = number(object, "ammoProductionRate", 0.0f, 0.0f);
    auto commandRange = number(object, "commandRange", 0.0f, 0.0f);
    auto commandCapacity = integer(object, "commandCapacity", 0);
    auto commandJamming = number(object, "commandJammingRange", 0.0f, 0.0f);
    auto turretTurn = number(object, "turretTurnRate", 0.0f, 0.0f);
    auto firingArc = number(object, "firingArc", 0.01f, 0.0f);
    auto firingHeight = number(object, "firingHeight", 2.0f, 0.000001f);
    auto targetHeight = number(object, "targetHeight", 1.5f, 0.000001f);
    if (!health || !radius || !buildTime || !sight || !weaponType || !garrison || !capturable || !captureTime ||
        !powerProduced || !powerUsed || !powerPriority || !influence || !incomeResource || !incomeRate ||
        !repairCost || !repairResource || !shield || !shieldRegen || !shieldDelay || !garrisonBonus ||
        !ammoCapacity || !ammoRange || !ammoRate || !ammoResource || !ammoCost || !ammoProduction ||
        !commandRange || !commandCapacity || !commandJamming || !turretTurn || !firingArc ||
        !firingHeight || !targetHeight)
        return invalid<void>("RTS building definition contains an invalid field", "building");

    weapon::WeaponEntity* weapon = nullptr;
    if (!weaponType.value().empty()) {
        if (!weaponFactory)
            return invalid<void>("RTS building definition requires a weapon factory", "building.weaponType");
        auto created = weaponFactory(weaponType.value(), building.identity()->subject.persistentId());
        if (!created) return Result<void>::failure(created.status());
        weapon = std::move(created).takeValue();
    }

    building.placement()->placed = true;
    building.construction()->buildTimeSeconds = buildTime.value();
    building.integrity()->state.subject = building.identity()->subject;
    building.integrity()->state.health = health.value();
    building.integrity()->state.maxHealth = health.value();
    building.integrity()->alive = health.value() > 0.0f;
    building.integrity()->repairCostPerHealth = repairCost.value();
    building.integrity()->repairResource = std::move(repairResource).takeValue();
    building.shield()->capacity = shield.value();
    building.shield()->value = shield.value();
    building.shield()->regenRate = shieldRegen.value();
    building.shield()->regenDelay = shieldDelay.value();
    building.vision()->sightRange = sight.value();
    building.garrison()->capacity = static_cast<std::size_t>(garrison.value());
    building.garrison()->damageBonusPerOccupant = garrisonBonus.value();
    building.capture()->capturable = capturable.value();
    building.capture()->durationSeconds = captureTime.value();
    building.infrastructure()->powerProduced = powerProduced.value();
    building.infrastructure()->powerConsumed = powerUsed.value();
    building.infrastructure()->powerPriority = powerPriority.value();
    building.infrastructure()->buildInfluenceRadius = influence.value();
    building.infrastructure()->incomeResource = std::move(incomeResource).takeValue();
    building.infrastructure()->incomeRate = incomeRate.value();
    building.supply()->capacity = ammoCapacity.value();
    building.supply()->stock = ammoCapacity.value();
    building.supply()->range = ammoRange.value();
    building.supply()->transferRate = ammoRate.value();
    building.supply()->productionResource = std::move(ammoResource).takeValue();
    building.supply()->productionCostPerRound = static_cast<std::int64_t>(std::ceil(ammoCost.value()));
    building.supply()->productionRate = ammoProduction.value();
    building.command()->range = commandRange.value();
    building.command()->capacity = commandCapacity.value();
    building.command()->jammingRange = commandJamming.value();
    building.command()->active = commandRange.value() > 0.0f;
    building.combat()->firingHeight = firingHeight.value();
    building.combat()->targetHeight = targetHeight.value();
    building.dropoff()->radius = radius.value();
    if (const auto dropoff = boolean(object, "dropoff", false); dropoff && dropoff.value())
        building.dropoff()->acceptedResources.push_back("minerals");
    if (weapon != nullptr) {
        auto link = WeaponLink::bind(ecs::handle_of(weapon));
        if (!link) return Result<void>::failure(link.status());
        building.weapon()->link = std::move(link).takeValue();
        building.combat()->acquisitionRange = sight.value();
        building.combat()->engagementRange = sight.value();
        building.combat()->turnRateDegrees = turretTurn.value();
        building.combat()->aimToleranceDegrees = firingArc.value();
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

}  // namespace eve::rts
