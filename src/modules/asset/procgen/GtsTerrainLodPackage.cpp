#include "asset/procgen/GtsTerrainLodPackage.h"

#include "asset/CanonicalMesh.h"
#include "asset/import/ImportCommon.h"

#include <algorithm>
#include <limits>

namespace eve::asset_procgen {
namespace {
template<class T> Result<T> fail(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), {}, {}, "asset.procgen.gts-lod"));
}
Value::Array vec3(float x, float y, float z) { return {Value(double(x)), Value(double(y)), Value(double(z))}; }
}

Result<asset_import::PreparedAssetImport> prepareGtsTerrainLodPackage(
    const asset_import::ImportPackageIdentity& package, const procgen::GtsTerrainLodSet& lods,
    const std::string& terrainName, const asset_import::AssetImportLimits& limits) {
    using namespace asset_import;
    auto manifest = asset_import::detail::baseManifest(package, "eve.gts-terrain-lod");
    if (!manifest) return Result<PreparedAssetImport>::failure(manifest.status());
    auto plan = procgen::planGtsTerrainLodAssets(lods, terrainName, "Meshes");
    if (!plan) return Result<PreparedAssetImport>::failure(plan.status());
    if (plan.value().empty()) return fail<PreparedAssetImport>(DiagnosticCode::InvalidArgument, "GTS LOD set has no meshes");
    if (plan.value().size() > limits.maximumAssets)
        return fail<PreparedAssetImport>(DiagnosticCode::InvalidArgument, "GTS LOD asset count exceeds limit");
    PreparedAssetImport result;
    result.manifest = std::move(manifest).takeValue();
    std::uint64_t totalBytes = 0;
    for (const auto& entry : plan.value()) {
        const auto* tile = lods.tileAt(entry.tileIndex);
        if (!tile || entry.levelIndex < 0 || entry.levelIndex >= static_cast<int>(tile->levels.size()))
            return fail<PreparedAssetImport>(DiagnosticCode::InvalidArgument, "GTS LOD plan references a missing mesh");
        const auto& mesh = tile->levels[static_cast<std::size_t>(entry.levelIndex)];
        asset::CanonicalMeshData canonical;
        canonical.positions = mesh.positions(); canonical.normals = mesh.normals(); canonical.indices = mesh.indices();
        if (mesh.hasVertexColors()) canonical.colors = mesh.colors();
        if (!mesh.uvs().empty()) canonical.texcoords.emplace(0, mesh.uvs());
        asset::CanonicalMeshLimits meshLimits;
        meshLimits.maximumVertices = limits.maximumVerticesPerPrimitive;
        meshLimits.maximumIndices = limits.maximumIndicesPerPrimitive;
        meshLimits.maximumDecodedBytes = limits.maximumDecodedBytes;
        auto blob = asset::encodeCanonicalMesh(canonical, meshLimits);
        if (!blob) return Result<PreparedAssetImport>::failure(blob.status());
        if (blob.value().size() > limits.maximumDecodedBytes || totalBytes > limits.maximumDecodedBytes - blob.value().size())
            return fail<PreparedAssetImport>(DiagnosticCode::InvalidArgument, "GTS LOD package exceeds decoded byte limit");
        totalBytes += blob.value().size();
        const auto id = package.packageId.child("gts-terrain:" + std::to_string(entry.tileIndex) + ":lod:" +
                                                std::to_string(entry.levelIndex));
        auto ref = asset_import::detail::assetRef(id);
        if (!ref) return Result<PreparedAssetImport>::failure(ref.status());
        const std::string base = "assets/" + id.format() + "/";
        const std::string definitionPath = base + "asset.json", blobPath = base + "mesh.bin";
        const auto& positions = mesh.positions();
        float minX=positions[0], minY=positions[1], minZ=positions[2], maxX=minX, maxY=minY, maxZ=minZ;
        for (std::size_t i=0;i<positions.size();i+=3) {
            minX=std::min(minX,positions[i]); minY=std::min(minY,positions[i+1]); minZ=std::min(minZ,positions[i+2]);
            maxX=std::max(maxX,positions[i]); maxY=std::max(maxY,positions[i+1]); maxZ=std::max(maxZ,positions[i+2]);
        }
        Value::Array uvSets; if (!mesh.uvs().empty()) uvSets.emplace_back(int64_t(0));
        Value definition(Value::Object{{"blob",blobPath},{"boundsMax",vec3(maxX,maxY,maxZ)},
            {"boundsMin",vec3(minX,minY,minZ)},{"coordinateSystem","right-handed-x-right-y-up-minus-z-forward"},
            {"colors",mesh.hasVertexColors()},{"frontFace","counter-clockwise"},{"indexCount",int64_t(mesh.getIndexCount())},{"normals",true},
            {"schema","eve.mesh"},{"schemaVersion",int64_t(3)},{"topology","triangles"},
            {"texcoordSets",std::move(uvSets)},{"unit","meter"},{"vertexCount",int64_t(mesh.getVertexCount())}});
        auto json = definition.toJson();
        if (!json) return Result<PreparedAssetImport>::failure(json.status());
        std::vector<std::uint8_t> jsonBytes(json.value().begin(),json.value().end());
        result.manifest.assets.push_back({ref.value(),"eve.mesh",SchemaVersion(3),definitionPath,
            asset_import::detail::sha256(jsonBytes),{"mesh","terrain","gts-lod",entry.meshName}});
        result.entries.push_back({definitionPath,std::move(jsonBytes)});
        result.entries.push_back({blobPath,std::move(blob).takeValue()});
        const std::string key="tile-"+std::to_string(entry.tileIndex)+"-lod-"+std::to_string(entry.levelIndex);
        result.manifest.entrypoints.emplace(key,ref.value());
        if (!result.manifest.entrypoints.contains("default")) result.manifest.entrypoints.emplace("default",ref.value());
        auto mapping=asset_import::detail::assetRef(id); if(!mapping) return Result<PreparedAssetImport>::failure(mapping.status());
        result.sourceMappings.push_back({entry.objectName,std::move(mapping).takeValue()});
        result.findings.push_back({terrainName,entry.objectName,ImportDisposition::Translated,
            "GTS tile LOD converted to canonical eve.mesh/3 as "+entry.meshName});
    }
    auto report=asset_import::detail::finalizeImportReport(result,package,"Unity Pcg Pro GTS","4.2.2",
        {{"terrainName",terrainName},{"tileCount",int64_t(lods.getTileCount())},{"lodCount",int64_t(lods.getLevelCount())}});
    if (!report) return Result<PreparedAssetImport>::failure(report.status());
    auto verified=asset::buildEvaArchive(result.manifest,result.entries);
    if (!verified) return Result<PreparedAssetImport>::failure(verified.status());
    return Result<PreparedAssetImport>::success(std::move(result));
}
}