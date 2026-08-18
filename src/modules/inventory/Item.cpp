#include "inventory/Item.h"

#include "common/Json.h"

#include <algorithm>

namespace eve::inventory {

using eve::json::Value;

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

ItemDefinition parseItemObject(Value o) {
    ItemDefinition def;
    if (!o.isObject()) return def;
    def.id = o.getString("id");
    def.displayName = o.getString("displayName", def.id);
    def.maxStack = o.getInt("maxStack", 1);
    def.weight = o.getFloat("weight", 0.f);
    def.volume = o.getFloat("volume", 0.f);
    def.tags = o.getStringArray("tags");
    def.category = o.getString("category");
    def.equipSlot = o.getString("equipSlot");
    def.extra = o.getStringMap("extra");
    return def;
}

}  // namespace

int ItemRegistry::loadFromJson(const std::string &json, std::string *error) {
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
            ItemDefinition def = parseItemObject(root.at(i));
            if (def.id.empty()) continue;
            registerItem(def);
            ++n;
        }
    } else if (root.isObject()) {
        ItemDefinition def = parseItemObject(root);
        if (!def.id.empty()) {
            registerItem(def);
            ++n;
        }
    }
    return n;
}

}  // namespace eve::inventory
