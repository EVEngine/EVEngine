#include "editor/EditorPhysicsAsset.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eve::editor {
namespace {
const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>(); if (!object) return nullptr;
    const auto found = object->find(key); return found == object->end() ? nullptr : &found->second;
}
EditorResult<PhysicsColliderAssetGeometry> error(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<PhysicsColliderAssetGeometry>::error(status, RuleId(rule), std::move(message));
}
bool floats(const EditorValue* value, std::vector<float>& output) {
    const auto* array = value ? value->getIf<EditorValue::Array>() : nullptr;
    if (!array) return false;
    output.reserve(array->size());
    for (const auto& entry : *array) {
        const auto* number = entry.getIf<double>();
        if (!number || !std::isfinite(*number) || *number < -std::numeric_limits<float>::max() ||
            *number > std::numeric_limits<float>::max()) return false;
        output.push_back(static_cast<float>(*number));
    }
    return true;
}
bool indices(const EditorValue* value, std::vector<std::int32_t>& output) {
    const auto* array = value ? value->getIf<EditorValue::Array>() : nullptr;
    if (!array) return false;
    output.reserve(array->size());
    for (const auto& entry : *array) {
        const auto* number = entry.getIf<int64_t>();
        if (!number || *number < 0 || *number > std::numeric_limits<std::int32_t>::max()) return false;
        output.push_back(static_cast<std::int32_t>(*number));
    }
    return true;
}
bool convexPolygon(const std::vector<float>& vertices) {
    if (vertices.size() < 6 || vertices.size() > 16 || vertices.size() % 2 != 0) return false;
    const size_t count = vertices.size() / 2;
    double sign = 0.0;
    for (size_t index = 0; index < count; ++index) {
        const size_t next = (index + 1) % count;
        const size_t after = (index + 2) % count;
        const double ax = vertices[next * 2] - vertices[index * 2];
        const double ay = vertices[next * 2 + 1] - vertices[index * 2 + 1];
        const double bx = vertices[after * 2] - vertices[next * 2];
        const double by = vertices[after * 2 + 1] - vertices[next * 2 + 1];
        const double cross = ax * by - ay * bx;
        if (std::abs(cross) <= 1e-8) return false;
        if (sign == 0.0) sign = cross;
        else if ((cross > 0.0) != (sign > 0.0)) return false;
    }
    return true;
}
}

