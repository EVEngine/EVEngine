#include "rts/RTSTech.h"

#include "common/Value.h"
#include "rts/RTSTypes.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace eve::rts {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, "rts.tech"));
}

const Value* field(const Value::Object& object, std::string_view name) {
    const auto it = object.find(std::string(name));
    return it == object.end() ? nullptr : &it->second;
}

std::string text(const Value::Object& object, std::string_view name) {
    const auto* value = field(object, name);
    const auto* string = value == nullptr ? nullptr : value->getIf<std::string>();
    return string == nullptr ? std::string{} : *string;
}

Result<double> factor(const Value::Object& object, std::string_view name) {
    const auto* value = field(object, name);
    if (value == nullptr) return Result<double>::success(1.0, Status::success(StatusCode::NoOp));
    double result = 0.0;
    if (const auto* real = value->getIf<double>()) result = *real;
    else if (const auto* integer = value->getIf<std::int64_t>()) result = static_cast<double>(*integer);
    else return failure<double>(DiagnosticCode::InvalidArgument, "RTS upgrade factor must be numeric", std::string(name));
    if (!std::isfinite(result) || result < 0.0)
        return failure<double>(DiagnosticCode::InvalidArgument, "RTS upgrade factor must be finite and non-negative",
                               std::string(name));
    return Result<double>::success(result);
}

bool contains(const std::vector<std::string>& values, std::string_view value) {
    return std::binary_search(values.begin(), values.end(), value);
}

bool insert(std::vector<std::string>& values, std::string value) {
    const auto at = std::lower_bound(values.begin(), values.end(), value);
    if (at != values.end() && *at == value) return false;
    values.insert(at, std::move(value));
    return true;
}

bool sameFaction(const FactionLink& link, const Faction& faction) { return link.resolve() == &faction; }

Result<Value::Object> upgrade(definitions::DefinitionRegistry& registry, std::string_view id) {
    auto resolved = registry.resolve("upgrade", std::string(id));
    if (!resolved) return Result<Value::Object>::failure(resolved.status());
    auto parsed = Value::fromJson(resolved.value().get().json);
    if (!parsed) return Result<Value::Object>::failure(parsed.status());
    auto value = std::move(parsed).takeValue();
    const auto* object = value.getIf<Value::Object>();
    if (object == nullptr) return failure<Value::Object>(DiagnosticCode::InvalidArgument,
                                                         "RTS upgrade definition must be an object", "upgrade");
    return Result<Value::Object>::success(*object);
}

