#include "rpg/Effect.h"

#include "common/Json.h"

#include <algorithm>

namespace eve::rpg {

using eve::json::Value;

bool EffectDefinition::hasTag(const std::string &tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

std::string EffectDefinition::getExtra(const std::string &key, const std::string &fallback) const {
    auto it = extra.find(key);
    return it == extra.end() ? fallback : it->second;
}

std::unordered_map<std::string, EffectDefinition> &EffectRegistry::table() {
    static std::unordered_map<std::string, EffectDefinition> t;
    return t;
}

void EffectRegistry::registerEffect(const EffectDefinition &def) {
    if (def.id.empty()) return;
    table()[def.id] = def;
}

const EffectDefinition *EffectRegistry::find(const std::string &id) {
    auto &t = table();
    auto it = t.find(id);
    return it == t.end() ? nullptr : &it->second;
}

bool EffectRegistry::remove(const std::string &id) { return table().erase(id) > 0; }

void EffectRegistry::clear() { table().clear(); }

int EffectRegistry::count() { return int(table().size()); }

namespace {

EffectDefinition parseEffectObject(Value o) {
    EffectDefinition def;
    if (!o.isObject()) return def;
    def.id = o.getString("id");
    def.durationPolicy = o.getString("durationPolicy", "instant");
    def.duration = o.getFloat("duration", 0.f);
    def.period = o.getFloat("period", 0.f);
    def.stackPolicy = o.getString("stackPolicy", "none");
    def.maxStacks = o.getInt("maxStacks", 1);
    def.tags = o.getStringArray("tags");
    def.extra = o.getStringMap("extra");

    const Value mods = o.get("modifiers");
    for (size_t i = 0; i < mods.size(); ++i) {
        const Value mo = mods.at(i);
        EffectModifierSpec spec;
        spec.attribute = mo.getString("attribute");
        spec.op = mo.getString("op", "add");
        spec.value = mo.getDouble("value", 0.0);
        spec.priority = mo.getInt("priority", 0);
        if (!spec.attribute.empty()) def.modifiers.push_back(std::move(spec));
    }
    return def;
}

}  // namespace

int EffectRegistry::loadFromJson(const std::string &json, std::string *error) {
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
            EffectDefinition def = parseEffectObject(root.at(i));
            if (def.id.empty()) continue;
            registerEffect(def);
            ++n;
        }
    } else if (root.isObject()) {
        EffectDefinition def = parseEffectObject(root);
        if (!def.id.empty()) {
            registerEffect(def);
            ++n;
        }
    }
    return n;
}

}  // namespace eve::rpg