EditorResult<PhysicsColliderAssetGeometry> AssetDatabasePhysicsColliderResolver::resolve(
    const std::string& reference, const std::string& expectedKind) const {
    if (!database_ || reference.empty()) return error(EditorStatus::Rejected,
        "editor.physics.invalid-asset-reference", "Collider resolver requires AssetDB and asset reference");
    auto found = database_->find(AssetGuid(reference));
    if (!found.value) found = database_->findByUri(reference);
    if (!found.value) return error(EditorStatus::NotFound, "editor.physics.collider-asset-not-found",
                                   "Collider asset was not found by GUID or logical URI");
    const AssetRecord& record = *found.value;
    if (record.status != AssetStatus::Ready && record.status != AssetStatus::Warning)
        return error(EditorStatus::Conflict, "editor.physics.collider-asset-not-ready",
                     "Collider asset is not ready for runtime construction");
    const auto* kindEntry = field(record.metadata, "kind");
    const auto* kind = kindEntry ? kindEntry->getIf<std::string>() : nullptr;
    if (!kind || *kind != expectedKind) return error(EditorStatus::Conflict,
        "editor.physics.collider-kind-mismatch", "Collider asset kind does not match the authored shape");
    PhysicsColliderAssetGeometry result; result.kind = *kind;
    if (*kind == "polygon" || *kind == "chain") {
        if (!floats(field(record.metadata, "vertices"), result.vertices) ||
            result.vertices.size() % 2 != 0 ||
            (*kind == "polygon" && !convexPolygon(result.vertices)) ||
            (*kind == "chain" && (result.vertices.size() < 4 || result.vertices.size() > 200000)))
            return error(EditorStatus::Rejected, "editor.physics.invalid-collider-vertices-2d",
                         "2D collider requires bounded finite packed XY vertices");
        if (*kind == "chain") {
            const auto* loopValue = field(record.metadata, "loop");
            const auto* loop = loopValue ? loopValue->getIf<bool>() : nullptr;
            result.loop = loop ? *loop : false;
            if (result.loop && result.vertices.size() < 6)
                return error(EditorStatus::Rejected, "editor.physics.invalid-chain-loop",
                             "Closed chain collider requires at least three vertices");
            for (size_t index = 2; index < result.vertices.size(); index += 2)
                if (result.vertices[index] == result.vertices[index - 2] &&
                    result.vertices[index + 1] == result.vertices[index - 1])
                    return error(EditorStatus::Rejected, "editor.physics.duplicate-chain-vertex",
                                 "Chain collider contains consecutive duplicate vertices");
        }
    } else if (*kind == "convex-hull" || *kind == "triangle-mesh") {
        if (!floats(field(record.metadata, "vertices"), result.vertices) || result.vertices.size() % 3 != 0 ||
            result.vertices.size() > 3000000 || (*kind == "convex-hull" && result.vertices.size() < 12))
            return error(EditorStatus::Rejected, "editor.physics.invalid-collider-vertices",
                         "Collider asset requires finite packed XYZ vertices");
        if (*kind == "triangle-mesh") {
            if (!indices(field(record.metadata, "indices"), result.indices) || result.indices.size() % 3 != 0 ||
                result.indices.empty() || result.indices.size() > 6000000)
                return error(EditorStatus::Rejected, "editor.physics.invalid-collider-indices",
                             "Triangle mesh requires bounded packed triangle indices");
            const std::size_t vertexCount = result.vertices.size() / 3;
            if (std::any_of(result.indices.begin(), result.indices.end(), [vertexCount](std::int32_t index) {
                    return static_cast<std::size_t>(index) >= vertexCount;
                })) return error(EditorStatus::Rejected, "editor.physics.collider-index-range",
                                 "Triangle mesh index exceeds its vertex count");
        }
    } else if (*kind == "height-field") {
        const auto integer = [&](const char* key) -> const int64_t* {
            const auto* value = field(record.metadata, key);
            return value ? value->getIf<int64_t>() : nullptr;
        };
        const auto number = [&](const char* key) -> const double* {
            const auto* value = field(record.metadata, key);
            return value ? value->getIf<double>() : nullptr;
        };
        const auto* countX = integer("countX"); const auto* countZ = integer("countZ");
        const auto* sizeX = number("cellSizeX"); const auto* sizeZ = number("cellSizeZ");
        if (!countX || !countZ || *countX < 2 || *countZ < 2 || *countX > 100000 || *countZ > 100000 ||
            static_cast<std::uint64_t>(*countX) * static_cast<std::uint64_t>(*countZ) > 100000000ULL ||
            !sizeX || !sizeZ || !std::isfinite(*sizeX) || !std::isfinite(*sizeZ) || *sizeX <= 0.0 ||
            *sizeZ <= 0.0 || !floats(field(record.metadata, "heights"), result.heights) ||
            result.heights.size() != static_cast<std::size_t>(*countX) * static_cast<std::size_t>(*countZ))
            return error(EditorStatus::Rejected, "editor.physics.invalid-height-field",
                         "Height field dimensions, spacing or samples are invalid");
        result.countX = static_cast<int>(*countX); result.countZ = static_cast<int>(*countZ);
        result.cellSizeX = static_cast<float>(*sizeX); result.cellSizeZ = static_cast<float>(*sizeZ);
        const auto range = std::minmax_element(result.heights.begin(), result.heights.end());
        result.minimumHeight = *range.first; result.maximumHeight = *range.second;
        if (result.minimumHeight == result.maximumHeight) result.maximumHeight = result.minimumHeight + 0.001F;
    } else {
        return error(EditorStatus::Unsupported, "editor.physics.unsupported-collider-asset",
                     "Collider asset kind is unsupported");
    }
    return EditorResult<PhysicsColliderAssetGeometry>::applied(std::move(result));
}

}  // namespace eve::editor
