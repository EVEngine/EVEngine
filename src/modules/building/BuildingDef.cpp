#include "building/BuildingDef.h"

#include "common/Json.h"

#include <algorithm>

namespace eve::building {

using eve::json::Value;

bool BuildingDefinition::hasTag(const std::string &tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

std::string BuildingDefinition::getExtra(const std::string &key,
                                         const std::string &fallback) const {
    auto it = extra.find(key);
    return it == extra.end() ? fallback : it->second;
}

std::string BuildingDefinition::getVisual2d(const std::string &key,
                                            const std::string &fallback) const {
    auto it = visual2d.find(key);
    return it == visual2d.end() ? fallback : it->second;
}

std::string BuildingDefinition::getVisual3d(const std::string &key,
                                            const std::string &fallback) const {
    auto it = visual3d.find(key);
    return it == visual3d.end() ? fallback : it->second;
}

int BuildingDefinition::getCost(const std::string &resource, int fallback) const {
    auto it = cost.find(resource);
    return it == cost.end() ? fallback : it->second;
}

bool BuildingDefinition::maskAt(int localX, int localY) const {
    if (localX < 0 || localY < 0 || localX >= footprintW || localY >= footprintH) return false;
    if (footprintMask.empty()) return true;
    const size_t idx = size_t(localY) * size_t(footprintW) + size_t(localX);
    if (idx >= footprintMask.size()) return false;
    return footprintMask[idx] != 0;
}

bool PlacedBuilding::hasTag(const std::string &tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

std::string PlacedBuilding::getProp(const std::string &key, const std::string &fallback) const {
    auto it = props.find(key);
    return it == props.end() ? fallback : it->second;
}

void PlacedBuilding::setProp(const std::string &key, const std::string &value) {
    props[key] = value;
}

std::unordered_map<std::string, BuildingDefinition> &BuildingRegistry::table() {
    static std::unordered_map<std::string, BuildingDefinition> t;
    return t;
}

void BuildingRegistry::registerBuilding(const BuildingDefinition &def) {
    if (def.id.empty()) return;
    BuildingDefinition copy = def;
    if (copy.footprintW <= 0) copy.footprintW = 1;
    if (copy.footprintH <= 0) copy.footprintH = 1;
    if (copy.displayName.empty()) copy.displayName = copy.id;
    if (copy.snapMode.empty()) copy.snapMode = "grid";
    if (copy.rotationMode.empty()) copy.rotationMode = "cardinal";
    if (copy.validateRule.empty()) copy.validateRule = "default";
    const size_t expected = size_t(copy.footprintW) * size_t(copy.footprintH);
    if (!copy.footprintMask.empty() && copy.footprintMask.size() != expected) {
        copy.footprintMask.clear();  // 长度不匹配时回退实心矩形
    }
    table()[copy.id] = std::move(copy);
}

const BuildingDefinition *BuildingRegistry::find(const std::string &id) {
    auto &t = table();
    auto it = t.find(id);
    return it == t.end() ? nullptr : &it->second;
}

bool BuildingRegistry::remove(const std::string &id) { return table().erase(id) > 0; }

void BuildingRegistry::clear() { table().clear(); }

int BuildingRegistry::count() { return int(table().size()); }

namespace {

BuildingDefinition parseBuildingObject(Value o) {
    BuildingDefinition def;
    if (!o.isObject()) return def;
    def.id = o.getString("id");
    def.displayName = o.getString("displayName", def.id);
    def.category = o.getString("category");
    def.channel = o.getString("channel");
    def.renderMode = o.getString("renderMode");
    def.footprintW = o.getInt("footprintW", 1);
    def.footprintH = o.getInt("footprintH", 1);
    def.snapMode = o.getString("snapMode", "grid");
    def.rotationMode = o.getString("rotationMode", "cardinal");
    def.validateRule = o.getString("validateRule", "default");
    def.tags = o.getStringArray("tags");
    def.requireTerrain = o.getIntArray("requireTerrain");
    def.forbidTerrain = o.getIntArray("forbidTerrain");
    def.requireAdjacentTag = o.getString("requireAdjacentTag");
    def.requireAdjacentTerrain = o.getInt("requireAdjacentTerrain", -1);
    def.cost = o.getIntMap("cost");
    def.extra = o.getStringMap("extra");
    def.visual2d = o.getStringMap("visual2d");
    def.visual3d = o.getStringMap("visual3d");

    const Value mask = o.get("footprintMask");
    def.footprintMask.reserve(mask.size());
    for (size_t i = 0; i < mask.size(); ++i)
        def.footprintMask.push_back(uint8_t(mask.at(i).asInt(0) != 0 ? 1 : 0));
    return def;
}

}  // namespace

int BuildingRegistry::loadFromJson(const std::string &json, std::string *error) {
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
            BuildingDefinition def = parseBuildingObject(root.at(i));
            if (def.id.empty()) continue;
            registerBuilding(def);
            ++n;
        }
    } else if (root.isObject()) {
        BuildingDefinition def = parseBuildingObject(root);
        if (!def.id.empty()) {
            registerBuilding(def);
            ++n;
        }
    }
    return n;
}

}  // namespace eve::building
