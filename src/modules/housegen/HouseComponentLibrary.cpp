#include "housegen/HouseComponentLibrary.h"

#include "common/Json.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace eve::housegen {
namespace {

using eve::json::Value;

template <typename T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message,
                      std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(
        code, std::move(message), std::move(path), {}, "housegen.components"));
}

eve::Result<SocketDirection> parseDirection(std::string_view value) {
    if (value == "north") return eve::Result<SocketDirection>::success(SocketDirection::North);
    if (value == "east") return eve::Result<SocketDirection>::success(SocketDirection::East);
    if (value == "south") return eve::Result<SocketDirection>::success(SocketDirection::South);
    if (value == "west") return eve::Result<SocketDirection>::success(SocketDirection::West);
    if (value == "up") return eve::Result<SocketDirection>::success(SocketDirection::Up);
    if (value == "down") return eve::Result<SocketDirection>::success(SocketDirection::Down);
    return failure<SocketDirection>(eve::DiagnosticCode::InvalidArgument,
                                    "socket direction is invalid", "direction");
}

eve::Result<HouseComponent> parseComponent(Value object) {
    if (!object.isObject())
        return failure<HouseComponent>(eve::DiagnosticCode::ParseError,
                                       "component must be an object");

    HouseComponent out;
    out.id = object.getString("id");
    out.modelPath = object.getString("model");
    out.category = object.getString("category");
    out.width = object.getInt("width", 1);
    out.depth = object.getInt("depth", 1);
    out.height = object.getInt("height", 1);
    out.weight = object.getInt("weight", 1);
    out.tags = object.getStringArray("tags");
    if (object.get("rotations").isArray()) out.rotations = object.getIntArray("rotations");

    const Value sockets = object.get("sockets");
    for (size_t i = 0; i < sockets.size(); ++i) {
        const Value socketObject = sockets.at(i);
        if (!socketObject.isObject())
            return failure<HouseComponent>(eve::DiagnosticCode::ParseError,
                                           "socket must be an object", "sockets");
        auto direction = parseDirection(socketObject.getString("direction"));
        if (!direction.ok())
            return eve::Result<HouseComponent>::failure(direction.status());

        HouseSocket socket;
        socket.direction = std::move(direction).takeValue();
        socket.type = socketObject.getString("type");
        socket.accepts = socketObject.getStringArray("accepts");
        if (socket.type.empty())
            return failure<HouseComponent>(eve::DiagnosticCode::InvalidArgument,
                                           "socket needs a valid direction and type", "sockets");
        out.sockets.push_back(std::move(socket));
    }

    const Value material = object.get("material");
    if (material.isObject()) {
        const Value color = material.get("baseColor");
        if (color.isArray()) {
            if (color.size() != 3 && color.size() != 4)
                return failure<HouseComponent>(eve::DiagnosticCode::InvalidArgument,
                                               "material baseColor needs 3 or 4 values",
                                               "material.baseColor");
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
    return eve::Result<HouseComponent>::success(std::move(out));
}

}  // namespace

eve::Result<void> HouseComponentLibrary::registerComponent(const HouseComponent &component) {
    if (component.id.empty() || component.modelPath.empty() || component.category.empty())
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "component needs id, model and category");
    if (component.width < 1 || component.depth < 1 || component.height < 1 || component.weight < 1)
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "component dimensions and weight must be positive");
    for (const int rotation : component.rotations) {
        if (rotation < 0 || rotation >= 360 || rotation % 90 != 0)
            return failure<void>(eve::DiagnosticCode::InvalidArgument,
                                 "rotations must be cardinal degrees", "rotations");
    }

    const auto inUnitRange = [](float value) { return value >= 0.f && value <= 1.f; };
    if ((component.material.hasBaseColor &&
         (!inUnitRange(component.material.baseColorR) ||
          !inUnitRange(component.material.baseColorG) ||
          !inUnitRange(component.material.baseColorB) ||
          !inUnitRange(component.material.baseColorA))) ||
        (component.material.hasMetallic && !inUnitRange(component.material.metallic)) ||
        (component.material.hasRoughness && !inUnitRange(component.material.roughness)) ||
        !inUnitRange(component.material.cellBombStrength) ||
        !inUnitRange(component.material.cellBombRotation))
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "material values must be between 0 and 1", "material");
    if (component.material.parallaxScale < 0.f || component.material.parallaxScale > 0.2f ||
        component.material.parallaxMinLayers < 1.f ||
        component.material.parallaxMaxLayers < component.material.parallaxMinLayers ||
        component.material.cellBombScale <= 0.f)
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "material sampling parameters are invalid", "material");
    if (components_.contains(component.id))
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "duplicate component id: " + component.id, "id");

    components_.emplace(component.id, component);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> HouseComponentLibrary::loadFromJson(std::string_view json) {
    std::string parseError;
    const eve::json::Document document =
        eve::json::Document::parse(std::string(json), &parseError);
    if (!document.valid())
        return failure<void>(eve::DiagnosticCode::ParseError,
                             parseError.empty() ? "invalid component manifest" : parseError);

    const Value root = document.root();
    const Value values = root.isArray() ? root : root.get("components");
    if (!values.isArray())
        return failure<void>(eve::DiagnosticCode::ParseError,
                             "expected components array", "components");

    HouseComponentLibrary candidate;
    for (size_t i = 0; i < values.size(); ++i) {
        auto component = parseComponent(values.at(i));
        if (!component.ok()) return eve::Result<void>::failure(component.status());
        auto registered = candidate.registerComponent(std::move(component).takeValue());
        if (!registered.ok()) return registered;
    }
    components_ = std::move(candidate.components_);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> HouseComponentLibrary::loadFromFile(std::string_view filename) {
    const std::string path(filename);
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return failure<void>(eve::DiagnosticCode::NotFound,
                             "cannot open component manifest: " + path, path);

    std::ostringstream json;
    json << input.rdbuf();
    HouseComponentLibrary candidate;
    auto loaded = candidate.loadFromJson(json.str());
    if (!loaded.ok()) return loaded;

    const std::filesystem::path assetRoot =
        std::filesystem::absolute(std::filesystem::path(path)).parent_path();
    for (auto &[_, component] : candidate.components_) {
        std::filesystem::path model(component.modelPath);
        if (model.is_relative())
            component.modelPath = (assetRoot / model).lexically_normal().string();
    }
    components_ = std::move(candidate.components_);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void HouseComponentLibrary::clear() { components_.clear(); }

eve::OptionalRef<const HouseComponent> HouseComponentLibrary::find(std::string_view id) const {
    const auto it = components_.find(std::string(id));
    if (it == components_.end()) return {};
    return std::cref(it->second);
}

int HouseComponentLibrary::count() const { return int(components_.size()); }

std::vector<std::string> HouseComponentLibrary::ids() const {
    std::vector<std::string> result;
    result.reserve(components_.size());
    for (const auto &[id, _] : components_) result.push_back(id);
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::reference_wrapper<const HouseComponent>> HouseComponentLibrary::byCategory(
    std::string_view category, std::string_view style) const {
    std::vector<std::reference_wrapper<const HouseComponent>> result;
    for (const auto &[_, component] : components_) {
        if (component.category == category &&
            (style.empty() || std::find(component.tags.begin(), component.tags.end(), style) !=
                                  component.tags.end()))
            result.push_back(std::cref(component));
    }
    if (result.empty() && !style.empty()) return byCategory(category, {});
    std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
        return left.get().id < right.get().id;
    });
    return result;
}

}  // namespace eve::housegen
