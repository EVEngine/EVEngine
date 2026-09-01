#include "asset_import/TerrainImporter.h"

#include "asset_import/ImportCommon.h"

#include "common/Utf8Validation.h"

#include <bit>
#include <cmath>
#include <limits>
#include <set>

namespace eve::asset_import {
namespace {

void put32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift != 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

void putFloat(std::vector<std::uint8_t>& bytes, float value) { put32(bytes, std::bit_cast<std::uint32_t>(value)); }

void putString(std::vector<std::uint8_t>& bytes, std::string_view value) {
    put32(bytes, static_cast<std::uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

Result<void> addAsset(PreparedAssetImport& output, PersistentId id, std::string type,
                      Value::Object definition, std::vector<std::uint8_t> blob,
                      std::string blobName, std::vector<std::string> tags) {
    auto reference = detail::assetRef(id);
    if (!reference) return Result<void>::failure(reference.status());
    const std::string root = "assets/" + id.format() + "/";
    const std::string definitionPath = root + "asset.json";
    if (!blob.empty()) definition["blob"] = Value(root + blobName);
    auto encoded = Value(std::move(definition)).toJson();
    if (!encoded) return Result<void>::failure(encoded.status());
    std::string text = std::move(encoded).takeValue();
    output.manifest.assets.push_back({std::move(reference).takeValue(), std::move(type), SchemaVersion(1),
                                      definitionPath,
                                      detail::sha256(std::span<const std::uint8_t>(
                                          reinterpret_cast<const std::uint8_t*>(text.data()), text.size())),
                                      std::move(tags)});
    output.entries.push_back({definitionPath, {text.begin(), text.end()}});
    if (!blob.empty()) output.entries.push_back({root + blobName, std::move(blob)});
    return Result<void>::success();
}

}  // namespace

Result<PreparedAssetImport> prepareCanonicalTerrainImport(const ImportPackageIdentity& package,
                                                           const CanonicalTerrainInput& terrain,
                                                           std::string_view importer,
                                                           const AssetImportLimits& limits) {
    if (terrain.width < 2 || terrain.height < 2 || !std::isfinite(terrain.spacingX) ||
        !std::isfinite(terrain.spacingZ) || terrain.spacingX <= 0 || terrain.spacingZ <= 0 ||
        terrain.heightsMeters.size() != std::uint64_t(terrain.width) * terrain.height)
        return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument,
                                                    "terrain dimensions, spacing or height count is invalid");
    const std::uint64_t heightBytes = std::uint64_t(terrain.heightsMeters.size()) * sizeof(float) + 24;
    if (heightBytes > limits.maximumDecodedBytes || terrain.layers.size() > limits.maximumAssets ||
        terrain.scatterRules.size() > limits.maximumAssets || terrain.instances.size() > limits.maximumAssets)
        return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument,
                                                    "terrain import budget is exceeded");
    for (const float height : terrain.heightsMeters)
        if (!std::isfinite(height))
            return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError,
                                                        "terrain height contains a non-finite value");
    auto manifest = detail::baseManifest(package, importer);
    if (!manifest) return Result<PreparedAssetImport>::failure(manifest.status());
    PreparedAssetImport output;
    output.manifest = std::move(manifest).takeValue();
    output.manifest.provenance["sourceTransform"] = Value(terrain.sourceTransform);

    const PersistentId terrainId = package.packageId.child("terrain:root");
    const PersistentId materialId = package.packageId.child("terrain:material");
    const PersistentId pcgId = package.packageId.child("terrain:pcg");
    const PersistentId instancesId = package.packageId.child("terrain:instances");

