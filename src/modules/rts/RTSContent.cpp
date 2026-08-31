#include "rts/RTSContent.h"

#include "common/Value.h"

#include <algorithm>
#include <array>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace eve::rts {
namespace {

template <typename T>
Result<T> invalid(std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message),
                                                 std::move(path), {}, "rts.content"));
}

struct StagedDefinition {
    std::string type;
    std::string id;
    Value       value;
};

const Value* field(const Value::Object& object, std::string_view name) {
    const auto it = object.find(std::string(name));
    return it == object.end() ? nullptr : &it->second;
}

std::string text(const Value::Object& object, std::string_view name) {
    const auto* value = field(object, name);
    const auto* result = value == nullptr ? nullptr : value->getIf<std::string>();
    return result == nullptr ? std::string{} : *result;
}

double number(const Value::Object& object, std::string_view name, double fallback) {
    const auto* value = field(object, name);
    if (value == nullptr) return fallback;
    if (const auto* real = value->getIf<double>()) return *real;
    if (const auto* integer = value->getIf<std::int64_t>()) return static_cast<double>(*integer);
    return fallback;
}

void normalizeWeapon(Value::Object& object) {
    if (field(object, "kind") == nullptr) object.emplace("kind", Value("ranged"));
    const double speed = number(object, "projectileSpeed", 0.0);
    if (field(object, "logic") == nullptr) object.emplace("logic", Value(speed > 0.0 ? "projectile" : "hitscan"));
    if (field(object, "ammo") == nullptr) {
        const int magazine = std::max(1, static_cast<int>(number(object, "magazineSize", 0.0)));
        const int reserve = number(object, "magazineSize", 0.0) > 0.0 ? 0 : -1;
        object.emplace("ammo", Value(Value::Object{{"mag", Value(magazine)},
                                                    {"reload", Value(number(object, "reloadTime", 1.0))},
                                                    {"reserve", Value(reserve)}}));
    }
    if (field(object, "projectile") == nullptr && speed > 0.0) {
        object.emplace("projectile", Value(Value::Object{{"aoe", Value(number(object, "splashRadius", 0.0))},
                                                          {"pelletCount", Value(1)},
                                                          {"pelletSpread", Value(0.0)},
                                                          {"speed", Value(speed)},
                                                          {"type", Value("rts")}}));
    }
    const std::string targets = text(object, "targets");
    if (field(object, "targetsGround") == nullptr)
        object.emplace("targetsGround", Value(targets != "air"));
    if (field(object, "targetsAir") == nullptr)
        object.emplace("targetsAir", Value(targets != "ground"));
}

bool available(const definitions::DefinitionRegistry& registry, const std::vector<StagedDefinition>& staged,
               std::string_view type, std::string_view id) {
    if (id.empty()) return true;
    if (std::any_of(staged.begin(), staged.end(), [&](const auto& value) {
            return value.type == type && value.id == id;
        })) return true;
    auto resolved = registry.resolve(std::string(type), std::string(id));
    if (resolved) return true;
    resolved.ignore("optional RTS content cross-reference lookup");
    return false;
}

Result<void> requireReference(const definitions::DefinitionRegistry& registry,
                              const std::vector<StagedDefinition>& staged, const StagedDefinition& owner,
                              const Value::Object& object, std::string_view member, std::string_view targetType) {
    const auto* source = field(object, member);
    if (source == nullptr) return Result<void>::success(Status::success(StatusCode::NoOp));
    const auto* id = source->getIf<std::string>();
    if (id == nullptr)
        return invalid<void>("RTS content reference must be a string", owner.type + "." + owner.id + "." +
                                                                            std::string(member));
    if (!available(registry, staged, targetType, *id))
        return invalid<void>("RTS content references an unknown " + std::string(targetType) + " '" + *id + "'",
                             owner.type + "." + owner.id + "." + std::string(member));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

}  // namespace

