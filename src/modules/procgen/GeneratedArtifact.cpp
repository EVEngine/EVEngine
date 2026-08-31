#include "procgen/GeneratedArtifact.h"

#include "procgen/algorithms/CastleMesh.h"
#include "procgen/algorithms/HexTerrain.h"
#include "procgen/algorithms/MarchingCubes.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace eve::procgen {
namespace {

eve::Result<GeneratedArtifact> rejectedArtifact(eve::DiagnosticCode code, std::string message) {
    return eve::Result<GeneratedArtifact>::failure(
        eve::Diagnostic::error(code, std::move(message), "generatedArtifact"));
}

eve::Result<ArtifactPart> rejectedPart(eve::DiagnosticCode code, std::string message) {
    return eve::Result<ArtifactPart>::failure(
        eve::Diagnostic::error(code, std::move(message), "generatedArtifact.part"));
}

std::uint64_t fnv1a(std::string_view text) noexcept {
    std::uint64_t value = 14695981039346656037ull;
    for (const unsigned char byte : text) {
        value ^= byte;
        value *= 1099511628211ull;
    }
    return value;
}

bool hasControl(std::string_view text) noexcept {
    for (const unsigned char byte : text) {
        if (std::iscntrl(byte) != 0) return true;
    }
    return false;
}

bool typeMatches(ArtifactType type, const ArtifactLeafPayload &payload) noexcept {
    switch (type) {
        case ArtifactType::Grid: return std::holds_alternative<Grid2D>(payload);
        case ArtifactType::PointSet: return std::holds_alternative<PointSet>(payload);
        case ArtifactType::MeshData: return std::holds_alternative<MeshData>(payload);
        case ArtifactType::ImageData: return std::holds_alternative<ImageData>(payload);
        case ArtifactType::Collider: return std::holds_alternative<Collider>(payload);
        case ArtifactType::Composite: return false;
    }
    return false;
}

bool typeMatches(ArtifactType type, const GeneratedArtifact::Payload &payload) noexcept {
    return type == artifactType(payload);
}

bool finiteBounds(const Bounds &bounds) noexcept {
    return bounds.valid && std::isfinite(bounds.minX) && std::isfinite(bounds.minY) && std::isfinite(bounds.minZ) &&
           std::isfinite(bounds.maxX) && std::isfinite(bounds.maxY) && std::isfinite(bounds.maxZ) &&
           bounds.minX <= bounds.maxX && bounds.minY <= bounds.maxY && bounds.minZ <= bounds.maxZ;
}

bool validMesh(const MeshData &mesh) noexcept {
    if (mesh.empty() || mesh.positions().size() % 3u != 0u || mesh.indices().size() % 3u != 0u) return false;
    const std::size_t vertices = mesh.positions().size() / 3u;
    if ((!mesh.normals().empty() && mesh.normals().size() != mesh.positions().size()) ||
        (!mesh.uvs().empty() && mesh.uvs().size() != vertices * 2u))
        return false;
    return std::all_of(mesh.positions().begin(), mesh.positions().end(),
                       [](float value) { return std::isfinite(value); }) &&
           std::all_of(mesh.normals().begin(), mesh.normals().end(),
                       [](float value) { return std::isfinite(value); }) &&
           std::all_of(mesh.uvs().begin(), mesh.uvs().end(), [](float value) { return std::isfinite(value); }) &&
           std::all_of(mesh.indices().begin(), mesh.indices().end(),
                       [vertices](std::uint32_t index) { return index < vertices; });
}

bool validLeaf(ArtifactType type, const ArtifactLeafPayload &payload) noexcept {
    if (!typeMatches(type, payload)) return false;
    return std::visit(
        [](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Grid2D>)
                return value.getWidth() > 0 && value.getHeight() > 0;
            else if constexpr (std::is_same_v<T, PointSet>)
                return !value.points().empty() &&
                       std::all_of(value.points().begin(), value.points().end(), [](const auto &point) {
                           return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
                       });
            else if constexpr (std::is_same_v<T, MeshData>)
                return validMesh(value);
            else
                return value.isValid();
        },
        payload);
}

bool validDependencies(ArtifactId self, const std::vector<ArtifactId> &dependencies) {
    std::unordered_set<ArtifactId> seen;
    for (ArtifactId dependency : dependencies) {
        if (dependency.isNil() || dependency == self || !seen.emplace(dependency).second) return false;
    }
    return true;
}

bool validPart(const ArtifactPart &part) {
    return !part.role.empty() && !part.id.isNil() && !part.schemaVersion.isZero() && !part.buildKey.empty() &&
           finiteBounds(part.bounds) && validDependencies(part.id, part.dependencies) &&
           validLeaf(part.type, part.payload);
}

bool validArtifact(const GeneratedArtifact &artifact) {
    if (artifact.id.isNil() || artifact.schemaVersion.isZero() || artifact.buildKey.empty() ||
        !finiteBounds(artifact.bounds) || !validDependencies(artifact.id, artifact.dependencies) ||
        !typeMatches(artifact.type, artifact.payload))
        return false;
    if (artifact.type != ArtifactType::Composite) {
        return std::visit(
            [&](const auto &value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, CompositeArtifact>)
                    return false;
                else
                    return validLeaf(artifact.type, ArtifactLeafPayload(value));
            },
            artifact.payload);
    }
    const auto &composite = std::get<CompositeArtifact>(artifact.payload);
    if (composite.children.empty()) return false;
    std::unordered_set<ArtifactId>  identities;
    std::unordered_set<std::string> roles;
    for (const ArtifactPart &child : composite.children) {
        if (child.id == artifact.id || !validPart(child) || !identities.emplace(child.id).second ||
            !roles.emplace(child.role).second)
            return false;
    }
    return true;
}

eve::Result<void> validateBatchAgainstStore(const ArtifactStore                  &store,
                                            const std::vector<GeneratedArtifact> &artifacts) {
    if (artifacts.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "artifact publish batch must not be empty", "artifacts"));

    std::unordered_map<ArtifactId, ArtifactType> batch;
    batch.reserve(artifacts.size() * 5u);
    for (const GeneratedArtifact &artifact : artifacts) {
        if (!validArtifact(artifact))
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "artifact structure or typed payload is invalid", "artifact"));
        if (store.typeOf(artifact.id).has_value() || !batch.emplace(artifact.id, artifact.type).second)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "artifact identity is already published", "artifact.id"));
        if (artifact.type == ArtifactType::Composite) {
            for (const ArtifactPart &part : std::get<CompositeArtifact>(artifact.payload).children) {
                if (store.typeOf(part.id).has_value() || !batch.emplace(part.id, part.type).second)
                    return eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::Conflict, "composite part identity conflicts", "artifact.children.id"));
            }
        }
    }

    const auto typeFor = [&store, &batch](ArtifactId id) -> std::optional<ArtifactType> {
        const auto inBatch = batch.find(id);
        if (inBatch != batch.end()) return inBatch->second;
        return store.typeOf(id);
    };
    const auto checkDependencies =
        [&typeFor](ArtifactType type, const std::vector<ArtifactId> &dependencies) -> std::optional<eve::Diagnostic> {
        bool meshDependency = false;
        for (ArtifactId dependency : dependencies) {
            const auto found = typeFor(dependency);
            if (!found)
                return eve::Diagnostic::error(eve::DiagnosticCode::NotFound,
                                              "artifact dependency is not published or in batch",
                                              "artifact.dependencies");
            meshDependency = meshDependency || *found == ArtifactType::MeshData;
        }
        if (type == ArtifactType::Collider && !meshDependency)
            return eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                          "collider must depend on a mesh artifact", "artifact.dependencies");
        return std::nullopt;
    };
    for (const GeneratedArtifact &artifact : artifacts) {
        if (auto error = checkDependencies(artifact.type, artifact.dependencies))
            return eve::Result<void>::failure(std::move(*error));
        if (artifact.type == ArtifactType::Composite) {
            for (const ArtifactPart &part : std::get<CompositeArtifact>(artifact.payload).children) {
                if (auto error = checkDependencies(part.type, part.dependencies))
                    return eve::Result<void>::failure(std::move(*error));
            }
        }
    }
    return eve::Result<void>::success();
}

const eve::Value *stateMember(const eve::Value::Object &object, const char *name) noexcept {
    const auto found = object.find(name);
    return found == object.end() ? nullptr : &found->second;
}

eve::Value encodeFloatArray(const std::vector<float> &values) {
    eve::Value::Array result;
    result.reserve(values.size());
    for (const float value : values) result.emplace_back(value);
    return eve::Value(std::move(result));
}

eve::Value encodeUInt32Array(const std::vector<std::uint32_t> &values) {
    eve::Value::Array result;
    result.reserve(values.size());
    for (const std::uint32_t value : values) result.emplace_back(std::int64_t(value));
    return eve::Value(std::move(result));
}

eve::Value encodeUInt8Array(const std::vector<std::uint8_t> &values) {
    eve::Value::Array result;
    result.reserve(values.size());
    for (const std::uint8_t value : values) result.emplace_back(std::int64_t(value));
    return eve::Value(std::move(result));
}

eve::Value encodeIntArray(const std::vector<int> &values) {
    eve::Value::Array result;
    result.reserve(values.size());
    for (const int value : values) result.emplace_back(std::int64_t(value));
    return eve::Value(std::move(result));
}