    std::vector<std::uint8_t> heights;
    heights.insert(heights.end(), {'E', 'V', 'T', 'R', 'N', 0, 1, 0});
    put32(heights, terrain.width); put32(heights, terrain.height);
    putFloat(heights, terrain.spacingX); putFloat(heights, terrain.spacingZ);
    for (float height : terrain.heightsMeters) putFloat(heights, height);
    Value::Object terrainDefinition;
    terrainDefinition["schema"] = Value("eve.terrain");
    terrainDefinition["schemaVersion"] = Value(std::int64_t(1));
    terrainDefinition["width"] = Value(static_cast<std::int64_t>(terrain.width));
    terrainDefinition["height"] = Value(static_cast<std::int64_t>(terrain.height));
    terrainDefinition["spacingX"] = Value(double(terrain.spacingX));
    terrainDefinition["spacingZ"] = Value(double(terrain.spacingZ));
    terrainDefinition["coordinateSystem"] = Value("right-handed-x-right-y-up-minus-z-forward");
    terrainDefinition["heightUnit"] = Value("meter");
    if (!terrain.layers.empty()) terrainDefinition["material"] = Value("asset://" + materialId.format());
    if (!terrain.scatterRules.empty()) terrainDefinition["pcg"] = Value("asset://" + pcgId.format());
    if (!terrain.instances.empty()) terrainDefinition["instances"] = Value("asset://" + instancesId.format());
    auto addedTerrain = addAsset(output, terrainId, "eve.terrain", std::move(terrainDefinition),
                                 std::move(heights), "heightfield.bin", {"terrain"});
    if (!addedTerrain) return Result<PreparedAssetImport>::failure(addedTerrain.status());

