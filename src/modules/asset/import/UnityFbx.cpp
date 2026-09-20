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

#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
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
        const auto  token = match[1].str();
        const auto  value = std::stod(token, &end);
        if (end != token.size() || !std::isfinite(value)) return {};
        return value;
    } catch (...) {
        return {};
    }
}

std::set<std::int64_t> referencedMeshIds(const UnityProjectImportRequest& request,
                                         std::string_view sourceGuid) {
    std::set<std::int64_t> result;
    const std::regex reference("m_Mesh: *\\{fileID: *(-?[0-9]+), *guid: *" +
                               std::string(sourceGuid) + R"(, *type: *[23]\})",
                               std::regex::icase);
    for (const auto& [path, bytes] : request.files) {
        if (unity_detail::foldAscii(std::filesystem::path(path).extension().string()) != ".prefab" || bytes.empty())
            continue;
        const std::string text(bytes.begin(), bytes.end());
        for (std::sregex_iterator match(text.begin(), text.end(), reference), end; match != end; ++match) {
            try {
                std::size_t parsed = 0;
                const auto id = std::stoll((*match)[1].str(), &parsed);
                if (parsed == static_cast<std::size_t>((*match)[1].length()) && id != 0) result.emplace(id);
            } catch (...) {
            }
        }
    }
    return result;
}

void put32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back(std::uint8_t(value >> shift));
}

