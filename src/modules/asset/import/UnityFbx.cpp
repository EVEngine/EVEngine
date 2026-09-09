#include "asset/import/ImportCommon.h"
#include "asset/import/UnityImporter.h"
#include "asset/import/UnitySourceInternal.h"

#if defined(__EMSCRIPTEN__)
namespace eve::asset_import {
Result<PreparedAssetImport> prepareUnityFbx(const UnityProjectImportRequest&, const UnitySourceAsset& source) {
    return detail::failure<PreparedAssetImport>(DiagnosticCode::Unsupported,
                                                "FBX conversion requires the desktop Assimp provider", source.path);
}
}  // namespace eve::asset_import
#else
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <xxHash/xxhash.h>
#include <assimp/IOSystem.hpp>
#include <assimp/Importer.hpp>

#include <bit>
#include <cmath>
#include <functional>
#include <regex>
#include <set>

namespace eve::asset_import {
namespace {
// Third-party IO boundary: the in-memory FBX is the only authorized input.
class NoExternalIO final : public Assimp::IOSystem {
public:
    bool              Exists(const char*) const override { return false; }
    char              getOsSeparator() const override { return '/'; }
    Assimp::IOStream* Open(const char*, const char*) override { return nullptr; }
    void              Close(Assimp::IOStream*) override {}
};

std::optional<double> setting(const std::string& text, const std::string& name) {
    std::smatch match;
    if (!std::regex_search(text, match, std::regex("(?:^|\\n)    " + name + ": *([^\\r\\n]+)"))) return {};
    try {
        std::size_t end   = 0;
        const auto  value = std::stod(match[1].str(), &end);
        if (end != match[1].length() || !std::isfinite(value)) return {};
        return value;
    } catch (...) {
        return {};
    }
}

void put32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back(std::uint8_t(value >> shift));
}

Result<PreparedAssetImport> meshImport(const UnityProjectImportRequest& request, const UnitySourceAsset& source,
                                       const aiMesh& mesh, std::int64_t fileId, float scale) {
    if (!mesh.HasPositions() || !mesh.HasNormals() || !mesh.HasTextureCoords(0) || mesh.HasBones() ||
        mesh.mNumAnimMeshes)
        return detail::failure<PreparedAssetImport>(
            DiagnosticCode::Unsupported,
            "FBX static mesh requires positions, imported normals and UV0; skin/morph conversion is unavailable",
            source.path);
    if (mesh.mNumVertices > request.limits.maximumVerticesPerPrimitive ||
        std::uint64_t(mesh.mNumFaces) * 3 > request.limits.maximumIndicesPerPrimitive ||
        std::uint64_t(mesh.mNumVertices) * 32 + std::uint64_t(mesh.mNumFaces) * 12 > request.limits.maximumDecodedBytes)
        return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument, "FBX mesh exceeds budget",
                                                    source.path);
    std::vector<std::uint8_t> buffer;
    auto                      scalar = [&](float value) { put32(buffer, std::bit_cast<std::uint32_t>(value)); };
    // Unity's FBX importer reflects X. The canonical right-handed basis then reflects Z.
    for (unsigned i = 0; i < mesh.mNumVertices; ++i) {
        scalar(-mesh.mVertices[i].x * scale);
        scalar(mesh.mVertices[i].y * scale);
        scalar(-mesh.mVertices[i].z * scale);
    }
    const auto normalOffset = buffer.size();
    for (unsigned i = 0; i < mesh.mNumVertices; ++i) {
        scalar(-mesh.mNormals[i].x);
        scalar(mesh.mNormals[i].y);
        scalar(-mesh.mNormals[i].z);
    }
    const auto uvOffset = buffer.size();
    for (unsigned i = 0; i < mesh.mNumVertices; ++i) {
        // Canonical image rows/glTF UV origin are top-left; Unity mesh UV origin is bottom-left.
        scalar(mesh.mTextureCoords[0][i].x);
        scalar(1.f - mesh.mTextureCoords[0][i].y);
    }
    const auto indexOffset = buffer.size();
    for (unsigned i = 0; i < mesh.mNumFaces; ++i) {
        if (mesh.mFaces[i].mNumIndices != 3)
            return detail::failure<PreparedAssetImport>(DiagnosticCode::Unsupported,
                                                        "FBX contains non-triangle primitives", source.path);
        for (unsigned j = 0; j < 3; ++j) put32(buffer, mesh.mFaces[i].mIndices[j]);
    }
    Value::Array views, accessors;
    auto stream = [&](std::size_t offset, std::size_t length, std::uint64_t count, const char* type, int component) {
        const auto index = std::int64_t(views.size());
        views.emplace_back(Value::Object{{"buffer", Value(std::int64_t(0))},
                                         {"byteOffset", Value(std::int64_t(offset))},
                                         {"byteLength", Value(std::int64_t(length))}});
        accessors.emplace_back(Value::Object{{"bufferView", Value(index)},
                                             {"componentType", Value(std::int64_t(component))},
                                             {"count", Value(std::int64_t(count))},
                                             {"type", Value(type)}});
    };
    stream(0, normalOffset, mesh.mNumVertices, "VEC3", 5126);
    stream(normalOffset, uvOffset - normalOffset, mesh.mNumVertices, "VEC3", 5126);
    stream(uvOffset, indexOffset - uvOffset, mesh.mNumVertices, "VEC2", 5126);
    stream(indexOffset, buffer.size() - indexOffset, std::uint64_t(mesh.mNumFaces) * 3, "SCALAR", 5125);
    Value primitive(Value::Object{{"attributes", Value(Value::Object{{"POSITION", Value(std::int64_t(0))},
                                                                     {"NORMAL", Value(std::int64_t(1))},
                                                                     {"TEXCOORD_0", Value(std::int64_t(2))}})},
                                  {"indices", Value(std::int64_t(3))}});
    Value document(Value::Object{
        {"asset", Value(Value::Object{{"version", Value("2.0")}})},
        {"buffers", Value(Value::Array{Value(Value::Object{{"uri", Value("mesh.bin")},
                                                           {"byteLength", Value(std::int64_t(buffer.size()))}})})},
        {"bufferViews", Value(std::move(views))},
        {"accessors", Value(std::move(accessors))},
        {"meshes",
         Value(Value::Array{Value(Value::Object{{"primitives", Value(Value::Array{std::move(primitive)})}})})}});
    auto  json = document.toJson();
    if (!json) return Result<PreparedAssetImport>::failure(json.status());
    auto identity      = request.package;
    identity.packageId = identity.packageId.child("unity:" + source.guid + ":" + std::to_string(fileId));
    auto result        = prepareGltfImport({identity,
                                            source.path,
                                            {json.value().begin(), json.value().end()},
                                            {{"mesh.bin", std::move(buffer)}},
                                            request.limits});
    if (!result) return result;
    for (auto& mapping : result.value().sourceMappings) mapping.sourceObject = std::to_string(fileId);
    return result;
}
}  // namespace