eve::Value encodeBounds(const Bounds &bounds) {
    eve::Value::Object result;
    result.emplace("minX", eve::Value(bounds.minX));
    result.emplace("minY", eve::Value(bounds.minY));
    result.emplace("minZ", eve::Value(bounds.minZ));
    result.emplace("maxX", eve::Value(bounds.maxX));
    result.emplace("maxY", eve::Value(bounds.maxY));
    result.emplace("maxZ", eve::Value(bounds.maxZ));
    result.emplace("valid", eve::Value(bounds.valid));
    return eve::Value(std::move(result));
}

eve::Value encodeMetadata(const eve::Value::Object &metadata) { return eve::Value(metadata); }

eve::Value encodeDependencies(const std::vector<ArtifactId> &dependencies) {
    eve::Value::Array result;
    result.reserve(dependencies.size());
    for (const ArtifactId dependency : dependencies) result.emplace_back(dependency.format());
    return eve::Value(std::move(result));
}

eve::Value encodeStringMap(const std::unordered_map<std::string, std::string> &values) {
    eve::Value::Object result;
    for (const auto &[key, value] : values) result.emplace(key, eve::Value(value));
    return eve::Value(std::move(result));
}

eve::Value encodeFloatMap(const std::unordered_map<std::string, float> &values) {
    eve::Value::Object result;
    for (const auto &[key, value] : values) result.emplace(key, eve::Value(value));
    return eve::Value(std::move(result));
}

eve::Value encodeIntMap(const std::unordered_map<std::string, std::int64_t> &values) {
    eve::Value::Object result;
    for (const auto &[key, value] : values) result.emplace(key, eve::Value(value));
    return eve::Value(std::move(result));
}

eve::Value encodeBoolMap(const std::unordered_map<std::string, bool> &values) {
    eve::Value::Object result;
    for (const auto &[key, value] : values) result.emplace(key, eve::Value(value));
    return eve::Value(std::move(result));
}

eve::Value encodeVectorMap(const std::unordered_map<std::string, ProcgenAttributeVector> &values) {
    eve::Value::Object result;
    for (const auto &[key, value] : values) {
        eve::Value::Array encoded{eve::Value(value.x), eve::Value(value.y), eve::Value(value.z)};
        result.emplace(key, eve::Value(std::move(encoded)));
    }
    return eve::Value(std::move(result));
}

eve::Value encodeGrid(const Grid2D &grid) {
    eve::Value::Object result;
    result.emplace("width", eve::Value(grid.getWidth()));
    result.emplace("height", eve::Value(grid.getHeight()));
    result.emplace("cells", encodeUInt32Array(grid.cells()));
    result.emplace("detail", encodeUInt8Array(grid.detail()));
    result.emplace("metadata", encodeStringMap(grid.metadata()));
    eve::Value::Array objects;
    objects.reserve(grid.objects().size());
    for (const GridObject &object : grid.objects()) {
        eve::Value::Object encoded;
        encoded.emplace("name", eve::Value(object.name));
        encoded.emplace("type", eve::Value(object.type));
        encoded.emplace("x", eve::Value(object.x));
        encoded.emplace("y", eve::Value(object.y));
        encoded.emplace("width", eve::Value(object.width));
        encoded.emplace("height", eve::Value(object.height));
        encoded.emplace("gid", eve::Value(std::int64_t(object.gid)));
        objects.emplace_back(std::move(encoded));
    }
    result.emplace("objects", eve::Value(std::move(objects)));
    return eve::Value(std::move(result));
}

eve::Value encodePointSet(const PointSet &points) {
    eve::Value::Object result;
    eve::Value::Array  encodedPoints;
    encodedPoints.reserve(points.points().size());
    for (std::size_t index = 0; index < points.points().size(); ++index) {
        const ProcgenPoint& point = points.points()[index];
        eve::Value::Object encoded;
        encoded.emplace("x", eve::Value(point.x));
        encoded.emplace("y", eve::Value(point.y));
        encoded.emplace("z", eve::Value(point.z));
        encoded.emplace("normalX", eve::Value(point.normalX));
        encoded.emplace("normalY", eve::Value(point.normalY));
        encoded.emplace("normalZ", eve::Value(point.normalZ));
        encoded.emplace("pitch", eve::Value(point.pitch));
        encoded.emplace("yaw", eve::Value(point.yaw));
        encoded.emplace("roll", eve::Value(point.roll));
        encoded.emplace("scaleX", eve::Value(point.scaleX));
        encoded.emplace("scaleY", eve::Value(point.scaleY));
        encoded.emplace("scaleZ", eve::Value(point.scaleZ));
        encoded.emplace("density", eve::Value(point.density));
        encoded.emplace("seed", eve::Value(std::int64_t(point.seed)));
        encoded.emplace("boundsMinX", eve::Value(point.boundsMinX));
        encoded.emplace("boundsMinY", eve::Value(point.boundsMinY));
        encoded.emplace("boundsMinZ", eve::Value(point.boundsMinZ));
        encoded.emplace("boundsMaxX", eve::Value(point.boundsMaxX));
        encoded.emplace("boundsMaxY", eve::Value(point.boundsMaxY));
        encoded.emplace("boundsMaxZ", eve::Value(point.boundsMaxZ));
        encoded.emplace("colorR", eve::Value(point.colorR));
        encoded.emplace("colorG", eve::Value(point.colorG));
        encoded.emplace("colorB", eve::Value(point.colorB));
        encoded.emplace("colorA", eve::Value(point.colorA));
        encoded.emplace("steepness", eve::Value(point.steepness));
        eve::Value::Object floatAttributes;
        eve::Value::Object intAttributes;
        eve::Value::Object boolAttributes;
        eve::Value::Object vectorAttributes;
        eve::Value::Object stringAttributes;
        for (std::size_t column = 0; column < points.attributes().columnCount(); ++column) {
            const std::string name(points.attributes().columnName(column));
            if (!points.attributes().has(index, name)) continue;
            switch (*points.attributes().typeOf(name)) {
                case ProcgenAttributeType::Float:
                    floatAttributes.emplace(name, eve::Value(*points.attributes().getFloat(index, name)));
                    break;
                case ProcgenAttributeType::Int:
                    intAttributes.emplace(name, eve::Value(*points.attributes().getInt(index, name)));
                    break;
                case ProcgenAttributeType::Bool:
                    boolAttributes.emplace(name, eve::Value(*points.attributes().getBool(index, name)));
                    break;
                case ProcgenAttributeType::Vector: {
                    const auto        value = *points.attributes().getVector(index, name);
                    eve::Value::Array components{eve::Value(value.x), eve::Value(value.y), eve::Value(value.z)};
                    vectorAttributes.emplace(name, eve::Value(std::move(components)));
                    break;
                }
                case ProcgenAttributeType::String:
                    stringAttributes.emplace(name,
                                             eve::Value(std::string(*points.attributes().getString(index, name))));
                    break;
            }
        }
        encoded.emplace("floatAttributes", eve::Value(std::move(floatAttributes)));
        encoded.emplace("intAttributes", eve::Value(std::move(intAttributes)));
        encoded.emplace("boolAttributes", eve::Value(std::move(boolAttributes)));
        encoded.emplace("vectorAttributes", eve::Value(std::move(vectorAttributes)));
        encoded.emplace("stringAttributes", eve::Value(std::move(stringAttributes)));
        encodedPoints.emplace_back(std::move(encoded));
    }
    result.emplace("points", eve::Value(std::move(encodedPoints)));
    return eve::Value(std::move(result));
}

eve::Value encodeMesh(const MeshData &mesh) {
    eve::Value::Object result;
    result.emplace("positions", encodeFloatArray(mesh.positions()));
    result.emplace("normals", encodeFloatArray(mesh.normals()));
    result.emplace("uvs", encodeFloatArray(mesh.uvs()));
    result.emplace("indices", encodeUInt32Array(mesh.indices()));
    result.emplace("groupNames", [&mesh] {
        eve::Value::Array names;
        names.reserve(mesh.groupNames().size());
        for (const auto &name : mesh.groupNames()) names.emplace_back(name);
        return eve::Value(std::move(names));
    }());
    result.emplace("triangleGroups", encodeIntArray(mesh.triangleGroups()));
    result.emplace("activeGroup", eve::Value(mesh.activeGroup()));
    result.emplace("metadata", encodeStringMap(mesh.metadata()));
    return eve::Value(std::move(result));
}

eve::Value encodeImage(const ImageData &image) {
    eve::Value::Object result;
    result.emplace("width", eve::Value(image.width));
    result.emplace("height", eve::Value(image.height));
    result.emplace("channels", eve::Value(image.channels));
    result.emplace("format", eve::Value(image.format));
    result.emplace("pixels", encodeUInt8Array(image.pixels));
    return eve::Value(std::move(result));
}

eve::Value encodeCollider(const Collider &collider) {
    eve::Value::Object result;
    result.emplace("vertices", encodeFloatArray(collider.vertices));
    result.emplace("indices", encodeUInt32Array(collider.indices));
    result.emplace("bounds", encodeBounds(collider.bounds));
    result.emplace("shape", eve::Value(collider.shape));
    return eve::Value(std::move(result));
}