Result<PreparedAssetImport> meshImport(const UnityProjectImportRequest& request, const UnitySourceAsset& source,
                                       const aiMesh& mesh, std::int64_t fileId, std::optional<unsigned> submesh,
                                       float scale) {
    if (!mesh.HasPositions() || !mesh.HasNormals() || !mesh.HasTextureCoords(0) || mesh.HasBones() ||
        mesh.mNumAnimMeshes)
        return detail::failure<PreparedAssetImport>(
            DiagnosticCode::Unsupported,
            "FBX static mesh requires positions, imported normals and UV0; skin/morph conversion is unavailable",
            source.path);
    std::uint64_t vertexFloats = 6;
    for (unsigned set = 0; set < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++set)
        if (mesh.HasTextureCoords(set)) vertexFloats += 2;
    if (mesh.HasTangentsAndBitangents()) vertexFloats += 4;
    for (unsigned set = 0; set < AI_MAX_NUMBER_OF_COLOR_SETS; ++set)
        if (mesh.HasVertexColors(set)) vertexFloats += 4;
    if (mesh.mNumVertices > request.limits.maximumVerticesPerPrimitive ||
        std::uint64_t(mesh.mNumFaces) * 3 > request.limits.maximumIndicesPerPrimitive ||
        vertexFloats > request.limits.maximumDecodedBytes / 4 / mesh.mNumVertices ||
        std::uint64_t(mesh.mNumVertices) * vertexFloats * 4 + std::uint64_t(mesh.mNumFaces) * 12 >
            request.limits.maximumDecodedBytes)
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
    stream(normalOffset, buffer.size() - normalOffset, mesh.mNumVertices, "VEC3", 5126);
    Value::Object attributes{{"POSITION", Value(std::int64_t(0))}, {"NORMAL", Value(std::int64_t(1))}};
    for (unsigned set = 0; set < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++set) {
        if (!mesh.HasTextureCoords(set)) continue;
        const auto offset = buffer.size();
        for (unsigned i = 0; i < mesh.mNumVertices; ++i) {
            // Canonical image rows/glTF UV origin are top-left; Unity mesh UV origin is bottom-left.
            scalar(mesh.mTextureCoords[set][i].x);
            scalar(1.f - mesh.mTextureCoords[set][i].y);
        }
        const auto accessor = std::int64_t(accessors.size());
        stream(offset, buffer.size() - offset, mesh.mNumVertices, "VEC2", 5126);
        attributes["TEXCOORD_" + std::to_string(set)] = Value(accessor);
    }
    if (mesh.HasTangentsAndBitangents()) {
        const auto offset = buffer.size();
        for (unsigned i = 0; i < mesh.mNumVertices; ++i) {
            scalar(-mesh.mTangents[i].x);
            scalar(mesh.mTangents[i].y);
            scalar(-mesh.mTangents[i].z);
            const auto& n = mesh.mNormals[i];
            const auto& t = mesh.mTangents[i];
            const auto& b = mesh.mBitangents[i];
            const auto  handedness =
                ((n.y * t.z - n.z * t.y) * b.x + (n.z * t.x - n.x * t.z) * b.y + (n.x * t.y - n.y * t.x) * b.z) < 0.f
                     ? -1.f
                     : 1.f;
            scalar(handedness);
        }
        const auto accessor = std::int64_t(accessors.size());
        stream(offset, buffer.size() - offset, mesh.mNumVertices, "VEC4", 5126);
        attributes["TANGENT"] = Value(accessor);
    }
    for (unsigned set = 0; set < AI_MAX_NUMBER_OF_COLOR_SETS; ++set) {
        if (!mesh.HasVertexColors(set)) continue;
        const auto offset = buffer.size();
        for (unsigned i = 0; i < mesh.mNumVertices; ++i) {
            scalar(mesh.mColors[set][i].r);
            scalar(mesh.mColors[set][i].g);
            scalar(mesh.mColors[set][i].b);
            scalar(mesh.mColors[set][i].a);
        }
        const auto accessor = std::int64_t(accessors.size());
        stream(offset, buffer.size() - offset, mesh.mNumVertices, "VEC4", 5126);
        attributes["COLOR_" + std::to_string(set)] = Value(accessor);
    }
    const auto indexOffset = buffer.size();
    for (unsigned i = 0; i < mesh.mNumFaces; ++i) {
        if (mesh.mFaces[i].mNumIndices != 3)
            return detail::failure<PreparedAssetImport>(DiagnosticCode::Unsupported,
                                                        "FBX contains non-triangle primitives", source.path);
        for (unsigned j = 0; j < 3; ++j) put32(buffer, mesh.mFaces[i].mIndices[j]);
    }
    const auto indexAccessor = std::int64_t(accessors.size());
    stream(indexOffset, buffer.size() - indexOffset, std::uint64_t(mesh.mNumFaces) * 3, "SCALAR", 5125);
    Value primitive(Value::Object{{"attributes", Value(std::move(attributes))}, {"indices", Value(indexAccessor)}});
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
    std::string identityKey = "unity:" + source.guid + ":" + std::to_string(fileId);
    if (submesh) identityKey += ":submesh:" + std::to_string(*submesh);
    identity.packageId = identity.packageId.child(identityKey);
    auto result        = prepareGltfImport({identity,
                                            source.path,
                                            {json.value().begin(), json.value().end()},
                                            {{"mesh.bin", std::move(buffer)}},
                                            request.limits});
    if (!result) return result;
    for (auto& mapping : result.value().sourceMappings)
        mapping.sourceObject = std::to_string(fileId) +
                               (submesh ? "/submesh/" + std::to_string(*submesh) : std::string{});
    return result;
}
}  // namespace

