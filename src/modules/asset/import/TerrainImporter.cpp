#include "asset/import/TerrainImporter.h"

#include "asset/import/ImportCommon.h"

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

Result<void> addAsset(PreparedAssetImport& output, PersistentId id, std::string type, Value::Object definition,
                      std::vector<std::uint8_t> blob, std::string blobName, std::vector<std::string> tags,
                      SchemaVersion schemaVersion = SchemaVersion(1)) {
    auto reference = detail::assetRef(id);
    if (!reference) return Result<void>::failure(reference.status());
    const std::string root           = "assets/" + id.format() + "/";
    const std::string definitionPath = root + "asset.json";
    if (!blob.empty()) definition["blob"] = Value(root + blobName);
    auto encoded = Value(std::move(definition)).toJson();
    if (!encoded) return Result<void>::failure(encoded.status());
    std::string text = std::move(encoded).takeValue();
    output.manifest.assets.push_back(
        {std::move(reference).takeValue(), std::move(type), schemaVersion, definitionPath,
         detail::sha256(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size())),
         std::move(tags)});
    output.entries.push_back({definitionPath, {text.begin(), text.end()}});
    if (!blob.empty()) output.entries.push_back({root + blobName, std::move(blob)});
    return Result<void>::success();
}

}  // namespace