Result<ContentImportReceipt> RTSContentLoader::load(definitions::DefinitionRegistry& registry,
                                                     std::string_view json) {
    auto parsed = Value::fromJson(json);
    if (!parsed) return Result<ContentImportReceipt>::failure(parsed.status());
    Value root = std::move(parsed).takeValue();
    const auto* object = root.getIf<Value::Object>();
    if (object == nullptr) return invalid<ContentImportReceipt>("RTS content root must be an object", "root");
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 7> arrays{{
        {"weapons", "weapon"}, {"units", "unit"}, {"buildings", "building"}, {"upgrades", "upgrade"},
        {"statusEffects", "effect"}, {"abilities", "ability"}, {"damageMultipliers", "damage_multiplier"},
    }};
    std::vector<StagedDefinition> staged;
    std::set<std::pair<std::string, std::string>> keys;
    for (const auto& [member, type] : arrays) {
        const auto* value = field(*object, member);
        if (value == nullptr) continue;
        const auto* entries = value->getIf<Value::Array>();
        if (entries == nullptr)
            return invalid<ContentImportReceipt>("RTS content collection must be an array", std::string(member));
        for (std::size_t index = 0; index < entries->size(); ++index) {
            const auto* item = (*entries)[index].getIf<Value::Object>();
            if (item == nullptr)
                return invalid<ContentImportReceipt>("RTS content item must be an object",
                                                     std::string(member) + "[" + std::to_string(index) + "]");
            Value::Object normalized = *item;
            std::string id = text(normalized, "id");
            if (type == "damage_multiplier" && id.empty()) {
                const std::string damage = text(normalized, "damageType");
                const std::string armor = text(normalized, "armorType");
                if (!damage.empty() && !armor.empty()) {
                    id = damage + "." + armor;
                    normalized.emplace("id", Value(id));
                }
            }
            if (id.empty())
                return invalid<ContentImportReceipt>("RTS content item requires a non-empty id",
                                                     std::string(member) + "[" + std::to_string(index) + "].id");
            if (!keys.emplace(std::string(type), id).second)
                return invalid<ContentImportReceipt>("RTS content pack contains a duplicate definition",
                                                     std::string(type) + ":" + id);
            if (type == "weapon") normalizeWeapon(normalized);
            staged.push_back({std::string(type), std::move(id), Value(std::move(normalized))});
        }
    }
    for (const auto& definition : staged) {
        const auto* item = definition.value.getIf<Value::Object>();
        if (definition.type == "unit") {
            for (const auto& [member, target] : {std::pair{"weaponType", "weapon"},
                                                 {"producer", "building"}, {"prerequisite", "building"}}) {
                auto checked = requireReference(registry, staged, definition, *item, member, target);
                if (!checked) return Result<ContentImportReceipt>::failure(checked.status());
            }
        } else if (definition.type == "building") {
            for (const auto& [member, target] : {std::pair{"weaponType", "weapon"},
                                                 {"prerequisite", "building"}}) {
                auto checked = requireReference(registry, staged, definition, *item, member, target);
                if (!checked) return Result<ContentImportReceipt>::failure(checked.status());
            }
        } else if (definition.type == "upgrade") {
            for (const auto& [member, target] : {std::pair{"producer", "building"},
                                                 {"prerequisiteBuilding", "building"},
                                                 {"prerequisiteUpgrade", "upgrade"},
                                                 {"targetUnit", "unit"}, {"targetBuilding", "building"}}) {
                auto checked = requireReference(registry, staged, definition, *item, member, target);
                if (!checked) return Result<ContentImportReceipt>::failure(checked.status());
            }
        } else if (definition.type == "ability") {
            for (const auto& [member, target] : {std::pair{"casterUnit", "unit"},
                                                 {"statusEffect", "effect"}}) {
                auto checked = requireReference(registry, staged, definition, *item, member, target);
                if (!checked) return Result<ContentImportReceipt>::failure(checked.status());
            }
        }
    }

    const std::string rollback = registry.snapshotJson();
    ContentImportReceipt receipt;
    for (const auto& definition : staged) {
        auto encoded = definition.value.toJson();
        if (!encoded) {
            auto restored = registry.restoreJson(rollback);
            if (!restored) return Result<ContentImportReceipt>::failure(restored.status());
            return Result<ContentImportReceipt>::failure(encoded.status());
        }
        auto existing = registry.handle(definition.type, definition.id);
        const bool replace = existing.ok();
        if (!replace) existing.ignore("RTS content insert lookup");
        auto published = replace ? registry.replace(definition.type, definition.id, 1, encoded.value())
                                 : registry.insert(definition.type, definition.id, 1, encoded.value());
        if (!published) {
            const Status failure = published.status();
            auto restored = registry.restoreJson(rollback);
            if (!restored) return Result<ContentImportReceipt>::failure(restored.status());
            return Result<ContentImportReceipt>::failure(failure);
        }
        replace ? ++receipt.replaced : ++receipt.inserted;
    }
    return Result<ContentImportReceipt>::success(receipt,
        Status::success(staged.empty() ? StatusCode::NoOp : StatusCode::Applied));
}

}  // namespace eve::rts