eve::Value encodeLeafPayload(ArtifactType type, const ArtifactLeafPayload &payload) {
    eve::Value::Object result;
    result.emplace("kind", eve::Value(artifactTypeName(type)));
    result.emplace("data", std::visit(
                               [](const auto &value) -> eve::Value {
                                   using T = std::decay_t<decltype(value)>;
                                   if constexpr (std::is_same_v<T, Grid2D>)
                                       return encodeGrid(value);
                                   else if constexpr (std::is_same_v<T, PointSet>)
                                       return encodePointSet(value);
                                   else if constexpr (std::is_same_v<T, MeshData>)
                                       return encodeMesh(value);
                                   else if constexpr (std::is_same_v<T, ImageData>)
                                       return encodeImage(value);
                                   else if constexpr (std::is_same_v<T, Collider>)
                                       return encodeCollider(value);
                                   else {
                                       static_assert(std::is_same_v<T, void>, "unhandled artifact leaf payload");
                                       return eve::Value{};
                                   }
                               },
                               payload));
    return eve::Value(std::move(result));
}

eve::Value encodePart(const ArtifactPart &part) {
    eve::Value::Object result;
    result.emplace("role", eve::Value(part.role));
    result.emplace("artifactId", eve::Value(part.id.format()));
    result.emplace("type", eve::Value(artifactTypeName(part.type)));
    result.emplace("schemaVersion", eve::Value(std::to_string(part.schemaVersion.value())));
    result.emplace("buildKey", eve::Value(part.buildKey.format()));
    result.emplace("bounds", encodeBounds(part.bounds));
    result.emplace("dependencies", encodeDependencies(part.dependencies));
    result.emplace("metadata", encodeMetadata(part.metadata));
    result.emplace("payload", encodeLeafPayload(part.type, part.payload));
    return eve::Value(std::move(result));
}

eve::Value encodeArtifact(const GeneratedArtifact &artifact) {
    eve::Value::Object result;
    result.emplace("artifactId", eve::Value(artifact.id.format()));
    result.emplace("type", eve::Value(artifactTypeName(artifact.type)));
    result.emplace("schemaVersion", eve::Value(std::to_string(artifact.schemaVersion.value())));
    result.emplace("buildKey", eve::Value(artifact.buildKey.format()));
    result.emplace("bounds", encodeBounds(artifact.bounds));
    result.emplace("dependencies", encodeDependencies(artifact.dependencies));
    result.emplace("metadata", encodeMetadata(artifact.metadata));
    if (artifact.type == ArtifactType::Composite) {
        eve::Value::Array children;
        const auto       &composite = std::get<CompositeArtifact>(artifact.payload);
        children.reserve(composite.children.size());
        for (const ArtifactPart &part : composite.children) children.emplace_back(encodePart(part));
        eve::Value::Object payload;
        payload.emplace("kind", eve::Value("composite"));
        payload.emplace("children", eve::Value(std::move(children)));
        result.emplace("payload", eve::Value(std::move(payload)));
    } else {
        result.emplace("payload", std::visit(
                                      [&artifact](const auto &value) -> eve::Value {
                                          using T = std::decay_t<decltype(value)>;
                                          if constexpr (std::is_same_v<T, CompositeArtifact>)
                                              return eve::Value{};
                                          else
                                              return encodeLeafPayload(artifact.type, ArtifactLeafPayload(value));
                                      },
                                      artifact.payload));
    }
    return eve::Value(std::move(result));
}

bool readStringValue(const eve::Value::Object &object, const char *name, std::string &output, bool allowEmpty = false) {
    const eve::Value *value = stateMember(object, name);
    const auto       *text  = value ? value->getIf<std::string>() : nullptr;
    if (!text || (!allowEmpty && text->empty())) return false;
    output = *text;
    return true;
}

bool readIntValue(const eve::Value::Object &object, const char *name, std::int64_t &output) {
    const eve::Value *value   = stateMember(object, name);
    const auto       *integer = value ? value->getIf<std::int64_t>() : nullptr;
    if (!integer) return false;
    output = *integer;
    return true;
}

bool readFloatValue(const eve::Value::Object &object, const char *name, float &output) {
    const eve::Value *value = stateMember(object, name);
    if (!value) return false;
    if (const auto *number = value->getIf<double>()) {
        if (!std::isfinite(*number) || *number < -std::numeric_limits<float>::max() ||
            *number > std::numeric_limits<float>::max())
            return false;
        output = static_cast<float>(*number);
        return std::isfinite(output);
    }
    if (const auto *integer = value->getIf<std::int64_t>()) {
        output = static_cast<float>(*integer);
        return std::isfinite(output);
    }
    return false;
}

bool readBoolValue(const eve::Value::Object &object, const char *name, bool &output) {
    const eve::Value *value   = stateMember(object, name);
    const auto       *boolean = value ? value->getIf<bool>() : nullptr;
    if (!boolean) return false;
    output = *boolean;
    return true;
}

template <class T>
bool decodeNumericArray(const eve::Value *encoded, std::vector<T> &output) {
    const auto *array = encoded ? encoded->getIf<eve::Value::Array>() : nullptr;
    if (!array) return false;
    output.clear();
    output.reserve(array->size());
    for (const eve::Value &value : *array) {
        if constexpr (std::is_same_v<T, float>) {
            if (const auto *number = value.getIf<double>()) {
                if (!std::isfinite(*number) || *number < -std::numeric_limits<float>::max() ||
                    *number > std::numeric_limits<float>::max())
                    return false;
                output.push_back(static_cast<float>(*number));
            } else if (const auto *integer = value.getIf<std::int64_t>()) {
                const float converted = static_cast<float>(*integer);
                if (!std::isfinite(converted)) return false;
                output.push_back(converted);
            } else {
                return false;
            }
        } else if constexpr (std::is_same_v<T, std::uint8_t> || std::is_same_v<T, std::uint32_t>) {
            const auto *integer = value.getIf<std::int64_t>();
            if (!integer || *integer < 0 || static_cast<std::uint64_t>(*integer) > std::numeric_limits<T>::max())
                return false;
            output.push_back(static_cast<T>(*integer));
        } else {
            const auto *integer = value.getIf<std::int64_t>();
            if (!integer || *integer < std::numeric_limits<T>::min() || *integer > std::numeric_limits<T>::max())
                return false;
            output.push_back(static_cast<T>(*integer));
        }
    }
    return true;
}

bool decodeBounds(const eve::Value *encoded, Bounds &bounds) {
    const auto *object = encoded ? encoded->getIf<eve::Value::Object>() : nullptr;
    if (!object || !readFloatValue(*object, "minX", bounds.minX) || !readFloatValue(*object, "minY", bounds.minY) ||
        !readFloatValue(*object, "minZ", bounds.minZ) || !readFloatValue(*object, "maxX", bounds.maxX) ||
        !readFloatValue(*object, "maxY", bounds.maxY) || !readFloatValue(*object, "maxZ", bounds.maxZ) ||
        !readBoolValue(*object, "valid", bounds.valid))
        return false;
    return finiteBounds(bounds);
}

bool decodeStringMap(const eve::Value *encoded, std::unordered_map<std::string, std::string> &output) {
    const auto *object = encoded ? encoded->getIf<eve::Value::Object>() : nullptr;
    if (!object) return false;
    output.clear();
    for (const auto &[key, value] : *object) {
        const auto *text = value.getIf<std::string>();
        if (!text) return false;
        output.emplace(key, *text);
    }
    return true;
}

bool decodeFloatMap(const eve::Value *encoded, std::unordered_map<std::string, float> &output) {
    const auto *object = encoded ? encoded->getIf<eve::Value::Object>() : nullptr;
    if (!object) return false;
    output.clear();
    for (const auto &[key, value] : *object) {
        if (const auto *number = value.getIf<double>()) {
            if (!std::isfinite(*number) || *number < -std::numeric_limits<float>::max() ||
                *number > std::numeric_limits<float>::max())
                return false;
            output.emplace(key, static_cast<float>(*number));
        } else if (const auto *integer = value.getIf<std::int64_t>()) {
            const float converted = static_cast<float>(*integer);
            if (!std::isfinite(converted)) return false;
            output.emplace(key, converted);
        } else {
            return false;
        }
    }
    return true;
}

bool decodeOptionalIntMap(const eve::Value *encoded,
                          std::unordered_map<std::string, std::int64_t> &output) {
    output.clear();
    if (!encoded) return true;
    const auto *object = encoded->getIf<eve::Value::Object>();
    if (!object) return false;
    for (const auto &[key, value] : *object) {
        const auto *integer = value.getIf<std::int64_t>();
        if (!integer) return false;
        output.emplace(key, *integer);
    }
    return true;
}

bool decodeOptionalBoolMap(const eve::Value *encoded,
                           std::unordered_map<std::string, bool> &output) {
    output.clear();
    if (!encoded) return true;
    const auto *object = encoded->getIf<eve::Value::Object>();
    if (!object) return false;
    for (const auto &[key, value] : *object) {
        const auto *boolean = value.getIf<bool>();
        if (!boolean) return false;
        output.emplace(key, *boolean);
    }
    return true;
}

bool decodeOptionalVectorMap(
    const eve::Value *encoded,
    std::unordered_map<std::string, ProcgenAttributeVector> &output) {
    output.clear();
    if (!encoded) return true;
    const auto *object = encoded->getIf<eve::Value::Object>();
    if (!object) return false;
    for (const auto &[key, value] : *object) {
        std::vector<float> components;
        if (!decodeNumericArray(&value, components) || components.size() != 3) return false;
        output.emplace(key, ProcgenAttributeVector{components[0], components[1], components[2]});
    }
    return true;
}