Result<PreparedAssetImport> prepareCanonicalTerrainImport(const ImportPackageIdentity& package,
                                                          const CanonicalTerrainInput& terrain,
                                                          std::string_view importer, const AssetImportLimits& limits) {
    if (terrain.width < 2 || terrain.height < 2 || !std::isfinite(terrain.spacingX) ||
        !std::isfinite(terrain.spacingZ) || terrain.spacingX <= 0 || terrain.spacingZ <= 0 ||
        terrain.heightsMeters.size() != std::uint64_t(terrain.width) * terrain.height)
        return Result<PreparedAssetImport>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain dimensions, spacing or height count is invalid",
                              {}, {}, "asset.import"));
    const std::uint64_t heightBytes = std::uint64_t(terrain.heightsMeters.size()) * sizeof(float) + 24;
    if (heightBytes > limits.maximumDecodedBytes || terrain.layers.size() > limits.maximumAssets ||
        terrain.scatterRules.size() > limits.maximumAssets || terrain.detailPrototypes.size() > limits.maximumAssets ||
        terrain.instances.size() > limits.maximumAssets)
        return Result<PreparedAssetImport>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain import budget is exceeded", {}, {}, "asset.import"));
    for (const float height : terrain.heightsMeters)
        if (!std::isfinite(height))
            return Result<PreparedAssetImport>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "terrain height contains a non-finite value", {}, {}, "asset.import"));
    auto manifest = detail::baseManifest(package, importer);
    if (!manifest) return Result<PreparedAssetImport>::failure(manifest.status());
    PreparedAssetImport output;
    output.manifest                               = std::move(manifest).takeValue();
    output.manifest.provenance["sourceTransform"] = Value(terrain.sourceTransform);

    const PersistentId terrainId   = package.packageId.child("terrain:root");
    const PersistentId materialId  = package.packageId.child("terrain:material");
    const PersistentId pcgId       = package.packageId.child("terrain:pcg");
    const PersistentId instancesId = package.packageId.child("terrain:instances");

    std::vector<std::uint8_t> heights;
    heights.insert(heights.end(), {'E', 'V', 'T', 'R', 'N', 0, 1, 0});
    put32(heights, terrain.width);
    put32(heights, terrain.height);
    putFloat(heights, terrain.spacingX);
    putFloat(heights, terrain.spacingZ);
    for (float height : terrain.heightsMeters) putFloat(heights, height);
    Value::Object terrainDefinition;
    terrainDefinition["schema"]           = Value("eve.terrain");
    terrainDefinition["schemaVersion"]    = Value(std::int64_t(1));
    terrainDefinition["width"]            = Value(static_cast<std::int64_t>(terrain.width));
    terrainDefinition["height"]           = Value(static_cast<std::int64_t>(terrain.height));
    terrainDefinition["spacingX"]         = Value(double(terrain.spacingX));
    terrainDefinition["spacingZ"]         = Value(double(terrain.spacingZ));
    terrainDefinition["coordinateSystem"] = Value("right-handed-x-right-y-up-minus-z-forward");
    terrainDefinition["heightUnit"]       = Value("meter");
    if (!terrain.layers.empty()) terrainDefinition["material"] = Value("asset://" + materialId.format());
    if (!terrain.scatterRules.empty()) terrainDefinition["pcg"] = Value("asset://" + pcgId.format());
    if (!terrain.instances.empty()) terrainDefinition["instances"] = Value("asset://" + instancesId.format());
    auto addedTerrain = addAsset(output, terrainId, "eve.terrain", std::move(terrainDefinition), std::move(heights),
                                 "heightfield.bin", {"terrain"});
    if (!addedTerrain) return Result<PreparedAssetImport>::failure(addedTerrain.status());

    if (!terrain.layers.empty()) {
        Value::Array layers;
        for (const auto& layer : terrain.layers) {
            if (layer.name.empty() || !std::isfinite(layer.tileSizeMeters) || layer.tileSizeMeters <= 0 ||
                (layer.normalConvention != "opengl" && layer.normalConvention != "directx") ||
                !std::isfinite(layer.normalScale) || layer.normalScale < -8 || layer.normalScale > 8 ||
                !std::isfinite(layer.metallic) || layer.metallic < 0 || layer.metallic > 1 ||
                !std::isfinite(layer.smoothness) || layer.smoothness < 0 || layer.smoothness > 1)
                return Result<PreparedAssetImport>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                              "terrain layer metadata is invalid",
                                                                              layer.name, {}, "asset.import"));
            auto vector = [](const auto& source) {
                Value::Array result;
                for (float value : source) {
                    if (!std::isfinite(value)) throw std::invalid_argument("non-finite terrain layer vector");
                    result.emplace_back(double(value));
                }
                return result;
            };
            if (layer.tileScaleMeters[0] <= 0 || layer.tileScaleMeters[1] <= 0)
                return Result<PreparedAssetImport>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "terrain layer scale is invalid", layer.name, {}, "asset.import"));
            for (const auto* image : {&layer.diffuseAsset, &layer.normalAsset, &layer.weightAsset, &layer.maskAsset})
                if (!image->empty() && !AssetRef::parse(*image))
                    return Result<PreparedAssetImport>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                                  "terrain image AssetRef is invalid",
                                                                                  layer.name, {}, "asset.import"));
            Value::Object value;
            value["name"]             = Value(layer.name);
            value["diffuseSource"]    = Value(layer.diffuseSource);
            value["normalSource"]     = Value(layer.normalSource);
            value["weightSource"]     = Value(layer.weightSource);
            value["maskSource"]       = Value(layer.maskSource);
            value["diffuseAsset"]     = Value(layer.diffuseAsset);
            value["normalAsset"]      = Value(layer.normalAsset);
            value["weightAsset"]      = Value(layer.weightAsset);
            value["maskAsset"]        = Value(layer.maskAsset);
            value["normalConvention"] = Value(layer.normalConvention);
            value["tileSizeMeters"]   = Value(double(layer.tileSizeMeters));
            try {
                value["tileScaleMeters"]  = Value(vector(layer.tileScaleMeters));
                value["tileOffsetMeters"] = Value(vector(layer.tileOffsetMeters));
                value["maskRemapMinimum"] = Value(vector(layer.maskRemapMinimum));
                value["maskRemapMaximum"] = Value(vector(layer.maskRemapMaximum));
                value["specular"]         = Value(vector(layer.specular));
            } catch (const std::invalid_argument&) {
                return Result<PreparedAssetImport>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                              "terrain layer vector is non-finite",
                                                                              layer.name, {}, "asset.import"));
            }
            value["metallic"]    = Value(double(layer.metallic));
            value["normalScale"] = Value(double(layer.normalScale));
            value["smoothness"]  = Value(double(layer.smoothness));
            layers.emplace_back(std::move(value));
        }
        Value::Object definition;
        definition["schema"]        = Value("eve.terrain-material");
        definition["schemaVersion"] = Value(std::int64_t(3));
        definition["layers"]        = Value(std::move(layers));
        if (!terrain.holesAsset.empty() && !AssetRef::parse(terrain.holesAsset))
            return Result<PreparedAssetImport>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "terrain holes AssetRef is invalid", {}, {}, "asset.import"));
        definition["holesSource"] = Value(terrain.holesSource);
        definition["holesAsset"]  = Value(terrain.holesAsset);
        Value::Array controls;
        Value::Array controlAssets;
        for (std::size_t index = 0; index < terrain.controlSources.size(); ++index) {
            controls.emplace_back(terrain.controlSources[index]);
            if (!terrain.controlAssets[index].empty() && !AssetRef::parse(terrain.controlAssets[index]))
                return Result<PreparedAssetImport>::failure(
                    Diagnostic::error(DiagnosticCode::ParseError, "terrain control AssetRef is invalid",
                                      std::to_string(index), {}, "asset.import"));
            controlAssets.emplace_back(terrain.controlAssets[index]);
        }
        definition["controlSources"] = Value(std::move(controls));
        definition["controlAssets"]  = Value(std::move(controlAssets));
        if (!std::isfinite(terrain.boundsMultiplier) || terrain.boundsMultiplier <= 0)
            return Result<PreparedAssetImport>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "terrain bounds multiplier is invalid", {}, {}, "asset.import"));
        definition["boundsMultiplier"] = Value(double(terrain.boundsMultiplier));
        auto added = addAsset(output, materialId, "eve.terrain-material", std::move(definition), {}, {},
                              {"terrain", "material"}, SchemaVersion(3));
        if (!added) return Result<PreparedAssetImport>::failure(added.status());
    }

    if (!terrain.scatterRules.empty()) {
        Value::Array          rules;
        std::set<std::string> ids;
        for (const auto& rule : terrain.scatterRules) {
            if (rule.id.empty() || rule.prototype.empty() || !ids.emplace(rule.id).second ||
                !std::isfinite(rule.densityPerSquareMeter) || rule.densityPerSquareMeter < 0 ||
                !std::isfinite(rule.minimumSlopeRadians) || !std::isfinite(rule.maximumSlopeRadians) ||
                rule.minimumSlopeRadians > rule.maximumSlopeRadians)
                return Result<PreparedAssetImport>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "terrain scatter rule is invalid", rule.id, {}, "asset.import"));
            Value::Object value;
            value["id"]                    = Value(rule.id);
            value["prototype"]             = Value(rule.prototype);
            value["layer"]                 = Value(rule.layer);
            value["densityPerSquareMeter"] = Value(double(rule.densityPerSquareMeter));
            value["minimumSlopeRadians"]   = Value(double(rule.minimumSlopeRadians));
            value["maximumSlopeRadians"]   = Value(double(rule.maximumSlopeRadians));
            value["seed"]                  = Value(static_cast<std::int64_t>(rule.seed & 0x7fffffffffffffffULL));
            rules.emplace_back(std::move(value));
        }
        Value::Object definition;
        definition["schema"]        = Value("eve.pcg-graph");
        definition["schemaVersion"] = Value(std::int64_t(1));
        definition["algorithm"]     = Value("terrain-layer-scatter-v1");
        definition["rules"]         = Value(std::move(rules));
        auto added = addAsset(output, pcgId, "eve.pcg-graph", std::move(definition), {}, {}, {"terrain", "pcg"});
        if (!added) return Result<PreparedAssetImport>::failure(added.status());
    }

    if (!terrain.instances.empty()) {
        std::vector<std::uint8_t> instances;
        instances.insert(instances.end(), {'E', 'V', 'I', 'N', 'S', 'T', 0, 1});
        put32(instances, static_cast<std::uint32_t>(terrain.instances.size()));
        put32(instances, 0);
        for (const auto& instance : terrain.instances) {
            if (instance.prototype.empty() || instance.prototype.size() > limits.maximumStringBytes ||
                !isValidUtf8(instance.prototype, Utf8NullPolicy::Reject))
                return Result<PreparedAssetImport>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "terrain instance prototype is invalid", {}, {}, "asset.import"));
            const float rotationLength =
                std::sqrt(instance.rotation[0] * instance.rotation[0] + instance.rotation[1] * instance.rotation[1] +
                          instance.rotation[2] * instance.rotation[2] + instance.rotation[3] * instance.rotation[3]);
            if (!std::isfinite(rotationLength) || rotationLength < 0.999f || rotationLength > 1.001f ||
                instance.scale[0] == 0.f || instance.scale[1] == 0.f || instance.scale[2] == 0.f)
                return Result<PreparedAssetImport>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain instance rotation or scale is invalid",
                                      instance.prototype, {}, "asset.import"));
            putString(instances, instance.prototype);
            for (float value : instance.position) {
                if (!std::isfinite(value))
                    return Result<PreparedAssetImport>::failure(Diagnostic::error(
                        DiagnosticCode::ParseError, "instance position is non-finite", {}, {}, "asset.import"));
                putFloat(instances, value);
            }
            for (float value : instance.rotation) {
                if (!std::isfinite(value))
                    return Result<PreparedAssetImport>::failure(Diagnostic::error(
                        DiagnosticCode::ParseError, "instance rotation is non-finite", {}, {}, "asset.import"));
                putFloat(instances, value);
            }
            for (float value : instance.scale) {
                if (!std::isfinite(value))
                    return Result<PreparedAssetImport>::failure(Diagnostic::error(
                        DiagnosticCode::ParseError, "instance scale is non-finite", {}, {}, "asset.import"));
                putFloat(instances, value);
            }
        }
        if (instances.size() > limits.maximumDecodedBytes)
            return Result<PreparedAssetImport>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "terrain instance blob exceeds budget", {}, {}, "asset.import"));
        Value::Object definition;
        definition["schema"]        = Value("eve.instance-set");
        definition["schemaVersion"] = Value(std::int64_t(5));
        definition["count"]         = Value(static_cast<std::int64_t>(terrain.instances.size()));
        definition["partition"]     = Value("single-cell");
        const bool validWavingGrass = std::isfinite(terrain.wavingGrassAmount) && terrain.wavingGrassAmount >= 0.f &&
                                      std::isfinite(terrain.wavingGrassSpeed) && terrain.wavingGrassSpeed >= 0.f &&
                                      std::isfinite(terrain.wavingGrassStrength) && terrain.wavingGrassStrength >= 0.f &&
                                      std::all_of(terrain.wavingGrassTint.begin(), terrain.wavingGrassTint.end(),
                                                  [](float value) { return std::isfinite(value) && value >= 0.f && value <= 1.f; });
        if (!validWavingGrass)
            return Result<PreparedAssetImport>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "terrain waving grass settings are invalid", {}, {}, "asset.import"));
        definition["wavingGrass"] = Value(Value::Object{
            {"amount", Value(double(terrain.wavingGrassAmount))},
            {"speed", Value(double(terrain.wavingGrassSpeed))},
            {"strength", Value(double(terrain.wavingGrassStrength))},
            {"tint", Value(Value::Array{Value(double(terrain.wavingGrassTint[0])),
                                        Value(double(terrain.wavingGrassTint[1])),
                                        Value(double(terrain.wavingGrassTint[2])),
                                        Value(double(terrain.wavingGrassTint[3]))})}});
        Value::Array          detailPrototypes;
        std::set<std::string> detailPrototypeIds;
        for (const auto& prototype : terrain.detailPrototypes) {
            const bool validMode = prototype.renderMode == "GrassBillboard" || prototype.renderMode == "Grass" ||
                                   prototype.renderMode == "VertexLit";
            const bool finite    = std::isfinite(prototype.minWidth) && std::isfinite(prototype.maxWidth) &&
                                   std::isfinite(prototype.minHeight) && std::isfinite(prototype.maxHeight) &&
                                   std::isfinite(prototype.noiseSpread) && std::isfinite(prototype.density) &&
                                   std::isfinite(prototype.alignToGround) && std::isfinite(prototype.positionJitter) &&
                                   std::isfinite(prototype.bendFactor) && std::isfinite(prototype.holeEdgePadding) &&
                                   std::all_of(prototype.healthyColor.begin(), prototype.healthyColor.end(),
                                               [](float value) { return std::isfinite(value) && value >= 0.f && value <= 1.f; }) &&
                                   std::all_of(prototype.dryColor.begin(), prototype.dryColor.end(),
                                               [](float value) { return std::isfinite(value) && value >= 0.f && value <= 1.f; });
            if (prototype.prototype.empty() || prototype.prototype.size() > limits.maximumStringBytes ||
                !isValidUtf8(prototype.prototype, Utf8NullPolicy::Reject) ||
                !detailPrototypeIds.emplace(prototype.prototype).second || !validMode || !finite ||
                prototype.minWidth <= 0 || prototype.maxWidth < prototype.minWidth || prototype.minHeight <= 0 ||
                prototype.maxHeight < prototype.minHeight || prototype.noiseSpread < 0 || prototype.density < 0 ||
                prototype.alignToGround < 0 || prototype.alignToGround > 1 || prototype.positionJitter < 0 ||
                prototype.positionJitter > 1 || prototype.bendFactor < 0 || prototype.bendFactor > 1 ||
                prototype.holeEdgePadding < 0 || prototype.holeEdgePadding > 1 || prototype.resourceAsset.empty() ||
                !AssetRef::parse(prototype.resourceAsset) ||
                (prototype.usePrototypeMesh != prototype.prototype.starts_with("unity-guid:")))
                return Result<PreparedAssetImport>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                              "terrain detail prototype is invalid",
                                                                              prototype.prototype, {}, "asset.import"));
            detailPrototypes.emplace_back(Value::Object{{"prototype", Value(prototype.prototype)},
                                                        {"renderMode", Value(prototype.renderMode)},
                                                        {"usePrototypeMesh", Value(prototype.usePrototypeMesh)},
                                                        {"useInstancing", Value(prototype.useInstancing)},
                                                        {"minWidth", Value(double(prototype.minWidth))},
                                                        {"maxWidth", Value(double(prototype.maxWidth))},
                                                        {"minHeight", Value(double(prototype.minHeight))},
                                                        {"maxHeight", Value(double(prototype.maxHeight))},
                                                        {"noiseSeed", Value(prototype.noiseSeed)},
                                                        {"noiseSpread", Value(double(prototype.noiseSpread))},
                                                        {"density", Value(double(prototype.density))},
                                                        {"alignToGround", Value(double(prototype.alignToGround))},
                                                        {"positionJitter", Value(double(prototype.positionJitter))},
                                                        {"healthyColor", Value(Value::Array{Value(double(prototype.healthyColor[0])), Value(double(prototype.healthyColor[1])), Value(double(prototype.healthyColor[2])), Value(double(prototype.healthyColor[3]))})},
                                                        {"dryColor", Value(Value::Array{Value(double(prototype.dryColor[0])), Value(double(prototype.dryColor[1])), Value(double(prototype.dryColor[2])), Value(double(prototype.dryColor[3]))})},
                                                        {"bendFactor", Value(double(prototype.bendFactor))},
                                                        {"holeEdgePadding", Value(double(prototype.holeEdgePadding))},
                                                        {"useDensityScaling", Value(prototype.useDensityScaling)},
                                                        {"resourceAsset", Value(prototype.resourceAsset)}});
        }
        for (const auto& instance : terrain.instances)
            if ((instance.prototype.starts_with("unity-texture-guid:") ||
                 (instance.prototype.starts_with("unity-guid:") && !terrain.detailPrototypes.empty())) &&
                !detailPrototypeIds.contains(instance.prototype))
                return Result<PreparedAssetImport>::failure(
                    Diagnostic::error(DiagnosticCode::NotFound, "terrain detail instance prototype is undeclared",
                                      instance.prototype, {}, "asset.import"));
        definition["prototypes"] = Value(std::move(detailPrototypes));
        auto added = addAsset(output, instancesId, "eve.instance-set", std::move(definition), std::move(instances),
                              "instances.bin", {"terrain", "instances"}, SchemaVersion(5));
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
        output.manifest.dependencies.push_back({rootRef.value(),
                                                std::move(target).takeValue(),
                                                asset::EvaDependencyKind::RuntimeRequired,
                                                "material",
                                                {},
                                                "eve.terrain-material/3"});
    }
    if (!terrain.scatterRules.empty()) {
        auto target = detail::assetRef(pcgId);
        if (!target) return Result<PreparedAssetImport>::failure(target.status());
        output.manifest.dependencies.push_back(
            {rootRef.value(),
             std::move(target).takeValue(),
             asset::EvaDependencyKind::RuntimeOptional,
             "pcg",
             {},
             "eve.pcg-graph/1",
             {{"behavior", Value("omit-feature")}, {"observableCode", Value("terrain.pcg-unavailable")}}});
    }
    if (!terrain.instances.empty()) {
        auto target = detail::assetRef(instancesId);
        if (!target) return Result<PreparedAssetImport>::failure(target.status());
        output.manifest.dependencies.push_back(
            {std::move(rootRef).takeValue(),
             std::move(target).takeValue(),
             asset::EvaDependencyKind::RuntimeOptional,
             "instances",
             {},
             "eve.instance-set/5",
             {{"behavior", Value("omit-feature")}, {"observableCode", Value("terrain.instances-unavailable")}}});
    }
    return Result<PreparedAssetImport>::success(std::move(output));
}

}  // namespace eve::asset_import
