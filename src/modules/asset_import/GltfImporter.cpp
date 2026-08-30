#include "asset_import/AssetImporter.h"

#include "asset_import/ImportCommon.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace eve::asset_import {
namespace {

const Value* member(const Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

Result<std::uint64_t> unsignedValue(const Value* value, std::string path, bool required = true,
                                    std::uint64_t fallback = 0) {
    if (!value && !required) return Result<std::uint64_t>::success(fallback);
    if (!value || !value->isInt64() || value->asInt() < 0)
        return detail::failure<std::uint64_t>(DiagnosticCode::ParseError,
                                              "glTF field must be a non-negative integer", std::move(path));
    return Result<std::uint64_t>::success(static_cast<std::uint64_t>(value->asInt()));
}

std::uint32_t little32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
           (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}

void put32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift != 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

void putFloat(std::vector<std::uint8_t>& bytes, float value) { put32(bytes, std::bit_cast<std::uint32_t>(value)); }

struct Document {
    Value root;
    std::vector<std::uint8_t> binaryChunk;
};

Result<Document> parseDocument(const GltfImportRequest& request) {
    if (request.documentBytes.empty() || request.documentBytes.size() > request.limits.maximumSourceBytes)
        return detail::failure<Document>(DiagnosticCode::InvalidArgument, "glTF document size is outside limits");
    std::string json;
    std::vector<std::uint8_t> binary;
    const auto bytes = std::span<const std::uint8_t>(request.documentBytes);
    if (bytes.size() >= 12 && little32(bytes, 0) == 0x46546c67) {
        if (little32(bytes, 4) != 2 || little32(bytes, 8) != bytes.size() || bytes.size() < 20)
            return detail::failure<Document>(DiagnosticCode::ParseError, "GLB header is invalid");
        std::size_t cursor = 12;
        const std::uint32_t jsonSize = little32(bytes, cursor);
        const std::uint32_t jsonType = little32(bytes, cursor + 4);
        cursor += 8;
        if (jsonType != 0x4e4f534a || jsonSize > bytes.size() - cursor)
            return detail::failure<Document>(DiagnosticCode::ParseError, "GLB JSON chunk is invalid");
        json.assign(reinterpret_cast<const char*>(bytes.data() + cursor), jsonSize);
        while (!json.empty() && (json.back() == ' ' || json.back() == '\0')) json.pop_back();
        cursor += jsonSize;
        if (cursor < bytes.size()) {
            if (bytes.size() - cursor < 8)
                return detail::failure<Document>(DiagnosticCode::ParseError, "GLB trailing chunk is truncated");
            const std::uint32_t binarySize = little32(bytes, cursor);
            const std::uint32_t binaryType = little32(bytes, cursor + 4);
            cursor += 8;
            if (binaryType != 0x004e4942 || binarySize > bytes.size() - cursor || cursor + binarySize != bytes.size())
                return detail::failure<Document>(DiagnosticCode::ParseError, "GLB BIN chunk is invalid");
            binary.assign(bytes.begin() + cursor, bytes.end());
        }
    } else {
        json.assign(request.documentBytes.begin(), request.documentBytes.end());
    }
    auto parsed = Value::fromJson(json);
    if (!parsed) return Result<Document>::failure(parsed.status());
    return Result<Document>::success({std::move(parsed).takeValue(), std::move(binary)});
}

bool safeUri(std::string_view uri, const AssetImportLimits& limits) {
    if (uri.empty() || uri.size() > limits.maximumStringBytes || uri.front() == '/' ||
        uri.find('\\') != std::string_view::npos || uri.starts_with("data:") || uri.find(':') != std::string_view::npos)
        return false;
    std::size_t start = 0;
    for (std::size_t index = 0; index <= uri.size(); ++index) {
        if (index != uri.size() && uri[index] != '/') continue;
        const auto segment = uri.substr(start, index - start);
        if (segment.empty() || segment == "." || segment == "..") return false;
        start = index + 1;
    }
    return true;
}

Result<std::vector<std::span<const std::uint8_t>>> resolveBuffers(
    const Value::Object& root, const Document& document, const GltfImportRequest& request) {
    const Value* buffersValue = member(root, "buffers");
    const auto* buffers = buffersValue ? buffersValue->getIf<Value::Array>() : nullptr;
    if (!buffers || buffers->empty())
        return detail::failure<std::vector<std::span<const std::uint8_t>>>(DiagnosticCode::ParseError,
                                                                          "glTF buffers are required", "$.buffers");
    std::vector<std::span<const std::uint8_t>> result;
    result.reserve(buffers->size());
    std::uint64_t totalBytes = 0;
    for (std::size_t index = 0; index < buffers->size(); ++index) {
        const auto* object = (*buffers)[index].getIf<Value::Object>();
        if (!object)
            return detail::failure<std::vector<std::span<const std::uint8_t>>>(DiagnosticCode::ParseError,
                                                                              "glTF buffer must be an object");
        auto declared = unsignedValue(member(*object, "byteLength"),
                                      "$.buffers[" + std::to_string(index) + "].byteLength");
        if (!declared) return Result<std::vector<std::span<const std::uint8_t>>>::failure(declared.status());
        std::span<const std::uint8_t> data;
        const Value* uriValue = member(*object, "uri");
        if (uriValue) {
            if (!uriValue->isString() || !safeUri(uriValue->asString(), request.limits))
                return detail::failure<std::vector<std::span<const std::uint8_t>>>(DiagnosticCode::Unsupported,
                                                                                  "external glTF buffer URI is unsafe or unsupported");
            const auto found = request.externalResources.find(uriValue->asString());
            if (found == request.externalResources.end())
                return detail::failure<std::vector<std::span<const std::uint8_t>>>(DiagnosticCode::NotFound,
                                                                                  "external glTF buffer was not supplied",
                                                                                  uriValue->asString());
            data = found->second;
        } else {
            if (index != 0 || document.binaryChunk.empty())
                return detail::failure<std::vector<std::span<const std::uint8_t>>>(DiagnosticCode::ParseError,
                                                                                  "buffer without URI requires the first GLB BIN chunk");
            data = document.binaryChunk;
        }
        if (declared.value() > data.size())
            return detail::failure<std::vector<std::span<const std::uint8_t>>>(DiagnosticCode::ParseError,
                                                                              "glTF buffer is shorter than byteLength");
        if (declared.value() > request.limits.maximumSourceBytes ||
            totalBytes > request.limits.maximumSourceBytes - declared.value())
            return detail::failure<std::vector<std::span<const std::uint8_t>>>(DiagnosticCode::InvalidArgument,
                                                                              "glTF buffer budget is exceeded");
        totalBytes += declared.value();
        result.push_back(data.first(static_cast<std::size_t>(declared.value())));
    }
    return Result<std::vector<std::span<const std::uint8_t>>>::success(std::move(result));
}

struct Accessor {
    std::span<const std::uint8_t> buffer;
    std::size_t offset = 0;
    std::size_t stride = 0;
    std::uint32_t count = 0;
    std::uint32_t componentType = 0;
    std::uint32_t components = 0;
};

std::uint32_t componentSize(std::uint32_t type) {
    if (type == 5120 || type == 5121) return 1;
    if (type == 5122 || type == 5123) return 2;
    if (type == 5125 || type == 5126) return 4;
    return 0;
}

Result<Accessor> accessorAt(const Value::Object& root,
                            const std::vector<std::span<const std::uint8_t>>& buffers,
                            std::uint64_t accessorIndex, const AssetImportLimits& limits) {
    const auto* accessors = member(root, "accessors") ? member(root, "accessors")->getIf<Value::Array>() : nullptr;
    const auto* views = member(root, "bufferViews") ? member(root, "bufferViews")->getIf<Value::Array>() : nullptr;
    if (!accessors || !views || accessorIndex >= accessors->size())
        return detail::failure<Accessor>(DiagnosticCode::ParseError, "glTF accessor index is invalid");
    const auto* object = (*accessors)[accessorIndex].getIf<Value::Object>();
    if (!object || member(*object, "sparse"))
        return detail::failure<Accessor>(DiagnosticCode::Unsupported, "sparse or malformed glTF accessor is unsupported");
    auto viewIndex = unsignedValue(member(*object, "bufferView"), "accessor.bufferView");
    auto count = unsignedValue(member(*object, "count"), "accessor.count");
    auto component = unsignedValue(member(*object, "componentType"), "accessor.componentType");
    auto accessorOffset = unsignedValue(member(*object, "byteOffset"), "accessor.byteOffset", false, 0);
    if (!viewIndex || !count || !component || !accessorOffset)
        return detail::failure<Accessor>(DiagnosticCode::ParseError, "glTF accessor fields are invalid");
    if (viewIndex.value() >= views->size() || count.value() == 0 ||
        count.value() > std::max(limits.maximumVerticesPerPrimitive, limits.maximumIndicesPerPrimitive))
        return detail::failure<Accessor>(DiagnosticCode::InvalidArgument, "glTF accessor exceeds limits");
    const Value* typeValue = member(*object, "type");
    if (!typeValue || !typeValue->isString())
        return detail::failure<Accessor>(DiagnosticCode::ParseError, "glTF accessor type is missing");
    std::uint32_t components = 0;
    if (typeValue->asString() == "SCALAR") components = 1;
    else if (typeValue->asString() == "VEC2") components = 2;
    else if (typeValue->asString() == "VEC3") components = 3;
    else if (typeValue->asString() == "VEC4") components = 4;
    else return detail::failure<Accessor>(DiagnosticCode::Unsupported, "glTF accessor shape is unsupported");
    const auto* view = (*views)[viewIndex.value()].getIf<Value::Object>();
    if (!view) return detail::failure<Accessor>(DiagnosticCode::ParseError, "glTF bufferView is malformed");
    auto bufferIndex = unsignedValue(member(*view, "buffer"), "bufferView.buffer");
    auto viewOffset = unsignedValue(member(*view, "byteOffset"), "bufferView.byteOffset", false, 0);
    auto viewLength = unsignedValue(member(*view, "byteLength"), "bufferView.byteLength");
    auto byteStride = unsignedValue(member(*view, "byteStride"), "bufferView.byteStride", false, 0);
    if (!bufferIndex || !viewOffset || !viewLength || !byteStride || bufferIndex.value() >= buffers.size())
        return detail::failure<Accessor>(DiagnosticCode::ParseError, "glTF bufferView fields are invalid");
    const std::uint32_t elementSize = componentSize(static_cast<std::uint32_t>(component.value())) * components;
    const std::uint64_t stride = byteStride.value() == 0 ? elementSize : byteStride.value();
    if (elementSize == 0 || stride < elementSize || stride > 252 ||
        viewOffset.value() > buffers[bufferIndex.value()].size() ||
        viewLength.value() > buffers[bufferIndex.value()].size() - viewOffset.value() ||
        accessorOffset.value() > viewLength.value())
        return detail::failure<Accessor>(DiagnosticCode::ParseError, "glTF accessor layout is invalid");
    const std::uint64_t required = (count.value() - 1) * stride + elementSize;
    if (required > viewLength.value() - accessorOffset.value())
        return detail::failure<Accessor>(DiagnosticCode::ParseError, "glTF accessor range is truncated");
    return Result<Accessor>::success({buffers[bufferIndex.value()],
                                      static_cast<std::size_t>(viewOffset.value() + accessorOffset.value()),
                                      static_cast<std::size_t>(stride), static_cast<std::uint32_t>(count.value()),
                                      static_cast<std::uint32_t>(component.value()), components});
}

Result<float> readFloat(const Accessor& accessor, std::uint32_t element, std::uint32_t component) {
    if (accessor.componentType != 5126 || component >= accessor.components)
        return detail::failure<float>(DiagnosticCode::Unsupported, "mesh attribute must use FLOAT components");
    const std::size_t offset = accessor.offset + std::size_t(element) * accessor.stride + component * 4;
    const float value = std::bit_cast<float>(little32(accessor.buffer, offset));
    if (!std::isfinite(value))
        return detail::failure<float>(DiagnosticCode::ParseError, "mesh attribute contains non-finite values");
    return Result<float>::success(value);
}

Result<std::uint32_t> readIndex(const Accessor& accessor, std::uint32_t element) {
    if (accessor.components != 1)
        return detail::failure<std::uint32_t>(DiagnosticCode::ParseError, "index accessor must be SCALAR");
    const std::size_t offset = accessor.offset + std::size_t(element) * accessor.stride;
    if (accessor.componentType == 5121) return Result<std::uint32_t>::success(accessor.buffer[offset]);
    if (accessor.componentType == 5123)
        return Result<std::uint32_t>::success(std::uint32_t(accessor.buffer[offset]) |
                                              (std::uint32_t(accessor.buffer[offset + 1]) << 8));
    if (accessor.componentType == 5125) return Result<std::uint32_t>::success(little32(accessor.buffer, offset));
    return detail::failure<std::uint32_t>(DiagnosticCode::Unsupported,
                                          "indices must use UNSIGNED_BYTE, UNSIGNED_SHORT or UNSIGNED_INT");
}

struct PrimitiveOutput {
    std::vector<std::uint8_t> blob;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
    bool hasNormals = false;
    bool hasTexcoords = false;
    float minimum[3] = {};
    float maximum[3] = {};
};

Result<PrimitiveOutput> decodePrimitive(const Value::Object& root, const Value::Object& primitive,
                                        const std::vector<std::span<const std::uint8_t>>& buffers,
                                        const AssetImportLimits& limits) {
    auto mode = unsignedValue(member(primitive, "mode"), "primitive.mode", false, 4);
    if (!mode) return Result<PrimitiveOutput>::failure(mode.status());
    if (mode.value() != 4)
        return detail::failure<PrimitiveOutput>(DiagnosticCode::Unsupported,
                                                "only glTF TRIANGLES primitives are supported");
    const auto* attributesValue = member(primitive, "attributes");
    const auto* attributes = attributesValue ? attributesValue->getIf<Value::Object>() : nullptr;
    if (!attributes)
        return detail::failure<PrimitiveOutput>(DiagnosticCode::ParseError, "primitive attributes are required");
    auto positionIndex = unsignedValue(member(*attributes, "POSITION"), "primitive.attributes.POSITION");
    if (!positionIndex) return Result<PrimitiveOutput>::failure(positionIndex.status());
    auto positions = accessorAt(root, buffers, positionIndex.value(), limits);
    if (!positions) return Result<PrimitiveOutput>::failure(positions.status());
    if (positions.value().components != 3 || positions.value().componentType != 5126 ||
        positions.value().count > limits.maximumVerticesPerPrimitive)
        return detail::failure<PrimitiveOutput>(DiagnosticCode::Unsupported,
                                                "POSITION must be a bounded FLOAT VEC3 accessor");
    std::optional<Accessor> normals;
    if (const Value* normal = member(*attributes, "NORMAL")) {
        auto normalIndex = unsignedValue(normal, "primitive.attributes.NORMAL");
        if (!normalIndex) return Result<PrimitiveOutput>::failure(normalIndex.status());
        auto decoded = accessorAt(root, buffers, normalIndex.value(), limits);
        if (!decoded) return Result<PrimitiveOutput>::failure(decoded.status());
        if (decoded.value().components != 3 || decoded.value().componentType != 5126 ||
            decoded.value().count != positions.value().count)
            return detail::failure<PrimitiveOutput>(DiagnosticCode::Unsupported, "NORMAL must match POSITION");
        normals = std::move(decoded).takeValue();
    }
    std::optional<Accessor> texcoords;
    if (const Value* texcoord = member(*attributes, "TEXCOORD_0")) {
        auto texcoordIndex = unsignedValue(texcoord, "primitive.attributes.TEXCOORD_0");
        if (!texcoordIndex) return Result<PrimitiveOutput>::failure(texcoordIndex.status());
        auto decoded = accessorAt(root, buffers, texcoordIndex.value(), limits);
        if (!decoded) return Result<PrimitiveOutput>::failure(decoded.status());
        if (decoded.value().components != 2 || decoded.value().componentType != 5126 ||
            decoded.value().count != positions.value().count)
            return detail::failure<PrimitiveOutput>(DiagnosticCode::Unsupported, "TEXCOORD_0 must match POSITION");
        texcoords = std::move(decoded).takeValue();
    }
    std::vector<std::uint32_t> indices;
    if (const Value* indicesValue = member(primitive, "indices")) {
        auto index = unsignedValue(indicesValue, "primitive.indices");
        if (!index) return Result<PrimitiveOutput>::failure(index.status());
        auto accessor = accessorAt(root, buffers, index.value(), limits);
        if (!accessor) return Result<PrimitiveOutput>::failure(accessor.status());
        if (accessor.value().count > limits.maximumIndicesPerPrimitive || accessor.value().count % 3 != 0)
            return detail::failure<PrimitiveOutput>(DiagnosticCode::InvalidArgument, "triangle index count is invalid");
        indices.reserve(accessor.value().count);
        for (std::uint32_t item = 0; item < accessor.value().count; ++item) {
            auto value = readIndex(accessor.value(), item);
            if (!value) return Result<PrimitiveOutput>::failure(value.status());
            if (value.value() >= positions.value().count)
                return detail::failure<PrimitiveOutput>(DiagnosticCode::ParseError, "mesh index exceeds vertex count");
            indices.push_back(value.value());
        }
    } else {
        if (positions.value().count % 3 != 0)
            return detail::failure<PrimitiveOutput>(DiagnosticCode::ParseError,
                                                    "unindexed triangle vertex count must be divisible by three");
        indices.resize(positions.value().count);
        for (std::uint32_t index = 0; index < indices.size(); ++index) indices[index] = index;
    }
    const std::uint64_t decodedSize = 24 + std::uint64_t(positions.value().count) *
        (12 + (normals ? 12 : 0) + (texcoords ? 8 : 0)) + std::uint64_t(indices.size()) * 4;
    if (decodedSize > limits.maximumDecodedBytes || decodedSize > std::numeric_limits<std::size_t>::max())
        return detail::failure<PrimitiveOutput>(DiagnosticCode::InvalidArgument, "canonical mesh exceeds decoded budget");
    PrimitiveOutput output;
    output.vertexCount = positions.value().count;
    output.indexCount = static_cast<std::uint32_t>(indices.size());
    output.hasNormals = normals.has_value();
    output.hasTexcoords = texcoords.has_value();
    output.blob.insert(output.blob.end(), {'E', 'V', 'M', 'E', 'S', 'H', 0, 1});
    put32(output.blob, output.vertexCount); put32(output.blob, output.indexCount);
    put32(output.blob, (output.hasNormals ? 1u : 0u) | (output.hasTexcoords ? 2u : 0u)); put32(output.blob, 0);
    for (std::uint32_t vertex = 0; vertex < output.vertexCount; ++vertex) {
        for (std::uint32_t axis = 0; axis < 3; ++axis) {
            auto value = readFloat(positions.value(), vertex, axis);
            if (!value) return Result<PrimitiveOutput>::failure(value.status());
            if (vertex == 0) output.minimum[axis] = output.maximum[axis] = value.value();
            else { output.minimum[axis] = std::min(output.minimum[axis], value.value());
                   output.maximum[axis] = std::max(output.maximum[axis], value.value()); }
            putFloat(output.blob, value.value());
        }
        if (normals) for (std::uint32_t axis = 0; axis < 3; ++axis) {
            auto value = readFloat(*normals, vertex, axis);
            if (!value) return Result<PrimitiveOutput>::failure(value.status());
            putFloat(output.blob, value.value());
        }
        if (texcoords) for (std::uint32_t axis = 0; axis < 2; ++axis) {
            auto value = readFloat(*texcoords, vertex, axis);
            if (!value) return Result<PrimitiveOutput>::failure(value.status());
            putFloat(output.blob, value.value());
        }
    }
    for (const auto index : indices) put32(output.blob, index);
    return Result<PrimitiveOutput>::success(std::move(output));
}

Value::Array vector3(const float values[3]) {
    return {Value(double(values[0])), Value(double(values[1])), Value(double(values[2]))};
}

}  // namespace

Result<PreparedAssetImport> prepareGltfImport(const GltfImportRequest& request) {
    if (request.sourceName.empty() || request.sourceName.size() > request.limits.maximumStringBytes)
        return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument, "glTF source name is invalid");
    auto document = parseDocument(request);
    if (!document) return Result<PreparedAssetImport>::failure(document.status());
    const auto* root = document.value().root.getIf<Value::Object>();
    if (!root) return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError, "glTF root must be an object");
    const auto* assetValue = member(*root, "asset");
    const auto* asset = assetValue ? assetValue->getIf<Value::Object>() : nullptr;
    const Value* version = asset ? member(*asset, "version") : nullptr;
    if (!version || !version->isString() || !version->asString().starts_with("2."))
        return detail::failure<PreparedAssetImport>(DiagnosticCode::UnknownVersion, "only glTF 2.x is supported",
                                                    "$.asset.version");
    auto buffers = resolveBuffers(*root, document.value(), request);
    if (!buffers) return Result<PreparedAssetImport>::failure(buffers.status());
    const Value* meshesValue = member(*root, "meshes");
    const auto* meshes = meshesValue ? meshesValue->getIf<Value::Array>() : nullptr;
    if (!meshes || meshes->empty())
        return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError, "glTF contains no meshes", "$.meshes");
    auto manifestResult = detail::baseManifest(request.package, "eve.gltf2");
    if (!manifestResult) return Result<PreparedAssetImport>::failure(manifestResult.status());
    PreparedAssetImport result;
    result.manifest = std::move(manifestResult).takeValue();
    std::uint32_t assetCount = 0;
    std::uint64_t decodedTotal = 0;
    for (std::size_t meshIndex = 0; meshIndex < meshes->size(); ++meshIndex) {
        const auto* mesh = (*meshes)[meshIndex].getIf<Value::Object>();
        const Value* primitivesValue = mesh ? member(*mesh, "primitives") : nullptr;
        const auto* primitives = primitivesValue ? primitivesValue->getIf<Value::Array>() : nullptr;
        if (!primitives || primitives->empty())
            return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError, "glTF mesh has no primitives");
        for (std::size_t primitiveIndex = 0; primitiveIndex < primitives->size(); ++primitiveIndex) {
            if (++assetCount > request.limits.maximumAssets)
                return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument,
                                                            "glTF primitive asset count exceeds limits");
            const auto* primitive = (*primitives)[primitiveIndex].getIf<Value::Object>();
            if (!primitive)
                return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError, "glTF primitive is malformed");
            auto decoded = decodePrimitive(*root, *primitive, buffers.value(), request.limits);
            if (!decoded) return Result<PreparedAssetImport>::failure(decoded.status());
            if (decoded.value().blob.size() > request.limits.maximumDecodedBytes ||
                decodedTotal > request.limits.maximumDecodedBytes - decoded.value().blob.size())
                return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument,
                                                            "total canonical mesh budget is exceeded");
            decodedTotal += decoded.value().blob.size();
            const PersistentId id = request.package.packageId.child(
                "gltf:mesh:" + std::to_string(meshIndex) + ":primitive:" + std::to_string(primitiveIndex));
            auto reference = detail::assetRef(id);
            if (!reference) return Result<PreparedAssetImport>::failure(reference.status());
            const std::string base = "assets/" + id.format() + "/";
            const std::string definitionPath = base + "asset.json";
            const std::string blobPath = base + "mesh.bin";
            Value::Object definition;
            definition["schema"] = Value("eve.mesh"); definition["schemaVersion"] = Value(std::int64_t(1));
            definition["topology"] = Value("triangles");
            definition["vertexCount"] = Value(static_cast<std::int64_t>(decoded.value().vertexCount));
            definition["indexCount"] = Value(static_cast<std::int64_t>(decoded.value().indexCount));
            definition["positions"] = Value(true); definition["normals"] = Value(decoded.value().hasNormals);
            definition["texcoord0"] = Value(decoded.value().hasTexcoords);
            definition["coordinateSystem"] = Value("right-handed-x-right-y-up-minus-z-forward");
            definition["unit"] = Value("meter"); definition["frontFace"] = Value("counter-clockwise");
            definition["boundsMin"] = Value(vector3(decoded.value().minimum));
            definition["boundsMax"] = Value(vector3(decoded.value().maximum));
            definition["blob"] = Value(blobPath);
            auto encoded = Value(std::move(definition)).toJson();
            if (!encoded) return Result<PreparedAssetImport>::failure(encoded.status());
            std::string definitionText = std::move(encoded).takeValue();
            result.manifest.assets.push_back({std::move(reference).takeValue(), "eve.mesh", SchemaVersion(1),
                                              definitionPath,
                                              detail::sha256(std::span<const std::uint8_t>(
                                                  reinterpret_cast<const std::uint8_t*>(definitionText.data()),
                                                  definitionText.size())), {"mesh", "source:gltf2"}});
            if (result.manifest.entrypoints.empty()) {
                auto entrypoint = detail::assetRef(id);
                if (!entrypoint) return Result<PreparedAssetImport>::failure(entrypoint.status());
                result.manifest.entrypoints.emplace("default", std::move(entrypoint).takeValue());
            }
            result.entries.push_back({definitionPath, {definitionText.begin(), definitionText.end()}});
            result.entries.push_back({blobPath, std::move(decoded).takeValue().blob});
            auto mapping = detail::assetRef(id);
            if (!mapping) return Result<PreparedAssetImport>::failure(mapping.status());
            const std::string sourceObject = "meshes[" + std::to_string(meshIndex) + "].primitives[" +
                                             std::to_string(primitiveIndex) + "]";
            result.sourceMappings.push_back({sourceObject, std::move(mapping).takeValue()});
            result.findings.push_back({request.sourceName, sourceObject,
                                       ImportDisposition::Translated,
                                       "triangle primitive converted to canonical EVMESH"});
        }
    }
    result.manifest.provenance["sourceName"] = Value(request.sourceName);
    result.manifest.provenance["sourceHash"] = Value(detail::sha256(request.documentBytes));
    result.manifest.provenance["sourceCoordinateSystem"] = Value("gltf2-right-handed-y-up");
    auto report = detail::finalizeImportReport(result, request.package, "gltf", "2.x");
    if (!report) return Result<PreparedAssetImport>::failure(report.status());
    return Result<PreparedAssetImport>::success(std::move(result));
}

}  // namespace eve::asset_import