bool readOptionalFloatValue(const eve::Value::Object &object, const char *name, float &output) {
    const eve::Value *value = stateMember(object, name);
    return !value || readFloatValue(object, name, output);
}

bool decodeMetadata(const eve::Value *encoded, eve::Value::Object &output) {
    const auto *object = encoded ? encoded->getIf<eve::Value::Object>() : nullptr;
    if (!object) return false;
    output = *object;
    return true;
}

bool decodeDependencies(const eve::Value *encoded, std::vector<ArtifactId> &output) {
    const auto *array = encoded ? encoded->getIf<eve::Value::Array>() : nullptr;
    if (!array) return false;
    output.clear();
    output.reserve(array->size());
    for (const eve::Value &value : *array) {
        const auto *text   = value.getIf<std::string>();
        const auto  parsed = text ? ArtifactId::parse(*text) : std::nullopt;
        if (!parsed || parsed->isNil()) return false;
        output.push_back(*parsed);
    }
    return true;
}

std::optional<ArtifactType> decodeArtifactType(const eve::Value::Object &object, const char *name = "type") {
    std::string text;
    if (!readStringValue(object, name, text)) return std::nullopt;
    for (const ArtifactType type : {ArtifactType::Grid, ArtifactType::PointSet, ArtifactType::MeshData,
                                    ArtifactType::ImageData, ArtifactType::Collider, ArtifactType::Composite})
        if (text == artifactTypeName(type)) return type;
    return std::nullopt;
}

bool decodeSchemaVersion(const eve::Value::Object &object, eve::SchemaVersion &version) {
    std::string text;
    if (!readStringValue(object, "schemaVersion", text)) return false;
    std::uint64_t value     = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0) return false;
    version = eve::SchemaVersion(value);
    return true;
}

bool decodePayload(ArtifactType type, const eve::Value *encoded, ArtifactLeafPayload &output) {
    const auto       *wrapper = encoded ? encoded->getIf<eve::Value::Object>() : nullptr;
    std::string       kind;
    const eve::Value *data = wrapper ? stateMember(*wrapper, "data") : nullptr;
    if (!wrapper || !readStringValue(*wrapper, "kind", kind) || kind != artifactTypeName(type) || !data) return false;
    const auto *object = data->getIf<eve::Value::Object>();
    if (!object) return false;
    if (type == ArtifactType::Grid) {
        std::int64_t                                 width = 0, height = 0;
        std::vector<std::uint32_t>                   cells;
        std::vector<std::uint8_t>                    detail;
        std::unordered_map<std::string, std::string> metadata;
        const auto                                  *objectsValue = stateMember(*object, "objects");
        const auto *objects = objectsValue ? objectsValue->getIf<eve::Value::Array>() : nullptr;
        if (!readIntValue(*object, "width", width) || !readIntValue(*object, "height", height) || width <= 0 ||
            height <= 0 || width > std::numeric_limits<int>::max() || height > std::numeric_limits<int>::max() ||
            !decodeNumericArray(stateMember(*object, "cells"), cells) ||
            !decodeNumericArray(stateMember(*object, "detail"), detail) || !objects ||
            !decodeStringMap(stateMember(*object, "metadata"), metadata))
            return false;
        const auto expected = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
        if (expected != cells.size() || expected != detail.size()) return false;
        Grid2D grid;
        grid.resize(static_cast<int>(width), static_cast<int>(height));
        grid.cells()  = std::move(cells);
        grid.detail() = std::move(detail);
        for (const auto &[key, value] : metadata) grid.setMeta(key, value);
        for (const eve::Value &encodedObject : *objects) {
            const auto  *item = encodedObject.getIf<eve::Value::Object>();
            std::string  name, objectType;
            float        x = 0.f, y = 0.f, objectWidth = 0.f, objectHeight = 0.f;
            std::int64_t gid = 0;
            if (!item || !readStringValue(*item, "name", name, true) ||
                !readStringValue(*item, "type", objectType, true) || !readFloatValue(*item, "x", x) ||
                !readFloatValue(*item, "y", y) || !readFloatValue(*item, "width", objectWidth) ||
                !readFloatValue(*item, "height", objectHeight) || !readIntValue(*item, "gid", gid) || gid < 0 ||
                gid > std::numeric_limits<std::uint32_t>::max())
                return false;
            grid.addObject(name, objectType, x, y, objectWidth, objectHeight, static_cast<int>(gid));
        }
        output = std::move(grid);
        return true;
    }
    if (type == ArtifactType::PointSet) {
        const auto *encodedPointsValue = stateMember(*object, "points");
        const auto *encodedPoints      = encodedPointsValue ? encodedPointsValue->getIf<eve::Value::Array>() : nullptr;
        if (!encodedPoints) return false;
        PointSet points;
        for (const eve::Value &encodedPoint : *encodedPoints) {
            const auto*                                             item = encodedPoint.getIf<eve::Value::Object>();
            ProcgenPoint                                            point;
            std::int64_t                                            seed = 0;
            std::unordered_map<std::string, float>                  floatAttributes;
            std::unordered_map<std::string, std::int64_t>           intAttributes;
            std::unordered_map<std::string, bool>                   boolAttributes;
            std::unordered_map<std::string, ProcgenAttributeVector> vectorAttributes;
            std::unordered_map<std::string, std::string>            stringAttributes;
            if (!item || !readFloatValue(*item, "x", point.x) || !readFloatValue(*item, "y", point.y) ||
                !readFloatValue(*item, "z", point.z) || !readFloatValue(*item, "normalX", point.normalX) ||
                !readFloatValue(*item, "normalY", point.normalY) || !readFloatValue(*item, "normalZ", point.normalZ) ||
                !readOptionalFloatValue(*item, "pitch", point.pitch) ||
                !readFloatValue(*item, "yaw", point.yaw) ||
                !readOptionalFloatValue(*item, "roll", point.roll) || !readFloatValue(*item, "scaleX", point.scaleX) ||
                !readFloatValue(*item, "scaleY", point.scaleY) || !readFloatValue(*item, "scaleZ", point.scaleZ) ||
                !readFloatValue(*item, "density", point.density) || !readIntValue(*item, "seed", seed) || seed < 0 ||
                seed > std::numeric_limits<std::uint32_t>::max() ||
                !readOptionalFloatValue(*item, "boundsMinX", point.boundsMinX) ||
                !readOptionalFloatValue(*item, "boundsMinY", point.boundsMinY) ||
                !readOptionalFloatValue(*item, "boundsMinZ", point.boundsMinZ) ||
                !readOptionalFloatValue(*item, "boundsMaxX", point.boundsMaxX) ||
                !readOptionalFloatValue(*item, "boundsMaxY", point.boundsMaxY) ||
                !readOptionalFloatValue(*item, "boundsMaxZ", point.boundsMaxZ) ||
                !readOptionalFloatValue(*item, "colorR", point.colorR) ||
                !readOptionalFloatValue(*item, "colorG", point.colorG) ||
                !readOptionalFloatValue(*item, "colorB", point.colorB) ||
                !readOptionalFloatValue(*item, "colorA", point.colorA) ||
                !readOptionalFloatValue(*item, "steepness", point.steepness) ||
                !decodeFloatMap(stateMember(*item, "floatAttributes"), floatAttributes) ||
                !decodeOptionalIntMap(stateMember(*item, "intAttributes"), intAttributes) ||
                !decodeOptionalBoolMap(stateMember(*item, "boolAttributes"), boolAttributes) ||
                !decodeOptionalVectorMap(stateMember(*item, "vectorAttributes"), vectorAttributes) ||
                !decodeStringMap(stateMember(*item, "stringAttributes"), stringAttributes))
                return false;
            point.seed = static_cast<std::uint32_t>(seed);
            const int pointIndex = points.appendPoint(std::move(point));
            for (const auto& [name, value] : floatAttributes)
                if (!points.trySetFloatAttribute(pointIndex, name, value).ok()) return false;
            for (const auto& [name, value] : intAttributes)
                if (!points.trySetIntAttribute(pointIndex, name, value).ok()) return false;
            for (const auto& [name, value] : boolAttributes)
                if (!points.trySetBoolAttribute(pointIndex, name, value).ok()) return false;
            for (const auto& [name, value] : vectorAttributes)
                if (!points.trySetVectorAttribute(pointIndex, name, value.x, value.y, value.z).ok()) return false;
            for (const auto& [name, value] : stringAttributes)
                if (!points.trySetStringAttribute(pointIndex, name, value).ok()) return false;
        }
        output = std::move(points);
        return true;
    }
    if (type == ArtifactType::MeshData) {
        MeshData                 mesh;
        std::vector<std::string> names;
        std::vector<int>         assignments;
        std::int64_t             activeGroup       = -1;
        const auto              *encodedNamesValue = stateMember(*object, "groupNames");
        const auto *encodedNames    = encodedNamesValue ? encodedNamesValue->getIf<eve::Value::Array>() : nullptr;
        const auto *encodedMetadata = stateMember(*object, "metadata");
        std::unordered_map<std::string, std::string> metadata;
        if (!decodeNumericArray(stateMember(*object, "positions"), mesh.positions()) ||
            !decodeNumericArray(stateMember(*object, "normals"), mesh.normals()) ||
            !decodeNumericArray(stateMember(*object, "uvs"), mesh.uvs()) ||
            !decodeNumericArray(stateMember(*object, "indices"), mesh.indices()) || !encodedNames ||
            !decodeNumericArray(stateMember(*object, "triangleGroups"), assignments) ||
            !readIntValue(*object, "activeGroup", activeGroup) || activeGroup < std::numeric_limits<int>::min() ||
            activeGroup > std::numeric_limits<int>::max() || !decodeStringMap(encodedMetadata, metadata))
            return false;
        for (const eve::Value &encodedName : *encodedNames) {
            const auto *name = encodedName.getIf<std::string>();
            if (!name) return false;
            names.push_back(*name);
        }
        auto restoredGroups =
            mesh.restoreGroupData(std::move(names), std::move(assignments), static_cast<int>(activeGroup));
        if (!restoredGroups.ok()) return false;
        for (const auto &[key, value] : metadata) mesh.setMeta(key, value);
        output = std::move(mesh);
        return true;
    }
    if (type == ArtifactType::ImageData) {
        ImageData    image;
        std::int64_t width = 0, height = 0, channels = 0;
        if (!readIntValue(*object, "width", width) || !readIntValue(*object, "height", height) ||
            !readIntValue(*object, "channels", channels) || width <= 0 || height <= 0 || channels <= 0 ||
            width > std::numeric_limits<int>::max() || height > std::numeric_limits<int>::max() ||
            channels > std::numeric_limits<int>::max() || !readStringValue(*object, "format", image.format) ||
            !decodeNumericArray(stateMember(*object, "pixels"), image.pixels))
            return false;
        image.width    = static_cast<int>(width);
        image.height   = static_cast<int>(height);
        image.channels = static_cast<int>(channels);
        if (!image.isValid()) return false;
        output = std::move(image);
        return true;
    }
    if (type == ArtifactType::Collider) {
        Collider collider;
        if (!decodeNumericArray(stateMember(*object, "vertices"), collider.vertices) ||
            !decodeNumericArray(stateMember(*object, "indices"), collider.indices) ||
            !decodeBounds(stateMember(*object, "bounds"), collider.bounds) ||
            !readStringValue(*object, "shape", collider.shape) || !collider.isValid())
            return false;
        output = std::move(collider);
        return true;
    }
    return false;
}

