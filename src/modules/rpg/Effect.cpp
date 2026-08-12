#include "rpg/Effect.h"
#include "rpg/JsonHelpers.h"

#include "data/DataModule.h"
#include "data/JsonDocument.h"

#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>

#include <algorithm>
#include <memory>

namespace eve::rpg {

using namespace json_helpers;

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

EffectDefinition parseEffectObject(Poco::JSON::Object::Ptr o) {
    EffectDefinition def;
    if (!o) return def;
    def.id = asString(o->get("id"));
    def.durationPolicy = asString(o->get("durationPolicy"), "instant");
    def.duration = asFloat(o->get("duration"), 0.f);
    def.period = asFloat(o->get("period"), 0.f);
    def.stackPolicy = asString(o->get("stackPolicy"), "none");
    def.maxStacks = asInt(o->get("maxStacks"), 1);
    def.tags = asStringArray(o, "tags");
    def.extra = asStringMap(o, "extra");

    if (o->has("modifiers")) {
        try {
            auto arr = o->getArray("modifiers");
            if (arr) {
                for (size_t i = 0; i < arr->size(); ++i) {
                    Poco::JSON::Object::Ptr mo;
                    try {
                        mo = arr->getObject(i);
                    } catch (...) {
                        continue;
                    }
                    if (!mo) continue;
                    EffectModifierSpec spec;
                    spec.attribute = asString(mo->get("attribute"));
                    spec.op = asString(mo->get("op"), "add");
                    spec.value = asDouble(mo->get("value"), 0.0);
                    spec.priority = asInt(mo->get("priority"), 0);
                    if (!spec.attribute.empty()) def.modifiers.push_back(std::move(spec));
                }
            }
        } catch (...) {
        }
    }
    return def;
}

}  // namespace

int EffectRegistry::loadFromJson(const std::string &json, std::string *error) {
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
            EffectDefinition def = parseEffectObject(o);
            if (def.id.empty()) continue;
            registerEffect(def);
            ++n;
        }
    } else if (doc->isObject()) {
        EffectDefinition def = parseEffectObject(doc->object());
        if (!def.id.empty()) {
            registerEffect(def);
            ++n;
        }
    }
    return n;
}

}  // namespace eve::rpg
