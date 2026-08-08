#include "rpg/Skill.h"
#include "rpg/JsonHelpers.h"

#include "data/DataModule.h"
#include "data/JsonDocument.h"

#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>

#include <algorithm>
#include <memory>
#include <unordered_map>

namespace eve::rpg {

using namespace json_helpers;

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

SkillDefinition parseSkillObject(Poco::JSON::Object::Ptr o) {
    SkillDefinition def;
    if (!o) return def;
    def.id = asString(o->get("id"));
    def.cooldown = asFloat(o->get("cooldown"), 0.f);
    def.castTime = asFloat(o->get("castTime"), 0.f);
    def.targetType = asString(o->get("targetType"), "self");
    def.grantedEffects = asStringArray(o, "grantedEffects");
    def.tags = asStringArray(o, "tags");

    if (o->has("costs")) {
        try {
            auto arr = o->getArray("costs");
            if (arr) {
                for (size_t i = 0; i < arr->size(); ++i) {
                    Poco::JSON::Object::Ptr co;
                    try {
                        co = arr->getObject(i);
                    } catch (...) {
                        continue;
                    }
                    if (!co) continue;
                    SkillCostSpec spec;
                    spec.attribute = asString(co->get("attribute"));
                    spec.amount = asDouble(co->get("amount"), 0.0);
                    if (!spec.attribute.empty()) def.costs.push_back(std::move(spec));
                }
            }
        } catch (...) {
        }
    }

    if (o->has("extra")) {
        try {
            auto eo = o->getObject("extra");
            if (eo) {
                for (auto it = eo->begin(); it != eo->end(); ++it)
                    def.extra[it->first] = asString(it->second);
            }
        } catch (...) {
        }
    }
    return def;
}

}  // namespace

int SkillRegistry::loadFromJson(const std::string &json, std::string *error) {
    auto *dm = eve::data::DataModule::create();
    std::string err;
    std::unique_ptr<data::JsonDocument> doc(dm->decodeJson(json, &err));
    if (!doc) {
        if (error) *error = err.empty() ? "invalid json" : err;
        return 0;
    }

    int n = 0;
    if (doc->isArray()) {
        auto arr = doc->array();
        if (!arr) return 0;
        for (size_t i = 0; i < arr->size(); ++i) {
            Poco::JSON::Object::Ptr o;
            try {
                o = arr->getObject(i);
            } catch (...) {
                continue;
            }
            if (!o) continue;
            SkillDefinition def = parseSkillObject(o);
            if (def.id.empty()) continue;
            registerSkill(def);
            ++n;
        }
    } else if (doc->isObject()) {
        SkillDefinition def = parseSkillObject(doc->object());
        if (!def.id.empty()) {
            registerSkill(def);
            ++n;
        }
    }
    return n;
}

}  // namespace eve::rpg