bool decodePart(const eve::Value &encoded, ArtifactPart &output) {
    const auto             *object = encoded.getIf<eve::Value::Object>();
    std::string             role, idText, buildKeyText;
    eve::SchemaVersion      schemaVersion;
    Bounds                  bounds;
    std::vector<ArtifactId> dependencies;
    eve::Value::Object      metadata;
    const auto              type = object ? decodeArtifactType(*object) : std::nullopt;
    const auto              parsedId =
        object && readStringValue(*object, "artifactId", idText) ? ArtifactId::parse(idText) : std::nullopt;
    if (!object || !readStringValue(*object, "role", role) || !parsedId || parsedId->isNil() || !type ||
        *type == ArtifactType::Composite || !decodeSchemaVersion(*object, schemaVersion) ||
        !readStringValue(*object, "buildKey", buildKeyText) || !decodeBounds(stateMember(*object, "bounds"), bounds) ||
        !decodeDependencies(stateMember(*object, "dependencies"), dependencies) ||
        !decodeMetadata(stateMember(*object, "metadata"), metadata))
        return false;
    const auto key = BuildKey::fromCanonical(buildKeyText);
    if (!key) return false;
    ArtifactLeafPayload payload;
    if (!decodePayload(*type, stateMember(*object, "payload"), payload)) return false;
    auto result = makeArtifactPart(std::move(role), *parsedId, *type, schemaVersion, *key, bounds,
                                   std::move(dependencies), std::move(metadata), std::move(payload));
    if (!result.ok()) return false;
    output = std::move(result).takeValue();
    return true;
}

bool decodeArtifact(const eve::Value &encoded, GeneratedArtifact &output) {
    const auto             *object = encoded.getIf<eve::Value::Object>();
    std::string             idText, buildKeyText;
    eve::SchemaVersion      schemaVersion;
    Bounds                  bounds;
    std::vector<ArtifactId> dependencies;
    eve::Value::Object      metadata;
    const auto              type = object ? decodeArtifactType(*object) : std::nullopt;
    const auto              parsedId =
        object && readStringValue(*object, "artifactId", idText) ? ArtifactId::parse(idText) : std::nullopt;
    if (!object || !parsedId || parsedId->isNil() || !type || !decodeSchemaVersion(*object, schemaVersion) ||
        !readStringValue(*object, "buildKey", buildKeyText) || !decodeBounds(stateMember(*object, "bounds"), bounds) ||
        !decodeDependencies(stateMember(*object, "dependencies"), dependencies) ||
        !decodeMetadata(stateMember(*object, "metadata"), metadata))
        return false;
    const auto key = BuildKey::fromCanonical(buildKeyText);
    if (!key) return false;
    GeneratedArtifact::Payload payload;
    if (*type == ArtifactType::Composite) {
        const auto *payloadObjectValue = stateMember(*object, "payload");
        const auto *payloadObject      = payloadObjectValue ? payloadObjectValue->getIf<eve::Value::Object>() : nullptr;
        std::string kind;
        const auto *encodedChildren = payloadObject ? stateMember(*payloadObject, "children") : nullptr;
        const auto *children        = encodedChildren ? encodedChildren->getIf<eve::Value::Array>() : nullptr;
        CompositeArtifact composite;
        if (!payloadObject || !readStringValue(*payloadObject, "kind", kind) || kind != "composite" || !children)
            return false;
        composite.children.reserve(children->size());
        for (const eve::Value &encodedPart : *children) {
            ArtifactPart part;
            if (!decodePart(encodedPart, part)) return false;
            composite.children.push_back(std::move(part));
        }
        payload = std::move(composite);
    } else {
        ArtifactLeafPayload leaf;
        if (!decodePayload(*type, stateMember(*object, "payload"), leaf)) return false;
        payload = std::visit(
            [](auto &&value) -> GeneratedArtifact::Payload {
                return GeneratedArtifact::Payload(std::forward<decltype(value)>(value));
            },
            std::move(leaf));
    }
    auto result = makeArtifact(*parsedId, *type, schemaVersion, *key, bounds, std::move(dependencies),
                               std::move(metadata), std::move(payload));
    if (!result.ok()) return false;
    output = std::move(result).takeValue();
    return true;
}

Bounds boundsForMesh(const MeshBuild &mesh) noexcept {
    Bounds bounds;
    for (int i = 0; i < mesh.getVertexCount(); ++i) {
        bounds.include(mesh.getPositionX(i), mesh.getPositionY(i), mesh.getPositionZ(i));
    }
    return bounds;
}

Bounds boundsForGrid(const Grid2D &grid) noexcept {
    Bounds bounds;
    if (grid.getWidth() <= 0 || grid.getHeight() <= 0) return bounds;
    bounds.include(0.f, 0.f, 0.f);
    bounds.include(float(grid.getWidth()), 0.f, float(grid.getHeight()));
    return bounds;
}

Bounds boundsForPoints(const PointSet &points) noexcept {
    Bounds bounds;
    for (const ProcgenPoint &point : points.points()) bounds.include(point.x, point.y, point.z);
    return bounds;
}

Collider colliderForMesh(const MeshBuild &mesh) {
    Collider collider;
    collider.vertices = mesh.positions();
    collider.indices  = mesh.indices();
    collider.bounds   = boundsForMesh(mesh);
    return collider;
}

Grid2D makeHexTopology(const Params &params) {
    const int width  = std::clamp(params.getInt("width", 32), 1, 256);
    const int height = std::clamp(params.getInt("height", 24), 1, 256);
    Grid2D    topology;
    topology.resize(width, height);
    const std::uint32_t seed = params.getSeed();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // This is a compact cell-topology projection. Elevation and dense
            // geometry remain in the mesh recipe; no per-cell ECS objects are
            // created merely to publish topology.
            const std::uint32_t value = seed ^ (std::uint32_t(x) * 0x9e3779b9u) ^ (std::uint32_t(y) * 0x85ebca6bu);
            topology.setCell(x, y, int((value ^ (value >> 16u)) % 10u));
        }
    }
    topology.setMeta("role", "topology");
    topology.setMeta("algorithm", "mesh.hexterrain");
    topology.setMeta("coordinateSpace", "hex_axial");
    return topology;
}

PointSet makeHexAnchors(const Params &params) {
    const float radius = params.getFloat("radius", 1.f);
    const int   width  = std::max(2, params.getInt("width", 32));
    const int   height = std::max(2, params.getInt("height", 24));
    const float xStep  = radius * 1.5f;
    const float zStep  = radius * 1.7320508076f;
    PointSet    anchors;
    anchors.add(0.f, 0.f, 0.f);
    anchors.add(float(width - 1) * xStep, 0.f, 0.f);
    anchors.add(0.f, 0.f, float(height - 1) * zStep);
    anchors.add(float(width - 1) * xStep, 0.f, (float(height - 1) + float((width - 1) & 1) * .5f) * zStep);
    anchors.setStringAttribute(0, "role", "spawn");
    anchors.setStringAttribute(1, "role", "edge");
    anchors.setStringAttribute(2, "role", "edge");
    anchors.setStringAttribute(3, "role", "edge");
    return anchors;
}

Grid2D makeCastleTopology(const Params &params) {
    const int rings = std::clamp(params.getInt("rings", 2), 1, 4);
    Grid2D    topology;
    topology.resize(rings, 1);
    for (int ring = 0; ring < rings; ++ring) topology.setCell(ring, 0, ring + 1);
    topology.setMeta("role", "topology");
    topology.setMeta("algorithm", "mesh.castle");
    topology.setMeta("coordinateSpace", "castle_ring");
    return topology;
}