    if (!terrain.layers.empty()) {
        Value::Array layers;
        for (const auto& layer : terrain.layers) {
            if (layer.name.empty() || !std::isfinite(layer.tileSizeMeters) || layer.tileSizeMeters <= 0 ||
                (layer.normalConvention != "opengl" && layer.normalConvention != "directx"))
                return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument,
                                                            "terrain layer metadata is invalid", layer.name);
            Value::Object value;
            value["name"] = Value(layer.name); value["diffuseSource"] = Value(layer.diffuseSource);
            value["normalSource"] = Value(layer.normalSource); value["weightSource"] = Value(layer.weightSource);
            value["normalConvention"] = Value(layer.normalConvention);
            value["tileSizeMeters"] = Value(double(layer.tileSizeMeters));
            layers.emplace_back(std::move(value));
        }
        Value::Object definition;
        definition["schema"] = Value("eve.terrain-material");
        definition["schemaVersion"] = Value(std::int64_t(1));
        definition["layers"] = Value(std::move(layers));
        auto added = addAsset(output, materialId, "eve.terrain-material", std::move(definition), {}, {},
                              {"terrain", "material"});
        if (!added) return Result<PreparedAssetImport>::failure(added.status());
    }

    if (!terrain.scatterRules.empty()) {
        Value::Array rules;
        std::set<std::string> ids;
        for (const auto& rule : terrain.scatterRules) {
            if (rule.id.empty() || rule.prototype.empty() || !ids.emplace(rule.id).second ||
                !std::isfinite(rule.densityPerSquareMeter) || rule.densityPerSquareMeter < 0 ||
                !std::isfinite(rule.minimumSlopeRadians) || !std::isfinite(rule.maximumSlopeRadians) ||
                rule.minimumSlopeRadians > rule.maximumSlopeRadians)
                return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument,
                                                            "terrain scatter rule is invalid", rule.id);
            Value::Object value;
            value["id"] = Value(rule.id); value["prototype"] = Value(rule.prototype);
            value["layer"] = Value(rule.layer); value["densityPerSquareMeter"] = Value(double(rule.densityPerSquareMeter));
            value["minimumSlopeRadians"] = Value(double(rule.minimumSlopeRadians));
            value["maximumSlopeRadians"] = Value(double(rule.maximumSlopeRadians));
            value["seed"] = Value(static_cast<std::int64_t>(rule.seed & 0x7fffffffffffffffULL));
            rules.emplace_back(std::move(value));
        }
        Value::Object definition;
        definition["schema"] = Value("eve.pcg-graph"); definition["schemaVersion"] = Value(std::int64_t(1));
        definition["algorithm"] = Value("terrain-layer-scatter-v1"); definition["rules"] = Value(std::move(rules));
        auto added = addAsset(output, pcgId, "eve.pcg-graph", std::move(definition), {}, {}, {"terrain", "pcg"});
        if (!added) return Result<PreparedAssetImport>::failure(added.status());
    }

    if (!terrain.instances.empty()) {
        std::vector<std::uint8_t> instances;
        instances.insert(instances.end(), {'E', 'V', 'I', 'N', 'S', 'T', 0, 1});
        put32(instances, static_cast<std::uint32_t>(terrain.instances.size())); put32(instances, 0);
        for (const auto& instance : terrain.instances) {
            if (instance.prototype.empty() || instance.prototype.size() > limits.maximumStringBytes ||
                !isValidUtf8(instance.prototype, Utf8NullPolicy::Reject))
                return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument,
                                                            "terrain instance prototype is invalid");
            const float rotationLength = std::sqrt(
                instance.rotation[0] * instance.rotation[0] +
                instance.rotation[1] * instance.rotation[1] +
                instance.rotation[2] * instance.rotation[2] +
                instance.rotation[3] * instance.rotation[3]);
            if (!std::isfinite(rotationLength) || rotationLength < 0.999f ||
                rotationLength > 1.001f || instance.scale[0] == 0.f ||
                instance.scale[1] == 0.f || instance.scale[2] == 0.f)
                return detail::failure<PreparedAssetImport>(
                    DiagnosticCode::InvalidArgument,
                    "terrain instance rotation or scale is invalid", instance.prototype);
            putString(instances, instance.prototype);
            for (float value : instance.position) { if (!std::isfinite(value)) return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError, "instance position is non-finite"); putFloat(instances, value); }
            for (float value : instance.rotation) { if (!std::isfinite(value)) return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError, "instance rotation is non-finite"); putFloat(instances, value); }
            for (float value : instance.scale) { if (!std::isfinite(value)) return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError, "instance scale is non-finite"); putFloat(instances, value); }
        }
        if (instances.size() > limits.maximumDecodedBytes)
            return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument,
                                                        "terrain instance blob exceeds budget");
        Value::Object definition;
        definition["schema"] = Value("eve.instance-set"); definition["schemaVersion"] = Value(std::int64_t(1));
        definition["count"] = Value(static_cast<std::int64_t>(terrain.instances.size()));
        definition["partition"] = Value("single-cell");
        auto added = addAsset(output, instancesId, "eve.instance-set", std::move(definition),
                              std::move(instances), "instances.bin", {"terrain", "instances"});
        if (!added) return Result<PreparedAssetImport>::failure(added.status());
    }

    auto terrainRef = detail::assetRef(terrainId);
    if (!terrainRef) return Result<PreparedAssetImport>::failure(terrainRef.status());
    output.manifest.entrypoints.emplace("default", std::move(terrainRef).takeValue());
    auto rootRef = detail::assetRef(terrainId);
    if (!rootRef) return Result<PreparedAssetImport>::failure(rootRef.status());
    if (!terrain.layers.empty()) {
        auto target = detail::assetRef(materialId);
        if (!target) return Result<PreparedAssetImport>::failure(target.status());
        output.manifest.dependencies.push_back({rootRef.value(), std::move(target).takeValue(),
                                                asset::EvaDependencyKind::RuntimeRequired, "material", {},
                                                "eve.terrain-material/1"});
    }
    if (!terrain.scatterRules.empty()) {
        auto target = detail::assetRef(pcgId);
        if (!target) return Result<PreparedAssetImport>::failure(target.status());
        output.manifest.dependencies.push_back({rootRef.value(), std::move(target).takeValue(),
                                                asset::EvaDependencyKind::RuntimeOptional, "pcg", {},
                                                "eve.pcg-graph/1",
                                                {{"behavior", Value("omit-feature")},
                                                 {"observableCode", Value("terrain.pcg-unavailable")}}});
    }
    if (!terrain.instances.empty()) {
        auto target = detail::assetRef(instancesId);
        if (!target) return Result<PreparedAssetImport>::failure(target.status());
        output.manifest.dependencies.push_back({std::move(rootRef).takeValue(), std::move(target).takeValue(),
                                                asset::EvaDependencyKind::RuntimeOptional, "instances", {},
                                                "eve.instance-set/1",
                                                {{"behavior", Value("omit-feature")},
                                                 {"observableCode", Value("terrain.instances-unavailable")}}});
    }
    return Result<PreparedAssetImport>::success(std::move(output));
}

}  // namespace eve::asset_import
