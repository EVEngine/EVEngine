#include "asset/import/AssetImporter.h"

#include "asset/import/GltfAnimation.h"
#include "asset/import/GltfDecode.h"
#include "asset/import/GltfMaterials.h"
#include "asset/import/ImportCommon.h"

#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>

namespace eve::asset_import {
namespace {

using namespace gltf;

struct PrimitiveOutput {
    std::vector<std::uint8_t> blob;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
    bool hasNormals = false;
    std::vector<uint32_t>     texcoordSets;
    float minimum[3] = {};
    float maximum[3] = {};
};

Result<float> readUv(const Accessor& uv, uint32_t vertex, uint32_t axis) {
    if (uv.componentType == 5126) return readFloat(uv, vertex, axis);
    const size_t   offset = uv.offset + size_t(vertex) * uv.stride + axis * componentSize(uv.componentType);
    const uint32_t value  = uv.componentType == 5121
                                ? uv.buffer[offset]
                                : uint32_t(uv.buffer[offset]) | (uint32_t(uv.buffer[offset + 1]) << 8);
    return Result<float>::success(float(value) / (uv.componentType == 5121 ? 255.f : 65535.f));
}

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
    if (positions.value().count == 0 || positions.value().components != 3 || positions.value().componentType != 5126 ||
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
    std::map<uint32_t, Accessor> texcoords;
    for (const auto& [name, texcoord] : *attributes) {
        if (!name.starts_with("TEXCOORD_")) continue;
        const std::string_view suffix(name.data() + 9, name.size() - 9);
        uint32_t               set    = 0;
        const auto             parsed = std::from_chars(suffix.data(), suffix.data() + suffix.size(), set);
        if (parsed.ec != std::errc{} || parsed.ptr != suffix.data() + suffix.size() || std::to_string(set) != suffix)
            return detail::failure<PrimitiveOutput>(DiagnosticCode::ParseError, "invalid UV set semantic");
        auto texcoordIndex = unsignedValue(&texcoord, "primitive.attributes." + name);
        if (!texcoordIndex) return Result<PrimitiveOutput>::failure(texcoordIndex.status());
        auto decoded = accessorAt(root, buffers, texcoordIndex.value(), limits);
        if (!decoded) return Result<PrimitiveOutput>::failure(decoded.status());
        const auto& uv = decoded.value();
        if (uv.components != 2 || uv.count != positions.value().count ||
            !((uv.componentType == 5126 && !uv.normalized) ||
              ((uv.componentType == 5121 || uv.componentType == 5123) && uv.normalized)))
            return detail::failure<PrimitiveOutput>(
                DiagnosticCode::Unsupported, "UV sets must match POSITION and use FLOAT or normalized unsigned VEC2");
        texcoords.emplace(set, std::move(decoded).takeValue());
    }
    std::vector<std::uint32_t> indices;
    if (const Value* indicesValue = member(primitive, "indices")) {
        auto index = unsignedValue(indicesValue, "primitive.indices");
        if (!index) return Result<PrimitiveOutput>::failure(index.status());
        auto accessor = accessorAt(root, buffers, index.value(), limits);
        if (!accessor) return Result<PrimitiveOutput>::failure(accessor.status());
        if (accessor.value().count == 0 || accessor.value().count > limits.maximumIndicesPerPrimitive ||
            accessor.value().count % 3 != 0)
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
    const std::uint64_t decodedSize =
        24 + uint64_t(texcoords.size()) * 4 +
        std::uint64_t(positions.value().count) * (12 + (normals ? 12 : 0) + uint64_t(texcoords.size()) * 8) +
        std::uint64_t(indices.size()) * 4;
    if (decodedSize > limits.maximumDecodedBytes || decodedSize > std::numeric_limits<std::size_t>::max())
        return detail::failure<PrimitiveOutput>(DiagnosticCode::InvalidArgument, "canonical mesh exceeds decoded budget");
    PrimitiveOutput output;
    output.vertexCount = positions.value().count;
    output.indexCount = static_cast<std::uint32_t>(indices.size());
    output.hasNormals = normals.has_value();
    for (const auto& [set, uv] : texcoords) output.texcoordSets.push_back(set);
    output.blob.insert(output.blob.end(), {'E', 'V', 'M', 'E', 'S', 'H', 0, 2});
    put32(output.blob, output.vertexCount); put32(output.blob, output.indexCount);
    put32(output.blob, output.hasNormals ? 1u : 0u);
    put32(output.blob, uint32_t(texcoords.size()));
    for (auto set : output.texcoordSets) put32(output.blob, set);
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
        for (const auto& [set, uv] : texcoords)
            for (std::uint32_t axis = 0; axis < 2; ++axis) {
                auto value = readUv(uv, vertex, axis);
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
    if (const auto* required = member(*root, "extensionsRequired")) {
        const auto* extensions = required->getIf<Value::Array>();
        if (!extensions)
            return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError,
                                                        "extensionsRequired must be an array");
        for (const auto& extension : *extensions)
            if (!extension.isString() || !gltf::supportsMaterialExtension(extension.asString()))
                return detail::failure<PreparedAssetImport>(
                    DiagnosticCode::Unsupported, "required glTF extension is not supported", "extensionsRequired");
    }
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
    result.manifest.provenance["importerVersion"] = Value(std::int64_t(2));
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
            if (member(*primitive, "targets"))
                return detail::failure<PreparedAssetImport>(DiagnosticCode::Unsupported,
                                                            "morph targets cannot be represented by the canonical mesh",
                                                            "meshes.primitives.targets");
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
            definition["schema"]        = Value("eve.mesh");
            definition["schemaVersion"] = Value(std::int64_t(2));
            definition["topology"] = Value("triangles");
            definition["vertexCount"] = Value(static_cast<std::int64_t>(decoded.value().vertexCount));
            definition["indexCount"] = Value(static_cast<std::int64_t>(decoded.value().indexCount));
            definition["positions"] = Value(true); definition["normals"] = Value(decoded.value().hasNormals);
            Value::Array uvSets;
            for (auto set : decoded.value().texcoordSets) uvSets.emplace_back(int64_t(set));
            definition["texcoordSets"]     = Value(std::move(uvSets));
            definition["coordinateSystem"] = Value("right-handed-x-right-y-up-minus-z-forward");
            definition["unit"] = Value("meter"); definition["frontFace"] = Value("counter-clockwise");
            definition["boundsMin"] = Value(vector3(decoded.value().minimum));
            definition["boundsMax"] = Value(vector3(decoded.value().maximum));
            definition["blob"] = Value(blobPath);
            auto encoded = Value(std::move(definition)).toJson();
            if (!encoded) return Result<PreparedAssetImport>::failure(encoded.status());
            std::string definitionText = std::move(encoded).takeValue();
            result.manifest.assets.push_back(
                {std::move(reference).takeValue(),
                 "eve.mesh",
                 SchemaVersion(2),
                 definitionPath,
                 detail::sha256(std::span<const std::uint8_t>(
                     reinterpret_cast<const std::uint8_t*>(definitionText.data()), definitionText.size())),
                 {"mesh", "source:gltf2"}});
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
    auto animation = gltf::appendAnimation(request, *root, buffers.value(), result);
    if (!animation) return Result<PreparedAssetImport>::failure(animation.status());
    auto materials = gltf::appendMaterials(request, *root, buffers.value(), result);
    if (!materials) return Result<PreparedAssetImport>::failure(materials.status());
    if (const auto* used = member(*root, "extensionsUsed")) {
        const auto* extensions = used->getIf<Value::Array>();
        if (!extensions)
            return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError, "extensionsUsed must be an array");
        for (const auto& extension : *extensions) {
            if (!extension.isString())
                return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError,
                                                            "extension name must be a string");
            const bool translated = gltf::supportsMaterialExtension(extension.asString());
            result.findings.push_back({request.sourceName, extension.asString(),
                                       translated ? ImportDisposition::Translated : ImportDisposition::Unsupported,
                                       translated ? "material extension parameters and texture bindings translated"
                                                  : "unknown optional extension"});
        }
    }
    for (std::size_t meshIndex = 0; meshIndex < meshes->size(); ++meshIndex) {
        const auto& mesh       = *(*meshes)[meshIndex].getIf<Value::Object>();
        const auto& primitives = *member(mesh, "primitives")->getIf<Value::Array>();
        for (const auto& value : primitives) {
            const auto& primitive  = *value.getIf<Value::Object>();
            const auto& attributes = *member(primitive, "attributes")->getIf<Value::Object>();
            for (const auto& [key, unused] : attributes) {
                (void)unused;
                if (key == "POSITION" || key == "NORMAL" || key.starts_with("TEXCOORD_")) continue;
                if (key.starts_with("JOINTS_") || key.starts_with("WEIGHTS_")) {
                    if (!member(*root, "skins"))
                        return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError,
                                                                    "joint attributes require a skin binding", key);
                    continue;
                }
                result.findings.push_back({request.sourceName, key, ImportDisposition::Unsupported,
                                           "vertex attribute is not represented by canonical EVMESH/2"});
            }
        }
    }
    result.manifest.provenance["sourceName"] = Value(request.sourceName);
    result.manifest.provenance["sourceHash"] = Value(detail::sha256(request.documentBytes));
    result.manifest.provenance["sourceCoordinateSystem"] = Value("gltf2-right-handed-y-up");
    auto report = detail::finalizeImportReport(result, request.package, "gltf", "2.x",
                                               {{"strict", Value(request.mode == GltfImportMode::Strict)}});
    if (!report) return Result<PreparedAssetImport>::failure(report.status());
    return Result<PreparedAssetImport>::success(std::move(result));
}

}  // namespace eve::asset_import
