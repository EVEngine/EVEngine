#include "rpg/Skill.h"

#include "common/Json.h"
#include "common/Value.h"
#include "rpg/SkillConditionCodec.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace eve::rpg {

using eve::json::Value;

namespace {

eve::definitions::DefinitionRegistry& canonicalSkillRegistry() {
    static eve::definitions::DefinitionRegistry registry;
    return registry;
}

eve::Result<std::string> canonicalJson(const SkillDefinition& def) {
    eve::Value::Object object;
    object.emplace("id", eve::Value(def.id));
    object.emplace("cooldown", eve::Value(static_cast<double>(def.cooldown)));
    object.emplace("castTime", eve::Value(static_cast<double>(def.castTime)));
    object.emplace("targetType", eve::Value(def.targetType));

    auto condition = encodeSkillCondition(def.castCondition);
    if (!condition) return eve::Result<std::string>::failure(condition.status());
    object.emplace("castCondition", std::move(condition).takeValue());

    eve::Value::Array effects;
    for (const auto& effect : def.grantedEffects) effects.emplace_back(effect);
    object.emplace("grantedEffects", eve::Value(std::move(effects)));

    eve::Value::Array tags;
    for (const auto& tag : def.tags) tags.emplace_back(tag);
    object.emplace("tags", eve::Value(std::move(tags)));

    eve::Value::Object extra;
    for (const auto& [key, value] : def.extra) extra.emplace(key, eve::Value(value));
    object.emplace("extra", eve::Value(std::move(extra)));

    eve::Value::Array costs;
    if (def.cost) {
        for (const auto& item : def.cost->items()) {
            costs.emplace_back(eve::Value(eve::Value::Object{
                {"attribute", eve::Value(item.resource.value())},
                {"amount", eve::Value(item.amount.value())},
            }));
        }
    }
    object.emplace("costs", eve::Value(std::move(costs)));
    return eve::Value(std::move(object)).toJson();
}

}  // namespace

bool SkillDefinition::hasTag(const std::string &tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

std::unordered_map<std::string, SkillDefinition> &table() {
    static std::unordered_map<std::string, SkillDefinition> t;
    return t;
}

eve::definitions::DefinitionRegistry& SkillRegistry::definitionRegistry() { return canonicalSkillRegistry(); }

void SkillRegistry::registerSkill(const SkillDefinition &def) {
    if (def.id.empty()) return;
    auto json = canonicalJson(def);
    if (!json) {
        json.ignore("legacy skill registration cannot serialize invalid definition");
        return;
    }
    auto&                                          registry = canonicalSkillRegistry();
    auto                                           existing = registry.resolve("rpg.skill", def.id);
    eve::Result<eve::definition::DefinitionHandle> result =
        existing.ok() ? registry.replace("rpg.skill", def.id, 1, std::move(json).takeValue())
        : existing.status().code() == eve::StatusCode::NotFound
            ? registry.insert("rpg.skill", def.id, 1, std::move(json).takeValue())
            : eve::Result<eve::definition::DefinitionHandle>::failure(existing.status());
    if (!result) return;
    table()[def.id] = def;
}

const SkillDefinition *SkillRegistry::find(const std::string &id) {
    auto &t = table();
    auto it = t.find(id);
    return it == t.end() ? nullptr : &it->second;
}

bool SkillRegistry::remove(const std::string& id) {
    auto& registry = canonicalSkillRegistry();
    auto  result   = registry.remove("rpg.skill", id);
    if (!result) return false;
    return table().erase(id) > 0;
}

void SkillRegistry::clear() {
    auto& skills   = table();
    auto& registry = canonicalSkillRegistry();
    for (const auto& [id, unused] : skills) {
        (void)unused;
        auto result = registry.remove("rpg.skill", id);
        result.ignore("legacy SkillRegistry::clear removes canonical skill definitions");
    }
    skills.clear();
}

int SkillRegistry::count() { return int(table().size()); }

namespace {

SkillDefinition parseSkillObject(Value o, std::string* error) {
    SkillDefinition def;
    if (!o.isObject()) return def;
    def.id = o.getString("id");
    def.cooldown = o.getFloat("cooldown", 0.f);
    def.castTime = o.getFloat("castTime", 0.f);
    def.targetType = o.getString("targetType", "self");
    def.grantedEffects = o.getStringArray("grantedEffects");
    def.tags = o.getStringArray("tags");

    const Value costs = o.get("costs");
    std::vector<eve::resource::ResourceCost> costItems;
    for (size_t i = 0; i < costs.size(); ++i) {
        const Value co = costs.at(i);
        const std::string resource = co.getString("attribute");
        const double      amount   = co.getDouble("amount", 0.0);
        // Keep the legacy JSON shape, but do not silently truncate a value
        // into the canonical integral resource Amount type.
        if (resource.empty()) continue;
        if (!std::isfinite(amount) || amount <= 0.0 || std::floor(amount) != amount || amount >= std::ldexp(1.0, 63)) {
            if (error) *error = "skill cost must be a finite positive integer: " + resource;
            return {};
        }
        auto item = eve::resource::ResourceCost::create(resource, static_cast<std::int64_t>(amount));
        if (!item) {
            if (error) *error = item.status().describe();
            return {};
        }
        costItems.push_back(std::move(item).takeValue());
    }
    if (!costItems.empty()) {
        auto canonical = eve::resource::CostSpec::create(std::move(costItems));
        if (!canonical) {
            if (error) *error = canonical.status().describe();
            return {};
        }
        def.cost = std::move(canonical).takeValue();
    }

    def.extra = o.getStringMap("extra");
    return def;
}

}  // namespace

int SkillRegistry::loadFromJson(const std::string &json, std::string *error) {
    std::string err;
    const eve::json::Document doc = eve::json::Document::parse(json, &err);
    if (!doc.valid()) {
        if (error) *error = err.empty() ? "invalid json" : err;
        return 0;
    }

    const Value root = doc.root();
    int n = 0;
    if (root.isArray()) {
        for (size_t i = 0; i < root.size(); ++i) {
            SkillDefinition def = parseSkillObject(root.at(i), error);
            if (def.id.empty()) continue;
            registerSkill(def);
            ++n;
        }
    } else if (root.isObject()) {
        SkillDefinition def = parseSkillObject(root, error);
        if (!def.id.empty()) {
            registerSkill(def);
            ++n;
        }
    }
    return n;
}

}  // namespace eve::rpg
