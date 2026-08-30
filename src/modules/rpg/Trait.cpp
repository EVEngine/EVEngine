#include "rpg/Trait.h"

#include "common/Json.h"

#include <algorithm>

namespace eve::rpg {

using eve::json::Value;

bool TraitDefinition::hasTag(const std::string &tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

std::string TraitDefinition::getExtra(const std::string &key, const std::string &fallback) const {
    auto it = extra.find(key);
    return it == extra.end() ? fallback : it->second;
}

namespace {
std::unordered_map<std::string, TraitDefinition> &traitTable() {
    static std::unordered_map<std::string, TraitDefinition> t;
    return t;
}
}  // namespace

void TraitRegistry::registerTrait(const TraitDefinition &def) {
    if (def.id.empty()) return;
    traitTable()[def.id] = def;
}

const TraitDefinition *TraitRegistry::find(const std::string &id) {
    auto &t = traitTable();
    auto it = t.find(id);
    return it == t.end() ? nullptr : &it->second;
}

bool TraitRegistry::remove(const std::string &id) { return traitTable().erase(id) > 0; }

void TraitRegistry::clear() { traitTable().clear(); }

int TraitRegistry::count() { return int(traitTable().size()); }

int TraitRegistry::loadFromJson(const std::string &json, std::string *error) {
    std::string err;
    const eve::json::Document doc = eve::json::Document::parse(json, &err);
    if (!doc.valid()) {
        if (error) *error = err.empty() ? "invalid json" : err;
        return 0;
    }
    const Value root = doc.root();
    int n = 0;
    const auto parseOne = [&](Value o) {
        if (!o.isObject()) return;
        TraitDefinition def;
        def.id = o.getString("id");
        if (def.id.empty()) return;
        def.tags = o.getStringArray("tags");
        def.extra = o.getStringMap("extra");
        const Value traits = o.get("traits");
        for (size_t i = 0; i < traits.size(); ++i) {
            const Value t = traits.at(i);
            TraitSpec spec;
            spec.kind = t.getString("kind");
            spec.target = t.getString("target");
            spec.value = t.getDouble("value", 0.0);
            if (spec.kind.empty()) continue;
            def.traits.push_back(std::move(spec));
        }
        traitTable()[def.id] = std::move(def);
        ++n;
    };
    if (root.isArray()) {
        for (size_t i = 0; i < root.size(); ++i) parseOne(root.at(i));
    } else if (root.isObject()) {
        parseOne(root);
    }
    return n;
}

}  // namespace eve::rpg