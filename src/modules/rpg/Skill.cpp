#include "rpg/Skill.h"

#include "common/Json.h"

#include <algorithm>
#include <unordered_map>

namespace eve::rpg {

using eve::json::Value;

bool SkillDefinition::hasTag(const std::string &tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

namespace {
std::unordered_map<std::string, SkillDefinition> &table() {
    static std::unordered_map<std::string, SkillDefinition> t;
    return t;
}
}  // namespace

void SkillRegistry::registerSkill(const SkillDefinition &def) {
    if (def.id.empty()) return;
    table()[def.id] = def;
}

const SkillDefinition *SkillRegistry::find(const std::string &id) {
    auto &t = table();
    auto it = t.find(id);
    return it == t.end() ? nullptr : &it->second;
}

bool SkillRegistry::remove(const std::string &id) { return table().erase(id) > 0; }

void SkillRegistry::clear() { table().clear(); }

int SkillRegistry::count() { return int(table().size()); }

namespace {

SkillDefinition parseSkillObject(Value o) {
    SkillDefinition def;
    if (!o.isObject()) return def;
    def.id = o.getString("id");
    def.cooldown = o.getFloat("cooldown", 0.f);
    def.castTime = o.getFloat("castTime", 0.f);
    def.targetType = o.getString("targetType", "self");
    def.grantedEffects = o.getStringArray("grantedEffects");
    def.tags = o.getStringArray("tags");

    const Value costs = o.get("costs");
    for (size_t i = 0; i < costs.size(); ++i) {
        const Value co = costs.at(i);
        SkillCostSpec spec;
        spec.attribute = co.getString("attribute");
        spec.amount = co.getDouble("amount", 0.0);
        if (!spec.attribute.empty()) def.costs.push_back(std::move(spec));
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
            SkillDefinition def = parseSkillObject(root.at(i));
            if (def.id.empty()) continue;
            registerSkill(def);
            ++n;
        }
    } else if (root.isObject()) {
        SkillDefinition def = parseSkillObject(root);
        if (!def.id.empty()) {
            registerSkill(def);
            ++n;
        }
    }
    return n;
}

}  // namespace eve::rpg
