#include "inventory/Item.h"
#include "inventory/JsonHelpers.h"

#include "data/DataModule.h"
#include "data/JsonDocument.h"

#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>

#include <algorithm>
#include <memory>

namespace eve::inventory {

using namespace json_helpers;

bool ItemDefinition::hasTag(const std::string &tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

std::string ItemDefinition::getExtra(const std::string &key, const std::string &fallback) const {
    auto it = extra.find(key);
    return it == extra.end() ? fallback : it->second;
}

void ItemStack::clear() {
    instanceId = 0;
    itemId.clear();
    quantity = 0;
    durability = -1.f;
    props.clear();
    tags.clear();
}

bool ItemStack::hasTag(const std::string &tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

std::string ItemStack::getProp(const std::string &key, const std::string &fallback) const {
    auto it = props.find(key);
    return it == props.end() ? fallback : it->second;
}

void ItemStack::setProp(const std::string &key, const std::string &value) { props[key] = value; }

std::unordered_map<std::string, ItemDefinition> &ItemRegistry::table() {
    static std::unordered_map<std::string, ItemDefinition> t;
    return t;
}

void ItemRegistry::registerItem(const ItemDefinition &def) {
    if (def.id.empty()) return;
    ItemDefinition copy = def;
    if (copy.maxStack <= 0) copy.maxStack = 1;
    if (copy.displayName.empty()) copy.displayName = copy.id;
    table()[copy.id] = std::move(copy);
}

const ItemDefinition *ItemRegistry::find(const std::string &id) {
    auto &t = table();
    auto it = t.find(id);
    return it == t.end() ? nullptr : &it->second;
}

bool ItemRegistry::remove(const std::string &id) { return table().erase(id) > 0; }

void ItemRegistry::clear() { table().clear(); }

int ItemRegistry::count() { return int(table().size()); }

namespace {

ItemDefinition parseItemObject(Poco::JSON::Object::Ptr o) {
    ItemDefinition def;
    if (!o) return def;
    def.id = asString(o->get("id"));
    def.displayName = asString(o->get("displayName"), def.id);
    def.maxStack = asInt(o->get("maxStack"), 1);
    def.weight = asFloat(o->get("weight"), 0.f);
    def.volume = asFloat(o->get("volume"), 0.f);
    def.tags = asStringArray(o, "tags");
    def.category = asString(o->get("category"));
    def.equipSlot = asString(o->get("equipSlot"));
    def.extra = asStringMap(o, "extra");
    return def;
}

}  // namespace

int ItemRegistry::loadFromJson(const std::string &json, std::string *error) {
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
            ItemDefinition def = parseItemObject(o);
            if (def.id.empty()) continue;
            registerItem(def);
            ++n;
        }
    } else if (doc->isObject()) {
        ItemDefinition def = parseItemObject(doc->object());
        if (!def.id.empty()) {
            registerItem(def);
            ++n;
        }
    }
    return n;
}

}  // namespace eve::inventory