Result<void> applyUnit(Unit& unit, std::string_view upgradeId, const Value::Object& definition) {
    const std::string target = text(definition, "targetUnit");
    if (!target.empty() && unit.definition()->id.name() != target)
        return Result<void>::success(Status::success(StatusCode::NoOp));
    if (target.empty() && !text(definition, "targetBuilding").empty())
        return Result<void>::success(Status::success(StatusCode::NoOp));
    if (contains(unit.technology()->applied, upgradeId))
        return Result<void>::success(Status::success(StatusCode::NoOp));
    auto attack = factor(definition, "attackMultiplier");
    auto health = factor(definition, "healthMultiplier");
    auto speed = factor(definition, "speedMultiplier");
    auto gather = factor(definition, "gatherMultiplier");
    auto shield = factor(definition, "shieldMultiplier");
    auto shieldRegen = factor(definition, "shieldRegenMultiplier");
    if (!attack) return Result<void>::failure(attack.status());
    if (!health) return Result<void>::failure(health.status());
    if (!speed) return Result<void>::failure(speed.status());
    if (!gather) return Result<void>::failure(gather.status());
    if (!shield) return Result<void>::failure(shield.status());
    if (!shieldRegen) return Result<void>::failure(shieldRegen.status());
    unit.combat()->upgradeDamageFactor *= static_cast<float>(attack.value());
    auto& durability = unit.durability()->state;
    if (durability.maxHealth > 0.0) {
        const double ratio = std::clamp(durability.health / durability.maxHealth, 0.0, 1.0);
        durability.maxHealth *= health.value();
        durability.health = durability.maxHealth * ratio;
    }
    unit.motion()->speed *= static_cast<float>(speed.value());
    unit.worker()->gatherRate *= static_cast<float>(gather.value());
    if (unit.shield()->capacity > 0.0f) {
        const float ratio = std::clamp(unit.shield()->value / unit.shield()->capacity, 0.0f, 1.0f);
        unit.shield()->capacity *= static_cast<float>(shield.value());
        unit.shield()->value = unit.shield()->capacity * ratio;
    }
    unit.shield()->regenRate *= static_cast<float>(shieldRegen.value());
    insert(unit.technology()->applied, std::string(upgradeId));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> applyBuilding(Building& building, std::string_view upgradeId, const Value::Object& definition) {
    const std::string target = text(definition, "targetBuilding");
    if (!target.empty() && building.definition()->id.name() != target)
        return Result<void>::success(Status::success(StatusCode::NoOp));
    if (target.empty() && !text(definition, "targetUnit").empty())
        return Result<void>::success(Status::success(StatusCode::NoOp));
    if (contains(building.technology()->applied, upgradeId))
        return Result<void>::success(Status::success(StatusCode::NoOp));
    auto health = factor(definition, "healthMultiplier");
    auto shield = factor(definition, "shieldMultiplier");
    auto shieldRegen = factor(definition, "shieldRegenMultiplier");
    if (!health) return Result<void>::failure(health.status());
    if (!shield) return Result<void>::failure(shield.status());
    if (!shieldRegen) return Result<void>::failure(shieldRegen.status());
    auto& integrity = building.integrity()->state;
    if (integrity.maxHealth > 0.0) {
        const double ratio = std::clamp(integrity.health / integrity.maxHealth, 0.0, 1.0);
        integrity.maxHealth *= health.value();
        integrity.health = integrity.maxHealth * ratio;
    }
    if (building.shield()->capacity > 0.0f) {
        const float ratio = std::clamp(building.shield()->value / building.shield()->capacity, 0.0f, 1.0f);
        building.shield()->capacity *= static_cast<float>(shield.value());
        building.shield()->value = building.shield()->capacity * ratio;
    }
    building.shield()->regenRate *= static_cast<float>(shieldRegen.value());
    insert(building.technology()->applied, std::string(upgradeId));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

}  // namespace

Result<std::size_t> TechnologySystem::step(definitions::DefinitionRegistry& registry) {
    std::size_t processed = 0;
    auto buildings = ecs::View<Building, Building::Identity, Building::Faction, Building::Production>();
    for (auto it = buildings.begin(); it != buildings.end(); ++it) {
        auto [identity, factionLink, production] = *it;
        auto* faction = dynamic_cast<Faction*>(factionLink->link.resolve());
        if (faction == nullptr) continue;
        for (const auto& task : production->values.completed("research")) {
            if (contains(faction->technology()->consumedTasks, task.id)) continue;
            auto definition = upgrade(registry, task.product);
            if (!definition) return Result<std::size_t>::failure(definition.status());
            const std::string prerequisite = text(definition.value(), "prerequisiteUpgrade");
            if (!prerequisite.empty() && !contains(faction->technology()->unlocked, prerequisite))
                return failure<std::size_t>(DiagnosticCode::PreconditionViolation,
                                            "completed RTS research lacks its prerequisite", task.product);
            insert(faction->technology()->unlocked, task.product);
            insert(faction->technology()->consumedTasks, task.id);
            ++processed;
        }
    }
    auto factions = ecs::View<Faction, Faction::Identity, Faction::Technology>();
    for (auto fit = factions.begin(); fit != factions.end(); ++fit) {
        auto [identity, technology] = *fit;
        auto* faction = dynamic_cast<Faction*>(ecs::try_get(identity->self));
        if (faction == nullptr) continue;
        for (const auto& upgradeId : technology->unlocked) {
            auto definition = upgrade(registry, upgradeId);
            if (!definition) return Result<std::size_t>::failure(definition.status());
            auto allUnits = ecs::View<Unit, Unit::Identity, Unit::Faction>();
            for (auto it = allUnits.begin(); it != allUnits.end(); ++it) {
                auto [unitIdentity, factionLink] = *it;
                auto* unit = dynamic_cast<Unit*>(ecs::try_get(unitIdentity->self));
                if (unit != nullptr && sameFaction(factionLink->link, *faction)) {
                    auto applied = applyUnit(*unit, upgradeId, definition.value());
                    if (!applied) return Result<std::size_t>::failure(applied.status());
                }
            }
            for (auto it = buildings.begin(); it != buildings.end(); ++it) {
                auto [buildingIdentity, factionLink, production] = *it;
                auto* building = dynamic_cast<Building*>(ecs::try_get(buildingIdentity->self));
                if (building != nullptr && sameFaction(factionLink->link, *faction)) {
                    auto applied = applyBuilding(*building, upgradeId, definition.value());
                    if (!applied) return Result<std::size_t>::failure(applied.status());
                }
            }
        }
    }
    return Result<std::size_t>::success(processed,
        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

}  // namespace eve::rts