Result<PreparedAssetImport> prepareUnityFbx(const UnityProjectImportRequest& request, const UnitySourceAsset& source) {
    const auto&       input    = request.files.at(source.path);
    const auto&       metadata = request.files.at(source.path + ".meta");
    const std::string meta(metadata.begin(), metadata.end());
    const auto        scale = setting(meta, "globalScale");
    if (!std::regex_search(meta, std::regex(R"(internalIDToNameTable:\s*\[\])")) ||
        setting(meta, "fileIdsGeneration") != 2 || !scale || *scale <= 0 || setting(meta, "useFileScale") != 1 ||
        setting(meta, "normalImportMode") != 0 || setting(meta, "swapUVChannels") != 0)
        return detail::failure<PreparedAssetImport>(
            DiagnosticCode::Unsupported,
            "FBX requires hashed file IDs, positive scale, file units, imported normals and original UV0", source.path);
    if (input.empty() || input.size() > request.limits.maximumSourceBytes)
        return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument, "FBX source exceeds budget",
                                                    source.path);
    Assimp::Importer importer;
    importer.SetIOHandler(new NoExternalIO());
    const auto* scene = importer.ReadFileFromMemory(input.data(), input.size(), aiProcess_Triangulate, "fbx");
    if (!scene || !scene->mRootNode)
        return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError, importer.GetErrorString(), source.path);
    if (scene->mNumAnimations || scene->mNumMeshes > request.limits.maximumAssets)
        return detail::failure<PreparedAssetImport>(DiagnosticCode::Unsupported,
                                                    "FBX animation or mesh budget is unsupported", source.path);
    std::uint64_t meshBytes = 0;
    for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
        const auto& mesh  = *scene->mMeshes[i];
        const auto  bytes = std::uint64_t(mesh.mNumVertices) * 32 + std::uint64_t(mesh.mNumFaces) * 12;
        if (bytes > request.limits.maximumDecodedBytes || meshBytes > request.limits.maximumDecodedBytes - bytes)
            return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument,
                                                        "aggregate FBX mesh budget exceeded", source.path);
        meshBytes += bytes;
    }
    double units      = 0;
    float  floatUnits = 0;
    if (scene->mMetaData) {
        if (!scene->mMetaData->Get("UnitScaleFactor", units) && scene->mMetaData->Get("UnitScaleFactor", floatUnits))
            units = floatUnits;
    }
    const double factor = units * 0.01 * *scale;
    if (!std::isfinite(factor) || factor <= 0 || factor > std::numeric_limits<float>::max())
        return detail::failure<PreparedAssetImport>(DiagnosticCode::Unsupported, "FBX file unit scale is unavailable",
                                                    source.path);
    auto manifest = detail::baseManifest(request.package, "eve.unity-fbx/1");
    if (!manifest) return Result<PreparedAssetImport>::failure(manifest.status());
    PreparedAssetImport out;
    out.manifest = std::move(manifest).takeValue();
    std::set<std::string>                                names;
    std::set<std::int64_t>                               ids;
    std::uint32_t                                        nodeCount = 0;
    std::function<Result<void>(const aiNode&, unsigned)> visit     = [&](const aiNode& node,
                                                                     unsigned      depth) -> Result<void> {
        if (++nodeCount > request.limits.maximumAssets || depth > 256)
            return detail::failure<void>(DiagnosticCode::InvalidArgument, "FBX hierarchy exceeds budget", source.path);
        if (node.mNumMeshes) {
            const std::string name(node.mName.C_Str());
            if (node.mNumMeshes != 1 || name.empty() || !names.insert(name).second)
                return detail::failure<void>(DiagnosticCode::Unsupported,
                                                 "FBX requires unique names and one mesh per node", source.path);
            const std::string key = "Type:Mesh->" + name + "0";
            const auto        id  = std::bit_cast<std::int64_t>(std::uint64_t(XXH64(key.data(), key.size(), 0)));
            if (id == 0 || !ids.insert(id).second)
                return detail::failure<void>(DiagnosticCode::Conflict, "FBX subasset identity collision", source.path);
            if (node.mMeshes[0] >= scene->mNumMeshes)
                return detail::failure<void>(DiagnosticCode::ParseError, "FBX mesh index is invalid", source.path);
            auto converted = meshImport(request, source, *scene->mMeshes[node.mMeshes[0]], id, float(factor));
            if (!converted) return Result<void>::failure(converted.status());
            auto& part = converted.value();
            out.manifest.entrypoints.emplace(std::to_string(id), part.manifest.assets.front().asset);
            for (auto& asset : part.manifest.assets) out.manifest.assets.push_back(std::move(asset));
            for (auto& entry : part.entries)
                if (entry.path.starts_with("assets/")) out.entries.push_back(std::move(entry));
            for (auto& mapping : part.sourceMappings) out.sourceMappings.push_back(std::move(mapping));
            out.findings.push_back(
                {source.path, "FBX.mesh:" + std::to_string(id), ImportDisposition::Translated, name});
        }
        for (unsigned i = 0; i < node.mNumChildren; ++i) {
            auto result = visit(*node.mChildren[i], depth + 1);
            if (!result) return result;
        }
        return Result<void>::success();
    };
    auto traversed = visit(*scene->mRootNode, 0);
    if (!traversed) return Result<PreparedAssetImport>::failure(traversed.status());
    if (out.manifest.assets.empty())
        return detail::failure<PreparedAssetImport>(DiagnosticCode::Unsupported, "FBX contains no static meshes",
                                                    source.path);
    out.findings.push_back({source.path, "FBX.additionalStreams", ImportDisposition::Unsupported,
                            "only position, imported normal and UV0 streams are converted; tangents, vertex colors and "
                            "secondary UVs are not applied"});
    return Result<PreparedAssetImport>::success(std::move(out));
}
}  // namespace eve::asset_import
#endif
