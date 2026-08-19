#include "housegen/HouseComponentLibrary.h"

#include "common/Json.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace eve::housegen {
namespace {

using eve::json::Value;

SocketDirection parseDirection(const std::string &s, bool *ok) {
    *ok = true;
    if (s == "north") return SocketDirection::North;
    if (s == "east") return SocketDirection::East;
    if (s == "south") return SocketDirection::South;
    if (s == "west") return SocketDirection::West;
    if (s == "up") return SocketDirection::Up;
    if (s == "down") return SocketDirection::Down;
    *ok = false;
    return SocketDirection::North;
}

bool parseComponent(Value o, HouseComponent &out, std::string *error) {
    if (!o.isObject()) { if (error) *error = "component must be an object"; return false; }
    out.id = o.getString("id");
    out.modelPath = o.getString("model");
    out.category = o.getString("category");
    out.width = o.getInt("width", 1);
    out.depth = o.getInt("depth", 1);
    out.height = o.getInt("height", 1);
    out.weight = o.getInt("weight", 1);
    out.tags = o.getStringArray("tags");
    if (o.get("rotations").isArray()) out.rotations = o.getIntArray("rotations");

    const Value sockets = o.get("sockets");
    for (size_t i = 0; i < sockets.size(); ++i) {
        const Value so = sockets.at(i);
        bool ok = false;
        HouseSocket s;
        s.direction = parseDirection(so.getString("direction"), &ok);
        s.type = so.getString("type");
        s.accepts = so.getStringArray("accepts");
        if (!ok || s.type.empty()) { if (error) *error = "socket needs a valid direction and type"; return false; }
        out.sockets.push_back(std::move(s));
    }

    const Value material = o.get("material");
    if (material.isObject()) {
        const Value color = material.get("baseColor");
        if (color.isArray()) {
            if (color.size() != 3 && color.size() != 4) {
                if (error) *error = "material baseColor needs 3 or 4 values";
                return false;
            }
            out.material.hasBaseColor = true;
            out.material.baseColorR = color.at(0).asFloat();
            out.material.baseColorG = color.at(1).asFloat();
            out.material.baseColorB = color.at(2).asFloat();
            out.material.baseColorA = color.size() == 4 ? color.at(3).asFloat() : 1.f;
        }
        out.material.baseColorTexture = material.getString("baseColorTexture");
        out.material.normalTexture = material.getString("normalTexture");
        out.material.heightTexture = material.getString("heightTexture");
        if (material.has("metallic")) {
            out.material.hasMetallic = true;
            out.material.metallic = material.getFloat("metallic");
        }
        if (material.has("roughness")) {
            out.material.hasRoughness = true;
            out.material.roughness = material.getFloat("roughness");
        }
        out.material.parallaxScale = material.getFloat("parallaxScale", 0.f);
        out.material.parallaxMinLayers = material.getFloat("parallaxMinLayers", 8.f);
        out.material.parallaxMaxLayers = material.getFloat("parallaxMaxLayers", 32.f);
        out.material.cellBombScale = material.getFloat("cellBombScale", 4.f);
        out.material.cellBombStrength = material.getFloat("cellBombStrength", 0.f);
        out.material.cellBombRotation = material.getFloat("cellBombRotation", 1.f);
    }
    return true;
}

}  // namespace

bool HouseComponentLibrary::registerComponent(const HouseComponent &c, std::string *error) {
    if (c.id.empty() || c.modelPath.empty() || c.category.empty()) {
        if (error) *error = "component needs id, model and category";
        return false;
    }
    if (c.width < 1 || c.depth < 1 || c.height < 1 || c.weight < 1) {
        if (error) *error = "component dimensions and weight must be positive";
        return false;
    }
    for (int r : c.rotations) if (r < 0 || r >= 360 || r % 90 != 0) {
        if (error) *error = "rotations must be cardinal degrees";
        return false;
    }
    const auto inUnitRange = [](float v) { return v >= 0.f && v <= 1.f; };
    if ((c.material.hasBaseColor &&
         (!inUnitRange(c.material.baseColorR) || !inUnitRange(c.material.baseColorG) ||
          !inUnitRange(c.material.baseColorB) || !inUnitRange(c.material.baseColorA))) ||
        (c.material.hasMetallic && !inUnitRange(c.material.metallic)) ||
        (c.material.hasRoughness && !inUnitRange(c.material.roughness)) ||
        !inUnitRange(c.material.cellBombStrength) || !inUnitRange(c.material.cellBombRotation)) {
        if (error) *error = "material values must be between 0 and 1";
        return false;
    }
    if (c.material.parallaxScale < 0.f || c.material.parallaxScale > 0.2f ||
        c.material.parallaxMinLayers < 1.f ||
        c.material.parallaxMaxLayers < c.material.parallaxMinLayers ||
        c.material.cellBombScale <= 0.f) {
        if (error) *error = "material sampling parameters are invalid";
        return false;
    }
    if (components_.contains(c.id)) { if (error) *error = "duplicate component id: " + c.id; return false; }
    components_.emplace(c.id, c);
    return true;
}

bool HouseComponentLibrary::loadFromJson(const std::string &json, std::string *error) {
    const eve::json::Document doc = eve::json::Document::parse(json, error);
    if (!doc.valid()) return false;
    const Value root = doc.root();
    const Value values = root.isArray() ? root : root.get("components");
    if (!values.isArray()) { if (error) *error = "expected components array"; return false; }
    HouseComponentLibrary candidate;
    for (size_t i = 0; i < values.size(); ++i) {
        HouseComponent c;
        if (!parseComponent(values.at(i), c, error) || !candidate.registerComponent(c, error))
            return false;
    }
    components_ = std::move(candidate.components_);
    return true;
}

bool HouseComponentLibrary::loadFromFile(const std::string &filename, std::string *error) {
    std::ifstream input(filename, std::ios::binary);
    if (!input) {
        if (error) *error = "cannot open component manifest: " + filename;
        return false;
    }
    std::ostringstream json;
    json << input.rdbuf();
    HouseComponentLibrary candidate;
    if (!candidate.loadFromJson(json.str(), error)) return false;
    const std::filesystem::path assetRoot =
        std::filesystem::absolute(std::filesystem::path(filename)).parent_path();
    for (auto &[_, component] : candidate.components_) {
        std::filesystem::path model(component.modelPath);
        if (model.is_relative()) component.modelPath = (assetRoot / model).lexically_normal().string();
    }
    components_ = std::move(candidate.components_);
    return true;
}

void HouseComponentLibrary::clear() { components_.clear(); }
const HouseComponent *HouseComponentLibrary::find(const std::string &id) const { auto it = components_.find(id); return it == components_.end() ? nullptr : &it->second; }
int HouseComponentLibrary::count() const { return int(components_.size()); }
std::vector<std::string> HouseComponentLibrary::ids() const { std::vector<std::string> out; for (const auto &[id, _] : components_) out.push_back(id); std::sort(out.begin(), out.end()); return out; }
std::vector<const HouseComponent *> HouseComponentLibrary::byCategory(const std::string &category, const std::string &style) const {
    std::vector<const HouseComponent *> out;
    for (const auto &[_, c] : components_) if (c.category == category && (style.empty() || std::find(c.tags.begin(), c.tags.end(), style) != c.tags.end())) out.push_back(&c);
    if (out.empty() && !style.empty()) return byCategory(category, {});
    std::sort(out.begin(), out.end(), [](auto *a, auto *b) { return a->id < b->id; });
    return out;
}

}  // namespace eve::housegen
