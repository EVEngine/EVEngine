#include "asset_scene/EvpackSceneTemplateLoader.h"

#include "common/Value.h"
#include "asset/RuntimeDefinition.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <set>

namespace eve::asset_scene {
namespace {

template <class T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {},
                                                "asset.scene.template"));
}

const Value* field(const Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

bool number(const Value& value, float& result) {
    if (!value.isNumeric()) return false;
    const double decoded = value.isInt64() ? double(value.asInt()) : value.asDouble();
    if (!std::isfinite(decoded) || decoded < -std::numeric_limits<float>::max() ||
        decoded > std::numeric_limits<float>::max())
        return false;
    result = static_cast<float>(decoded);
    return true;
}

template <std::size_t N>
bool vector(const Value* value, std::array<float, N>& result) {
    const auto* values = value ? value->getIf<Value::Array>() : nullptr;
    if (!values || values->size() != N) return false;
    for (std::size_t i = 0; i < N; ++i)
        if (!number((*values)[i], result[i])) return false;
    return true;
}

struct FlatNode {
    std::uint64_t sourceId = 0;
    std::uint64_t parentId = 0;
    scene::NodeDesc desc;
};

}  // namespace

Result<LoadedSceneTemplate> EvpackSceneTemplateLoader::load(
    const AssetRef& sceneTemplate, const asset::EvpackCapabilities& capabilities,
    const SceneTemplateLoadLimits& limits) const {
    auto payload = reader_.read(sceneTemplate, "eve.scene-template/1", capabilities,
                                limits.maximumDecodedBytes);
    if (!payload) return Result<LoadedSceneTemplate>::failure(payload.status());
    const asset::RuntimeAssetChunk* definition = nullptr;
    for (const auto& chunk : payload.value().chunks) {
        if (chunk.kind != asset::EvpackChunkKind::Definition) continue;
        if (definition)
            return failure<LoadedSceneTemplate>(DiagnosticCode::Conflict,
                                                "scene template has duplicate definitions");
        definition = &chunk;
    }
    if (!definition)
        return failure<LoadedSceneTemplate>(DiagnosticCode::NotFound,
                                            "scene template definition is missing");
    asset::RuntimeDefinitionLimits definitionLimits;
    definitionLimits.maximumBytes = limits.maximumDecodedBytes;
    auto metadata = asset::decodeRuntimeDefinition(definition->bytes, definitionLimits);
    if (!metadata) return Result<LoadedSceneTemplate>::failure(metadata.status());
    const auto* root = metadata.value().getIf<Value::Object>();
    const Value* schema = root ? field(*root, "schema") : nullptr;
    const Value* version = root ? field(*root, "schemaVersion") : nullptr;
    const Value* coordinate = root ? field(*root, "coordinateSystem") : nullptr;
    const Value* nodesValue = root ? field(*root, "nodes") : nullptr;
    const auto* nodes = nodesValue ? nodesValue->getIf<Value::Array>() : nullptr;
    if (!schema || !schema->isString() || schema->asString() != "eve.scene-template" ||
        !version || !version->isInt64() || version->asInt() != 1 || !coordinate ||
        !coordinate->isString() ||
        coordinate->asString() != "right-handed-x-right-y-up-minus-z-forward" || !nodes ||
        nodes->empty() || nodes->size() > limits.maximumNodes)
        return failure<LoadedSceneTemplate>(DiagnosticCode::ParseError,
                                            "scene-template metadata is invalid");
    std::map<std::uint64_t, FlatNode> flat;
    constexpr float radiansToDegrees = 57.29577951308232f;
    for (std::size_t i = 0; i < nodes->size(); ++i) {
        const auto* node = (*nodes)[i].getIf<Value::Object>();
        const Value* sourceId = node ? field(*node, "sourceFileId") : nullptr;
        const Value* parentId = node ? field(*node, "parentSourceFileId") : nullptr;
        const Value* objectId = node ? field(*node, "objectId") : nullptr;
        const Value* name = node ? field(*node, "name") : nullptr;
        std::array<float, 3> position{}, scale{};
        std::array<float, 4> rotation{};
        if (!sourceId || !sourceId->isInt64() || sourceId->asInt() <= 0 || !parentId ||
            !parentId->isInt64() || parentId->asInt() < 0 || !objectId || !objectId->isString() ||
            !name || !name->isString() || !vector(node ? field(*node, "position") : nullptr, position) ||
            !vector(node ? field(*node, "rotation") : nullptr, rotation) ||
            !vector(node ? field(*node, "scale") : nullptr, scale))
            return failure<LoadedSceneTemplate>(DiagnosticCode::ParseError,
                                                "scene node is malformed",
                                                "$.nodes[" + std::to_string(i) + "]");
        auto persistentId = SceneObjectId::parse(objectId->asString());
        const float length = std::sqrt(rotation[0] * rotation[0] + rotation[1] * rotation[1] +
                                       rotation[2] * rotation[2] + rotation[3] * rotation[3]);
        if (!persistentId || persistentId->isNil() || length < 0.999f || length > 1.001f ||
            scale[0] == 0.f || scale[1] == 0.f || scale[2] == 0.f)
            return failure<LoadedSceneTemplate>(DiagnosticCode::InvalidArgument,
                                                "scene identity, rotation, or scale is invalid",
                                                "$.nodes[" + std::to_string(i) + "]");
        const float x = rotation[0] / length, y = rotation[1] / length;
        const float z = rotation[2] / length, w = rotation[3] / length;
        const float pitch = std::atan2(2.f * (w * x + y * z), 1.f - 2.f * (x * x + y * y));
        const float yaw = std::asin(std::clamp(2.f * (w * y - z * x), -1.f, 1.f));
        const float roll = std::atan2(2.f * (w * z + x * y), 1.f - 2.f * (y * y + z * z));
        scene::NodeDesc desc;
        desc.id = std::to_string(sourceId->asInt());
        desc.key = desc.id;
        desc.name = name->asString();
        desc.persistentId = *persistentId;
        desc.withPosition(position[0], position[1], position[2])
            .withRotation(yaw * radiansToDegrees, pitch * radiansToDegrees,
                          roll * radiansToDegrees)
            .withScaleXYZ(scale[0], scale[1], scale[2]);
        const std::uint64_t id = static_cast<std::uint64_t>(sourceId->asInt());
        if (!flat.emplace(id, FlatNode{id, static_cast<std::uint64_t>(parentId->asInt()),
                                       std::move(desc)}).second)
            return failure<LoadedSceneTemplate>(DiagnosticCode::Conflict,
                                                "scene contains duplicate sourceFileId");
    }
    for (const auto& [id, node] : flat)
        if (node.parentId != 0 && !flat.contains(node.parentId))
            return failure<LoadedSceneTemplate>(DiagnosticCode::NotFound,
                                                "scene parent does not exist", std::to_string(id));
    std::set<std::uint64_t> visiting, completed;
    std::function<Result<scene::NodeDesc>(std::uint64_t, std::uint32_t)> build =
        [&](std::uint64_t id, std::uint32_t depth) -> Result<scene::NodeDesc> {
        if (depth > limits.maximumDepth)
            return failure<scene::NodeDesc>(DiagnosticCode::InvalidArgument,
                                            "scene hierarchy exceeds depth budget");
        if (!visiting.emplace(id).second)
            return failure<scene::NodeDesc>(DiagnosticCode::Conflict,
                                            "scene hierarchy contains a cycle");
        scene::NodeDesc result = flat.at(id).desc;
        for (const auto& [childId, child] : flat) {
            if (child.parentId != id) continue;
            auto built = build(childId, depth + 1);
            if (!built) return built;
            result.children.push_back(std::move(built).takeValue());
        }
        visiting.erase(id);
        completed.emplace(id);
        return Result<scene::NodeDesc>::success(std::move(result));
    };
    scene::NodeDesc resultRoot;
    resultRoot.id = "asset-root";
    resultRoot.key = "asset-root";
    resultRoot.name = "Asset Scene Template";
    for (const auto& [id, node] : flat) {
        if (node.parentId != 0) continue;
        auto built = build(id, 1);
        if (!built) return Result<LoadedSceneTemplate>::failure(built.status());
        resultRoot.children.push_back(std::move(built).takeValue());
    }
    if (resultRoot.children.empty() || completed.size() != flat.size())
        return failure<LoadedSceneTemplate>(DiagnosticCode::Conflict,
                                            "scene hierarchy has no valid root");
    return Result<LoadedSceneTemplate>::success(
        {sceneTemplate, std::move(resultRoot), std::move(payload).takeValue().variant});
}

}  // namespace eve::asset_scene