PointSet makeCastleAnchors(const Params &params) {
    const float width     = std::max(16.f, params.getFloat("width", 42.f));
    const float depth     = std::max(16.f, params.getFloat("depth", 36.f));
    const float gateWidth = std::clamp(params.getFloat("gateWidth", 5.f), 2.f, width * .3f);
    const float keepWidth = std::clamp(params.getFloat("keepWidth", width * .25f), 6.f, width * .55f);
    const float keepDepth = std::clamp(params.getFloat("keepDepth", depth * .24f), 6.f, depth * .55f);
    PointSet    anchors;
    anchors.add(0.f, 0.f, -depth * .5f - 1.f);
    anchors.add(-width * .5f + gateWidth, 0.f, -depth * .5f);
    anchors.add(width * .5f - gateWidth, 0.f, -depth * .5f);
    anchors.add(-keepWidth * .5f, 0.f, -keepDepth * .5f);
    anchors.add(keepWidth * .5f, 0.f, keepDepth * .5f);
    anchors.setStringAttribute(0, "role", "gate");
    anchors.setStringAttribute(1, "role", "outer_gate_left");
    anchors.setStringAttribute(2, "role", "outer_gate_right");
    anchors.setStringAttribute(3, "role", "keep_min");
    anchors.setStringAttribute(4, "role", "keep_max");
    return anchors;
}

eve::Result<ArtifactPart> partFromMesh(std::string role, ArtifactId parent, const BuildKey &parentKey, MeshBuild mesh,
                                       eve::Value::Object metadata) {
    auto childKey = BuildKey::fromCanonical(parentKey.format() + "/" + role);
    if (!childKey)
        return rejectedPart(eve::DiagnosticCode::InvalidArgument, "cannot derive child build key for " + role);
    const ArtifactId childId = parent.child(role);
    const Bounds     bounds  = boundsForMesh(mesh);
    return makeArtifactPart(std::move(role), childId, ArtifactType::MeshData, eve::SchemaVersion(1),
                            std::move(*childKey), bounds, {}, std::move(metadata), std::move(mesh));
}

eve::Result<ArtifactPart> partFromCollider(std::string role, ArtifactId parent, const BuildKey &parentKey,
                                           Collider collider, eve::Value::Object metadata) {
    auto childKey = BuildKey::fromCanonical(parentKey.format() + "/" + role);
    if (!childKey)
        return rejectedPart(eve::DiagnosticCode::InvalidArgument, "cannot derive child build key for " + role);
    const ArtifactId childId = parent.child(role);
    const Bounds     bounds  = collider.bounds;
    return makeArtifactPart(std::move(role), childId, ArtifactType::Collider, eve::SchemaVersion(1),
                            std::move(*childKey), bounds, {parent.child("mesh")}, std::move(metadata),
                            std::move(collider));
}

eve::Result<ArtifactPart> partFromGrid(std::string role, ArtifactId parent, const BuildKey &parentKey, Grid2D grid,
                                       eve::Value::Object metadata) {
    auto childKey = BuildKey::fromCanonical(parentKey.format() + "/" + role);
    if (!childKey)
        return rejectedPart(eve::DiagnosticCode::InvalidArgument, "cannot derive child build key for " + role);
    const ArtifactId childId = parent.child(role);
    const Bounds     bounds  = boundsForGrid(grid);
    return makeArtifactPart(std::move(role), childId, ArtifactType::Grid, eve::SchemaVersion(1), std::move(*childKey),
                            bounds, {}, std::move(metadata), std::move(grid));
}

eve::Result<ArtifactPart> partFromPoints(std::string role, ArtifactId parent, const BuildKey &parentKey,
                                         PointSet points, eve::Value::Object metadata) {
    auto childKey = BuildKey::fromCanonical(parentKey.format() + "/" + role);
    if (!childKey)
        return rejectedPart(eve::DiagnosticCode::InvalidArgument, "cannot derive child build key for " + role);
    const ArtifactId childId = parent.child(role);
    const Bounds     bounds  = boundsForPoints(points);
    return makeArtifactPart(std::move(role), childId, ArtifactType::PointSet, eve::SchemaVersion(1),
                            std::move(*childKey), bounds, {}, std::move(metadata), std::move(points));
}

template <class T>
bool appendPart(eve::Result<ArtifactPart> &&result, std::vector<ArtifactPart> &parts, eve::Status &failure) {
    if (!result.ok()) {
        failure = result.status();
        return false;
    }
    parts.emplace_back(std::move(result).takeValue());
    return true;
}

eve::Result<GeneratedArtifact> generateComposite(const Params &params, ArtifactId id, std::string_view recipe,
                                                 bool (*generateMesh)(const Params &, MeshBuild &, std::string &),
                                                 Grid2D topology, PointSet anchors) {
    if (id.isNil())
        return rejectedArtifact(eve::DiagnosticCode::InvalidArgument, "top-level artifact identity must not be nil");
    const auto key = BuildKey::forRecipe(recipe, params);
    if (!key)
        return rejectedArtifact(eve::DiagnosticCode::InvalidArgument, "recipe or parameters cannot form a build key");

    MeshBuild   mesh;
    std::string error;
    if (!generateMesh(params, mesh, error)) {
        return rejectedArtifact(eve::DiagnosticCode::Failed,
                                error.empty() ? "procedural mesh generation failed" : error);
    }
    const Collider            collider = colliderForMesh(mesh);
    std::vector<ArtifactPart> parts;
    parts.reserve(4);
    eve::Status        failure;
    eve::Value::Object meshMeta;
    meshMeta.emplace("role", eve::Value("mesh"));
    meshMeta.emplace("densePayload", eve::Value(true));
    auto meshPart = partFromMesh("mesh", id, *key, std::move(mesh), std::move(meshMeta));
    if (!appendPart<ArtifactPart>(std::move(meshPart), parts, failure))
        return eve::Result<GeneratedArtifact>::failure(std::move(failure));

    eve::Value::Object colliderMeta;
    colliderMeta.emplace("role", eve::Value("collider"));
    colliderMeta.emplace("source", eve::Value("mesh"));
    auto colliderPart = partFromCollider("collider", id, *key, collider, std::move(colliderMeta));
    if (!appendPart<ArtifactPart>(std::move(colliderPart), parts, failure))
        return eve::Result<GeneratedArtifact>::failure(std::move(failure));

    eve::Value::Object topologyMeta;
    topologyMeta.emplace("role", eve::Value("topology"));
    auto topologyPart = partFromGrid("topology", id, *key, std::move(topology), std::move(topologyMeta));
    if (!appendPart<ArtifactPart>(std::move(topologyPart), parts, failure))
        return eve::Result<GeneratedArtifact>::failure(std::move(failure));

    eve::Value::Object anchorsMeta;
    anchorsMeta.emplace("role", eve::Value("anchors"));
    anchorsMeta.emplace("coordinateSpace", eve::Value("world"));
    auto anchorsPart = partFromPoints("anchors", id, *key, std::move(anchors), std::move(anchorsMeta));
    if (!appendPart<ArtifactPart>(std::move(anchorsPart), parts, failure))
        return eve::Result<GeneratedArtifact>::failure(std::move(failure));

    CompositeArtifact composite;
    composite.children = std::move(parts);
    eve::Value::Object metadata;
    metadata.emplace("algorithm", eve::Value(std::string(recipe)));
    metadata.emplace("childCount", eve::Value(std::int64_t(composite.children.size())));
    metadata.emplace("determinism", eve::Value("seeded_cpu"));
    metadata.emplace("payloadPolicy", eve::Value("dense data stays typed"));
    const Bounds bounds = composite.children.front().bounds;
    return makeArtifact(id, ArtifactType::Composite, eve::SchemaVersion(1), *key, bounds, {}, std::move(metadata),
                        GeneratedArtifact::Payload(std::move(composite)));
}

}  // namespace

class ArtifactStoreStage final : public eve::artifact::PreparedPublication {
public:
    ArtifactStoreStage(ArtifactStore &owner, std::vector<GeneratedArtifact> artifacts)
        : owner_(&owner), artifacts_(std::move(artifacts)) {}

    ~ArtifactStoreStage() override { rollback(); }

    void commit() noexcept override {
        if (!owner_) return;
        owner_->commitStaged(std::move(artifacts_));
        owner_ = nullptr;
    }

    void rollback() noexcept override {
        if (!owner_) return;
        owner_->rollbackStaged();
        owner_ = nullptr;
        artifacts_.clear();
    }

private:
    ArtifactStore                 *owner_ = nullptr;
    std::vector<GeneratedArtifact> artifacts_;
};

std::optional<BuildKey> BuildKey::fromCanonical(std::string_view canonical) {
    if (canonical.empty() || hasControl(canonical)) return std::nullopt;
    return BuildKey(std::string(canonical));
}

std::optional<BuildKey> BuildKey::forRecipe(std::string_view recipeId, const Params &params) {
    if (recipeId.empty() || hasControl(recipeId)) return std::nullopt;
    std::ostringstream value;
    value << "procgen/v1/" << recipeId << '/' << std::hex << std::setw(16) << std::setfill('0')
          << fnv1a(std::string(recipeId) + '\0' + params.canonicalString());
    return BuildKey(value.str());
}