Result<PreparedAssetImport> prepareUnityFbx(const UnityProjectImportRequest& request, const UnitySourceAsset& source) {
    const auto&       input    = request.files.at(source.path);
    const auto&       metadata = request.files.at(source.path + ".meta");
    const std::string meta(metadata.begin(), metadata.end());
    const auto        scale = setting(meta, "globalScale");
    const auto generationSetting = setting(meta, "fileIdsGeneration");
    const auto generation = generationSetting.value_or(1.0);
    if ((generation != 1 && generation != 2) || !scale || *scale <= 0 || setting(meta, "useFileScale") != 1 ||
        setting(meta, "normalImportMode") != 0 || setting(meta, "swapUVChannels") != 0)
        return detail::failure<PreparedAssetImport>(
            DiagnosticCode::Unsupported,
            "FBX requires supported file IDs, positive scale, file units, imported normals and original UV0", source.path);
    if (input.empty() || input.size() > request.limits.maximumSourceBytes)
        return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument, "FBX source exceeds budget",
                                                    source.path);
    Assimp::Importer importer;
    importer.SetIOHandler(new NoExternalIO());
    const auto* scene = importer.ReadFileFromMemory(input.data(), input.size(),
                                                    aiProcess_Triangulate | aiProcess_CalcTangentSpace, "fbx");
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
    const auto referencedIds = generation == 1 ? referencedMeshIds(request, source.guid) : std::set<std::int64_t>{};
    std::uint32_t meshNodeCount = 0;
    std::function<void(const aiNode&)> countMeshNodes = [&](const aiNode& node) {
        if (node.mNumMeshes) ++meshNodeCount;
        for (unsigned index = 0; index < node.mNumChildren; ++index) countMeshNodes(*node.mChildren[index]);
    };
    countMeshNodes(*scene->mRootNode);
    if (generation == 1 && meshNodeCount > 1 &&
        std::any_of(referencedIds.begin(), referencedIds.end(), [](std::int64_t id) {
            return id < 4300000 || id >= 4400000 || ((id - 4300000) & 1) != 0;
        }))
        return detail::failure<PreparedAssetImport>(
            DiagnosticCode::Unsupported,
            "legacy FBX has non-indexed mesh references that cannot be mapped to multiple nodes", source.path);
    std::set<std::string>                                names;
    std::set<std::int64_t>                               ids;
    std::uint32_t                                        nodeCount = 0;
    std::function<Result<void>(const aiNode&, unsigned)> visit     = [&](const aiNode& node,
                                                                     unsigned      depth) -> Result<void> {
        if (++nodeCount > request.limits.maximumAssets || depth > 256)
            return detail::failure<void>(DiagnosticCode::InvalidArgument, "FBX hierarchy exceeds budget", source.path);
        if (node.mNumMeshes) {
            const std::string name(node.mName.C_Str());
            if (name.empty() || !names.insert(name).second)
                return detail::failure<void>(DiagnosticCode::Unsupported,
                                             "FBX requires unique mesh-bearing node names", source.path);
            std::int64_t id = 0;
            if (generation == 2) {
                const std::string key = "Type:Mesh->" + name + "0";
                id = std::bit_cast<std::int64_t>(std::uint64_t(XXH64(key.data(), key.size(), 0)));
            } else if (meshNodeCount == 1 && referencedIds.size() == 1) {
                id = *referencedIds.begin();
            } else {
                id = std::int64_t(4300000) + std::int64_t(node.mMeshes[0]) * 2;
            }
            if (id == 0 || !ids.insert(id).second)
                return detail::failure<void>(DiagnosticCode::Conflict, "FBX subasset identity collision", source.path);
            for (unsigned slot = 0; slot < node.mNumMeshes; ++slot) {
                if (node.mMeshes[slot] >= scene->mNumMeshes)
                    return detail::failure<void>(DiagnosticCode::ParseError, "FBX mesh index is invalid", source.path);
                const auto submesh = node.mNumMeshes > 1 ? std::optional<unsigned>(slot) : std::nullopt;
                auto converted = meshImport(request, source, *scene->mMeshes[node.mMeshes[slot]], id, submesh,
                                            float(factor));
                if (!converted) return Result<void>::failure(converted.status());
                auto& part = converted.value();
                const std::string entrypoint = std::to_string(id) +
                                               (submesh ? "/submesh/" + std::to_string(*submesh) : std::string{});
                out.manifest.entrypoints.emplace(entrypoint, part.manifest.assets.front().asset);
                for (auto& asset : part.manifest.assets) out.manifest.assets.push_back(std::move(asset));
                for (auto& entry : part.entries)
                    if (entry.path.starts_with("assets/")) out.entries.push_back(std::move(entry));
                for (auto& mapping : part.sourceMappings) out.sourceMappings.push_back(std::move(mapping));
            }
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
    out.findings.push_back({source.path, "FBX.vertexStreams", ImportDisposition::Translated,
                            "positions, imported normals, tangents, vertex colors and all available UV sets retained"});
    return Result<PreparedAssetImport>::success(std::move(out));
}
}  // namespace eve::asset_import
#endif
