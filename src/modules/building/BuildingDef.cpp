#include "building/BuildingDef.h"
#include "building/JsonHelpers.h"

#include "data/DataModule.h"
#include "data/JsonDocument.h"

#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>

#include <algorithm>
#include <memory>

namespace eve::building {

using namespace json_helpers;

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

BuildingDefinition parseBuildingObject(Poco::JSON::Object::Ptr o) {
    BuildingDefinition def;
    if (!o) return def;
    def.id = asString(o->get("id"));
    def.displayName = asString(o->get("displayName"), def.id);
    def.category = asString(o->get("category"));
    def.channel = asString(o->get("channel"));
    def.renderMode = asString(o->get("renderMode"));
    def.footprintW = asInt(o->get("footprintW"), 1);
    def.footprintH = asInt(o->get("footprintH"), 1);
    def.snapMode = asString(o->get("snapMode"), "grid");
    def.rotationMode = asString(o->get("rotationMode"), "cardinal");
    def.validateRule = asString(o->get("validateRule"), "default");
    def.tags = asStringArray(o, "tags");
    def.requireTerrain = asIntArray(o, "requireTerrain");
    def.forbidTerrain = asIntArray(o, "forbidTerrain");
    def.requireAdjacentTag = asString(o->get("requireAdjacentTag"));
    def.requireAdjacentTerrain = asInt(o->get("requireAdjacentTerrain"), -1);
    def.cost = asIntMap(o, "cost");
    def.extra = asStringMap(o, "extra");
    def.visual2d = asStringMap(o, "visual2d");
    def.visual3d = asStringMap(o, "visual3d");
    if (o->has("footprintMask")) {
        try {
            auto arr = o->getArray("footprintMask");
            if (arr) {
                def.footprintMask.reserve(arr->size());
                for (size_t i = 0; i < arr->size(); ++i) {
                    def.footprintMask.push_back(uint8_t(asInt(arr->get(i), 0) != 0 ? 1 : 0));
                }
            }
        } catch (...) {
        }
    }
    return def;
}

}  // namespace

int BuildingRegistry::loadFromJson(const std::string &json, std::string *error) {
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
            BuildingDefinition def = parseBuildingObject(o);
            if (def.id.empty()) continue;
            registerBuilding(def);
            ++n;
        }
    } else if (doc->isObject()) {
        BuildingDefinition def = parseBuildingObject(doc->object());
        if (!def.id.empty()) {
            registerBuilding(def);
            ++n;
        }
    }
    return n;
}

}  // namespace eve::building