void Bounds::include(float x, float y, float z) noexcept {
    if (!valid) {
        minX = maxX = x;
        minY = maxY = y;
        minZ = maxZ = z;
        valid       = true;
        return;
    }
    minX = std::min(minX, x);
    minY = std::min(minY, y);
    minZ = std::min(minZ, z);
    maxX = std::max(maxX, x);
    maxY = std::max(maxY, y);
    maxZ = std::max(maxZ, z);
}

bool ImageData::isValid() const noexcept {
    if (width <= 0 || height <= 0 || channels <= 0) return false;
    const auto expected =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * static_cast<std::uint64_t>(channels);
    return expected == pixels.size() && !format.empty();
}

bool Collider::isValid() const noexcept {
    return finiteBounds(bounds) && !shape.empty() && vertices.size() % 3u == 0u && indices.size() % 3u == 0u &&
           !vertices.empty() && !indices.empty() &&
           std::all_of(vertices.begin(), vertices.end(), [](float value) { return std::isfinite(value); }) &&
           std::all_of(indices.begin(), indices.end(),
                       [&](std::uint32_t index) { return index < vertices.size() / 3u; });
}

const ArtifactPart *CompositeArtifact::find(std::string_view role) const noexcept {
    const auto found =
        std::find_if(children.begin(), children.end(), [&](const ArtifactPart &part) { return part.role == role; });
    return found == children.end() ? nullptr : &*found;
}

ArtifactType artifactType(const GeneratedArtifact::Payload &payload) noexcept {
    return std::visit(
        [](const auto &value) -> ArtifactType {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Grid2D>) return ArtifactType::Grid;
            if constexpr (std::is_same_v<T, PointSet>) return ArtifactType::PointSet;
            if constexpr (std::is_same_v<T, MeshData>) return ArtifactType::MeshData;
            if constexpr (std::is_same_v<T, ImageData>) return ArtifactType::ImageData;
            if constexpr (std::is_same_v<T, Collider>) return ArtifactType::Collider;
            return ArtifactType::Composite;
        },
        payload);
}

const char *artifactTypeName(ArtifactType type) noexcept {
    switch (type) {
        case ArtifactType::Grid: return "grid";
        case ArtifactType::PointSet: return "point_set";
        case ArtifactType::MeshData: return "mesh_data";
        case ArtifactType::ImageData: return "image_data";
        case ArtifactType::Collider: return "collider";
        case ArtifactType::Composite: return "composite";
    }
    return "unknown";
}

eve::Result<GeneratedArtifact> makeArtifact(ArtifactId id, ArtifactType type, eve::SchemaVersion schemaVersion,
                                            BuildKey buildKey, Bounds bounds, std::vector<ArtifactId> dependencies,
                                            eve::Value::Object metadata, GeneratedArtifact::Payload payload) {
    GeneratedArtifact result;
    result.id            = id;
    result.type          = type;
    result.schemaVersion = schemaVersion;
    result.buildKey      = std::move(buildKey);
    result.bounds        = bounds;
    result.dependencies  = std::move(dependencies);
    result.metadata      = std::move(metadata);
    result.payload       = std::move(payload);
    if (!validArtifact(result))
        return rejectedArtifact(eve::DiagnosticCode::InvalidArgument, "artifact structure or typed payload is invalid");
    return eve::Result<GeneratedArtifact>::success(std::move(result));
}

eve::Result<ArtifactPart> makeArtifactPart(std::string role, ArtifactId id, ArtifactType type,
                                           eve::SchemaVersion schemaVersion, BuildKey buildKey, Bounds bounds,
                                           std::vector<ArtifactId> dependencies, eve::Value::Object metadata,
                                           ArtifactLeafPayload payload) {
    ArtifactPart result;
    result.role          = std::move(role);
    result.id            = id;
    result.type          = type;
    result.schemaVersion = schemaVersion;
    result.buildKey      = std::move(buildKey);
    result.bounds        = bounds;
    result.dependencies  = std::move(dependencies);
    result.metadata      = std::move(metadata);
    result.payload       = std::move(payload);
    if (!validPart(result))
        return rejectedPart(eve::DiagnosticCode::InvalidArgument,
                            "artifact part '" + result.role + "' structure or typed payload is invalid");
    return eve::Result<ArtifactPart>::success(std::move(result));
}

eve::Result<ArtifactId> ArtifactStore::publish(GeneratedArtifact artifact) {
    auto published = publishBatch({std::move(artifact)});
    if (!published.ok()) return eve::Result<ArtifactId>::failure(published.status());
    auto ids = std::move(published).takeValue();
    return eve::Result<ArtifactId>::success(ids.front());
}

eve::Result<std::vector<ArtifactId>> ArtifactStore::publishBatch(std::vector<GeneratedArtifact> artifacts) {
    if (stageActive_)
        return eve::Result<std::vector<ArtifactId>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "artifact store already has a pending stage"));
    if (artifacts.empty())
        return eve::Result<std::vector<ArtifactId>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "artifact publish batch must not be empty", "artifacts"));

    std::unordered_map<ArtifactId, ArtifactType> available;
    available.reserve(artifacts_.size() + partOwners_.size() + artifacts.size() * 5u);
    for (const auto &[id, artifact] : artifacts_) available.emplace(id, artifact.type);
    for (const auto &[partId, ownerId] : partOwners_) {
        const GeneratedArtifact *owner = find(ownerId);
        if (!owner || owner->type != ArtifactType::Composite) continue;
        const auto &children = std::get<CompositeArtifact>(owner->payload).children;
        const auto  found    = std::find_if(children.begin(), children.end(),
                                            [partId](const ArtifactPart &part) { return part.id == partId; });
        if (found != children.end()) available.emplace(partId, found->type);
    }

    std::vector<ArtifactId> ids;
    ids.reserve(artifacts.size());
    for (const GeneratedArtifact &artifact : artifacts) {
        if (!validArtifact(artifact))
            return eve::Result<std::vector<ArtifactId>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "artifact structure or typed payload is invalid", "artifact"));
        if (!available.emplace(artifact.id, artifact.type).second)
            return eve::Result<std::vector<ArtifactId>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "artifact or part identity is already published", "artifact.id"));
        ids.push_back(artifact.id);
        if (artifact.type == ArtifactType::Composite) {
            for (const ArtifactPart &part : std::get<CompositeArtifact>(artifact.payload).children) {
                if (!available.emplace(part.id, part.type).second)
                    return eve::Result<std::vector<ArtifactId>>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::Conflict, "composite part identity conflicts", "artifact.children.id"));
            }
        }
    }

    const auto checkDependencies = [&](ArtifactType                   type,
                                       const std::vector<ArtifactId> &dependencies) -> std::optional<eve::Diagnostic> {
        bool meshDependency = false;
        for (ArtifactId dependency : dependencies) {
            const auto found = available.find(dependency);
            if (found == available.end())
                return eve::Diagnostic::error(eve::DiagnosticCode::NotFound,
                                              "artifact dependency is not published or in batch",
                                              "artifact.dependencies");
            meshDependency = meshDependency || found->second == ArtifactType::MeshData;
        }
        if (type == ArtifactType::Collider && !meshDependency)
            return eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                          "collider must depend on a mesh artifact", "artifact.dependencies");
        return std::nullopt;
    };
    for (const GeneratedArtifact &artifact : artifacts) {
        if (auto error = checkDependencies(artifact.type, artifact.dependencies))
            return eve::Result<std::vector<ArtifactId>>::failure(std::move(*error));
        if (artifact.type == ArtifactType::Composite)
            for (const ArtifactPart &part : std::get<CompositeArtifact>(artifact.payload).children)
                if (auto error = checkDependencies(part.type, part.dependencies))
                    return eve::Result<std::vector<ArtifactId>>::failure(std::move(*error));
    }

    try {
        artifacts_.reserve(artifacts_.size() + artifacts.size());
        std::size_t newParts = 0;
        for (const GeneratedArtifact &artifact : artifacts)
            if (artifact.type == ArtifactType::Composite)
                newParts += std::get<CompositeArtifact>(artifact.payload).children.size();
        partOwners_.reserve(partOwners_.size() + newParts);
        for (GeneratedArtifact &artifact : artifacts) {
            const ArtifactId owner = artifact.id;
            if (artifact.type == ArtifactType::Composite)
                for (const ArtifactPart &part : std::get<CompositeArtifact>(artifact.payload).children)
                    partOwners_.emplace(part.id, owner);
            artifacts_.emplace(owner, std::move(artifact));
        }
    } catch (...) {
        for (ArtifactId id : ids) artifacts_.erase(id);
        for (auto owner = partOwners_.begin(); owner != partOwners_.end();) {
            if (std::find(ids.begin(), ids.end(), owner->second) != ids.end())
                owner = partOwners_.erase(owner);
            else
                ++owner;
        }
        return eve::Result<std::vector<ArtifactId>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "artifact store allocation failed; batch rolled back", "artifacts"));
    }
    return eve::Result<std::vector<ArtifactId>>::success(std::move(ids));
}

eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>> ArtifactStore::stagePublish(
    GeneratedArtifact artifact) {
    if (stageActive_)
        return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "artifact store already has a pending stage"));
    std::vector<GeneratedArtifact> artifacts;
    try {
        artifacts.emplace_back(std::move(artifact));
        auto valid = validateBatchAgainstStore(*this, artifacts);
        if (!valid.ok())
            return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::failure(valid.status());
        std::size_t newParts = 0;
        for (const auto &entry : artifacts)
            if (entry.type == ArtifactType::Composite)
                newParts += std::get<CompositeArtifact>(entry.payload).children.size();
        // Reserve while the store is still hidden. The no-throw commit only
        // transfers already-owned values into these reserved containers.
        artifacts_.reserve(artifacts_.size() + artifacts.size());
        partOwners_.reserve(partOwners_.size() + newParts);
        stageActive_ = true;
        return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::success(
            std::make_unique<ArtifactStoreStage>(*this, std::move(artifacts)));
    } catch (...) {
        stageActive_ = false;
        return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "artifact store stage allocation failed"));
    }
}

void ArtifactStore::commitStaged(std::vector<GeneratedArtifact> artifacts) noexcept {
    for (GeneratedArtifact &artifact : artifacts) {
        const ArtifactId owner = artifact.id;
        if (artifact.type == ArtifactType::Composite)
            for (const ArtifactPart &part : std::get<CompositeArtifact>(artifact.payload).children)
                partOwners_.emplace(part.id, owner);
        artifacts_.emplace(owner, std::move(artifact));
    }
    stageActive_ = false;
}

void ArtifactStore::rollbackStaged() noexcept { stageActive_ = false; }

const GeneratedArtifact *ArtifactStore::find(ArtifactId id) const noexcept {
    const auto found = artifacts_.find(id);
    return found == artifacts_.end() ? nullptr : &found->second;
}

const ArtifactPart *ArtifactStore::findPart(ArtifactId id) const noexcept {
    const auto owner = partOwners_.find(id);
    if (owner == partOwners_.end()) return nullptr;
    const GeneratedArtifact *artifact = find(owner->second);
    if (!artifact || artifact->type != ArtifactType::Composite) return nullptr;
    const auto &children = std::get<CompositeArtifact>(artifact->payload).children;
    const auto  found =
        std::find_if(children.begin(), children.end(), [id](const ArtifactPart &part) { return part.id == id; });
    return found == children.end() ? nullptr : &*found;
}

std::optional<ArtifactType> ArtifactStore::typeOf(ArtifactId id) const noexcept {
    const auto artifact = artifacts_.find(id);
    if (artifact != artifacts_.end()) return artifact->second.type;
    const auto part = partOwners_.find(id);
    if (part != partOwners_.end()) {
        const auto owner = artifacts_.find(part->second);
        if (owner != artifacts_.end() && owner->second.type == ArtifactType::Composite) {
            const auto &children = std::get<CompositeArtifact>(owner->second.payload).children;
            const auto  found    = std::find_if(children.begin(), children.end(),
                                                [id](const ArtifactPart &value) { return value.id == id; });
            if (found != children.end()) return found->type;
        }
    }
    return std::nullopt;
}

std::vector<ArtifactId> ArtifactStore::findByBuildKey(const BuildKey &key) const {
    std::vector<ArtifactId> result;
    for (const auto &entry : artifacts_) {
        if (entry.second.buildKey == key) result.push_back(entry.first);
    }
    std::sort(result.begin(), result.end(),
              [](const ArtifactId &lhs, const ArtifactId &rhs) { return lhs.format() < rhs.format(); });
    return result;
}

eve::Result<void> ArtifactStore::remove(ArtifactId id) {
    const auto found = artifacts_.find(id);
    if (found == artifacts_.end()) {
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "artifact identity is not published", "artifact.id"));
    }
    if (found->second.type == ArtifactType::Composite)
        for (const ArtifactPart &part : std::get<CompositeArtifact>(found->second.payload).children)
            partOwners_.erase(part.id);
    artifacts_.erase(found);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void ArtifactStore::clear() noexcept {
    partOwners_.clear();
    artifacts_.clear();
    stageActive_ = false;
}

std::size_t ArtifactStore::partCount() const noexcept { return partOwners_.size(); }

eve::Result<eve::Value> ArtifactStore::snapshotState() const {
    try {
        std::vector<ArtifactId> ids;
        ids.reserve(artifacts_.size());
        for (const auto &entry : artifacts_) ids.push_back(entry.first);
        std::sort(ids.begin(), ids.end(), [](ArtifactId lhs, ArtifactId rhs) { return lhs.format() < rhs.format(); });

        eve::Value::Array records;
        records.reserve(ids.size());
        for (ArtifactId id : ids) records.emplace_back(encodeArtifact(artifacts_.at(id)));
        eve::Value::Object state;
        state.emplace("provider", eve::Value("procgen.cpu-artifact-store"));
        state.emplace("version", eve::Value(std::int64_t(2)));
        state.emplace("unknownFields", eve::Value("ignore"));
        state.emplace("artifacts", eve::Value(std::move(records)));
        return eve::Result<eve::Value>::success(eve::Value(std::move(state)));
    } catch (...) {
        return eve::Result<eve::Value>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "artifact store snapshot allocation failed"));
    }
}

eve::Result<void> ArtifactStore::restoreState(const eve::Value &state) {
    if (stageActive_ || !artifacts_.empty())
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "artifact store restore requires an empty store"));
    const auto *object           = state.getIf<eve::Value::Object>();
    const auto *provider         = object ? stateMember(*object, "provider") : nullptr;
    const auto *providerName     = provider ? provider->getIf<std::string>() : nullptr;
    const auto *encodedVersion   = object ? stateMember(*object, "version") : nullptr;
    const auto *version          = encodedVersion ? encodedVersion->getIf<std::int64_t>() : nullptr;
    const auto *encodedArtifacts = object ? stateMember(*object, "artifacts") : nullptr;
    const auto *encodedList      = encodedArtifacts ? encodedArtifacts->getIf<eve::Value::Array>() : nullptr;
    if (!object || !providerName || *providerName != "procgen.cpu-artifact-store" || !version || *version != 2 ||
        !encodedList)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::UnknownVersion,
                                                                 "artifact store requires dense snapshot version 2"));

    std::vector<GeneratedArtifact> candidate;
    try {
        candidate.reserve(encodedList->size());
        for (const eve::Value &encoded : *encodedList) {
            GeneratedArtifact artifact;
            if (!decodeArtifact(encoded, artifact))
                return eve::Result<void>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "invalid dense artifact store record"));
            candidate.push_back(std::move(artifact));
        }
    } catch (...) {
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "artifact store restore allocation failed"));
    }
    auto valid = validateBatchAgainstStore(*this, candidate);
    if (!valid.ok()) return valid;
    std::unordered_map<ArtifactId, GeneratedArtifact> restoredArtifacts;
    std::unordered_map<ArtifactId, ArtifactId>        restoredParts;
    try {
        restoredArtifacts.reserve(candidate.size());
        std::size_t partCount = 0;
        for (const GeneratedArtifact &artifact : candidate)
            if (artifact.type == ArtifactType::Composite)
                partCount += std::get<CompositeArtifact>(artifact.payload).children.size();
        restoredParts.reserve(partCount);
        for (GeneratedArtifact &artifact : candidate) {
            const ArtifactId owner = artifact.id;
            if (artifact.type == ArtifactType::Composite)
                for (const ArtifactPart &part : std::get<CompositeArtifact>(artifact.payload).children)
                    restoredParts.emplace(part.id, owner);
            restoredArtifacts.emplace(owner, std::move(artifact));
        }
    } catch (...) {
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "artifact store restore indexing allocation failed"));
    }
    artifacts_.swap(restoredArtifacts);
    partOwners_.swap(restoredParts);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<GeneratedArtifact> generateHexTerrainArtifact(const Params &params, ArtifactId id) {
    return generateComposite(params, id, "mesh.hexterrain", generateHexTerrainMesh, makeHexTopology(params),
                             makeHexAnchors(params));
}

eve::Result<GeneratedArtifact> generateCastleArtifact(const Params &params, ArtifactId id) {
    return generateComposite(params, id, "mesh.castle", generateCastleMesh, makeCastleTopology(params),
                             makeCastleAnchors(params));
}

eve::Result<GeneratedArtifact> generateMeshArtifact(std::string_view recipeId, const Params &params, ArtifactId id) {
    if (recipeId == "mesh.hexterrain") return generateHexTerrainArtifact(params, id);
    if (recipeId == "mesh.castle") return generateCastleArtifact(params, id);
    if (id.isNil())
        return rejectedArtifact(eve::DiagnosticCode::InvalidArgument, "top-level artifact identity must not be nil");
    const auto key = BuildKey::forRecipe(recipeId, params);
    if (!key)
        return rejectedArtifact(eve::DiagnosticCode::InvalidArgument, "recipe or parameters cannot form a build key");
    MeshRecipeRegistry::instance().registerBuiltins();
    MeshBuild   mesh;
    std::string error;
    if (!MeshRecipeRegistry::instance().generate(std::string(recipeId), params, mesh, error))
        return rejectedArtifact(eve::DiagnosticCode::Failed,
                                error.empty() ? "procedural mesh generation failed" : error);
    eve::Value::Object metadata;
    metadata.emplace("algorithm", eve::Value(std::string(recipeId)));
    metadata.emplace("determinism", eve::Value("seeded_cpu"));
    const Bounds bounds = boundsForMesh(mesh);
    return makeArtifact(id, ArtifactType::MeshData, eve::SchemaVersion(1), *key, bounds, {}, std::move(metadata),
                        std::move(mesh));
}

}  // namespace eve::procgen
